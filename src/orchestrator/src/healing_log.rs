//! Özellik#3: Healing kararlarının kalıcı SQLite log'u.
//!
//! Her healing kararı (applied/pending/rejected/invalidated) ayrı bir yazma
//! thread'inde SQLite'a kaydedilir. Üretim noktaları YALNIZ `log_healing()` ile
//! lock-free bir kuyruğa non-blocking push yapar — hot-path'i veya C++ frame
//! thread'ini asla bloklamaz (ROADMAP madde 3 / J8 dersi / AGENTS.md bloklayan-
//! sorgu yasağı). Ayrı `std::thread` DB bağlantısını sahiplenir (`rusqlite::
//! Connection` `Sync` değil → tek-thread sahipliği en temiz model), kuyruğu
//! periyodik boşaltıp tek transaction'da batch yazar ve eski kayıtları budar.
//!
//! Bu Özellik#1 (yerel UI açıklaması) ve Özellik#2 (WS VendorEvent yayını)
//! yanına gelen ÜÇÜNCÜ fan-out'tur — aynı üretim noktaları, yeni keşif yok.
//! Kapsam: yalnız YAZMA. Sorgu/kalibrasyon (ROADMAP madde 5) ayrı özellik.

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex, OnceLock};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use crossbeam::queue::ArrayQueue;
use rusqlite::Connection;
use tracing::info;

/// Healing sonucu kodları — şemadaki `outcome` sütunu. FFI kontratıyla değil,
/// yalnız log şemasıyla ilgili (madde 5 sorgu katmanı bu kodları çözer).
pub const OUTCOME_APPLIED: u32 = 0;
pub const OUTCOME_PENDING: u32 = 1;
pub const OUTCOME_REJECTED: u32 = 2;
pub const OUTCOME_INVALIDATED: u32 = 3;

/// Kuyruk kapasitesi. Healing kararları ~1/sn (tick) + seyrek kullanıcı aksiyonu;
/// `FLUSH_INTERVAL` içinde taşma pratikte imkânsız (`metric_ring` 256 emsali).
const QUEUE_CAP: usize = 256;

/// Yazma thread'inin kuyruğu boşaltma periyodu. Düşük tutulur (latency) ama
/// batch olduğundan maliyet ihmal edilebilir.
const FLUSH_INTERVAL: Duration = Duration::from_millis(250);

/// Retention: kayıtlar bu yaştan eski olduğunda budanır. Zaman-tabanlı sınır —
/// kullanım yoğunluğundan bağımsız doğal birim ("son bir ay ne oldu"); kayıt-
/// sayısı değil (biri günde 5 biri 500 karar üretebilir). Ayarlanabilir sabit;
/// konfig yüzeyine YAGNI gereği çıkarılmadı.
const RETENTION_DAYS: u64 = 30;

/// İki budama arasındaki minimum süre — her flush'ta DELETE koşturmamak için.
const PRUNE_INTERVAL: Duration = Duration::from_secs(60);

const MICROS_PER_DAY: u64 = 24 * 60 * 60 * 1_000_000;

/// Bir healing kararının log kaydı — şema sütunlarıyla birebir. Üretim noktaları
/// bunu kurup `log_healing`'e verir. Append-only: her durum-geçişi (applied →
/// sonradan invalidated gibi) `action_id` ile ilişkilendirilen ayrı bir satırdır
/// (UPDATE yok). `applied`/`pending` kaydı tam üçlüyü (metric/value/threshold)
/// taşır; `rejected`/`invalidated` anında yalnız aksiyon bilinir → `metric_id`
/// None (8), değerler 0.
#[derive(Debug, Clone)]
pub struct HealingLogRecord {
    pub ts_unix_us: u64,
    pub action_id: u32,
    pub rule_id: String,
    /// `rules::metric_id` kodu (8 = None/açıklanamayan).
    pub metric_id: u32,
    pub current_value: i32,
    pub threshold: i32,
    /// `RjActionType` u32 discriminant.
    pub action_type: u32,
    /// `OUTCOME_*`.
    pub outcome: u32,
    /// Healing modu (0=AutoPilot,1=CoPilot,2=Assist,3=Manual).
    pub mode: u32,
}

