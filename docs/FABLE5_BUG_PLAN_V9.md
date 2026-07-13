# FABLE5_BUG_PLAN_V9.md — Reji Studio Üçüncü Nesil Bug Planı

**Hazırlayan:** Bu sohbetin (Claude, sohbet asistanı) kendisi — üç bağımsız
model incelemesinin sentezi.
**Kaynak incelemeler:** Claude Fable 5 (100 dosya, 346.660 token, 12.07.2026
18:38), Claude Opus 4.8 (100 dosya, 344.348 token, 12.07.2026 18:21), GLM 5.2
(100 dosya, 200.016 token, 12.07.2026 17:58).
**Dışlanan:** Minimax M3 — iki ayrı deneme (18:18 ve 19:00), ikisi de
yapılandırılmış rapora hiç dönüşmeden ham düşünme çıktısı olarak kesildi
(1098 ve 1358 satır, cümle ortasında bitiyor). Bu araç bu görev için
güvenilir sayılmadı; sentez tamamen diğer üç rapora dayanıyor.
**İlişki:** V8 bug planı (I1-I34) tamamen kapandıktan sonra alınan yeni bir
tarama turu — V7→V8 geçişiyle aynı desen (bağımsız model taramaları →
öncelik sıralı plan).

---

## Metodoloji ve Güvenilirlik Notu

Bu belge, **henüz Claude Code tarafından doğrulanmamış** ham bulguların
sentezidir. V8 boyunca defalarca kanıtlandığı gibi (I2/I3, I9/I10, I17, I29/
I31 örnekleri), bir bulgunun plan belgesinde yer alması onun doğru olduğu
anlamına gelmez — yalnızca **araştırılmaya değer olduğu** anlamına gelir.
Her madde, Claude Code tarafından ele alınmadan önce güncel `master`'a karşı
Faz 0 doğrulamasından geçmelidir (proje disiplini, istisnasız).

**Çapraz doğrulama derecesi** her maddede belirtilmiştir:
- 🟢 **3/3** — üç inceleyicinin de bağımsız olarak bulduğu madde (en yüksek güven)
- 🟡 **2/3** — iki inceleyicinin bulduğu madde
- 🔵 **1/3** — tek inceleyicinin bulduğu, kod parçasıyla desteklenen somut madde (doğrulama şart)

**V8 ile ilişki** de her maddede belirtilmiştir:
- **[V8 KÖR NOKTASI]** — V8'in kapsadığı bir alanda, V8'in gözden kaçırdığı madde
- **[V8 TAKİBİ]** — V8'in bir maddesinin (örn. I18) kapsamadığı benzer bir konum
- **[YENİ]** — V8'in hiç değinmediği tamamen yeni alan
- **[V8 TARAFINDAN ZATEN ELE ALINDI — DOĞRULA]** — V8'de kapandığı düşünülen ama incelemede tekrar gündeme gelen konu (yeniden açılma değil, teyit gerekir)

---

## Özet Tablo

