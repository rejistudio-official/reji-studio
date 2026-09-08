# TALİMAT: V11 Kapsam Adayları Notu — Kurulum

**Kaynak:** V10 taraması sırasında kapsam listesini `git diff` ile
sıfırdan çıkarmak zaman aldı ve bir grup (orchestrator healing/telemetri
zinciri) ilk taslakta atlanmıştı — ancak `git diff` teyidiyle
yakalandı. V11'de aynı zahmeti tekrarlamamak için, kapsam listesi
tur bittikçe biriktirilecek.

**Bu talimat kod yazmaz — bir belge kurar ve mevcut birikimi işler.**

---

## Bölüm 1 — Belgeyi oluştur

`docs/V11_KAPSAM_ADAYLARI.md` — basit ve bakımı kolay bir yapı:
başlık, amaç, başlangıç noktası (V10 kapanışı), tetikleyici koşul,
aday gruplar (dosya + ne değişti + commit), kapsam dışı listesi,
bilinçli kararlar listesi, ve bakım kuralı.

**Tetikleyici (takvim değil, koşul):** Şunlardan biri gerçekleşince
V11 başlatılır — (a) Faz 3 kompozisyon tamamlandığında, (b) CI C++
build onarımı bitip build.yml yeşile döndüğünde.

## Bölüm 2 — V10'dan bugüne birikeni işle

`git log --oneline` ile V10 kapanışından (plan mühürü, 4f55118
civarı) bugüne kadarki commit'leri çıkar ve grupla. Beklenen
gruplar (teyit et, eksik/fazla varsa düzelt):

- **VFR/CFR kare tekrarı:** frame_repeat_policy.h (yeni),
  pipeline.cpp tekrar mantığı, zaman bazlı IDR, send_diag.h
  alanları (dup, copy, idrF)
- **Preview darboğaz düzeltmesi:** 2-slot staging ring,
  DO_NOT_WAIT try-map, 30Hz seyreltme
- **NVENC VBV:** vbv_bits(), init_encoder, set_bitrate reconfig yolu
- **WS Ping/Pong:** ws_server.rs kontrol çerçeveleri + regresyon
  testleri
- **SendDiag enstrümantasyonu:** send_diag.h (yeni), pipeline.cpp
  ölçüm noktaları, enc mikro-split
- **Metrik zinciri:** gpu_load_pct, apply_collector_metrics saf
  seam, drainer testleri
- **Kural motoru (L21-L23):** predictive katman, frame_drop_pct
  diriltme, SRT durum loglaması
- **Hijyen turları:** Clippy temizliği (7 dosya), derleme uyarıları,
  FrameProfiler, ShaderCache, sceneActivated
- **Altyapı:** Cargo.lock izlemeye alındı, quality.yml clippy adımı,
  scripts/daily.ps1 + weekly.ps1 + lint.ps1, PostToolUse hook

**Risk notu ekle** — özellikle şunlar için:
- run_frame hot-path'ine bu dönemde ÇOK dokunuldu (preview, kare
  tekrarı, SendDiag) — eşzamanlılık/bütçe regresyonu riski
- Yeni bir eşzamanlılık deseni girdi (2-slot ring)
- Encoder yapılandırması değişti (VBV) — ABI'ye dokunmadı ama
  davranışı değiştirdi

## Bölüm 3 — Kapsam dışı ve bilinçli kararlar

**Kapsam dışı adayları (teyit et):**
- V8/V9/V10'un zaten sertleştirdiği ve o zamandan beri değişmemiş
  alanlar (capture çekirdeği, keyed-mutex, NVENC çekirdeği)

**Bilinçli kararlar (tarayıcılar "bulgu" sanmasın):**
V10'un 12 kalemlik listesini taşı, artı bu dönemde eklenenler:
- ULTRA_LOW_LATENCY tuning bilinçli korundu (VBV tek başına
  ölçüldü, tuning kararı ertelendi)
- set_fps_limit() VBV'yi güncellemiyor — yeni formül fps-bağımsız
  olduğu için gerek yok (bilinçli)
- Metrik bitrate_kbps yapılandırılan hedefi raporluyor, ölçülen
  çıkışı değil — bilinen eksiklik, iyileştirme adayı (bu, VBV
  bug'ının 3,4 ay görünmemesinin sebebiydi)
- PipelineCharacterization NVENC'i egzersiz etmiyor (mock yol) —
  encoder davranışını doğrulayamaz

## Bölüm 4 — Bakım kuralı (belgeye yaz)

> **Her tur kapanışında:** SESSION_NOTES mühürlemesine ek olarak,
> o turda değişen dosyalar bu listeye eklenir. Tur "tamamlandı"
> sayılmadan önce bu adım yapılmalı — V10'da kapsam listesini
> sonradan çıkarmak zaman almıştı.

---

## Sabit Kurallar

- Kod değiştirme, yalnız belge oluştur.
- Grup listesini `git log` ile DOĞRULA — yukarıdaki beklenen liste
  benim hafızamdan, eksik/fazla olabilir.
- Tamamlanınca commit + push.
- `docs/README.md`'ye bir satır ekle (indeks + statü: "durum kaydı").
