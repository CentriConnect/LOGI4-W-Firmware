# LOGI4W tools

## provision-board.ps1 — factory provisioning

One-board-at-a-time tool that writes per-board identity (AWS IoT Thing UUID + TLS cert/key) into flash, then flashes the shared production firmware on top.

### Prerequisites

- ESP-IDF v5.5.2 installed and `$IDF_PATH` resolvable (the script points at the standard install location; edit `$idfPath` at the top if yours differs).
- Production firmware already built (`build.bat` or `idf.py build`) — the script reuses the existing `build/` artifacts.
- Cert pack laid out per Thing under `$env:LOGI4W_CERTS` (default `C:\Users\michael.zagotta\logi4w-certs`):
  ```
  <Thing-UUID>/
    device.crt        AWS-issued device certificate (PEM)
    private.key       Matching RSA 2048 private key (PEM)
    aws-root-ca.pem   Amazon root CA (typically AmazonRootCA1)
  ```

### Usage

```powershell
# Dry-run — generates partition images under build/per-board/<Thing> without flashing
.\tools\provision-board.ps1 -Thing 412a5ffe-a8e0-4b4f-b50b-505443837674 -DryRun

# Provision a board on COM38
.\tools\provision-board.ps1 -Port COM38 -Thing 412a5ffe-a8e0-4b4f-b50b-505443837674
```

### What it does (order)

1. **NVS factory image** — writes `DeviceCfg/DeviceId` (Thing UUID) and `DeviceCfg/DeviceIdValid` (`0xBAFD`) as NVS blob entries. Generated at `build/per-board/<Thing>/factory_nvs.bin` (24 KB, fills the entire NVS partition).
2. **esp_secure_cert image** — runs `configure_esp_secure_cert.py` in `cust_flash_tlv` mode (no DS peripheral) to pack device cert + private key + CA into a TLV blob. Generated at `build/per-board/<Thing>/esp_secure_cert_data/esp_secure_cert.bin` (8 KB).
3. **Flash partitions** — esptool writes the two images to their fixed offsets:
   - `0xD000`  → `esp_secure_cert` partition
   - `0x13000` → `nvs` partition
4. **Flash shared firmware** — esptool writes the prebuilt bootloader + partition table + ota_data + app from `build/`. Same firmware on every board.

### Verifying a provisioned board

After flashing, watch the serial log (115200 baud). Expected sequence:

```
DeviceSettings Initialized.
AwsIotConfig: AWS IoT topics initialized for Thing: <UUID>
AwsIotClient: Certificates loaded from esp_secure_cert partition (cert=1248 B, key=1696 B)
AWS IoT Manager Initialized.
```

If `DeviceID not provisioned` appears instead, the NVS write didn't land — re-run the script.
If `esp_secure_cert: device cert read failed` appears, the cert partition didn't land.

### Gotchas

- **Wipes existing NVS.** Any stored WiFi creds, last-fuel-level, and provisioning state on the board are lost. Re-running on a deployed board is destructive.
- **No DS peripheral.** The private key sits in encrypted flash (partition table marks `esp_secure_cert` as encrypted) but is not protected by the ESP32-C6 Digital Signature peripheral. Switching to DS would require generating ciphertext+HMAC artifacts at provisioning and burning an eFuse — out of scope for this initial bring-up.
- **No flash encryption enabled yet.** The `encrypted` partition flag only takes effect once flash encryption is enabled in the bootloader. Until then the partition is plaintext. Track this as a hardening follow-up before field deployment.
