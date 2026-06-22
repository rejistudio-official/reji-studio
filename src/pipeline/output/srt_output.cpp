#include "srt_output.h"
#include "ffi_bridge.h"

#include <srt/srt.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ws2tcpip.h>
#include <objbase.h>

#include <atomic>
#include <array>
#include <cstring>
#include <mutex>
#include <thread>
#include <chrono>

// Layout doğrulama — Rust #[repr(C)] ile eşleşmeli (naturel hizalanmış, x64 MSVC).
// v0.3: 40 bytes; v0.4: 56 bytes (extended with thermal, load, network metrics)
// Rust tarafı için bkz. src/orchestrator/lib.rs ffi::RjMetricSample
static_assert(sizeof(RjMetricSample) == 56,
              "RjMetricSample C layout Rust repr(C) ile eslesmeli (56 bayt v0.4, pragma pack yok)");

namespace rj::pipeline::output {

namespace {

constexpr int kEpollWaitMs     = 100;
constexpr int kSendTimeoutMs   = 50;
constexpr int kMsgTtlMs        = 100;
constexpr int kMaxPayloadBytes = 1456;
constexpr int kPayloadSize     = 1316;

// ============================================================================
// SRT global yaşam döngüsü (process-scope, instance counted)
// ============================================================================
class SrtGlobalRegistry {
public:
    static SrtGlobalRegistry& instance() {
        static SrtGlobalRegistry r;
        return r;
    }