impl HealingLogRecord {
    /// Şu anki zaman damgasıyla kayıt kurar (üretim noktaları için ergonomi).
    #[allow(clippy::too_many_arguments)]
    pub fn now(
        action_id: u32,
        rule_id: String,
        metric_id: u32,
        current_value: i32,
        threshold: i32,
        action_type: u32,
        outcome: u32,
        mode: u32,
    ) -> Self {
        Self {
            ts_unix_us: now_us(),
            action_id,
            rule_id,
            metric_id,
            current_value,
            threshold,
            action_type,
            outcome,
            mode,
        }
    }
}

fn now_us() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_micros() as u64)
        .unwrap_or(0)
}

/// Süreç-global yazma kuyruğu. `start_writer` bir kez kurar; `log_healing` push eder.
static QUEUE: OnceLock<Arc<ArrayQueue<HealingLogRecord>>> = OnceLock::new();

/// V10/L14: writer thread'inin kontrol kulpu — `shutdown_writer` sinyal + join
/// için tutar. `start_writer` doldurur; shutdown `take` ile idempotent boşaltır.
static CONTROL: Mutex<Option<WriterControl>> = Mutex::new(None);

/// V10/L14: shutdown sinyali (Condvar — writer FLUSH_INTERVAL uykusundan anında
/// uyanır) + join edilecek thread kulpu.
struct WriterControl {
    stop: Arc<(Mutex<bool>, Condvar)>,
    handle: thread::JoinHandle<()>,
}

impl WriterControl {
    /// Sinyali kaldırır, writer'ı uyandırır ve son flush'ını bekler (join).
    fn shutdown(self) {
        {
            let (lock, cvar) = &*self.stop;
            *lock.lock().unwrap_or_else(|e| e.into_inner()) = true;
            cvar.notify_all();
        }
        if self.handle.join().is_err() {
            eprintln!("[HealingLog] writer thread join panik ile bitti");
        }
    }
}

/// Kuyruk dolu (writer geride/kapalı/başlatılmamış) olduğu için düşürülen kayıt
/// sayısı — `ffi::DROPPED_*_COUNT` emsali (sesli/sayılabilir kayıp, sessiz değil).
pub static DROPPED_LOGS_COUNT: AtomicU64 = AtomicU64::new(0);

/// Üretim noktalarının çağırdığı TEK yüzey: kaydı kuyruğa non-blocking push eder.
/// Writer başlatılmadıysa veya kuyruk doluysa sessizce düşürür + sayar. Disk/kilit
/// yok → healing akışı (tick veya C++ frame thread) asla bloklanmaz/etkilenmez.
pub fn log_healing(record: HealingLogRecord) {
    let Some(q) = QUEUE.get() else { return; };
    if q.push(record).is_err() {
        DROPPED_LOGS_COUNT.fetch_add(1, Ordering::Relaxed);
    }
}

/// Yazma thread'ini başlatır: kuyruğu kurar, ayrı `std::thread`'de DB'yi açıp
/// migrate eder ve batch yazma+budama döngüsünü koşturur. Bir kez etkili (tekrar
/// çağrı no-op). DB açılamazsa sesli loglar; healing akışı etkilenmez.
pub fn start_writer(db_path: PathBuf) {
    if QUEUE.get().is_some() {
        return; // zaten başlatıldı
    }
    let queue = Arc::new(ArrayQueue::new(QUEUE_CAP));
    if QUEUE.set(queue.clone()).is_err() {
        return; // yarış: başka çağrı kurdu
    }

    if let Some(ctrl) = spawn_writer(db_path, queue) {
        *CONTROL.lock().unwrap_or_else(|e| e.into_inner()) = Some(ctrl);
    }
}

