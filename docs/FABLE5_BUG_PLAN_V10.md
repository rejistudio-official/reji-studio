# FABLE5_BUG_PLAN_V10.md — Reji Studio Dördüncü Nesil Bug Planı (V9-Sonrası Yeni Kod)

**Durum:** ✅ **TAMAMEN KAPANDI (2026-07-30, canlı doğrulamalar dahil)** —
üç sprint de merge edildi (S1: L1-L7+ekler, S2: L8-L12+L21-L23,
S3: L13-L20). Çürütmeler: L7, L9'un CAN_PROVIDE kısmı. Açık kalan son
iki canlı GUI doğrulaması (L14, L18) 30.07'de tamamlandı; L18'in canlı
doğrulaması bir ek bulgu çıkardı (GpuScan WGC yolunda boş —
`TALIMAT_L18_GPUSCAN_BOYUT.md`, fix `ae360fd`, aynı gün canlı
doğrulandı). V10 sonrası bulgular V11 planına gider ("Capture loss
60 frames" null-streak notu dahil).

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
| L5 | Çift init yolu: profil önerisi hiç tetiklenmiyor | 🔵 1/4 (Fable) + canlı kanıt | 1 | ✅ FIXED 758d155 (tek init yolu) — canlı doğrulandı (30.07: profile/asked sıfırlanınca ilk-kurulum diyaloğu tetiklendi) |
| L6 | ASC kaybı yarışı: kalıcı sessiz ses ölümü | 🔵 1/4 (Fable) | 1 | ✅ FIXED fcdcb9e (asc_sent_ retry) |
| L7 | Shutdown-flush sırasında MF lazy-init SEH ihlali | 🔵 1/4 (Fable) | 1 | ❌ ÇÜRÜTÜLDÜ — on_packet streaming guard'ı drain'i keser |
| L8 | Zig ABI üst-sınır eksikliği + writeFlvTag sessiz kırpma | 🔵 1/4 (tavan) / 🟢 3/4 (kırpma) | 2 | ✅ FIXED 973dd19 (ABI boyut tavanları + writeFlvTag kırpma yerine reddet; merge 45db244) |
| L9 | MFT CAN_PROVIDE_SAMPLES yanlış yorumu + hata-yolu pSample sızıntısı | 🔵 1/4 (Fable) | 2 | ✅ FIXED 3cb573e (savunmacı pSample release) — CAN_PROVIDE yorumu ❌ ÇÜRÜTÜLDÜ (bkz. Çürütülenler) |
| L10 | Kanal-uyumsuzluğunda sessiz bozuk encode | 🔵 1/4 (Opus) | 2 | ✅ FIXED 53363f2 (format_gate — uyuşmazlıkta ses yolu güvenli kapanır) |
| L11 | step_kbps ölü parametre (BITRATE_REDUCE sabit %15) | 🔵 1/4 (GLM) | 2 | ✅ FIXED 9497b33 (unutulmuş bağlantıydı — param1/step_kbps REDUCE/RECOVER'da kullanılır) |
| L12 | A/V pts epoch doğrulaması (WASAPI QPC vs FramePacer::pts_us) | 🟡 2/4 | 2 | ✅ FIXED 9b29dc9 (tabanlar farklıydı — ses pts'i pacer origin'ine rebase) |
| L13 | rules_buf 64KB aşımında yanıltıcı "Kural okunamadı" | 🟡 2/4 | 3 | ✅ FIXED 208e274 (-2 dönüş kodu + UI'da ayrı mesaj) |
| L14 | HealingLog writer thread'e shutdown sinyali + son flush | 🔵 1/4 (Fable) | 3 | ✅ FIXED a0a9b33 (Condvar + rj_healing_log_shutdown) — canlı doğrulandı (30.07: normal kapanışta healing_log.sqlite kapanış anında flush, son olaylar mevcut) |
| L15 | rj_action_approve kuyruk-dolu geri koymada created tazelenmeli | 🔵 1/4 (Fable) | 3 | ✅ FIXED 208e274 (Instant::now ile tazeleme) |
| L16 | pcm_scratch_.reserve init'te (hot-path realloc) | 🔵 1/4 (Fable) | 3 | ✅ FIXED c8efdfd (8192 reserve + static_assert kilidi) |
| L17 | updateParamSet dupe başarısızlığında bool dönüş | 🔵 1/4 (Fable) | 3 | ✅ FIXED fa9749f (bool + rj_rtmp_send'de dürüst red) |
| L18 | Profil önerisi diyaloğunda vendor/VRAM eşleşmezliği | 🔵 1/4 (Kimi) | 3 | ✅ FIXED 35eb914 (max_vram_vendor_id) + ek fix ae360fd (GpuScan WGC'de boştu) — canlı doğrulandı (30.07: NVIDIA/7948MB → Performance uygulandı) |
| L19 | AudioRing dropped_ sayacı doluluk/geçersiz-girdi ayrımı | 🔵 1/4 (Fable) | 3 | ✅ FIXED ca06936 (dropped_full/rejected_invalid) |
| L20 | hot_reload throttle "Ok ama skip" sözleşmesi (ölü kod tuzağı) | 🔵 1/4 (Fable) | 3 | ✅ FIXED 32e732c (ReloadOutcome enum) |
| L21 | Predictive katman gönderim-hatasını yük sanıyor | 🟢 canlı kanıt | 2 | ✅ FIXED ecf9b99 (bağlantı-yokluğu drop'ları predictive trendden ayrıldı) — canlı doğrulandı (28.07: bitrate düşüşü yok) |
| L22 | frame_drop_pct ölü metrik: kural motoru kör | 🟢 canlı kanıt | 2 | ✅ FIXED ec24d2a (on_packet → record_frame/record_frame_drop besler) — canlı doğrulandı (28.07: drop=0% kesintisiz) |
| L23 | SRT bağlantı durumu gözlemlenebilirliği | 🔵 hijyen | 2 | ✅ FIXED 67d9258 (durum geçişleri run.log'a) — canlı doğrulandı (28.07: srt_connect_failed logu) |

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
**Durum (2026-07-28): ✅ FIXED 973dd19** (`feat/v10-l8-zig-abi-bounds`, merge
45db244) — ABI boyut tavanları eklendi, writeFlvTag 16MB üstünü maskelemek
yerine reddediyor.

### L9 — MFT CAN_PROVIDE_SAMPLES yanlış yorumu + pSample sızıntısı 🔵 1/4 (Fable)
**Konum:** `src/pipeline/audio/` — MFT çıkış örnekleme mantığı
(`output_provides_samples_`)
**Açıklama:** `output_provides_samples_` CAN_PROVIDE bitini de
"provides" sayıyor — CAN_PROVIDE'da caller buffer sağlamalı;
sağlamazsa MFT reddedip ses kalıcı kopabilir. Ayrıca
FAILED/NEED_MORE_INPUT dallarında MFT-provided pSample release
edilmiyor (teorik sızıntı).
**Durum (2026-07-28): kısmi ✅ FIXED 3cb573e / kısmi ❌ ÇÜRÜTÜLDÜ** (merge
dbf4bc8) — CAN_PROVIDE yorumu Faz 0'da çürütüldü (Çürütülenler kaydına
işlendi); hata-yolu pSample sızıntısı gerçekti, savunmacı release eklendi.

### L10 — Kanal-uyumsuzluğunda sessiz bozuk encode 🔵 1/4 (Opus)
**Konum:** `src/pipeline/audio/` — encoder kanal varsayımı
**Açıklama:** Cihaz mono/farklı kanal verirse encoder sabit 2ch
varsayımıyla yanlış frame sayısı/interleaving üretir — bilinen #5
(resampling yok) yalnız sample-rate'i kapsıyor, kanal boyutu ayrı ve
sessiz bozulma üretiyor.
**Önerilen düzeltme:** Format uyuşmazlığında ses yolunu güvenli kapat.
**Durum (2026-07-28): ✅ FIXED 53363f2** (merge dbf4bc8) — `format_gate.h`:
kanal/örnekleme uyuşmazlığında ses yolu güvenli kapanır (sessiz bozulma yok).

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
**Durum (2026-07-28): ✅ FIXED 9497b33** (merge dbf4bc8) — Faz 0 cevabı:
unutulmuş bağlantı; REDUCE/RECOVER artık param1/step_kbps profil adımını
kullanıyor, üç profilin step ayrımı fiilen çalışıyor.

### L12 — A/V pts epoch doğrulaması 🟡 2/4 (Fable orta + Opus spekülatif)
**Konum:** WASAPI capture pts (QPC-mutlak) vs `FramePacer::pts_us`
(`frame_pacer.h/cpp` — tarama kapsamı dışıydı)
**Açıklama:** İki tabanın aynı olduğu DOĞRULANAMADI. Tabanlar
farklıysa ilk-gelen epoch'u belirler, diğer akış ts=0'a yapışır veya
saatlerce kayar; drift valfi sürekli uyarır ama düzeltmez.
**Faz 0:** `frame_pacer.h/cpp` okunarak kesinleştirilecek — aynıysa
çürüt, farklıysa düzelt. (Fable raporunun kendisi de "pacer görülmeden
kesinleşmez" diye işaretledi.)
**Durum (2026-07-28): ✅ FIXED 9b29dc9** (merge dbf4bc8) — Faz 0 cevabı:
tabanlar farklıydı; ses pts'i pacer origin'ine rebase edildi (A/V epoch
eşitlendi).

### L21 — Predictive katman gönderim-hatasını yük sanıyor 🟢 canlı kanıt (Faz 0 reduce-tetikleyici teşhisi)
**Konum:** `src/orchestrator/src/healing.rs:657-664` (frame-drop trendi),
`src/pipeline/pipeline.cpp:307-310` (`on_packet` send-fail → drop),
`src/pipeline/output/srt_output.cpp:390` (`connected=false` → her send false)
**Açıklama:** START'a basıldığında SRT bağlantısı yoksa (caller-mode
bağlanamamış, `connected=false`) her encode paketi drop sayılır →
`FrameDropped` seli → predictive `ReduceBitrate(3500)`. Canlı kanıt
(`run.log` 22.07 22:28): cpu %14-18, gpu %20-22, drop %0 iken
"streaming started"dan ~2-3 sn sonra 6000→3500. Bitrate düşürmek
tıkanıklık tedavisidir, "bağlantı yok"u çözmez; streaming alıcısız
sürseydi 60 sn cooldown'la salınım üretirdi. S1-ek4 ikincil bulgusu
(b) (healing aktüasyonunda yayın-durumu kapısı yok) bu maddenin
kapsamına komşu.
**Önerilen düzeltme:** Gönderim-hatası drop'larını predictive
trendden ayır; bağlantı-yok/koptu durumunu mevcut
`notify_connection_lost` → fallback yoluna yönlendir. Kendi Faz
0/1'ini hak eden orta-büyük mimari iş.
**Durum (2026-07-28): ✅ FIXED ecf9b99** (`feat/v10-sprint2-grupA`, merge
612a357) — bağlantı-yokluğu drop'ları predictive trendden ayrıldı. **Canlı
doğrulandı (28.07):** SRT bağlantısız START'ta NVENC bitrate düşüşü hiç yok.

### L22 — frame_drop_pct ölü metrik: kural motoru kör 🟢 canlı kanıt (S1-ek4 ikincil bulgu (a)'nın resmileşmesi)
**Konum:** `src/pipeline/metrics_collector.cpp:93-101`
(`record_frame`/`record_frame_drop` — çağıran YOK),
`docs/config/profiles/*.json` (üç frame_drop kuralı)
**Açıklama:** `frame_drop_pct` daima 0 → `frame_drop_mild/high`
reduce kuralları HİÇ tetiklenmez, `frame_drop_recovery` (`< 3`)
KOŞULSUZ tetiklenir (healing_log.sqlite: 54/54 kayıt
frame_drop_recovery, current_value=0). S1 turundaki 3500→6000
iyileşmesi bu koşulsuz kural sayesinde çalıştı — tasarım gereği
değil, tesadüfen.
**Faz 0 sorusu:** Metrik diriltilmeli mi (Impl::frame_drops
zincirinden beslemek bir aday) yoksa üç kural profillerden
çıkarılmalı mı? İkisi birden olmaz — koşulsuz recovery şu an
fiili emniyet supabı, kaldırmadan önce recovery'nin gerçek
tetikleyicisi tanımlanmalı.
**Durum (2026-07-28): ✅ FIXED ec24d2a** (merge 612a357) — Faz 0 cevabı:
metrik diriltildi; `on_packet` → `record_frame`/`record_frame_drop` besler,
kurallar profillerde kaldı. **Canlı doğrulandı (28.07):** drop=0% kesintisiz.

### L23 — SRT bağlantı durumu gözlemlenebilirliği 🔵 hijyen (L21 teşhisinin açık bıraktığı nokta)
**Konum:** `src/pipeline/output/srt_output.cpp` (durum geçişleri
yalnız `OutputDebugStringA`; `run.log`'a hiçbir şey yazılmıyor)
**Açıklama:** Faz 0 teşhisinde SRT bağlantı durumu loglardan
doğrulanamadı — "alıcı yoktu" sonucu eleme yöntemiyle kuruldu.
connect başarı/başarısızlık ve `connected` geçişleri (kayıp dahil)
run.log'a yazılırsa bu sınıf teşhisler doğrudan kanıtla kapanır.
**Önerilen düzeltme:** Bağlantı durum geçişlerinde tek satır dbglog
(hot-path `send_internal`'a log koyma — yalnız geçiş anları).
**Durum (2026-07-28): ✅ FIXED 67d9258** (merge 612a357) — durum geçişleri
run.log'a yazılıyor. **Canlı doğrulandı (28.07):** `[rj_srt] connection
lost: srt_connect_failed` logu doğru çıkıyor.

---

## Sprint 3 — Düşük öncelik / hijyen ✅ KAPANDI (2026-07-28)

**Faz 0 (2026-07-28): sekiz maddenin SEKİZİ de kodda doğrulandı** —
bu turda çürütme yok. İki dal (CLAUDE.md 8b): davranış-nötr/teşhis
sınıfı `feat/v10-sprint3-hijyen` (merge e2e2f6b), görünür davranış +
yeni FFI yüzeyi `feat/v10-sprint3-canli` (merge 2ae5dd6).

- **L13** ✅ 208e274 — Faz 0: `ffi.rs` tek `-1` hem init-değil hem
  cap-yetersizi örtüyordu; `settings_dialog.cpp` tek mesaj basıyordu.
  Fix: `-2` ayrı kod + `setRulesError` ile ayrı UI mesajı.
- **L14** ✅ a0a9b33 🔍 — Faz 0: `writer_loop` sonsuz `loop{sleep(250ms)}`,
  hiçbir shutdown yolu yok (orchestrator'da genel shutdown FFI'ı da yoktu).
  Fix: Condvar bekleyişi + `shutdown_writer` (sinyal+join+son flush) +
  yeni `rj_healing_log_shutdown` FFI'ı, `~MainWindow` çağırır.
  **Canlı doğrulandı (30.07):** normal kapanış (WM_CLOSE) sonrası
  healing_log.sqlite dosya zamanı kapanış anıyla birebir; son kayıt
  kapanıştan ~1 dk öncesinin memory_pressure olayı (id=111) — writer
  shutdown flush'ı çalışıyor.
- **L15** ✅ 208e274 — Faz 0: `ffi.rs:1231` geri koymada `created:
  entry.created` (PENDING_TTL 30s → anında sweep riski). Fix: `Instant::now()`.
- **L16** ✅ c8efdfd — Faz 0: `encode()` içinde `resize` ilk çağrıda
  realloc. Fix: init'te `reserve(8192)`; AudioRing üst sınırıyla
  static_assert kilidi.
- **L17** ✅ fa9749f — Faz 0: `catch null` sessiz; void dönüş. Fix: bool
  dönüş; `rj_rtmp_send` başarısızlıkta dlog + false (kare dürüstçe reddedilir).
- **L18** ✅ 35eb914 🔍 — Faz 0: `main_window.cpp` display_vendor_id (iGPU) +
  max_gpu_vram_mb (dGPU) karışımı doğrulandı. Fix:
  `Pipeline::max_vram_vendor_id()` — vendor+VRAM aynı adaptörden.
  **Canlı doğrulandı (30.07), ek bulgu üzerinden:** ilk canlı deneme
  35eb914'ün yetmediğini gösterdi — WGC backend aktifken (Win11'de her
  zaman kazanan yol) `source_->dxgi()` null → GpuScan hiç dolmuyor →
  vendor=0/VRAM=0 → daima Stabilite. Ek fix `ae360fd`
  (`TALIMAT_L18_GPUSCAN_BOYUT.md`, Faz 0 + aynı-tur uygulama):
  `scan_gpus_standalone` (bağımsız DXGI factory, mevcut static
  `scan_gpus` yeniden kullanıldı) + `Impl::fallback_scan_` (WGC dalında
  init'te bir kez) + getter'lar `current_scan()` üzerinden. Kanıt:
  `profile/asked` sıfırlandı → diyalog tetiklendi → **Performance**
  uygulandı (rules.json birebir performance.json; suggest_profile
  Performance'ı yalnız AC + VRAM≥eşik'te önerir → 7948MB sinyali
  diyaloğa ulaştı, VRAM=0 olsaydı kesin Stabilite olurdu).
- **L19** ✅ ca06936 — Faz 0: iki ret sebebi tek `dropped_` sayacında.
  Fix: `dropped_full()` + `rejected_invalid()`; `dropped()` toplamı korur.
- **L20** ✅ 32e732c — Faz 0: throttle/mtime-skip yolları `Ok(())` dönüyor;
  tek çağıran `RuleEngine::new` olduğundan fiilen ölü ama sözleşme tuzağı.
  Fix: `ReloadOutcome` (Reloaded/SkippedThrottled/SkippedUnchanged).

**Doğrulama sınıfı:** L13/L15/L20 Rust birim testleri (141 lib testi PASS,
3 yeni), L14 Rust birim testi (izole writer flush) + canlı doğrulandı
(30.07), L16/L19 gtest (AacEncoderTest/AudioRingTest, yeni/güncel
testler), L17 Zig testi (15/15). Merge-sonrası tam build OK, ctest 23/25
(bilinen 2 kırık: FrameProfiler/ShaderCache). L18 kod incelemesi +
derleme + canlı doğrulandı (30.07, ek fix ae360fd ile — yukarıda).

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
- **L9 CAN_PROVIDE yorumu (kısmi)** — Sprint 2 Faz 0'da çürütüldü (3cb573e):
  `output_provides_samples_` yorumu mevcut akışta yanlış davranış üretmiyor;
  maddenin gerçek kısmı (hata-yolu pSample sızıntısı) düzeltildi.

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
- [x] S1-ek3 (7665cb0, `docs/HEALING_RECOVERY_KOK_DUZELTME.md`): S1-ek2'nin
      UI gate'i yanlış boyutu hedefledi (canlıda kutu yayın SIRASINDA çıktı)
      → kök çözüm Rust'ta: rj_update_bitrate_state FFI'sı ile C++ konfigüre
      bitrate durumunu bildirir (bitrate_kbps.store noktalarında + init);
      collect_rule_actions, recovery_has_deficit (0<current<original) doğru
      değilse BitrateRecover'ı hiç üretmez. UI gate kaldırıldı (gerekçe:
      meşru banner'ları da yutuyordu; kök kaynakta kesildi). 5 yeni Rust
      testi + 2 mevcut test açık-deficit imzasına taşındı; Rust 137+5+35,
      HealingOverlayTest 2/2 (geometri kilitleri), Characterization 1/1,
      OutputSubsystem 7/7. Kullanıcı: boşta VE yayında gözlem gerekli.
      "Sprint 2'ye ertelendi" kararı geri alındı — kapsam bu turda kapandı.
- [x] S1-ek4 (13be1ab, `docs/BITRATE_STATE_SENKRON.md`): 7665cb0 sonrası kutu
      START'sız belirdi. Faz 0: kullanıcı hipotezi (3500 = kalıcı ayar,
      current/original farklı kaynaklar) ÇÜRÜTÜLDÜ (registry bitrate=6000;
      rj_update_bitrate_state iki atomiği daima birlikte yazar, tek yazar).
      Kök neden: null capture frame ("yeni kare yok" — durağan ekranda normal)
      run_frame'de frame_drops sayılıyordu → boşta FrameDropped event'leri
      predictive katmanı besliyor → ReduceBitrate(3500) encoder'a GERÇEKTEN
      uygulanıyor (status bar "3500"ün kaynağı) → (3500,6000) gerçek açık →
      frame_drop_recovery (ölü frame_drop_pct=0 → koşul hep doğru) her 6s'de
      BitrateRecover + banner. Eski baseline_metrics.txt bunu canlı kaydetmişti
      (boşta drops=1/örnek, ~60. frame'de 6000→3500). Düzeltme:
      frame_drop_policy.h saf seam — yalnız EncodeFailed drop sayılır;
      NoNewFrame sayılmaz. FrameDropPolicyTest 3/3 (RED-GREEN), ctest 22/24
      (bilinen 2), yeni baseline boşta drops=0 / 6000 sabit (davranış bilinçli
      değişti → baseline bu kez commit'e SOKULDU). Kullanıcı: 3 senaryo gözlemi
      gerekli (boşta / yayında / gerçek düşüşte recovery). SONRAKİ SPRİNT:
      (a) frame_drop_pct ölü metrik (record_frame/record_frame_drop çağrılmıyor
      — reduce kuralları hiç, recovery daima tetiklenir), (b) healing
      aktüasyonunda yayın-durumu kapısı yok, (c) DERS — dört ardışık
      merge-öncesi bulgunun ortak paydası: mutlu-yol testleri "uygulama açık
      ama yayın yok" başlangıç durumunu hiç kapsamıyor; boşta-durum testleri
      sprint planına ayrı kalem olarak girmeli.
- [x] S1-ek2 (6c6fce1, `docs/SIYAH_KUTU_REGRESYON.md`): boşta her ~6s'de
      sahne paneli üstünde beliren kutu. ~5s timer YOK, L5 ile İLGİSİZ;
      periyot = hysteresis_ms 6000. Zincir: eski ~/.reji/rules.json mode
      adları (auto/co_pilot) motorla hiç eşleşmiyordu → L1-ek applyProfile'ı
      canlandırınca stabilite ilk EŞLEŞEN set oldu → frame_drop_recovery
      boşta her pencerede tetiklendi → healing overlay banner'ı. Düzeltme
      (onaylı): yayın yokken bilgi banner'ı bastırılır (geçmiş tutulur,
      onay prompt'ları muaf). Dürüst sınır: "içeriksiz siyah" render
      ayrıntısı doğrulanamadı — adjustSize hipotezi offscreen testle
      çürüdü; en olası açıklama 3s'lik koyu banner'ın kendisi (canlı
      teyit bekliyor). HealingOverlayTest 4/4 (yeni widget test altyapısı).
      SPRINT 2 NOTU: boşta (yayın yokken) kural değerlendirmesi/recovery
      üretimi tasarım sorusu — Rust tarafı kök çözüm adayı (L11 komşusu).
- [x] L1-ek (72b4b09, `docs/ACIL_L1_QRC_REGRESYON.md`): canlı profil-uygula
      denemesi ":/config/profiles/*.json bulunamadı" verdi. Kök neden L1
      regresyonu DEĞİL — statik reji_ui.lib'de qrc nesnesi linker'ca
      atılıyordu (self-registration hiç çalışmadı); applyProfile L5'e dek
      ölü yol olduğundan hiç görünmemişti, şablon tohumlama da latent
      kırıktı. Düzeltme: ensureResourcesRegistered (Q_INIT_RESOURCE).
      Test boşluğu kapatıldı: QrcResourcesTest reji_ui'yi uygulamayla aynı
      biçimde link'ler; negatif kontrol canlı hatayı birebir üretti.
      Kullanıcı yeniden denemeli: öneri diyaloğunda "Uygula".
- [x] Faz 0 reduce-tetikleyici araştırması
      (`docs/REDUCE_TETIKLEYICI_ARASTIRMA.md`, kod değişikliği YOK):
      6000→3500 düşüşünün kaynağı kural motoru DEĞİL — predictive katman
      (hedef 3500 = REDUCED_BITRATE_KBPS imzası), besleyen sinyal START
      sonrası SRT gönderim-hatası drop'ları (127.0.0.1:9000'de dinleyici
      yok, `connected=false` → her paket drop). Gerçek CPU/GPU yükü YOKTU
      (cpu ≤%22, gpu ≤%22, mem %58, drop_pct %0). healing_log.sqlite:
      hiç reduce kaydı yok (predictive log'a yazmıyor), 4 recovery kaydı
      4 healing adımıyla birebir. Hibrit-GPU ölçümü bu olayda temiz
      (PDH engtype_3D max — ama NVENC encode motorunu ölçmüyor, ileri
      not). S1-ek4 düzeltmesinin boşta-doğrulaması BAŞARILI (boşta
      drop=0, 6000 sabit). KARAR (kullanıcı): Sprint 1 merge edilir;
      bulgular L21-L23 olarak Sprint 2'ye — "madem buradayız" tuzağından
      bilinçli kaçınma.
- [x] Sprint 2 tamamlandı ve merge edildi (2026-07-28, master `dbf4bc8`,
      origin senkron): üç dal — `feat/v10-sprint2-grupA` (L23 67d9258,
      L21 ecf9b99, L22 ec24d2a; merge 612a357), `feat/v10-l8-zig-abi-bounds`
      (L8 973dd19; merge 45db244), `feat/v10-sprint2-grupB` (L9 3cb573e,
      L10 53363f2, L11 9497b33, L12 9b29dc9; merge dbf4bc8). Canlı GUI
      doğrulaması (28.07): L21/L23 beklendiği gibi — srt_connect_failed
      logu doğru, bitrate düşüşü yok, drop=0%. Merge-sonrası tam build +
      ctest 23/25 PASS (bilinen 2 kırık: FrameProfiler/ShaderCache).
      Yerel dallar silindi. Yan gözlem (kod incelemesiyle teyit): "Capture
      loss detected (60 frames)" SRT'den bağımsız — null-streak eşiği
      statik ekranda ~1sn'de doluyor (`kNullStreakReinit=60`); zararsız,
      V11 adayı not edildi.
- [x] Sprint 3 tamamlandı ve merge edildi (2026-07-28): Faz 0'da sekiz
      maddenin sekizi doğrulandı (çürütme yok). İki dal —
      `feat/v10-sprint3-hijyen` (L13+L15 208e274, L20 32e732c, L16 c8efdfd,
      L19 ca06936, L17 fa9749f; merge e2e2f6b) ve `feat/v10-sprint3-canli`
      (L14 a0a9b33, L18 35eb914; merge 2ae5dd6). Gerekçe (8b): hijyen
      grubu davranış-nötr ama 2+ commit ve L13/L17 FFI sözleşmesine
      dokunuyor → dal; L14 yeni FFI yüzeyi (rj_healing_log_shutdown) +
      L18 görünür davranış → ayrı dal. Testler: Rust lib 141 PASS
      (3 yeni + L14 flush testi), Zig 15/15, merge-sonrası tam build +
      ctest 23/25 (bilinen 2 kırık). L14/L18 canlı GUI doğrulaması
      kullanıcıda. Bu sprintle **V10 TAMAMEN KAPANDI**.
- [x] `TALIMAT_V10_TARAMA_HAZIRLIK.md` ve `TALIMAT_V10_SPRINT3.md` →
      `docs/talimatlar/` arşivine taşındı (kapanış mühürü).
- [x] Canlı doğrulama turu tamamlandı (2026-07-30) — plan bu tarihle
      NİHAİ mühürlendi: (1) L18 canlı denemesi ek bulgu çıkardı — WGC
      backend'de GpuScan hiç dolmuyor (dxgi() null), Donanım Profilleme
      her donanımda Stabilite öneriyordu; Faz 0 boyut tespiti
      (`TALIMAT_L18_GPUSCAN_BOYUT.md` → arşiv) küçük buldu, aynı turda
      `ae360fd` ile kapatıldı (scan_gpus_standalone + fallback_scan_).
      (2) L18 uçtan uca canlı doğrulandı: profile/asked sıfırlandı,
      diyalog tetiklendi (L5'in de canlı kanıtı), NVIDIA/7948MB sinyali
      Performance önerisine dönüştü ve uygulandı (rules.json =
      performance.json, 12000 kbps/60 fps). (3) L14 canlı doğrulandı:
      normal kapanışta healing_log.sqlite kapanış anında flush edildi,
      son olaylar (111 kayıt, sonuncusu kapanıştan ~1 dk önce) diskte.
      Açık işaret kalmadı; V10 sonrası her bulgu V11'e.
