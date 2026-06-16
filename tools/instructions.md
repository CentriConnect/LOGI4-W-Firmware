# LOGI4W — Build + Flash + Provision (Codex runbook)

**Audience:** an automated agent (Codex) running on the build machine.
**Goal:** with one command, build the LOGI4W firmware from source and flash a
connected ESP32-C6 board with **its AWS certificates + DeviceID + the firmware**,
then report a clear PASS/FAIL for every step. Works on a blank ("fresh") board
and on re-provisioning an existing one.

Read this whole file before running anything. The final section
("**Agent directives**") tells you exactly what to report and when to stop and
ask for help.

---

## 1. Files in this zip
| File | Purpose |
|---|---|
| `build-and-provision.ps1` | the driver script (the only thing you run) |
| `verify-ble.py` | stage-5 BLE check helper (called by the script) |
| `instructions.md` | this runbook |

> The zip contains **no secrets**. The private keys live only in the cert packs
> on the build machine (see §3.4) and are never copied into this zip or any
> public location.

---

## 2. What the script does (5 stages)
| # | Stage | Pass criterion |
|---|---|---|
| 1 | **VALIDATE** | ESP-IDF, esptool, and the cert pack are all present |
| 2 | **BUILD** | `idf.py build` succeeds and 4 firmware bins exist |
| 3 | **GENERATE** | `esp_secure_cert.bin` + `factory_nvs.bin` are produced |
| 4 | **FLASH** | all 6 flash regions written, each `Hash of data verified` |
| 5 | **VERIFY** | board advertises `MyPropane-<short>` over BLE (non-fatal) |

Flash region map (offsets are fixed by the partition table — do not change):

