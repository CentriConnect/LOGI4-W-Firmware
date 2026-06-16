# LOGI4W Firmware Requirements — REV B + 2026-06-01 Feedback

## Sources
- **LOGI-4W Firmware Update Specification REV B** (2026-01-25, N. Theoret) — see
  `Centri - LOGI-4W - Firmware Update Specification - REV B.docx`.
- **Email exchange with N. Theoret, 2026-06-01** — replies to the bench-findings deliverable
  (`Context/LOGI4W/nick-firmware-findings-response.md`).

## Scope
Captures every change vs current firmware (master @ `v1.1.1`). Supersedes the inferred schema in
the bench-findings deliverable (Nick's REV B keys differ from what was guessed there). Each
requirement has an ID, statement, acceptance criterion, and source. Open questions are at the end.

## Index
1. MQTT Telemetry Topic
2. MQTT Telemetry Payload Schema
3. Device Shadow (Desired-State Fields)
4. Posting Schedule (Appendix A bitmask)
5. Provisioning Mode (updates/conflicts with existing reqs)
6. Successful Provisioning / First-Boot Sequence
7. LED Timing
8. BLE Advertising Name
9. I2C / SHT4x Reliability
10. Out-of-Scope
11. Open Questions

---

## 1. MQTT Telemetry Topic

**REQ-TOPIC-01.** Device shall publish all telemetry to `logi4wifi/device/{DeviceID}` (QoS 1, not
retained).
- *Acceptance:* AWS IoT subscription on `logi4wifi/device/+` receives device JSON; AWS IoT topic
  rule is updated to the new topic in the same cutover.
- *Replaces:* `dt/propane-tank/{DeviceID}/telemetry` at `MQTT/AwsIotConfig.cpp:51`.
- *Source:* REV B §"MQTT Topics".

**REQ-TOPIC-02.** Remove dead/legacy telemetry topic strings so there is one source of truth in the
source tree.
- *Acceptance:* `git grep` finds only `logi4wifi/device/`; the strays `dt/logi/%s/telemetry`
  (`hal/EspAwsIoTClient.cpp:30`) and `AWS_IOT_TELEMETRY_TOPIC` (`MQTT/AwsIotClient.cpp`) are gone or
  redirected.

---

## 2. MQTT Telemetry Payload Schema

**REQ-SCHEMA-01.** Telemetry payload shall use the REV B short-form keys exactly as defined below.
Keys not in this list shall not be included.

| Key | Description | Type | Notes / current code field |
|-----|-------------|------|----------------------------|
| `dev` | DeviceID (32-char UUID) | String | current `DeviceId` |
| `dts` | DeviceDateTimeIso (ISO 8601) | String | current `DateTimeIso` |
| `sch` | SchemaMQTT (schema rev) | String | **NOT** the MQTT protocol version (`3.1.1` is wrong) — see OPEN-Q-6 |
| `ver` | Version | String | example `"3.1,1.2,1.2"` — see OPEN-Q-2 |
| `lsq` | SignalQualWiFi (RSSI) | String | currently `LteSignalQuality` int — switch to WiFi RSSI, **String** per REV B |
| `bat` | BatteryVolts | Float | current `BatteryVolts` |
| `ful` | SensorPercent (tank level %) | Float | current `FuelLevel` |
| `amb` | DeviceTempCelsius | Float | current `TempC` |
| `sol` | SolarVolts | Float | current `SolarVolts` |
| `chg` | ChargerStatus (0=NotChg, 1=Chg) | Integer | current `ChargerStatus` |
| `lat` | Latitude | Float | |
| `lon` | Longitude | Float | |
| `alt` | Altitude | Float | |
| `gsq` | SignalQualGPS | String | current `GpsSignalQuality` |
| `err` | ErrorLog | String | format per OPEN-Q-3 |
| `raw` | SensorVolts (2 decimals) | Float | current `RawLevelCounts` |
| `supv` | SensorSupplyVolts (2 decimals) | Float | current `SupplyVoltage` |
| `psch` | PostingSchedule (8 `HH:MM;DD` strings) | String Array | current `PostingSchedule` |
| `fdt` | FillDwellTime (sec, min 300) | Integer | current `FillDwellTime` |
| `wto` | WiFiTimeout (sec, min 300) | Integer | renames `LteAttemptTimeout` |
| `bleadv` | BleAdvFrequency (ms, min 1000) | Integer | **NEW** |
| `fpd` | FillPostDelta (%, min 10) | Integer | current `FillPostDeltaValue` |
| `pdt` | PostDwellTime (sec, min 60) | Integer | current `PostDwellTime` |

**REQ-SCHEMA-02.** Telemetry payload shall **NOT** include the following (removed in REV B):
`imei`, `iccid`, `mfw`, `btmp`, `lteto`.
- *Acceptance:* payload inspection shows none of these keys.

**REQ-SCHEMA-03.** Float fields `raw` and `supv` shall be serialized to **two decimal places** per
REV B notes.

**REQ-SCHEMA-04.** Cloud-side AWS IoT rule + parser shall be updated to the new short-form keys
**before** firmware is flipped, to avoid dropping data mid-cutover. Coordinated with REQ-TOPIC-01 as
one schema cutover.

---

## 3. Device Shadow (Desired-State Fields)

The cloud shall write desired-state fields; device shall apply within constraints and reject below
minimum.

| Field | Description | Type | Min | Default |
|-------|-------------|------|-----|---------|
| `post_schedule` | 8 slots, `HH:MM;DD` per Appendix A | String[8] | — | one slot `17:00;FF`, rest `00:00;00` |
| `fill_dwell_time` | Dwell time before fill post (sec) | Integer | 300 | 900 |
| `wifi_timeout` | Wi-Fi connect timeout (sec) | Integer | 300 | 2000 |
| `fill_alarm_delta` | Fill-detect Δ% threshold | Integer | 10 | 10 |
| `post_dwell_time` | Post-cycle dwell (sec) | Integer | 60 | 120 |
| `ble_adv_time` | BLE adv interval (ms) | Integer | 1000 | 8000 |

**REQ-SHADOW-01.** Device shall accept shadow updates for all 6 desired-state fields, persist to
NVS, and apply on the next cycle.

**REQ-SHADOW-02.** `wifi_timeout` takes precedence over `post_dwell_time` — device sleeps at the
WiFi Timeout boundary even if PostDwell hasn't elapsed (REV B example).

**REQ-SHADOW-03.** Maximum 8 posting-schedule slots; if user provides fewer, unused slots default
to `00:00;00`.

---

## 4. Posting Schedule (REV B Appendix A — bitmask)

**REQ-SCHED-01.** Posting schedule entry format: `HH:MM;DD` where `DD` is a 1-byte hex bitmask.
- MSB `0x80` = global enable flag for the slot.
- Days (LSB → MSB-1): `0x01` Sun, `0x02` Mon, `0x04` Tue, `0x08` Wed, `0x10` Thu, `0x20` Fri,
  `0x40` Sat.
- Examples: `FF` = enabled, every day; `81` = enabled, Sunday only; `7F` = disabled.

**REQ-SCHED-02.** Cap of 8 daily post slots per device, enforced firmware-side.

---

## 5. Provisioning Mode

These updates supersede / extend the existing `docs/provisioning-mode-requirements.md`.

**REQ-PROV-01 (CHANGE — supersedes existing prov-reqs #7 for the power-cycle case).** On a
hardware **power-on** reset (battery removal/restore), firmware shall erase Wi-Fi credentials from
NVS **before** entering provisioning mode.
- *Acceptance:* battery pull → power on → device advertises in provisioning mode with no stored
  credentials; provisioning re-runs from scratch.
- *Source:* Email 2026-06-01 #4.
- *⚠ Conflict:* existing `provisioning-mode-requirements.md` #7 says "old credentials should not be
  overwritten UNTIL new credentials are received and tested." This new requirement **overrides**
  that for power-on resets. Existing #7 still applies when entering provisioning via shadow reset
  bool or repeated auth failures.
- *See* OPEN-Q-1 (which reset reasons count as "power cycle").

**REQ-PROV-02 (NEW).** During the 48-hour provisioning window, device shall use **light sleep**
with BLE advertising at a **1000 ms** interval (per Nick's 2026-06-01 #6, grounded in the
2026-04-30 power study: ~3.79 mA / ~111 days at 1000 ms light-sleep ADV).
- *Acceptance:* Joulescope shows light-sleep current floor (~3.4 mA) during the prov window; BLE
  scanner finds the device continuously without waiting for an ADV window.
- *Source:* Email 2026-06-01 #6 + power study (sleep-test repo `light-sleep-test` branch).
- *Note:* default `ble_adv_time` shadow value is 8000 ms; during prov mode firmware shall use
  1000 ms unless shadow explicitly overrides (or per OPEN-Q-7).

**REQ-PROV-03 (UNCHANGED).** Normal duty cycle uses **deep sleep** between minute wakes — no BLE
advertising during normal duty cycle (existing prov-reqs #1).

**REQ-PROV-04 (UNCHANGED).** After the 48-hour prov window expires: valid creds → normal duty
cycle; invalid → deep sleep until next reboot (existing prov-reqs #6).

---

## 6. Successful Provisioning / First-Boot Sequence

**REQ-FIRSTBOOT-01 (NEW).** After successful provisioning (creds saved AND AWS broker connected),
firmware shall execute the following sequence **in order**:
1. **POST** telemetry to `logi4wifi/device/{DeviceID}`.
2. **CHECK** for pending AWS IoT Jobs and apply any updates.
3. **GET** device shadow and sync any desired-state fields into local NVS settings.
4. **POST** telemetry **again** with shadow-updated fields reflected in the payload.
5. Enter normal duty cycle (deep sleep until next scheduled wake).
- *Acceptance:* bench test — provision device while watching MQTT logs; observe exactly 2 publishes
  on the telemetry topic, with the jobs handshake and shadow get/update between them; second
  publish reflects any shadow-pushed config changes.
- *Source:* Email 2026-06-01 #7 (matches cellular product behavior).

**REQ-FIRSTBOOT-02.** The same 4-step sequence shall run on the **first boot of a
previously-provisioned device** (replaces the current `very_first_post_complete` RTC-flag
behavior in `ScheduleCheckStateMachine.cpp:15`).
- *Note:* this fixes the bench-finding where a software reboot did **not** force a post (RTC flag
  survived the reboot). After REQ-PROV-01, a power-cycle wipe forces re-provisioning anyway, which
  then triggers REQ-FIRSTBOOT-01 — so the RTC-flag path is no longer needed.

---

## 7. LED Timing

**REQ-LED-01.** "Powered on" LED indication shall be visible within **3 seconds** of power-on
(target: match cellular product's 1–3 s behavior when the solar cell lights up).
- *Acceptance:* stopwatch from battery-connect to first LED blink ≤ 3 s; bench-verify against a
  cellular unit side-by-side.
- *Source:* Email 2026-06-01 #8.
- *Implementation:* move the initial yellow `BlinkLed` call to immediately after
  `LogiHardwareDriver::Initialize()` in `ApplicationStateMachine.cpp` (~line 110) — currently it
  runs at line 234, after the blocking Wi-Fi connect at line 186 (~30 s timeout).

**REQ-LED-02 (UNCHANGED).** The initial blink fires only on `WAKEUP_REASON_RESET` (true power-on /
reset), not on deep-sleep timer wakes. Existing behavior preserved
(`ApplicationStateMachine.cpp:236`).

---

## 8. BLE Advertising Name

**REQ-BLE-01.** BLE advertised name shall be `MyPropane-XXXX`, where `XXXX` is the **first 4 hex
characters of the device's UUID (DeviceID)**.
- Example: DeviceID `412a5ffe-a8e0-4b4f-b50b-505443837674` → BLE name `MyPropane-412a` (or
  `MyPropane-412A`, see OPEN-Q-4 for case).
- *Acceptance:* nRF Connect / phone scan shows `MyPropane-XXXX`; multiple nearby units have
  distinct names matching their respective DeviceIDs.
- *Source:* Email 2026-06-01 #9.
- *Implementation:* replace `LOGI_%02X%02X%02X` (mac bytes 3-5) at
  `ProvisioningStateMachine.cpp:388` with the first 4 hex chars of `DeviceSettings::KEY_DEVICE_ID`.
  The MyPropane phone app scan filter must match.

---

## 9. I2C / SHT4x Reliability

**REQ-I2C-01.** SHT4x temperature/humidity sensor and IS31FL3193 RGB LED (both on I2C bus 0) shall
read/write reliably across deep-sleep wake cycles, including on **battery power** (Nick's failing
bench unit was on battery per Email 2026-06-01 #3).
- *Acceptance:* 100 consecutive wake cycles report valid `amb` (non-zero, non-sentinel) with no
  `I2C transaction timeout` errors in the log; humidity (`rh` is not in REV B but the SHT4x reads
  both channels regardless) returns sane values.
- *Implementation candidates (pick after bench iteration):*
  - Drop I2C clock from 400 kHz → 100 kHz to match the proven-reliable
    `hardware_test.cpp:331` checkout-firmware setting.
  - Call existing `TemperatureSensorSht4x::Reset()` (sends `0x94`) on init + ~2 ms settle.
  - Add bus re-init + 2–3× retry on `ESP_ERR_INVALID_STATE`; never report `0 °C` as a real reading.
- *Gating:* see OPEN-Q-5 (does the PCB have external I2C pull-ups?). Also relevant: ESP32-C6
  hardware I2C reliability under ESP-IDF v5.5.2 was poor enough during PCBA checkout that we used
  software bit-bang there (see `hardware_test.cpp`).

---

## 10. Out-of-Scope (not changing in this revision)

- Kconfig values not flagged in REV B or Nick's email (`LOGI_NO_POST_RESET_TIMEOUT_MS`,
  `LOGI_BLE_INACTIVITY_TIMEOUT_MIN`, `LOGI_FIRMWARE_UPDATE_TIMEOUT_S`, `LOGI_SLEEP_GRACE_PERIOD_S`).
- Factory-provisioning flow (per-board UUID + cert in `esp_secure_cert`) — already implemented on
  local branch `firmware/per-board-identity` (3 unpushed commits: `8c3a61a`, `ebfd2a2`, `5f6121f`).
- Secondary bench-log anomalies that need root-cause investigation rather than a spec change:
  battery-temperature NTC scaling fault (`-273.15 °C` sentinel), un-scaled `SupplyVoltage` /
  `RawLevelCounts`, humidity-prints-uptime-ms log bug. Tracked in project memory `issues.md`
  (ISS-FW-009).

---

## 11. Open Questions for Nick

1. **Reset-reason scope for REQ-PROV-01.** Does the "power cycle wipes NVS" rule apply to all
   `ESP_RST_*` reasons, or only `ESP_RST_POWERON`? Recommend **only** `ESP_RST_POWERON`
   (user-initiated). Brown-out (`ESP_RST_BROWNOUT`) and watchdog (`ESP_RST_TASK_WDT` / `INT_WDT`)
   should **not** wipe — otherwise a battery glitch in the field re-provisions the device.
2. **`ver` field format.** REV B example shows `"3.1,1.2,1.2"`. Is this `HW.minor, SW.major.minor,
   MQTT.major.minor` (matching the existing Kconfig `LOGI_HARDWARE_VERSION_* / SOFTWARE_VERSION_* /
   MQTT_VERSION_*`) or some other grouping?
3. **`err` field format.** ErrorLog is `String` per REV B — comma-separated short error codes? JSON
   array? Free text? Recommend short comma-separated codes for compactness and easy cloud parsing.
4. **`MyPropane-XXXX` casing.** Uppercase `412A` or lowercase `412a`? Recommend lowercase (matches
   UUID convention) — but the MyPropane app scan filter must match either way.
5. **External I2C pull-ups on the v1.1.x PCB.** Decides REQ-I2C-01 fix: external present → can keep
   400 kHz, just stop using internal; absent → either add external (2.2k–4.7k) or drop to 100 kHz.
6. **`sch` field value.** What string does the device send? Spec-revision-style ("B" for REV B),
   semantic-versioned ("1.0"), or something else? Need a schema-version naming scheme.
7. **`bleadv` scope.** REV B defines `ble_adv_time` as a shadow field with default 8000 ms, but
   per REQ-PROV-03 the device does not advertise during normal duty cycle. So `bleadv` only takes
   effect during the 48-hour provisioning window — confirm. And: during prov mode, should device
   honor the shadow value or always use 1000 ms per Email 2026-06-01 #6?

---

## Traceability — bench-findings → requirements

| Bench finding (ISS-FW-…) | New requirement(s) |
|--------------------------|--------------------|
| 001 Topic wrong | REQ-TOPIC-01, REQ-TOPIC-02 |
| 002 Schema wrong | REQ-SCHEMA-01..04, REQ-SHADOW-01..03, REQ-SCHED-01..02 |
| 003 SHT4x I2C | REQ-I2C-01 |
| 004 No factory reset | REQ-PROV-01 (resolved differently — power cycle IS the reset) |
| 005 No post after prov | REQ-FIRSTBOOT-01, REQ-FIRSTBOOT-02 |
| 006 LED 30 s late | REQ-LED-01, REQ-LED-02 |
| 007 BLE name | REQ-BLE-01 |
| 008 Light sleep not in prod | REQ-PROV-02 (light sleep adopted for prov window only) |
| 009 Secondary anomalies | Out-of-Scope §10 (tracked separately) |
| 010 Git: per-board branch unpushed | Out-of-Scope §10 (separate workflow) |
