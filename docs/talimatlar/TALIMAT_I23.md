# TALİMAT: I23 — `execute_copy()` Slot Senkronu / GPU-Interop Derinlemesine İnceleme

**Kaynak:** `docs/FABLE5_BUG_PLAN_V8.md` (I23), Sprint 3-4 Faz 0 ön-triyajı (I23 → (b) ayrıldı)
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

Sprint 3-4'ün ön-triyajında I23 şu bulguyla (b) sınıfına (ayrı tur) alındı:

> "İncelemede büyüdü: derin GPU-interop bölgesi (I2/I3/I32 territory),
> WGC'de inert (`execute_copy` hiç koşmuyor), slot-senkron
> (`last_used_slot`/`next_slot`) zaten mevcut (F8/G2/I13). Riskli, kendi
> turunu hak ediyor."

Bu talimat, o ayrı turun kendisi. Konum: `copy_optimizer.cpp::execute_copy`
ve `external_memory_bridge`.

**Kritik bağlam (önceki oturumlardan, atlanmayacak):**

1. **WGC/DXGI topoloji gerçeği** — bu proje boyunca en büyük tek keşif:
   aktif capture/encode yolu WGC üzerinden NVIDIA-only zero-copy'dir.
   `copy_optimizer.cpp`'nin GPU-interop kodu (cross-adapter keyed-mutex,
   `execute_copy`) yalnızca **DXGI fallback** yolunda çalışır — bu yol,
   `WgcScreenCapture::is_supported()` Win11'de her zaman `true` döndüğü
   için **fiilen hiç seçilmiyor**, yalnızca WGC başarısız olursa devreye
   girer (nadiren).
2. Bu, I23'ün **düşük runtime riski ama sıfır olmayan önemi** anlamına
   gelir: kod şu an çalışmıyor, ama WGC arızalanırsa (farklı donanım,
   sürücü sorunu, gelecekteki bir Windows sürümü değişikliği) devreye
   girecek tek yol bu. Bu yüzden "zaten çalışmıyor, önemsiz" sonucuna
   atlamak yanlış olur — I4/I5/I27/I28/I30/I32'de zaten bu gerekçeyle
   düzeltmeler yapıldı ve "boşa gitmedi" olarak not edildi.
3. **I32'nin kendisi bu bölgede kritik bir düzeltmeydi** (`invalidate_pool()`
   üçlü-free, gerçek memory corruption riski). I23'ün I32 ile çakışıp
   çakışmadığı, aynı slot senkron mekanizmasına dokunup dokunmadığı
   **ilk kontrol edilmesi gereken şey**.
4. `.claude/skills/vulkan-interop-debug/SKILL.md` bu ayrımı (Aktif yol —
   WGC / Fallback yol — DXGI) yansıtacak şekilde tam revize edilmişti —
   bu skill implementasyondan önce okunmalı, güncel kalması bu görevin
   bir parçası.

---

## Faz 0 — Derin Keşif (kod yazmadan, zorunlu — bu bölge için standarttan daha titiz)

1. `.claude/skills/vulkan-interop-debug/SKILL.md`'yi oku.
2. `execute_copy()`'nin tam akışını çıkar: hangi koşulda çağrılıyor (yalnız
   DXGI fallback aktifken mi — bunu find-references ile teyit et, "WGC'de
   inert" iddiasını yeniden doğrula, önceki bulguyu körü körüne kabul etme),
   hangi kaynaklara (texture, keyed-mutex, `external_memory_bridge`)
   dokunuyor.
3. **I23'ün asıl iddiasını netleştir** — V8 planındaki orijinal tanımı oku:
   `execute_copy`'de tam olarak hangi senkronizasyon sorunu iddia ediliyor
   (race condition mi, slot yeniden kullanım hatası mı, keyed-mutex sırası
   mı)? Bu, ön-triyajda tam netleşmemişti.
