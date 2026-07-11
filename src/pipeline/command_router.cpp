// src/pipeline/command_router.cpp
//
// CommandRouter implementasyonu. Davranış, Pipeline'ın eski komut/aksiyon koduyla
// (run_frame adım 0/1/1a/1b, action_processor_main, SPSC ring) birebir aynıdır
// (Aşama 5 saf çıkarma — baseline_metrics.txt ile doğrulanır).
#include "command_router.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <cstdarg>
#include <cstdio>

namespace rj {
namespace {

inline void dbglog(const char* fmt, ...) noexcept {
    char buf[256]; va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
    OutputDebugStringA(buf); OutputDebugStringA("\n");
    fprintf(stderr, "[reji] %s\n", buf); fflush(stderr);
}

//  SEH leaf functions  __declspec(noinline), POD params, no destructible locals.
__declspec(noinline)
static int seh_command_drain(RjCommand* buf, int max) noexcept {
    __try   { return rj_command_drain(buf, max); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
__declspec(noinline)
static int seh_ws_command_dequeue(int* cmd, int* param) noexcept {
    __try   { return rj_ws_command_dequeue(cmd, param); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
__declspec(noinline)
static uint16_t seh_get_ws_port() noexcept {
    __try   { return rj_get_ws_port(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

} // namespace

bool CommandRouter::start(SceneCallback scene_cb, ActionHandler on_action) {
    scene_cb_  = std::move(scene_cb);
    on_action_ = std::move(on_action);
    action_processor_running_.store(true, std::memory_order_release);
    action_processor_ = std::thread([this] { action_thread_main(); });
    return true;
}

void CommandRouter::stop() {
    action_processor_running_.store(false, std::memory_order_release);
    if (action_processor_.joinable()) {
        action_processor_.join();
    }
}

bool CommandRouter::push_frame_cmd(const FrameCmd& cmd) noexcept {
    uint32_t head = frame_cmd_head_.load(std::memory_order_relaxed);
    uint32_t next = (head + 1) % kFrameCmdCap;
    if (next == frame_cmd_tail_.load(std::memory_order_acquire)) return false;
    frame_cmd_buf_[head] = cmd;
    frame_cmd_head_.store(next, std::memory_order_release);
    return true;
}

void CommandRouter::drain_and_apply(CommandApplier apply_command,
                                    FrameCmdApplier apply_frame_cmd,
                                    StartStopHandler handle_start_stop) {
    // 0) İlk frame'de gerçek WS portunu logla (Tokio bind'ı async — init anında hazır olmayabilir)
    if (!ws_port_logged_) {
        uint16_t ws_port = seh_get_ws_port();
        if (ws_port != 0) {
            dbglog("[Pipeline] WS listening on ws://127.0.0.1:%u/ws (control: http://127.0.0.1:%u/)",
                   (unsigned)ws_port, (unsigned)ws_port);
            ws_port_logged_ = true;
        }
    }

    // 1) Command drain  clamp [0,8]; log negative
    int n = seh_command_drain(cmd_buf_.data(), kCmdDrainMax);
    if (n < 0) {
        dbglog("[Pipeline] rj_command_drain negative: %d", n);
        n = 0;
    } else if (n > kCmdDrainMax) {
        n = kCmdDrainMax;
    }
    for (int i = 0; i < n; ++i) apply_command(cmd_buf_[i]);

    // 1a) WS command drain — rj_ws_command_queue (Rust) → run_frame() thread
    {
        int ws_cmd = 0, ws_param = 0;
        while (seh_ws_command_dequeue(&ws_cmd, &ws_param)) {
            switch (ws_cmd) {
                case 1:
                case 2: handle_start_stop(ws_cmd);            break;  // start/stop_stream
                case 3:
                case 4:  // scene_cut/fade — param cut/fade'de kullanılmaz ama imza tutarlı
                    if (scene_cb_) scene_cb_(ws_cmd, static_cast<uint32_t>(ws_param));
                    break;
                case 5:  // set_scene — param = 0-based sahne indeksi
                    if (scene_cb_) scene_cb_(ws_cmd, static_cast<uint32_t>(ws_param));
                    break;
                default: dbglog("[Pipeline] unknown ws_cmd=%d", ws_cmd); break;
            }
        }
    }

    // 1b) C6: Drain SPSC frame commands from action_processor (no lock needed)
    {
        uint32_t tail = frame_cmd_tail_.load(std::memory_order_relaxed);
        uint32_t head = frame_cmd_head_.load(std::memory_order_acquire);
        while (tail != head) {
            apply_frame_cmd(frame_cmd_buf_[tail]);
            tail = (tail + 1) % kFrameCmdCap;
        }
        frame_cmd_tail_.store(tail, std::memory_order_release);
    }
}

void CommandRouter::action_thread_main() {
    // D14: COM init once at thread start — required for WMI/DXGI calls on this thread
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // V8/I9: CoUninitialize yalnız başarılı CoInitializeEx (S_OK/S_FALSE) için
    // çağrılmalı. RPC_E_CHANGED_MODE FAILED'dır → bu çağrı COM'u BU thread'de
    // initialize etmedi; koşulsuz uninit apartment ref-count'unu dengesizleştirir.
    // (Doğru desen: wasapi_capture.cpp / srt_output.cpp::ComGuard.)
    const bool com_ok = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "[Pipeline] COM init failed: 0x%08X\n", hr);
        fflush(stderr);
    }

    while (action_processor_running_.load(std::memory_order_acquire)) {
        RjAction action{};
        // Poll rj_action_dequeue (FFI call) — non-blocking, returns false if queue empty
        if (rj_action_dequeue(&action)) {
            if (on_action_) on_action_(action);
        }
        // Prevent busy-wait: yield briefly if queue is empty
        Sleep(100);  // 100ms poll — V8/I1 sonrası RuleEngine aksiyonları buradan akıyor;
                     // kural değerlendirmesi ~1s periyotta olduğundan 100ms gecikme kabul edilebilir
    }
    dbglog("[Pipeline] action processor stopped");

    if (com_ok) CoUninitialize();  // V8/I9: D14 çifti — yalnız başarılı init'te
}

} // namespace rj

#else  // !_WIN32

namespace rj {
bool CommandRouter::start(SceneCallback, ActionHandler) { return false; }
void CommandRouter::stop() {}
bool CommandRouter::push_frame_cmd(const FrameCmd&) noexcept { return false; }
void CommandRouter::drain_and_apply(CommandApplier, FrameCmdApplier, StartStopHandler) {}
void CommandRouter::action_thread_main() {}
} // namespace rj

#endif // _WIN32
