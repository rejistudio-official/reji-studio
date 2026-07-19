# Talimatlar Arşivi

Bu klasör, **Claude (sohbet asistanı)** tarafından üretilip **Claude Code**'a iletilen
görev talimatlarının tarihsel arşividir. Her dosya, belirli bir iş maddesi için
hazırlanmış tek seferlik bir uygulama/keşif talimatına karşılık gelir.

> **Bunlar "canlı" doküman DEĞİLDİR.** Yalnızca tarihsel referanstır — hangi görevin
> hangi gerekçeyle nasıl verildiğini gösterir. Güncel durum, açık maddeler ve mevcut
> mimari için **`docs/SESSION_NOTES.md`** ile ilgili skill dosyalarına bakın.
> Bir talimattaki dosya/fonksiyon/bayrak adı kod tabanında hâlâ geçerli varsayılmamalı;
> yazıldığı andaki durumu yansıtır.

---

## V8 Serisi

| Dosya | İlgili madde | Durum |
|-------|--------------|-------|
| `V8_I1_TALIMAT.md` | V8 I1 — RuleEngine → HealingMonitor bağlama | FIXED + PUSHED |
| `V8_I2_I3_KESIF_TALIMAT.md` | V8 I2/I3 — senkronizasyon keşfi (Alt-Adım A) | DONE (keşif) |
| `V8_EKLE_I28-31_TALIMAT.md` | V8 I28–I31 — ek keşif talimatı | DONE (keşif) |
| `V8_I28_TALIMAT.md` | V8 I28 — `oldLayout=UNDEFINED` layout geçişi | KAPANDI |
| `V8_I30_TALIMAT.md` | V8 I30 — GpuResourceManager ölü keyed-mutex/fence kodu | FIXED + PUSHED |
| `V8_I32_TALIMAT.md` | V8 I32 — `invalidate_pool` üçlü-free hatası | FIXED + PUSHED |
| `V8_KESIF_GUNCELLEME_I32_TALIMAT.md` | V8 I32 — keşif güncellemesi | DONE (keşif) |
| `V8_NOEXCEPT_TALIMAT.md` | V8 ek madde — `ITransport` arayüzünü `noexcept` ile sağlamlaştırma | ARŞİV |
| `TALIMAT_I9_I10.md` | V8 I9 + I10 — COM yaşam döngüsü ve SEH filtreleri | ARŞİV |
| `TALIMAT_I14.md` | V8 I14 — `rj_metrics_poll` implementasyonu | ARŞİV |
| `TALIMAT_I23.md` | V8 I23 — `execute_copy()` slot senkronu / GPU-interop derinlemesine inceleme | ARŞİV |
| `TALIMAT_I33_I11.md` | V8 I33 + I11 — CoPilot onay kapısı + action-queue mimarisi | FIXED (df1c163..b20608f) |
| `TALIMAT_I8_WS_AUTH.md` | V8 I8 — obs-websocket WS auth (7 commit; legacy `{cmd}` açığı dahil) | FIXED (b00116d..da843fd) |

## Sprint 3-4 Serisi

| Dosya | İlgili madde | Durum |
|-------|--------------|-------|
| `TALIMAT_SPRINT_3_4.md` | Sprint 3-4 — I15-I18 (performans/mimari) + I21-I26 (temizlik) + I34 | ARŞİV |
| `TALIMAT_SPRINT3_GRUPB.md` | Sprint 3-4 Grup B — I21 (hardcoded path) + I24 (sınırsız CStr okuma) | ARŞİV |
| `TALIMAT_SPRINT3_GRUPD.md` | Sprint 3-4 Grup D — I15 (hot-path metrics push) + I18 (WASAPI katman ihlali) | ARŞİV |

## V9 Serisi

