#include "encode_nvenc.h"
#ifdef RJ_PLATFORM_WINDOWS

#include <cstdio>
#include <windows.h>

// ---------------------------------------------------------------------------
// NVENC SDK header — required for struct / GUID / version-macro definitions.
// Install NVIDIA Video Codec SDK and set NVENC_SDK_PATH in CMake.
// DLL (nvEncodeAPI64.dll) is loaded at runtime via LoadLibrary — no import lib.
// ---------------------------------------------------------------------------
#ifdef RJ_NVENC_AVAILABLE
#include <nvEncodeAPI.h>
#endif

namespace reji {

// ===========================================================================
// Stub implementation — compiled when NVENC SDK headers are not available.
// ===========================================================================
#ifndef RJ_NVENC_AVAILABLE

struct NvencEncoder::Impl {};

NvencEncoder::NvencEncoder() = default;
NvencEncoder::~NvencEncoder() { shutdown(); }

bool NvencEncoder::init(ID3D11Device*, const Config&, PacketCallback) {
    printf("[NVENC] SDK not available — set NVENC_SDK_PATH at CMake configure time\n");
    return false;
}
void NvencEncoder::shutdown()                              {}
bool NvencEncoder::encode_frame(ID3D11Texture2D*, int64_t, bool) { return false; }
void NvencEncoder::flush()                                 {}
bool NvencEncoder::set_bitrate(uint32_t)                   { return false; }
bool NvencEncoder::is_initialized() const                  { return false; }

#else // RJ_NVENC_AVAILABLE
// ===========================================================================
// Full implementation
// ===========================================================================

typedef NVENCSTATUS (NVENCAPI* PFN_NvEncodeAPICreateInstance)(NV_ENCODE_API_FUNCTION_LIST*);

static constexpr int kBitstreamPoolSize = 3;  // triple-buffer; keeps GPU pipeline full

// ---------------------------------------------------------------------------
// Impl — all NVENC state is private to this translation unit
// ---------------------------------------------------------------------------
struct NvencEncoder::Impl {
    HMODULE                    dll           = nullptr;
    NV_ENCODE_API_FUNCTION_LIST api          = {};   // function table filled by SDK
    void*                      session       = nullptr;

    // Lazy-registered input resource cache
    ID3D11Texture2D*           reg_tex       = nullptr;
    NV_ENC_REGISTERED_PTR      reg_res       = nullptr;

    // Output bitstream ring
    NV_ENC_OUTPUT_PTR          out_bufs[kBitstreamPoolSize] = {};
    int                        out_idx       = 0;

    // Saved state for dynamic reconfiguration (set_bitrate)
    NV_ENC_CONFIG              saved_cfg     = {};
    NV_ENC_INITIALIZE_PARAMS   saved_init    = {};   // stored without encodeConfig ptr

    NvencEncoder::Config       config        = {};
    PacketCallback             on_packet;
    bool                       initialized   = false;
    int64_t                    frame_idx     = 0;

