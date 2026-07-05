// src/pipeline/rtmp/rtmp_transport.zig
//
// RtmpTransport'un Zig çekirdeği (Faz2/Aşama2.2): OBS librtmp (LGPL 2.1,
// third_party/librtmp, NO_CRYPTO — düz rtmp://, TLS kararı A) üzerine
// C ABI (rj_rtmp_*) verir. C++ tarafı: src/pipeline/output/rtmp_transport.cpp.
//
// Veri yolu: NVENC Annex-B H.264 ES (start-code'lu NAL'ler) → NAL ayrıştır →
// SPS/PPS'ten AVCDecoderConfigurationRecord (sequence header, bir kez) →
// kareler AVCC (4B uzunluk önekli) FLV video tag'i → RTMP_Write (FLV tag
// akışını RTMP paketlerine kendisi çevirir, rtmp.c:5318).
//
// Basitleştirmeler (bilinçli, SESSION_NOTES'ta belgeli):
// - Yalnız H.264 video. AAC ses yolu yok (Reji'de AAC encoder yok), HEVC FLV
//   standardında yok (enhanced-RTMP ayrı iş). onMetaData script tag'i
//   gönderilmiyor (Twitch/YouTube zorunlu tutmaz).
// - composition time hep 0: encode_nvenc frameIntervalP=1 (B-frame yok) → dts==pts.
// - Timestamp: ilk paketin pts_us'i t=0 kabul edilir, FLV ms cinsinden göreli.
//
// Panik disiplini (Zig/ABI sınırı): Zig'de catch_unwind YOK; panik süreç
// abort'udur ve C++ tarafına UNWIND ETMEZ (UB değil, ani ölüm). Bu yüzden bu
// dosyada panik yolu bırakılmaz: tüm ayırmalar/aritmetik `catch`/kontrollü,
// export sınırı her hatada false/null döner. Bkz. SESSION_NOTES.
//
// İş parçacığı modeli: send() yalnız encode thread'inden; init/shutdown başka
// thread'den ama OutputSubsystem transport_atomic_ yayını sayesinde send ile
// örtüşmez (SrtOutput ile aynı sözleşme). İç kilit yok.

const std = @import("std");

const rt = @cImport({
    @cDefine("NO_CRYPTO", "1");
    @cInclude("librtmp/rtmp.h");
    @cInclude("librtmp/log.h");
    @cInclude("stdio.h");
    @cInclude("stdlib.h");
});

const allocator = std.heap.c_allocator;

// Zig std API çalkantısından bağımsız, yeniden kullanılabilir büyüyen tampon.
// Kapasite korunur → sıcak yolda (send) ısınma sonrası heap ayırma nadirdir.
const Buf = struct {
    data: []u8 = &.{},
    len: usize = 0,

    fn ensure(self: *Buf, cap: usize) !void {
        if (self.data.len >= cap) return;
        var new_cap: usize = if (self.data.len == 0) 4096 else self.data.len;
        while (new_cap < cap) new_cap *|= 2;
        if (self.data.len == 0) {
            self.data = try allocator.alloc(u8, new_cap);
        } else {
            self.data = try allocator.realloc(self.data, new_cap);
        }
    }
    fn appendSlice(self: *Buf, bytes: []const u8) !void {
        try self.ensure(self.len + bytes.len);
        @memcpy(self.data[self.len..][0..bytes.len], bytes);
        self.len += bytes.len;
    }
    fn appendByte(self: *Buf, b: u8) !void {
        try self.ensure(self.len + 1);
        self.data[self.len] = b;
        self.len += 1;
    }
    fn appendBe24(self: *Buf, v: u32) !void {
        try self.appendByte(@intCast((v >> 16) & 0xFF));
        try self.appendByte(@intCast((v >> 8) & 0xFF));
        try self.appendByte(@intCast(v & 0xFF));
    }
    fn appendBe32(self: *Buf, v: u32) !void {
        try self.appendByte(@intCast((v >> 24) & 0xFF));
        try self.appendBe24(v & 0xFFFFFF);
    }
    fn items(self: *const Buf) []const u8 {
        return self.data[0..self.len];
    }
    fn deinit(self: *Buf) void {
        if (self.data.len > 0) allocator.free(self.data);
        self.* = .{};
    }
};

const no_pts: i64 = std.math.minInt(i64);

