# HF-SI4732 app for F4HWN

For Quansheng UV-K5 (8) V3.

This app uses the F4HWN overlay API in the local `app_api.h`. Run `build.bat`
to create the ELF and binary. `build.sh` also creates a `.app` when
`F4HWN_ROOT` points to a firmware checkout containing `App/apps/pack_app.py`.
`APP_VMA` defaults to `0x20000280`; override it to match the overlay workspace
address in your firmware build.

The SI4732 bit-banged I2C driver uses PA10 for SCL and PA11 for SDA. Connect
the receiver to those pins; GPIO access is configured for the PY32F071.
The SI4732 analog audio output must also be connected to the radio audio input
as required by the hardware modification; I2C alone does not carry audio.
Displayed RSSI is converted from the chip's dBuV reading to dBm, assuming a
50-ohm input.
Frequencies are shown in kHz and are limited by the SI4732 tuning range
(150-30000 kHz).
Enter 3-5 frequency digits directly; the app selects the matching band (or the
full-range band) and commits 3- or 4-digit entries after a short pause.
USB demodulation is not enabled; it requires a compatible SI4732 SSB patch.
Until one is supplied, `#` remains the firmware function key.
The app enables the radio speaker audio path after SI4732 initialization.
Press `*` to toggle sound; `F` + `*` still starts an automatic scan, and
`F` + `UP`/`DOWN` starts a manual scan.
The SI4732 starts at its maximum volume setting; `*` sets it to zero while muted.
AM soft-mute attenuation is disabled and AVC gain is raised for weak signals;
this can also make background noise more audible.
