# Claude Code Talimatı — V8/I6, I7, I12, I13: Doğrulama + Gerekli Düzeltmeler

`.claude/skills/vulkan-interop-debug/SKILL.md` yüklü ("Per-slot kaynak ilkesi",
shutdown sırası dersleri ilgili).

**Bu talimat iki aşamalı: önce doğrulama (I7 için muhtemelen zaten kapalı),
sonra gerekli olduğu netleşen kısımlar için gerçek düzeltme (I6/I12/I13).**

## Aşama 1 — I7 Doğrulaması (muhtemelen zaten çözülmüş)

`wasapi_capture.cpp::shutdown()` içinde `"D16: Null owner_ before unregister"`
yorumu var — bu, I7'nin tarif ettiği UAF sorununun **daha önceki bir V-planında
(D-serisi, V4-V7 arası) zaten çözüldüğünü** gösteriyor. Ama koddaki gerçek sıra
(`clear_owner()` ÖNCE, `Unregister` SONRA) V8'in I7'sinin önerdiği çözümün
(`Unregister` ÖNCE, `clear_owner()` SONRA) **tersi**.

1. `docs/FABLE5_BUG_PLAN_V8.md`'de veya daha eski bir V-planında "D16" referansını
   bul — hangi dosyada, hangi tarihte, tam olarak ne çözüldüğü yazıyor mu?
