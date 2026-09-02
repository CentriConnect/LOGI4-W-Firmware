# LOGI4W EOL BLE Beacon Spec

This is the connectionless end-of-line diagnostics burst emitted before normal
WiFi BLE provisioning starts.

## Scan Behavior

- Scan duration: at least 5 seconds.
- Scan mode: active BLE scan required.
- No BLE connection is required.
- Expected advertisement interval: 100 ms.
- Filter on manufacturer data company ID `0xFFFF` and magic bytes.
- The device stops this EOL beacon after the burst and then starts normal
  provisioning. It does not resume EOL beaconing again during that boot.

The scanner should collect both:

- Advertisement manufacturer packet: magic `LW`, measurement values.
- Scan-response manufacturer packet: magic `LI`, programmed device identity.

Use the BLE advertiser address observed during the scan window to associate the
`LW` and `LI` packets.

## Byte Order

All multi-byte integer fields are unsigned little-endian.

Voltage fields are millivolts.

Fuel level is percent x10. Example: `743` means `74.3%`.

## Advertisement Manufacturer Packet: `LW`

Maximum payload length is 23 bytes.

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 2 | Company ID, `0xFFFF` |
| 2 | 2 | Magic, ASCII `LW` |
| 4 | 1 | Schema version, currently `1` |
| 5 | 1 | Flags |
| 6 | 1 | Firmware major |
| 7 | 1 | Firmware minor |
| 8 | 1 | Firmware revision |
| 9 | 2 | Battery voltage, mV |
| 11 | 2 | Solar voltage, mV |
| 13 | 2 | Sensor raw voltage, mV |
| 15 | 2 | Sensor supply voltage, mV |
| 17 | 2 | Fuel level, percent x10 |
| 19 | 2 | Fault bitmask, low 16 bits |
| 21 | 2 | Uptime seconds, wraps at 65535 |

## Scan Response Manufacturer Packet: `LI`

Maximum payload length is 26 bytes.

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 2 | Company ID, `0xFFFF` |
| 2 | 2 | Magic, ASCII `LI` |
| 4 | 1 | Schema version, currently `1` |
| 5 | 1 | Flags |
| 6 | 4 | FNV-1a 32-bit hash of programmed device ID string |
| 10 | 16 | Programmed device UUID encoded as raw bytes |

If the UUID cannot be encoded, the UUID bytes are zero and the UUID encoded flag
is clear.

## Flags

| Bit | Meaning |
| ---: | --- |
| 0 | Programmed device ID is present and marked valid |
| 1 | Battery voltage is valid |
| 2 | Solar voltage is valid |
| 3 | Sensor raw voltage is valid |
| 4 | Sensor supply voltage is valid |
| 5 | Fuel level is valid |
| 6 | UUID bytes are encoded in the `LI` scan response |
| 7 | Reserved |

## Fault Bits

The advertisement carries the lower 16 bits of the firmware fault accumulator.

| Bit | Meaning |
| ---: | --- |
| 0 | ADC fault |
| 1 | Ambient sensor fault |
| 2 | Fuel sensor fault |
| 3 | GPS fault |
| 4 | NTP fault |
| 5 | WiFi fault |
| 6 | AWS fault |
| 7 | Low battery |
| 8 | Charger fault |
| 9 | Power fault |

## Decode Notes

- Treat missing `LW` or missing `LI` after a 5-second active scan as EOL fail or
  retry, depending on station policy.
- Treat schema versions other than `1` as unsupported.
- Reconstruct the UUID by formatting the 16 raw UUID bytes as canonical UUID
  text.
- Pass/fail thresholds should live in the PC tester, not firmware.
- Firmware does not advertise WiFi SSID, WiFi password, AWS endpoint, certs, or
  keys.
