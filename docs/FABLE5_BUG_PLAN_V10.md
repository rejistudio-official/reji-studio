# FABLE5_BUG_PLAN_V10.md — Reji Studio Dördüncü Nesil Bug Planı (V9-Sonrası Yeni Kod)

**Durum:** 🟠 SENTEZ İŞLENDİ — dört model taraması tamamlandı, L1-L20
atandı. Faz 0 doğrulamaları Sprint 1'den başlıyor.

**Hazırlayan:** Sentez sohbeti (dört bağımsız model raporunun
triyajı — `docs/V10_SENTEZ_TRIYAJ.md` kanonik ara belge).
**Kaynak incelemeler (2026-07-21):**
- Claude Fable 5 — 14 ana + 4 spekülatif bulgu
- Claude Opus 4.8 — öz-eleyerek ~6 net bulgu
- GLM 5.2 — uzun analiz, 2 net bulgu
- Kimi K3 — uzun analiz, 4 net bulgu (bu turda eklenen dördüncü model)

**İlişki:** V9 planı (J1-J16 + HP1-HP4) 14.07.2026'da, K-serisi
(K1-K7 Vulkan/GL interop) hemen ardından kapandı. Bu tur, **V9
kapanışından (`efe0fec`) sonra eklenen ve hiç bağımsız taramadan
geçmemiş** kod kütlesini hedefliyor: ses pipeline'ı, donanım
profilleme, kural yönetimi zinciri, WS/Ayarlar genişletmeleri, ISource
katmanı ve orchestrator healing/telemetri zinciri (Özellik#1-5 +
GetStats). Tetikleyici: son canlı GUI testlerinin bu bölgede üç gerçek
bug bulması (hot-reload kuruluş-sırası `449c084`, içe aktarım
kopyalama `c99f1b6`, dışa aktarım kör-kopyalama `e36176e`). Amaç:
Faz 3 wiring'i bu bölgeye dokunmadan önce temiz baseline.

---

## Metodoloji ve Güvenilirlik Notu

Bu belge, **henüz Claude Code tarafından doğrulanmamış** ham
bulguların sentezidir. V8/V9 boyunca defalarca kanıtlandığı gibi
(I2/I3, I29/I31, J10/J11 çürütmeleri), bir bulgunun planda yer alması
doğru olduğu anlamına gelmez — yalnızca **araştırılmaya değer** olduğu
anlamına gelir. Her madde, ele alınmadan önce güncel `master`'a karşı
Faz 0 doğrulamasından geçmelidir (proje disiplini, istisnasız).
Rapor iddiası kanıt değildir.

**Çapraz doğrulama derecesi** (bu tur dört inceleyici):
- 🟢 **4/4 veya 3/4** — üç+ inceleyicinin bağımsız bulduğu madde (en yüksek güven)
- 🟡 **2/4** — iki inceleyicinin bulduğu madde
- 🔵 **1/4** — tek inceleyicinin bulduğu, kod parçasıyla desteklenen madde (doğrulama şart)

**Numaralandırma:** Bu turun maddeleri **L** öneki alır (V8=I, V9=J,
Vulkan/GL turu=K).

---

## Kapsam Özeti

Tam liste ve kapsam-dışı kalemler: `docs/V10_TARAMA_PROMPT.md`
(kanonik). Özet — `git diff efe0fec..master`, docs hariç 88 dosya,
~7.250 satır ekleme:

