# TALİMAT: V9 Sprint 3 — J9, J10, J11, J12, J13

**Kaynak:** `docs/FABLE5_BUG_PLAN_V9.md` (J9-J13), tekil-kaynaklı bulgular
(Opus 4.8 veya GLM 5.2'den, çapraz doğrulanmamış).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü ve Kritik Uyarı

Sprint 1-2 tamamen kapandı, sekiz madde de (J1-J8) gerçek çıktı — hiçbiri
hayalet değildi, yalnızca aktiflik derecesi değişti (bazıları gerçek açık,
bazıları latent/tutarlılık). Sprint 3 **yapısal olarak farklı**: beş
maddenin hepsi 🔵 **1/3 konsensüs** — tek bir modelin bulduğu, diğer
ikisinin hiç değinmediği iddialar. V9 planının kendi notu:

> "Tek inceleyicili ama somut ve makul — doğrulama gerekir."

**Bu, önceki sprintlerden daha yüksek bir şüphecilik eşiği gerektirir.**
J7'de tam olarak bu oldu: GLM'in "doğru" bulduğu bir protokolü kendi
başımıza yeniden doğruladık (körü körüne güvenmedik) ve gerçekten
sorunsuz çıktı — yalnızca adlandırma iyileştirildi. Sprint 3'te bazı
maddeler benzer şekilde "iddia abartılı/yanlış" çıkabilir; bu bir
başarısızlık değil, disiplinin doğru çalıştığının kanıtıdır.

**Özellikle J11 ve J12** (GLM'in tekil "critical" iddiaları) için
talimat: **önce iddianın kendisi kanıtlanacak, sonra düzeltme
düşünülecek.** Faz 0'da "bu gerçek bir race mi" sorusunun cevabı "hayır"
çıkarsa, bu bir başarısız Faz 0 değil, doğru sonuçtur — raporla ve kapat.

Sıra önerisi: **J13 → J11 → J12 → J9 → J10** (en küçük/somuttan,
doğrulama yükü en ağır olana; J11/J12 ilişkili dosyalarda olduğundan
art arda).

---

## J13 — `GpuInteropSubsystem::cache_last_images` shutdown'da temizlenmiyor

**V9 kaynağı:** Opus 2.2 (1/3)

### Faz 0 (zorunlu)
1. `src/pipeline/gpu_interop_subsystem.cpp`, `shutdown()`'ı bul.
2. `ext_bridge_`'in sıfırlandığını (VkImage'ların yok edildiğini) teyit et.
3. `last_staging_vk_`/`last_target_vk_` cache alanlarının (veya benzer
   isimli atomikler/üyeler) `shutdown()` içinde temizlenip temizlenmediğini
   kontrol et.
4. Bu cache'e `shutdown()` sonrası erişebilecek bir çağıran var mı
   (`get_last_frame_images()`'in GL thread'den `shutdown()` sonrası
   çağrılma ihtimali) — gerçek bir use-after-free senaryosu mümkün mü,
   yoksa çağıran zaten `shutdown()` sonrası hiç çağrılmıyor mu
   (find-references + çağıran tarafın yaşam döngüsü kontrolü).

### Faz 1 (yalnızca Faz 0 doğrularsa)
Opus'un önerisi basit: `shutdown()` içinde cache atomiklerini `nullptr`'a
set et. Küçük, düşük riskli — I13/I23'ten (I23'ün de bu dosyayla komşu
olduğunu unutma, `ext_bridge_`) hatırlanacağı gibi bu bölge dikkat
gerektiriyor ama bu değişiklik cerrahi.

