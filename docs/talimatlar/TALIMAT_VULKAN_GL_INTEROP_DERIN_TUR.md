# TALİMAT: Derin Vulkan/GL Interop Turu (I23'ün Devamı) — v2, Kesin Bulgularla

**Kaynak:** Üç V9 incelemesinin (Fable5, Opus 4.8, GLM 5.2) "VULKAN/GL
INTEROP" bölümleri — bu sefer tam metinleriyle yeniden çıkarıldı (önceki
taslakta bu bölge yalnızca genel bir "çelişiyorlar" notuyla geçilmişti,
şimdi madde madde kesin).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

Bu bölgede üç rapor **kısmen çelişiyor değil** — asıl durum daha
nüanslı: bazı maddeler 2/3 çapraz doğrulanmış, bazıları GLM'in **kendi
raporunun içinde** birbirini kısmen çürüten iki ayrı gözlem, bazıları da
zaten J6/J7 ile kapatılmış maddelerin bir kısmını kapsıyor ama **J7'nin
kapsamadığı ek bir risk** taşıyor. Bu talimat artık genel bir "sıfırdan
denetle" turu değil — **yedi somut bulgu (K1-K7)**, her biri kaynağı ve
güven seviyesiyle net.

**En kritik iki bulgu (öncelik sırası bunlara göre kuruldu):**
- **K2** — J7'nin kapatmadığı bir keyed-mutex riski: Vulkan tarafının
  acquire timeout'u **sonsuz** (`UINT32_MAX`), D3D11 tarafı 16ms sınırlı.
  D3D11 acquire başarılı olup `CopyResource` sırasında exception/device-lost
  olursa (`ReleaseSync` atlanırsa), Vulkan tarafı **sonsuza kadar** GPU
  kuyruğunda bloke olabilir — tüm uygulamanın donması riski.
- **K5/GLM iç çelişkisi** — GLM'in kendi raporu 3.1'de "senkronizasyon
  zinciri doğru" derken, 3.2'de **aynı mekanizmanın** (`is_slot_signaled()`)
  bir slot'un ilk kullanımında GL fence beklemesini atladığını, bu yüzden
  Vulkan kopyası bitmeden GL'in o texture'ı okuyabileceğini buluyor. Bu
  iki gözlem birbirini çürütmüyor aslında — 3.1 "genel akış doğru"
  derken 3.2 "ama ilk-kullanım edge-case'i kaçmış" diyor. Faz 0'da bu
  ayrımı netleştir.

---

## Bulgu Tablosu

