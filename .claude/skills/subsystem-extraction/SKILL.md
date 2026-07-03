---
name: subsystem-extraction
description: Reji Studio'da Pipeline::Impl'den (veya başka bir şişmiş sınıftan) yeni alt sistem çıkarma prosedürü — Faz 0'da 9 aşamada oturan desen (CaptureSubsystem, EncodeSubsystem, GpuInteropSubsystem, RecoveryCoordinator, CommandRouter). Yeni modül ekleme, pipeline.cpp'yi küçültme, "X'i ayrı sınıfa taşı", scene composition gibi yeni alt sistem tasarlama veya mevcut alt sistemleri yeniden düzenleme görevlerinin HEPSİNDE bu skill'i kullan. Kullanıcı "refactor", "extract", "alt sistem", "subsystem", "Impl'den çıkar" veya "modülerleştir" dediğinde de tetiklenir.
---

# Subsystem Extraction — Reji Studio deseni

Faz 0'da kanıtlanan yöntem: `Pipeline::Impl` ince bir orkestratöre indirildi,
sorumluluklar bağımsız test edilebilir alt sistemlere taşındı.
Bu desen yeni modüller (scene composition, mixer vb.) için de geçerlidir.

## Mimari sözleşme

1. **Thin orchestrator:** `pipeline.cpp` iş mantığı içermez; alt sistemleri
   init/run/shutdown sırasıyla çağırır ve aralarındaki köprüyü kurar.
2. **Donanım izolasyonu:** Donanım bağımlı kod YALNIZCA
   `pipeline/gpu/external_memory_bridge.*` ve `pipeline/capture/capture_dxgi.*`
   içinde yaşar. `pipeline.cpp` bunları doğrudan include etmez —
   **sadece callback üzerinden** konuşur. Yeni alt sistem bu kuralı bozamaz.
3. **Sıkı düğümler orkestratörde kalır:** İki alt sistemi birden ilgilendiren
   mantık (örn. keyed-mutex yeniden değerlendirmesi hem capture'a hem GPU
   interop'a dokunur) taşınmaz; orkestratörde kalır ve commit mesajında
   "sıkı düğüm korundu" diye açıklanır. Zorla taşımak gizli bağımlılık üretir.
4. **Shutdown sırası kutsaldır:** Mevcut release/cleanup sırası (örn.
   GpuInterop shutdown'ı VulkanInitializer release'inden ÖNCE) bire bir korunur.
5. **FFI sınırına dokunulmaz** — gerekiyorsa önce `ffi-safety-review` skill'i.

## Çıkarma prosedürü (aşama başına bir commit)

**Aşama 0 — Karakterizasyon:** Taşımadan ÖNCE mevcut davranışı kilitle:
`tests/pipeline_characterization_test.cpp`'de ilgili yolu kapsayan test var mı?
Yoksa ekle, `baseline_metrics.txt` ile karşılaştırılabilir çıktı üret.
Yeşil olmadan çıkarmaya başlama.

**Aşama 1 — Sınır çizimi:** Impl'den taşınacak alan/metotların listesini çıkar.
Her biri için karar: taşınır / sıkı düğüm (kalır) / callback'e dönüşür.
Bu listeyi commit mesajının gövdesine yaz (repodaki Stage 7 commit'i şablondur).

**Aşama 2 — İskelet:** `src/pipeline/<ad>_subsystem.cpp` (+ header `include/`
altında, mevcut alt sistemlerin dosya düzenini kopyala). Arayüz asgari:
`init(...)`, iş metotları, `shutdown()`. Ctor'da iş yapma.

**Aşama 3 — Taşıma:** Alanları ve mantığı taşı; Impl'de her çağrı yerini
`alt_sistem_.metot(...)` delegasyonuyla değiştir. Davranış değişikliği YASAK —
bu aşama saf taşımadır ("guard bire bir" ilkesi). İyileştirme fikri çıkarsa
not al, ayrı commit'e bırak.

**Aşama 4 — Build kaydı:** `src/pipeline/CMakeLists.txt`'e yeni .cpp'yi ekle.
Vulkan'a dokunuyorsa `NOT REJI_VULKAN_MOCK` bloğuna (gpu_interop örneği);
dokunmuyorsa mock build'de de derlenmeli — `cmake --preset mock` ile doğrula.

**Aşama 5 — Doğrulama zinciri:**
```
just build                                  # Release build
ctest --test-dir build                      # PipelineIntegration + Characterization
just run                                    # 30s stabilite: crash yok,
                                            # "First frame" log'u var, findstr "HATA" run.log boş
```

**Aşama 6 — Birim testi:** Alt sistemin kendi test dosyası
`tests/test_<ad>.cpp` (test_frame_pacing.cpp desenini izle) + tests/CMakeLists.txt kaydı.

**Aşama 7 — Commit:** Format repodaki Stage commit'leriyle aynı:
```
refactor: extract <AdSubsystem> (<kapsam özeti>) from Pipeline::Impl — Stage N

<neyin taşındığı, sıkı düğümlerin neden kaldığı>
- init(): ... -> alt_sistem_.init(...)
- run_frame(): ...
- shutdown(): ... (sıra korundu)
- CMakeLists: <dosya> <hangi bloğa>

Doğrulama: build --target all OK; ctest <geçen testler>
```

**Aşama 8 — Belgeleme:** CONTEXT.md'de durumu güncelle,
`docs/memory.md`'ye oturum notu düş, CLAUDE.md mimari şemasına
yeni dizin/dosya girdisini ekle.

## Kırmızı çizgiler

- Karakterizasyon testi yeşil değilken taşıma başlatma
- Taşıma + davranış değişikliği aynı commit'te olamaz
- `ffi_bridge.h` / `ffi_auto.h`'a bu iş kapsamında dokunma
- Bir aşamada birden fazla alt sistem çıkarma
- Shutdown/init sırasını "daha mantıklı" diye değiştirme — ayrı görev aç