| Grup | Bölge | Öne çıkan riskler |
|---|---|---|
| 1 | Ses pipeline'ı (audio/*, output_subsystem, rtmp_transport.zig) | SPSC thread sınırları, MF/COM yaşam döngüsü, yeni FFI/ABI yüzeyi |
| 2 | Donanım profilleme (profile_advisor, profiles/*.json, applyProfile) | Karar mantığı, profil-içerik tutarlılığı |
| 3 | Kural yönetimi zinciri (import/export/hot-reload, snapshot FFI) | Son üç bug'ın bölgesi — Qt dosya-sistemi hata yolları |
| 4 | WS/Ayarlar (ConnectionGuard, QTabWidget, bitrate_policy) | RAII çıkış yolları, persistence |
| 5 | ISource katmanı (i_source.h, ExistingDesktopSource) | Kontrat/adapter doğruluğu (izole, wire edilmemiş) |
| 6 | Orchestrator healing/telemetri (calibration, healing_log, sys_stats, VendorEvent) | SQLite yazma yolları, fan-out, FFI deltası |

Kapsam dışı: V8/V9/K-serisinin sertleştirdiği eski bölgeler,
`run_frame()` capture-wiring (Faz 3 değiştirecek), 12 kalemlik
bilinen/bilinçli açık listesi (prompt §3c — yanlış-pozitif önleme).

---

## Özet Tablo

| # | Madde | Doğrulama | Sprint | Durum |
|---|---|---|---|---|
| L1 | writeValidatedRules hata-yolu zinciri (backup/restore/motor-disk ayrışması/exportRules TOCTOU) | 🟢 4/4 | 1 | ✅ FIXED c528b7c (+L1-ek 72b4b09: statik-lib qrc kaydı — L5'in açığa çıkardığı latent bug, L1 regresyonu değil) |
| L2 | Audio metrik kirliliği: FrameDropPct{0} enjeksiyonu | 🟡 2/4 | 1 | ✅ FIXED 3e1fcd4 (+CpuUsage{0} ve WS broadcast dahil) |
| L3 | Kalibrasyon hot-reload'da sessizce kayboluyor | 🔵 1/4 (Kimi) | 1 | ✅ FIXED 73d0137 (adopt_calibration) |
| L4 | Auto-reload kapalıyken import watcher'ı yeniden silahlandırıyor | 🔵 1/4 (Kimi) | 1 | ✅ FIXED 8af4f3c (enabled parametresi) |
| L5 | Çift init yolu: profil önerisi hiç tetiklenmiyor | 🔵 1/4 (Fable) + canlı kanıt | 1 | ✅ FIXED 758d155 (tek init yolu) — canlı doğrulama kullanıcıda |
| L6 | ASC kaybı yarışı: kalıcı sessiz ses ölümü | 🔵 1/4 (Fable) | 1 | ✅ FIXED fcdcb9e (asc_sent_ retry) |
| L7 | Shutdown-flush sırasında MF lazy-init SEH ihlali | 🔵 1/4 (Fable) | 1 | ❌ ÇÜRÜTÜLDÜ — on_packet streaming guard'ı drain'i keser |
| L8 | Zig ABI üst-sınır eksikliği + writeFlvTag sessiz kırpma | 🔵 1/4 (tavan) / 🟢 3/4 (kırpma) | 2 | Faz 0 bekliyor |
| L9 | MFT CAN_PROVIDE_SAMPLES yanlış yorumu + hata-yolu pSample sızıntısı | 🔵 1/4 (Fable) | 2 | Faz 0 bekliyor |
| L10 | Kanal-uyumsuzluğunda sessiz bozuk encode | 🔵 1/4 (Opus) | 2 | Faz 0 bekliyor |
| L11 | step_kbps ölü parametre (BITRATE_REDUCE sabit %15) | 🔵 1/4 (GLM) | 2 | Faz 0 bekliyor (tasarım mı unutma mı?) |
| L12 | A/V pts epoch doğrulaması (WASAPI QPC vs FramePacer::pts_us) | 🟡 2/4 | 2 | Faz 0 bekliyor (pacer okunmadan karar yok) |
| L13 | rules_buf 64KB aşımında yanıltıcı "Kural okunamadı" | 🟡 2/4 | 3 | Faz 0 bekliyor |
| L14 | HealingLog writer thread'e shutdown sinyali + son flush | 🔵 1/4 (Fable) | 3 | Faz 0 bekliyor |
| L15 | rj_action_approve kuyruk-dolu geri koymada created tazelenmeli | 🔵 1/4 (Fable) | 3 | Faz 0 bekliyor |
| L16 | pcm_scratch_.reserve init'te (hot-path realloc) | 🔵 1/4 (Fable) | 3 | Faz 0 bekliyor |
| L17 | updateParamSet dupe başarısızlığında bool dönüş | 🔵 1/4 (Fable) | 3 | Faz 0 bekliyor |
| L18 | Profil önerisi diyaloğunda vendor/VRAM eşleşmezliği | 🔵 1/4 (Kimi) | 3 | Faz 0 bekliyor |
| L19 | AudioRing dropped_ sayacı doluluk/geçersiz-girdi ayrımı | 🔵 1/4 (Fable) | 3 | Faz 0 bekliyor |
| L20 | hot_reload throttle "Ok ama skip" sözleşmesi (ölü kod tuzağı) | 🔵 1/4 (Fable) | 3 | Faz 0 bekliyor |

---

## Sprint 1 — Yüksek değer/etki (çapraz-doğrulanmış + kritik tekiller)

### L1 — writeValidatedRules hata-yolu zinciri 🟢 4/4 (en güçlü örtüşme)
**Kaynak:** Fable 5 + Opus 4.8 + GLM 5.2 + Kimi K3 — dört bağımsız
model aynı bölgeyi işaret etti (son üç GUI bug'ının tam bölgesi).
**Konum:** `src/ui/main_window.cpp` — `writeValidatedRules`, `exportRules`
**Açıklama:** Dört alt-madde:
- (a) Adım 2: `QFile::copy(dest, backup)` dönüşü kontrolsüz — yedek
  alınamazsa sessizce devam (Opus/GLM).
- (b) Adım 3: `QFile::remove(dest)` + `copy(src,dest)` başarısızsa
  rules.json diskte YOK, backup'tan restore adımı yok (Fable/Opus).
- (c) Motor-disk ayrışması: `validateRulesFile` yan etkisiyle motor
  YENİ kuralları yüklemiş durumda; yazma başarısız olunca kullanıcıya
  "dosya uygulanmadı — eski kurallar korunuyor" denir ama motor fiilen
  yeni kurallarla çalışır, restart'a kadar (Kimi — en net formülasyon).
- (d) exportRules: hedef önce silinir, kopya başarısızsa kullanıcının
  eski yedeği geri dönüşsüz gitmiş (Fable — TOCTOU).
**Önerilen düzeltme:** QSaveFile/geçici-ad + atomik rename deseni;
backup başarısızsa asıl dosyaya dokunma; copy-fail'de backup restore +
motor reload; hata mesajını gerçek duruma uydur.
**Faz 0 (2026-07-21): ✅ DOĞRULANDI — dört alt-madde de kodda mevcut.**
(a) `main_window.cpp:1020-1021` copy dönüşü kontrolsüz.
(b) `:1028-1031` remove+copy başarısızsa restore yok, rules.json diskte yok.
(c) `validateRulesFile:1004` doğrulamayı `rj_reload_rules(tmpPath)` ile
yapıyor → CANLI motora temp-dosya kuralları yüklenir (yan etki). Rapordan
GENİŞ: yalnız hata yolunda değil — exportRules dahil HER doğrulama motoru
temp dosyaya çevirir; başarı yolunda ikinci reload (gerçek path) düzeltir
ama hata yolunda motor temp kurallarında kalır ve engine.file_path silinmiş
QTemporaryFile'ı gösterir. (d) `exportRules:955-961` remove-sonra-copy,
kopya başarısızsa eski yedek geri dönüşsüz gitmiş.

### L2 — Audio metrik kirliliği: FrameDropPct{0} enjeksiyonu 🟡 2/4
**Kaynak:** Opus 4.8 + Kimi K3
**Konum:** `src/orchestrator/src/ffi.rs` — drainer, `media_events_for_sample`
**Açıklama:** Ses metrik örnekleri (source_id=1, 1Hz)
frame_drop_pct=0 taşıyor; `media_events_for_sample` source_id ayrımı
yapmadan koşulsuz FrameDropPct yayınlıyor (frame_drop plumbing
V10-yeni) → videonun gerçek drop sinyali ~1Hz'de sıfırlanıyor → sahte
`frame_drop_recovery` tetiklenmesi + gerçek drop kurallarının
bastırılması. Kimi'nin ek gözlemi: MemUsage'ın >0 guard'ı bu sorunu
zaten biliyor (asimetri kanıtı); audio frame_drops'un (ses
glitch'leri) video FrameDropped trendine karışması ikincil sorun.
**Önerilen düzeltme:** Drainer'da media event üretimini source_id==0'a
(video) sınırla.
**Faz 0 (2026-07-21): ✅ DOĞRULANDI + rapordan geniş.**
`wasapi_capture.cpp:574-585` audio örneği `RjMetricSample s{}` (sıfır-init,
frame_drop_pct=0, cpu_load_pct=0) + source_id=1 push eder;
`ffi.rs:421-432 media_events_for_sample` ve `:501` drainer çağrısı
source_id ayrımı yapmaz → FrameDropPct{0} 1Hz enjeksiyonu doğru. EK-1:
`system_events_for_sample:386` CpuUsage'ı KOŞULSUZ yayar → audio örnekleri
CpuUsage{0} da enjekte eder (aynı sınıf, raporlarda yok). EK-2: drainer WS
JSON broadcast'i (`:483-490`) audio örneklerini ayrımsız yayınlar → WS
istemcilerine 1Hz sahte fps/kbps karışır. MemUsage >0 guard asimetrisi
(Kimi) doğru.

### L3 — Kalibrasyon hot-reload'da sessizce kayboluyor 🔵 1/4 (Kimi — kod-kanıtlı)
**Konum:** `src/orchestrator/src/ffi.rs` (`rj_reload_rules`),
`src/orchestrator/src/healing.rs` (HealingMonitor, `calibration_done`)
**Açıklama:** `rj_reload_rules` yepyeni RuleEngine kurar → Özellik
#5'in kalibre eşiği (engine-içi tablo) atılır; HealingMonitor
`calibration_done=true` olduğundan bir daha uygulamaz. Herhangi bir
hot-reload/import/profil-uygulama, kalibrasyonu kalıcı siler —
kurallar statik eşiklere döner, `[kalibre]` etiketi kaybolur,
kullanıcıya hiçbir sinyal yok.
**Önerilen düzeltme:** Kalibrasyon tablosunu engine dışında yaşat veya
engine değişiminde HealingMonitor yeniden uygulasın.
**Faz 0 (2026-07-21): ✅ DOĞRULANDI.** `rules.rs:352` RuleEngine::new boş
CalibrationTable kurar; `ffi.rs:1367-1375` rj_reload_rules engine'i
komple değiştirir (tablo taşınmaz); `healing.rs:444-453` calibration_done
guard'ı finalize'ı tek seferlik yapar (test: calibration_finalizes_only_once)
→ herhangi bir reload sonrası kalibrasyon kalıcı kaybolur, sinyal yok.
L1(c) ile etkileşim: her import/export doğrulaması da engine'i değiştirdiği
için kayıp penceresi rapordakinden bile geniş.

### L4 — Auto-reload kapalıyken import watcher'ı yeniden silahlandırıyor 🔵 1/4 (Kimi)
**Konum:** `src/ui/main_window.cpp` / `src/ui/rules_watch.h` — `armRulesWatch`
**Açıklama:** Kullanıcı auto-reload'u açıp kapatırsa (watcher nesnesi
var, path'ler temizlenmiş), sonra import yaparsa:
`writeValidatedRules`/`reloadRulesNow` içindeki `armRulesWatch()`
çağrıları path'leri geri ekler → **checkbox kapalıyken** harici
düzenlemeler sessizce hot-reload olur. `armRulesWatch` içinde
`isAutoReloadEnabled` kontrolü yok. Bizim 449c084 düzeltmemizin
(yeniden-silahlandırma ekleyen) etkileşim alanı.
**Önerilen düzeltme:** `armRulesWatch` girişine enabled kontrolü.
**Faz 0 (2026-07-21): ✅ DOĞRULANDI.** `armRulesWatch` (main_window.cpp:863)
yalnız `rules_watcher_` null kontrolü yapar, enabled kontrolü yok.
Gate'siz çağıranlar: `reloadRulesNow:902` (koşulsuz) ve
`writeValidatedRules:1037-1039` (tam da watcher İNAKTİFKEN çalışan dal).
`onAutoReloadToggled(false):873-882` path'leri siler ama watcher nesnesi
yaşar → toggle aç-kapat + import senaryosunda path'ler geri eklenir,
`onRulesPathChanged:896` debounce'u yeniden başlatır → checkbox kapalıyken
sessiz hot-reload. Tek gate'li çağıran: `:825-828` (doğru desen).

### L5 — Çift init yolu: profil önerisi hiç tetiklenmiyor 🔵 1/4 (Fable) + CANLI KANIT
**Konum:** `src/ui/main_window.cpp` — ctor vs `initPipeline()`
**Açıklama:** `maybeSuggestProfileOnFirstRun` tetikleyicisi yalnız
`MainWindow::initPipeline()` içinde; ctor kendi `pipeline_->init(pcfg)`
yolunu kullanıyor ve orada singleShot YOK. Gerçek akış ctor'daysa
ilk-kurulum önerisi hiç görünmez. Ayrıca iki giriş noktasının yan-etki
kümeleri farklı: ctor frame thread başlatır ama singleShot yok;
initPipeline singleShot var ama frame thread yok — ikinci init
senaryosunda frame thread'siz "init'li ama kare dönmüyor" durumu.
**Kullanıcı çapraz-kontrolü (yapıldı):** Kullanıcı profil önerisi
diyaloğunu hiç GÖRMEDİĞİNİ teyit etti — bulgunun canlı kanıtı mevcut.
Faz 0 kod izini doğrulamakla sınırlı; doğrudan düzeltme tasarımına
geçilebilir.
**Önerilen düzeltme:** Tek init yoluna birleştirme + frame-thread yan
etkisinin iki yolda da tutarlı olması.
**Faz 0 (2026-07-21): ✅ DOĞRULANDI — rapordan da kesin.**
`initPipeline`'ın repo genelinde SIFIR çağıranı var (yalnız tanımlar:
main_window.cpp:292 Qt6, :1288 stub, header bildirimi) → ölü kod. Gerçek
akış ctor'un doğrudan `pipeline_->init(pcfg)` yolu (:172), singleShot
yok → `maybeSuggestProfileOnFirstRun` HİÇBİR akışta tetiklenmiyor.
Kullanıcının "diyaloğu hiç görmedim" canlı kanıtıyla birebir uyumlu.
Frame thread yalnız ctor'da (:251-261) başlar; iki tanım #if QT6_AVAILABLE
dallarında (çakışma değil).

### L6 — ASC kaybı yarışı: kalıcı sessiz ses ölümü 🔵 1/4 (Fable — dar pencere, kalıcı etki)
**Konum:** `src/pipeline/audio/audio_encode_bridge.cpp`
(`ensure_encoder` → `set_audio_config`), `src/pipeline/rtmp/rtmp_transport.zig`
(`rj_rtmp_send_audio`)
**Açıklama:** `ensure_encoder` → `set_audio_config` dönüşü kontrol
edilmiyor (`(void)`); transport o anda null'sa (stop_stream yarışı)
ASC kaybolur, `encoder_ready_=true` kaldığından bir daha denenmez →
`rj_rtmp_send_audio` `t.asc orelse return false` ile TÜM ses
frame'lerini kalıcı reddeder. Yeniden start_stream'de ses ölü kalır.
**Önerilen düzeltme:** Dönüşü sakla, başarısızsa sonraki drain'de
yeniden dene (asc_sent_ bayrağı).
**Faz 0 (2026-07-21): ✅ DOĞRULANDI.**
`audio_encode_bridge.cpp:55` `(void)out_->set_audio_config(...)` +
`:56` koşulsuz `encoder_ready_=true`; `output_subsystem.cpp:101-104`
transport_atomic_ null'sa false döner (stop_stream `set_streaming(false)`
ile null'lar); `rtmp_transport.zig:390` `t.asc orelse return false` —
ASC'siz TÜM ses frame'leri reddedilir. encoder_ready_ yalnız bridge
shutdown'ında sıfırlanır → stop/start döngüsünde ASC bir daha denenmez.
Yarış penceresi dar (ilk drain × stop_stream) ama etki kalıcı — iddia doğru.

### L7 — Shutdown-flush sırasında MF lazy-init SEH ihlali 🔵 1/4 (Fable)
**Konum:** `src/pipeline/pipeline.cpp` (`seh_shutdown_subsystems`),
`src/pipeline/audio/audio_encode_bridge.cpp` (`drain`)
**Açıklama:** `seh_shutdown_subsystems` içindeki `enc->flush()` kalan
paketleri boşaltır → `on_packet` → `audio_bridge_.drain()` → hiç ses
akmamışsa **shutdown anında** MFStartup/MFT create tetiklenir —
`__try` scope'unda C++ destructor'lı nesnelerle MF çağrısı (proje SEH
disiplini kural 4 ihlali). `audio_bridge_.shutdown()` flush'tan SONRA
çalıştığından `enabled_` hâlâ true.
**Önerilen düzeltme:** `enabled_=false` store'unu (veya bridge
shutdown'ı) seh_shutdown_subsystems'ten ÖNCE al; drain'e "shutdown
başladı" kontrolü.
**Faz 0 (2026-07-21): ❌ ÇÜRÜTÜLDÜ.** İddia edilen zincir
(`enc->flush()` → on_packet → `audio_bridge_.drain()`) shutdown'da
kopuk: `pipeline.cpp:750` `streaming=false` store'u `seh_shutdown_subsystems`
çağrısından (:767) ÖNCE gelir ve on_packet `:297`'de
`if (!self->streaming.load(...)) return;` guard'ı drain'e (:305)
ulaşmadan erken döner → __try scope'unda MF lazy-init tetiklenemez.
Fable'ın "enabled_ hâlâ true" gözlemi doğru ama etkisiz — drain hiç
çağrılmaz. Latent not: guard kaldırılırsa tuzak geri gelir; on_packet'teki
guard'ın shutdown-sırası sözleşmesi kod yorumuyla belgelenebilir
(düzeltme yok, hijyen adayı).

---

## Sprint 2 — Orta öncelik

### L8 — Zig ABI üst-sınır eksikliği 🔵 1/4 (tavan) / 🟢 3/4 (writeFlvTag kırpma)
**Konum:** `src/pipeline/rtmp/rtmp_transport.zig` —
`rj_rtmp_send_audio`/`set_audio_config`/`rj_rtmp_send`, `writeFlvTag`
**Açıklama:** Boyut tavanı yok — bozuk usize sınırsız slice +
`@memcpy` (J1 `cstr_bounded` dersi bu yüzeyde uygulanmamış;
prompt'un özel dikkat çağrısı #3'e doğrudan cevap). Ek: `writeFlvTag`
`body.len & 0xFFFFFF` sessiz kırpma (Fable/Opus/GLM) — 16MB üstü
body'de bozuk tag; maske yerine reddet.

### L9 — MFT CAN_PROVIDE_SAMPLES yanlış yorumu + pSample sızıntısı 🔵 1/4 (Fable)
**Konum:** `src/pipeline/audio/` — MFT çıkış örnekleme mantığı
(`output_provides_samples_`)
**Açıklama:** `output_provides_samples_` CAN_PROVIDE bitini de
"provides" sayıyor — CAN_PROVIDE'da caller buffer sağlamalı;
sağlamazsa MFT reddedip ses kalıcı kopabilir. Ayrıca
FAILED/NEED_MORE_INPUT dallarında MFT-provided pSample release
edilmiyor (teorik sızıntı).

### L10 — Kanal-uyumsuzluğunda sessiz bozuk encode 🔵 1/4 (Opus)
**Konum:** `src/pipeline/audio/` — encoder kanal varsayımı
**Açıklama:** Cihaz mono/farklı kanal verirse encoder sabit 2ch
varsayımıyla yanlış frame sayısı/interleaving üretir — bilinen #5
(resampling yok) yalnız sample-rate'i kapsıyor, kanal boyutu ayrı ve
sessiz bozulma üretiyor.
**Önerilen düzeltme:** Format uyuşmazlığında ses yolunu güvenli kapat.

### L11 — step_kbps ölü parametre 🔵 1/4 (GLM — Donanım Profilleme değer kaybı)
**Konum:** `src/orchestrator/` — RuleEngine param1 taşıma,
`apply_action` BITRATE_REDUCE
**Açıklama:** Kural motoru step_kbps'i param1'e özenle taşıyor;
`apply_action` BITRATE_REDUCE'da bunu YOK SAYIP sabit %15 uyguluyor →
üç profilin mild(300)/high(750) step ayrımı fiilen çalışmıyor, ölü
konfigürasyon.
**Faz 0 sorusu:** Bilinçli tasarım mı (yüzde-tabanlı tercih) yoksa
unutulmuş bağlantı mı? Bilinçliyse profillerden step_kbps kaldırılmalı
(yanıltıcı), değilse param1 kullanılmalı.

### L12 — A/V pts epoch doğrulaması 🟡 2/4 (Fable orta + Opus spekülatif)
**Konum:** WASAPI capture pts (QPC-mutlak) vs `FramePacer::pts_us`
(`frame_pacer.h/cpp` — tarama kapsamı dışıydı)
**Açıklama:** İki tabanın aynı olduğu DOĞRULANAMADI. Tabanlar
farklıysa ilk-gelen epoch'u belirler, diğer akış ts=0'a yapışır veya
saatlerce kayar; drift valfi sürekli uyarır ama düzeltmez.
**Faz 0:** `frame_pacer.h/cpp` okunarak kesinleştirilecek — aynıysa
çürüt, farklıysa düzelt. (Fable raporunun kendisi de "pacer görülmeden
kesinleşmez" diye işaretledi.)

---

## Sprint 3 — Düşük öncelik / hijyen

- **L13** 🟡 2/4 (Fable+Opus) — `rules_buf` 64KB aşımında yanıltıcı
  "Kural okunamadı"; boyut-aşımı ile motor-hazır-değil ayrımı.
- **L14** 🔵 (Fable) — HealingLog writer thread'e shutdown sinyali +
  son flush; düzenli kapanışta son 250ms kaybı.
- **L15** 🔵 (Fable) — `rj_action_approve` kuyruk-dolu geri koymada
  `created` tazelenmeli; onaylanan aksiyon anında TTL'e düşebilir.
- **L16** 🔵 (Fable) — `pcm_scratch_.reserve` init'te; hot-path realloc.
- **L17** 🔵 (Fable) — `updateParamSet` dupe başarısızlığında bool dönüş.
- **L18** 🔵 (Kimi) — Profil önerisi diyaloğunda vendor/VRAM
  eşleşmezliği; display vendor (iGPU) + max VRAM (dGPU) yan yana
  "Intel 12GB" gibi yanıltıcı gösterim.
- **L19** 🔵 (Fable) — AudioRing `dropped_` sayacı doluluk/geçersiz-girdi ayrımı.
- **L20** 🔵 (Fable) — `hot_reload` throttle "Ok ama skip" sözleşmesi;
  fiilen ölü kod, ileride tuzak; ayırt edilebilir dönüş.

---

## Çürütülen / Elenenlerin Kaydı

- **COM-init-frame-thread** — Kimi kendisi eledi (MFStartup COM init
  eder, test kanıtı destekliyor). İzlenecek: Faz 0'da bir kez
  doğrulanabilir, düşük maliyet.
- **AudioRing SPSC ordering** — 3 model bağımsız doğruladı, temiz.
- **nextNal 3/4-byte prefix** — GLM kendisi eledi (EBSP geçerli
  H.264'te sorun değil).
- **qpc_now_us overflow** — Opus + Kimi bağımsız doğruladı, güvenli.
- **ExistingDesktopSource katmanı** — iki model bağımsız temiz buldu
  (henüz wire edilmemiş olduğu notuyla).
- **capture_loop clamp/ReleaseBuffer** — Opus + GLM doğruladı, doğru.

## Spekülatif Havuz (düzeltme yok, kayıt)

- Settings ctor senkron COM enumerasyonu (2 model, spekülatif) —
  yavaş BT ses sürücülerinde açılış takılması.
- frame_thread busy-spin (Fable, kapsam-sınırında) — capture yoksa
  %100 çekirdek; wiring turunda ele alınabilir.
- NOTIFY_END_OF_STREAM + COMMAND_DRAIN sırası (Fable).

---

## Sabit Kurallar

- Her L maddesi Faz 0 doğrulamasından geçer — dört model de yanılabilir
  (V9'da J10/J11 çürütülmüştü); rapor iddiası kanıt değildir.
- L5'in kullanıcı çapraz-kontrolü yapıldı: diyalog hiç görülmedi
  (canlı kanıt) — Faz 0 kod izi doğrulamasıyla sınırlı.
- L12 pacer kodu okunmadan karara bağlanmaz.
- CLAUDE.md Bölüm 8b dal disiplini; sprint başına dal değerlendirmesi.
- `tests/baseline_metrics.txt` asla commit edilmez.
- Her düzeltme "test edildi / kod incelemesiyle / kullanıcıda"
  ayrımıyla raporlanır.

---

## Süreç Notları

- [x] Dört model taraması çalıştırıldı (kullanıcı) — sentez:
      `docs/V10_SENTEZ_TRIYAJ.md`
- [x] Sentez yapıldı, L-numaraları atandı (L1-L20 + çürütülen kayıt +
      spekülatif havuz)
- [ ] Linear'da V10 issue açıldı _(bkz. talimat Bölüm D)_
- [x] Sprint'lere bölündü (S1: L1-L7, S2: L8-L12, S3: L13-L20)
- [x] Sprint 1 Faz 0 tamamlandı (L1-L6 doğrulandı, L7 çürütüldü)
- [x] Sprint 1 düzeltmeleri `feat/v10-sprint1` dalında (Bölüm 8b: çok
      commit + güvenlik-hassas → feature dalı): L2 3e1fcd4, L3 73d0137,
      L1 c528b7c, L4 8af4f3c, L6 fcdcb9e, L5 758d155. Testler: Rust
      132+5+35 PASS, RulesWatchTest 4/4, AudioWireTest 10/10,
      OutputSubsystemTest 7/7, PipelineCharacterization 1/1, reji_app
      build OK. Merge + push kullanıcı onayı bekliyor; L5/L1 GUI
      akışları canlı testte doğrulanacak.
- [x] L1-ek (72b4b09, `docs/ACIL_L1_QRC_REGRESYON.md`): canlı profil-uygula
      denemesi ":/config/profiles/*.json bulunamadı" verdi. Kök neden L1
      regresyonu DEĞİL — statik reji_ui.lib'de qrc nesnesi linker'ca
      atılıyordu (self-registration hiç çalışmadı); applyProfile L5'e dek
      ölü yol olduğundan hiç görünmemişti, şablon tohumlama da latent
      kırıktı. Düzeltme: ensureResourcesRegistered (Q_INIT_RESOURCE).
      Test boşluğu kapatıldı: QrcResourcesTest reji_ui'yi uygulamayla aynı
      biçimde link'ler; negatif kontrol canlı hatayı birebir üretti.
      Kullanıcı yeniden denemeli: öneri diyaloğunda "Uygula".
- Tamamlanınca `TALIMAT_V10_TARAMA_HAZIRLIK.md` → `docs/talimatlar/`
  arşivine taşınacak.
