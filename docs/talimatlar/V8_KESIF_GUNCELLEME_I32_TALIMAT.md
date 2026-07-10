# Claude Code Talimatı — V8 Planını Keşif Bulgularıyla Güncelle + I32 Ekle

**Salt dokümantasyon — kod değişikliği yok.** `docs/SESSION_NOTES.md`'deki keşif
zaten var (commit `9ebeaf5`); bu talimat aynı bulguları `docs/FABLE5_BUG_PLAN_V8.md`'nin
KENDİSİNE işliyor — çünkü SESSION_NOTES bir oturum günlüğü, V8 planı ise
"güncel durum" referansı olmalı. İkisi tutarsız kalırsa, plana bakan biri
yanlış bilgiyle karşılaşır.

## 1. Öncelik Matrisi — I2, I3, I28, I29, I30, I31 satırlarını güncelle

Mevcut satırları SİLME, **açıklama sütununa keşif sonucunu ekle** (tarih
damgalı, orijinal bulguyu de koru — dürüstlük ilkesi, "yanlıştı" demek
"hiç değerli değildi" demek değil):

```
| I2  | Fable+Opus  | AMD path capture_next() cross-API sync yok ... **[KEŞİF 07.07: YANLIŞ KONUMLANMIŞ — encode yolu referans HW'de her zaman CPU-fallback, senaryo oluşmuyor, bkz. SESSION_NOTES]** | ... | Kritik | Sprint 1 |
| I3  | Fable+Opus  | Keyed-mutex/QFOT protokolu tutarsiz ... **[KEŞİF 07.07: KISMEN GEÇERLİ, konum capture_dxgi.cpp'ye taşındı (preview yolu), GpuResourceManager'da değil]** | ... | Kritik | Sprint 1 |
| I28 | Opus        | ... oldLayout=UNDEFINED ... **[KEŞİF 07.07: KASITLI/DOKÜMANTE TASARIM (D2/E4 yorumu) — defekt olduğu şüpheli, validation-layer ile doğrulanmalı]** | ... | Kritik | Sprint 1 |
| I29 | Opus+Fable  | Keyed mutex yanlis/eslesmeyen memory ... **[KEŞİF 07.07: ÇÜRÜTÜLDÜ — tek import 3 slota alias, uyuşmazlık yok. Komşuda gerçek bug bulundu → I32]** | ... | Kritik | Sprint 1 |
| I30 | MiniMax     | Cross-adapter shared texture'da KEYEDMUTEX flag'i YOK ... **[KEŞİF 07.07: ÖLÜ KODU HEDEFLİYOR — flag eklemek encode yolunun E_INVALIDARG kök nedenini çözmez, keyed_mutex_* üyeleri zaten %100 ölü]** | ... | Kritik | Sprint 1 |
| I31 | Opus+GLM    | BGRA/RGBA format tutarsizligi ... **[KEŞİF 07.07: HARİTALANDI, DEFEKT YOK — preview yolunda tek swizzle noktası (shader .bgra), zincir tutarlı]** | ... | Yuksek | Sprint 1 |
```

## 2. Yeni satır ekle: I32

Öncelik matrisine, I31'den sonra:
```
| I32 | Keşif (07.07) | invalidate_pool() aynı VkImage/VkDeviceMemory'yi 3 slotta ayrı ayrı free ediyor (tek import, 3-slot alias) — cözünürlük/reinit'te üçlü-free/UB | external_memory_bridge.zig:276-285 | Kritik | Sprint 1 |
```

## 3. Yeni detaylı bölüm ekle — I31'in detay bölümünden sonra

