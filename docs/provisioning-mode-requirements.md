Hey Nick, just want to run my some of the firmware decisions from our meeting to make sure they are accurate:

# Provisioning Mode Requirements

1. During normal duty cycle (telemetry gather + cloud post), device does not advertise.

2. Device enters provisioning mode on reboot/power cycle, shadow JSON reset bool, wifi credentials no longer valid, wifi credentials missing or corrupt at boot

3. Device enters normal duty cycle only IF credentials are received, AWS connection is established, and NVS credential write succeeds

4. During provisioning mode, the RGB LED blinks blue @ 1Hz, once successful connection established and creds saved, blink green @ 1Hz for 30 mins. Failed credentials keep device in provisioning mode.

5. While phone holds connection, firmware runs a inactivity time out of 5 mins, drops connection, continues advertising

6. After 48hr elapsed provisioning mode, if device has valid credentials, continue normal duty cycle. If not, deep sleep until another reboot occurs.

7. In the case that device has existing NVS wifi credentials and enters provisioning mode, old credentials should not be overwritten UNTIL new credentials are received and tested (shadow reset bool must be cleared before reboot)