4. `last_used_slot`/`next_slot` mekanizmasının (F8/G2/I13'te kurulmuş)
   tam olarak neyi çözdüğünü ve I23'ün iddia ettiği sorunun bunun
   **kapsamında mı dışında mı** kaldığını belirle. Üç olası sonuç:
   - (a) I23'ün sorunu zaten bu mekanizmayla çözülmüş → I29/I31 gibi
     çürütülüp kapatılabilir.
   - (b) I23'ün sorunu gerçek ve bu mekanizmanın kapsamı dışında → asıl
     düzeltme gerekiyor.
   - (c) I23'ün sorunu kısmen geçerli, mekanizma yanlış konumda/eksik
     uygulanmış → I2/I3 deseni (yeniden çerçeveleme).
5. `external_memory_bridge`'in cross-adapter (AMD+NVIDIA) paylaşımla
   ilişkisini teyit et — önceki oturumda bu tür paylaşımın endüstri çapında
   bilinen çözülmemiş bir sınırlama olduğu araştırılıp kapatılmıştı; I23
   bu sınırlamanın **kendisini çözmeye çalışmıyor**, yalnız var olan
   fallback kodunun kendi iç tutarlılığını (slot senkronu) hedefliyor —
   bu ayrımı raporda net tut, kapsamı şişirme.

**Faz 0 çıktısı:** I23'ün gerçek iddiasının netleşmiş hali + yukarıdaki
(a)/(b)/(c) sınıflandırması + I32 ile çakışma/çakışmama analizi. Onaya
sun.

## Faz 1 — Yaklaşım Onayı (yalnızca (b) veya (c) sonucunda gerekli)

Faz 0 (a) ile sonuçlanırsa Faz 1 gerekmez, doğrudan kapanış dokümantasyonuna
geç (V8'de çürütme gerekçesi + `docs/talimatlar/` arşivi).

(b)/(c) durumunda:
- Düzeltmenin tam kapsamı (hangi fonksiyon/lokasyon).
- **Test edilebilirlik sorunu (bu görevün en zor kısmı):** kod normal
  çalışırken hiç tetiklenmiyor (WGC aktif). Doğrulama için seçenekler:
  - Sentetik/zorlanmış DXGI yolu (WGC'yi geçici olarak devre dışı bırakıp
    fallback'i tetikleyen bir test modu var mı, yoksa eklenmesi mi
    gerekiyor?) — varsa kullan, yoksa eklemenin bu görevin kapsamında
    olup olmadığını değerlendir (aşırı genişletme riski, dikkatli ol).
  - Saf/izole birim testi (slot senkron mantığını gerçek GPU kaynağı
    olmadan, mock/simülasyon ile test etme — I10'daki `seh_classify` saf
    seam testi gibi bir yaklaşım burada da uygulanabilir mi?).
  - Kod incelemesiyle doğrulama + açık dürüstlük notu (gerçek donanımda
    hiç çalıştırılmadı).
- Bu bölge riskli olduğundan (gerçek memory corruption geçmişi — I32),
  değişiklik minimal ve cerrahi olmalı; büyük bir yeniden yazım değil.

## Faz 2 — İmplementasyon (yalnızca gerekiyorsa, küçük commit'ler)

Faz 1 onayından sonra netleşecek. Genel kural: bu bölgede her commit
tek bir mantıksal değişiklik olmalı, mümkünse `vulkan-interop-debug`
skill'inin kayıtlı desenleriyle çapraz kontrol edilmeli.

## Faz 3 — Test ve Dürüstlük Sınırları

- Runtime testi mümkünse (sentetik DXGI yolu) çalıştır ve raporla.
- Mümkün değilse: kod incelemesi + saf seam testi + **açık ve öne çıkan**
  bir dürüstlük notu ("gerçek DXGI fallback yolunda hiç çalıştırılmadı,
  yalnız statik analiz + izole birim test").
- Regresyon: mevcut ctest paketi (özellikle `GpuResourcePitch`,
  `PipelineCharacterization`) PASS kalmalı.
- Bu kod inert olduğundan kullanıcıya görünen bir davranış değişikliği
  **olmamalı** (WGC aktif olduğu sürece) — bunu raporda açıkça belirt.

## Faz 4 — Kapanış Dokümantasyonu

- `FABLE5_BUG_PLAN_V8.md`: I23 durumu (çürütüldü / düzeltildi / yeniden
  çerçevelendi — Faz 0 sonucuna göre).
- `.claude/skills/vulkan-interop-debug/SKILL.md`: gerekiyorsa güncelle.
- `docs/SESSION_NOTES.md`, talimatı arşivle.
- Bu, **V8 planının I1-I34 arasındaki son açık maddesi** — kapandığında
  planın tamamı (I2/I3/I29/I31 çürütmeleri hariç tümü) kapanmış olacak.
  CONTEXT.md'ye ve istenirse dış senkron kaynaklarına (Notion/Todoist/
  Linear REJ-14) yansıt.

## Sabit Kurallar (hatırlatma)

- Küçük, mantıksal commit'ler; seri tamamlanınca push öncesi onay.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı bu görevde özellikle kritik — inert kod için
  abartılı doğrulama iddiasında bulunma.
- Faz 0 bulguları varsayımlarla (bu talimat dahil) çelişirse implementasyona
  geçmeden dur, raporla (kurulu proje deseni, I2/I3/I8/I9/I10/I14/I17
  serisi).
- Bu bölge geçmişte gerçek memory corruption'a (I32) ev sahipliği yaptı —
  hız değil dikkat önceliklidir.
