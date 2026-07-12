# TALİMAT: V9 Sprint 1 — J1, J2, J3, J4

**Kaynak:** `docs/FABLE5_BUG_PLAN_V9.md` (J1-J4), üç bağımsız model
incelemesinin (Fable 5, Opus 4.8, GLM 5.2) sentezi.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü ve Kritik Uyarı

V9, henüz Claude Code tarafından **hiç doğrulanmamış** bir plan belgesi —
V8'in aksine, bu maddeler implementasyon sırasında ortaya çıkan derin
keşiflerden değil, tek geçişlik statik model taramalarından geliyor.
Plan belgesinin kendi metodoloji notu şunu açıkça söylüyor: bir bulgunun
belgede yer alması onun doğru olduğu anlamına gelmez, yalnızca araştırmaya
değer olduğu anlamına gelir.

**Bu talimat için özellikle geçerli:** V8'de plan metnine körü körüne
güvenmek defalarca yanlış çıktı (I2/I3 yanlış konumlanmıştı, I9 "2 yer"
derken 1 yerdi, I17'nin öncülü çürüktü, I29/I31 tamamen hayaletti). V9
belgesindeki her madde için de aynı şüphecilik uygulanacak — istisnasız.

Dört madde birbirinden bağımsız, farklı dosyalarda. Ortak yalnızca risk
seviyeleri (düşük-orta) ve V8 ile ilişkileri (ikisi kör nokta/takip, biri
ertelenmiş karar, biri tamamen yeni). Her biri kendi Faz 0'ından geçecek;
biri diğerini bloklamıyor, ama sıra J3 → J1 → J4 → J2 öneriliyor (basitten
karmaşığa, dosya çakışması yok).

---

## J3 — Cross-adapter `transfer()` senkronsuz yol, guard yok

**V9 kaynağı:** Fable5 1.1, Opus 1.1, GLM 1.3 (3/3, üçü de "critical")

### Faz 0 (zorunlu, kod yazmadan)
1. `src/pipeline/capture/gpu_resource_manager.cpp`, `GpuResourceManager::transfer()`
   içindeki cross-adapter branch'i bul (`!same_adapter_ && !use_cpu_fallback_`
   koşulu, üç raporun da işaret ettiği yer).
2. **Kritik doğrulama:** Bu branch'in I2/I3'ün kapattığı DXGI-fallback CPU
   yolundan **gerçekten farklı** bir üçüncü yol olduğunu teyit et — V9
   belgesi bunu iddia ediyor ama sen kendi gözünle kontrol et. Aynı yolsa
   plan yanlış konumlandırılmış demektir, raporla.
3. Bu branch'in gerçekten hiç tetiklenmediğini (mevcut donanımda
   `create_cross_adapter_shared()` her zaman başarısız olduğu için)
   find-references ve/veya kod incelemesiyle teyit et — "hiç koşmuyor"
   iddiasını kabul etmeden önce doğrula.
4. Kodun kendi yorumunun senkronizasyon eksikliğini gerçekten itiraf edip
   etmediğini kontrol et (üç raporun alıntıladığı "SENKRONİZASYON İÇERMİYOR"
   yorumu).

### Faz 1 (yaklaşım — basit, muhtemelen tek satır)
Üç inceleyici de aynı yönde: branch'i sil ya da `return nullptr` ile
fail-closed yap. Faz 0 bulgusuna göre hangisinin daha uygun olduğuna karar
ver (silme daha temiz ama gelecekte cross-adapter desteği planlanıyorsa
guard'lı bırakmak daha esnek olabilir — CONTEXT.md'de böyle bir plan yoksa
silmeyi tercih et). Onaya sun.

### Faz 2-3
Küçük, tek commit muhtemelen yeterli. Regresyon: mevcut ctest paketi
etkilenmemeli (bu yol zaten hiç koşmuyor).

---

## J1 — `rj_push_scene_names` sınırsız `CStr::from_ptr`

**V9 kaynağı:** Opus 6.6, Fable5 6.6 (2/3) — **V8 KÖR NOKTASI (I24)**