| # | Bulgu | Kaynak | Konum | Durum |
|---|---|---|---|---|
| K1 | GL memory object resolution değişiminde yeniden oluşturulmuyor — yalnız texture yeniden yaratılıyor, memory object eski (küçük) boyutta kalıyor → resize sonrası GL interop UB/tamamen bozuk | Fable5 3.5 **+** Opus 3.1 (2/3) | `preview_widget.cpp`, GL interop bloğu | Doğrulama + düzeltme |
| K2 | Keyed-mutex timeout asimetrisi: D3D11 16ms sınırlı, Vulkan `UINT32_MAX` sonsuz. D3D11 acquire sonrası `CopyResource` exception atarsa `ReleaseSync` atlanır → Vulkan sonsuza kadar bloke, TDR/app-hang | Fable5 3.3 (1/3, ama **J7'nin kapsamadığı** ek risk) | `capture_dxgi.cpp` + `copy_optimizer.cpp` | Doğrulama + düzeltme |
| K3 | `slot_gl_signaled_` bayrağının hayat döngüsü kırılgan — iki farklı açıdan: (a) Fable5: GL o slot'u hiç render etmezse bayrak sonsuza dek "signaled" kalır, gelecekteki submit'ler sinyali sessizce düşürür → kalıcı senkronsuzluğa geçiş; (b) Opus: binary semaphore'ların value'su olmadığından farklı bir frame'in sinyaliyle eşleşme (stale pairing) riski, timeline semaphore'a geçiş öneriliyor (extension zaten probe ediliyor) | Fable5 3.2 **+** Opus 3.2 (2/3, farklı açılardan aynı mekanizma) | `copy_optimizer.cpp` (submit) + `preview_widget.cpp` (paintGL wait) | Doğrulama + düzeltme |
| K4 | `glClientWaitSync` dönüş değeri kontrol edilmiyor — `GL_TIMEOUT_EXPIRED` olsa bile kopya devam ediyor → GL hâlâ okurken hedef image üzerine yazma (write-while-read) | Fable5 3.1 (1/3, somut) | `preview_widget.cpp`, `paintGL()` GL-interop wait | Doğrulama + düzeltme |
| K5 | GL fence bir slot'un ilk kullanımında (`gl_draw_fences_[pool_idx] == nullptr`) atlanıyor — **GLM'in kendi 3.1 bulgusuyla gerilimli**: 3.1 zinciri "doğru" derken 3.2 bu edge-case'i buluyor. İlk kullanımda Vulkan kopyası bitmeden GL okuyabilir | GLM 3.2 (1/3, ama GLM'in kendi 3.1'iyle çapraz okunmalı) | `preview_widget.cpp`, `paintGL()` | Doğrulama + düzeltme |
| K6 | Tüm pool slot'larının **aynı** D3D11 shared-texture memory'sini import ettiği varsayımı doğrulanamıyor (Zig kaynağı incelemeden görülemiyor) — yanlışsa keyed-mutex yanlış `VkDeviceMemory`'yi koruyor, sessizce senkronu geçersiz kılıyor | Fable5 3.4 (1/3, doğrulama-öncelikli, J13/I13 "assertion ekle" deseniyle) | `preview_widget.cpp` ↔ Zig bridge tarafı | Yalnızca doğrulama/assertion |
| K7 | GLM'in "sorun yok" dediği iki alan: (a) 3.1 — layout transition zinciri doğru (K5 ile birlikte okunmalı, kısmi); (b) 3.3 — external memory queue-family ownership transfer barrier'ları spec'e uygun | GLM 3.1(a)/3.3(b) (kendi içinde "no fix needed" sonucu) | `copy_optimizer.cpp`, `execute_copy()` | Yeniden doğrula, muhtemelen kapat |

**Zaten kapalı, bu turun kapsamı DIŞINDA (karıştırma):**
- Fable5 3.6 = Opus 3.4 → **J6** (AMD spin-wait) — kapalı, dokunma.
- Fable5 3.3'ün "key değerleri protokolü" kısmı (0/1 ring tutarlılığı,
  paylaşımlı sabit yokluğu) → **J7** ile kapatıldı (sabitler
  `reji_constants.h`'ye çıkarıldı). **Ama K2 (timeout asimetrisi +
  exception-safety) J7'nin kapsamına hiç girmedi** — J7 yalnızca
  adlandırma/sabit çıkarma refactoruydu, timeout değerlerine veya
  hata-yolu güvenliğine dokunmadı. Bunu Faz 0'da teyit et.

---

## Faz 0 — Doğrulama (kod yazmadan, her madde için)

Her K-maddesi için:
1. İddia edilen kod konumunu bul, güncel `master`'a karşı (I23/J6/J7
   sonrası hâliyle) yeniden oku — bayat olabilir, körü körüne kabul etme.
2. **K2 için özellikle:** J7'nin commit'ini (`452a4bb`) incele — gerçekten
   yalnızca sabit isimlerini mi değiştirdi, yoksa timeout değerlerine de
   dokundu mu? Net ayrım yap.
3. **K5 için özellikle:** GLM 3.1'in "doğru" dediği akış ile 3.2'nin
   bulduğu ilk-kullanım boşluğunun gerçekten aynı mekanizmanın (
   `is_slot_signaled()`) iki farklı durumu (steady-state vs ilk kullanım)
   olduğunu doğrula — bu, GLM'in kendi kendiyle çelişmediğini, yalnızca
   eksiksiz bir analiz yapmadığını gösterir.
4. **K1 ve K3 için:** İki bağımsız kaynağın aynı kök nedene mi işaret
   ettiğini (gerçek 2/3 çapraz doğrulama) yoksa farklı iki soruna mı
   (yanlışlıkla birleştirilmiş) işaret ettiğini netleştir.
