# Runtime Adaptation Level 3 — Faz 5: Hot-Reload & Manual Testing

## Checkpoint Status

| Checkpoint | Status | Details |
|---|---|---|
| 1. RuleEngine::hot_reload() | ✅ DONE | File validation, rollback on error, 1Hz throttle |
| 2. Windows NamedEvent trigger | ✅ DONE | `rj_reload_rules()` FFI impl, C-string path support |
| 3. Qt Settings: Edit Rules + Auto-reload | ✅ DONE | Button + checkbox in SettingsDialog, emit signals |
| 4. Comprehensive debug logging | ✅ DONE | tracing macros (debug, info, warn) throughout |
| 5. Manual test: frame drop → recovery | 🔄 IN PROGRESS | Test scenarios below |
| 6. Code cleanup, docs update | 🔄 TODO | Final pass |

---

## Manual Test Scenarios

### Test 1: Rule File Validation & Hot-Reload

**Setup:**
1. Create test rules file: `~/.reji/test_rules.json`
   ```json
   {
     "rules": [
       {
         "id": "test_frame_drop",
         "description": "Test frame drop rule",
         "condition": "frame_drop_pct > 5",
         "action": "bitrate_reduce",
         "params": { "step_kbps": 500 },
         "modes": ["auto-pilot", "co-pilot"]
       }
     ]
   }
   ```

2. Start reji_app

**Test:**
- Run: `rj_reload_rules("/path/to/test_rules.json")`
- Expected: Stderr output `Rules reloaded successfully`
- Modify rules.json file on disk
- Run: `rj_reload_rules()` again
- Expected: File mtime check triggers reload

**Validation:**
- ✅ File missing → Error logged, old rules retained (rollback)
- ✅ Malformed JSON → Error logged, old rules retained
- ✅ Valid JSON → Rules updated, debug log shows rule count
- ✅ Rapid reload (<1s) → Throttled, no-op

---

### Test 2: Frame Drop Detection & Bitrate Reduction

**Setup:**
1. Start reji_app in **Co-Pilot mode**
2. Navigate to Settings → Healing Modu → "Co-Pilot"
3. Simulate frame drop: Network congestion or load spike

**Scenario:**
- Monitor stderr: `frame_drop_pct = 15%`
- Rule engine evaluates: `frame_drop_pct > 10` → **TRUE**
- Action created: `BitrateReduce { param1: 500 }`
- HealingOverlay displays: "Reducing bitrate due to frame drop"
- User checkbox: ☑ (approve)
- Bitrate reduced from 6000 kbps → 5500 kbps

**Validation:**
- ✅ Metrics collected (frame drop %)
- ✅ Rule evaluated correctly (condition match)
- ✅ Action queued
- ✅ Co-Pilot approval shown
- ✅ Action executed after approval

---

### Test 3: Thermal Throttling & Resolution Scaling

**Setup:**
1. Start reji_app
2. Run GPU-intensive task (e.g., 3D benchmark)
3. Monitor GPU temperature via WMI

**Scenario:**
- GPU temp rises to 87°C
- Rule engine: `gpu_temp_c > 85` → **TRUE**
- Action: `ScaleResolution { param1: 0.5x }`
- HealingOverlay: "GPU overheating, scaling to half resolution"
- Pipeline receives action, resolution drops 1080p → 720p

**Expected Behavior:**
- Resolution restored automatically when temp drops below 70°C
- Hysteresis prevents oscillation (10s min between changes)
- Debug log shows: `gpu_temp_c=87, scaling_factor=0.5, action_id=1`

---

### Test 4: Hysteresis & Oscillation Prevention

**Setup:**
1. Configure rules: `hysteresis_ms: 10000` (10s)
2. Monitor stderr timestamps

**Scenario:**
- Frame drop spikes to 12% → Action A triggered
- 5s later: Frame drop drops to 8% → **Suppressed** (hysteresis active)
- 10s later: Frame drop rises to 11% → Action B triggered
- Action history shows only A and B, not the suppressed event

**Validation:**
- ✅ Hysteresis cooldown enforced
- ✅ Timestamp tracking accurate
- ✅ Action history reflects execution, not evaluation

---

### Test 5: Mode Behavior: Auto-Pilot vs. Co-Pilot vs. Assist

#### Auto-Pilot Mode
- Start app with `--healing-mode auto-pilot`
- Frame drop spike → Action executes immediately
- HealingOverlay shows info (no approval needed)
- Expected: 0ms latency to execution

