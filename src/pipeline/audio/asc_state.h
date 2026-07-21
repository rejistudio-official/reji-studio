// src/pipeline/audio/asc_state.h
//
// V10/L6: AudioSpecificConfig (ASC) durum makinesinin saf çekirdeği —
// header-only, link'siz test edilir (flv_audio_tag.h / pcm_convert.h deseni).
//
// Kök neden: ensure_encoder() `set_audio_config` dönüşünü (void)'e atıp
// `encoder_ready_=true`'yu "ASC işi bitti" sayıyordu. Transport o anda null'sa
// (stop_stream yarışı) ASC kaybolur, bir daha denenmez ve rj_rtmp_send_audio
// `t.asc orelse return false` ile TÜM ses frame'lerini kalıcı reddeder.
// Model: encoder hazır olmak ve ASC'nin gitmiş olması BAĞIMSIZ iki bit;
// hazır-ama-gitmemiş durumunda her drain yeniden dener.
#pragma once

namespace reji::pipeline::audio {

// drain() her turda sorar: ASC yeniden denenmeli mi?
//  - Encoder hazır değilken deneme anlamsız (ASC henüz yok).
//  - ASC gittiyse tekrar gönderilmez (transport seq_sent'i kendi yönetir).
inline bool asc_retry_needed(bool encoder_ready, bool asc_sent) {
    return encoder_ready && !asc_sent;
}

} // namespace reji::pipeline::audio