### Faz 0 (zorunlu)
1. `src/orchestrator/src/ffi.rs`'te `rj_push_scene_names`'i bul.
2. I24'ün diğer üç yerde (`rj_connection_lost`, `rj_reload_rules`,
   `rj_set_ws_password`) uyguladığı `cstr_bounded` desenini incele
   (I24 commit'i: e4f468b).
3. `rj_push_scene_names`'in gerçekten hâlâ sınırsız `CStr::from_ptr`
   kullandığını teyit et — V9 raporlarının tarih damgası (12.07.2026
   17:58-18:38) I24'ün push'undan (aynı gün, daha erken) sonra mı önce mi
   kontrol et; eğer I24'ten önceki bir kod durumuna bakıyorlarsa bulgu
   bayat olabilir. **Bunu doğrulamadan ilerleme.**
4. Mevcut manuel `MAX_NAME_LEN` kırpma mantığının tam olarak nasıl
   çalıştığını çıkar (Opus'un raporu bunun kaldırılabileceğini iddia
   ediyor).

### Faz 1 (yaklaşım — I24 deseninin tekrarı, muhtemelen tasarım turu gerekmez)
Faz 0 doğrularsa: `cstr_bounded(ptr, MAX_NAME_LEN)` kullan, manuel kırpma
mantığını kaldır. Bu I24'ün birebir devamı olduğundan ayrı bir onay turu
gerekmeyebilir — ama küçük bir onay isteği olarak Faz 0 sonucunu bildir.

### Faz 2-3
Tek commit. Birim test: I24'teki `cstr_bounded` testleriyle aynı desen
(normal isim geçer, aşırı uzun/NUL'suz isim güvenli reddedilir).

---

## J4 — `ExternalMemoryBridge::get_frame_images` `static` slot

**V9 kaynağı:** Opus 1.5 (HIGH), Minimax (kırık raporun sağlam parçası),
GLM 5.1 (3/3) — **I23'ün bilinçli ertelediği karar, yeniden gündemde**

### Faz 0 (zorunlu — bu maddede özellikle dikkatli ol)
1. I23'ün Faz 1 tasarım kararını hatırla: `static uint32_t slot`
   `external_memory_bridge.cpp:40`'ta bilinçli olarak korunmuştu ("kapsamı
   şişirmemek için static'i korurum, yalnız değerini expose ederim").
   I23 sonrası bu kodun **güncel halini** oku — slot artık tek doğruluk
   kaynağı olarak dışarı akıtılıyor, ama fonksiyon-yerel static olma
   durumu değişmedi.
2. Üç inceleyicinin iddiasını teyit et: gerçekten process-global mutable
   state mi (iki `ExternalMemoryBridge` örneği veya farklı thread'lerden
   çağrılırsa çakışma riski var mı)? Projede şu an kaç `ExternalMemoryBridge`
   örneği yaratılıyor, hangi thread'den çağrılıyor — find-references.
3. **Gerçek risk değerlendirmesi:** Eğer projede tek bir bridge örneği ve
   tek bir çağıran thread varsa (muhtemelen öyle), bu şu an aktif bir bug
   değil — üç inceleyicinin "risk" dediği şey gelecekteki bir refactor'da
   ortaya çıkabilecek bir kırılganlık. Bunu Faz 0 raporunda açıkça belirt;
   "aktif bug" ile "kırılganlık/kod kokusu" arasındaki fark önemli.

### Faz 1 (onay gerekli — I23'ün kararını değiştiriyoruz)
Opus'un önerdiği değişiklik: `slot`'u zaten var olan `pool_index_`
member'ına taşı. Bu, I23'ün "kapsamı şişirmeme" kararını tersine çeviriyor
— onay iste, otomatik uygulama. Onay isteğinde şunu netleştir: I23'ün
kararı yanlış değildi (o zaman kapsamı doğru sınırlamıştı), şimdi üç
bağımsız kaynağın aynı noktayı işaret etmesi kararı yeniden değerlendirmeye
değer kılıyor.

### Faz 2-3
I23'ün zaten dokunduğu dosya — küçük, cerrahi bir değişiklik olmalı.
`SlotRingTest` (I23'te eklenen) hâlâ PASS olmalı, regresyon riski düşük
ama I23'ün "riskli bölge" (I32 geçmişi) uyarısı burada da geçerli.

---

## J2 — SRT output, I18 FFI-sink desenini almamış

**V9 kaynağı:** Opus 5.5 (1/3, ama somut) — **V8 TAKİBİ (I18)**

### Faz 0 (zorunlu)
1. I18'in wasapi'de kurduğu deseni tam olarak hatırla: `AudioFrameCallback`
   tipi (ham fonksiyon-pointer + `void*` user_data), `init()`'e enjeksiyon,
   gerçek `::rj_*` çağrısının `AudioSubsystem::on_connection_lost`/
   `on_metrics` passthrough'una taşınması (I18 commit'i: 2640fe8).
2. `src/pipeline/output/srt_output.cpp`'de `rj_metrics_push` (`send_internal`),
   `rj_connection_lost`, `rj_start_monitor` (`init_internal`) çağrılarının
   gerçekten hâlâ doğrudan (sink olmadan) yapıldığını teyit et.
3. SRT'nin çağıranı/sahibi kim (`OutputSubsystem`?) — I18'de audio için
   `AudioSubsystem` neyse, SRT için hangi sınıfın aynı rolü oynayacağını
   belirle.
4. **Fark noktası:** I18'de `rj_connection_lost` self-healing/recovery
   zincirine bağlıydı, davranışsal eşdeğerlik şartı vardı. SRT'de de aynı
   zincire bağlı mı (muhtemelen evet, `MediaEvent::SourceDisconnected`)?
   Bunu teyit et — J2'nin de I18 gibi "davranış hiç değişmemeli" hedefi
   taşıyıp taşımadığını belirler.

### Faz 1 (onay gerekli — tasarım I18'in birebir mirror'ı olmalı)
I18'in izlediği deseni (ham fonksiyon-pointer sink + passthrough) SRT'ye
uyarla. Yeni mimari icat etme. Faz 0'da bulunan sahiplik yapısına göre
(`OutputSubsystem` sink'leri) somut tasarımı onaya sun.

### Faz 2-3
I18 şablonunu izle: sink tanımı → wiring → passthrough. Davranışsal
eşdeğerlik testi (I18'de olduğu gibi, mümkünse simülasyon, değilse kod
incelemesiyle + açık dürüstlük notu).

---

## Genel Sıra ve Commit Disiplini

Önerilen sıra: **J3 → J1 → J4 → J2** (basitten karmaşığa, düşükten
yüksek dikkat gerektirene). Her madde kendi Faz 0 raporunu ayrı ayrı
sunacak — dördünü birden Faz 0'a sokmak yerine, birini bitirip onaya
sunduktan sonra bir sonrakine geçmek tercih edilir (V8'deki alt-grup
disiplini).

- Her madde en az 1 commit; J2 ve J4 muhtemelen 2-3 commit (kod + test +
  docs).
- `tests/baseline_metrics.txt` asla commit edilmez.
- Push: V8'deki gibi, her madde tamamlanıp doğrulandığında ayrı push
  öncesi onay — dördünü topluca beklemeye gerek yok, her biri bağımsız.
- Dokümantasyon: her madde kapandığında `FABLE5_BUG_PLAN_V9.md`'de o
  maddenin durumu güncellenmeli (J1 ✅, J3 ✅ vb.), `SESSION_NOTES.md`'ye
  ek.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık — V9'un statik-tarama kökenli
  olması nedeniyle bu ayrım burada özellikle önemli.

Faz 0 bulguları V9 belgesinin varsayımlarıyla çelişirse (örn. J1'in bayat
olduğu, J4'ün aktif bug olmadığı ortaya çıkarsa) implementasyona geçmeden
dur, raporla — V8'in kurulu deseni (I2/I3/I8/I9/I10/I14/I17 serisi) burada
da geçerli.