```markdown
### I32 — invalidate_pool() Üçlü-Free (I29 Keşfinin Yan Bulgusu)

**Kaynak:** V8 I2/I3/I28-I31 keşfi (Alt-Adım A, 07.07.2026) — I29'u araştırırken
bulundu, V8'in orijinal 26 maddesinde yoktu.

**Sorun:** `external_memory_bridge.zig`'deki pooled image sistemi, 3 slotun
hepsini **aynı** fiziksel `VkImage`/`VkDeviceMemory`'ye alias ediyor (tek NT-handle
import, 3 slota kopyalanıyor — bu kısım kasıtlı ve I29'un çürütülmesinin
sebebi). Ama `invalidate_pool()` (satır 276-285), her slotu BAĞIMSIZ sanıp
üçünde de ayrı ayrı `vkDestroyImage`+`vkFreeMemory` çağırıyor:
```zig
for (&state.image_pool) |*slot| {
    if (slot.image != null) {
        vk.vkDestroyImage(state.device, slot.image, null);  // 3 kez, AYNI handle
        slot.image = null;
    }
    if (slot.memory != null) {
        vk.vkFreeMemory(state.device, slot.memory, null);   // 3 kez, AYNI handle
        slot.memory = null;
    }
}
```
Slotlar aynı handle'ı paylaştığı için, texture pointer değiştiğinde (çözünürlük
değişimi/reinit) **aynı VkImage/VkDeviceMemory 3 kez free ediliyor** — undefined
behavior, potansiyel heap corruption/crash. İlk çağrı güvenli (hepsi dolu,
null değil), ama sonraki free'ler zaten geçersiz handle üzerinde çalışıyor.
Dedup guard yok.

**Cozum:** Free işlemini slot bazında değil, **fiziksel kaynak bazında** yap —
3 slotun aynı handle'ı paylaştığını bilen kodda, `vkDestroyImage`/`vkFreeMemory`
sadece BİR KEZ çağrılmalı:
```zig
fn invalidate_pool() void {
    // ... (GL memory objects, GPU idle wait aynı kalır) ...

    // Tek fiziksel kaynak, 3 slot alias — BİR KEZ free et
    if (state.image_pool[0].image != null) {
        vk.vkDestroyImage(state.device, state.image_pool[0].image, null);
        vk.vkFreeMemory(state.device, state.image_pool[0].memory, null);
    }
    for (&state.image_pool) |*slot| {
        slot.image = null;
        slot.memory = null;
    }
    // ... (D3D11 NT handle temizliği aynı kalır) ...
}
```

**Test:** Çözünürlük değişimi (veya `invalidate_pool()`'u manuel iki kez
tetikleyen bir birim testi) sonrası crash/AV olmadığını doğrula. Vulkan
validation layer açıkken çalıştırmak, double-free'yi net bir VUID ile
yakalayabilir (`VUID-vkDestroyImage-image-parameter` benzeri).

---
```

## 4. Uygulama Notları'nı güncelle

Mevcut "I2 + I3 + I28 + I29 + I30 + I31 hepsi aynı bölgeyi kapsıyor, TEK
oturumda ele alınmalı" paragrafını şununla DEĞİŞTİR:

```
- **07.07.2026 keşfi bu grupla ilgili temel varsayımı çürüttü** — I2/I3/I28-I31
  TEK bir bölge değil, İKİ bağımsız yol (encode: GpuResourceManager, ölü/CPU-fallback;
  preview: capture_dxgi→Vulkan→GL, canlı). Ayrı commit'ler önerilir, tek paket YOK.
  Önerilen gerçek sıra (SESSION_NOTES'taki keşif raporundan):
  1. **I32** (üçlü-free) — izole, düşük risk, en yüksek gerçek-bug önceliği. İLK YAPILACAK.
  2. **I30 → ölü kod temizliği** — `keyed_mutex_display_/encode_/copy_fence_` üyelerini
     `GpuResourceManager`'dan kaldır (kullanılmıyorlar, kafa karıştırıyorlar).
  3. **I28** — muhtemelen kod değişikliği yok, validation-layer ile "gerçekten
     sorun mu" doğrulaması + kasıtlı tasarımın kod yorumunda daha net belgelenmesi.
  4. **I2/I3** — preview yolunun `use_keyed_mutex_=false` fallback senkronizasyon
     doğruluğu, ayrı ve dikkatli bir inceleme gerektiriyor (henüz derinlemesine
     incelenmedi, sadece konumu doğru tespit edildi).
  5. **I29, I31** — kapandı, kod değişikliği gerekmiyor (I29 → I32'ye devretti,
     I31 → defekt yok).
```

## Doğrulama Checklist

- [ ] I2/I3/I28/I29/I30/I31 satırları keşif notuyla güncellendi (orijinal metin korundu)
- [ ] I32 öncelik matrisine ve detaylı bölüme eklendi
- [ ] Uygulama Notları güncellendi
- [ ] Commit: `docs(plan): V8'i I2/I3/I28-I31 keşif bulgularıyla güncelle, I32 ekle (üçlü-free)`
- [ ] Push yapma — onay bekle