| Offset | Region | Source |
|---|---|---|
| `0xD000` | esp_secure_cert (this board's AWS certs) | generated in stage 3 |
| `0x13000` | nvs (this board's DeviceID) | generated in stage 3 |
| `0x0` | bootloader | built in stage 2 |
| `0x8000` | partition-table | built in stage 2 |
| `0x19000` | ota_data_initial | built in stage 2 |
| `0x20000` | application | built in stage 2 |

`<short>` = the first 4 hex characters of the Thing UUID (e.g. Thing
`ed60838a-…` → BLE name `MyPropane-ed60`). This is the live proof the DeviceID
took.

---

## 3. Prerequisites (verify these before running)
### 3.1 Repository
The full **LOGI4W-production** repo must be cloned locally. The build needs the
entire source tree; this is not a standalone build.

### 3.2 ESP-IDF v5.5.2
Installed and discoverable. Default path:
`C:\Users\michael.zagotta\esp\v5.5.2\esp-idf`.
If it lives elsewhere, set it before running:
```powershell
$env:IDF_PATH = 'D:\path\to\esp-idf'
```
Check it exists: the file `<IDF_PATH>\export.bat` must be present.

### 3.3 Python + tools
- `python --version` → 3.8+ on PATH.
- `python -m esptool version` → any 4.x or 5.x (the script flashes with this).
- `pip install bleak` → needed only for the stage-5 BLE check (optional; a
  missing `bleak` makes stage 5 WARN, not FAIL).

### 3.4 Cert pack (per board)
The board's identity files must already exist at `<CertDir>\<Thing>\`:
```
<CertDir>\<thing-uuid>\device.crt
<CertDir>\<thing-uuid>\private.key
<CertDir>\<thing-uuid>\aws-root-ca.pem
```
Default `CertDir` = `C:\Users\michael.zagotta\logi4w-certs`
(override with `$env:LOGI4W_CERTS`). These come from Nick / CentriConnect
(created in AWS IoT) — this tool only **consumes** them; it never mints keys.
**Never** copy these files to a public repo or release.

### 3.5 Hardware
- The ESP32-C6 board connected with a **USB cable that has data lines** (many
  cables are power-only and will not enumerate a COM port).
- Power present (battery or bench supply).

---

## 4. Installation
Copy **both** `build-and-provision.ps1` and `verify-ble.py` into the repo's
`tools\` folder, next to the existing `provision-board.ps1`. They must sit in
the same folder (the script finds `provision-board.ps1` and `verify-ble.py`
beside itself).
```powershell
Copy-Item .\build-and-provision.ps1 <repo>\tools\ -Force
Copy-Item .\verify-ble.py          <repo>\tools\ -Force
```

---

## 5. Running it
From the repo root:
```powershell
cd <repo>
.\tools\build-and-provision.ps1 -Thing <thing-uuid> [-Port COMxx] [-Clean] [-FlashTimeoutSec 120] [-SkipVerify]
```

| Argument | Required | Meaning |
|---|---|---|
| `-Thing` | **yes** | AWS IoT Thing UUID; must match a folder in `CertDir` |
| `-Port` | no | COM port (e.g. `COM39`). If omitted, auto-detects one ESP32-C6 (VID 303A); errors if 0 or >1 found |
| `-CertDir` | no | Root of per-Thing cert folders (default in §3.4) |
| `-Clean` | no | From-scratch build (`fullclean` + `set-target esp32c6`) instead of incremental. Use on a fresh clone or if the build behaves oddly |
| `-FlashTimeoutSec` | no | How long stage 4 keeps trying to catch a flash window (default `120`) |
| `-SkipVerify` | no | Skip the stage-5 BLE check |

**Worked example:**
```powershell
.\tools\build-and-provision.ps1 -Thing ed60838a-24cb-42a0-b358-670984739837 -Port COM39
```

---

## 6. Stage-by-stage: expected output, pass/fail, what to do on failure

### Stage 1 — VALIDATE
- **Expect:** `[STEP 1/5] Validate inputs : PASS`.
- **FAIL causes:** ESP-IDF path wrong (set `$env:IDF_PATH`); cert pack missing a
  file (put all 3 files in `<CertDir>\<Thing>\`); esptool not installed
  (`pip install esptool`). **Exit code 1.**

### Stage 2 — BUILD
- **Expect:** ESP-IDF activates, ninja builds, then
  `[STEP 2/5] Build firmware : PASS`. A "smallest app partition is nearly full
  (2% free)" warning is **normal**, not a failure.
- **FAIL causes:** compile error, or `set-target` needed on a fresh clone — retry
  with `-Clean`. **Exit code 2.**

### Stage 3 — GENERATE
- **Expect:** `[1/4] Generating NVS…`, `[2/4] Generating esp_secure_cert…`, then
  `[STEP 3/5] Generate images : PASS`. The "private shall be stored as plaintext"
  line is **expected** (no DS peripheral).
- **FAIL causes:** cert files malformed. **Exit code 3.**

### Stage 4 — FLASH (the one that depends on the board being awake)
- **Expect:** `COM## openable (boot window) - flashing all 6 regions...`, then six
  blocks each ending `Hash of data verified.`, then
  `[STEP 4/5] Flash : PASS`.
- **Behaviour while waiting:** the script prints status as it polls — e.g.
  `present but not openable (light sleep)` or `board not present (deep sleep…)`.
  This is the catch-and-flash loop doing its job (see §7).
- **FAIL cause:** no flash window caught within `-FlashTimeoutSec`. The board is
  asleep and needs a power cycle. **Exit code 4.** → See §7 and the Agent
  directives.

### Stage 5 — VERIFY (BLE, non-fatal)
- **Expect:** after a ~15 s boot wait it scans up to 3×; on success
  `[STEP 5/5] Verify BLE : PASS` with the RSSI.
- **WARN (not FAIL):** `MyPropane-<short> not seen after 3 scans`. The flash
  already PASSED (hashes verified); a WARN here usually means a BLE adapter/range
  issue or the board hadn't finished booting. The overall RESULT can still be
  PASS.

---

## 7. How it handles a board going in and out of sleep
This is the most important operational detail.

A LOGI4W board in **light sleep** still enumerates a COM port, **but every
attempt to open that port fails** ("device attached to the system is not
functioning"). In **deep sleep** the port disappears entirely. The key fact the
script relies on: **a port that opens successfully means the chip just booted —
i.e. you are inside the ~15 s flash window right now.**

Stage 4 is a **catch-and-flash** loop:
1. Poll for the port. If it is **absent** (deep sleep) or **present-but-unopenable**
   (light sleep), it waits and keeps polling, printing the state.
2. The moment the port becomes **openable**, it fires esptool immediately and
   writes all 6 regions in a single pass (one connect = best fit for the short
   window).
3. If a flash attempt **fails against a running app** (port opens but the chip
   refuses the reset), it **disarms** until the board next reboots/sleeps, so it
   does not spam useless retries.
4. After `-FlashTimeoutSec` with no successful window, it **FAILs** with a clear
   message and exit code 4.

**The script cannot power-cycle the board itself.** If a board is genuinely
asleep, a human must pull power (battery/USB) for ~3 seconds and reconnect; the
loop then catches the boot window automatically. To avoid the issue entirely,
keep the board on **USB** — firmware v1.2.1+ stays awake while a USB host is
attached, so it is reflashable at any time. A **fresh/blank** board has no app to
sleep and is always openable.

---

## 8. Reading the result (for automation)
- Every stage prints `[STEP n/5] <name> : PASS | WARN | FAIL`.
- A `SUMMARY` block lists all five stages, followed by a single
  `RESULT: PASS` or `RESULT: FAIL` line.
- **Exit code:** `0` = success (a stage-5 WARN is still success); otherwise the
  exit code equals the failing stage number (`1` validate, `2` build,
  `3` generate, `4` flash).

---

## 9. Troubleshooting quick reference
| Symptom | Cause / fix |
|---|---|
| `No serial data received` / can't connect | window missed or board asleep → power-cycle and re-run, or raise `-FlashTimeoutSec` |
| `Could not open COM##… not functioning` | board in light sleep → power-cycle to open a boot window |
| Port present but silent after flashing | a terminal asserting DTR/RTS latched the chip into ROM download → power-cycle to recover |
| `Multiple ESP32-C6 ports found` | pass `-Port COMxx` explicitly |
| Build fails on a fresh clone | run with `-Clean` (does `set-target esp32c6` first) |
| Stage 5 WARN, everything else PASS | flash succeeded; BLE scan just didn't catch the beacon — re-scan or ignore |

---

## 10. Agent directives (Codex — do this)
1. **Run the script once** with the `-Thing` (and `-Port` if known) provided to
   you. Stream its output.
2. **At the end, report a concise, high-level status of every operation** — not a
   raw log dump. Use this shape:
   - **Validate:** PASS/FAIL
   - **Build firmware:** PASS/FAIL
   - **Generate certs + DeviceID:** PASS/FAIL
   - **Flash (6 regions):** PASS/FAIL
   - **BLE identity (`MyPropane-<short>`):** PASS/WARN
   - **Overall:** `RESULT: PASS|FAIL` (exit code N), plus one plain-English
     sentence (e.g. "Board built, flashed, and verified as MyPropane-ed60.").
3. **If stage 4 (Flash) FAILs because no flash window was caught (exit code 4):**
   do **not** loop or blindly retry. **Stop and ask the operator to power-cycle
   the board** — "Please unplug the board's power (USB/battery) for ~3 seconds,
   reconnect it, and tell me when it's done" — then re-run the same command once
   they confirm. Mention that keeping the board on USB (firmware v1.2.1+) avoids
   this because it stays awake.
4. **If any other stage FAILs (exit 1/2/3):** report which stage, quote the
   failure note the script printed, and apply the matching fix from §9 (e.g.
   `-Clean` for build, fix the cert pack for validate). Do not proceed past a
   failed stage.
5. **If stage 5 WARNs but RESULT is PASS:** report success, and note the BLE
   check did not see the beacon this run (the flash itself passed — hashes
   verified). Offer to re-scan.
6. **Never** put cert files, `esp_secure_cert.bin`, `factory_nvs.bin`, or the
   per-board build output into any public repo, release, or chat attachment.