/// V10/L14: düzenli kapanışta writer'ı durdurur — kuyruktaki son kayıtlar
/// (son `FLUSH_INTERVAL` penceresine düşenler dahil) diske yazıldıktan sonra
/// döner. İdempotent; writer hiç başlamadıysa no-op. Çağrı sonrası
/// `log_healing` push'ları drop+sayılır (`DROPPED_LOGS_COUNT`).
pub fn shutdown_writer() {
    let ctrl = CONTROL.lock().unwrap_or_else(|e| e.into_inner()).take();
    if let Some(ctrl) = ctrl {
        ctrl.shutdown();
    }
}

/// Writer thread'ini kurar (test edilebilir çekirdek — global state'e dokunmaz).
fn spawn_writer(
    db_path: PathBuf,
    queue: Arc<ArrayQueue<HealingLogRecord>>,
) -> Option<WriterControl> {
    let stop = Arc::new((Mutex::new(false), Condvar::new()));
    let stop_thread = stop.clone();
    match thread::Builder::new()
        .name("healing-log-writer".into())
        .spawn(move || writer_loop(queue, db_path, stop_thread))
    {
        Ok(handle) => Some(WriterControl { stop, handle }),
        Err(e) => {
            // Thread kurulamazsa kuyruk dolar ve push'lar drop+sayılır (yut ama sesli).
            eprintln!("[HealingLog] writer thread spawn edilemedi: {} — log devre dışı", e);
            None
        }
    }
}

fn writer_loop(
    queue: Arc<ArrayQueue<HealingLogRecord>>,
    db_path: PathBuf,
    stop: Arc<(Mutex<bool>, Condvar)>,
) {
    let conn = match open_db(&db_path) {
        Ok(c) => c,
        Err(e) => {
            eprintln!(
                "[HealingLog] DB açılamadı ({:?}): {} — healing-log devre dışı, healing akışı etkilenmiyor",
                db_path, e
            );
            return; // kuyruk dolunca push'lar drop+sayılır
        }
    };
    info!(path = ?db_path, "healing-log writer başladı");

    let mut batch: Vec<HealingLogRecord> = Vec::with_capacity(QUEUE_CAP);
    let mut last_prune = Instant::now();
    loop {
        // V10/L14: sabit uyku yerine Condvar bekleyişi — shutdown sinyali
        // FLUSH_INTERVAL uykusunu anında keser; kilit tutulurken sinyal
        // atlanamaz (bekleme kilidi atomik bırakır — kayıp uyanma yok).
        let stopping = {
            let (lock, cvar) = &*stop;
            let mut guard = lock.lock().unwrap_or_else(|e| e.into_inner());
            if !*guard {
                guard = cvar
                    .wait_timeout(guard, FLUSH_INTERVAL)
                    .unwrap_or_else(|e| e.into_inner())
                    .0;
            }
            *guard
        };

        batch.clear();
        while let Some(rec) = queue.pop() {
            batch.push(rec);
            if batch.len() >= QUEUE_CAP {
                break;
            }
        }
        if !batch.is_empty() {
            if let Err(e) = insert_batch(&conn, &batch) {
                // Batch kaybı sesli loglanır; healing akışı zaten push'u beklemedi.
                eprintln!("[HealingLog] batch yazımı başarısız ({} kayıt düştü): {}", batch.len(), e);
            }
        }

        if stopping {
            // Son flush yukarıda yapıldı — budamayı atla, düzenli çık.
            info!("healing-log writer düzenli kapanışla durdu");
            return;
        }

        if last_prune.elapsed() >= PRUNE_INTERVAL {
            let cutoff = now_us().saturating_sub(RETENTION_DAYS * MICROS_PER_DAY);
            match prune_older_than(&conn, cutoff) {
                Ok(n) if n > 0 => info!(pruned = n, "healing-log retention budaması"),
                Ok(_) => {}
                Err(e) => eprintln!("[HealingLog] retention budaması başarısız: {}", e),
            }
            last_prune = Instant::now();
        }
    }
}

