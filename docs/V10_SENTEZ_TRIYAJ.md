# V10 SENTEZ VE TRİYAJ — Dört Model Raporunun Birleştirilmesi (L-Serisi)

**Kaynak raporlar (2026-07-21):** Fable 5 (14 ana + 4 spekülatif),
Opus 4.8 (öz-eleyerek ~6 net bulgu), GLM 5.2 (uzun analiz, 2 net bulgu),
Kimi K3 (uzun analiz, 4 net bulgu — yeni dördüncü model).
**Görev:** Bu sentezi `docs/FABLE5_BUG_PLAN_V10.md` iskeletine işle,
sonra Sprint 1'den başlayarak Faz 0 doğrulamalarıyla ilerle. Her L
maddesi V8/V9 desenindeki gibi kendi Faz 0'ından geçecek — rapor
iddiası, koda karşı doğrulanmadan düzeltme yazılmayacak.

---

## SPRINT 1 — Yüksek değer/etki (çapraz-doğrulanmış + kritik tekiller)

### L1 — writeValidatedRules hata-yolu zinciri (4/4 MODEL — en güçlü örtüşme)
Dört bağımsız model aynı bölgeyi işaret etti (son üç GUI bug'ının tam bölgesi):
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
**Önerilen yön:** QSaveFile/geçici-ad + atomik rename deseni; backup
başarısızsa asıl dosyaya dokunma; copy-fail'de backup restore + motor
reload; hata mesajını gerçek duruma uydur.

### L2 — Audio metrik kirliliği: FrameDropPct{0} enjeksiyonu (2/4: Opus + Kimi)
Ses metrik örnekleri (source_id=1, 1Hz) frame_drop_pct=0 taşıyor;
`media_events_for_sample` source_id ayrımı yapmadan koşulsuz
FrameDropPct yayınlıyor (frame_drop plumbing V10-yeni) → videonun
gerçek drop sinyali ~1Hz'de sıfırlanıyor → sahte `frame_drop_recovery`
tetiklenmesi + gerçek drop kurallarının bastırılması. Kimi'nin ek
gözlemi: MemUsage'ın >0 guard'ı bu sorunu zaten biliyor (asimetri
kanıtı); audio frame_drops'un (ses glitch'leri) video FrameDropped
trendine karışması ikincil sorun.
**Önerilen yön:** Drainer'da media event üretimini source_id==0'a
(video) sınırla.

### L3 — Kalibrasyon hot-reload'da sessizce kayboluyor (1/4: Kimi — tekil ama kod-kanıtlı)
`rj_reload_rules` yepyeni RuleEngine kurar → Özellik #5'in kalibre
eşiği (engine-içi tablo) atılır; HealingMonitor `calibration_done=true`
olduğundan bir daha uygulamaz. Herhangi bir hot-reload/import/profil-
uygulama, kalibrasyonu kalıcı siler — kurallar statik eşiklere döner,
`[kalibre]` etiketi kaybolur, kullanıcıya hiçbir sinyal yok.
**Önerilen yön:** Kalibrasyon tablosunu engine dışında yaşat veya
engine değişiminde HealingMonitor yeniden uygulasın.

### L4 — Auto-reload kapalıyken import watcher'ı yeniden silahlandırıyor (1/4: Kimi)
Kullanıcı auto-reload'u açıp kapatırsa (watcher nesnesi var, path'ler
temizlenmiş), sonra import yaparsa: `writeValidatedRules`/`reloadRulesNow`
içindeki `armRulesWatch()` çağrıları path'leri geri ekler →
**checkbox kapalıyken** harici düzenlemeler sessizce hot-reload olur.
`armRulesWatch` içinde `isAutoReloadEnabled` kontrolü yok. Bizim
449c084 düzeltmemizin (yeniden-silahlandırma ekleyen) etkileşim alanı.
**Önerilen yön:** `armRulesWatch` girişine enabled kontrolü.