| # | Madde | Doğrulama | V8 İlişkisi | Öncelik |
|---|---|---|---|---|
| J1 | `rj_push_scene_names` sınırsız `CStr::from_ptr` | 🟡 2/3 | V8 KÖR NOKTASI (I24) | Sprint 1 |
| J2 | SRT output, I18 FFI-sink desenini almamış | 🔵 1/3 | V8 TAKİBİ (I18) | Sprint 1 |
| J3 | Cross-adapter `transfer()` senkronsuz yol, guard yok | 🟢 3/3 | YENİ (I2/I3'ten farklı yol) | Sprint 1 |
| J4 | `ExternalMemoryBridge::get_frame_images` `static` slot | 🟢 3/3 | V8 TARAFINDAN ZATEN ELE ALINDI — DOĞRULA (I23) | Sprint 1 |
| J5 | `action_thread_main` sabit 100ms poll, kuyruk boşalana kadar sürmüyor | 🟢 3/3 | YENİ | Sprint 2 |
| J6 | AMD fallback spin-wait timeout'suz (TDR riski) | 🟢 3/3 | YENİ | Sprint 2 |
| J7 | Keyed-mutex key sabitleri paylaşımlı header'da değil | ✅ 🟡 2/3 | YENİ (bakım riski, aktif bug değil) — FIXED 452a4bb | Sprint 2 |
| J8 | `MetricsCollector::poll()` frame thread'de PDH sorgusu çalıştırıyor | ✅ 🟡 2/3 | YENİ (AGENTS.md ihlali — DOĞRULANDI) — FIXED efb0fe3 | Sprint 2 |
| J9 | NVENC `set_resolution` init'te `maxEncodeWidth/Height` ayarlamıyor | 🔵 1/3 | YENİ | Sprint 3 |
| J10 | Bitrate azaltma `REDUCED_BITRATE_KBPS` sabitini yok sayıyor | 🔵 1/3 (Minimax, kırık raporun sağlam parçası) | YENİ | Sprint 3 |
| J11 | GLM: `shared_texture_`'a kilitsiz erişim (preview toggle race) | 🔵 1/3 | YENİ | Sprint 3 — doğrulama önce |
| J12 | GLM: `client_sock_` atomik olmayan erişim (SRT worker/frame thread) | 🔵 1/3 | YENİ | Sprint 3 — doğrulama önce |
| J13 | `GpuInteropSubsystem::cache_last_images` shutdown'da temizlenmiyor | ✅ 🔵 1/3 | YENİ (latent — aktif UAF değil, ama cache asimetrisi gerçek) — FIXED 9413a5e | Sprint 3 |
| J14 | Kimlik bilgileri (WS parola, RTMP key) registry'de düz metin | 🟡 2/3 | YENİ (bilinen/kabul edilmiş sınırlama) | Sprint 4 |
| J15 | `program_widget.cpp` hot-path'te `convertToFormat` alloc | 🔵 1/3 | YENİ | Sprint 4 |
| J16 | Küçük temizlik/performans maddeleri (bkz. Sprint 4 detayı) | çeşitli | YENİ | Sprint 4 |

---

## Sprint 1 — En Yüksek Güven + V8 Kör Noktaları (öncelik: hemen)

### J1 — `rj_push_scene_names` sınırsız `CStr::from_ptr` 🟡 2/3 ✅ FIXED (7dc6c5a)
**Durum:** `cstr_bounded(ptr, MAX_NAME_LEN=256)`'ya geçirildi, manuel sonradan-kırpma
kaldırıldı. Faz 0 bulguları: (1) bayat DEĞİL — satır 3a93110'da geldi, I24 dokunmadı
(I24'te "zaten kırpıyor" gerekçesiyle kapsam dışı bırakılmıştı = V8 kör noktası);
(2) sonradan-kırpma taramayı sınırlamıyordu → OOB read gerçek açık. Davranış farkı:
NUL'u 256 byte ötesinde olan isim eskiden kırpılırdı, artık reddedilip atlanır (I24
semantiği). Rust lib 68 test PASS (+1 yeni). Push onay bekliyor.
**Kaynak:** Opus 6.6, Fable5 6.6 (aynı dosyayı ayrı ayrı işaret ediyor)
**Konum:** `src/orchestrator/src/ffi.rs`, `rj_push_scene_names`
**Açıklama:** I24'te tam bu zafiyet sınıfı (`rj_connection_lost`,
`rj_reload_rules`, `rj_set_ws_password`) `cstr_bounded` ile kapatılmıştı.
`rj_push_scene_names` aynı I24 Faz 0 triyajında **taranmamış** — hâlâ
sınırsız `CStr::from_ptr(ptr).to_string_lossy()` kullanıyor. Bozuk/NUL-
sonlandırılmamış bir sahne adı pointer'ı C++ tarafından gelirse OOB okuma.
**Önerilen düzeltme:** `cstr_bounded(ptr, MAX_NAME_LEN)` kullan, manuel
`MAX_NAME_LEN` kırpma mantığını kaldır (Opus'un önerdiği gibi).
**Not:** Bu maddenin V8 kapsamında iken atlanmış olması, I24 Faz 0
triyajının find-references taramasının bu çağrı yerini kaçırdığı anlamına
gelir — Faz 0'da neden kaçırıldığını anlamak (belki farklı bir dosyada/
isimlendirmede) küçük bir ek değer taşır ama zorunlu değil.