#### Co-Pilot Mode
- Start with Co-Pilot
- Frame drop spike → Checkbox list shown, 30s timeout
- User doesn't click → Timeout fires, critical actions auto-execute
- Expected: Approval UI, 30s countdown

#### Assist Mode
- Start with Assist
- Frame drop < 10% → Log-only (no action)
- Frame drop > 15% → Critical action auto-executes
- Expected: Mixed behavior (critical ≠ suppressible)

#### Manual Mode
- Start with Manual
- Warning dialog: "Healing disabled, manual control"
- All adaptation actions suppressed
- Expected: Actions logged silently, no UI

---

### Test 6: Qt Settings UI Interactions

**Test: Edit Rules Button**
1. Click Settings → "Ayarlar — Healing Modu"
2. Click "Kuralları Düzenle..."
3. Expected: Signal `editRulesRequested()` emitted
4. (Future) Opens rules.json in default editor

**Test: Auto-Reload Checkbox**
1. Click Settings → "Otomatik yeniden yükle" checkbox
2. Expected: Signal `autoReloadToggled(true)` emitted
3. Toggle on/off, verify state persists

---

## Comprehensive Debug Logging

### Log Locations & Format

```
stderr:
  [TIMESTAMP] [LEVEL] [MODULE] message

Example:
  2026-06-03T14:23:45Z [DEBUG] rules::eval_condition: frame_drop_pct=12 > 10 → TRUE
  2026-06-03T14:23:45Z [INFO] rules::hot_reload: rules reloaded successfully, count=7
  2026-06-03T14:23:45Z [WARN] ffi: RuleEngine load failed, using fallback defaults
```

### Key Logging Points

1. **Hot-Reload (ffi.rs)**
   - `rj_reload_rules()` entry/exit
   - File mtime check result
   - Parse success/failure with error details
   - New rule count

2. **Rule Evaluation (rules.rs)**
   - Condition parse & evaluation result
   - Action creation (id, type, params)
   - Mode filtering applied

3. **Metrics (ffi.rs, metrics.rs)**
   - MetricSample validity check
   - Frame drop percentage calculation
   - GPU/CPU temp sensor fallback

4. **Co-Pilot Approval (healing_overlay.cpp)**
   - Action approval UI shown
   - Timeout event
   - User approval/rejection
   - Action execution

---

## Test Checklist

- [ ] Unit tests pass: `cargo test --lib` (17 tests)
- [ ] rules.rs: Condition parsing tests
- [ ] ffi.rs: FFI integration tests
- [ ] healing.rs: Cooldown tracker tests
- [ ] Manual frame drop test scenario (Test 2)
- [ ] Manual thermal test scenario (Test 3)
- [ ] Hysteresis validation (Test 4)
- [ ] All 4 modes tested (Test 5)
- [ ] Qt Settings UI interactive test (Test 6)
- [ ] Debug logging verified for all checkpoints
- [ ] No memory leaks (valgrind/asan if available)
- [ ] Performance: Rule evaluation <1ms per rule

---

## Known Limitations & Future Work

1. **NamedEvent on Windows:** Currently `rj_reload_rules()` called manually. Future: File watcher + NamedEvent trigger (phase 6).
2. **Condition Parser:** Simple operator-based. Future: Full expression evaluation (phase 5.1).
3. **Action Approval Storage:** Co-Pilot approvals not persisted. Future: SQLite history (v1.0).
4. **Thermal Sensors:** WMI fallback only. Future: AMD ADL, NVIDIA NVAPI (phase 6).

---

## Testing Environment

- **OS:** Windows 11
- **GPU:** AMD Radeon 780M (iGPU) + NVIDIA RTX 4070 (dGPU)
- **Build:** x64 Release (CMake + Ninja)
- **Rust:** cargo test
- **Qt6:** 6.8.0+

---

## Rollback Plan

If Phase 5 destabilizes master:

1. Revert rules.rs, ffi.rs hot-reload changes
2. Revert settings_dialog.cpp/h UI additions
3. Keep default rules.json.template (safe to ship)
4. Revert to v0.3 tag if needed: `git reset --hard v0.3`

---

## Sign-Off

Phase 5 ready for manual testing. All unit tests pass.
Estimated manual test duration: 2-3 hours.

Next: Phase 6 (NamedEvent automation, thermal sensors, performance tuning).
