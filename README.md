# HF-SI4732 app for F4HWN

Full-Release **1.3.2** for the Quansheng UV-K5 (8) V3. The app uses the
F4HWN overlay API and an SI4732 receiver connected to the radio.
The Alpha phase runs through 0.6.x; the Beta phase runs from 0.7.0 through
0.9.5 and uses the **Beta-Release** label. Starting at 1.0.0, releases use the
**Full-Release** label.
After 1.0.0, increment the patch component by 2 per release; when that would
reach 10, roll over to the next minor version (for example, 1.0.8 becomes
1.1.0).

## Features

- AM reception across the SI4732 range, 150-30000 kHz.
- Centered frequency display in MHz (for example 7.000 or 24.000), the current tuning step, AM mode, and
  chip RSSI displayed as dBm (assuming a 50-ohm input), with a four-bar signal
  indicator. The first bar always stays filled; the other bars are hollow until
  their levels are reached: two bars at -90 dBm, three at -80 dBm, and four at
  -70 dBm or stronger. Zero RSSI is displayed as -107 dBm.
- Live audio waveform sampled through the firmware's API-level-3 PA4 capture.
- A speaker icon in the top status bar shows when audio is enabled. The dBm
  reading and tuning step are stacked vertically at the left.
- Manual signal scanning, 10 persistent memory channels, and
  direct frequency entry.
- A second preset menu for managing the same 10 saved frequencies.
- Audio starts enabled at the SI4732's maximum volume setting (63). AM
  soft-mute attenuation is disabled and AVC gain is increased; this can make
  background noise more noticeable.

## Controls

`#` refers to the radio's F/function key (`APP_KEY_F` in the overlay API).

| Control | Action |
| --- | --- |
| Up / Down | Tune by the selected step in VFO mode; move between saved channels in memory mode |
| Side 2 (flashlight button) | Cycle the tuning step: 12, 10, 9, 5, 2, then 1 kHz; the selection appears below the dBm reading |
| Side 1 (button below PTT) | Open or close the preset menu |
| Double-press `#` quickly | Start a manual scan upward |
| `#` + Up / Down | Start a manual scan in that direction |
| `#` + `3` | Toggle VFO / memory mode |
| `*` | Toggle audio on or mute |
| Menu | In the preset menu, save the typed frequency or tune the selected preset; otherwise save a VFO frequency or mark a memory for deletion |
| Exit | Stop a scan, cancel an entry/menu prompt, or leave the app |

Press `#` to arm the modifier; an `F` appears outside the title box on the
right side of the title row until you press another button. A quick
double-press of `#` still starts an upward scan.

The SI4732 AM tuning command accepts whole-kHz frequencies. Steps below 1 kHz
and an exact 12.5 kHz step are not supported; the 12 kHz step is the closest
available option.

### Frequency entry

In VFO mode, enter 3-5 digits in kHz. A 5-digit entry tunes immediately;
3- and 4-digit entries tune after a one-second pause. Valid frequencies are
150-30000 kHz. Up/Down tuning and manual scanning traverse this full range and
wrap at its endpoints. Direct frequency entry can tune anywhere in the
supported 150-30000 kHz range.

### Scanning and memories

- A manual scan steps every 50 ms and stops after two consecutive readings at
  approximately -90 dBm (the SI4732 RSSI estimate assumes a 50-ohm input).
  Use `#` + Up/Down to start in either direction, or double-press `#` quickly
  to scan upward; press Up, Down, or Exit to stop.
- Use `#` + `3` to enter or leave memory mode. In memory mode, Up/Down moves
  between stored channels; enter a two-digit channel number (01-10) to select
  one.
- To save the current VFO frequency, press Menu, enter a two-digit channel
  number (01-10), then press Menu again. In memory mode, press Menu twice to
  delete the selected channel.

Saved frequencies are kept in the app configuration and remain after restart.

### User presets

Press Side 1 (the button below PTT) to open the 10-slot preset menu.
Use Up/Down to browse slots. Enter a frequency in kHz and press Menu to save it
to the selected slot; an empty or occupied slot can be used. Press Menu on an
occupied slot without entering digits to tune to it and return to the receiver.
The preset menu and memory mode use the same saved channels. Exit or Side 1
closes the preset menu without leaving the app. While it is open, the receiver
status and waveform are hidden so the preset selection is uncluttered.

## Display notes and limitations

`HF-Receive` is shown in an inverse badge. The speaker icon appears in the top
status bar when audio is enabled. The dBm reading and tuning-step indicator
are stacked vertically at the left. Signal bars sit to the left of the
waveform. The dBm display and signal bars use smoothed RSSI to reduce flicker.
The first signal bar remains filled and the other bars stay hollow until
crossing -90, -80, and -70 dBm. The centered footer shows **1.3.2 Xserv**.
The waveform shows audio samples
from PA4 at a fixed scale. Waveform capture requires API-level-3 firmware and
an audio signal at the capture input.

The standard HF Receiver app remains AM-only. The displayed RSSI-to-dBm
conversion assumes a 50-ohm input and is an estimate, not a calibrated
measurement; a zero RSSI reading is displayed as -107 dBm.

## Standalone SSB receiver

The optional **HF SSB** app is separate from the standard AM app, so the
existing AM features and app size remain unchanged. It starts at 7000 kHz in AM,
supports 150-30000 kHz, and provides AM, USB, and LSB demodulation.

| Control | Action |
| --- | --- |
| Up / Down | Tune by the selected step |
| Side 2 (flashlight button) | Cycle 12, 10, 9, 5, 2, and 1 kHz steps |
| `#` + `2` | Cycle AM -> USB -> LSB -> AM |
| `*` | Toggle audio on or mute |
| Number keys | Enter a frequency in kHz (3-5 digits); 5 digits tunes immediately, shorter entries after a pause |
| Exit | Leave the app |

The first switch from AM to USB loads the full SI4732 SSB patch from five
companion apps named `SSBPATCH0` through `SSBPATCH4`. Install all five together
with `HF SSB`; each carrier consumes one app slot. The SSB app requires firmware
with the `sysinfo` overlay capability. Without all five valid carriers it stays
in AM and reports a patch error. Loading and receiving SSB have not yet been
verified on a physical radio.

The patch data in `ssb_patches` is packaged locally by the build scripts; the
script does not copy patch bytes into source code. Treat the patch and generated
carrier apps as private and do not redistribute them unless you have confirmed
that you are authorized to do so.

## Hardware

The bit-banged SI4732 I2C driver uses PA10 for SCL and PA11 for SDA. Connect
the receiver's control lines to these pins; GPIO access is configured for the
PY32F071. The SI4732 analog audio output must also be connected to the radio's
audio path as required by the hardware modification. I2C alone does not carry
audio. The overlay enables the radio speaker audio path after SI4732
initialization.

## Build and package

Install the ARM GNU embedded toolchain (`arm-none-eabi-gcc`, `objcopy`, and
`size`) and Python before building.

- On Windows, run `build.bat`. It compiles and packages the AM app, standalone
  SSB app, and five patch carriers under `build`. If `F4HWN_ROOT` points to a
  firmware checkout, it uses that checkout's packer; otherwise it uses the
  project's local packer.
- On a Unix-like shell, set `F4HWN_ROOT` and run `build.sh` for the same
  binaries and packages. Without `F4HWN_ROOT`, `build.sh` creates only the
  compiled `.elf` and `.bin` files.
- The overlay API minimum is level 3. `APP_VMA` defaults to `0x20000280`;
  override it to match the overlay workspace address in the firmware build.
- Install the generated `.app` using the firmware's normal overlay-app
  installation method.
