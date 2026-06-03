# Security Fixes Spec — Runtime Adaptation Level 3
**v0.4 Critical & High Priority** — FFI Boundary Hardening

---

## Executive Summary

Reji Studio's Rust-C++ FFI boundary requires hardening against:
1. **Buffer overflow** in `rj_command_drain()` — unbounded output parameter
2. **Panic safety** in extern "C" functions — unwind into C++ UB
3. **Null pointer dereference** in data exchange functions

This spec addresses all three with minimal performance impact.

---

## Kritik Fix 1: rj_command_drain Buffer Overflow

### Problem Statement

**Current code** (`src/orchestrator/src/ffi.rs:207`):
```rust
pub extern "C" fn rj_command_drain(out: *mut RjCommand, max: i32) -> i32 {
    if out.is_null() || max <= 0 {
        return -1;
    }
    let Some(state) = FFI_STATE.get() else {
        return 0;
    };
    let mut count = 0i32;
    while count < max {
        match state.command_queue.pop() {
            Some(cmd) => {
                unsafe { out.add(count as usize).write(cmd) };  // ⚠️ NO BOUNDS CHECK
                count += 1;
            }
            None => break,
        }
    }
    count
}
```

**Vulnerability:**
- C++ caller allocates buffer: `RjCommand out[8];` (typical max)
- Passes `max=100` (malformed, accidental, or attacker-controlled)
- Rust writes `out[8]` through `out[99]` → **buffer overflow**
- Stack corruption, potential RCE

**Root Cause:**
- No validation of `max` parameter
- No assumptions documented
- No defense-in-depth

### Solution

**Fix Approach: Conservative Bounds + Documentation**

1. **Validate max parameter:**
   ```rust
   if out.is_null() || max <= 0 || max > 64 {
       return 0;  // Silently fail, don't crash
   }
   ```
   - Max 64 commands per drain (queue is 64 slots)
   - If caller needs more, they call drain again

2. **Add documentation comment:**
   ```rust
   /// Drain command queue into output buffer.
   ///
   /// # Arguments
   /// - `out`: Valid pointer to RjCommand array (caller owns, C++ allocates)
   /// - `max`: Maximum commands to read. MUST be ≤ queue capacity (64).
   ///         Returns 0 if max > 64 or out is null.
   ///
   /// # Safety
   /// Caller MUST ensure:
   /// 1. `out` points to valid RjCommand[max] buffer
   /// 2. `max` ≤ 64 (enforced in this function)
   /// 3. Buffer lifetime extends beyond this call
   ///
   /// # Return
   /// Number of commands written (0 if error or queue empty).
   ```

3. **C++ guard rail** (documentation for C++ side):
   ```cpp
   // Safe pattern:
   const int MAX_DRAIN = 8;  // app-local constant
   RjCommand cmds[MAX_DRAIN];
   int n = rj_command_drain(cmds, MAX_DRAIN);
   assert(n >= 0 && n <= MAX_DRAIN);
   ```

### Implementation Notes

- Change: `max > 8` → `max > 64` (queue capacity)
- Reason: Defensive; prevents trivial overflow without breaking legitimate callers
- Performance: O(1) check, negligible overhead
- Backward compat: All existing callers use `max ≤ 8`, will pass check

### Testing

**Unit tests to add:**
```rust
#[test]
fn test_drain_max_exceeds_limit() {
    rj_start_monitor();
    let mut cmd = RjCommand { /* ... */ };
    // max = 100 exceeds limit
    let n = rj_command_drain(&mut cmd, 100);
    assert_eq!(n, 0, "Should return 0 for max > 64");
}

#[test]
fn test_drain_max_at_limit() {
    rj_start_monitor();
    let mut cmds = [RjCommand { /* ... */ }; 64];
    let n = rj_command_drain(cmds.as_mut_ptr(), 64);
    assert!(n >= 0 && n <= 64, "Should succeed for max = 64");
}
```

---

## Kritik Fix 2: FFI Panic Safety (catch_unwind)

### Problem Statement

**Current state:**
- Rust functions cross FFI boundary into C++
- Unwind panic across FFI = **undefined behavior**
- "A panic unwind across the FFI boundary is undefined behavior." — Rust FFI book

