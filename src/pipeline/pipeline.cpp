/**
 * reji_pipeline — Medya pipeline çekirdeği
 * Platform: Windows (v0.1), macOS (v0.3), Linux (v0.4)
 */

#include "include/pipeline.h"
#include <cstdio>

namespace reji {

Pipeline::Pipeline() {
    printf("[Pipeline] Baslatildi\n");
}

Pipeline::~Pipeline() {
    printf("[Pipeline] Kapatildi\n");
}

bool Pipeline::init(const PipelineConfig& cfg) {
    config_ = cfg;
    printf("[Pipeline] Konfigürasyon: %ux%u@%u fps\n",
           cfg.width, cfg.height, cfg.fps);
    return true;
}

void Pipeline::shutdown() {
    printf("[Pipeline] Shutdown\n");
}

} // namespace reji