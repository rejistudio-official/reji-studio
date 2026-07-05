// src/pipeline/rtmp/happy_eyeballs.zig
//
// third_party/librtmp/rtmp.c'nin beklediği happy_eyeballs_* C API'sinin Zig
// implementasyonu (Faz2/Aşama2.2). OBS'in shared/happy-eyeballs'ı (MIT) libobs
// util'ine bağımlı olduğundan kopyalanamadı — bu dosya sıfırdan yazıldı.
//
// KAPSAM BİLİNÇLİ DAR: Reji tek bir ingest sunucusuna bağlanan istemcidir;
// RFC 8305'in paralel IPv4/IPv6 yarışı gerekmez. getaddrinfo ile çöz, adayları
// SIRAYLA blocking connect ile dene, ilk başarılıda dur. rtmp.c sokete
// SO_RCVTIMEO/SO_SNDTIMEO uygular (yalnızca blocking sokette çalışır) ve hiç
// ioctlsocket(FIONBIO) çağırmaz → soket BLOCKING modda devredilmeli (öyle).
//
// ABI sözleşmesi: compat/happy-eyeballs.h (MIT, Twitch 2023) imzalarıyla bire bir.
// Panik disiplini: export sınırında panik YOK — tüm hatalar dönüş koduyla
// (bkz. SESSION_NOTES Faz2/Aşama2.2 "Zig panic/ABI sınırı").

const std = @import("std");
const builtin = @import("builtin");

const c = @cImport({
    @cInclude("winsock2.h");
    @cInclude("ws2tcpip.h");
});

const allocator = std.heap.c_allocator;

// Zig 0.16'da std.time.Timer kaldırıldı — QPC ile ns ölç (winsock2.h zaten
// windows.h'ı getiriyor, QueryPerformanceCounter cImport'ta mevcut).
fn nowNs() u64 {
    var freq: c.LARGE_INTEGER = undefined;
    var cnt: c.LARGE_INTEGER = undefined;
    if (c.QueryPerformanceFrequency(&freq) == 0) return 0;
    if (c.QueryPerformanceCounter(&cnt) == 0) return 0;
    const f: u64 = @intCast(freq.QuadPart);
    if (f == 0) return 0;
    const q: u64 = @intCast(cnt.QuadPart);
    return @intCast((@as(u128, q) * std.time.ns_per_s) / f);
}

// rtmp.c (Windows dalı): E_INVAL == WSAEINVAL. -E_INVAL "parametre hatası" demek.
const err_inval: c_int = c.WSAEINVAL;

const Ctx = struct {
    socket: c.SOCKET = c.INVALID_SOCKET,
    remote: c.sockaddr_storage = std.mem.zeroes(c.sockaddr_storage),
    remote_len: c_int = 0,
    bind_addr: c.sockaddr_storage = std.mem.zeroes(c.sockaddr_storage),
    bind_len: c.socklen_t = 0,
    has_bind: bool = false,
    err: c_int = 0,
    connected: bool = false,
    name_res_ns: u64 = 0,
    connect_ns: u64 = 0,
};

fn fromOpaque(p: ?*anyopaque) ?*Ctx {
    return @ptrCast(@alignCast(p orelse return null));
}

var wsa_started = std.atomic.Value(bool).init(false);

fn ensureWsa() void {
    if (wsa_started.load(.acquire)) return;
    var data: c.WSADATA = undefined;
    // Birden fazla WSAStartup zararsız (ref sayılır); process ömrü boyunca açık kalır.
    _ = c.WSAStartup((2 << 8) | 2, &data);
    wsa_started.store(true, .release);
}

export fn happy_eyeballs_create(context: ?*?*anyopaque) c_int {
    const out = context orelse return -err_inval;
    out.* = null;
    const ctx = allocator.create(Ctx) catch return -12; // -ENOMEM
    ctx.* = .{};
    out.* = @ptrCast(ctx);
    return 0;
}

export fn happy_eyeballs_set_bind_addr(
    context: ?*anyopaque,
    addr_len: c.socklen_t,
    addr_storage: ?*c.sockaddr_storage,
) c_int {
    const ctx = fromOpaque(context) orelse return -err_inval;
    if (addr_len == 0 or addr_storage == null) {
        ctx.has_bind = false;
        ctx.bind_len = 0;
        return 0;
    }
    ctx.bind_addr = addr_storage.?.*;
    ctx.bind_len = addr_len;
    ctx.has_bind = true;
    return 0;
}