5. **K6 için:** Zig tarafı bridge kodunu (`external_memory_bridge.zig`
   gibi) incele — Fable5'in "buradan doğrulanamaz" dediği şeyi Claude
   Code doğrudan kod erişimiyle doğrulayabilir, bu bir avantaj.
6. **K7 için:** GLM'in "no fix needed" sonucunu bağımsız olarak
   yeniden değerlendir (J11/J7 deseni — başka bir modelin "doğru" dediğine
   körü körüne güvenme, kendin doğrula).

**Faz 0 çıktısı:** Her K-maddesi için (a) gerçek/latent/teorik/çürütülebilir
sınıflandırması, (b) I23/J6/J7 ile ilişkisinin netleşmiş hali. Onaya sun.

## Faz 1 — Tasarım (yalnızca gerçek/latent çıkan maddeler için)

Öncelik sırası (risk × doğrulama gücüne göre):
1. **K2** (hang riski, gerçek olursa en kritik) — timeout'u bounded yap
   (Fable5 önerisi: ~100ms), acquire/release arasına scope guard ekle
   (exception-safe RAII deseni, I9/I10 dersleriyle tutarlı).
2. **K1** (2/3 doğrulanmış, "GL interop tamamen bozuk" iddiası) —
   resolution değişiminde memory object + texture'ı birlikte yeniden
   oluştur/re-import et.
3. **K3** (2/3, iki farklı açı) — Faz 0'ın hangi spesifik race'in gerçek
   olduğunu netleştirmesine göre: bayrak epoch/timeout ekleme (Fable5) ve/
   veya timeline semaphore'a geçiş (Opus, daha büyük değişiklik — kapsamı
   büyütüp büyütmeyeceğine karar ver, büyükse ayrı madde öner).
4. **K5** — ilk-kullanım edge-case'inde GL fence'i atlamak yerine bekle,
   veya slot'un ilk karesinde interop yerine CPU fallback'e düş (GLM'in
   iki önerisinden birini seç, gerekçele).
5. **K4** — `glClientWaitSync` dönüşünü kontrol et, timeout'ta kareyi
   düşür (kopyayı atla).
6. **K6** — yalnızca assertion/log ekle (I13/J13 "savunma-derinliği"
   deseni), büyük bir refactor değil.
7. **K7** — Faz 0 teyidi "doğru" çıkarsa V9/bu plana "GLM'in bulgusu
   doğrulandı, no-op" notuyla kapat (I29/I31/J11 deseni).

Her biri için tasarımı onaya sun, implementasyondan önce.

## Faz 2-3 — İmplementasyon ve Test

- Küçük, sıralı commit'ler (K2 önce — en yüksek risk).
- Regresyon: `PipelineCharacterization` + `GpuResourcePitch` +
  `SlotRingTest` (I23) her commit'te.
- Bu bölge WGC aktifken inert — runtime testi zor, kod incelemesi + saf
  seam testleri (I10/I23 deseni) birincil doğrulama, dürüstçe belirt.
- `tests/baseline_metrics.txt` asla commit edilmez.

## Faz 4 — Dokümantasyon

- `.claude/skills/vulkan-interop-debug/SKILL.md` güncellenmeli (özellikle
  K2'nin hang riski ve K1'in resize-breaks-interop bulgusu kalıcı bilgi
  olarak eklenmeli).
- Bulgu sayısı/büyüklüğüne göre V9'a ek mi (K-maddeleri V9 planına
  eklenebilir) yoksa ayrı bir belge mi (öneri: V9'a ek — bu hâlâ V9'un
  taramasından, yalnızca gecikmeli işlendi) — kullanıcıya sor.
- `SESSION_NOTES.md`'ye ekle, talimatı arşivle.

## Sabit Kurallar

- Bu bölge geçmişte gerçek memory corruption'a (I32) ve ciddi drift
  bug'ına (I23) ev sahipliği yaptı — hız değil dikkat önceliktir.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Push: her madde/grup tamamlanınca ayrı onay (kod değiştiren işler için);
  saf çürütme/dokümantasyon commit'leri doğrudan push edilebilir.
- Faz 0 bulguları bu talimatın sınıflandırmasıyla çelişirse dur, raporla
  — kurulu proje deseni (I2/I3/I8/I9/I10/I14/I17/J7/J11 serisi).
