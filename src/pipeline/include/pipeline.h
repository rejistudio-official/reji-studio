#pragma once
#include <cstdint>

namespace reji {

struct PipelineConfig {
    uint32_t width  = 1920;
    uint32_t height = 1080;
    uint32_t fps    = 60;
};

class Pipeline {
public:
    Pipeline();
    ~Pipeline();

    bool init(const PipelineConfig& cfg);
    void shutdown();

private:
    PipelineConfig config_;
};

} // namespace reji