export fn happy_eyeballs_connect(context: ?*anyopaque, hostname: ?[*:0]const u8, port: c_int) c_int {
    const ctx = fromOpaque(context) orelse return -err_inval;
    const host = hostname orelse return -err_inval;
    if (port <= 0 or port > 65535) return -err_inval;

    ensureWsa();

    const t0 = nowNs();

    var hints = std.mem.zeroes(c.addrinfo);
    hints.ai_family = c.AF_UNSPEC;
    hints.ai_socktype = c.SOCK_STREAM;
    hints.ai_protocol = c.IPPROTO_TCP;

    var port_buf: [8]u8 = undefined;
    const port_z = std.fmt.bufPrintZ(&port_buf, "{d}", .{@as(u32, @intCast(port))}) catch
        return -err_inval;

    var res: ?*c.addrinfo = null;
    const gai = c.getaddrinfo(host, port_z.ptr, &hints, &res);
    if (gai != 0 or res == null) {
        // getaddrinfo Windows'ta WSA hata kodu döndürür (örn. WSAHOST_NOT_FOUND).
        ctx.err = if (gai != 0) gai else c.WSAHOST_NOT_FOUND;
        return -1;
    }
    defer c.freeaddrinfo(res);

    ctx.name_res_ns = nowNs() -| t0;

    var ai: ?*c.addrinfo = res;
    while (ai) |a| : (ai = a.ai_next) {
        const s = c.socket(a.ai_family, a.ai_socktype, a.ai_protocol);
        if (s == c.INVALID_SOCKET) {
            ctx.err = c.WSAGetLastError();
            continue;
        }
        if (ctx.has_bind) {
            if (c.bind(s, @ptrCast(&ctx.bind_addr), @intCast(ctx.bind_len)) != 0) {
                ctx.err = c.WSAGetLastError();
                _ = c.closesocket(s);
                continue;
            }
        }
        // Blocking connect — başarıda soket blocking modda kalır (rtmp.c gereksinimi).
        if (c.connect(s, a.ai_addr, @intCast(a.ai_addrlen)) == 0) {
            const copy_len: usize = @min(@as(usize, @intCast(a.ai_addrlen)), @sizeOf(c.sockaddr_storage));
            const src: [*]const u8 = @ptrCast(a.ai_addr.?);
            const dst: [*]u8 = @ptrCast(&ctx.remote);
            @memcpy(dst[0..copy_len], src[0..copy_len]);
            ctx.remote_len = @intCast(a.ai_addrlen);
            ctx.socket = s;
            ctx.connected = true;
            ctx.err = 0;
            ctx.connect_ns = nowNs() -| t0;
            return 0;
        }
        ctx.err = c.WSAGetLastError();
        _ = c.closesocket(s);
    }
    ctx.connect_ns = nowNs() -| t0;
    return -1;
}

export fn happy_eyeballs_get_socket_fd(context: ?*const anyopaque) c.SOCKET {
    const ctx = fromOpaque(@constCast(context)) orelse return c.INVALID_SOCKET;
    if (!ctx.connected) return c.INVALID_SOCKET;
    return ctx.socket;
}

export fn happy_eyeballs_get_remote_addr(context: ?*const anyopaque, addr: ?*c.sockaddr_storage) c_int {
    const ctx = fromOpaque(@constCast(context)) orelse return -err_inval;
    const out = addr orelse return -err_inval;
    if (!ctx.connected) return -err_inval;
    out.* = ctx.remote;
    return ctx.remote_len;
}

export fn happy_eyeballs_get_error_code(context: ?*const anyopaque) c_int {
    const ctx = fromOpaque(@constCast(context)) orelse return -err_inval;
    return ctx.err;
}

export fn happy_eyeballs_get_error_message(context: ?*const anyopaque) ?[*:0]const u8 {
    _ = context;
    return null;
}

export fn happy_eyeballs_get_name_resolution_time_ns(context: ?*const anyopaque) u64 {
    const ctx = fromOpaque(@constCast(context)) orelse return 0;
    return ctx.name_res_ns;
}

export fn happy_eyeballs_get_connection_time_ns(context: ?*const anyopaque) u64 {
    const ctx = fromOpaque(@constCast(context)) orelse return 0;
    return ctx.connect_ns;
}

// Senkron implementasyon: connect zaten blokladı — süreç bittiğinde sonuç kesin.
export fn happy_eyeballs_try(context: ?*anyopaque) c_int {
    const ctx = fromOpaque(context) orelse return -err_inval;
    return if (ctx.connected) 0 else -1;
}