const Transport = struct {
    rtmp: ?*rt.RTMP = null,
    url: ?[:0]u8 = null,
    stream_idx: c_int = 0,
    sps: ?[]u8 = null,
    pps: ?[]u8 = null,
    seq_sent: bool = false,
    first_pts_us: i64 = no_pts,
    body: Buf = .{},
    tag: Buf = .{},
};

fn get(handle: ?*anyopaque) ?*Transport {
    return @ptrCast(@alignCast(handle orelse return null));
}

// ── Annex-B ayrıştırma (saf fonksiyonlar — birim testli) ────────────────────

const Nal = struct { start: usize, end: usize };

// `from` konumundan itibaren ilk start code'un BİTİŞİNİ (NAL başlangıcı) döndürür.
fn findStartCode(data: []const u8, from: usize) ?usize {
    if (data.len < 3) return null;
    var i = from;
    while (i + 3 <= data.len) : (i += 1) {
        if (data[i] != 0 or data[i + 1] != 0) continue;
        if (data[i + 2] == 1) return i + 3;
        if (data[i + 2] == 0 and i + 4 <= data.len and data[i + 3] == 1) return i + 4;
    }
    return null;
}

// Sonraki NAL'i döndürür; `from` önceki NAL başlangıcı (veya 0).
fn nextNal(data: []const u8, from: usize) ?Nal {
    const start = findStartCode(data, from) orelse return null;
    // NAL sonu = sonraki start code'un ÖNCESİ (00 00 [00] 01 önekini geri kırp).
    if (findStartCode(data, start)) |next_start| {
        var end = next_start - 3;
        if (end >= 1 and data[end - 1] == 0) end -= 1; // 4 baytlık önekti
        return .{ .start = start, .end = end };
    }
    return .{ .start = start, .end = data.len };
}

// ── AVCDecoderConfigurationRecord (ISO 14496-15 §5.2.4.1) ───────────────────

fn buildAvcConfig(out: *Buf, sps: []const u8, pps: []const u8) !void {
    if (sps.len < 4 or pps.len == 0) return error.BadParameterSet;
    try out.appendByte(1); // configurationVersion
    try out.appendByte(sps[1]); // AVCProfileIndication
    try out.appendByte(sps[2]); // profile_compatibility
    try out.appendByte(sps[3]); // AVCLevelIndication
    try out.appendByte(0xFF); // lengthSizeMinusOne = 3 (4 baytlık NAL uzunlukları)
    try out.appendByte(0xE1); // numOfSequenceParameterSets = 1
    try out.appendByte(@intCast((sps.len >> 8) & 0xFF));
    try out.appendByte(@intCast(sps.len & 0xFF));
    try out.appendSlice(sps);
    try out.appendByte(1); // numOfPictureParameterSets
    try out.appendByte(@intCast((pps.len >> 8) & 0xFF));
    try out.appendByte(@intCast(pps.len & 0xFF));
    try out.appendSlice(pps);
}

// ── FLV tag + RTMP_Write ────────────────────────────────────────────────────

const flv_video_tag: u8 = 0x09;

fn writeFlvTag(t: *Transport, r: *rt.RTMP, tag_type: u8, ts_ms: u32, body: []const u8) bool {
    t.tag.len = 0;
    const ok = blk: {
        t.tag.appendByte(tag_type) catch break :blk false;
        t.tag.appendBe24(@intCast(body.len & 0xFFFFFF)) catch break :blk false;
        t.tag.appendBe24(ts_ms & 0xFFFFFF) catch break :blk false;
        t.tag.appendByte(@intCast((ts_ms >> 24) & 0xFF)) catch break :blk false;
        t.tag.appendBe24(0) catch break :blk false; // StreamID
        t.tag.appendSlice(body) catch break :blk false;
        t.tag.appendBe32(@intCast((11 + body.len) & 0xFFFFFFFF)) catch break :blk false;
        break :blk true;
    };
    if (!ok) return false;
    const n = rt.RTMP_Write(r, @ptrCast(t.tag.data.ptr), @intCast(t.tag.len), t.stream_idx);
    return n > 0;
}

fn updateParamSet(slot: *?[]u8, nal: []const u8, seq_sent: *bool) void {
    if (slot.*) |old| {
        if (std.mem.eql(u8, old, nal)) return; // değişmedi
        allocator.free(old);
        slot.* = null;
        seq_sent.* = false; // parametre seti değişti — sequence header yenilenmeli
    }
    slot.* = allocator.dupe(u8, nal) catch null;
}

// ── C ABI exportları ────────────────────────────────────────────────────────

