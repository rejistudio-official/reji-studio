# Claude Code Talimatı — V8/I32: invalidate_pool() Üçlü-Free Düzeltmesi

`.claude/skills/vulkan-interop-debug/SKILL.md` yüklü — "Per-slot kaynak ilkesi"
burada TERSİNE işliyor: bu üç slot per-slot DEĞİL, tek fiziksel kaynağın
alias'ı — kod bunu unutmuş.

**Kaynak:** `docs/FABLE5_BUG_PLAN_V8.md`, madde I32 (09.07.2026 keşfi, I29
araştırılırken bulundu). En yüksek gerçek-bug önceliği — memory corruption sınıfı.

## Sorun (SESSION_NOTES'ta zaten tam doğrulanmış)

`external_memory_bridge.zig`'deki 3-slotlu image pool, **tek bir fiziksel**
`VkImage`/`VkDeviceMemory`'yi (tek NT-handle import) 3 slota da aynı değerle
kopyalıyor (kasıtlı tasarım — I29'un "keyed mutex yanlış nesneyi koruyor"
şüphesini bu yüzden çürütmüştük, tek kaynak olduğu için uyuşmazlık yok).

Ama `invalidate_pool()` (satır ~276-285) bunu bilmiyor, her slotu bağımsız
sanıp üçünde de ayrı ayrı free çağırıyor:
```zig
for (&state.image_pool) |*slot| {
    if (slot.image != null) {
        vk.vkDestroyImage(state.device, slot.image, null);  // AYNI handle, 3 kez
        slot.image = null;
    }
    if (slot.memory != null) {
        vk.vkFreeMemory(state.device, slot.memory, null);   // AYNI handle, 3 kez
        slot.memory = null;
    }
}
```
Çözünürlük değişimi/reinit sırasında tetiklenir — üçlü-free, undefined behavior,
potansiyel heap corruption/crash.

## Yapılacaklar

### 1. Düzeltme

```zig
fn invalidate_pool() void {
    // B16: GL memory object'leri NT handle'lar kapanmadan önce sil
    if (gl_delete_memory_objects) |pfn| {
        pfn(@intCast(POOL_SIZE), &gl_memory_objects);
        gl_memory_objects = .{0} ** POOL_SIZE;
        gl_delete_memory_objects = null;
    }
    // GPU idle bekle — F4 dersi
    if (state.device != null) {
        _ = vk.vkDeviceWaitIdle(state.device);
    }

    // I32 duzeltmesi: 3 slot AYNI fiziksel VkImage/VkDeviceMemory'yi alias
    // ediyor (tek NT-handle import) — free işlemi BİR KEZ yapılmalı, slot
    // basina degil. Once image, sonra memory (E14 dersi, sira korunuyor).
    if (state.image_pool[0].image != null) {
        vk.vkDestroyImage(state.device, state.image_pool[0].image, null);
    }
    if (state.image_pool[0].memory != null) {
        vk.vkFreeMemory(state.device, state.image_pool[0].memory, null);
    }
    for (&state.image_pool) |*slot| {
        slot.image = null;
        slot.memory = null;
    }

    // D3D11 NT handle'larını kapat (bu kısım DEĞİŞMEDİ — her NT handle
    // gerçekten ayrı mı, yoksa bu da alias mı? KONTROL ET, aşağıya bak)
    for (&state.d3d11_nt_handles) |*h| {
        if (h.*) |handle| {
            _ = std.os.windows.CloseHandle(handle);
            h.* = null;
        }
    }
    state.cached_texture_ptr = null;
}
```

### 2. ÖNEMLİ — D3D11 NT handle'ları da alias mı kontrol et

Talimat yazarken SADECE `VkImage`/`VkDeviceMemory`'nin alias olduğu
doğrulanmıştı (SESSION_NOTES). `state.d3d11_nt_handles` dizisinin de aynı
şekilde tek bir handle'ın 3 kopyası mı, yoksa gerçekten 3 ayrı NT handle mı
(örn. her slot ayrı `DuplicateHandle` ile açılmış olabilir, Windows handle'ları
VkImage gibi "aynı değer" mantığıyla çalışmaz, her `DuplicateHandle` çağrısı
gerçekten ayrı bir kernel referansı üretir) — **kod yazmadan önce bunu izle**.
- Eğer 3 handle da gerçekten aynı değerse (aynı `HANDLE` üç kez saklanmış) →
  o da tek `CloseHandle` olmalı, yukarıdaki gibi düzelt.
- Eğer her biri ayrı `DuplicateHandle` çağrısıyla açılmışsa (farklı değerler,
  aynı kernel nesnesini işaret etseler bile) → mevcut for-loop DOĞRU, her
  birinin ayrı `CloseHandle` edilmesi gerekir, DOKUNMA.
SESSION_NOTES'a hangi durumun geçerli olduğunu ve nasıl doğrulandığını yaz.

### 3. Test

- Çözünürlük değişimini tetikleyen bir senaryo (veya `invalidate_pool()`'u
  art arda iki kez çağıran bir birim testi) ile crash/AV olmadığını doğrula.
- Vulkan validation layer açıkken çalıştır — önceki (bozuk) davranışta
  `VUID-vkDestroyImage-image-parameter` veya benzeri bir double-free VUID'i
  tetiklenip tetiklenmediğini (varsa) not al; düzeltme sonrası bu VUID
  kaybolmalı.
- `ctest --test-dir build` — bilinen 2 hariç yeni kırılma yok.

## Doğrulama Checklist

- [ ] `VkImage`/`VkDeviceMemory` free'si tek seferlik yapıldı
- [ ] D3D11 NT handle'larının alias olup olmadığı ARAŞTIRILDI, sonuca göre
      ya aynı şekilde tekilleştirildi ya da mevcut per-handle döngü korundu —
      hangisi olduğu ve NEDEN, SESSION_NOTES'a yazıldı
- [ ] Çözünürlük değişimi/çift-çağrı senaryosu test edildi, crash yok
- [ ] Validation layer karşılaştırması yapıldı (mümkünse)
- [ ] `ctest --test-dir build` — bilinen 2 hariç yeni kırılma yok
- [ ] `docs/FABLE5_BUG_PLAN_V8.md`'de I32 satırına `[DÜZELTILDI - commit <hash>]`
      notu (I5'teki iki-commit desenini kullan: önce fix, sonra hash'i plana yazan takip commit'i)
- [ ] `docs/SESSION_NOTES.md`'ye özet
- [ ] Commit: `fix(gpu): V8/I32 — invalidate_pool() üçlü-free düzeltmesi (tek fiziksel kaynak, tek free)`
- [ ] Push yapma — özet raporla, onay bekle

## Sınır

Bu talimat SADECE `invalidate_pool()`'daki free mantığını düzeltiyor. I30
(ölü keyed_mutex üyelerinin temizliği), I28 (validation doğrulaması), I2/I3
(preview yolu fallback incelemesi) kapsamın dışında — ayrı talimatlar olarak
gelecek.