### L5 — Çift init yolu: profil önerisi hiç tetiklenmiyor olabilir (1/4: Fable)
`maybeSuggestProfileOnFirstRun` tetikleyicisi yalnız
`MainWindow::initPipeline()` içinde; ctor kendi `pipeline_->init(pcfg)`
yolunu kullanıyor ve orada singleShot YOK. Gerçek akış ctor'daysa
ilk-kurulum önerisi hiç görünmez. Ayrıca iki giriş noktasının yan-etki
kümeleri farklı: ctor frame thread başlatır ama singleShot yok;
initPipeline singleShot var ama frame thread yok — ikinci init
senaryosunda frame thread'siz "init'li ama kare dönmüyor" durumu.
**Faz 0 kritik sorusu:** Gerçek çalışma akışı hangi yolu kullanıyor?
**KULLANICI ÇAPRAZ-KONTROLÜ:** Kullanıcı bugüne kadar profil önerisi
diyaloğunu hiç GÖRMEDİYSE bu bulgunun canlı kanıtıdır — Faz 0'da sor.

### L6 — ASC kaybı yarışı: kalıcı sessiz ses ölümü (1/4: Fable — dar pencere ama kalıcı etki)
`ensure_encoder` → `set_audio_config` dönüşü kontrol edilmiyor
(`(void)`); transport o anda null'sa (stop_stream yarışı) ASC kaybolur,
`encoder_ready_=true` kaldığından bir daha denenmez →
`rj_rtmp_send_audio` `t.asc orelse return false` ile TÜM ses
frame'lerini kalıcı reddeder. Yeniden start_stream'de ses ölü kalır.
**Önerilen yön:** Dönüşü sakla, başarısızsa sonraki drain'de yeniden
dene (asc_sent_ bayrağı).

### L7 — Shutdown-flush sırasında MF lazy-init SEH ihlali (1/4: Fable)
`seh_shutdown_subsystems` içindeki `enc->flush()` kalan paketleri
boşaltır → `on_packet` → `audio_bridge_.drain()` → hiç ses akmamışsa
**shutdown anında** MFStartup/MFT create tetiklenir — `__try` scope'unda
C++ destructor'lı nesnelerle MF çağrısı (proje SEH disiplini kural 4
ihlali). `audio_bridge_.shutdown()` flush'tan SONRA çalıştığından
`enabled_` hâlâ true.
**Önerilen yön:** `enabled_=false` store'unu (veya bridge shutdown'ı)
seh_shutdown_subsystems'ten ÖNCE al; drain'e "shutdown başladı" kontrolü.

## SPRINT 2 — Orta öncelik

### L8 — Zig ABI üst-sınır eksikliği (1/4: Fable — prompt'un özel dikkat çağrısı #3'e doğrudan cevap)
`rj_rtmp_send_audio`/`set_audio_config`/`rj_rtmp_send` boyut tavanı
yok — bozuk usize sınırsız slice + @memcpy (J1 `cstr_bounded` dersi bu
yüzeyde uygulanmamış). Ek: `writeFlvTag` `body.len & 0xFFFFFF` sessiz
kırpma (3/4 model değindi: Fable/Opus/GLM) — 16MB üstü body'de bozuk
tag; maske yerine reddet.

### L9 — MFT CAN_PROVIDE_SAMPLES yanlış yorumu + hata-yolu pSample sızıntısı (1/4: Fable)
`output_provides_samples_` CAN_PROVIDE bitini de "provides" sayıyor —
CAN_PROVIDE'da caller buffer sağlamalı; sağlamazsa MFT reddedip ses
kalıcı kopabilir. Ayrıca FAILED/NEED_MORE_INPUT dallarında MFT-provided
pSample release edilmiyor (teorik sızıntı).

### L10 — Kanal-uyumsuzluğunda sessiz bozuk encode (1/4: Opus)
Cihaz mono/farklı kanal verirse encoder sabit 2ch varsayımıyla yanlış
frame sayısı/interleaving üretir — bilinen #5 (resampling yok) yalnız
sample-rate'i kapsıyor, kanal boyutu ayrı ve sessiz bozulma üretiyor.
**Önerilen yön:** Format uyuşmazlığında ses yolunu güvenli kapat.

### L11 — step_kbps ölü parametre (1/4: GLM — Donanım Profilleme değer kaybı)
Kural motoru step_kbps'i param1'e özenle taşıyor; `apply_action`
BITRATE_REDUCE'da bunu YOK SAYIP sabit %15 uyguluyor → üç profilin
mild(300)/high(750) step ayrımı fiilen çalışmıyor, ölü konfigürasyon.
**Faz 0 sorusu:** Bilinçli tasarım mı (yüzde-tabanlı tercih) yoksa
unutulmuş bağlantı mı? Bilinçliyse profillerden step_kbps kaldırılmalı
(yanıltıcı), değilse param1 kullanılmalı.