    bool acquire() noexcept {
        std::call_once(once_, [this] {
            ok_ = (srt_startup() == 0);
        });
        if (!ok_) return false;
        ref_.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    bool release() noexcept {
        if (!ok_) return false;
        ref_.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    int  refcount() const noexcept { return ref_.load(std::memory_order_acquire); }
    bool ok()       const noexcept { return ok_; }

private:
    std::once_flag   once_;
    std::atomic<int> ref_{0};
    bool             ok_{false};
};

// ============================================================================
// COM init guard — thread başına idempotent
// ============================================================================
struct ComGuard {
    bool initialized = false;
    bool init_now() noexcept {
        HRESULT hr = CoInitializeEx(nullptr,
                                    COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
        if (hr == S_OK || hr == S_FALSE) {
            initialized = true;
            return true;
        }
        if (hr == RPC_E_CHANGED_MODE) {
            initialized = false;
            return true;
        }
        return false;
    }
    ~ComGuard() {
        if (initialized) CoUninitialize();
    }
};

// ============================================================================
// SEH leaf fonksiyonları — C++ nesne yaratımı/yıkımı içermez, noexcept.
// ============================================================================

__declspec(noinline)
static int seh_leaf_sendmsg(SRTSOCKET sock,
                            const char* buf,
                            int len,
                            int ttl_ms,
                            int in_order) noexcept
{
    SRT_MSGCTRL mctrl;
    srt_msgctrl_init(&mctrl);
    mctrl.msgttl  = ttl_ms;
    mctrl.inorder = in_order;
    return srt_sendmsg2(sock, buf, len, &mctrl);
}

__declspec(noinline)
static int seh_leaf_close(SRTSOCKET sock) noexcept {
    return srt_close(sock);
}

__declspec(noinline)
static int seh_leaf_epoll_release(int eid) noexcept {
    return srt_epoll_release(eid);
}

__declspec(noinline)
static int seh_leaf_setsockopt(SRTSOCKET sock, int opt,
                               const void* val, int len) noexcept {
    return srt_setsockopt(sock, 0, static_cast<SRT_SOCKOPT>(opt), val, len);
}

__declspec(noinline)
static void seh_leaf_global_cleanup(int do_srt_cleanup) noexcept {
    if (do_srt_cleanup) srt_cleanup();
}

static LONG seh_filter(DWORD code) noexcept {
    if (code == EXCEPTION_STACK_OVERFLOW ||
        code == EXCEPTION_BREAKPOINT     ||
        code == EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static int seh_safe_sendmsg(SRTSOCKET s, const char* b, int l,
                            int ttl, int io) noexcept {
    int rv = -1;
    __try { rv = seh_leaf_sendmsg(s, b, l, ttl, io); }
    __except (seh_filter(GetExceptionCode())) { rv = -1; }
    return rv;
}

static int seh_safe_close(SRTSOCKET s) noexcept {
    int rv = -1;
    __try { rv = seh_leaf_close(s); }
    __except (seh_filter(GetExceptionCode())) { rv = -1; }
    return rv;
}

static int seh_safe_epoll_release(int eid) noexcept {
    int rv = -1;
    __try { rv = seh_leaf_epoll_release(eid); }
    __except (seh_filter(GetExceptionCode())) { rv = -1; }
    return rv;
}

static int seh_safe_setsockopt(SRTSOCKET s, int opt,
                               const void* v, int l) noexcept {
    int rv = -1;
    __try { rv = seh_leaf_setsockopt(s, opt, v, l); }
    __except (seh_filter(GetExceptionCode())) { rv = -1; }
    return rv;
}

static void seh_safe_global_cleanup(int do_srt_cleanup) noexcept {
    __try { seh_leaf_global_cleanup(do_srt_cleanup); }
    __except (seh_filter(GetExceptionCode())) { }
}

} // namespace

// ============================================================================
// Impl
// ============================================================================
struct SrtOutput::Impl {
    SRTSOCKET             sock        = SRT_INVALID_SOCK;  // listen (listener) or connect (caller)
    SRTSOCKET             client_sock_ = SRT_INVALID_SOCK; // accepted client — listener mode only
    int                   epoll_id = -1;
    Config                cfg{};

    std::atomic<bool>     connected{false};
    std::atomic<bool>     initialized{false};
    std::atomic<bool>     shutting_down{false};
    std::atomic<bool>     notified_lost{false};
    std::atomic<uint32_t> bitrate_kbps{0};

    ComGuard    com_guard;
    std::thread worker_thread;

    bool configure_socket(SRTSOCKET s) noexcept {
        if (s == SRT_INVALID_SOCK) return false;

        int     no    = 0;
        int     yes   = 1;
        int     lat   = static_cast<int>(cfg.latency_ms ? cfg.latency_ms : 200);
        int     ttype = SRTT_LIVE;
        int     pload = kPayloadSize;
        int     stout = kSendTimeoutMs;
        int64_t maxbw = (cfg.bandwidth_kbps > 0)
            ? static_cast<int64_t>(cfg.bandwidth_kbps) * 1000
            : 0;

        if (seh_safe_setsockopt(s, SRTO_SNDSYN,    &no,    sizeof no)    < 0) return false;
        if (seh_safe_setsockopt(s, SRTO_RCVSYN,    &no,    sizeof no)    < 0) return false;
        if (seh_safe_setsockopt(s, SRTO_TSBPDMODE, &yes,   sizeof yes)   < 0) return false;
        if (seh_safe_setsockopt(s, SRTO_LATENCY,   &lat,   sizeof lat)   < 0) return false;
        if (seh_safe_setsockopt(s, SRTO_TRANSTYPE, &ttype, sizeof ttype) < 0) return false;

        seh_safe_setsockopt(s, SRTO_PAYLOADSIZE, &pload, sizeof pload);
        seh_safe_setsockopt(s, SRTO_SNDTIMEO,   &stout, sizeof stout);
        seh_safe_setsockopt(s, SRTO_MAXBW,      &maxbw, sizeof maxbw);
        return true;
    }

    bool init_internal(const Config& cfg_in) noexcept {
        if (initialized.load(std::memory_order_acquire)) return false;

        if (!com_guard.init_now()) return false;
        if (!SrtGlobalRegistry::instance().acquire()) return false;

        cfg = cfg_in;
        bitrate_kbps.store(cfg.bandwidth_kbps, std::memory_order_release);

        sock = srt_create_socket();
        if (sock == SRT_INVALID_SOCK) return false;

        if (!configure_socket(sock)) {
            seh_safe_close(sock);
            sock = SRT_INVALID_SOCK;
            return false;
        }

        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port   = htons(cfg.port);
        if (inet_pton(AF_INET, cfg.host, &sa.sin_addr) != 1) {
            seh_safe_close(sock);
            sock = SRT_INVALID_SOCK;
            return false;
        }

        if (cfg.caller_mode) {
            int rv = srt_connect(sock, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
            if (rv == SRT_ERROR) {
                int err = srt_getlasterror(nullptr);
                if (err != SRT_EASYNCSND && err != SRT_EASYNCRCV &&
                    err != SRT_EASYNCFAIL) {
                    seh_safe_close(sock);
                    sock = SRT_INVALID_SOCK;
                    return false;
                }
            }
        } else {
            if (srt_bind(sock, reinterpret_cast<sockaddr*>(&sa), sizeof sa) == SRT_ERROR ||
                srt_listen(sock, 1) == SRT_ERROR) {
                seh_safe_close(sock);
                sock = SRT_INVALID_SOCK;
                return false;
            }
        }

        epoll_id = srt_epoll_create();
        if (epoll_id < 0) {
            seh_safe_close(sock);
            sock = SRT_INVALID_SOCK;
            return false;
        }
        int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR |
                     (cfg.caller_mode ? SRT_EPOLL_CONNECT : SRT_EPOLL_IN);
        if (srt_epoll_add_usock(epoll_id, sock, &events) == SRT_ERROR) {
            seh_safe_epoll_release(epoll_id);
            seh_safe_close(sock);
            sock     = SRT_INVALID_SOCK;
            epoll_id = -1;
            return false;
        }

        rj_start_monitor();

        shutting_down.store(false, std::memory_order_release);
        notified_lost.store(false, std::memory_order_release);
        worker_thread = std::thread(&Impl::worker_loop, this);

        initialized.store(true, std::memory_order_release);
        return true;
    }

    void notify_connection_lost(const char* reason) noexcept {
        bool expected = false;
        if (notified_lost.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
            rj_connection_lost(reason);
        }
    }

    void update_state_from_socket() noexcept {
        SRTSOCKET check = cfg.caller_mode ? sock : client_sock_;
        if (check == SRT_INVALID_SOCK) {
            connected.store(false, std::memory_order_release);
            return;
        }
        SRT_SOCKSTATUS st    = srt_getsockstate(check);
        bool           now_c = (st == SRTS_CONNECTED);
        bool           was   = connected.exchange(now_c, std::memory_order_acq_rel);

        if (now_c) {
            notified_lost.store(false, std::memory_order_release);
        } else if (was && !shutting_down.load(std::memory_order_acquire)) {
            notify_connection_lost("srt_socket_disconnected");
        }
    }

    void process_commands() noexcept {}

    void worker_loop() noexcept {
        while (!shutting_down.load(std::memory_order_acquire)) {
            SRT_EPOLL_EVENT ev[1];
            int n = srt_epoll_uwait(epoll_id, ev, 1, kEpollWaitMs);
            if (n > 0) {
                int e = ev[0].events;
                if (e & SRT_EPOLL_ERR) {
                    if (!cfg.caller_mode && ev[0].fd == client_sock_) {
                        // Client koptu — sadece client soketi kapat, listen soket korunur.
                        srt_epoll_remove_usock(epoll_id, client_sock_);
                        seh_safe_close(client_sock_);
                        client_sock_ = SRT_INVALID_SOCK;
                        connected.store(false, std::memory_order_release);
                        if (!shutting_down.load(std::memory_order_acquire))
                            notify_connection_lost("srt_client_disconnected");
                    } else {
                        connected.store(false, std::memory_order_release);
                        notify_connection_lost("srt_epoll_error");
                    }
                } else if ((e & SRT_EPOLL_IN) && !cfg.caller_mode) {
                    // Yeni bağlantı: listen soketi (sock) üzerinden accept et.
                    SRTSOCKET client = srt_accept(sock, nullptr, nullptr);
                    if (client != SRT_INVALID_SOCK) {
                        // Önceki client varsa temizle (yeni bağlantı öncelikli).
                        if (client_sock_ != SRT_INVALID_SOCK) {
                            srt_epoll_remove_usock(epoll_id, client_sock_);
                            seh_safe_close(client_sock_);
                        }
                        client_sock_ = client;
                        configure_socket(client_sock_);
                        int oev = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                        srt_epoll_add_usock(epoll_id, client_sock_, &oev);
                        connected.store(true, std::memory_order_release);
                        notified_lost.store(false, std::memory_order_release);
                    } else {
                        OutputDebugStringA("[rj_srt] srt_accept failed, retaining listen socket\n");
                    }
                } else if (e & (SRT_EPOLL_OUT | SRT_EPOLL_CONNECT)) {
                    update_state_from_socket();
                }
            }
            process_commands();
        }
        connected.store(false, std::memory_order_release);
    }

    // Hot-path: heap yok, retry yok, non-blocking.
    bool send_internal(const uint8_t* data, size_t size, int64_t /*pts*/) noexcept {
        if (!initialized.load(std::memory_order_acquire))  return false;
        if (shutting_down.load(std::memory_order_acquire)) return false;
        if (!data || size == 0 || size > kMaxPayloadBytes) return false;
        if (!connected.load(std::memory_order_acquire))    return false;

        SRTSOCKET send_sock = cfg.caller_mode ? sock : client_sock_;
        if (send_sock == SRT_INVALID_SOCK) return false;

        int rv = seh_safe_sendmsg(send_sock,
                                  reinterpret_cast<const char*>(data),
                                  static_cast<int>(size),
                                  kMsgTtlMs, /*inorder*/ 1);
        if (rv == SRT_ERROR) {
            int err = srt_getlasterror(nullptr);
            if (err == SRT_EASYNCSND) return false; // buffer dolu, paket düş

            if (err == SRT_ECONNLOST || err == SRT_ECONNREJ  ||
                err == SRT_ECONNFAIL || err == SRT_ECONNSETUP ||
                err == SRT_ENOCONN) {
                connected.store(false, std::memory_order_release);
                if (!shutting_down.load(std::memory_order_acquire)) {
                    notify_connection_lost("srt_sendmsg_connection_lost");
                }
            }
            return false;
        }

        // Başarılı gönderim — anlık bitrate metriğini Rust ring buffer'a ilet.
        {
            FILETIME ft;
            GetSystemTimePreciseAsFileTime(&ft);
            constexpr uint64_t kWindowsToUnix = 116444736000000000ULL; // 100ns birimleri
            uint64_t raw   = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
            uint64_t ts_us = raw > kWindowsToUnix ? (raw - kWindowsToUnix) / 10ULL : 0ULL;

            RjMetricSample m{};
            m.magic_head   = RJ_METRIC_MAGIC;
            m.timestamp_us = ts_us;
            m.bitrate_kbps = bitrate_kbps.load(std::memory_order_relaxed);
            m.fps_actual   = 0.0f;
            m.cpu_percent  = 0.0f;
            m.frame_drops  = 0;
            m.magic_tail   = RJ_METRIC_MAGIC;
            rj_metrics_push(&m);
        }
        return true;
    }

    bool apply_bitrate(uint32_t kbps) noexcept {
        SRTSOCKET s = cfg.caller_mode ? sock : client_sock_;
        if (s == SRT_INVALID_SOCK) return false;
        bitrate_kbps.store(kbps, std::memory_order_release);
        int64_t maxbw = static_cast<int64_t>(kbps) * 1000;
        return seh_safe_setsockopt(s, SRTO_MAXBW, &maxbw, sizeof maxbw) >= 0;
    }

    // C++ kaynakları temizle (thread join). SEH bloğunun dışında çalışır.
    bool prepare_shutdown() noexcept {
        bool expected = true;
        if (!initialized.compare_exchange_strong(expected, false,
                                                 std::memory_order_acq_rel)) {
            return true; // hiç açılmamış / zaten kapalı
        }
        shutting_down.store(true, std::memory_order_release);
        connected.store(false, std::memory_order_release);

        if (worker_thread.joinable()) worker_thread.join();
        return true;
    }

    ~Impl() {
        if (initialized.load(std::memory_order_acquire)) {
            prepare_shutdown();
            do_seh_cleanup();
        }
    }

    // SEH-korumalı, leaf-only C kaynak temizliği.
    void do_seh_cleanup() noexcept {
        SRTSOCKET local_sock        = sock;
        SRTSOCKET local_client_sock = client_sock_;
        int       local_eid         = epoll_id;
        sock         = SRT_INVALID_SOCK;
        client_sock_ = SRT_INVALID_SOCK;
        epoll_id     = -1;

        if (local_eid >= 0)                        seh_safe_epoll_release(local_eid);
        if (local_client_sock != SRT_INVALID_SOCK)  seh_safe_close(local_client_sock);
        if (local_sock != SRT_INVALID_SOCK)         seh_safe_close(local_sock);

        SrtGlobalRegistry::instance().release();
        if (SrtGlobalRegistry::instance().refcount() == 0) {
            seh_safe_global_cleanup(/*do_srt_cleanup=*/1);
        }
    }
};

// ============================================================================
// Public yüzey
// ============================================================================
SrtOutput::SrtOutput()  : impl_(std::make_unique<Impl>()) {}
SrtOutput::~SrtOutput() {
    if (impl_) shutdown();
}

bool SrtOutput::init(const Config& cfg) {
    if (!impl_) return false;
    return impl_->init_internal(cfg);
}

bool SrtOutput::send_packet(const uint8_t* data, size_t size, int64_t pts) {
    if (!impl_) return false;
    return impl_->send_internal(data, size, pts);
}

bool SrtOutput::is_connected() const noexcept {
    if (!impl_) return false;
    return impl_->connected.load(std::memory_order_acquire);
}

bool SrtOutput::set_bitrate(uint32_t kbps) {
    if (!impl_) return false;
    return impl_->apply_bitrate(kbps);
}

// shutdown(): C++ teardown prepare_shutdown(), ardından SEH-sarılı C cleanup.
bool SrtOutput::shutdown() {
    if (!impl_) return false;
    impl_->prepare_shutdown();
    impl_->do_seh_cleanup();
    return true;
}

} // namespace rj::pipeline::output
