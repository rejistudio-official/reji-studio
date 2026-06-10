\## Reji Studio — Kritik Geliştirme Kuralları



\### FFI Kuralları

\- ffi\_bridge.c stub'ları: Rust implementasyonu geldikten sonra KALDIR

\- Her #\[no\_mangle] fonksiyon tek kaynak olmalı (lib.rs VEYA ffi.rs, ikisi değil)

\- FFI değişikliklerinde cbindgen çalıştır

\- Her FFI dönüş değeri clamp edilmeli



\### Vulkan Kuralları

\- External memory kullanımında glWaitSemaphoreEXT ZORUNLU

\- Her image layout geçişinde oldLayout doğru set edilmeli

\- Validation layer her zaman açık (DEBUG ve RELEASE)

\- Keyed mutex: AcquireSync/ReleaseSync çifti her zaman tamamlanmalı

\- Shutdown sırası: Bridge → VulkanInitializer (bu sırayla)



\### Thread Kuralları

\- wait() dönüş değeri her zaman kontrol edilmeli

\- wait() timeout olursa terminate() + ek wait() yapılmalı

\- Shutdown path için ayrı test senaryosu yazılmalı

\- deleteLater() blocking loop içinde kullanılmamalı



\### Rust Kuralları

\- unwrap() yasak — unwrap\_or\_else() veya ? operatörü kullan

\- tokio::select! dallarında Err(Lagged) ve Err(Closed) her zaman ele alınmalı

\- cargo test + cargo clippy her commit öncesi zorunlu

\- Mutex poison: unwrap\_or\_else(|p| p.into\_inner()) kullan

\- crossbeam ArrayQueue::pop() Option döner, Result değil



\### Hot-Path Kuralları

\- Frame başına heap allocation yasak (push\_back, new, malloc)

\- \_\_try içinde fonksiyon çağrısı yasak — log'u dışarı taşı

\- Mutex lock scope minimal tutulmalı

\- Ring buffer: sabit boyutlu array tercih et, vector değil



\### Metrik Kuralları

\- Ring buffer'a delta değer push et, kümülatif değil

\- Frame drop penceresi: (current - prev) hesabı zorunlu



\### Model Seçim Zorunluluğu

\- Vulkan/GL interop değişikliği → claude-opus-4-8 ile review

\- FFI/ABI değişikliği → claude-opus-4-8 ile review

\- Rust async pattern → claude-opus-4-8 ile review

\- Diğer değişiklikler → claude-sonnet-4-6 yeterli