/// DB'yi açar, WAL + synchronous=NORMAL ayarlar, şemayı kurar. WAL: tek yazar +
/// (madde 5) ileride eşzamanlı okuma sorunsuz. synchronous=NORMAL: uygulama-crash'e
/// karşı güvenli, yalnız OS-crash'te son txn riski — bir log için kabul edilebilir
/// dayanıklılık/performans dengesi.
fn open_db(path: &Path) -> rusqlite::Result<Connection> {
    let conn = Connection::open(path)?;
    conn.pragma_update(None, "journal_mode", "WAL")?;
    conn.pragma_update(None, "synchronous", "NORMAL")?;
    init_schema(&conn)?;
    Ok(conn)
}

/// Şema + migration — idempotent (`IF NOT EXISTS`). İlk kurulumda tabloyu+indeksi
/// oluşturur; sonraki açılışlarda no-op. `ts_unix_us` indeksi madde 5'in zaman-
/// aralığı sorguları içindir.
fn init_schema(conn: &Connection) -> rusqlite::Result<()> {
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS healing_log (
            id            INTEGER PRIMARY KEY,
            ts_unix_us    INTEGER NOT NULL,
            action_id     INTEGER NOT NULL,
            rule_id       TEXT    NOT NULL DEFAULT '',
            metric_id     INTEGER NOT NULL,
            current_value INTEGER NOT NULL,
            threshold     INTEGER NOT NULL,
            action_type   INTEGER NOT NULL,
            outcome       INTEGER NOT NULL,
            mode          INTEGER NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_healing_ts ON healing_log(ts_unix_us);",
    )
}

/// Kayıtları tek transaction'da yazar (batch — her INSERT'te fsync değil).
/// `ts_unix_us` (u64) SQLite INTEGER (i64) olarak yazılır; mikrosaniye epoch ~292471
/// yıla dek i64'e sığar (rusqlite u64 ToSql sunmaz — bilinçli `as i64` cast).
fn insert_batch(conn: &Connection, records: &[HealingLogRecord]) -> rusqlite::Result<()> {
    let tx = conn.unchecked_transaction()?;
    {
        let mut stmt = tx.prepare_cached(
            "INSERT INTO healing_log
             (ts_unix_us, action_id, rule_id, metric_id, current_value, threshold, action_type, outcome, mode)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
        )?;
        for r in records {
            stmt.execute(rusqlite::params![
                r.ts_unix_us as i64,
                r.action_id,
                r.rule_id,
                r.metric_id,
                r.current_value,
                r.threshold,
                r.action_type,
                r.outcome,
                r.mode,
            ])?;
        }
    }
    tx.commit()
}