### L12 — A/V pts epoch doğrulaması (2/4: Fable orta + Opus spekülatif)
Ses pts'i WASAPI QPC-mutlak; video pts'i `FramePacer::pts_us` —
pacer implementasyonu tarama kapsamı dışıydı, iki tabanın aynı olduğu
DOĞRULANAMADI. Tabanlar farklıysa ilk-gelen epoch'u belirler, diğer
akış ts=0'a yapışır veya saatlerce kayar; drift valfi sürekli uyarır
ama düzeltmez. **Faz 0: frame_pacer.h/cpp okunarak kesinleştirilecek**
— aynıysa çürüt, farklıysa düzelt. (Fable raporunun kendisi de
"pacer görülmeden kesinleşmez" diye işaretledi — dürüst sınır.)

## SPRINT 3 — Düşük öncelik / hijyen

- **L13** — `rules_buf` 64KB aşımında yanıltıcı "Kural okunamadı"
  (2/4: Fable+Opus) — boyut-aşımı ile motor-hazır-değil ayrımı.
- **L14** — HealingLog writer thread'e shutdown sinyali + son flush
  (Fable) — düzenli kapanışta son 250ms kaybı.
- **L15** — `rj_action_approve` kuyruk-dolu geri koymada `created`
  tazelenmeli (Fable) — onaylanan aksiyon anında TTL'e düşebilir.
- **L16** — `pcm_scratch_.reserve` init'te (Fable) — hot-path realloc.
- **L17** — `updateParamSet` dupe başarısızlığında bool dönüş (Fable).
- **L18** — Profil önerisi diyaloğunda vendor/VRAM eşleşmezliği
  (Kimi) — display vendor (iGPU) + max VRAM (dGPU) yan yana "Intel
  12GB" gibi yanıltıcı gösterim.
- **L19** — AudioRing `dropped_` sayacı doluluk/geçersiz-girdi ayrımı
  (Fable).
- **L20** — `hot_reload` throttle "Ok ama skip" sözleşmesi (Fable) —
  fiilen ölü kod, ileride tuzak; ayırt edilebilir dönüş.

## ÇÜRÜTÜLEN / ELENENLERİN KAYDI (plan dosyasına işlenecek)
- COM-init-frame-thread (Kimi kendisi eledi — MFStartup COM init eder,
  test kanıtı destekliyor). İzlenecek: yine de Faz 0'da bir kez
  doğrulanabilir, düşük maliyet.
- AudioRing SPSC ordering (3 model bağımsız doğruladı — temiz).
- nextNal 3/4-byte prefix (GLM kendisi eledi — EBSP geçerli H.264'te
  sorun değil).
- qpc_now_us overflow (Opus + Kimi bağımsız doğruladı — güvenli).
- ExistingDesktopSource katmanı — iki model bağımsız temiz buldu
  (henüz wire edilmemiş olduğu notuyla).
- capture_loop clamp/ReleaseBuffer (Opus + GLM doğruladı — doğru).

## SPEKÜLATİF HAVUZ (düzeltme yok, kayıt)
- Settings ctor senkron COM enumerasyonu (2 model, spekülatif) —
  yavaş BT ses sürücülerinde açılış takılması.
- frame_thread busy-spin (Fable, kapsam-sınırında) — capture yoksa
  %100 çekirdek; wiring turunda ele alınabilir.
- NOTIFY_END_OF_STREAM + COMMAND_DRAIN sırası (Fable).

---

## Sabit Kurallar
- Her L maddesi Faz 0 doğrulamasından geçer — dört model de yanılabilir
  (V9'da J10/J11 çürütülmüştü); rapor iddiası kanıt değildir.
- L5'in Faz 0'ında kullanıcıya sorulacak: profil önerisi diyaloğunu
  hiç gördü mü?
- L12 pacer kodu okunmadan karara bağlanmaz.
- CLAUDE.md Bölüm 8b dal disiplini; sprint başına dal değerlendirmesi.
- `tests/baseline_metrics.txt` asla commit edilmez.
- Her düzeltme "test edildi / kod incelemesiyle / kullanıcıda" ayrımıyla
  raporlanır.