    // -----------------------------------------------------------------------
    // DLL load + function list population
    // -----------------------------------------------------------------------
    bool load_dll() {
        dll = LoadLibraryW(L"nvEncodeAPI64.dll");
        if (!dll) {
            printf("[NVENC] LoadLibrary(nvEncodeAPI64.dll) failed: %lu\n",
                   GetLastError());
            return false;
        }
        auto* create_fn = reinterpret_cast<PFN_NvEncodeAPICreateInstance>(
            GetProcAddress(dll, "NvEncodeAPICreateInstance"));
        if (!create_fn) {
            printf("[NVENC] GetProcAddress(NvEncodeAPICreateInstance) failed\n");
            return false;
        }
        api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
        NVENCSTATUS s = create_fn(&api);
        if (s != NV_ENC_SUCCESS) {
            printf("[NVENC] NvEncodeAPICreateInstance failed: %d\n", s);
            return false;
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // Session open — attach D3D11 device as NVENC device context
    // -----------------------------------------------------------------------
    NVENCSTATUS open_session(ID3D11Device* device) {
        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS p = {};
        p.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        p.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
        p.device     = device;
        p.apiVersion = NVENCAPI_VERSION;
        return api.nvEncOpenEncodeSessionEx(&p, &session);
    }

    // -----------------------------------------------------------------------
    // Preset config retrieval — SDK fills in codec defaults for the tuning mode
    // -----------------------------------------------------------------------
    NVENCSTATUS get_preset_cfg(NV_ENC_CONFIG& out) {
        NV_ENC_PRESET_CONFIG pc = {};
        pc.version           = NV_ENC_PRESET_CONFIG_VER;
        pc.presetCfg.version = NV_ENC_CONFIG_VER;

        GUID codec  = (config.codec == Config::Codec::H264)
                    ? NV_ENC_CODEC_H264_GUID : NV_ENC_CODEC_HEVC_GUID;

        NVENCSTATUS s = api.nvEncGetEncodePresetConfigEx(
            session, codec,
            NV_ENC_PRESET_P4_GUID,              // balanced low-latency preset
            NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY,
            &pc);
        if (s == NV_ENC_SUCCESS) { out = pc.presetCfg; }
        return s;
    }

    // -----------------------------------------------------------------------
    // Encoder initialization
    // -----------------------------------------------------------------------
    NVENCSTATUS init_encoder() {
        NV_ENC_CONFIG enc = {};
        NVENCSTATUS s = get_preset_cfg(enc);
        if (s != NV_ENC_SUCCESS) {
            printf("[NVENC] GetEncodePresetConfigEx failed: %d\n", s);
            return s;
        }

        // Constant-bitrate RC — ideal for live streaming with fixed network bandwidth.
        enc.rcParams.rateControlMode  = NV_ENC_PARAMS_RC_CBR;
        enc.rcParams.averageBitRate   = config.bitrate_kbps     * 1000u;
        enc.rcParams.maxBitRate       = config.max_bitrate_kbps * 1000u;
        // VBV size = 1 frame of data — minimum buffering, maximum responsiveness.
        enc.rcParams.vbvBufferSize    = config.bitrate_kbps * 1000u / config.fps_num;
        enc.rcParams.vbvInitialDelay  = enc.rcParams.vbvBufferSize;

        // No B-frames: every P-frame depends only on the previous frame.
        // Reduces end-to-end latency by (gop_size - 1) / fps seconds.
        enc.frameIntervalP            = 1;
        enc.gopLength                 = config.gop_size;

        NV_ENC_INITIALIZE_PARAMS init = {};
        init.version           = NV_ENC_INITIALIZE_PARAMS_VER;
        init.encodeGUID        = (config.codec == Config::Codec::H264)
                                  ? NV_ENC_CODEC_H264_GUID : NV_ENC_CODEC_HEVC_GUID;
        init.presetGUID        = NV_ENC_PRESET_P4_GUID;
        init.encodeWidth       = config.width;
        init.encodeHeight      = config.height;
        init.darWidth          = config.width;
        init.darHeight         = config.height;
        init.frameRateNum      = config.fps_num;
        init.frameRateDen      = config.fps_den;
        init.enableEncodeAsync = 0;   // synchronous — encode returns when output is ready
        init.enablePTD         = 1;   // NVENC decides I/P frame placement
        init.tuningInfo        = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
        init.encodeConfig      = &enc;

        s = api.nvEncInitializeEncoder(session, &init);
        if (s == NV_ENC_SUCCESS) {
            // Save state for reconfiguration; strip the dangling encodeConfig pointer.
            saved_cfg            = enc;
            saved_init           = init;
            saved_init.encodeConfig = nullptr;
        }
        return s;
    }

    // -----------------------------------------------------------------------
    // Output bitstream buffer pool creation
    // -----------------------------------------------------------------------
    NVENCSTATUS create_output_bufs() {
        for (int i = 0; i < kBitstreamPoolSize; ++i) {
            NV_ENC_CREATE_BITSTREAM_BUFFER bb = {};
            bb.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
            NVENCSTATUS s = api.nvEncCreateBitstreamBuffer(session, &bb);
            if (s != NV_ENC_SUCCESS) { return s; }
            out_bufs[i] = bb.bitstreamBuffer;
        }
        return NV_ENC_SUCCESS;
    }

    // -----------------------------------------------------------------------
    // Lazy resource registration — register once per unique texture pointer.
    // The shared texture from GpuResourceManager is stable across frames,
    // so RegisterResource is called at most once per encoder session.
    // -----------------------------------------------------------------------
    bool ensure_registered(ID3D11Texture2D* tex) {
        if (tex == reg_tex && reg_res) { return true; }

        if (reg_res) {
            api.nvEncUnregisterResource(session, reg_res);
            reg_res = nullptr;
            reg_tex = nullptr;
        }

        D3D11_TEXTURE2D_DESC desc = {};
        tex->GetDesc(&desc);

        NV_ENC_REGISTER_RESOURCE reg = {};
        reg.version            = NV_ENC_REGISTER_RESOURCE_VER;
        reg.resourceType       = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        reg.resourceToRegister = tex;
        reg.width              = desc.Width;
        reg.height             = desc.Height;
        reg.pitch              = 0;  // NVENC computes stride from D3D11 texture
        reg.subResourceIndex   = 0;
        // ARGB = D3D11 BGRA_UNORM — DXGI Desktop Duplication native format
        reg.bufferFormat       = NV_ENC_BUFFER_FORMAT_ARGB;
        reg.bufferUsage        = NV_ENC_INPUT_IMAGE;

        NVENCSTATUS s = api.nvEncRegisterResource(session, &reg);
        if (s != NV_ENC_SUCCESS) {
            printf("[NVENC] RegisterResource failed: %d  tex=%p  %ux%u\n",
                   s, static_cast<void*>(tex), desc.Width, desc.Height);
            return false;
        }
        reg_res = reg.registeredResource;
        reg_tex = tex;
        return true;
    }

    // -----------------------------------------------------------------------
    // Single-frame encode: map → submit → unmap → drain bitstream
    // -----------------------------------------------------------------------
    bool encode_one(ID3D11Texture2D* tex, int64_t pts, bool force_idr) {
        if (!ensure_registered(tex)) { return false; }

        // Map registered DIRECTX resource → NV_ENC_INPUT_PTR.
        // No data movement: NVENC reads directly from NVIDIA VRAM.
        NV_ENC_MAP_INPUT_RESOURCE map = {};
        map.version            = NV_ENC_MAP_INPUT_RESOURCE_VER;
        map.registeredResource = reg_res;

        NVENCSTATUS s = api.nvEncMapInputResource(session, &map);
        if (s != NV_ENC_SUCCESS) {
            printf("[NVENC] MapInputResource failed: %d\n", s);
            return false;
        }

        NV_ENC_PIC_PARAMS pic = {};
        pic.version         = NV_ENC_PIC_PARAMS_VER;
        pic.pictureStruct   = NV_ENC_PIC_STRUCT_FRAME;
        pic.inputBuffer     = map.mappedResource;
        pic.bufferFmt       = map.mappedBufferFmt;
        pic.inputWidth      = config.width;
        pic.inputHeight     = config.height;
        pic.outputBitstream = out_bufs[out_idx];
        pic.pictureType     = NV_ENC_PIC_TYPE_UNKNOWN;  // NVENC decides I vs P
        pic.encodePicFlags  = force_idr
                            ? static_cast<uint32_t>(NV_ENC_PIC_FLAG_FORCEIDR)
                            : 0u;
        pic.inputTimeStamp  = static_cast<uint64_t>(pts);
        pic.frameIdx        = static_cast<uint32_t>(frame_idx++);

        s = api.nvEncEncodePicture(session, &pic);

        // Unmap must happen regardless of encode result.
        api.nvEncUnmapInputResource(session, map.mappedResource);

        if (s == NV_ENC_ERR_NEED_MORE_INPUT) {
            // NVENC buffering an initial frame (should not happen with frameIntervalP=1,
            // but handled defensively).
            return true;
        }
        if (s != NV_ENC_SUCCESS) {
            printf("[NVENC] EncodePicture failed: %d\n", s);
            return false;
        }

        return drain_one(pts);
    }

    // -----------------------------------------------------------------------
    // Lock bitstream, fire callback, unlock
    // -----------------------------------------------------------------------
    bool drain_one(int64_t pts) {
        NV_ENC_LOCK_BITSTREAM lock = {};
        lock.version         = NV_ENC_LOCK_BITSTREAM_VER;
        lock.outputBitstream = out_bufs[out_idx];
        lock.doNotWait       = 0;   // block — synchronous mode guarantees data is ready

        NVENCSTATUS s = api.nvEncLockBitstream(session, &lock);
        if (s != NV_ENC_SUCCESS) {
            printf("[NVENC] LockBitstream failed: %d\n", s);
            return false;
        }

        if (on_packet && lock.bitstreamSizeInBytes > 0) {
            Packet pkt;
            pkt.data        = static_cast<const uint8_t*>(lock.bitstreamBufferPtr);
            pkt.size        = lock.bitstreamSizeInBytes;
            pkt.is_keyframe = (lock.pictureType == NV_ENC_PIC_TYPE_IDR ||
                               lock.pictureType == NV_ENC_PIC_TYPE_I);
            pkt.pts         = pts;
            on_packet(pkt);
        }

        api.nvEncUnlockBitstream(session, lock.outputBitstream);
        out_idx = (out_idx + 1) % kBitstreamPoolSize;
        return true;
    }

    // -----------------------------------------------------------------------
    // EOS — flushes any internally buffered frames (none in synchronous mode
    // with frameIntervalP=1, but send anyway for correctness)
    // -----------------------------------------------------------------------
    void send_eos() {
        NV_ENC_PIC_PARAMS eos = {};
        eos.version        = NV_ENC_PIC_PARAMS_VER;
        eos.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
        api.nvEncEncodePicture(session, &eos);
    }

    // -----------------------------------------------------------------------
    // Cleanup — unregister, destroy buffers, destroy session, free DLL
    // -----------------------------------------------------------------------
    void destroy() {
        if (reg_res) {
            api.nvEncUnregisterResource(session, reg_res);
            reg_res = nullptr;
            reg_tex = nullptr;
        }
        for (int i = 0; i < kBitstreamPoolSize; ++i) {
            if (out_bufs[i]) {
                api.nvEncDestroyBitstreamBuffer(session, out_bufs[i]);
                out_bufs[i] = nullptr;
            }
        }
        if (session) {
            api.nvEncDestroyEncoder(session);
            session = nullptr;
        }
        if (dll) {
            FreeLibrary(dll);
            dll = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// NvencEncoder — public API
// ---------------------------------------------------------------------------

NvencEncoder::NvencEncoder() : impl_(std::make_unique<Impl>()) {}
NvencEncoder::~NvencEncoder() { shutdown(); }

bool NvencEncoder::init(ID3D11Device* device, const Config& cfg, PacketCallback on_packet) {
    if (!device) { return false; }

    impl_->config    = cfg;
    impl_->on_packet = std::move(on_packet);

    if (!impl_->load_dll()) { return false; }

    NVENCSTATUS s = impl_->open_session(device);
    if (s != NV_ENC_SUCCESS) {
        printf("[NVENC] OpenEncodeSessionEx failed: %d\n", s);
        return false;
    }

    s = impl_->init_encoder();
    if (s != NV_ENC_SUCCESS) {
        printf("[NVENC] InitializeEncoder failed: %d\n", s);
        return false;
    }

    s = impl_->create_output_bufs();
    if (s != NV_ENC_SUCCESS) {
        printf("[NVENC] CreateBitstreamBuffer failed: %d\n", s);
        return false;
    }

    impl_->initialized = true;
    printf("[NVENC] Ready  %ux%u@%u/%ufps  %ukbps  codec=%s\n",
           cfg.width, cfg.height, cfg.fps_num, cfg.fps_den, cfg.bitrate_kbps,
           cfg.codec == Config::Codec::H264 ? "H264" : "HEVC");
    return true;
}

void NvencEncoder::shutdown() {
    if (!impl_ || !impl_->initialized) { return; }
    flush();
    impl_->destroy();
    impl_->initialized = false;
}

bool NvencEncoder::encode_frame(ID3D11Texture2D* tex, int64_t pts, bool force_idr) {
    if (!impl_->initialized || !tex) { return false; }
    return impl_->encode_one(tex, pts, force_idr);
}

void NvencEncoder::flush() {
    if (!impl_->initialized || !impl_->session) { return; }
    impl_->send_eos();
}

bool NvencEncoder::set_bitrate(uint32_t kbps) {
    if (!impl_->initialized) { return false; }

    impl_->saved_cfg.rcParams.averageBitRate = kbps * 1000u;
    impl_->saved_cfg.rcParams.maxBitRate     = kbps * 1000u;
    impl_->saved_cfg.rcParams.vbvBufferSize  =
        kbps * 1000u / impl_->config.fps_num;

    NV_ENC_RECONFIGURE_PARAMS rp = {};
    rp.version                         = NV_ENC_RECONFIGURE_PARAMS_VER;
    rp.reInitEncodeParams              = impl_->saved_init;
    rp.reInitEncodeParams.encodeConfig = &impl_->saved_cfg;
    rp.resetEncoder                    = 0;  // hot swap — no session teardown
    rp.forceIDR                        = 0;

    NVENCSTATUS s = impl_->api.nvEncReconfigureEncoder(impl_->session, &rp);
    if (s != NV_ENC_SUCCESS) {
        printf("[NVENC] ReconfigureEncoder (bitrate %u kbps) failed: %d\n", kbps, s);
        return false;
    }
    impl_->config.bitrate_kbps = kbps;
    printf("[NVENC] Bitrate → %u kbps\n", kbps);
    return true;
}

bool NvencEncoder::is_initialized() const {
    return impl_ && impl_->initialized;
}

#endif // RJ_NVENC_AVAILABLE
} // namespace reji
#endif // RJ_PLATFORM_WINDOWS