// Teşhis: REJI_RTMP_LOG=<dosya-yolu> ortam değişkeni set ise librtmp'in kendi
// logunu (RTMP_LOGDEBUG) o dosyaya yönlendir. GUI alt sisteminde stderr
// kaybolduğu için sahada RTMP sorunlarını görünür kılmanın tek yolu bu.
var log_file: ?*rt.FILE = null;

// Teşhis yardımcı — yalnız REJI_RTMP_LOG etkinken yazar (sıcak yolda değil:
// ilk kareler + hata durumları).
fn dlog(comptime fmt: []const u8, args: anytype) void {
    const f = log_file orelse return;
    var buf: [256]u8 = undefined;
    const s = std.fmt.bufPrintZ(&buf, fmt, args) catch return;
    _ = rt.fprintf(f, "REJI: %s\n", s.ptr);
    _ = rt.fflush(f);
}

fn maybeEnableLibrtmpLog() void {
    if (log_file != null) return;
    const path = rt.getenv("REJI_RTMP_LOG") orelse return;
    const f = rt.fopen(path, "a") orelse return;
    log_file = f;
    rt.RTMP_LogSetOutput(f);
    rt.RTMP_LogSetLevel(rt.RTMP_LOGDEBUG);
}

export fn rj_rtmp_create() ?*anyopaque {
    maybeEnableLibrtmpLog();
    const t = allocator.create(Transport) catch return null;
    t.* = .{};
    return @ptrCast(t);
}

// url: SUNUCU URL'i (rtmp://host/app — stream key HARİÇ). OBS librtmp'in
// RTMP_ParseURL'i app = path'in TAMAMI der (parseurl.c:138 "just.. whatever"),
// playpath/stream key ayrı olarak RTMP_AddStream ile verilir (OBS UI'daki
// Server / Stream Key ayrımının birebir karşılığı).
export fn rj_rtmp_init(handle: ?*anyopaque, url: ?[*:0]const u8, stream_key: ?[*:0]const u8) bool {
    const t = get(handle) orelse return false;
    const u = url orelse return false;
    const key = stream_key orelse return false;
    if (t.rtmp != null) return false; // çift init yok

    const r: ?*rt.RTMP = rt.RTMP_Alloc();
    const rp = r orelse return false;
    rt.RTMP_Init(rp);

    // librtmp SetupURL string'i YERİNDE değiştirir ve içine pointer tutar —
    // kopya Transport ömrü boyunca yaşamalı. (AddStream ise playpath'i
    // RTMP_ParsePlaypath ile kendine kopyalar — key için dupe gerekmez.)
    const dup = allocator.dupeZ(u8, std.mem.span(u)) catch {
        rt.RTMP_Free(rp);
        return false;
    };

    if (rt.RTMP_SetupURL(rp, dup.ptr) == 0) {
        allocator.free(dup);
        rt.RTMP_Free(rp);
        return false;
    }
    const idx = rt.RTMP_AddStream(rp, key);
    if (idx < 0) {
        allocator.free(dup);
        rt.RTMP_Free(rp);
        return false;
    }
    rt.RTMP_EnableWrite(rp);

    if (rt.RTMP_Connect(rp, null) == 0) {
        allocator.free(dup);
        rt.RTMP_Free(rp);
        return false;
    }
    // ConnectStream: connect result → createStream → publish akışını yürütür
    // (rtmp.c:3182-3205), AddStream'lenmiş playpath'i kullanır.
    if (rt.RTMP_ConnectStream(rp, 0) == 0) {
        rt.RTMP_Close(rp);
        allocator.free(dup);
        rt.RTMP_Free(rp);
        return false;
    }

    t.rtmp = rp;
    t.url = dup;
    t.stream_idx = idx;
    return true;
}

var send_diag_count: u32 = 0;

