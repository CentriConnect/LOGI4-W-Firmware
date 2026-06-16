# LOGI4W REV B — Implementation, Testing & Validation Plan

## Overview
Sequences all 9 changes from `firmware-update-requirements-rev-b.md`. Phase A is firmware-only and
bench-validatable. Phase B is the coordinated topic+schema cutover (firmware + cloud together).
**Every step has a pass/fail gate; do not proceed to the next step until the current one passes.**

## Strategy & sequencing rationale
- **Order is dependency-driven, not requirement-numbered.** I2C reliability (#9) goes early because
  the LED and the sensors share the same bus. BLE name (#8) needs the per-board-identity UUID work.
  Post-sequence (#6) depends on shadow handling (#3). Schema cutover (#1+#2) is last because it
  requires cloud-side coordination and we want all firmware behavior validated first.
- **One commit per step.** Atomic, easy to bisect, easy to revert.
- **Bench-test on battery** (per Nick 2026-06-01 #3 — the failing log was on battery, so reproduce
  there). Bench-supply for sanity comparison only.

## Branching
- **Base branch:** `firmware/per-board-identity` (3 unpushed commits — includes Thing UUID from
  NVS, factory-provisioning script, DeviceSettings first-boot fix). REQ-BLE-01 needs UUID from
  DeviceSettings, which lives on this branch.
- **Working branch:** `firmware/revb-spec-updates` (off `firmware/per-board-identity`).
- **Never push.** Per project rules, feature branches stay local until merged to `master`.

## Pre-implementation gates — answers needed from Nick BEFORE coding
| Gate | Blocks | Recommendation |
|------|--------|----------------|
| PDEC-001 | Step 3 (I2C) | Bench-measure if Nick unsure |
| PDEC-006 | Phase 0 baseline | Confirm flashed build |
| PDEC-007 | Step 4 (power-cycle wipe) | `ESP_RST_POWERON` only |
| PDEC-008 | Phase B (schema) | `ver` field grouping |
| PDEC-009 | Phase B (schema) | `sch` field value |
| PDEC-010 | Step 2 (BLE name) | Case — lowercase recommended |
| PDEC-011 | Phase B (schema) | `err` format |
| PDEC-012 | Step 4 (power-cycle wipe) | Confirm existing prov-reqs #7 still applies for non-power-cycle |
| PDEC-013 | Step 5 (light sleep) | `bleadv` shadow scope during prov |

Phase A Steps 1, 3 (if PDEC-001 known), 5, 6, 7 can start in parallel with Nick's answers; Steps 2,
4 and Phase B are blocked until answered.

---

## Phase 0 — Bench setup & baseline

### Equipment checklist
- Bench unit (LOGI4W PCBA), USB cable, charged 18650 battery in holder.
- Joulescope JS220 (in series on battery + line).
- Phone with **nRF Connect** (BLE scanner) and **MyPropane** test app build.
- MQTT subscriber: AWS IoT MQTT Test Client (AWS Console) **and** a local `mosquitto_sub` pointed
  at the broker for redundancy.
- Serial terminal: `idf.py monitor` (115200 baud).
- Stopwatch / phone timer for LED-blink timing.
- Multimeter for I2C pull-up voltage check.

### Baseline (do once, save logs)
1. Confirm exact firmware build on the bench unit (resolve PDEC-006):
   ```
   idf.py -p <COM> monitor   # capture boot log → look for SW version & git hash if printed
   ```
   - If unit doesn't match a tagged release, flash `release/LOGI4W_v1.1.1_full.bin` to get a known
     baseline. Save the boot log to `bench-baseline/v1.1.1-boot.log`.
2. Capture baseline behavior (save under `bench-baseline/`):
   - Boot serial log (5 minutes)
   - One full provisioning attempt with mobile app, MQTT subscriber capturing topics + payloads
   - Joulescope current trace (60 s) covering one wake-sample-sleep cycle
   - LED-blink stopwatch reading (power-on → first blink)
3. Bench-measure I2C pull-ups (if PDEC-001 still open):
   - Power off the board, unplug battery.
   - Measure resistance from SDA-to-3V3 and SCL-to-3V3 with a multimeter (continuity probe).
     - <10 kΩ → external present (≈2.2k–4.7k)
     - >100 kΩ → no external, internal-only.
   - Alternative: scope SDA at 400 kHz during a normal boot — slow rounded edges = no externals.

### Branch setup
```
git checkout firmware/per-board-identity      # base
git checkout -b firmware/revb-spec-updates    # working branch
```
Confirm working tree clean before each commit (`git status`).

### **PASS GATE 0**
- [ ] Bench unit firmware version known (PDEC-006 answered).
- [ ] Baseline logs/traces saved.
- [ ] Pull-up situation known (PDEC-001 answered or bench-measured).
- [ ] Working branch created and clean.

---

## Phase A — Firmware-only changes

### Step 1 — LED timing (REQ-LED-01)

**Why first:** Easy, low risk, kills the biggest field-support driver (~30 s LED silence on power-on),
independent of other changes.

**Files**
- `main/StateMachines/ApplicationStateMachine.cpp` (Initialize, ~lines 65-280).

**Implementation**
1. Locate the "Power-on LED sequence" block at `:234-254` (currently runs AFTER `_networkManager->Connect`
   at `:186`).
2. Split into two blocks:
   - **Block A — "powered on" indicator:** the yellow `BlinkLed(LedState_YellowBlink, 3, 200, 200)`
     call. Move to immediately after `_logiHardwareDriver->Initialize()` succeeds at `~:110`. Keep
     the `WAKEUP_REASON_RESET` gate.
   - **Block B — "connect result" indicator:** the green/red `BlinkLed` at `:244-250`. Leave at
     `:234-254` after the connect attempt (this is the connection-outcome feedback).
3. No other behavior changes; ESP_LOGI lines for each block (so we can verify ordering in the log).

**Build**
```
idf.py build
```
No new warnings; flash with `idf.py -p <COM> flash monitor`.

**Bench validation**
1. Battery disconnect, wait 10 s.
2. Battery connect, **start stopwatch on click**.
3. Record time-to-first-blink (yellow). Should be **≤ 3 s** (target 1–2 s).
4. Record time-to-result-blink (green or red). Should be ≤ Wi-Fi-timeout + ~1 s (~31 s if Wi-Fi
   times out, ~5–15 s if it connects).
5. Wait through 2 deep-sleep wakes (~2 minutes). Verify **no spurious LED blinks** on wake
   (gate-on-RESET still works).
6. Repeat 3× to confirm consistency.

**Pass criteria**
- [ ] Yellow blink visible within 3 s, 3 trials in a row.
- [ ] Green/red blink occurs at expected time after yellow.
- [ ] Zero LED activity during deep-sleep wakes.
- [ ] No new errors/warnings in serial log.

**Rollback**
`git reset --hard HEAD~1` and re-flash baseline.

**Commit**
```
firmware: move power-on LED blink ahead of Wi-Fi connect (REQ-LED-01)
```

### **PASS GATE 1 — do not proceed to Step 2 until LED timing validated.**

---

### Step 2 — BLE advertising name (REQ-BLE-01)

**Why second:** Trivial, no architectural impact, validates that DeviceSettings UUID is accessible
this early in boot (sanity check on the per-board-identity branch work).

**Blocked on:** PDEC-010 (case).

**Files**
- `main/StateMachines/ProvisioningStateMachine.cpp` (~line 388, the `LOGI_%02X%02X%02X` snprintf).
- `main/logi/DeviceSettings.h/.cpp` (getter for DeviceID UUID — should already exist).

**Implementation**
1. Replace the `LOGI_` snprintf with:
   ```cpp
   const char* uuid = _deviceSettings.getDeviceId();  // confirm exact getter name
   // first 4 hex chars; UUID is 8-4-4-4-12 format, first 4 chars are hex
   char prefix[5] = {0};
   strncpy(prefix, uuid, 4);
   snprintf(serviceName, maxLen, "MyPropane-%s", prefix);
   ```
2. **Edge case:** if UUID is unprovisioned default (`00000000-...`), advertise `MyPropane-0000` and
   log a WARN ("device not factory-provisioned, advertising default name"). Do **not** crash or
   fall back to MAC.
3. Apply case per PDEC-010 (recommended: leave as-is from UUID, which is lowercase hex).

**Build**
```
idf.py build && idf.py -p <COM> flash monitor
```

**Bench validation**
1. Boot device, enter provisioning mode.
2. nRF Connect → scan. Expected name: `MyPropane-XXXX` where XXXX = first 4 of UUID.
3. Verify by comparing to the UUID printed in the boot log.
4. **Multi-unit test (if 2nd unit available):** power on both, verify distinct names.
5. **Mobile app test:** open MyPropane app, scan; confirm the app's scan filter sees the new name.
   **If the app filter is hard-coded to `LOGI_*`, flag immediately — app needs an update before
   Phase A can ship.**

**Pass criteria**
- [ ] Advertised name matches `MyPropane-{first4ofUUID}`.
- [ ] 2 units appear with distinct names.
- [ ] MyPropane app discovers the device.
- [ ] No errors in serial log.

**Rollback**
`git reset --hard HEAD~1`, re-flash.

**Commit**
```
firmware: BLE name MyPropane-XXXX (first 4 of DeviceID) (REQ-BLE-01)
```

### **PASS GATE 2 — must have MyPropane app discovery confirmed before continuing.**

---

### Step 3 — I2C / SHT4x reliability (REQ-I2C-01)

**Why third:** Foundational. LED, SHT4x, and ADC scaling all depend on a healthy I2C bus. Bench
unit was on battery during the failures (Nick #3), which is a tougher case than bench supply.

**Blocked on:** PDEC-001 (external pull-ups Y/N). Implementation branches:
- **Branch A** (externals present): keep 400 kHz, disable internal pull-ups.
- **Branch B** (no externals): drop to 100 kHz **and** keep external-resistor advisory in release
  notes for future PCB rev.

**Files**
- `main/logi/EspLogiHardwareFactory.cpp` (`I2C_FREQ_HZ` at `:31`, bus init at `:84-98`).
- `main/hal/EspI2cMasterConfig.h` (`clk_speed_hz` default at `:34`).
- `main/drivers/TemperatureSensorSht4x.cpp` (Init `:42-50`, Reset `:169`, Measure `:85-110`).
- `main/logi/LogiHardwareDriver.cpp` (the read path that logs "Failed to read Temperature Sensor"
  at `:230`).

**Implementation (Branch B — most likely path)**
1. `EspLogiHardwareFactory.cpp:31` → `I2C_FREQ_HZ = 100000`.
2. `EspI2cMasterConfig.h:34` → default `clk_speed_hz = 100000`.
3. `EspLogiHardwareFactory.cpp:90` → leave `enable_internal_pullup = true` (still need *some* pull-up).
4. `TemperatureSensorSht4x` init: call existing `Reset()` (`0x94`) + `vTaskDelay(pdMS_TO_TICKS(2))`
   immediately after bus is up.
5. Measure error path: wrap `i2c_master_transmit` in a retry loop:
   ```cpp
   esp_err_t err = ESP_OK;
   for (int attempt = 1; attempt <= 3; ++attempt) {
       err = i2c_master_transmit(...);
       if (err == ESP_OK) break;
       if (err == ESP_ERR_INVALID_STATE) {
           // bus stuck — tear down + recreate
           i2c_del_master_bus(_bus); // or equivalent
           recreate_bus_and_device();
           Reset(); vTaskDelay(pdMS_TO_TICKS(2));
       }
       vTaskDelay(pdMS_TO_TICKS(10));
   }
   if (err != ESP_OK) {
       // log and return failure — do NOT report 0°C as real data
   }
   ```
6. In `LogiHardwareDriver.cpp:230`, when SHT4x read fails, **do not** zero the cached temp/hum;
   instead, mark them invalid (e.g. `NAN` or a `valid` flag) and skip emitting them in the next post.

**Build**
```
idf.py build && idf.py -p <COM> flash monitor
```

**Bench validation (this is the most rigorous step — run all of these)**
1. **Bench-supply soak (30 cycles):** plug bench supply, let device run 30 wake-sample-sleep cycles
   (~30 min). Grep serial log for `I2C transaction timeout`, `INVALID_STATE`, `SHT4X.*failed`.
   Expected: **0 hits.**
2. **Battery soak (50 cycles):** swap to battery, run 50 cycles (~50 min). Same grep. Expected:
   **0 hits.** This is the case that was failing.
3. **Sensor sanity:** every cycle's `amb` reading should be 18–28 °C (room) and `rh` 20–70 %.
   Reject any reading outside ±5 °C of bench thermometer.
4. **Cold-start torture (10× battery pull):** power-cycle device 10×, capture first SHT4x read each
   time. All 10 must succeed.
5. **LED still works on the slower bus:** verify yellow blink from Step 1 still appears ≤ 3 s.
6. **Compare to baseline:** before this change, Nick's bench unit showed 100 % failure on the first
   SHT4x read. After, should be 0 % failure.

**Pass criteria**
- [ ] 50/50 successful SHT4x reads on battery, no errors in log.
- [ ] Sensor values within ±2 °C of bench thermometer.
- [ ] 10/10 cold-start reads succeed.
- [ ] LED still blinks ≤ 3 s on power-on (Step 1 not regressed).

**Rollback**
`git reset --hard HEAD~1`. If only the retry logic is bad, retry-loop can be commented out
independently.

**Commit**
```
firmware: I2C 100kHz + SHT4x soft-reset + retry on INVALID_STATE (REQ-I2C-01)
```

### **PASS GATE 3 — 50/50 battery soak is the gate. Anything less, iterate.**

---

### Step 4 — Power cycle wipes NVS credentials (REQ-PROV-01)

**Why fourth:** Behavior change to provisioning entry. Must come before light sleep (Step 5) so
prov mode is correct when we instrument it.

**Blocked on:** PDEC-007 (which reset reasons trigger wipe) **and** PDEC-012 (existing prov-reqs #7
disposition).

**Files**
- `main/main.cpp` (TEST_MODE 2, app_main path around `:1297`).
- `main/StateMachines/ApplicationStateMachine.cpp` (early in Initialize).
- `main/StateMachines/ProvisioningStateMachine.cpp` (`clearStoredCredentials()` at `:422` — already
  exists, just call it).
- `main/hal/EspPowerManager.cpp` (`esp_reset_reason()` already mapped at `:53-78`).

**Implementation**
1. Very early in `ApplicationStateMachine::Initialize()` (before any NVS read or network init):
   ```cpp
   esp_reset_reason_t rr = esp_reset_reason();
   if (rr == ESP_RST_POWERON) {
       ESP_LOGW(TAG, "Power-on reset detected — wiping Wi-Fi credentials (REQ-PROV-01)");
       _provisioningStateMachine->clearStoredCredentials();
   }
   ```
   - Apply same logic for any other reset reasons Nick adds via PDEC-007 (default: POWERON only).
   - **Explicitly exclude:** `ESP_RST_DEEPSLEEP`, `ESP_RST_BROWNOUT`, `ESP_RST_TASK_WDT`,
     `ESP_RST_INT_WDT`, `ESP_RST_PANIC`, `ESP_RST_SW`. A brown-out in the field must NOT wipe.
2. Update `docs/provisioning-mode-requirements.md` #7 to add: "EXCEPTION: on `ESP_RST_POWERON`,
   credentials are erased before entering provisioning mode (REQ-PROV-01)."

**Build & flash**
```
idf.py build && idf.py -p <COM> flash monitor
```

**Bench validation (matrix — all must pass)**
| Reset cause | How to trigger | Expected | Verify by |
|-------------|----------------|----------|-----------|
| Power-on | Battery pull/restore | **Wipes**, enters prov | Log `wiping Wi-Fi credentials`, BLE adv shows MyPropane in prov mode |
| Deep sleep wake | Normal 60s cycle | **Does NOT wipe** | Continues posting with stored creds |
| Software reset | `esp_restart()` via debug shell (or temp button binding) | **Does NOT wipe** | Reconnects to same Wi-Fi |
| Brownout (simulated) | Drop battery voltage briefly with bench supply | **Does NOT wipe** | (Skip if too hard — note as untested) |
| Watchdog | Force WDT via temp `vTaskDelay(60000)` in a debug build | **Does NOT wipe** | Reconnects to same Wi-Fi |
| Panic | Force `abort()` in a debug build | **Does NOT wipe** | Reconnects to same Wi-Fi |

**Pass criteria**
- [ ] All 6 rows of the matrix behave per "Expected" (or marked untested with justification).
- [ ] Power-on after provisioning: device is back in prov mode, app sees MyPropane name.
- [ ] Soft reset after provisioning: device reconnects to same Wi-Fi without re-provisioning.

**Rollback**
`git reset --hard HEAD~1`. If only the matrix is wrong, the `if (rr == ESP_RST_POWERON)` guard is
the only line to revisit.

**Commit**
```
firmware: wipe NVS credentials on ESP_RST_POWERON only (REQ-PROV-01)
```

### **PASS GATE 4 — power-on wipes, all 5 other reset causes preserve. Critical safety property.**

---

### Step 5 — Light sleep during 48 h provisioning window (REQ-PROV-02)

**Why fifth:** Independent of #6 and #7, but should come after #4 so a power-cycle re-prov correctly
enters the new light-sleep mode.

**Blocked on:** PDEC-013 (does device honor shadow `bleadv` during prov, or always 1000 ms).
Recommend: honor shadow value if present (default 1000 ms during prov, vs spec default 8000 ms in
normal contexts).

**Reference:** the sleep-test repo `light-sleep-test` branch — proven port with measured 3.79 mA at
1000 ms ADV. Cherry-pick the relevant config.

**Files**
- `sdkconfig.defaults` (add power-management configs).
- `main/StateMachines/ProvisioningStateMachine.cpp` (start/stop hooks, ADV interval setting).
- `main/hal/EspBluetoothManager.cpp` (NimBLE ADV interval API call).
- `main/hal/EspPowerManager.cpp` (PM lock acquire during normal duty so deep sleep still works there).

**Implementation**
1. Add to `sdkconfig.defaults` (from sleep-test):
   ```
   CONFIG_PM_ENABLE=y
   CONFIG_FREERTOS_USE_TICKLESS_IDLE=y
   CONFIG_PM_POWER_DOWN_CPU_IN_LIGHT_SLEEP=y
   CONFIG_BT_LE_SLEEP_ENABLE=y
   CONFIG_ESP_PHY_MAC_BB_PD=y
   ```
2. In `EspPowerManager`, expose `AllowLightSleep(bool)` that toggles a PM lock:
   - During prov: release the "no light sleep" lock.
   - During normal duty cycle: acquire the lock (forces deep sleep between cycles, unchanged behavior).
3. In `ProvisioningStateMachine::start()`:
   - Read `bleadv` from shadow (default 1000 ms during prov per PDEC-013 recommendation).
   - Call `EspBluetoothManager::setAdvInterval(1000ms)` (convert to NimBLE `min/max_interval` ticks).
   - Call `EspPowerManager::AllowLightSleep(true)`.
4. In `ProvisioningStateMachine::stop()` (success or 48h timeout):
   - Call `EspPowerManager::AllowLightSleep(false)`.
5. Deep-sleep path (`EspPowerManager.cpp:35`) unchanged.

**Build**
```
idf.py reconfigure   # picks up sdkconfig changes
idf.py build && idf.py -p <COM> flash monitor
```

**Bench validation (Joulescope-heavy)**
1. **Prov mode current:**
   - Power cycle to enter prov mode (Step 4 confirmed this works).
   - Joulescope 60 s capture during prov mode.
   - Expected mean: **3.6–4.0 mA** (vs ~10–15 mA at deep sleep with frequent BLE wakes). Match the
     sleep-test measurement of 3.79 mA at 1000 ms ADV ±0.2 mA.
2. **BLE continuous discoverability:**
   - nRF Connect scan during prov: device should appear **immediately**, not after waiting.
   - 10 scan attempts, all 10 must find the device within 2 seconds.
3. **Normal duty cycle current:**
   - Provision the device successfully → enters normal duty.
   - Joulescope 5-minute capture covering multiple deep-sleep cycles.
   - Expected mean during deep sleep: **10–15 µA** (unchanged from baseline). Wake spikes unchanged.
   - **Regression check:** must not have inadvertently kept light sleep enabled during duty.
4. **Prov-to-duty transition:**
   - Provision device, watch Joulescope during the transition.
   - Should see current drop from ~3.8 mA → deep-sleep floor within ~5 s of successful prov.
5. **48 h window expiry:**
   - Skip live test (48 h too long). Instead, temporarily reduce `LOGI_PROVISIONING_TIMEOUT_HOURS`
     in Kconfig to 1 minute for this test build.
   - After 1 min in prov without creds: device should enter deep sleep until reboot
     (existing prov-reqs #6). Joulescope confirms current drops to deep-sleep floor.
   - Restore Kconfig value to 48 h before committing.

**Pass criteria**
- [ ] Prov-mode mean current 3.6–4.0 mA (matches study).
- [ ] BLE found on all 10 quick-scans during prov.
- [ ] Normal duty cycle current unchanged from baseline (regression test).
- [ ] Prov→duty transition observed.
- [ ] 48h-window-expiry behavior unchanged (deep sleep).

**Rollback**
Sdkconfig changes can be reverted; PM lock logic is in EspPowerManager. `git reset --hard HEAD~1`.

**Commit**
```
firmware: light sleep during 48h prov window @ 1000ms ADV (REQ-PROV-02)
```

### **PASS GATE 5 — current measurements must match the study within tolerance.**

---

### Step 6 — Device shadow: 6 desired-state fields (REQ-SHADOW-01..03, REQ-SCHED-01..02)

**Why sixth:** Needed before Step 7 (post-prov sequence) because step 4 of that sequence is
"sync shadow → post again with updated fields." Schedule bitmask format is part of this.

**Files**
- `main/logi/DeviceSettings.h/.cpp` — add `ble_adv_time` (default 8000 ms, min 1000); add
  `wifi_timeout` (rename of `LteAttemptTimeout` internally OR keep internal name, rename only the
  NVS key + shadow key — recommended: keep internal `_lteTimeout` to minimize churn, just rename
  the NVS key and external interface).
- `main/MQTT/ShadowParser.cpp/.h` — extend to parse all 6 fields, with min-value validation +
  rejection logging.
- `main/StateMachines/ScheduleCheckStateMachine.cpp` — confirm Appendix A bitmask interpretation
  matches REV B; the existing schedule code uses `DaysOfWeek` byte already.
- `main/MQTT/AwsIotClient.cpp` — when applying shadow updates, push new `bleadv` into NimBLE.

**Implementation**
1. Add `_bleAdvTime` member + `getBleAdvTime() / setBleAdvTime()` to `DeviceSettings`.
2. NVS key `BleAdvTime` (uint32 ms, default 8000, min 1000).
3. `ShadowParser`: handle `ble_adv_time`; reject < 1000 with WARN. Same pattern for the other 5 fields
   (most already exist).
4. Verify Appendix A bitmask interpretation in `ScheduleCheckStateMachine`:
   - MSB (`0x80`) = enable flag.
   - `0x01..0x40` = Sun..Sat.
   - **Test vectors:** `FF` (every day enabled), `81` (Sun only), `7F` (disabled — MSB clear), `80`
     (no days enabled).
5. After shadow `bleadv` update is applied: if device is currently advertising (prov mode), update
   the live ADV interval (NimBLE `ble_gap_adv_set_data` / restart adv with new params).
6. Persist any shadow-applied changes to NVS before deep sleep so they survive.

**Build**
```
idf.py build && idf.py -p <COM> flash monitor
```

**Bench validation**
1. **Shadow round-trip per field (6 tests):**
   - Use AWS IoT MQTT Test Client to push a desired-state update for one field at a time.
   - Verify in serial log: parsed, validated, stored.
   - Reboot device (deep-sleep wake counts).
   - Verify value persists (log shows loaded value matches what cloud sent).
2. **Min-value validation:**
   - For each field, push a value below minimum (e.g. `wifi_timeout: 100`, min 300).
   - Verify rejected in log, previous value retained, shadow reported state unchanged.
3. **Schedule bitmask test vectors (5 tests):**
   - Push `post_schedule: ["10:00;FF", "00:00;00", ...]` → device posts at 10:00 UTC every day.
   - Push `["10:00;81", ...]` → Sun only.
   - Push `["10:00;02", "10:00;04", ...]` → Mon and Tue separate slots.
   - Push 8 slots — device honors all 8.
   - Push 9 slots — device rejects 9th, accepts 8.
   - Validate by setting `LOGI_SCHEDULE_HOUR_TIMEZONE_OFFSET` to current local + advancing time.
4. **`ble_adv_time` live update:**
   - Enter prov mode (1000 ms ADV from Step 5).
   - Push shadow update `ble_adv_time: 2000`.
   - Verify NimBLE adv interval changes (Joulescope shows shift in adv pulse rate).
   - Joulescope mean current shifts accordingly.
5. **`wifi_timeout` precedence:**
   - Push `wifi_timeout=600, post_dwell_time=60`. Force a slow post (completes at ~580 s).
   - Device must sleep at 600 s, not 640 s. (Verify in log.)

**Pass criteria**
- [ ] All 6 fields round-trip cloud → device → NVS → cloud reported state.
- [ ] Min-value rejection works for all 6.
- [ ] 5 schedule bitmask test vectors all behave correctly.
- [ ] `bleadv` live update changes the ADV interval without reboot.
- [ ] `wifi_timeout` precedence over `post_dwell_time` verified.

**Rollback**
Per-field, can revert specific lines without losing other fields. Full rollback: `git reset --hard HEAD~1`.

**Commit**
```
firmware: shadow desired-state — ble_adv_time + min validation (REQ-SHADOW-01..03, REQ-SCHED-01..02)
```

### **PASS GATE 6 — all 6 fields settable + persisted + live-applied where applicable.**

---

### Step 7 — Post-provisioning sequence: post → jobs → shadow → post (REQ-FIRSTBOOT-01..02)

**Why seventh:** Requires #6 (shadow handling) and #4 (power-cycle wipe semantics) to be working.

**Files**
- `main/StateMachines/ProvisioningStateMachine.cpp` (`ProvisioningStateSuccess` `~:195-240` —
  current path just goes to SuccessDisplay then reboot).
- `main/StateMachines/PostingStateMachine.cpp` (the post call).
- `main/MQTT/AwsIotJobsHandler.cpp` (existing jobs check).
- `main/MQTT/AwsIotManager.cpp` / `AwsIotClient.cpp` (shadow GET).
- `main/StateMachines/ScheduleCheckStateMachine.cpp` (RTC flag `very_first_post_complete` at `:15`
  — gets repurposed or removed).

**Implementation**
1. After `ProvisioningStateSuccess` confirms creds saved AND `AwsIotManager` connection
   established, add a new sub-state `ProvisioningStatePostJobsShadow`:
   1. **POST 1:** call `PostingStateMachine` to publish telemetry. Wait for QoS-1 ACK (existing
      `WAIT_FOR_QOS_ACK` pattern) up to e.g. 10 s.
   2. **JOBS:** call `AwsIotJobsHandler::checkAndApply()` (existing). Wait for completion up to ~30 s.
      Apply any FOTA / config jobs (if FOTA, this reboots — sequence resumes on next boot).
   3. **SHADOW:** publish to `$aws/things/{thing}/shadow/get`, wait for `/accepted` callback, sync
      desired-state fields into local NVS via existing `ShadowParser`.
   4. **POST 2:** call `PostingStateMachine` again. The serializer pulls current NVS values, so the
      payload reflects any shadow-applied updates (matches REV B telemetry which echoes the config
      fields).
   5. Transition to SuccessDisplay (green LED 30 min) → reboot → normal duty cycle.
2. **First-boot path (REQ-FIRSTBOOT-02):** in `WakeStateMachine` / `ScheduleCheckStateMachine`,
   the same sequence runs on `WAKEUP_REASON_RESET` when creds are present (i.e., the device just
   came out of a reset with valid creds — typically post-provisioning reboot).
3. **Remove `very_first_post_complete` RTC flag** — it's superseded:
   - After power cycle: NVS wiped by REQ-PROV-01 → re-prov → REQ-FIRSTBOOT-01 sequence fires.
   - After software reboot of a provisioned device: REQ-FIRSTBOOT-02 sequence fires.
   - On a timer wake mid-day: schedule-only behavior, no force post (unchanged).
4. **Failure handling:**
   - If POST 1 fails: retry up to 3× with backoff. If all fail, log + skip jobs/shadow + go to
     deep sleep (next wake will retry).
   - If JOBS or SHADOW times out: log, proceed to POST 2 anyway (don't block on optional steps).

**Build**
```
idf.py build && idf.py -p <COM> flash monitor
```

**Bench validation**
1. **End-to-end provisioning happy path:**
   - Subscribe to `logi4wifi/device/+` (or current topic if Phase B not yet done) via AWS IoT MQTT
     Test Client AND mosquitto_sub for redundancy.
   - Power cycle → wipes creds → enters prov.
   - Provision via mobile app with valid creds.
   - Capture MQTT output: expect **2 distinct telemetry publishes** in <60 s of successful prov,
     with the device's `$aws/things/{thing}/jobs/...` and `$aws/things/{thing}/shadow/get` traffic
     between them.
2. **Shadow-update reflected in POST 2:**
   - Before provisioning, set desired shadow `fill_dwell_time=600` (≠ default 900).
   - Provision the device.
   - Verify POST 1's payload has `fdt=900` (old/default) or similar.
   - Verify POST 2's payload has `fdt=600` (synced from shadow).
3. **First-boot of provisioned device (REQ-FIRSTBOOT-02):**
   - Boot a device that's already provisioned (no NVS wipe — software reset only).
   - Verify the same 2-publish sequence runs.
4. **Timer-wake mid-day:**
   - Wait through a non-scheduled timer wake.
   - Verify **no force post** (only scheduled or fill-detected post should fire).
5. **POST 1 failure → recovery:**
   - Disable Wi-Fi at AP briefly during POST 1.
   - Verify 3 retries logged, then graceful give-up (no crash), then deep sleep.

**Pass criteria**
- [ ] Provisioning produces exactly 2 telemetry publishes in MQTT subscriber, with jobs+shadow traffic in between.
- [ ] POST 2 payload reflects shadow-updated values.
- [ ] First-boot of provisioned device also runs the sequence.
- [ ] Timer wake mid-day does NOT force a post.
- [ ] POST 1 failure: 3 retries then graceful degradation.

**Rollback**
`git reset --hard HEAD~1`. Restore RTC flag logic if needed (but careful — REQ-PROV-01 then needs
adjustment).

**Commit**
```
firmware: post→jobs→shadow→post sequence after provisioning (REQ-FIRSTBOOT-01..02)
```

### **PASS GATE 7 — happy path + shadow-sync verified. Failure recovery is bonus.**

---

### Phase A integration soak (gate before Phase B)

**Purpose:** confirm no regressions from cumulative Phase A changes; verify 24-hour stability before
touching the cloud cutover.

**Procedure**
1. Fully provisioned device, on battery, Joulescope on battery line.
2. Set a posting schedule with **2 daily posts** (one in the next hour for active test, one
   tomorrow).
3. Let run 24 h.
4. Continuously capture: serial log, MQTT subscriber output, Joulescope mean current.

**Pass criteria**
- [ ] No I2C errors (Step 3 holds under soak).
- [ ] Power cycle mid-soak → wipes creds → re-prov → post sequence → resumes normal duty (Steps 4+7).
- [ ] LED behavior consistent (Step 1).
- [ ] Mean current consistent with deep-sleep model (~14 µA + posting cycles).
- [ ] Scheduled posts happen within ±5 min window per spec.
- [ ] No spurious reboots, panics, or watchdog resets.
- [ ] BLE name still correct after a power-cycle cycle (Step 2).

### **PASS GATE A — Phase A passes 24 h soak before Phase B begins.**

Bump Kconfig version: `LOGI_SOFTWARE_VERSION_MINOR = 2 → 1.2.0` (or whatever scheme PDEC-008 lands
on). Commit:
```
release: bump to v1.2.0 — Phase A REV B updates (LED, BLE name, I2C, power-cycle, light sleep,
shadow, post-sequence)
```

---

## Phase B — Topic + Schema cutover (REQ-TOPIC-01..02, REQ-SCHEMA-01..04)

**Why last:** Cloud-side dependency. Firmware change must land in the same window as the cloud
parser + AWS IoT topic-rule update, or telemetry drops. We want Phase A's improvements live first so
the cutover doesn't conflate firmware-quality issues with schema-cutover issues.

**Blocked on:** PDEC-008 (`ver` format), PDEC-009 (`sch` value), PDEC-011 (`err` format), AND cloud
team's parser/rule update ready.

### Pre-cutover coordination (do this first, days before)
1. Confirm with Nick / cloud owner:
   - Who updates the AWS IoT topic rule (account access).
   - Who updates the downstream parser / dashboard / alerts.
   - Cutover window scheduled (low-traffic time preferred — e.g. weekend morning).
   - Rollback plan agreed (cloud keeps OLD rule active until firmware confirmed; we keep ability to
     re-flash baseline if it goes sideways).
2. Cloud team prep (gate on their confirmation):
   - **Dual-rule:** AWS IoT routes BOTH `dt/propane-tank/+/telemetry` AND `logi4wifi/device/+` to
     the ingestion pipeline (so we can run mixed firmware temporarily).
   - **Parser update:** accepts BOTH long-form (`DeviceId/FuelLevel/...`) AND short-form
     (`dev/ful/...`) keys for the cutover window. Once all units are on new firmware, long-form
     support can be removed.
   - **Dashboard / alerts** updated to read short-form fields.
   - Cloud confirms test event sent to `logi4wifi/device/test123` is received and parsed correctly.

### Firmware implementation
**Files**
- `main/MQTT/AwsIotConfig.cpp:51` — change topic format string to `"logi4wifi/device/%s"`.
- `main/hal/EspAwsIoTClient.cpp:30` — delete `dt/logi/%s/telemetry` (dead).
- `main/MQTT/AwsIotClient.cpp` `AWS_IOT_TELEMETRY_TOPIC` — redirect or remove (if unused after #1
  fix, remove).
- `main/MQTT/AwsIotClient.cpp` `buildPayload`-equivalent (~`:474+`) — rewrite to emit REV B keys
  per REQ-SCHEMA-01 table.
- `main/Kconfig.projbuild` `LOGI_MQTT_VERSION_*` (lines 176-184) — bump to reflect schema cutover
  if PDEC-009 says to use this for `sch`.

**Implementation**
1. **Topic** — one-line change at `AwsIotConfig.cpp:51`.
2. **Schema serializer** — rewrite the cJSON builder block:
   ```cpp
   cJSON_AddStringToObject(root, "dev",  thingName);
   cJSON_AddStringToObject(root, "dts",  isoTimestamp);
   cJSON_AddStringToObject(root, "sch",  schemaVer);    // per PDEC-009
   cJSON_AddStringToObject(root, "ver",  versionStr);   // per PDEC-008
   cJSON_AddStringToObject(root, "lsq",  wifiRssiStr);  // string per REV B
   cJSON_AddNumberToObject(root, "bat",  batteryVolts);
   cJSON_AddNumberToObject(root, "ful",  fuelPercent);
   cJSON_AddNumberToObject(root, "amb",  ambientC);
   cJSON_AddNumberToObject(root, "sol",  solarVolts);
   cJSON_AddNumberToObject(root, "chg",  chargerStatus);
   cJSON_AddNumberToObject(root, "lat",  lat);
   cJSON_AddNumberToObject(root, "lon",  lon);
   cJSON_AddNumberToObject(root, "alt",  alt);
   cJSON_AddStringToObject(root, "gsq",  gpsQualStr);
   cJSON_AddStringToObject(root, "err",  errorLogStr);  // per PDEC-011
   // raw + supv to 2 decimals (use snprintf into a temp buffer with %.2f)
   cJSON_AddNumberToObject(root, "raw",  round100(sensorVolts) / 100.0);
   cJSON_AddNumberToObject(root, "supv", round100(supplyVolts) / 100.0);
   cJSON_AddItemToObject(   root, "psch", buildScheduleArray());
   cJSON_AddNumberToObject(root, "fdt",  fillDwellTime);
   cJSON_AddNumberToObject(root, "wto",  wifiTimeout);
   cJSON_AddNumberToObject(root, "bleadv", bleAdvTime);
   cJSON_AddNumberToObject(root, "fpd",  fillPostDelta);
   cJSON_AddNumberToObject(root, "pdt",  postDwellTime);
   ```
3. **Confirm `imei`/`iccid`/`mfw`/`btmp`/`lteto` are gone** — `git grep` after the change.
4. Bump Kconfig `LOGI_MQTT_VERSION_*` (or whatever PDEC-009 says).

**Build**
```
idf.py build
```
Save artifact: `release/LOGI4W_v1.2.0_full.bin` (or appropriate name).

### Cutover execution (coordinated session)
1. **T-1 hour:** cloud team confirms dual-rule and dual-schema parser are live.
2. **T-0:** flash one bench unit with new firmware.
3. **T+1 min:** verify bench unit posts to `logi4wifi/device/{deviceId}` with new keys.
4. **T+5 min:** verify cloud parser ingests, dashboard shows fresh data, no errors in cloud logs.
5. **T+15 min:** repeat with a 2nd unit if available, to confirm not unit-specific.
6. **T+1 hour:** if all green, gradually roll to field units (controlled by FOTA — separate plan).
7. **Post-cutover** (once all units migrated): cloud team removes the old `dt/propane-tank/` topic
   rule and long-form key support.

### Bench validation
1. **Topic correctness:** subscribe to `logi4wifi/device/+`; new firmware publishes there.
2. **Schema correctness — every REV B field present:**
   - Capture one publish; parse JSON.
   - Verify all 23 keys from the REQ-SCHEMA-01 table are present (or omitted per the REV B "may be
     omitted if not applicable" rule for the right cases).
   - Verify **none** of `imei`/`iccid`/`mfw`/`btmp`/`lteto` appear.
3. **Numeric precision:** `raw` and `supv` are 2 decimals (e.g. `3.41`, not `3.4105782`).
4. **Field types match REV B:** `lsq` is String (not Int as before), `chg` is Integer, etc.
5. **Cloud parser sanity:** check the AWS IoT rule logs — no parsing errors, dashboard fields
   populated.
6. **Old topic absent:** `git grep dt/propane-tank` returns nothing in `main/`.

**Pass criteria**
- [ ] Bench unit publishes to `logi4wifi/device/{deviceId}` exclusively.
- [ ] Payload includes all expected REV B keys, no removed keys.
- [ ] Cloud parser accepts payload without errors; dashboard fresh.
- [ ] Field types and precisions match REV B.
- [ ] Old topic strings removed from source.

**Rollback (if cutover goes wrong)**
- Cloud team: keep the old `dt/propane-tank/` rule live (we never removed it pre-cutover).
- Firmware: re-flash `release/LOGI4W_v1.1.1_full.bin` to bench unit.
- Field units: if FOTA already pushed → push v1.1.1 back via FOTA (separate procedure).
- Post-mortem before retry.

**Commit**
```
firmware: REV B schema cutover — topic + short-form keys (REQ-TOPIC-01..02, REQ-SCHEMA-01..04)
```

### **PASS GATE B — bench unit verified end-to-end against the cloud before field rollout.**

---

## Phase C — Release

1. **Final version bump:** `LOGI_SOFTWARE_VERSION_*` → e.g. `v1.2.0`. Commit on its own.
2. **Build release artifacts:**
   ```
   idf.py build
   cp build/LOGI4W.bin release/LOGI4W_v1.2.0_app.bin
   # repeat for bootloader, partition-table, ota_data_initial, full
   ```
3. **Release notes** (markdown source + branded PDF per `feedback_github_releases` memory):
   - REQ-IDs implemented, traceability to ISS-FW-001..010.
   - Behavior changes for installers (power cycle now wipes! — make this prominent).
   - Cloud-side dependencies (topic + schema).
   - Known issues, open questions remaining.
4. **Resolve docs conflict** (PDEC-012): update `docs/provisioning-mode-requirements.md` #7 to
   reference REQ-PROV-01 as the power-cycle exception.
5. **Merge to master:**
   ```
   git checkout master
   git merge --no-ff firmware/revb-spec-updates -m "Merge REV B spec updates (v1.2.0)"
   git tag -a v1.2.0 -m "REV B spec updates"
   ```
6. **Push master + tag to origin** (only `master`, never feature branches):
   ```
   git push origin master
   git push origin v1.2.0
   ```
7. **GitHub release** (`gh release create v1.2.0 ...`) with the branded PDF attached.
8. **Update project memory:** `state.md` → Phase Complete; `changelog.md` → v1.2.0 entry;
   `issues.md` → close ISS-FW-001..008, leave 009 (secondary anomalies) open.

---

## Overall rollback strategy

| Failure point | Action |
|---------------|--------|
| Step N bench fails | Revert Step N commit only; iterate or escalate |
| Phase A 24h soak fails | Bisect Phase A commits; revert the offending one |
| Phase B cutover fails | Cloud keeps old rule live; re-flash v1.1.1; post-mortem |
| Field FOTA fails (post-release) | Separate FOTA rollback plan; v1.1.1 binary kept on AWS |

---

## Tracking
- Each gate passing → tick the corresponding `[ ]` checkbox in this doc, commit, push to local
  branch only.
- New issues found during implementation → append to project memory `issues.md` (ISS-FW-011+).
- Decisions resolved during implementation → move from `pending-decisions.md` to `decisions.md`.

## Risks & contingencies
- **PDEC-007 unanswered too long:** can start Step 4 with the recommended (POWERON-only) behavior
  and revisit if Nick disagrees later. Low risk — easy to widen the reset-reason set than to narrow it.
- **Mobile app scan filter hard-coded to `LOGI_*`:** Step 2 unblocks Phase A locally, but the app
  update is gating for field rollout. Coordinate with app team early.
- **Cloud team not ready for Phase B cutover window:** Phase A can release as v1.2.0-alpha (no
  topic/schema change) so the bench/installer wins are not blocked on cloud.
- **Battery brown-out wipes NVS during a power glitch in the field:** PDEC-007 must be answered as
  POWERON-only or this becomes a field reliability issue.
- **24 h soak reveals a regression late in Phase A:** built-in to the plan (Gate A). Better to find
  it on the bench than in the field.
