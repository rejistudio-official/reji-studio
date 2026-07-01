#pragma once
#ifdef RJ_PLATFORM_WINDOWS

#include <cstdint>
#include <functional>
#include <memory>
#include <d3d11.h>
#include "reji_constants.h"

namespace reji {

// ---------------------------------------------------------------------------
// NvencEncoder — zero-copy H.264/HEVC encoder via NVENC SDK
//
// Input:  ID3D11Texture2D* on the encode GPU (from DxgiCapturePipeline)
// Path:   NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX — no CPU copy, VRAM stays on GPU
// DLL:    nvEncodeAPI64.dll loaded at runtime via LoadLibrary
//
// Typical usage (single thread):
//   NvencEncoder enc;
//   enc.init(capture.encode_gpu()->d3d_device(), cfg,
//            [](auto& pkt){ srt_send(pkt.data, pkt.size); });
//   while (streaming) {
//       auto* tex = capture.capture_next();          // NVIDIA VRAM
//       if (tex) enc.encode_frame(tex, pts++);       // zero-copy into NVENC
//   }
//   enc.flush();
//
// Thread safety: not thread-safe; call from a single encode thread.
// ---------------------------------------------------------------------------
class NvencEncoder {
public:
    struct Config {
        uint32_t width            = 1920;
        uint32_t height           = 1080;
        uint32_t fps_num          = 60;
        uint32_t fps_den          = 1;
        uint32_t bitrate_kbps     = rj::constants::kDefaultBitrateKbps;
        uint32_t max_bitrate_kbps = 8000;   ///< peak cap (CBR ceiling)
        uint32_t gop_size         = 120;    ///< 2 s at 60 fps
        enum class Codec { H264, HEVC } codec = Codec::H264;
    };

    /// Compressed output packet — valid only inside the PacketCallback invocation.
    struct Packet {
        const uint8_t* data;
        size_t         size;
        bool           is_keyframe;
        int64_t        pts;          ///< echoed from encode_frame()
    };

    using PacketCallback = std::function<void(const Packet&)>;

    NvencEncoder();
    ~NvencEncoder();

    NvencEncoder(const NvencEncoder&) = delete;
    NvencEncoder& operator=(const NvencEncoder&) = delete;

    /// Load nvEncodeAPI64.dll, open NVENC session on `encode_device`,
    /// configure codec/RC, create output bitstream buffers.
    /// `encode_device` must be the same D3D11 device the input textures live on.
    bool init(ID3D11Device* encode_device, const Config& cfg, PacketCallback on_packet);

    void shutdown();

    /// Register (lazily, cached) and encode a D3D11 texture.
    /// The texture is passed directly to NVENC via DIRECTX resource type — no CPU copy.
    /// Returns false on NVENC error; timeout / NEED_MORE_INPUT returns true (not errors).
    bool encode_frame(ID3D11Texture2D* tex, int64_t pts, bool force_idr = false);

    /// Flush remaining buffered frames (EOS). Call before shutdown on stream end.
    void flush();

    /// Dynamically update bitrate — effective from the next frame.
    /// Calls nvEncReconfigureEncoder; does not interrupt the encode session.
    bool set_bitrate(uint32_t kbps);

    /// Scale resolution by factor (e.g., 0.75 = 75% of original).
    /// Note: NVENC resolution changes require careful handling; partial implementation for v0.4.
    bool set_resolution(float scale_factor);

    /// Set frame rate limit (e.g., 30 fps). Requires reconfiguration.
    bool set_fps_limit(uint32_t fps);

    bool is_initialized() const;

private:
    struct Impl;  // defined in encode_nvenc.cpp — hides all NVENC SDK types
    std::unique_ptr<Impl> impl_;
};

} // namespace reji
#endif // RJ_PLATFORM_WINDOWS
