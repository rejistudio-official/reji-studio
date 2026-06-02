# optimization.md — Reji Studio Optimizasyon Raporu

> Bu dosya her milestone sonunda güncellenir.
> Komut: `/optimize src/pipeline/ src/ui/`

---

## Profil Özeti

| Metrik | Değer | Hedef | Durum |
|---|---|---|---|
| Frame latency (capture→display) | ? ms | < 16.6ms | ⬜ |
| GPU memory (preview staging) | ? MB | < 50MB | ⬜ |
| CPU kullanımı (run_frame thread) | ? % | < 5% | ⬜ |
| Frame drop oranı | ? /sn | 0 | ⬜ |
| PBO upload süresi | ? µs | < 1ms | ⬜ |

---

## Tespit Edilen Sorunlar

### Yüksek Öncelik
*(Henüz doldurulmadı)*

### Orta Öncelik
*(Henüz doldurulmadı)*

### Düşük Öncelik
*(Henüz doldurulmadı)*

---

## Öneriler

### Hot-path (run_frame)
- [ ] `capture_next()` → `release_frame()` arası süre ölçülmeli
- [ ] PBO upload async doğrulanmalı (GPU fence)
- [ ] `convertToFormat(RGBA8888)` maliyeti profillenmeli

### Bellek
- [ ] Staging texture pooling — her frame yeniden allocate edilmiyor mu?
- [ ] `QImage::copy()` deep copy maliyeti — zero-copy alternatif?

### GPU
- [ ] `DwmFlush()` maliyeti — kaç ms alıyor?
- [ ] PBO→texture DMA async mi, sync mi?

---

## Araçlar

```cmd
# CPU profil — Visual Studio Profiler
devenv reji_app.sln /Run /Profile

# GPU profil — RenderDoc
renderdoc --capture reji_app.exe

# Bellek — Dr. Memory
drmemory -- reji_app.exe
```

---

*Son güncelleme: —*
*Güncelleyen: —*

---

# security.md — Reji Studio Güvenlik Raporu

> Bu dosya her milestone sonunda güncellenir.
> Komut: `/security-review src/`

---

## Güvenlik Tarama Özeti

| Araç | Son Tarih | Sonuç |
|---|---|---|
| cargo audit | 2026-06-01 | ✅ Temiz |
| cppcheck | 2026-06-01 | ✅ Temiz |
| Claude Security Review | 2026-06-01 | 1 bulgu düzeltildi |
| Manual review | — | ⬜ |

---

## Tamamlanan Düzeltmeler

### fix(security): pollMetrics rj_command_drain sınırla
**Commit:** a4b8c48
**Bulgu:** `main_window.cpp::pollMetrics()` içinde `rj_command_drain` return değeri
kontrol edilmiyordu. Negatif veya 8'den büyük değer stack corruption riski.
**Düzeltme:** `n < 0 → 0`, `n > 8 → 8` clamp-and-wrap pattern.
**Severity:** Orta

---

## Açık Bulgular

*(Henüz yok)*

---

## Bilinen Riskler (Kabul Edilmiş)

| Risk | Sebep | Kabul Gerekçesi |
|---|---|---|
| In-process C++ plugin | Çökme tüm pipeline'ı etkiler | OBS/vMix ile aynı tradeoff |
| Stream key bellekte | Şifreleme yok | v1.0 sonrası ele alınacak |
| DLL injection | NVIDIA driver güvene alındı | Sistem sürücüsü |

---

## Kontrol Listesi

### Her PR'da
- [ ] `cargo audit` geçti mi?
- [ ] `cppcheck` uyarı artışı yok mu?
- [ ] FFI sınırında yeni `unsafe` blok var mı?
- [ ] Yeni external input var mı? (ağ, dosya, IPC)

### Her Milestone'da
- [ ] `/security-review src/` çalıştır
- [ ] OWASP Top 10 kontrol et (ağ özellikleri için)
- [ ] Bağımlılık güncellemelerini kontrol et
- [ ] `security.md` güncelle

---

## Güvenlik Mimarisi Özeti

```
Plugin sistemi:
  - C ABI zorunlu (versiyonlar arası kararlılık)
  - Ed25519 imza zorunlu (v1.0)
  - In-process (gecikme bütçesi gereği)
  - Kabul edilmiş risk: çökme tüm pipeline'ı etkiler

FFI sınırı:
  - SEH wrapper zorunlu (__try/__except)
  - canary value her MetricSample'da
  - 2 core developer onayı gerektirir

Ağ:
  - SRT — şifreli transport (v0.3+)
  - WebSocket — lokal ağ, PWA kontrol (v0.4+)
  - HTTPS zorunlu (dış servisler)
```

---

*Son güncelleme: —*
*Güncelleyen: —*
