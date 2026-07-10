# Talimatlar Arşivi

Bu klasör, **Claude (sohbet asistanı)** tarafından üretilip **Claude Code**'a iletilen
görev talimatlarının tarihsel arşividir. Her dosya, belirli bir V8 iş maddesi için
hazırlanmış tek seferlik bir uygulama/keşif talimatına karşılık gelir.

> **Bunlar "canlı" doküman DEĞİLDİR.** Yalnızca tarihsel referanstır — hangi görevin
> hangi gerekçeyle nasıl verildiğini gösterir. Güncel durum, açık maddeler ve mevcut
> mimari için **`docs/SESSION_NOTES.md`** ile ilgili **skill dosyalarına** bakın.
> Bir talimattaki dosya/fonksiyon/bayrak adı kod tabanında hâlâ geçerli varsayılmamalı;
> yazıldığı andaki durumu yansıtır.

## İçerik

| Dosya | İlgili madde | Durum |
|-------|--------------|-------|
| `V8_I1_TALIMAT.md` | V8 I1 — RuleEngine → HealingMonitor bağlama | FIXED + PUSHED |
| `V8_I2_I3_KESIF_TALIMAT.md` | V8 I2/I3 — senkronizasyon keşfi (Alt-Adım A) | DONE (keşif) |
| `V8_EKLE_I28-31_TALIMAT.md` | V8 I28–I31 — ek keşif talimatı | DONE (keşif) |
| `V8_I28_TALIMAT.md` | V8 I28 — `oldLayout=UNDEFINED` layout geçişi | KAPANDI |
| `V8_I30_TALIMAT.md` | V8 I30 — GpuResourceManager ölü keyed-mutex/fence kodu | FIXED + PUSHED |
| `V8_I32_TALIMAT.md` | V8 I32 — `invalidate_pool` üçlü-free hatası | FIXED + PUSHED |
| `V8_KESIF_GUNCELLEME_I32_TALIMAT.md` | V8 I32 — keşif güncellemesi | DONE (keşif) |
| `V8_NOEXCEPT_TALIMAT.md` | V8 ek madde — `ITransport` arayüzünü `noexcept` ile sağlamlaştırma | Talimat (output/ katmanı) |

## Not

Bu dosyalar depoya daha önce hiç commit edilmemiş (untracked) olarak duruyordu ve
tek bir arşivleme commit'iyle `docs/talimatlar/` altına toplanmıştır.