export fn rj_rtmp_send(handle: ?*anyopaque, data_ptr: ?[*]const u8, size: usize, pts_us: i64) bool {
    const t = get(handle) orelse return false;
    const r = t.rtmp orelse return false;
    if (rt.RTMP_IsConnected(r) == 0) {
        dlog("send: baglanti yok (size={d})", .{size});
        return false;
    }
    if (size == 0) return true;
    const data = (data_ptr orelse return false)[0..size];

    const diag = send_diag_count < 10;
    if (diag) send_diag_count += 1;

    // NAL'leri ayrıştır: SPS/PPS yakala, kare gövdesini AVCC olarak biriktir.
    t.body.len = 0;
    var is_keyframe = false;
    var have_frame_data = false;
    var pos: usize = 0;
    while (nextNal(data, pos)) |nal| {
        pos = nal.start;
        const bytes = data[nal.start..nal.end];
        if (bytes.len == 0) continue;
        const nal_type: u8 = bytes[0] & 0x1F;
        if (diag) dlog("send#{d}: nal type={d} len={d} (paket size={d})", .{ send_diag_count, nal_type, bytes.len, size });
        switch (nal_type) {
            7 => updateParamSet(&t.sps, bytes, &t.seq_sent),
            8 => updateParamSet(&t.pps, bytes, &t.seq_sent),
            9 => {}, // AUD — FLV'de gereksiz, atla
            else => {
                if (nal_type == 5) is_keyframe = true;
                if (nal_type >= 1 and nal_type <= 5) have_frame_data = true;
                t.body.appendBe32(@intCast(bytes.len & 0xFFFFFFFF)) catch return false;
                t.body.appendSlice(bytes) catch return false;
            },
        }
    }

    // Sequence header (AVC config) — SPS+PPS hazır olunca bir kez, ts=0.
    if (!t.seq_sent) {
        const sps = t.sps orelse {
            if (have_frame_data) dlog("send: SPS yok, kare DROP (nal'ler yukarida)", .{});
            return !have_frame_data; // SPS'siz kare gönderilemez
        };
        const pps = t.pps orelse {
            if (have_frame_data) dlog("send: PPS yok, kare DROP", .{});
            return !have_frame_data;
        };
        t.tag.len = 0; // tag buf'ı geçici gövde kurulumuna kullanma — ayrı kur
        var hdr: Buf = .{};
        defer hdr.deinit();
        hdr.appendSlice(&.{ 0x17, 0x00, 0x00, 0x00, 0x00 }) catch return false;
        buildAvcConfig(&hdr, sps, pps) catch return false;
        const seq_ok = writeFlvTag(t, r, flv_video_tag, 0, hdr.items());
        dlog("send: sequence header yazildi ok={}", .{seq_ok});
        if (!seq_ok) return false;
        t.seq_sent = true;
    }

    if (!have_frame_data) return true; // yalnız parametre setleri geldi — drop değil

    // Göreli FLV zaman damgası (ms) — ilk kare t=0.
    if (t.first_pts_us == no_pts) t.first_pts_us = pts_us;
    var rel_us = pts_us -% t.first_pts_us;
    if (rel_us < 0) rel_us = 0;
    const ts_ms: u32 = @truncate(@as(u64, @intCast(@divTrunc(rel_us, 1000))));

    // VideoTagHeader'ı gövdenin ÖNÜNE koymak için tag'i iki parça yazmak yerine
    // küçük bir başlıkla birleştir: [frame/codec, AVCPacketType=1, cts=0x000000]
    var frame: Buf = .{};
    defer frame.deinit();
    frame.appendSlice(&.{ if (is_keyframe) 0x17 else 0x27, 0x01, 0x00, 0x00, 0x00 }) catch return false;
    frame.appendSlice(t.body.items()) catch return false;

    const ok = writeFlvTag(t, r, flv_video_tag, ts_ms, frame.items());
    if (diag or !ok) dlog("send: frame ts={d}ms key={} body={d}B ok={}", .{ ts_ms, is_keyframe, frame.len, ok });
    return ok;
}

export fn rj_rtmp_is_connected(handle: ?*anyopaque) bool {
    const t = get(handle) orelse return false;
    const r = t.rtmp orelse return false;
    return rt.RTMP_IsConnected(r) != 0;
}

export fn rj_rtmp_shutdown(handle: ?*anyopaque) void {
    const t = get(handle) orelse return;
    if (t.rtmp) |r| rt.RTMP_Close(r);
}

export fn rj_rtmp_destroy(handle: ?*anyopaque) void {
    const t = get(handle) orelse return;
    if (t.rtmp) |r| {
        rt.RTMP_Close(r); // idempotent — zaten kapalıysa no-op
        rt.RTMP_Free(r);
        t.rtmp = null;
    }
    if (t.url) |u| allocator.free(u);
    if (t.sps) |s| allocator.free(s);
    if (t.pps) |p| allocator.free(p);
    t.body.deinit();
    t.tag.deinit();
    allocator.destroy(t);
}

// happy_eyeballs exportlarını bu derleme birimine dahil et (librtmp linki için).
comptime {
    _ = @import("happy_eyeballs.zig");
}