export fn happy_eyeballs_timedwait(context: ?*anyopaque, time_in_millis: c_ulong) c_int {
    _ = time_in_millis;
    const ctx = fromOpaque(context) orelse return -err_inval;
    return if (ctx.connected) 0 else -1;
}

export fn happy_eyeballs_timedwait_default(context: ?*anyopaque) c_int {
    const ctx = fromOpaque(context) orelse return -err_inval;
    return if (ctx.connected) 0 else -1;
}

// Sözleşme gereği soketi KAPATMAZ (sahiplik rtmp.c'ye geçti).
export fn happy_eyeballs_destroy(context: ?*anyopaque) c_int {
    const ctx = fromOpaque(context) orelse return -err_inval;
    allocator.destroy(ctx);
    return 0;
}

// ── Zig birim testleri (zig build rtmp-test) ────────────────────────────────
test "connect yerel dinleyiciye başarılı, soket blocking devrediliyor" {
    // Arrange: yerel bir TCP dinleyici aç (std.net 0.16'da kaldırıldı — ham winsock)
    ensureWsa();
    const srv = c.socket(c.AF_INET, c.SOCK_STREAM, c.IPPROTO_TCP);
    try std.testing.expect(srv != c.INVALID_SOCKET);
    defer _ = c.closesocket(srv);
    var sa = std.mem.zeroes(c.sockaddr_in);
    sa.sin_family = c.AF_INET;
    sa.sin_port = 0; // OS port seçsin
    sa.sin_addr.S_un.S_addr = c.htonl(0x7F000001); // 127.0.0.1
    try std.testing.expectEqual(@as(c_int, 0), c.bind(srv, @ptrCast(&sa), @sizeOf(c.sockaddr_in)));
    try std.testing.expectEqual(@as(c_int, 0), c.listen(srv, 1));
    var bound = std.mem.zeroes(c.sockaddr_in);
    var bound_len: c_int = @sizeOf(c.sockaddr_in);
    try std.testing.expectEqual(@as(c_int, 0), c.getsockname(srv, @ptrCast(&bound), &bound_len));
    const port: c_int = @intCast(c.ntohs(bound.sin_port));

    // Act
    var ctx: ?*anyopaque = null;
    try std.testing.expectEqual(@as(c_int, 0), happy_eyeballs_create(&ctx));
    defer _ = happy_eyeballs_destroy(ctx);
    const rc = happy_eyeballs_connect(ctx, "127.0.0.1", port);

    // Assert
    try std.testing.expectEqual(@as(c_int, 0), rc);
    const s = happy_eyeballs_get_socket_fd(ctx);
    try std.testing.expect(s != c.INVALID_SOCKET);
    try std.testing.expectEqual(@as(c_int, 0), happy_eyeballs_try(ctx));
    var storage: c.sockaddr_storage = undefined;
    try std.testing.expect(happy_eyeballs_get_remote_addr(ctx, &storage) > 0);
    _ = c.closesocket(s); // sözleşme: soketi çağıran kapatır
}

test "connect kapalı porta başarısız, hata kodu dolu" {
    var ctx: ?*anyopaque = null;
    try std.testing.expectEqual(@as(c_int, 0), happy_eyeballs_create(&ctx));
    defer _ = happy_eyeballs_destroy(ctx);
    // Ayrılmış port 1'e bağlantı reddedilir (dinleyici yok).
    const rc = happy_eyeballs_connect(ctx, "127.0.0.1", 1);
    try std.testing.expect(rc != 0);
    try std.testing.expect(happy_eyeballs_get_error_code(ctx) != 0);
    try std.testing.expectEqual(c.INVALID_SOCKET, happy_eyeballs_get_socket_fd(ctx));
}

test "geçersiz parametreler -E_INVAL döner" {
    try std.testing.expectEqual(-err_inval, happy_eyeballs_connect(null, "x", 80));
    var ctx: ?*anyopaque = null;
    _ = happy_eyeballs_create(&ctx);
    defer _ = happy_eyeballs_destroy(ctx);
    try std.testing.expectEqual(-err_inval, happy_eyeballs_connect(ctx, null, 80));
    try std.testing.expectEqual(-err_inval, happy_eyeballs_connect(ctx, "x", 0));
    try std.testing.expectEqual(-err_inval, happy_eyeballs_connect(ctx, "x", 70000));
}
