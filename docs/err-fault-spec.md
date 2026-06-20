# LOGI4W Telemetry Faults — `err` / `deviceStatus`

Each post reports the faults seen since the last successful post. `err` = comma-separated
codes (`""` = none); `deviceStatus` = the same flags as a bitmask (`1 << bit`).

| Code | Bit | What it is | Triggered when |
|---|---|---|---|
| `ADC` | 0 | ADS1015 ADC read failed (battery / solar / batt-temp) | any of the three ADS1015 channel reads returns not-OK |
| `AMB` | 1 | Ambient temp/humidity (SHT4x) read failed | the SHT4x read returns false |
| `FUEL` | 2 | Fuel-level read failed | the analog level-sensor read returns false |
| `GPS` | 3 | No GPS fix for this post | no valid fix within the 120 s acquire window on the final post |
| `NTP` | 4 | Clock not time-synced | the SNTP time sync returns false |
| `WIFI` | 5 | Wi-Fi connect failed | the STA connect fails or times out |
| `AWS` | 6 | AWS IoT connect/publish failed | the MQTT connect fails, or a telemetry publish fails |
| `LOWBAT` | 7 | Battery below threshold | battery voltage > 0 and < 3.5 V (`CONFIG_LOGI_BATTERY_VOLTAGE_POST_THRESHOLD_10X`) |
| `PWR` | 9 | Power-supply (load-switch) fault | the SPS_ERROR input (IO18, active-low) reads low |

Bit 8 (`CHG`) is unused — IO23 is a charging-*status* line, not a fault, so it is not reported.