2. Koddaki mevcut sırayı (`clear_owner` → `Unregister`) analiz et: bu, callback'in
   HER erişimde `owner_`'ı atomic kontrol ettiği bir "defansif" strateji mi
   (yani `Unregister`'ın bloklayıcı olup olmadığına bağımlı değil)? `DeviceNotifyClient`
   sınıfının callback metotlarını (`OnDeviceStateChanged` vb.) bul, her birinin
   gerçekten `owner_`'ı null kontrolüyle başladığını doğrula.
3. Eğer bu analiz "D16 zaten yeterli, güvenli" sonucuna varıyorsa:
   `docs/FABLE5_BUG_PLAN_V8.md`'de I7 satırına
   `[DOĞRULANDI 10.07: D16 (önceki V-planı) ile ZATEN ÇÖZÜLMÜŞ — V8 yazılırken
   güncel kod kontrol edilmemiş, yinelenen madde. clear_owner-önce stratejisi
   Unregister-önce stratejisinden FARKLI ama geçerli bir çözüm — callback her
   erişimde owner_ kontrol ediyor, Unregister'ın bloklayıcı olmasına bağımlı
   değil.]` diye işaretle. **Kod değişikliği YAPMA.**
4. Eğer analiz gerçek bir boşluk buluyorsa (örn. callback'in bazı yolları
   `owner_`'ı kontrol etmeden bir şeye erişiyor), o zaman gerçek bir düzeltme
   gerekiyor — bana bulguyu raporla, ben ayrı bir talimat yazarım (bu talimatın
   kapsamına dahil etme, çünkü D16'nın neden yetersiz kaldığını anlamak ayrı
   bir analiz gerektirir).

## Aşama 2 — I6 + I12 + I13 (aynı bölge, birlikte)

### Önce doğrula: SEH zaten yeterli mi?

`is_copy_ready()`'nin SEH (`__try`/`__except`) ile sarılı olduğunu doğruladım —
bu, `device_` geçersiz bir handle'a dönüşürse oluşacak access violation'ı
muhtemelen zaten yakalıyor. Ama şunu netleştir:
- SEH, bir access violation'ı yakalar mı, yoksa `device_`'in "stale ama teknik
  olarak hâlâ eski değeri taşıyan bir pointer" (örn. `Reset()` sonrası `nullptr`
  değil de silinmiş bellek adresi) olması durumunda sessiz UB'ye mi yol açar?
  (`ComPtr::Reset()`'in `device_`'i gerçekten `nullptr` yaptığını, "kullanılmış
  ama serbest bırakılmamış" bir ara duruma düşürmediğini doğrula.)
- Eğer `device_.Reset()` atomic değilse (birden fazla instruction'a bölünüyorsa),
  `is_copy_ready()`'nin başındaki `if (!device_ ...)` kontrolü ile asıl
  `pfn_wait_semaphores_()` çağrısı arasında `device_` gerçekten null'lanabilir mi
  (TOCTOU)? Bu, SEH'in yakalayabileceği bir şey mi yoksa yakalayamayacağı bir
  şey mi (örn. sessizce yanlış ama "geçerli görünen" bir handle'a erişim)?

### Eğer gerçek bir boşluk varsa: düzelt

```cpp
// copy_optimizer.h'ye ekle:
std::atomic<bool> alive_{true};

// shutdown()'ın EN BAŞINA:
void GpuCopyOptimizer::shutdown() {
    alive_.store(false, std::memory_order_release);
    // ... mevcut shutdown mantığı ...
}

// is_copy_ready()'nin EN BAŞINA:
bool GpuCopyOptimizer::is_copy_ready(...) {
    if (!alive_.load(std::memory_order_acquire)) return false;
    // ... mevcut kontroller ve SEH bloğu ...
}
```

### I12 — MainWindow yıkım sırası (henüz hiç dokunulmamış, gerçekten eksik)

`~MainWindow()` şu an:
```cpp
MainWindow::~MainWindow() {
    saveWindowState();
    stopFrameThread();
    if (copy_optimizer_initialized_) {
        copy_optimizer_.shutdown();
    }
}
```
`preview_widget_`'ın `copy_optimizer_`/`bridge_` referanslarını koparma adımı
YOK. `PreviewWidget`'ın gerçekten böyle setter'lara sahip olup olmadığını
kontrol et (`setBridge(nullptr)`, `setCopyOptimizer(nullptr)` gibi — yoksa
uygun isimde ne varsa). Varsa, `stopFrameThread()`'den SONRA,
`copy_optimizer_.shutdown()`'dan ÖNCE ekle:
```cpp
MainWindow::~MainWindow() {
    saveWindowState();
    stopFrameThread();
    if (preview_widget_) {
        preview_widget_->setBridge(nullptr);          // gerçek metot adını doğrula
        preview_widget_->setCopyOptimizer(nullptr);    // gerçek metot adını doğrula
    }
    if (copy_optimizer_initialized_) {
        copy_optimizer_.shutdown();
    }
}
```
Eğer böyle setter'lar yoksa (`PreviewWidget` bu referansları başka şekilde
tutuyorsa), gerçek API'ye göre eşdeğer bir "referans koparma" adımı tasarla,
bana ne bulduğunu raporla.

### I13 — ilk kare sıralaması

`is_copy_ready()`'nin render tarafından, tamamlanmamış bir Vulkan blit'ini
örneklemeyi engelleyecek şekilde gerçekten gate'lendiğini doğrula (V8'deki
öneri: `current_pool_idx_` render için `last_used_slot()`'un TAMAMLANMIŞ
halini göstermeli). Bunu I6/I12 ile aynı PR'da, ayrı bir alt-adım olarak ele al.

## Test

- I7 (eğer sadece doğrulama ise): test gerekmez, sadece analiz + dokümantasyon.
- I6/I12/I13 (eğer kod değişikliği varsa): manuel shutdown testi (`just run` →
  kapat, `run.log`'da crash/AV yok), mümkünse throw/assert deneyi (I27'deki gibi)
  ile `alive_` flag'inin gerçekten koruma sağladığını kanıtla.
- `ctest --test-dir build` — bilinen 2 hariç yeni kırılma yok.

## Doğrulama Checklist

- [ ] I7: D16 referansı bulundu, analiz edildi, sonuç (zaten çözülmüş / gerçek
      boşluk var) netleştirildi
- [ ] I6: SEH'in yeterliliği analiz edildi, gerekirse `alive_` flag'i eklendi
- [ ] I12: `preview_widget_` referans koparma adımı eklendi (gerçek API'ye göre)
- [ ] I13: gate'leme mantığı doğrulandı
- [ ] Manuel/otomatik test yapıldı, sonuç raporlandı
- [ ] `docs/FABLE5_BUG_PLAN_V8.md`'de I6, I7, I12, I13 (I12/I13 V8'de resmi
      madde değildi — I6'nın notu olarak zaten referanslıydı, orada güncelle)
      uygun şekilde işaretlendi
- [ ] `docs/SESSION_NOTES.md`'ye özet
- [ ] Commit(ler) — I7 sadece dokümantasyonsa ayrı, küçük bir commit; I6/I12/I13
      kod değişikliği içeriyorsa ayrı bir commit
- [ ] Push yapma — özet raporla, onay bekle

## Sınır

I2/I3 (kapandı), I28/I30 (kapandı) bu talimatın kapsamı dışında.
