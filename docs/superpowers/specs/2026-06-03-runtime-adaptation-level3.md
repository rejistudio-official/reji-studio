# Runtime Adaptation Seviye 3 Spec
**v0.4** — Frame Drop, Thermal, Network Metrikleri → Bitrate/Kalite Otomatik Adaptasyon

---

## Özet

Reji Studio, pipeline'dan gelen gerçek zamanlı metrikleri (frame drop %, GPU/CPU sıcaklık, network RTT) analiz ederek encode bitrate, preview kalitesi ve CPU/GPU iş yükünü otomatik olarak ayarlar. Adaptasyon kuralları JSON/TOML config dosyalarından okunur, hot-reload destekler ve 4 davranış modu (Auto-Pilot, Co-Pilot, Assist, Manual) sunar.

---

## Motivasyon

**Problem:**
- Live stream sırasında ağ bozulması, thermal throttling veya CPU overload → frame drop → viewer experience düşer
- Sabit bitrate coding → darboğazda kütüphanede buffering, low-end hardware'de stall
- Şu an: static config, kullanıcı manuel ayarlama

**Çözüm:**
- Otomatik adaptasyon: metrikleri izle, rule engine'de karar ver, aksiyonu execute et
- Kademeli geri kazanım: durumu düzeldikçe kaliteyi restore
- Hysteresis: çok sık değişmeyin diye 10s aralık minimum
- 4 mod: başlangıç → otomatik, standart → checkbox approve, uzman → log-only
- Bulut profil: "RTX 4070 + 1080p60@6000kbps" gibi başarılı config hafızası (v1.0'de)

---

## Kapsam

### Seviye 3 Implementasyon (v0.4)

#### 1. Frame Drop Counter & Izleme
- **Mevcut:** `pipeline.cpp::FrameProfiler` → zaten `frame_drop` sayıyor
- **Genişletme:** `frame_drop_rate = frame_drop / total_frames * 100` — 30s rolling window
- **Metric:** `RjMetricSample.frame_drop_pct` (yeni field)
- **Threshold:** Warning > 5%, Critical > 15%

#### 2. GPU/CPU Sıcaklık İzleme (WMI)
- **WMI Query:** `Win32_OperatingSystem`, `Win32_PerfFormattedData_PerfProc_Process`
- **GPU Temp:** AMD ADL fallback, NVIDIA NVAPI fallback, generic WMI sensor
- **CPU Temp:** WMI sensor ortalama, logical core max
- **Metric:** `RjMetricSample.gpu_temp_c`, `cpu_temp_c` (yeni)
- **Update Rate:** 1 Hz (every 1 second)
- **Threshold:** Throttle warning > 80°C, critical > 85°C

#### 3. Bitrate Adaptasyon (Kural Tabanlı)
- **Aksiyon:** `SetBitrate(new_bitrate_kbps)` → pipeline encode encoder'a pass
- **Düşürme:** Frame drop > 10% → bitrate -= 500 kbps (step)
- **Artırma:** Frame drop < 5% → bitrate += 250 kbps (kademeli geri kazanım)
- **Hysteresis:** Min 10s arasında state değişikliği (oscillation prevent)
- **Range:** Min 500 kbps, Max user-specified (default 8000)

#### 4. Kalite Adaptasyon
- **Resolution Scaling:**
  - GPU temp > 85°C OR frame drop > 15% → half resolution (1080p → 720p)
  - GPU temp > 80°C OR frame drop > 10% → maintain
  - GPU temp < 70°C AND frame drop < 5% → restore full res
- **FPS Cap:**
  - Frame drop > 15% → preview 30fps cap (encode unchanged)
  - Frame drop < 5% → restore preview 60fps
- **Preview Quality:**
  - RAM > 85% → quarter resolution (1920x1080 → 960x540)
  - RAM < 70% → restore

#### 5. Ağ Metrikleri (Optional v0.4.1+)
- **RTT (Round Trip Time):** Ortalama > 50ms → encoder preset düşür (fast → faster)
- **Packet Loss:** > 5% → FEC redundancy +2% (experimental)
- **Jitter:** > 30ms → buffer size +50%
- **Source:** SRT protocol built-in RTT, fallback ICMP ping

#### 6. Kural Motoru (Rule Engine)
- **Format:** JSON veya TOML, user-editable
- **File:** `~/.reji/rules.json` (default) veya `--rules-file` flag
- **Hot-reload:** SIGHUP (Unix) / NamedEvent (Windows) trigger
- **Rule Structure:**
  ```json
  {
    "rules": [
      {
        "id": "frame_drop_high",
        "condition": "frame_drop_pct > 10 && hysteresis_ms > 10000",
        "action": "bitrate_reduce",
        "params": {"step_kbps": 500},
        "modes": ["auto-pilot", "co-pilot", "assist"]
      },
      {
        "id": "thermal_throttle",
        "condition": "gpu_temp_c > 85",
        "action": "scale_resolution",
        "params": {"scale_factor": 0.5},
        "modes": ["auto-pilot", "co-pilot"]
      }
    ]
  }
  ```

#### 7. HealingOverlay Entegrasyonu
- **Action Display:** "Frame drop detected, reducing bitrate..." → overlay text
- **Notification:** Desktop notification + opt-in sound
- **Approve UI:** Co-Pilot modda checkbox list → seçili aksiyonlar execute
- **History:** Last 10 action log (HealingOverlay::action_history)

#### 8. Dört Davranış Modu
| Mod | Kapsam | Davranış | Approve |
|---|---|---|---|
| **Auto-Pilot** | Başlangıç | Tüm aksiyonlar otomatik | Yok |
| **Co-Pilot** | Standart | Aksiyon listesi → checkbox seçim | User seçer |
| **Assist** | Uzman | Kritik otomatik, orta/düşük = log only | Yok |
| **Manual** | Uzman | Tüm adaptasyon off, log + başlangıç uyarı | Devre dışı |

---

## Mimari

### Veri Akışı

```
Pipeline (C++)
  ├─ frame_drop_pct, gpu_temp_c, cpu_temp_c → RjMetricSample
  └─ FFI: rj_metrics_poll() → Rust

Orchestrator (Rust)
  ├─ RuleEngine::evaluate(metrics, rules) → [Action]
  ├─ HealingDecider::filter_by_mode(actions, mode) → [FilteredAction]
  └─ FFI: rj_action_enqueue(action) → Pipeline

Pipeline (C++)
  ├─ FFI: rj_action_dequeue() → action struct
  └─ Encoder: SetBitrate(bitrate), SetResolution(scale), SetFpsLimit(fps)

HealingOverlay (UI, Qt)
  ├─ Listen: action_event signal → updateUI()
  └─ User: approveAction(action_id) → rj_action_approve() FFI
```

### Modülleri

#### C++ Pipeline (`src/pipeline/`)
- **`include/metrics_collector.h`** — GPU temp, frame drop, CPU load collection
  - `class MetricsCollector`
  - `poll_thermal_sensors()` — WMI query
  - `get_latest_metrics()` — thread-safe RjMetricSample
- **`capture/capture_dxgi.cpp`** — frame_drop counter (zaten var, expand)
  - `AcquireNextFrame()` → frame_drop track
  - Add: `frame_drop_pct` calculation, 30s rolling window

#### Rust Orchestrator (`src/orchestrator/`)
- **`rule_engine.rs`** — JSON/TOML parser, rule evaluation
  - `struct Rule { condition, action, params, modes }`
  - `impl RuleEngine`
  - `fn evaluate(&self, metrics: &Metrics) -> Vec<Action>`
  - `fn hot_reload(&mut self, path: &str) -> Result<(), Box<dyn Error>>`
- **`metrics.rs`** (expand) — AdaptationDecider
  - Hysteresis tracking: `last_action_time: Instant`
  - Mode filtering: `fn filter_by_mode(&self, actions, mode) -> Vec<Action>`
  - Queue management: `action_queue: crossbeam::queue::ArrayQueue`
- **`healing.rs`** (extend) — HealingOverlay communication
  - `struct ActionEvent { id, action, timestamp, status }`
  - `fn notify_ui(event: ActionEvent)` → NamedPipe

#### Qt UI (`src/ui/`)
- **`healing_overlay.cpp`** (extend)
  - `void onActionEvent(const ActionEvent& evt)` — display action
  - `QListWidget action_history` — last 10 actions
  - Co-Pilot mode: `QCheckBox[] action_checkboxes` → approve UI
  - `void approveAction(int action_id)` → FFI call

#### FFI Boundary (`src/ffi/`)
- **`ffi_bridge.h`** — new structures
  ```c
  typedef struct {
    uint32_t frame_drop_pct;
    int16_t gpu_temp_c;
    int16_t cpu_temp_c;
    int32_t reserved;
  } RjMetricSample;

  typedef struct {
    uint32_t id;
    uint32_t action_type;  // BITRATE_REDUCE, SCALE_RESOLUTION, etc.
    int32_t param1;
    int32_t param2;
  } RjAction;

  typedef enum {
    RJ_MODE_AUTO_PILOT = 0,
    RJ_MODE_CO_PILOT = 1,
    RJ_MODE_ASSIST = 2,
    RJ_MODE_MANUAL = 3,
  } RjHealingMode;

  extern "C" {
    __declspec(noinline) bool rj_metrics_poll(RjMetricSample* out);
    __declspec(noinline) bool rj_action_dequeue(RjAction* out);
    __declspec(noinline) bool rj_action_approve(uint32_t action_id);
    __declspec(noinline) bool rj_set_healing_mode(RjHealingMode mode);
  }
  ```

---

## Detaylı Tasarım

### 1. Metrikleri Toplama (C++)

**`MetricsCollector` sınıfı:**
```cpp
class MetricsCollector {
  std::chrono::steady_clock::time_point poll_deadline;
  RjMetricSample metrics;
  std::mutex metrics_lock;

public:
  bool poll() {  // Called every 1s from background thread
    if (now < poll_deadline) return true;  // throttle polling
    
    metrics.frame_drop_pct = calculate_frame_drop_pct();  // 30s window
    metrics.gpu_temp_c = query_gpu_thermal_wmi();         // with fallback
    metrics.cpu_temp_c = query_cpu_thermal_wmi();
    
    poll_deadline = now + 1s;
    return true;
  }

  RjMetricSample get_latest() {
    std::lock_guard lock(metrics_lock);
    return metrics;
  }
};
```

**Frame Drop Calculation:**
```cpp
float calculate_frame_drop_pct() {
  static std::deque<uint32_t> drop_window;  // 30s @ 60fps = 1800 frames
  uint32_t current_drop = frame_profiler_->dropped_frames();
  
  drop_window.push_back(current_drop);
  while (drop_window.size() > 1800) drop_window.pop_front();
  
  uint32_t total_drops = std::accumulate(drop_window.begin(), drop_window.end(), 0u);
  return (total_drops / (float)(drop_window.size() + 1)) * 100.f;
}
```

**GPU Temp Fallback Chain:**
```
AMD ADL (if available)
  → NVIDIA NVAPI (if available)
    → WMI Win32_OperatingSystem sensor
      → fallback 0°C (sensor unavailable)
```

### 2. Kural Değerlendirmesi (Rust)

**`RuleEngine` impl:**
```rust
pub struct RuleEngine {
  rules: Vec<Rule>,
  last_reload: Instant,
  file_path: PathBuf,
}

impl RuleEngine {
  pub fn evaluate(&self, metrics: &Metrics, mode: HealingMode) -> Vec<Action> {
    let mut actions = Vec::new();
    
    for rule in &self.rules {
      if !rule.modes.contains(&mode) { continue; }
      
      if rule.eval_condition(metrics) {
        let action = rule.create_action(metrics);
        actions.push(action);
      }
    }
    
    actions
  }

  pub fn hot_reload(&mut self) -> Result<(), Box<dyn Error>> {
    let now = Instant::now();
    if now.duration_since(self.last_reload) < Duration::from_secs(1) {
      return Ok(());  // throttle reload
    }
    
    let content = std::fs::read_to_string(&self.file_path)?;
    self.rules = serde_json::from_str(&content)?;
    self.last_reload = now;
    Ok(())
  }
}
```

### 3. Hysteresis & Mode Filtering

**`AdaptationDecider` (Rust):**
```rust
pub struct AdaptationDecider {
  last_action_time: Instant,
  hysteresis_min_ms: u64,  // default 10000ms = 10s
  mode: HealingMode,
}

impl AdaptationDecider {
  pub fn should_adapt(&self) -> bool {
    let elapsed = self.last_action_time.elapsed().as_millis();
    elapsed >= self.hysteresis_min_ms as u128
  }

  pub fn filter_by_mode(&self, actions: Vec<Action>) -> Vec<Action> {
    match self.mode {
      HealingMode::AutoPilot => {
        // All actions pass
        actions
      },
      HealingMode::CoPilot => {
        // Critical actions pass, orta/düşük = require_approval flag
        actions.iter().map(|a| {
          if a.is_critical() {
            Action { require_approval: false, ..a }
          } else {
            Action { require_approval: true, ..a }
          }
        }).collect()
      },
      HealingMode::Assist => {
        // Critical actions pass, orta/düşük = log_only flag
        actions.iter().map(|a| {
          if a.is_critical() {
            Action { require_approval: false, ..a }
          } else {
            Action { log_only: true, ..a }
          }
        }).collect()
      },
      HealingMode::Manual => {
        // No actions pass
        Vec::new()
      },
    }
  }
}
```

### 4. Action Dispatching

**Action Types:**
```rust
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum ActionType {
  BitrateReduce { step_kbps: i32 },
  BitrateRecover { step_kbps: i32 },
  ScaleResolution { scale_factor: f32 },
  RestoreResolution,
  CapFps { fps_limit: u32 },
  RestoreFps,
  LogOnly { message: String },
}

pub struct Action {
  pub id: u32,
  pub action_type: ActionType,
  pub timestamp: Instant,
  pub require_approval: bool,
  pub log_only: bool,
}
```

**Action Queue (lock-free, Rust → C++):**
```rust
// In orchestrator.rs
static ACTION_QUEUE: ArrayQueue<Action> = ArrayQueue::new(64);

pub fn enqueue_action(action: Action) -> Result<(), Action> {
  ACTION_QUEUE.push(action)
}
```

**FFI Dequeue (C++):**
```c
bool rj_action_dequeue(RjAction* out) {
  // Call Rust fn: orchestrator::poll_action_queue()
  // Convert Rust Action → C RjAction
  // return true if action available
}
```

### 5. HealingOverlay Integration (Yaklaşım C: Co-Pilot Aksiyon Ayarları)

**Settings UI (Co-Pilot Mode):**
```
☑ Bitrate otomatik (varsayılan: açık)
☑ Kaynak yeniden bağlan (varsayılan: açık)
☐ Çözünürlük düşür (varsayılan: kapalı)
☐ FPS sınırla (varsayılan: kapalı)
```

**onActionEvent() Logic:**
- Aksiyon type'ı settings'de "otomatik" ise → direkt execute (checkbox state ignore)
- Aksiyon type'ı settings'de "manuel" ise → HealingOverlay checkbox göster, 30s timeout → iptal

**Qt Slot:**
```cpp
void HealingOverlay::onActionEvent(const ActionEvent& event) {
  // Add to history
  d_->history_list->insertItem(0, QString("[%1] %2").arg(event.timestamp, event.description));
  while (d_->history_list->count() > 10) {
    delete d_->history_list->takeItem(d_->history_list->count() - 1);
  }
  
  switch (d_->healing_mode) {
    case HealingMode::CoPilot: {
      // Check settings for this action type (e.g., bitrate_reduce)
      bool is_auto = get_action_setting_auto(event.action_type);  // Consult SettingsDialog
      
      if (is_auto) {
        // Auto-execute (don't show checkbox)
        emit actionApproved(event.id);
        d_->lbl_message->setText(QString("Auto: %1").arg(event.description));
        d_->history_list->show();
        show();
        d_->remaining_ms = 3000;
        d_->lbl_countdown->setText("3s");
        d_->timer->start();
      } else {
        // Manual: show checkbox, 30s timeout
        d_->lbl_message->setText(tr("Eylem onayı (30s):"));
        d_->action_list->clear();
        d_->action_list->show();
        
        auto* item = new QListWidgetItem(event.description);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        d_->action_list->addItem(item);
        
        d_->current_action_id = event.id;
        connect(...);  // checkbox toggle → emit actionApproved
        
        show();
        raise();
        d_->co_pilot_timeout->start();  // 30s → timeout cancels
      }
      break;
    }
    // ... other modes ...
  }
}
```

---

## Kural Dosyası Örnekler

### `~/.reji/rules.json`

```json
{
  "rules": [
    {
      "id": "frame_drop_mild",
      "description": "Frame drop < 10%, mild reduction",
      "condition": "frame_drop_pct > 5 && frame_drop_pct <= 10",
      "action": "bitrate_reduce",
      "params": {
        "step_kbps": 250
      },
      "modes": ["auto-pilot", "co-pilot", "assist"]
    },
    {
      "id": "frame_drop_high",
      "description": "Frame drop > 10%, aggressive reduction",
      "condition": "frame_drop_pct > 10",
      "action": "bitrate_reduce",
      "params": {
        "step_kbps": 500
      },
      "modes": ["auto-pilot", "co-pilot", "assist"]
    },
    {
      "id": "frame_drop_recovery",
      "description": "Frame drop < 5%, gradual bitrate recovery",
      "condition": "frame_drop_pct < 5",
      "action": "bitrate_recover",
      "params": {
        "step_kbps": 250
      },
      "modes": ["auto-pilot", "co-pilot"]
    },
    {
      "id": "gpu_thermal_throttle",
      "description": "GPU temp > 85°C, scale to half resolution",
      "condition": "gpu_temp_c > 85",
      "action": "scale_resolution",
      "params": {
        "scale_factor": 0.5
      },
      "modes": ["auto-pilot", "co-pilot"]
    },
    {
      "id": "gpu_thermal_restore",
      "description": "GPU temp < 70°C, restore full resolution",
      "condition": "gpu_temp_c < 70",
      "action": "restore_resolution",
      "params": {},
      "modes": ["auto-pilot", "co-pilot"]
    },
    {
      "id": "cpu_load_high",
      "description": "CPU > 90%, cap preview FPS to 30",
      "condition": "cpu_load_pct > 90",
      "action": "cap_fps",
      "params": {
        "fps_limit": 30
      },
      "modes": ["auto-pilot", "co-pilot"]
    },
    {
      "id": "memory_pressure",
      "description": "RAM > 85%, scale preview to quarter",
      "condition": "memory_usage_pct > 85",
      "action": "scale_resolution",
      "params": {
        "scale_factor": 0.25
      },
      "modes": ["auto-pilot", "co-pilot", "assist"]
    }
  ],
  "hysteresis_ms": 10000,
  "default_mode": "co-pilot"
}
```

### `~/.reji/rules.toml` (Alternative)

```toml
[metadata]
hysteresis_ms = 10000
default_mode = "co-pilot"

[[rules]]
id = "frame_drop_mild"
description = "Frame drop < 10%, mild reduction"
condition = "frame_drop_pct > 5 && frame_drop_pct <= 10"
action = "bitrate_reduce"
modes = ["auto-pilot", "co-pilot", "assist"]

[rules.params]
step_kbps = 250

[[rules]]
id = "gpu_thermal_throttle"
condition = "gpu_temp_c > 85"
action = "scale_resolution"
modes = ["auto-pilot", "co-pilot"]

[rules.params]
scale_factor = 0.5
```

---

## FFI Sınırı Spesifikasyonu

### Yeni RjMetricSample

```c
typedef struct {
  uint32_t frame_drop_pct;      // [0, 100]
  int16_t  gpu_temp_c;          // [-128, 127] °C
  int16_t  cpu_temp_c;          // [-128, 127] °C
  uint32_t memory_usage_pct;    // [0, 100]
  uint32_t cpu_load_pct;        // [0, 100]
  uint16_t network_rtt_ms;      // [0, 65535] ms
  uint8_t  network_loss_pct;    // [0, 100] %
  uint8_t  reserved;
  uint32_t canary;              // = 0xEEFF1234
} RjMetricSample;
```

### Yeni RjAction

```c
typedef enum {
  RJ_ACTION_BITRATE_REDUCE = 0,
  RJ_ACTION_BITRATE_RECOVER = 1,
  RJ_ACTION_SCALE_RESOLUTION = 2,
  RJ_ACTION_RESTORE_RESOLUTION = 3,
  RJ_ACTION_CAP_FPS = 4,
  RJ_ACTION_RESTORE_FPS = 5,
  RJ_ACTION_LOG_ONLY = 6,
} RjActionType;

typedef struct {
  uint32_t id;              // action unique ID
  RjActionType type;
  int32_t param1;           // step_kbps, fps_limit, etc.
  int32_t param2;           // reserved
  uint32_t canary;          // = 0xEEFF1234
} RjAction;
```

### Yeni FFI Fonksiyonlar

```c
// Poll latest metrics from pipeline
__declspec(noinline) bool rj_metrics_poll(RjMetricSample* out);

// Dequeue next action (FIFO)
__declspec(noinline) bool rj_action_dequeue(RjAction* out);

// Approve pending action (Co-Pilot mode)
__declspec(noinline) bool rj_action_approve(uint32_t action_id);

// Set healing mode
__declspec(noinline) bool rj_set_healing_mode(uint32_t mode);  // RjHealingMode

// Get current healing mode
__declspec(noinline) uint32_t rj_get_healing_mode(void);

// Reload rules from file (async)
__declspec(noinline) bool rj_reload_rules(const char* path);
```

---

## 4 Davranış Modu Detayları

### Auto-Pilot (Başlangıç Modu)
- **Davranış:** Tüm aksiyonlar tamamen otomatik, delay yok
- **Kullanıcı:** Başlangıç kullanıcıları
- **HealingOverlay:** "Bitrate reduced to 5.5Mbps due to frame drop..." bildirim
- **Onay:** Yok, execution immediate
- **Logging:** Action log tutulur, ileride gözden geçirebilir

```cpp
case RJ_MODE_AUTO_PILOT:
  for (const auto& action : actions) {
    execute_action(action);
    notify_ui(action, "auto");  // just info
  }
  break;
```

### Co-Pilot (Standart Modu)
- **Davranış:** Aksiyon listesi → checkbox seçim, seçili olanlar execute
- **Kullanıcı:** Standart canlı yayıncılar
- **HealingOverlay:** 
  - ☐ Reduce bitrate by 500 kbps
  - ☐ Scale preview to half resolution
  - ☐ Cap FPS to 30
- **Onay:** Kullanıcı checkbox → rj_action_approve(id)
- **Timeout:** 30s timeout sonra auto-execute kritik aksiyonlar

```cpp
case RJ_MODE_CO_PILOT:
  for (const auto& action : actions) {
    if (action.require_approval) {
      notify_ui_for_approval(action);  // show checkbox
      wait_for_approval(action_id, 30s);
    } else {
      execute_action(action);  // critical, auto-execute
    }
  }
  break;
```

### Assist (Uzman Modu)
- **Davranış:** Kritik aksiyonlar otomatik, orta/düşük = log-only
- **Kullanıcı:** Uzman yayıncılar, bilgisayar kontrol etmek isteyenler
- **HealingOverlay:** Log-only aksiyon → action history list
- **Onay:** Yok
- **Manual kontrol:** Kullanıcı UI'dan manuel adjust yapabilir

```cpp
case RJ_MODE_ASSIST:
  for (const auto& action : actions) {
    if (action.is_critical()) {
      execute_action(action);
    } else {
      log_action(action);  // history only
      notify_ui(action, "log");
    }
  }
  break;
```

### Manual (Uzman + Devre Dışı)
- **Davranış:** Tüm otomatik adaptasyon off
- **Kullanıcı:** Gelişmiş yayıncılar, production deneyimi
- **HealingOverlay:** Başlangıçta bir kez uyarı dialog
  - "Healing is disabled. You must manually adjust settings."
- **Onay:** Yok, devre dışı
- **Logging:** Durumu log'a yazılır (silently)

```cpp
case RJ_MODE_MANUAL:
  for (const auto& action : actions) {
    log_action(action, "suppressed");  // silent log
  }
  if (first_init) {
    show_warning_dialog("Healing disabled, manual control.");
  }
  break;
```

---

## Implementasyon Sırası

### Faz 1: Temel İnfrastrüktür (1-2 gün)
1. **C++:** `MetricsCollector` class + WMI thermal query
2. **Rust:** `RuleEngine` struct + JSON parser
3. **FFI:** `rj_metrics_poll()`, `rj_action_dequeue()`, `rj_set_healing_mode()`
4. **Build:** CMakeLists, Cargo.toml dependency (serde_json)

### Faz 2: Kural Değerlendirmesi (1 gün)
5. **Rust:** `AdaptationDecider` + hysteresis logic
6. **Rust:** Mode filtering (Auto-Pilot/Co-Pilot/Assist/Manual)
7. **Default rules.json** template oluştur
8. **Unit tests:** Rule evaluation, hysteresis

### Faz 3: Pipeline Integrasyonu (1 gün)
9. **C++:** Pipeline main loop'a `rj_metrics_poll()` call
10. **C++:** Orchestrator'dan `rj_action_dequeue()` poll, apply actions
11. **Encoder:** `SetBitrate()`, `SetResolution()`, `SetFpsLimit()` implement
12. **Integration test:** Metrics → Actions

### Faz 4: UI & Modes (1 gün)
13. **Qt:** `HealingOverlay::onActionEvent()` slot
14. **Qt:** Co-Pilot mode checkbox list
15. **Qt:** Mode selection dropdown (Settings)
16. **Qt:** Action history list

### Faz 5: Hot-Reload & Polish (0.5 gün)
17. **Rust:** `RuleEngine::hot_reload()`
18. **UI:** Rules editor (advanced settings)
19. **Logging:** Comprehensive debug logging
20. **Testing:** Manual scenarios (frame drop simulation, etc.)

---

## Test Planı

### Unit Tests

#### Rust
```rust
#[test]
fn test_rule_evaluation_frame_drop() {
  let engine = RuleEngine::from_json(r#"...rules.json..."#);
  let metrics = Metrics {
    frame_drop_pct: 12,
    ..Default::default()
  };
  let actions = engine.evaluate(&metrics, HealingMode::AutoPilot);
  assert_eq!(actions[0].action_type, ActionType::BitrateReduce);
}

#[test]
fn test_hysteresis() {
  let mut decider = AdaptationDecider::new(10000);  // 10s
  assert!(decider.should_adapt());
  
  std::thread::sleep(Duration::from_millis(5000));
  assert!(!decider.should_adapt());
  
  std::thread::sleep(Duration::from_millis(6000));
  assert!(decider.should_adapt());
}

#[test]
fn test_mode_filtering_co_pilot() {
  let decider = AdaptationDecider::new_with_mode(HealingMode::CoPilot);
  let actions = vec![
    Action { is_critical: true, .. },
    Action { is_critical: false, .. },
  ];
  let filtered = decider.filter_by_mode(actions);
  
  assert!(!filtered[0].require_approval);
  assert!(filtered[1].require_approval);
}
```

#### C++
```cpp
TEST(MetricsCollector, FrameDropCalculation) {
  MetricsCollector collector;
  // Simulate 100 frames, 5 drops
  for (int i = 0; i < 100; ++i) {
    if (i % 20 == 0) collector.record_frame_drop();
    collector.record_frame();
  }
  auto metrics = collector.get_latest();
  ASSERT_NEAR(metrics.frame_drop_pct, 5.0f, 0.1f);
}

TEST(MetricsCollector, ThermalSensor) {
  MetricsCollector collector;
  // Depends on system hardware, may skip on CI
  auto metrics = collector.get_latest();
  EXPECT_GE(metrics.gpu_temp_c, -128);
  EXPECT_LE(metrics.gpu_temp_c, 127);
}
```

### Integration Tests

1. **Frame Drop Adaptation:**
   - Simulate frame drop rate 12% → verify bitrate reduction
   - Wait 10s (hysteresis) → simulate drop < 5% → verify bitrate recovery

2. **Thermal Scaling:**
   - Mock GPU temp to 87°C → verify resolution scaled to 0.5x
   - Mock GPU temp to 65°C → verify restoration

3. **Mode Behavior:**
   - Auto-Pilot: action → immediate execute
   - Co-Pilot: action → checkbox approval required
   - Assist: critical action → auto, other → log
   - Manual: all actions suppressed

4. **Hot-Reload:**
   - Load rules.json
   - Modify rules.json on disk
   - Trigger hot-reload event
   - Verify new rules applied

### Manual Tests

1. **Real Frame Drop:** Simulate network congestion (tc qdisc) → observe bitrate reduction
2. **GPU Stress:** Run GPU intensive task → observe thermal adaptation
3. **UI Interactions:** Test each healing mode, co-pilot approval, action history

---

## Yapılandırma & Deployment

### Default Rules
- `docs/config/rules.json.template` — ship with app
- User rules: `~/.reji/rules.json` — edit allowed
- Fallback: hardcoded defaults (no rules file)

### Konfigürasyon Hiyerarşisi
```
1. User custom rules (~/.reji/rules.json)
2. App bundled rules (resources/rules.json.template)
3. Hardcoded defaults (src/orchestrator/defaults.rs)
```

### CLI Flags
```
--healing-mode {auto-pilot|co-pilot|assist|manual}
--rules-file <path>
--disable-healing  # alias for --healing-mode manual
```

### Settings UI
- Dropdown: Healing Mode
- Button: Edit Rules (open rules.json in editor)
- Checkbox: Auto-reload rules on file change

---

## Öğrenme & İyileştirme (Future)

### Seviye 4 (v0.5) Hazırlık
- Saat bazlı profil: sabah test, akşam live
- İzleyici sayısı adaptasyonu
- Platform-specific bitrate limits

### Seviye 5 (v1.0) Hazırlık
- SQLite oturum metrikleri kaydı
- Başarılı konfigürasyon tarafından otomatik preset önerisi
- Anomali tespiti ("GPU temp spike", "sudden frame drop burst")

---

## Riski & Mitigasyon

| Risk | Etki | Mitigation |
|---|---|---|
| Hysteresis çok kısa → oscillation | Quality jitter, viewer annoyance | Default 10s, user configurable |
| Frame drop calculation yanlış | Yanlış adaptation | Unit test, manual validation |
| Thermal sensor unavailable | No thermal adaptation | Graceful fallback, log warning |
| Action queue taşma (64 max) | Lost actions | Monitor queue, increase size if needed |
| Hot-reload crash | App restart gerekli | File validation before reload, rollback on error |
| Co-Pilot mode timeout → stale action | Old decision applied | Timestamp check, discard if >60s old |

---

## Referanslar

- `docs/memory.md` — Karar Motoru 6 seviye overview
- `docs/progress.md` — v0.4 roadmap
- `CONTEXT.md` — Mimari baseline
- `src/orchestrator/` — Rust module locations
- `src/pipeline/include/frame_profiler.h` — frame_drop metrikleri (mevcut)

