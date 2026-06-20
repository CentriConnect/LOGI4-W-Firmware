# LOGI4W `err` / FaultFlags — Low-Level Spec + Test Plan

_Draft June 20, 2026. Wires the telemetry **`err`** (string codes) and **`deviceStatus`** (int32 bitmask)
per the proposed scheme. Both fields already exist in `LogiSensorData`/`TelemetryContext` but are blanked
(`errorLog[0]='\0'`). Codes are the proposed default — Nick to lock the final set._

## 1. Goal
Report the device's **faults since the last successful post**. `err` = human-readable comma codes
(`"ADC,GPS"`); `deviceStatus` = the same flags as a bitmask. `deviceStatus` is the source of truth;
`err` is rendered from it, so they always agree.

## 2. Faults module — `main/logi/Faults.h` + `Faults.cpp`
```c
typedef enum {
  FAULT_ADC    = 1u<<0,  // ADS1015 read fail (bat/sol/supply/temp)
  FAULT_AMB    = 1u<<1,  // SHT4x read fail
  FAULT_FUEL   = 1u<<2,  // fuel read fail / 9705 status error
  FAULT_GPS    = 1u<<3,  // no GPS fix inside the acquire window
  FAULT_NTP    = 1u<<4,  // NTP time sync fail
  FAULT_WIFI   = 1u<<5,  // Wi-Fi connect fail
  FAULT_AWS    = 1u<<6,  // AWS connect/publish fail
  FAULT_LOWBAT = 1u<<7,  // battery below threshold
  FAULT_CHG    = 1u<<8,  // charger error
  FAULT_PWR    = 1u<<9,  // power error
} fault_flag_t;

void     Faults_Set(fault_flag_t f);            // atomic OR into the accumulator
uint32_t Faults_Get(void);
void     Faults_Clear(void);
void     Faults_Render(char* err_out, size_t err_len, uint32_t* status_out); // "ADC,GPS" + bitmask
```
- Accumulator: `static volatile uint32_t s_faults` (file static). `Faults_Set` uses `__atomic_or_fetch`
  (failures fire from different tasks — measurement, posting, network).
- **Per-post lifetime**, NOT RTC-persisted: cleared after a good post (§4).
- A `{flag,"CODE"}` table in the `.cpp` drives `Faults_Render` (order = bit order).

## 3. Trigger points — call `Faults_Set(...)` at the EXISTING failure path (no new detection)
| flag | file · function · condition |
|---|---|
| `ADC` | `LogiHardwareDriver::UpdateAdcReadingsAndFilters` — any `_battery/_solar/_batteryTempAdc.GetCounts()` ≠ `HAL_ADC_OK` |
| `AMB` | `LogiHardwareDriver::UpdateI2cReadingsAndFilters` — `_tempSensor.Read()` == false |
| `FUEL` | `LogiHardwareDriver::UpdateAdcReadingsAndFilters` — `_analogLevelSensor.Read()` == false |
| `GPS` | `PostingStateMachine` acquire-final — GPS still invalid when the 120 s window expires |
| `NTP` | Posting + Provisioning SMs — `_timeKeeper->SyncTime()` == false |
| `WIFI`| `EspNetworkManager` — STA connect fail/timeout |
| `AWS` | `AwsIotManager`/`AwsIotClient` — connect or publish returns error |
| `LOWBAT`| post path — `AnalogBatteryVoltage < CONFIG_LOGI_BATTERY_VOLTAGE_POST_THRESHOLD_10X/10` (the currently-commented battery check) |
| `CHG` | post-cycle — `IsChargingErrorActive()` |
| `PWR` | post-cycle — `IsPowerErrorActive()` |

## 4. Telemetry integration
- In **both** context builders (`populateFirstBootTelemetryContext` and the `PostingStateMachine` ctx
  builder) replace `ctx.errorLog[0]='\0'` with:
  ```c
  Faults_Render(ctx.errorLog, sizeof(ctx.errorLog), &ctx.deviceStatus);
  ctx.errorLogValid = true; ctx.deviceStatusValid = true;
  ```
- Serializer (`AwsIotClient`) already emits `err`; add `deviceStatus` to the JSON if not present.
- **Clear:** call `Faults_Clear()` **only after the FINAL post publishes OK** (so the ACK + final of one
  cycle carry the same fault set; the next cycle starts clean). If the publish fails, do NOT clear — the
  faults (incl. `AWS`) ride to the next attempt.

## 5. Per-fault test plan — verify EACH triggers correctly
Verify each via the AWS post (`err`/`deviceStatus` in S3) **or** the serial log line.

| fault | induce on the bench | expect |
|---|---|---|
| `ADC` | unpower/disconnect the ADS1015 (or hold SPS so the bus clamps on a non-R7/R8 board) | `err` has `ADC`, bit0 |
| `AMB` | pull the SHT4x / hold it in reset | `AMB`, bit1 |
| `FUEL`| force `AnalogLevelSensor.Read` fail (fuel ADC uninit / inject) | `FUEL`, bit2 |
| `GPS` | run indoors / no antenna (no fix in 120 s) | `GPS`, bit3 |
| `NTP` | block NTP (firewall UDP/123, or set a bad SNTP host) | `NTP`, bit4 |
| `WIFI`| provision with a wrong Wi-Fi password | `WIFI`, bit5 |
| `AWS` | wrong endpoint / block 8883 (or revoke the cert) | `AWS`, bit6 |
| `LOWBAT`| raise the threshold above actual `bat` (Kconfig/shadow → 5 V) | `LOWBAT`, bit7 |
| `CHG` | drive the charging-error GPIO (IO23) active | `CHG`, bit8 |
| `PWR` | drive the power-error GPIO (IO18) active | `PWR`, bit9 |

**Required checks beyond the per-fault inductions:**
- **Render self-test** (covers all 10 codes + format fast): a debug routine — `FAULT_SELFTEST` build flag
  or a serial cmd — that `Faults_Set`s every flag, `Faults_Render`s, and logs. Pass = string
  `"ADC,AMB,FUEL,GPS,NTP,WIFI,AWS,LOWBAT,CHG,PWR"` and `deviceStatus == 0x3FF`.
- **Clear check:** after a clean post, the **next** post is `"err":""`, `deviceStatus 0`.
- **Multi-fault check:** induce two at once (e.g. no GPS + raised LOWBAT) → `err":"GPS,LOWBAT"`, bits 3+7
  (confirms accumulation + ordering).
- **Fallback for hard-to-induce (`NTP`/`AWS`/`FUEL`):** a temporary `#ifdef FAULT_INJECT` hook that forces
  each *condition* (not just the flag) at its trigger point, so the wiring path is exercised; + code review
  that every failure branch calls `Faults_Set`.

## 6. Decisions / notes
- Thread-safe `Faults_Set` (atomic OR) — failures occur in multiple tasks.
- `deviceStatus` 0x3FF = all 10; leaves bits 10–31 for future codes.
- Implementation order: (1) `Faults` module + render self-test, (2) easy triggers ADC/AMB/FUEL/CHG/PWR,
  (3) connect/timeout triggers NTP/WIFI/AWS/GPS/LOWBAT, (4) integrate render + clear, (5) bench-induce each.
- Lock the code set/format with Nick before shipping.