### J2 — SRT output, I18 FFI-sink desenini almamış 🔵 1/3 (somut, yüksek güven) ✅ FIXED (65aeb13)
**Durum:** I18 birebir mirror uygulandı. İki event sink (on_connection_lost, on_metrics)
ITransport::Config'e eklendi (latency_ms/bandwidth_kbps'in "SRT'ye özel, RTMP yok sayar"
deseniyle aynı); OutputSubsystem passthrough'ları (AudioSubsystem karşılığı) init'te
enjekte eder → SrtTransport → SrtOutput::Config → Impl doğrudan ::rj_* yerine sink çağırır.
SRT init non-null ister (I18 sözleşmesi). Kapsam (kullanıcı onaylı): rj_start_monitor
doğrudan bırakıldı (lifecycle bootstrap, event değil). Davranış birebir (passthrough sadece
::rj_* çağırır); eşdeğerlik kod incelemesiyle — ağ/cihaz-kaybı runtime sim YAPILMADI
(SRT bağlantısı gerektirir), I18'deki gibi dürüstçe not. ctest OutputSubsystemTest +
PipelineCharacterization PASS. Push onay bekliyor.
**Kaynak:** Opus 5.5
**Konum:** `src/pipeline/output/srt_output.cpp`
**Açıklama:** I18, wasapi capture katmanının doğrudan `::rj_connection_lost`/
`::rj_metrics_push` çağırmasını `AudioFrameCallback` deseniyle (sink
enjeksiyonu) düzeltmişti. Opus'un tespiti: SRT output katmanı **aynı
ihlali** hâlâ yapıyor — `send_internal`'de `rj_metrics_push`, `init_internal`'de
`rj_connection_lost`/`rj_start_monitor` doğrudan çağrılıyor.
**Önerilen düzeltme:** I18'in izlediği deseni (`AudioSubsystem::on_metrics`/
`on_connection_lost` passthrough) birebir mirror'la — `OutputSubsystem`
üzerinden sink enjeksiyonu.
**Neden Sprint 1:** Bu, "yeni bir keşif" değil, kendi tamamladığımız bir
işin (I18) bilinçsizce yarım bırakılmış ikizi. Çözüm şablonu zaten var,
kopyalanabilir — düşük risk, yüksek netlik.