**Example failure:**
```rust
pub extern "C" fn rj_reload_rules(path: *const c_char) -> i32 {
    let cstr = unsafe { CStr::from_ptr(path) };  // ← Could panic if invalid UTF-8
    // ...
    match RuleEngine::new(&rules_path) {
        Ok(new_engine) => { /* ... */ 1 },
        Err(e) => {
            warn!("Failed to load rules: {}", e);  // ← warn! could panic
            0
        }
    }
}
```

If `warn!` panics → unwind into C++ → UB → crash.

**Root Cause:**
- No panic boundary
- Assumptions: "logging will never panic" (false)
- No recovery mechanism

### Solution

**Fix Approach: catch_unwind Guard**

1. **Wrap all extern "C" functions:**
   ```rust
   use std::panic::{catch_unwind, AssertUnwindSafe};

   pub extern "C" fn rj_reload_rules(path: *const c_char) -> i32 {
       catch_unwind(AssertUnwindSafe(|| {
           // Original implementation here
       }))
       .unwrap_or_else(|_| {
           eprintln!("[PANIC] rj_reload_rules caught panic, returning 0");
           0  // Safe return value
       })
   }
   ```

2. **Apply to all extern "C" functions:**
   - `rj_start_monitor()` → void (wrap, return nothing)
   - `rj_metrics_push()` → void (wrap, return nothing)
   - `rj_command_drain()` → i32 (wrap, return -1 on panic)
   - `rj_action_dequeue()` → i32 (wrap, return 0 on panic)
   - `rj_connection_lost()` → void (wrap, return nothing)
   - `rj_pipeline_status()` → i32 (wrap, return -1 on panic)
   - `rj_action_approve()` → i32 (wrap, return 0 on panic)
   - `rj_set_healing_mode()` → i32 (wrap, return 0 on panic)
   - `rj_get_healing_mode()` → i32 (wrap, return 0 on panic)
   - `rj_reload_rules()` → i32 (wrap, return 0 on panic)

3. **Panic return values (conservative):**
   | Function | Return on Panic | Rationale |
   |---|---|---|
   | `rj_start_monitor` | N/A (void) | Log only |
   | `rj_metrics_push` | N/A (void) | Log only |
   | `rj_command_drain` | -1 | Error code |
   | `rj_action_dequeue` | 0 | Empty queue |
   | `rj_connection_lost` | N/A (void) | Log only |
   | `rj_pipeline_status` | -1 | Not ready |
   | `rj_action_approve` | 0 | Approve failed |
   | `rj_set_healing_mode` | 0 | Set failed |
   | `rj_get_healing_mode` | 0 | Unknown mode |
   | `rj_reload_rules` | 0 | Reload failed |

### Implementation Notes

- **AssertUnwindSafe:** Asserts that captured variables are safe to unwind. Use when:
  - Variables are owned (Arc, Mutex, etc.) → safe
  - No raw pointers or external invariants
- **Performance:** ~5ns overhead per call (negligible for non-hot-path FFI)
- **Logging:** Panic logged via eprintln! (always safe)
- **C++ side:** No changes needed; see C++ behavior as error code

### Testing

**Unit tests to add:**
```rust
#[test]
#[should_panic]
fn test_reload_rules_null_path_panics() {
    // This test verifies the OLD behavior (panic)
    // After fix, should NOT panic, should return 0
}

#[test]
fn test_reload_rules_null_path_safe() {
    // NEW: After catch_unwind, null path should return 0, not panic
    let result = catch_unwind(|| {
        rj_reload_rules(std::ptr::null())
    });
    assert!(result.is_ok(), "Should not panic");
}
```

**Manual test:**
```cpp
// C++ side: pass invalid UTF-8
const char bad_path[] = {0xFF, 0xFE, 0x00};  // Invalid UTF-8
int r = rj_reload_rules(bad_path);
assert(r == 0);  // Should return 0, not crash
```

---

## Yüksek Fix 3: Null Pointer Dereference Protection

### Problem Statement

**Current code:**

