# Healing-Log Şema Referansı (Özellik#3)

Bu belge, healing kararlarının kalıcı SQLite log'unun (`healing_log` tablosu)
şemasını tanımlar. Kapsam **yalnız yazma**dır; sorgu/kalibrasyon mantığı
(ROADMAP madde 5 — "Kalibre edilmiş temel çizgi") ayrı bir özelliktir ve bu
şemayı **tüketici** olarak kullanacaktır. Bu belge o özellik için hazır bir
başlangıç noktasıdır.

## Dosya konumu

`%LOCALAPPDATA%\reji-studio\healing_log.sqlite` (`paths::db_path`). Log
dosyalarıyla aynı kök — yeni bir dizin mekanizması icat edilmedi (I21 emsali).
WAL modu açık olduğundan yanında `-wal` / `-shm` yardımcı dosyaları oluşur.

## Tablo: `healing_log`

Append-only. Her **durum-geçişi** (bir aksiyonun applied/pending olması, sonra
rejected/invalidated olması) ayrı bir satırdır; `action_id` bunları ilişkilendirir.
`UPDATE` yoktur (KISS + basit toplama).

| Sütun           | Tip     | Açıklama |
|-----------------|---------|----------|
| `id`            | INTEGER PRIMARY KEY | rowid, monoton. |
| `ts_unix_us`    | INTEGER NOT NULL | Yazma anı, UNIX epoch mikrosaniye. **İndeksli** (`idx_healing_ts`) — madde 5'in zaman-aralığı sorguları için. i64 olarak saklanır (u64→i64 cast; ~292471 yıla dek güvenli). |
| `action_id`     | INTEGER NOT NULL | FFI aksiyon ID'si (`next_action_id`). Aynı aksiyonun durum-geçişlerini ilişkilendiren korelatör. |
| `rule_id`       | TEXT NOT NULL DEFAULT '' | Aksiyonu üreten kural (`rules.json`). |
| `metric_id`     | INTEGER NOT NULL | `rules::metric_id` kodu (aşağıda). `8` = None (açıklanamayan / o an açıklama yok). |
| `current_value` | INTEGER NOT NULL | Tetikleyen metriğin o anki değeri (açıklama yoksa 0). |
| `threshold`     | INTEGER NOT NULL | Kuralın eşiği (açıklama yoksa 0). |
| `action_type`   | INTEGER NOT NULL | `RjActionType` kodu (aşağıda). |
| `outcome`       | INTEGER NOT NULL | Sonuç kodu (aşağıda). |
| `mode`          | INTEGER NOT NULL | Healing modu (aşağıda). |

### `outcome` kodları

| Kod | Anlam | Üretim noktası |
|-----|-------|----------------|
| 0 | `applied` | Otomatik uygulandı (`evaluate_rule_engine`) **veya** onaydan sonra aktüatöre gitti (`rj_action_approve`). |
| 1 | `pending` | CoPilot manuel-kategori, kullanıcı onayı bekliyor (`enqueue_pending`). |
| 2 | `rejected` | Kullanıcı CoPilot'ta reddetti (`rj_action_reject`). |
| 3 | `invalidated` | TTL doldu (`sweep_expired_pending`) veya mod değişti (`clear_pending_on_mode_change`). |

`applied`/`pending` satırları tam üçlüyü (metric/value/threshold) taşır — bu veri
kararın üretim anında hazırdır. `rejected`/`invalidated` anlarında yalnız aksiyon
bilinir → `metric_id = 8` (None), `current_value = threshold = 0`; ilgili üçlü
aynı `action_id`'nin önceki `applied`/`pending` satırından okunabilir.

### `metric_id` kodları (`rules::metric_id`)

| 0 frame_drop_pct · 1 gpu_temp_c · 2 cpu_temp_c · 3 memory_usage_pct · 4 cpu_load_pct · 5 gpu_load_pct · 6 network_rtt_ms · 7 network_loss_pct · 8 None |

### `action_type` kodları (`RjActionType`)

| 0 BitrateReduce · 1 BitrateRecover · 2 ScaleResolution · 3 RestoreResolution · 4 CapFps · 5 RestoreFps · 6 LogOnly |

### `mode` kodları (`HealingMode`)

| 0 AutoPilot · 1 CoPilot · 2 Assist · 3 Manual |

## Retention

Kayıtlar `RETENTION_DAYS` (varsayılan **30 gün**) yaşından eski olduğunda
periyodik `DELETE` ile budanır (`healing_log.rs`, writer thread'i ~60s'de bir).
Zaman-tabanlı sınır seçildi; kayıt-sayısı değil (kullanım yoğunluğundan bağımsız
doğal birim: "son bir ay ne oldu"). Ayarlanabilir sabit.

## Yazma modeli (özet)

Üretim noktaları yalnız lock-free bir kuyruğa non-blocking push yapar
(`healing_log::log_healing`) — hot-path veya C++ frame thread'i asla bloklamaz
(ROADMAP madde 3 / J8 dersi / AGENTS.md bloklayan-sorgu yasağı). Ayrı bir
`std::thread` DB bağlantısını sahiplenir (`rusqlite::Connection` `Sync` değil),
kuyruğu ~250ms'de bir boşaltıp tek transaction'da batch yazar. `PRAGMA
journal_mode=WAL`, `synchronous=NORMAL` (uygulama-crash'e güvenli; yalnız
OS-crash'te son txn riski). Yazma başarısız olursa sesli loglanır, healing akışı
etkilenmez (I10 "yut ama sesli logla").

## Örnek sorgular (madde 5 için başlangıç)

```sql
-- Son 24 saatte metrik başına tetiklenme dağılımı
SELECT metric_id, COUNT(*) AS n, AVG(current_value) AS avg_val
FROM healing_log
WHERE ts_unix_us >= (strftime('%s','now','-1 day') * 1000000)
  AND outcome IN (0, 1)          -- applied/pending
GROUP BY metric_id ORDER BY n DESC;

-- Bir aksiyonun tam yaşam döngüsü (applied → invalidated gibi)
SELECT ts_unix_us, outcome, mode FROM healing_log
WHERE action_id = ?1 ORDER BY ts_unix_us;

-- Reddedilme oranı (kural başına — hangi kural kullanıcıyı rahatsız ediyor)
SELECT rule_id,
       SUM(outcome = 2) AS rejected,
       SUM(outcome IN (0,1)) AS proposed
FROM healing_log GROUP BY rule_id;
```