/// `cutoff_us`'ten eski kayıtları siler; silinen satır sayısını döner (retention).
fn prune_older_than(conn: &Connection, cutoff_us: u64) -> rusqlite::Result<usize> {
    conn.execute(
        "DELETE FROM healing_log WHERE ts_unix_us < ?1",
        rusqlite::params![cutoff_us as i64],
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rec(ts: u64, id: u32, outcome: u32) -> HealingLogRecord {
        HealingLogRecord {
            ts_unix_us: ts,
            action_id: id,
            rule_id: "gpu_thermal".into(),
            metric_id: 1, // GPU_TEMP_C
            current_value: 87,
            threshold: 85,
            action_type: 2, // ScaleResolution
            outcome,
            mode: 1, // CoPilot
        }
    }

    #[test]
    fn schema_init_is_idempotent() {
        // Migration iki kez çağrılınca (ilk kurulum + sonraki açılış) panik/err yok.
        let conn = Connection::open_in_memory().unwrap();
        init_schema(&conn).unwrap();
        init_schema(&conn).unwrap();
    }

    #[test]
    fn insert_and_read_back_roundtrip() {
        // Bir kayıt doğru şema ile yazılır ve birebir geri okunur.
        let conn = Connection::open_in_memory().unwrap();
        init_schema(&conn).unwrap();
        insert_batch(&conn, &[rec(1_000, 42, OUTCOME_APPLIED)]).unwrap();

        let (ts, id, rule, metric, val, thr, atype, outcome, mode): (
            i64, u32, String, u32, i32, i32, u32, u32, u32,
        ) = conn
            .query_row(
                "SELECT ts_unix_us, action_id, rule_id, metric_id, current_value, threshold, action_type, outcome, mode FROM healing_log",
                [],
                |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?, r.get(3)?, r.get(4)?, r.get(5)?, r.get(6)?, r.get(7)?, r.get(8)?)),
            )
            .unwrap();
        assert_eq!(ts, 1_000);
        assert_eq!(id, 42);
        assert_eq!(rule, "gpu_thermal");
        assert_eq!(metric, 1);
        assert_eq!(val, 87);
        assert_eq!(thr, 85);
        assert_eq!(atype, 2);
        assert_eq!(outcome, OUTCOME_APPLIED);
        assert_eq!(mode, 1);
    }

    #[test]
    fn insert_batch_writes_all_rows() {
        // Tek transaction'da birden çok kayıt — hepsi yazılır.
        let conn = Connection::open_in_memory().unwrap();
        init_schema(&conn).unwrap();
        insert_batch(
            &conn,
            &[
                rec(1, 1, OUTCOME_APPLIED),
                rec(2, 2, OUTCOME_PENDING),
                rec(3, 3, OUTCOME_REJECTED),
            ],
        )
        .unwrap();
        let n: u32 = conn
            .query_row("SELECT COUNT(*) FROM healing_log", [], |r| r.get(0))
            .unwrap();
        assert_eq!(n, 3);
    }

    #[test]
    fn shutdown_flushes_pending_records_without_waiting_interval() {
        // V10/L14: kapanış sinyali writer'ı FLUSH_INTERVAL uykusundan uyandırır,
        // kuyruktaki son kayıtlar diske iner — eski `loop{sleep}` düzeninde bu
        // pencere (son ~250ms) süreç çıkışında sessizce kayboluyordu.
        let dir = std::env::temp_dir().join("reji_l14_flush");
        std::fs::create_dir_all(&dir).unwrap();
        let db = dir.join(format!("healing_l14_{}.sqlite", std::process::id()));
        let _ = std::fs::remove_file(&db);

        // Global QUEUE/CONTROL'e dokunmadan izole writer (test edilebilir çekirdek).
        let queue: Arc<ArrayQueue<HealingLogRecord>> = Arc::new(ArrayQueue::new(QUEUE_CAP));
        let ctrl = spawn_writer(db.clone(), queue.clone()).expect("writer spawn edilmeli");

        queue.push(rec(1_000, 7, OUTCOME_APPLIED)).unwrap();
        queue.push(rec(2_000, 8, OUTCOME_PENDING)).unwrap();

        let started = Instant::now();
        ctrl.shutdown();
        // Sinyal uyandırma çalışıyorsa join FLUSH_INTERVAL'den belirgin kısa sürer
        // (DB açılışı dahil pay bırakılır — kilit iddia aşağıdaki satır sayısı).
        assert!(started.elapsed() < Duration::from_secs(5), "shutdown makul sürede dönmeli");

        let conn = Connection::open(&db).unwrap();
        let n: u32 = conn
            .query_row("SELECT COUNT(*) FROM healing_log", [], |r| r.get(0))
            .unwrap();
        assert_eq!(n, 2, "kapanış öncesi push'lanan TÜM kayıtlar son flush ile diske inmeli");
        drop(conn);
        let _ = std::fs::remove_file(&db);
    }

    #[test]
    fn prune_removes_only_old_rows() {
        // Retention: yalnız cutoff'tan eski kayıtlar silinir, yenisi kalır.
        let conn = Connection::open_in_memory().unwrap();
        init_schema(&conn).unwrap();
        insert_batch(
            &conn,
            &[
                rec(100, 1, OUTCOME_APPLIED),
                rec(200, 2, OUTCOME_APPLIED),
                rec(5_000, 3, OUTCOME_APPLIED),
            ],
        )
        .unwrap();
        let removed = prune_older_than(&conn, 1_000).unwrap();
        assert_eq!(removed, 2, "yalnız cutoff'tan (1000) eski iki kayıt silinmeli");
        let n: u32 = conn
            .query_row("SELECT COUNT(*) FROM healing_log", [], |r| r.get(0))
            .unwrap();
        assert_eq!(n, 1, "cutoff'tan yeni tek kayıt kalmalı");
    }
}
