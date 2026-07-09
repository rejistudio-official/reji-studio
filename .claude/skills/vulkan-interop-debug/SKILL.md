---
name: vulkan-interop-debug
description: Reji Studio'da Vulkan / D3D11 interop, GPU ve görüntü sorunlarını ayıklama prosedürü. Device-lost, TDR, VK_ERROR_DEVICE_LOST, siyah ekran, tearing, donmuş preview, yanlış renk (BGRA/RGBA), validation layer hatası, VUID ihlali, keyed mutex, timeline semaphore, external memory, copy_optimizer, RecoveryCoordinator veya healing davranışıyla ilgili HER hata ayıklama görevinde bu skill'i kullan. Kullanıcı "GPU", "Vulkan", "interop", "TDR", "device lost", "siyah ekran", "tearing" veya "preview bozuk" dediğinde de tetiklenir.
---

# Vulkan Interop Debug — Reji Studio

Çift GPU referans donanımı: **AMD Radeon 780M (iGPU, display) + NVIDIA RTX 4070
Laptop (dGPU, encode)**. Bu topolojinin tuhaflıkları hata analizinin başlangıç
noktasıdır — genel Vulkan bilgisiyle değil, buradaki gerçeklerle başla.

## Donanım topolojisi gerçekleri (docs/memory.md'den)

- Ekran **AMD iGPU'ya** bağlı; NVENC **NVIDIA dGPU'da**.
- DXGI Desktop Duplication **yalnızca AMD adapter'ında** çalışır;
  NVIDIA'da `E_ACCESSDENIED` döner. Bu bir hata değil, platform gerçeğidir.
- `display_vendor_id()` → `gpu_scan_.entries[0]` = display (AMD);
  `entries[1]+` = encode (NVIDIA).
- Cross-adapter transfer (`GpuResourceManager` SharedHandle AMD→NVIDIA) GERÇEKTEN
  AKTİF — `same_adapter_` gerçek LUID karşılaştırmasıyla belirleniyor
  (`gpu_resource_manager.cpp:222`), hardcode değil. Referans donanımda
  (AMD display + NVIDIA encode) `same_adapter_=false` çıkar, cross-adapter
  yolu (`create_cross_adapter_shared`) devrede. Bu not 2026-07 öncesi
  bayattı, I2/I3 keşfi sırasında düzeltildi.
- Render path: NVIDIA (0x10DE) → `kNvDxInterop` stub (fiilen PBO çalışır);
  diğer → `kPbo`. `selectRenderPath()` init sonrası bir kez, GL thread'de.

## Semptom → ilk şüpheli haritası

| Semptom | İlk bakılacak yer |
|---|---|
| Tearing / yarım frame | Keyed mutex hangi kaynağı koruyor? (V7-H2: yanlış VkDeviceMemory) + acquire/release eşleşmesi |
| Deadlock frame'de | Timeline semaphore wait değeri vs signal değeri; başarısız submit sonrası bekleme (V7-H17 sınıfı) |
| Siyah ekran (preview) | GL texture filter/completeness (V7-H19) → sonra external memory import başarısı |
| Yanlış renk | BGRA/RGBA swap (V7-H7 sınıfı) — format zincirini DXGI→Vulkan→GL uçtan uca yaz |
| Rastgele crash / VK_ERROR_DEVICE_LOST | Validation layer AÇ (aşağıda), VUID topla; command buffer yaşam döngüsü (reset-while-pending, V7-H1 sınıfı) |
| Capture hiç başlamıyor | Duplication hangi adapter'da deneniyor? AMD dışıysa E_ACCESSDENIED normaldir |
| İyileşme döngüsü (sürekli recreate) | RecoveryCoordinator log'ları + healing.rs hysteresis (5s) — kural fırtınası mı, gerçek device-lost mu ayır |

## Standart ayıklama prosedürü

1. **Reprodüksiyon + log:** `just run` → `run.log`. `findstr /i "HATA DEVICE_LOST recovery"` ile ilk hata anını bul. İlk hatadan SONRAKİ hatalar genelde kaskaddır — köke odaklan.
2. **Validation layer'ı aç** (debug build):
   `set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` (veya vkconfig ile).
   Çıkan **VUID kodunu** aynen not et — düzeltme commit'inde referans ver
   (ev stili: V7-H1'deki gibi `VUID-vkResetCommandBuffer-commandBuffer-00045`).
3. **Sınıflandır:** senkronizasyon mu (semaphore/mutex/barrier), yaşam döngüsü mü
   (reset/destroy sırası), format mı, topoloji mi (yanlış adapter)?
4. **Tek değişkenli deney:** mock preset'te de oluyor mu?
   (`cmake --preset mock`) Oluyorsa Vulkan'a değil mantığa bak — çok zaman kazandırır.
5. **Frame içi görsel analiz gerekiyorsa RenderDoc** ile capture al;
   keyed mutex/semaphore sorunlarında Nsight Graphics daha iyi görür.
6. **Düzeltme kuralı:** Senkronizasyon düzeltmeleri davranış değiştirir —
   önce karakterizasyon/timing testi (`tests/test_gpu_query_timing.cpp`,
   `test_frame_pacing.cpp`) yeşilken başla, düzeltme sonrası tekrar koş.

## Proje ilkeleri (bulgu geçmişinden damıtılmış — ihlal etme)

- **Per-slot kaynak ilkesi:** Pending olabilecek her şey (command buffer,
  semaphore, staging) slot başına ayrılır; tek kaynağı `SIMULTANEOUS_USE`
  ile paylaşmak yasak (H1/H11/H17 dersi).
- **Cross-thread erişilen her bayrak atomic** olmalı (H4/H18 dersi);
  "sadece okuyorum" savunması geçersiz.
- **Windows HANDLE'lar RAII/CloseHandle ile kapanır** (H5 dersi).
- **Shutdown sırası:** GpuInterop shutdown → VulkanInitializer release.
  Statik yıkım sırasına güvenme (H20 dersi).
- Keyed mutex D3D11 **texture**'ı korur; Vulkan tarafında beklenen şeyin
  aynı fiziksel kaynak olduğunu import zincirinden doğrula.

## RecoveryCoordinator / self-healing etkileşimi

Device-lost analizi yaparken healing motorunu hesaba kat:
- `recovery_coordinator.cpp` pipeline tarafı yeniden kurulumu yönetir;
  `healing.rs` kural bazlı aksiyonları (hysteresis_ms=5s, öncelik:
  BitrateReduce > CapFps > ScaleResolution > Recover > LogOnly).
- Semptom "kendini tekrar eden recreate" ise: log'da recovery tetikleyen
  metriği bul — sahte metrik (NaN fps, H8 sınıfı) healing'i yanlış tetikleyebilir.
- Debug sırasında healing'i susturmak istersen kural dosyasını
  (`~/.reji/rules.json`) log_only'ye çevir; kodda devre dışı bırakma.

## Çıktı formatı

Analiz sonucu şunları içermeli: kök neden (VUID/log kanıtıyla), sınıfı
(sync/lifecycle/format/topoloji), önerilen düzeltme, hangi ilkeye bağlandığı,
ve doğrulama planı (hangi test + validation layer temiz mi).
Yeni bir hata SINIFI bulunursa bu skill'in ilkeler bölümüne ekle.