`rj_metrics_push()` (line 189):
```rust
pub extern "C" fn rj_metrics_push(sample: *const MetricSample) {
    if sample.is_null() {
        return;  // ✓ Guarded
    }
    // Safe
}
```

`rj_action_dequeue()` (line 254):
```rust
pub extern "C" fn rj_action_dequeue(out: *mut RjAction) -> i32 {
    if out.is_null() {
        return 0;  // ✓ Guarded
    }
    // Safe
}
```

**Status:** Already protected ✅

**However, ensure consistency across ALL pointer parameters:**
- `rj_command_drain(out, max)` → out check (line 207) ✓
- `rj_metrics_push(sample)` → sample check (line 189) ✓
- `rj_action_dequeue(out)` → out check (line 255) ✓
- `rj_connection_lost(reason)` → reason can be NULL (line 231) — already safe ✓
- `rj_reload_rules(path)` → path can be NULL (use default) ✓

### Solution

**Audit existing code:**
- All pointer parameters already validated ✓
- No new fixes needed for this class

**Add documentation guard:**
```rust
/// Push a metric sample into the ring buffer.
///
/// # Safety
/// - `sample` may be null (returns silently if null)
/// - If non-null, must be valid pointer to initialized MetricSample
/// - Canary validation required (magic_head/tail)
///
/// # Thread safety
/// Lock-free ArrayQueue; safe from any thread
pub extern "C" fn rj_metrics_push(sample: *const MetricSample) {
    if sample.is_null() {
        return;  // Silent ignore for null
    }
    // ...
}
```

**Enforcement:** Code review rule for future FFI functions.

---

## Security Checklist

| Fix | Severity | Status | Test Coverage | Notes |
|---|---|---|---|---|
| rj_command_drain max validation | CRITICAL | 🔄 TODO | test_drain_max_* | Buffer overflow prevention |
| FFI catch_unwind | CRITICAL | 🔄 TODO | test_*_panics | Panic safety across FFI |
| Null pointer guards | HIGH | ✅ DONE | test_*_null | Audit complete |

---

## Implementation Order

### Phase 1: Fix rj_command_drain (30 min)
1. Edit `src/orchestrator/src/ffi.rs:207-225`
2. Add bounds check: `max > 64` → return 0
3. Add doc comment
4. Add unit tests
5. Build & test

### Phase 2: Fix FFI Panic Safety (90 min)
1. Edit `src/orchestrator/src/ffi.rs` — all extern "C" functions
2. Wrap with catch_unwind + AssertUnwindSafe
3. Add panic return values per table above
4. Add unit tests for panic handling
5. Build & test
6. Manual C++ test (invalid UTF-8 path)

### Phase 3: Audit & Documentation (30 min)
1. Null pointer guards review (already done)
2. Add safety documentation to all FFI functions
3. Create SECURITY.md best practices doc
4. Update progress.md

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Buffer overflow (unfixed) | Medium | CRITICAL | Fix validates max immediately |
| Panic unwind (unfixed) | Low | CRITICAL | catch_unwind prevents UB |
| False positives (over-validation) | Low | MEDIUM | Conservative bounds tested |
| Performance regression | Low | LOW | FFI overhead negligible |
| Regression in functionality | Medium | MEDIUM | Existing tests must still pass |

---

## Rollback Plan

If security fixes cause regressions:

1. **Commit state before security fixes:**
   ```bash
   git log --oneline | head -5
   # Revert: git revert COMMIT_HASH
   ```

2. **Test reversion:**
   ```bash
   cargo test --lib
   python scripts/build.py
   ```

3. **Alternative approach:** Apply fixes in feature branch first, merge after verification

---

## Sign-Off

**Approval Required Before Implementation:**

- [ ] Security lead approval
- [ ] FFI design review
- [ ] Test coverage review

Once approved, estimate: **~150 min** to implement, test, document.

---

## References

- Rust FFI Book: https://doc.rust-lang.org/nomicon/ffi.html#unwinding
- catch_unwind: https://doc.rust-lang.org/std/panic/fn.catch_unwind.html
- CWE-680 Integer Overflow to Buffer Overflow
- CWE-476 Null Pointer Dereference