### Faz 2-3
Tek commit muhtemelen yeterli. Regresyon: `PipelineCharacterization`
(shutdown senaryosunu zaten kapsıyor, J8'de de bu rol için kullanıldı).

---

## J11 — `shared_texture_`'a kilitsiz erişim (preview toggle race)

**V9 kaynağı:** GLM 1.1 (1/3, "critical" işaretlenmiş, diğer iki rapor
değinmiyor) — **doğrulama önce**

### Faz 0 (zorunlu, standarttan daha titiz — bu bir "critical" iddiası)
1. `src/pipeline/capture/capture_dxgi.cpp`, `capture_next()`'i (frame
   thread) ve `ensure_preview_staging()`/`set_preview_requested()`'ı
   (herhangi bir thread'den çağrılabilir) bul.
2. `cb_mutex_`'in (veya varsa başka bir kilidin) hangi alanları
   koruduğunu tam olarak çıkar — `shared_texture_`, `keyed_mutex_shared_`,
   `staging_texture_`, `preview_staging_` gerçekten bu kilidin **dışında**
   mı erişiliyor?
3. **Somut senaryo kur:** preview toggle sırasında (`set_preview_requested`
   çağrılırken) `capture_next()` aynı anda bu texture'lara erişiyorsa,
   gerçekten bir `Reset()`/use-after-free oluşabiliyor mu? Kod akışını
   adım adım izle, varsayımla değil.
4. **Bu maddenin J6'nın dokunduğu aynı dosyada olduğunu unutma** — J6'nın
   eklediği spin/timeout mantığının bu senaryoyla etkileşimi var mı
   kontrol et (yeni bir yan etki yaratmadığından emin ol).
5. Sonuç üç şıktan biri olacak: (a) gerçek race, düzeltme gerekli,
   (b) teorik ama pratikte imkânsız (örn. preview toggle çok nadir/
   senkronize başka bir yerde), (c) GLM'in yanlış okuması, race yok.

### Faz 1 (yalnızca (a) durumunda)
Faz 0 bulgusuna göre şekillenecek — muhtemelen ilgili alanları
`cb_mutex_` kapsamına almak veya preview toggle'ı frame thread'e
sinyalleyip senkron yapmak. Önce somut tasarımı onaya sun.

### Faz 2-3 (yalnızca (a) durumunda)
Davranışsal eşdeğerlik + regresyon (`PipelineCharacterization`,
`GpuResourcePitch`). Race condition'ı deterministik test etmek zor —
mümkünse thread sanitizer/stres testi düşün, değilse kod incelemesiyle
doğrula ve dürüstlük notunu açıkça yaz.

---

## J12 — `client_sock_` atomik olmayan erişim (SRT)

**V9 kaynağı:** GLM 1.2 (1/3, "critical") — **doğrulama önce, J11 ile
aynı yaklaşım**

### Faz 0 (zorunlu)
1. `src/pipeline/output/srt_output.cpp`, worker thread'in `client_sock_`'u
   kapatıp `SRT_INVALID_SOCK` yazdığı yeri ve `send_internal`'in bu
   socket'i kullandığı yeri bul.
2. **J2'nin bu dosyaya zaten dokunduğunu unutma** — I18/J2'nin eklediği
   sink mimarisinin bu senaryoyla ilişkisi var mı (yeni bir yan etki
   riski var mı) kontrol et.
3. Worker thread ile frame/encode thread'in gerçekten örtüşebileceği
   somut bir senaryo kur — `client_sock_`'a yazma ve okuma gerçekten
   farklı thread'lerden, senkronizasyonsuz mu oluyor?
4. Sonuç yine üç şıktan biri: gerçek race / teorik-pratikte-imkânsız /
   yanlış okuma.

### Faz 1 (yalnızca gerçek race doğrulanırsa)
Muhtemelen `std::atomic<SRTSOCKET>` veya mevcut bir mutex'in kapsamına
alma. Basit, cerrahi bir çözüm tercih edilmeli — büyük bir yeniden yazım
değil.

### Faz 2-3
J11 ile aynı yaklaşım: davranışsal eşdeğerlik + regresyon, race
determinizmi test zorluğu dürüstçe belirtilecek.

---

## J9 — NVENC `set_resolution` init'te `maxEncodeWidth/Height` ayarlamıyor

**V9 kaynağı:** Opus 2.3 (1/3)

### Faz 0 (zorunlu)
1. `src/pipeline/encode/encode_nvenc.cpp`, encoder init parametrelerini
   ve `set_resolution`'ı bul.
2. `maxEncodeWidth`/`maxEncodeHeight`'ın gerçekten init'te set edilip
   edilmediğini teyit et — NVENC SDK dokümantasyonuna göre bu alanların
   gerçekten "yalnız başlangıç çözünürlüğüne izin ver" anlamına gelip
   gelmediğini doğrula (varsayma, kontrol et).
3. **Bu, self-healing çözünürlük düşürme/yükseltme yoluyla (I33+I11,
   RuleEngine) doğrudan ilişkili** — `RJ_ACTION_RESOLUTION_*` aksiyonunun
   gerçekte reconfig'i tetikleyip tetiklemediğini, başarısız olursa ne
   olduğunu (sessizce mi yutuluyor, loglanıyor mu) izle.
4. Gerçek bir fonksiyonel hata mı (reconfig başarısız oluyor) yoksa
   teorik bir SDK-kısıt okuması mı — mümkünse loglardan/davranıştan
   kanıt ara, yoksa kod incelemesiyle en iyi tahmini ver.

### Faz 1 (yalnızca gerçek hata doğrulanırsa)
Init'te `maxEncodeWidth/Height`'ı olası maksimum çözünürlüğe (veya NVENC
SDK'nın önerdiği bir üst sınıra) set etmek. Basit parametre düzeltmesi
olması bekleniyor.

### Faz 2-3
Regresyon + mümkünse gerçek bir çözünürlük-değişikliği senaryosunu test
et (birim test zor olabilir, entegrasyon/manuel doğrulama gerekebilir —
dürüstçe belirt).

---

## J10 — Bitrate azaltma `REDUCED_BITRATE_KBPS` sabitini yok sayıyor

**V9 kaynağı:** Minimax'ın kırık raporunun kendi içinde tutarlı tek
bölümü (kaynak güvenilirliği düşük, ama iddia kendi içinde somut)

### Faz 0 (zorunlu — özellikle "kasıtlı mı tutarsız mı" ayrımı)
1. `pipeline.cpp::apply_action`, `RJ_ACTION_BITRATE_REDUCE` case'ini bul.
2. `current * 0.85f` yüzdesel azaltmanın gerçekten kullanıldığını teyit
   et.
3. Rust tarafındaki `REDUCED_BITRATE_KBPS` sabitinin (muhtemelen
   `rules.rs` veya `healing.rs`'te) tam olarak ne amaçla tanımlandığını
   ve `RjAction::param1`'in bu değeri gerçekten taşıyıp taşımadığını
   izle.
4. **Kritik soru:** Bu kasıtlı bir tasarım mı (yüzdesel azaltma daha
   kademeli/güvenli, `param1`'in C++ tarafında yok sayılması bilinçli)
   yoksa gerçek bir tutarsızlık mı (RuleEngine bir hedef gönderiyor,
   C++ onu hiç kullanmıyor, davranış RuleEngine'in niyetinden sapıyor)?
   Bunu `RuleEngine`'in `param1`'i neden gönderdiğini (commit geçmişi,
   yorum, ilişkili I19/I20 healing mode bağlamı) inceleyerek belirle.

### Faz 1 (yalnızca gerçek tutarsızlık doğrulanırsa)
Faz 0 bulgusuna göre: ya C++ tarafı `param1`'i kullanacak şekilde
düzeltilir, ya da kasıtlı olduğu netleşirse V9'da "çürütüldü, tasarım
kararı" notuyla kapatılır (I29/I31 deseni). İkisi de meşru sonuç.

### Faz 2-3
Değişiklik gerekiyorsa küçük, cerrahi. Regresyon: healing'in bitrate
azaltma davranışını test eden mevcut bir test varsa çalıştır, yoksa
davranış değişikliğini raporda açıkça belirt.

---

## Genel Sıra ve Commit Disiplini

- Sıra: J13 → J11 → J12 → J9 → J10. Her biri kendi Faz 0 raporunu ayrı
  sunacak, onay beklenecek.
- **Önemli: bir maddenin Faz 0'ı "iddia yanlış/abartılı" sonucuna
  varırsa, bu maddeyi V9 planında kapat (I29/I31 deseni) ve bir sonrakine
  geç — implementasyona zorlanmaz.**
- Her düzeltilen madde en az 1 commit.
- `tests/baseline_metrics.txt` asla commit edilmez.
- Push: her madde bağımsız, tamamlanan hemen push onayına sunulsun.
- Dokümantasyon: her madde kapandığında (düzeltilerek veya çürütülerek)
  `FABLE5_BUG_PLAN_V9.md` güncellenmeli, `SESSION_NOTES.md`'ye ek.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık — bu sprintte özellikle J11/J12'nin
  race-condition doğası nedeniyle "test edildi" iddiasına karşı dikkatli
  ol (race'ler deterministik test edilmesi zor problemlerdir).

Faz 0 bulguları V9 belgesinin varsayımlarıyla çelişirse (iddia bayat,
yanlış konumlandırılmış, veya tamamen geçersiz çıkarsa) implementasyona
geçmeden dur, raporla — kurulu proje deseni (I2/I3/I8/I9/I10/I14/I17/J7
serisi).
