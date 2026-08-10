# docs/ İndeksi

Hangi soruya hangi belgenin cevap verdiği ve her belgenin **statüsü**
(bağlayıcı mı, durum kaydı mı, tarihsel mi). Yeni oturumda (veya
`/clear` sonrası) önce buraya bak.

## Soru → Dosya

| Soru | Dosya |
|---|---|
| Şu an ne durumdayız? | `CONTEXT.md` (bu klasördeki — aşağıdaki nota bak) |
| Hangi işler planlı? | `ROADMAP.md` |
| Falanca tur ne yaptı? | `SESSION_NOTES.md` |
| FFI sözleşmesi ne? | `FFI_CONTRACT.md` |
| Benchmark sonuçları? | `BENCHMARK_RESULTS.md` |
| V10'da hangi bug'lar vardı? | `FABLE5_BUG_PLAN_V10.md` (V1–V9 aynı desende) + `V10_SENTEZ_TRIYAJ.md` |
| Falanca özellik nasıl yapıldı? | `talimatlar/` (60 dosya; kendi `README.md`'si var) |
| Wiring kararları neydi? | `FAZ0_RAPOR_WIRING.md`, `ON_KONTROL_WIRING_BAYATLIK.md` |
| Vulkan tarafı nasıl çalışır? | `VULKAN_DEV_GUIDE.md`, `vulkan-sync-diagram.md` |
| Ajan/model tarama çıktıları? | `reviews/` (⚠ statü notuna bak — iddia, kanıt değil) |
| Healing log şeması? | `HEALING_LOG_SCHEMA.md` |
| Geliştirme ritmi/tarama düzeni? | `GELISTIRME_RITMI.md` |
| Eski/geçersiz kayıtlar | `archive/` (memory.md, progress.md — ⚠ ARŞİV uyarılı) |

## Belge Statüleri

Aynı klasörde duran belgeler aynı ağırlıkta değildir:

| Statü | Belgeler | Anlamı |
|---|---|---|
| **Bağlayıcı** (karar/sözleşme) | `../CLAUDE.md`, `FFI_CONTRACT.md`, `ROADMAP.md`'nin faz tanımları | Kurallardır; ihlal edilmez, değişiklik onay gerektirir. |
| **Durum kaydı** (güncel gerçek) | `CONTEXT.md`, `SESSION_NOTES.md`, `BENCHMARK_RESULTS.md` | Mevcut durumu yansıtır; her mühürlemede güncellenir. |
| **Tarihsel** (tamamlanmış, referans) | `talimatlar/` (60 dosya), `FABLE5_BUG_PLAN_V8/V9/V10.md` | Bitmiş işlerin kaydı; "nasıl yapılmıştı"ya bakılır, güncel durum buradan okunmaz. |
| **Bağlayıcı DEĞİL** (tarama çıktısı/arka plan) | `reviews/` | Model taramalarının ham çıktıları — aşağıdaki uyarıya bak. |
| **Geçersiz** | `archive/` | Proje başında (2026-05/06) yazıldı, güncel durumla çelişir; dosyaların başında ⚠ uyarı var. |

### ⚠ `reviews/` uyarısı: iddia ≠ kanıt

`reviews/` altındaki dosyalar çok-model taramalarının **ham
çıktılarıdır**. Bunlar bulgu değil **iddiadır**; her biri Faz 0
doğrulamasından (kod üzerinde teyit) geçmeden doğru sayılmaz. Model
raporları düzenli olarak yanılıyor: V9'da J10/J11, V10'da L7/L9
çürütüldü (L9 kısmi çürütme: commit `57d285c`). Bir oturum `reviews/`
altındaki bir dosyayı okuyup "bu doğrulanmış bir bug" sanmamalı —
triyaj süreci `V10_SENTEZ_TRIYAJ.md`'de örneklenmiştir.

### İki CONTEXT.md ayrımı

| Dosya | Rol | Statü |
|---|---|---|
| `docs/CONTEXT.md` (bu klasör) | **Birincil proje durum kaydı.** Mühürlemede güncellenir; son mühür V10 NİHAİ (`4f55118`, 2026-07-30) kökteki dosyadan daha sonradır. | Durum kaydı — güncel |
| `../CONTEXT.md` (depo kökü) | Sohbet asistanının GitHub'dan doğrulanmış **dış özeti**; ayrı bir bakım hattı. | İkincil — proje durumu sorusunun cevabı bu değil |

Çelişki hâlinde `docs/CONTEXT.md` esas alınır.