// MinGW ws2tcpip.h, gai_strerrorA'yı dış sembol olarak bırakır ve MSVC
// ws2_32.lib'de yoktur (MSVC header'ı inline verir). rtmp.c'de tek kullanım
// yeri log mesajı (rtmp.c:805). Weak export: MSVC linkini doyurur, MinGW
// test linkinde başka tanım varsa ona boyun eğer.
fn gaiStrErrorA(ecode: c_int) callconv(.c) [*:0]const u8 {
    _ = ecode;
    return "getaddrinfo error";
}
comptime {
    @export(&gaiStrErrorA, .{ .name = "gai_strerrorA", .linkage = .weak });
}

// ── Birim testler (saf muxing/ayrıştırma — ağ gerektirmez) ──────────────────

test {
    _ = @import("happy_eyeballs.zig"); // onun testlerini de koştur
}

test "findStartCode 3 ve 4 baytlık önekleri bulur" {
    const d3 = [_]u8{ 0, 0, 1, 0x67, 0xAA };
    try std.testing.expectEqual(@as(?usize, 3), findStartCode(&d3, 0));
    const d4 = [_]u8{ 0, 0, 0, 1, 0x68 };
    try std.testing.expectEqual(@as(?usize, 4), findStartCode(&d4, 0));
    const none = [_]u8{ 1, 2, 3, 4 };
    try std.testing.expectEqual(@as(?usize, null), findStartCode(&none, 0));
}

test "nextNal ardışık NAL'leri önekleri kırparak döndürür" {
    // SPS(4B) + 4-baytlık önekle PPS(2B) + IDR(3B)
    const data = [_]u8{
        0, 0, 1, 0x67, 1, 2, 3, // SPS
        0, 0, 0, 1, 0x68, 9, // PPS
        0, 0, 1, 0x65, 7, 8, // IDR
    };
    var pos: usize = 0;
    const n1 = nextNal(&data, pos).?;
    try std.testing.expectEqual(@as(u8, 0x67), data[n1.start]);
    try std.testing.expectEqual(@as(usize, 4), n1.end - n1.start);
    pos = n1.start;
    const n2 = nextNal(&data, pos).?;
    try std.testing.expectEqual(@as(u8, 0x68), data[n2.start]);
    try std.testing.expectEqual(@as(usize, 2), n2.end - n2.start);
    pos = n2.start;
    const n3 = nextNal(&data, pos).?;
    try std.testing.expectEqual(@as(u8, 0x65), data[n3.start]);
    try std.testing.expectEqual(@as(usize, 3), n3.end - n3.start);
    try std.testing.expectEqual(@as(?Nal, null), nextNal(&data, n3.start));
}

test "buildAvcConfig doğru AVCC üretir" {
    var out: Buf = .{};
    defer out.deinit();
    const sps = [_]u8{ 0x67, 0x64, 0x00, 0x28, 0xAC }; // profil high(0x64) seviye 4.0(0x28)
    const pps = [_]u8{ 0x68, 0xEE };
    try buildAvcConfig(&out, &sps, &pps);
    const got = out.items();
    try std.testing.expectEqual(@as(u8, 1), got[0]);
    try std.testing.expectEqual(@as(u8, 0x64), got[1]); // profile
    try std.testing.expectEqual(@as(u8, 0x28), got[3]); // level
    try std.testing.expectEqual(@as(u8, 0xFF), got[4]);
    try std.testing.expectEqual(@as(u8, 0xE1), got[5]);
    try std.testing.expectEqual(@as(u8, 5), got[7]); // sps len (BE düşük bayt)
    try std.testing.expectEqualSlices(u8, &sps, got[8..13]);
    try std.testing.expectEqual(@as(u8, 1), got[13]); // num pps
    try std.testing.expectEqual(@as(u8, 2), got[15]); // pps len
    try std.testing.expectEqualSlices(u8, &pps, got[16..18]);
}

test "buildAvcConfig kısa SPS'i reddeder" {
    var out: Buf = .{};
    defer out.deinit();
    const sps = [_]u8{ 0x67, 0x64 };
    const pps = [_]u8{0x68};
    try std.testing.expectError(error.BadParameterSet, buildAvcConfig(&out, &sps, &pps));
}

test "Buf büyür ve içerik korunur" {
    var buf: Buf = .{};
    defer buf.deinit();
    var i: usize = 0;
    while (i < 10_000) : (i += 1) try buf.appendByte(@truncate(i));
    try std.testing.expectEqual(@as(usize, 10_000), buf.len);
    try std.testing.expectEqual(@as(u8, @truncate(9_999)), buf.items()[9_999]);
}