### J3 — Cross-adapter `transfer()` senkronsuz yol, guard yok 🟢 3/3 ✅ FIXED (5f9d3ed)
**Durum:** Fail-closed uygulandı (`return nullptr`). Faz 0 V9'u üç noktada da
doğruladı: (1) I2/I3 CPU-fallback yolundan gerçekten farklı üçüncü yol; (2)
mevcut dual-GPU HW'de ölü (`create_cross_adapter_shared` E_INVALIDARG →
`use_cpu_fallback_=true`), aktif bug değil ama latent tehlike; (3) kod yorumu
senkron eksikliğini itiraf ediyor. Silme yerine fail-closed seçildi çünkü
`ROADMAP.md` E7 (cross-adapter NVENC, keyed mutex + fence) gelecek planı var —
scaffold korunuyor, yol gerçek senkronizasyon eklenene dek veri döndürmez.
Regresyon yok (PipelineCharacterization + GpuResourcePitchTest PASS, bilinen 2
kırık FrameProfiler/ShaderCache alakasız). Push onay bekliyor.
**Kaynak:** Fable5 1.1, Opus 1.1, GLM 1.3 — üçü de "critical" işaretlemiş
**Konum:** `src/pipeline/capture/gpu_resource_manager.cpp`,
`GpuResourceManager::transfer()`, cross-adapter branch
**Açıklama:** **Dikkat — bu, I2/I3'ün kapattığı DXGI-fallback CPU yolundan
farklı bir üçüncü yol.** Bu, gerçek cross-adapter (NT-handle paylaşımlı,
CPU-fallback olmayan) branch — mevcut donanımda (AMD+NVIDIA) hiç
tetiklenmiyor çünkü `create_cross_adapter_shared()` her zaman başarısız
oluyor, ama kod hâlâ orada ve senkronsuz. Kodun kendi yorumu bunu itiraf
ediyor ("BU KOD SENKRONİZASYON İÇERMİYOR"). Farklı donanım/sürücü
kombinasyonunda (örn. Intel+NVIDIA) bu yol gerçekten tetiklenebilir ve
yırtık kare/GPU çökmesi üretebilir.
**Önerilen düzeltme (üç inceleyici de aynı yönde):** Bu branch'i ya tamamen
sil ya da `return nullptr` ile fail-closed yap. Gerçek senkronizasyon
(D3D11.4 shared fence veya keyed-mutex) eklemek kapsam dışı — hedef donanımda
zaten kullanılmıyor.
**Not:** WGC/DXGI keşfinden (I2/I3) hatırlanacağı gibi, bu projede "aktif
olmayan ama kodda duran yol" kalıbı daha önce de gerçek riskler barındırmıştı
(I4/I5/I27/I28/I30/I32 hep DXGI fallback'e aitti). Bu farklı bir üçüncü yol
olduğundan aynı dikkatle ele alınmalı — ama düzeltmenin kendisi (guard/silme)
çok daha basit, derin bir tasarım turu gerektirmiyor.

### J4 — `ExternalMemoryBridge::get_frame_images` `static` slot 🟢 3/3 ✅ FIXED (f23103a)
**Durum:** `static uint32_t slot` → per-instance `pool_index_` member (h:132, ölü ama
tam bu amaç için adlandırılmış). Faz 0: **aktif bug değil** — tek bridge örneği
(gpu_interop_subsystem.cpp:16) + tek çağıran thread (pipeline döngüsü); risk = process-global
paylaşım kırılganlığı (2. örnek/thread eklenirse). I23'ün "kapsamı şişirmeme" kararı
tersine çevrildi (üç bağımsız kaynak aynı noktayı işaret etti). Tek-instance davranışı
birebir aynı. ctest SlotRingTest + PipelineCharacterization PASS. Push onay bekliyor.
**Kaynak:** Opus 1.5 (HIGH), Minimax (kırık raporun critical #1'i), GLM 5.1
— üçü de bağımsız aynı satırı işaret ediyor
**Konum:** `src/pipeline/gpu/external_memory_bridge.cpp`, `get_frame_images()`
**Açıklama:** **Bu madde I23 ile doğrudan ilişkili — dikkatle okunmalı.**
I23 Faz 1 tasarımında bu `static uint32_t slot` bilinçli olarak korunmuştu:
> "Fonksiyon-yerel static de bir koku; üye `frame_slot_`'a taşımak
> opsiyonel — kapsamı şişirmemek için static'i korurum, yalnız değerini
> expose ederim."
I23, bu slotu **tek doğruluk kaynağı** yaptı (drift sorununu çözdü) ama
onun kendisinin thread-safety/reentrancy sorununu (process-global mutable
state, iki `ExternalMemoryBridge` örneği veya çoklu thread'den çağrılırsa
çakışma riski) **kasıtlı olarak ertelemişti**. Üç bağımsız inceleyicinin
aynı noktayı işaret etmesi, bu ertelemenin yeniden değerlendirilmesi
gerektiğine işaret ediyor.
**Önerilen düzeltme:** `slot`'u zaten var olan `pool_index_` member'ına
taşı (Opus'un kod örneği bunu somut gösteriyor).
**Neden Sprint 1:** I23 zaten bu dosyaya dokunmuşken yapılacak küçük bir
ek adım; ayrı bir büyük tur gerektirmez, ama I23'ün "tamamlandı" algısını
tam kapatır.

---

## Sprint 2 — Üçlü Doğrulanmış, Yeni Alan (öncelik: yakın gelecek)

### J5 — `action_thread_main` sabit 100ms poll 🟢 3/3
**Kaynak:** Fable5 4.3, Opus 4.2, GLM 4.2
**Konum:** `src/pipeline/command_router.cpp`, `action_thread_main()`
**Açıklama:** Bir aksiyon dequeue edildikten sonra kuyrukta başka aksiyon
olsa bile 100ms uyuyor — N aksiyonluk bir patlama N×100ms sürer. Healing
aksiyonlarına gecikme ekliyor.
**Önerilen düzeltme:** Uyumadan önce kuyruğu iç döngüde boşalt; ya da
Rust tarafından sinyallenen bir Windows event/condvar'a geç (Opus'un
önerisi, daha temiz ama daha büyük değişiklik).

### J6 — AMD fallback spin-wait timeout'suz 🟢 3/3
**Kaynak:** Fable5 3.6, Opus 3.4, GLM 2.3
**Konum:** `src/pipeline/capture/capture_dxgi.cpp`, `amd_copy_fence_` spin
(`YieldProcessor()` ile `GetData(...DONOTFLUSH)` bekleme)
**Açıklama:** GPU çökerse (TDR, sürücü crash) sınırsız spin, frame thread'i
donduruyor. Ayrıca CPU çekirdeğini gereksiz yere %100 kullanıyor.
**Önerilen düzeltme:** Sınırlı spin sayısı sonrası `Sleep(0)`/`SwitchToThread`
eskalasyonu veya event-query + bounded sleep deseni.

### J7 — Keyed-mutex key sabitleri paylaşımlı header'da değil ✅ FIXED (452a4bb) 🟡 2/3
**Kaynak:** Fable5 3.3, Opus 3.3 (ikisi de HIGH bakım riski diyor)
**Konum:** `copy_optimizer.cpp` (`km_acquire_key_=1`, `km_release_key_=0`)
vs `capture_dxgi.cpp` (`AcquireSync(0)`/`ReleaseSync(1)`)
**Açıklama:** **Aktif bir bug değil** — GLM bu protokolü satır satır
doğrulayıp doğru bulmuş, Minimax'ın ham notları da aynı sonuca varmış.
Risk, değerlerin şu an "şans eseri" tutarlı olması ve paylaşımlı bir sabit
olmadan gelecekte bir taraf değiştirilirse sessizce kalıcı deadlock'a
dönüşmesi.
**Önerilen düzeltme:** Key sabitlerini tek bir paylaşımlı header'a
(`reji_constants.h` gibi) çıkar, her iki taraf oradan referans alsın.
**Öncelik notu:** Düşük risk ama düşük efor da — bakım borcu temizliği.
**SONUÇ (452a4bb):** Faz 0 iddiayı doğruladı — aktif bug yok, protokol
kendim satır satır doğrulandı (GLM'e körü körüne güvenilmedi). Bakım riski
teorik değil (capture_dxgi.cpp bu sprint'te 3× dokunuldu; iki dosya
değerleri TERS rollerle kullanıyor → sinsi drift). `reji_constants.h`'ye
rol-tabanlı iki sabit eklendi: `kKeyedMutexKeyD3D11=0` (D3D11 yazma turu),
`kKeyedMutexKeyVulkan=1` (Vulkan okuma turu). Saf refactor, değerler
birebir. PipelineCharacterization + GpuResourcePitch + SlotRing PASS.

### J8 — `MetricsCollector::poll()` frame thread'de PDH sorgusu ✅ FIXED (efb0fe3) 🟡 2/3
**Kaynak:** Fable5 4.4, Opus 4.3 (Opus özellikle "AGENTS.md açıkça bunu
yasaklıyor" diyor)
**Konum:** `src/pipeline/metrics_collector.cpp` / `pipeline.cpp::run_frame()`
**Açıklama:** **Dikkat — I14 ile karıştırılmamalı.** I14, `rj_metrics_poll`
FFI fonksiyonunu (UI'ın metrik barı için, `MetricState`'ten atomik okuma)
implemente etmişti — o taraf zaten AGENTS.md'nin blokla­yan-sorgu yasağına
uygun tasarlandı. Bu madde **farklı bir bileşen**: `MetricsCollector::poll()`,
C++ tarafında `PdhCollectQueryData` çağıran fonksiyon, frame thread'den
(1Hz throttle'lı olsa da) tetikleniyor. Bu, projenin kendi
`AGENTS.md:114` kısıtının (WMI/bloklayan sorgu hot-path'te yasak) olası
bir ihlali.
**Önerilen düzeltme:** `MetricsCollector::poll()`'u ayrı 1Hz arka plan
thread'ine taşı; `run_frame()` yalnız atomik snapshot okusun
(`get_latest()`).
**Faz 0'da netleştirilmesi gereken:** Bu gerçekten AGENTS.md ihlali mi
(kısıt tam olarak neyi yasaklıyor, throttle'lı çağrı istisna mı) — kod
incelemesiyle teyit şart, varsayılmasın.
**SONUÇ (efb0fe3):** Faz 0 ihlali DOĞRULADI. AGENTS.md:108-115 iki kümülatif
kural içeriyor — (1) "thermal, CPU load queries ayrı thread'te" + ✅ DOĞRU
"background thread (MetricsCollector) → poll", (2) 1Hz throttle. Throttle,
ayrı-thread gereksiniminin istisnası DEĞİL. Kod #2'yi sağlıyor ama #1'i ihlal
ediyordu (run_frame:688'den çağrı) VE kendi header/build_sample yorumlarıyla
("background thread") çelişiyordu. Şiddet düşük (thermal WMI stub → adlandırılan
deadlock senaryosu yok; 1Hz; tahmini ~0.5-3ms/sn jitter, ölçülmedi=kod
incelemesi). Düzeltme: MetricsSubsystem'e CommandRouter desenli 1Hz arka plan
thread'i (poll_loop + stop()/join + RAII destructor); run_frame yalnız
get_latest() snapshot okur. **I14 AYRIMI:** `rj_metrics_poll` (Rust, UI atomik
okuma) ≠ `MetricsCollector::poll()` (C++, PDH) — bu madde ikincisi.

---

## Sprint 3 — Tekil Bulgular, Doğrulama Öncelikli (öncelik: Faz 0 sonrası)

Bu maddeler tek bir inceleyiciden geliyor. Somut kod referanslarıyla
destekleniyorlar ama diğer iki rapor tarafından teyit edilmemiş — Faz 0'da
**önce gerçekliği doğrulanmalı**, sonra düzeltme planlanmalı.

### J9 — NVENC `set_resolution` init'te `maxEncodeWidth/Height` ayarlamıyor 🔵 1/3
**Kaynak:** Opus 2.3
**Konum:** `src/pipeline/encode/encode_nvenc.cpp`
**Açıklama:** `set_resolution` yeni boyutu ayarlıyor ama encoder init'te
`maxEncodeWidth/Height` hiç set edilmemiş — NVENC yalnız başlangıç
çözünürlüğü için ayrılmış olabilir, büyütme reconfig'i başarısız olabilir.
Bu, self-healing çözünürlük düşürme/geri yükleme yolunda gerçek bir
fonksiyonel hata iddiası.
**Faz 0'da doğrulanacak:** NVENC init parametrelerinin gerçek değerleri,
reconfig'in gerçekten başarısız olup olmadığı (loglardan/davranıştan).

### J10 — Bitrate azaltma `REDUCED_BITRATE_KBPS` sabitini yok sayıyor 🔵 1/3
**Kaynak:** Minimax'ın kırık raporunun kendi içinde tutarlı tek bölümü
**Konum:** `pipeline.cpp::apply_action`, `RJ_ACTION_BITRATE_REDUCE` case
**Açıklama:** C++ tarafı `current * 0.85f` ile yüzdesel azaltma yapıyor;
Rust tarafındaki `REDUCED_BITRATE_KBPS = 3500` sabiti (healing hedefi
olarak tanımlı) hiç kullanılmıyor, `RjAction::param1` de bu amaçla
gönderiliyor ama C++ tarafında yok sayılıyor.
**Faz 0'da doğrulanacak:** Bu kasıtlı bir tasarım mı (yüzdesel azaltma
daha kademeli, sabit hedef ise ani düşüş) yoksa gerçek bir tutarsızlık mı —
`RuleEngine`'in `param1`'i neden gönderdiği incelenmeli.

### J11 — `shared_texture_`'a kilitsiz erişim (preview toggle race) 🔵 1/3
**Kaynak:** GLM 1.1 ("critical" işaretlenmiş, diğer iki rapor değinmiyor)
**Konum:** `src/pipeline/capture/capture_dxgi.cpp`, `capture_next()`
**Açıklama:** `capture_next()` (frame thread) `shared_texture_`,
`keyed_mutex_shared_`, `staging_texture_`, `preview_staging_`'e kilitsiz
erişiyor; `ensure_preview_staging()`/`set_preview_requested()` (herhangi
bir thread'den, `cb_mutex_` altında) bu texture'ları `Reset()` edebiliyor.
Preview toggle sırasında kopya devam ederken texture serbest bırakılırsa
use-after-free/null-deref iddiası.
**Faz 0'da doğrulanacak (öncelikli):** `cb_mutex_`'in gerçekten bu
alanları korumadığı find-references ile teyit edilmeli — GLM'in tek
başına bulduğu bir "critical" iddia, gerçek olup olmadığı doğrulanmadan
implementasyona geçilmemeli.

### J12 — `client_sock_` atomik olmayan erişim (SRT) 🔵 1/3
**Kaynak:** GLM 1.2 ("critical" işaretlenmiş)
**Konum:** `src/pipeline/output/srt_output.cpp`, `send_internal` /
`worker_loop`
**Açıklama:** Worker thread `client_sock_`'u kapatıp `SRT_INVALID_SOCK`
yazarken, frame/encode thread aynı anda `send_internal`'de bu socket'i
kullanıyor olabilir — mutex/atomic koruması yok, use-after-free/geçersiz
socket'e gönderim iddiası.
**Faz 0'da doğrulanacak:** J11 gibi, tek kaynaklı "critical" iddia —
gerçek eşzamanlılık senaryosunun mümkün olup olmadığı (worker ve frame
thread'in gerçekten örtüşüp örtüşmediği) find-references ile teyit
edilmeli.
**Not:** J2 (SRT/I18 deseni) ile aynı dosyada — Faz 0'da birlikte
incelenmesi verimli olabilir, ama farklı sorunlar (biri katman ihlali,
biri thread-safety).

### J13 — `GpuInteropSubsystem::cache_last_images` shutdown'da temizlenmiyor ✅ FIXED (9413a5e) 🔵 1/3
**Kaynak:** Opus 2.2
**Konum:** `src/pipeline/gpu_interop_subsystem.cpp`, `shutdown()`
**Açıklama:** `shutdown()` `ext_bridge_`'i sıfırlıyor (VkImage'ları yok
ediyor) ama `last_staging_vk_`/`last_target_vk_` cache'i temizlenmiyor.
Sonraki bir `get_last_frame_images()` çağrısı (GL thread) artık yok
edilmiş handle'ları döndürebilir.
**Önerilen düzeltme:** Opus'un verdiği kod örneği net — `shutdown()`
içinde cache atomiklerini `nullptr`'a set et.
**Not:** Bu, küçük ve düşük riskli bir madde; doğrulama sonrası doğrudan
düzeltilebilir, ayrı bir tasarım turu gerektirmez.

**Faz 0 sonucu (doğrulandı):** İddianın teknik çekirdeği DOĞRU — cache
gerçekten dangling kalıyor (`ext_bridge_shutdown()` `image_pool`/
`gl_target_images` VkImage'larını `vkDestroyImage` ile yok ediyor,
`external_memory_bridge.zig`) ve okuma yolu (`get_last_frame_images`)
yazma yolundaki (`get_frame_images`, `if (raw() && ...)`) `raw()`/bridge-alive
guard'ından **yoksun** — asimetri gerçek. ANCAK "aktif use-after-free"
kısmı LATENT: getter'ın tek çağıranı (`main_window.cpp:154`,
`d3d11_frame_callback`, yalnızca `run_frame` içinde frame thread'de) normal
teardown'da (`~MainWindow`: `stopFrameThread()` join → sonra `pipeline_`
yıkımı → `shutdown()`) shutdown'dan ÖNCE join ediliyor → dangling cache hiç
okunmuyor. Şık (b): teorik geçerli, normal akışta ulaşılamaz.
**Faz 1 (uygulandı):** Savunma-derinliği — düşük maliyet (3 satır, davranış
değişikliği yok) + header'ın belgelediği GL-thread okuyucusunu geleceğe
karşı güvenceye alma gerekçesiyle düzeltildi (I29/I31 "çürüt" değil; iddia
yanlış değildi, yalnızca şu anki etkisi sıfırdı). Regresyon:
`PipelineCharacterization` PASS.
**Kapsam-dışı gözlem:** `stopFrameThread()` `wait(5000)` zaman aşarsa
(run_frame 5sn+ asılırsa) frame thread join edilmeden teardown devam eder →
shutdown ile `run_frame` yarışır. Bu J13'ün konusu değil (daha geniş bir
teardown race'i, `get_frame_images`/`shared_texture()` erişiminin tümünü
kapsar); yeni V9 maddesi önerilmiyor, yalnızca ileride tökezleme olmaması
için SESSION_NOTES'a not düşüldü.

---

## Sprint 4 — Düşük Öncelik / Bakım / Bilinen Sınırlamalar

### J14 — Kimlik bilgileri düz metin (WS parola, RTMP key) 🟡 2/3
**Kaynak:** Fable5 6.3, GLM 6.3
**Konum:** `settings_dialog.cpp`, `QSettings` üzerinden registry
**Açıklama:** Zaten I8 sırasında bilinen ve kabul edilmiş bir sınırlama
("OBS ile aynı yaklaşım" yorumu kodda mevcut). Fable5 ek olarak WS
parolasının **ağ-yüzeyli bir kontrol noktasını** koruduğunu vurguluyor —
herhangi bir yerel process registry'yi okuyup akışı durdurabilir.
**Önerilen düzeltme (opsiyonel, defense-in-depth):** `CryptProtectData`
(DPAPI) ile sarmalama.
**Öncelik notu:** Bilinçli kabul edilmiş bir tasarım kararı olduğundan
düşük öncelik — yalnızca kullanıcı bunu gerçek bir tehdit olarak görürse
ele alınmalı.

### J15 — `program_widget.cpp` hot-path'te `convertToFormat` alloc 🔵 1/3
**Kaynak:** Fable5 2.2 ("HOT-PATH: no heap allocation" yorumuyla doğrudan
çelişiyor olması dikkat çekici)
**Konum:** `src/ui/program_widget.cpp`, `paintGL()`
**Açıklama:** Her çizilen karede ~8MB'lık bir `QImage::convertToFormat`
alloc'u, hemen üstündeki "hot-path'te alloc yok" yorumuyla çelişiyor.
**Önerilen düzeltme:** BGRA'yı doğrudan `GL_BGRA` ile yükle (PreviewWidget'ın
zaten yaptığı gibi, shader swizzle ile) veya dönüşümü tek seferlik
`uploadFrame`'e taşı.

### J16 — Diğer küçük maddeler (gruplandırılmış, ayrı Faz 0 gerekmeyebilir)
Aşağıdakiler tekil kaynaklı, düşük risk/düşük etkili, muhtemelen Sprint
1-3 çalışması sırasında "bu arada" yakalanabilecek maddeler:

- **`create_cross_adapter_shared()`'da `OpenSharedResource1` başarısız
  olursa handle sızıntısı** (GLM 2.1) — J3 ile aynı dosya/bölge, birlikte
  ele alınabilir.
- **`FrameProfiler` her mark çağrısında mutex alıyor** (GLM 4.4) —
  60fps'te 6 kilit/kare, tanı-amaçlı kod için performans notu.
- **`preview_staging_` legacy texture hâlâ her karede kopyalanıyor**
  (Fable5 4.2, Opus 2.4) — deprecated işaretli ama hâlâ aktif, silinmesi
  öneriliyor.
- **`next_action_id()` wrap'ta teorik 0-sentinel çakışması** (Fable5 1.7,
  Opus 1.6) — ~13 yıl kesintisiz çalışmada gerçekleşir, I23'teki u32-global
  ID kararıyla aynı analiz sınıfı, pratik önemi yok, `fetch_update` döngüsü
  ile bir satırlık iyileştirme mümkün.
- **`MainWindow` god-object** (Fable5 5.3), **`PreviewWidget`'ın Vulkan
  submission'ı doğrudan sürmesi** (Fable5 5.1) — mimari refactor
  önerileri, bug değil, ayrı bir mimari tartışma gerektirir (Sprint 3-4
  kapsamının çok ötesinde, belki Faz 3/ISource çalışmasıyla birlikte
  değerlendirilmeli).
- **Vulkan/GL interop derinlemesine bulgular** (Fable5 3.1-3.5, Opus
  3.1-3.4, GLM 3.1-3.3) — GL semaphore/fence/slot yaşam döngüsü etrafında
  çok sayıda ince bulgu var, bazıları çelişkili (örn. GLM 3.1 "sorun yok"
  derken Opus 3.2 aynı alanda "eksik sync" diyor). **Bu alan I23'ün
  kapsadığı bölgeyle iç içe** — I23 slot senkronunu düzeltti ama semaphore/
  fence yaşam döngüsünün tamamını kapsamadı. Ayrı, derin bir GPU-interop
  turu (I23'ün devamı niteliğinde) gerektirir; bu plana tek tek madde
  olarak eklenmedi, çünkü üç raporun birbirini kısmen çelişmesi tek
  başına bir Faz 0 araştırma konusu.

---

## Sonraki Adım Önerisi

Sprint 1'in dört maddesi (J1-J4) hem en yüksek güvenilirlikte hem de kendi
V8 işimizin doğal devamı — ayrı talimatlar halinde hazırlanabilir, muhtemelen
J1+J4 (FFI/slot, küçük ve ilişkili) ve J2 (SRT/I18 deseni, kendi başına net)
şeklinde gruplanabilir. J3 (cross-adapter guard) tek satırlık bir düzeltme
olduğundan bağımsız, hızlı bir madde olarak ele alınabilir.

Sprint 3'ün GLM-kaynaklı iki "critical" iddiası (J11, J12) dikkatle
işaretlenmeli — somut ve ciddi görünüyorlar ama tek kaynaklı olduklarından
Faz 0'da gerçekliklerinin kanıtlanması, düzeltme çalışmasından önce gelmeli.

Bu belge, Claude Code'a verilecek talimatların kaynak dokümanı olarak
kullanılacak — V8'deki gibi, her talimat kendi Faz 0→1→2→3 döngüsünü
işletecek ve bu belgedeki hiçbir madde sorgusuz kabul edilmeyecek.