| Dosya | İlgili madde | Durum |
|-------|--------------|-------|
| `TALIMAT_V9_SPRINT1.md` | V9 Sprint 1 — J1, J2, J3, J4 | ARŞİV |
| `TALIMAT_V9_SPRINT2.md` | V9 Sprint 2 — J5, J6, J7, J8 | ARŞİV |
| `TALIMAT_V9_SPRINT3.md` | V9 Sprint 3 — J9, J10, J11, J12, J13 | ARŞİV |
| `TALIMAT_V9_SPRINT4_HEALING_PLUMBING.md` | V9 Sprint 4 (J14-J16) + Healing Plumbing (HP1-HP4) | ARŞİV |

## Faz 1 — OBS-WebSocket Uyumluluk Katmanı

| Dosya | İlgili madde | Durum |
|-------|--------------|-------|
| `FAZ1_OBS_WEBSOCKET_DESIGN.md` | Faz 1 tasarım dokümanı — obs-websocket v5 protokol alt kümesi | DONE (tasarım) |
| `FAZ1_CLAUDE_CODE_TALIMAT.md` | Faz 1 — tüm aşamalar için uygulama talimatı (Hello/Identify/Request/Auth/Msgpack) | FIXED + PUSHED |
| `FAZ1_ASAMA1_TALIMAT.md` | Faz 1 Aşama 1 — handshake iskeleti (Hello/Identify/Identified) | FIXED + PUSHED |

## Özellikler

| Dosya | İlgili madde | Durum |
|-------|--------------|-------|
| `TALIMAT_OZELLIK1_COPILOT_ACIKLAMA.md` | Özellik #1 — CoPilot aksiyon açıklaması | ARŞİV |
| `TALIMAT_OZELLIK2_HEALING_WS.md` | Özellik #2 — Healing durumunu obs-websocket VendorEvent olarak açmak | FIXED + PUSHED |
| `TALIMAT_OZELLIK3_SQLITE_HEALING_LOG.md` | Özellik #3 — SQLite healing-log | ARŞİV |
| `TALIMAT_OZELLIK5_KALIBRASYON.md` | Özellik #5 — Kalibre edilmiş temel çizgi (statik eşikler yerine) | ARŞİV |

## Bakım / Araştırma / Diğer

| Dosya | İlgili madde | Durum |
|-------|--------------|-------|
| `TALIMAT_FRAME_DROP_PLUMBING.md` | `frame_drop_pct` plumbing düzeltmesi (kural motorundaki en kritik metrik) | ARŞİV |
| `TALIMAT_KURALLARI_DUZENLE.md` | "Kuralları Düzenle..." stub + otomatik reload bağlama | ARŞİV |
| `TALIMAT_KURAL_SETI_PAYLASIM.md` | Paylaşılabilir kural setleri — dışa/içe aktar (ROADMAP Sütun 3) | FIXED + PUSHED |
| `TALIMAT_BENCHMARK_KARSILASTIRMA.md` | Reji vs OBS karşılaştırmalı benchmark aracı (ROADMAP Sütun 2) | FIXED + PUSHED |
| `TALIMAT_VULKAN_GL_INTEROP_DERIN_TUR.md` | Vulkan/GL interop derin turu — I23 devamı, v2 | ARŞİV |
| `TALIMAT_GUI_GOZLEM_ARASTIRMA.md` | GUI gözlem turu — canlı app incelemesi (keşif) | DONE (keşif) |
| `TALIMAT_AYARLAR_ARASTIRMA.md` | Ayarlar penceresi kapsamlı araştırması (keşif) | DONE (keşif) |
| `TALIMAT_ARSIV_VE_MAKRO_MOTORU.md` | Eski planlama dosyalarının arşivlenmesi + makro motoru roadmap notu | DONE |
| `TALIMAT_AYARLAR_KATEGORI_DUZENI.md` | Ayarlar UX Madde 6/C — Settings dialog'u QTabWidget ile 5 sekmeye kategorize etme | FIXED + PUSHED (GUI onayı ayrı bekliyor) |

---

*Son güncelleme: 2026-07-19 — 42 dosya (README dahil)*
