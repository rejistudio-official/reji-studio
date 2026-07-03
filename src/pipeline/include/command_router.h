// src/pipeline/include/command_router.h
//
// CommandRouter — komut/aksiyon yönlendirme alt sistemi (Aşama 5'te Pipeline::Impl'den
// çıkarıldı). Sahiplendikleri:
//   - action_processor thread'i (COM init + rj_action_dequeue poll döngüsü)
//   - SPSC ring buffer (action thread producer → run_frame consumer)
//   - her frame drain: WS port log + rj_command_drain + WS command drain + SPSC drain
//
// TASARIM: CommandRouter, Pipeline::Impl'i veya EncodeSubsystem'i BİLMEZ. Encode'a/
// state'e dokunan tüm mantık (apply_command / apply_frame_cmd / apply_action /
// start_stream / stop_stream / scene_cb) dışarıdan callback olarak verilir.
//
// "Sıkı düğüm" olan on_packet burada değil; orkestratörde (Impl) kalır.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include "ffi_bridge.h"   // RjCommand, RjAction (-I .../src/ffi)

namespace rj {

class CommandRouter {
public:
    static constexpr int kCmdDrainMax = 8;
    static constexpr int kFrameCmdCap = 16;

    // C6: action_processor (producer) → run_frame (consumer) arası komut.
    struct FrameCmd { int action_type; int32_t param1; };

    using SceneCallback    = std::function<void(int, uint32_t)>;    // ws_cmd 3/4/5 (param: SetScene idx)
    using ActionHandler    = std::function<void(const RjAction&)>;  // action thread
    using CommandApplier   = std::function<void(const RjCommand&)>; // rj_command_drain
    using FrameCmdApplier  = std::function<void(const FrameCmd&)>;  // SPSC drain
    using StartStopHandler = std::function<void(int)>;             // ws_cmd 1/2

    // action_processor thread'i başlatır. scene_cb: WS scene komutları (3/4);
    // on_action: her dequeue edilen RjAction için action thread'de çağrılır.
    bool start(SceneCallback scene_cb, ActionHandler on_action);

    // action_processor_running=false + join. Pipeline::shutdown() sırasında,
    // mevcut akışdaki yerini koruyarak çağrılır.
    void stop();

    // run_frame()'den her frame: adım 0 (WS port log) + 1 (command drain) +
    // 1a (WS command drain) + 1b (SPSC frame cmd drain).
    void drain_and_apply(CommandApplier apply_command,
                         FrameCmdApplier apply_frame_cmd,
                         StartStopHandler handle_start_stop);

    // SPSC producer (action thread) → run_frame consumer. Kapasite dolu ise false.
    bool push_frame_cmd(const FrameCmd& cmd) noexcept;

private:
    void action_thread_main();

    std::thread          action_processor_;
    std::atomic<bool>    action_processor_running_{false};
    SceneCallback        scene_cb_;
    ActionHandler        on_action_;

    std::array<RjCommand, kCmdDrainMax> cmd_buf_{};   // drain scratch (frame thread)
    bool                 ws_port_logged_ = false;

    std::array<FrameCmd, kFrameCmdCap> frame_cmd_buf_{};
    std::atomic<uint32_t> frame_cmd_head_{0};
    std::atomic<uint32_t> frame_cmd_tail_{0};
};

} // namespace rj
