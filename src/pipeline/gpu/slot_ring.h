#pragma once
#include <cstdint>
#include "../include/reji_constants.h"

// I23: GPU kaynak havuzu (staging/target image, command buffer, GL interop
// texture) için TEK round-robin ilerleme kaynağı. ExternalMemoryBridge slot'u
// üretir; slot GpuInteropSubsystem cache → Pipeline → PreviewWidget → execute_copy
// boyunca taşınır. Böylece bridge image kimliği, optimizer per-slot layout
// tracking'i ve widget GL-interop indexlemesi TEK index'te hizalanır (drift yok).
//
// Saf (GPU'suz) fonksiyon: seam birim testi için izole edilebilir.
namespace rj::pipeline::gpu {

// [0, kGpuPoolSize) üzerinde round-robin bir sonraki slot.
constexpr uint32_t next_pool_slot(uint32_t cur) noexcept {
    return (cur + 1u) % static_cast<uint32_t>(rj::constants::kGpuPoolSize);
}

}  // namespace rj::pipeline::gpu
