# SI4732 Chip Code Guide

This guide points out the code that communicates with the SI4732 receiver,
what each part does, and where the two apps call it. The main implementation
is in [si4732_minimal.c](./si4732_minimal.c), with declarations and constants
in [si4732_minimal.h](./si4732_minimal.h).

## Quick map

- **Physical connection / I2C transport:** `si4732_minimal.c`, bit-banged
  I2C helpers and GPIO register access.
- **Chip commands and properties:** `si4732_minimal.c`, `send_cmd`,
  `read_response`, and `set_property`.
- **App-facing receiver API:** `si4732_minimal.h` and the public
  `SI4732_*` functions in `si4732_minimal.c`.
- **AM app usage:** [hf_app.c](./hf_app.c).
- **AM/SSB app usage:** [hf_ssb_app.c](./hf_ssb_app.c).
- **UI frequency formatting is not chip communication.** The apps store and
  tune frequencies in whole kHz; converting them to text such as `7.000 MHz`
  only changes what is drawn on screen.

## Hardware and low-level I2C

The driver uses software/bit-banged I2C on the PY32F071 GPIOA pins:

- `SI4732_ADDR` is `0x11` when the chip's SEN pin is low. The I2C transport
  shifts this 7-bit address left and adds the read/write bit.
- SCL is PA10 and SDA is PA11, configured in `si4732_minimal.h`.
- `SI4732_I2C_Init()` configures the GPIO pins for open-drain-style signaling,
  enables pull-ups, and releases both lines.
- `pin_input()` releases a pin so the line can go high; `pin_output()` drives
  it low. `scl_high/low()` and `sda_high/low()` wrap those operations.
- `sda_read()` samples the SDA input register.
- `i2c_delay()` is the short timing delay used by the bit-banged bus.
- `i2c_start()` and `i2c_stop()` produce I2C start and stop conditions.
- `i2c_write_bit()` and `i2c_read_bit()` transfer one data bit.
- `i2c_write_byte()` and `i2c_read_byte()` transfer a byte and handle the
  I2C ACK/NACK bit.

These are private driver helpers. App code should use the public `SI4732_*`
API instead of calling them directly.

## Command transport helpers

All chip commands go through a small shared command layer:

- `wait_cts()` sends `CMD_GET_INT_STATUS` (`0x14`) and polls the status byte
  until the chip reports CTS (command clear to send). It stops on the chip's
  error bit or after its retry limit.
- `send_cmd(cmd, len)` waits for CTS, sends the SI4732 write address, then
  writes the command bytes. It returns false for a missing ACK or failed
  readiness check.
- `read_response(buf, len)` waits for CTS, reads response bytes, ACKs all but
  the last byte, and returns false if the read address is not acknowledged.
- `set_property(property, value)` constructs the six-byte
  `CMD_SET_PROPERTY` (`0x12`) command and sends it using `send_cmd()`.

The command constants and property IDs are in `si4732_minimal.h`. In
particular, the AM tune command is `CMD_AM_TUNE_FREQ` (`0x40`), the signal
quality command is `CMD_AM_RSQ_STATUS` (`0x43`), and the volume property is
`PROP_RX_VOLUME` (`0x4000`).

## Public driver functions

| Function | What it does | Used by |
| --- | --- | --- |
| `SI4732_I2C_Init()` | Initializes the PA10/PA11 software I2C lines. | `SI4732_Init()` |
| `SI4732_Init()` | Initializes I2C, powers up the chip in AM with analog audio and crystal settings, applies AM soft-mute and AVC properties, sets maximum volume, and returns success/failure. | Both apps at startup; SSB app also uses it to recover after patch-load failure. |
| `SI4732_PowerDown()` | Sends `CMD_POWER_DOWN` (`0x11`). | Both apps at shutdown; SSB app uses it around SSB patch loading/recovery. |
| `SI4732_SetFrequency(kHz)` | Validates 150–30000 kHz, builds the AM-tune command with the integer kHz value and both antenna-capacitor bytes set to auto, and sends it. In SSB builds, it adds USB/LSB tune flags according to the current mode. Returns false on invalid frequency or communication failure. | `hf_app.c`'s `tune()` and `hf_ssb_app.c`'s `tune()`. |
| `SI4732_SetVolume(vol)` | Clamps volume to 0–63 and writes `PROP_RX_VOLUME`. | Both apps when audio is enabled/muted; also called during initialization. |
| `SI4732_GetRSSI()` | Requests AM/SSB receive-signal-quality status and returns the RSSI response byte. A communication failure returns zero. | Both apps for signal display; AM app also uses RSSI for scan stop decisions. |
| `SI4732_GetSNR()` | Uses the same status response and returns its SNR byte. | Public API, but not currently called by either app. |
| `SI4732_SetMode(mode)` | In AM-only builds, accepts only AM. In SSB builds, selects AM, USB, or LSB; USB/LSB require the SSB mode property. | Standalone SSB app when cycling modes. |
| `SI4732_SetBFO(hz)` | In non-AM-only builds, writes the SSB BFO property when currently in USB/LSB. | Available API, but not currently called by either app. |
| `SI4732_LoadSsbPatch(api)` | Finds the five installed patch-carrier assets, powers up in patch-loading mode, sends patch records to the chip, waits for completion, then selects SSB mode. Returns false if assets, API services, or chip communication are invalid. | Standalone SSB app when switching from AM into USB. |

### Initialization details

`SI4732_Init()` sends `CMD_POWER_UP` with AM, analog audio, and crystal
configuration, then sets:

- `PROP_AM_SOFT_MUTE_MAX_ATTENUATION` to zero (disables AM soft-mute
  attenuation).
- `PROP_AM_AVC_MAX_GAIN` to `AM_AVC_MAX_GAIN`.
- `PROP_RX_VOLUME` to the maximum supported volume (63).

It does not tune a frequency itself. After successful initialization, each
app calls its own `tune()` helper, which calls `SI4732_SetFrequency()`.

### Frequency units

`SI4732_SetFrequency()` accepts **whole kilohertz** (`uint16_t kHz`) and
encodes that number into the chip's tune command. For example, 7000 means
7000 kHz (7 MHz). The decimal-MHz display is just presentation; it does not
change the chip command or the frequency stored by the app.

## SSB patch-specific code

`SI4732_LoadSsbPatch()` uses resident firmware storage services in addition
to I2C:

- `findPatchParts()` scans app slots for committed packages named
  `SSBPATCH0` through `SSBPATCH4`, checks each asset's expected length, and
  records each asset address.
- Patch bytes are read through `api->sys_storage_read()`, in 8-byte records.
- `CMD_POWER_UP` is sent in patch-loading mode before the records.
- Each patch record is transferred with `send_cmd()`. The first record must
  start with `0x15`; subsequent records may start with `0x15` or `0x16`.
- On transfer failure, the driver powers down and attempts to reinitialize
  AM before returning false.
- On success, it waits for CTS and sets the SSB mode property.

This explains the companion apps: install `HFSSB.app` together with all five
`SSBPATCH*.app` packages. Compilation alone does not confirm that patch
loading or SSB reception works on a physical radio.

## How the apps use the driver

### AM receiver: `hf_app.c`

- `app_main()` calls `SI4732_Init()` once on startup.
- Its local `tune()` calls `SI4732_SetFrequency(g.freq_kHz)`.
- Frequency entry, memory/preset selection, tuning steps, and scanning all
  update `g.freq_kHz` and then call `tune()`.
- `draw()` calls `SI4732_GetRSSI()` to show the signal reading and bars.
- `scanStep()` calls `SI4732_GetRSSI()` to determine whether to keep scanning.
- `toggleAudio()` calls `SI4732_SetVolume()` with either 63 or 0.
- On exit, `app_main()` calls `SI4732_PowerDown()`.

### Standalone SSB receiver: `hf_ssb_app.c`

- `app_main()` initializes the chip and calls its local `tune()`.
- Its `tune()` calls `SI4732_SetFrequency(g.frequency)`.
- `draw()` calls `SI4732_GetRSSI()` for its signal display.
- `toggleAudio()` calls `SI4732_SetVolume()` with either 63 or 0.
- `changeMode()` loads the patch with `SI4732_LoadSsbPatch()` before entering
  USB, then uses `SI4732_SetMode()` to switch between USB, LSB, and AM.
- On patch-load failure it powers down and reinitializes AM.
- On app exit it calls `SI4732_PowerDown()`.

## Constants that describe the chip

Defined in `si4732_minimal.h`:

- `SI4732_MIN_FREQUENCY_KHZ` / `SI4732_MAX_FREQUENCY_KHZ`: 150 and 30000.
- `SI4732_MAX_VOLUME`: 63.
- `PROP_RX_VOLUME`: receive volume property (`0x4000`).
- `PROP_AM_SOFT_MUTE_MAX_ATTENUATION`: AM soft-mute property (`0x3302`).
- `PROP_AM_AVC_MAX_GAIN`: AM AVC property (`0x3103`).
- `PROP_SSB_BFO` / `PROP_SSB_MODE`: SSB BFO and mode properties.
- `SI_MODE_AM`, `SI_MODE_USB`, `SI_MODE_LSB`: driver mode values.

The header also declares a `si_bands[]` example table. The current UI does
not use this table for tuning.

## Important limitation

The driver accesses PY32F071 GPIO registers directly and uses short
software-timed I2C delays. A build passing does not prove that the selected
board pins, pull-ups, wiring, audio path, or chip timing work on hardware.
No physical-radio test result is currently recorded.
