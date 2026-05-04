# hermes-radio-daemon

Radio control daemon for the [HERMES](https://github.com/Rhizomatica/hermes-net) network. Replaces the standalone `sbitx_controller` and embeds its DSP/ALSA path in-process. Supports both HF Signals (sBitx/zBitx) hardware and Hamlib-controlled CAT radios.

## Features

- **`hfsignals` backend** — embedded sBitx/zBitx DSP + ALSA path with all original hardware control (Si5351, GPIO, I2C, WM8731 codec)
- **`hamlib` backend** — Hamlib CAT/PTT control for IC-7100, IC-7300, TS-480, etc.
- **7 modulation modes**: LSB, USB, CW, **FM** (NBFM), **AM** (broadcast), **DRM** (Digital Radio Mondiale via Dream), and RADEv2 digital voice
- **SSB voice DSP chain**: 3-band pre-EQ, wideband compressor, pre-emphasis, DC block, limiter
- **RX voice DSP chain**: DC block, adaptive noise reduction (libspecbleach), AGC (SLOW/MEDIUM/FAST), soft limiter
- **Audio bridge** for websocket RX/TX streaming on both backends
- Up to 4 profiles with frequency, mode, power, timeout, and digital-voice state
- Websocket control/media API with binary audio frames and waterfall/spectrum
- WAV recording with remote start/stop
- INI configuration at `/etc/sbitx/core.ini` and `/etc/sbitx/user.ini`
- Optional CPU affinity pinning
- zBitx hardware profile support (different GPIO layout, BFO, TX/RX switching)

## Binaries

| Binary | Description |
|--------|-------------|
| `radio_daemon` | Main daemon (both backends compiled in) |
| `radio_client` | CLI client (SHM protocol, identical to sbitx_client API) |
| `sbitx_client` | Symlink → `radio_client` (backward compat) |

## Directory Structure

```
hermes-radio-daemon/
├── sbitx/         HF Signals backend (GPIO, I2C, Si5351, ALSA, config)
├── hamlib/        Hamlib CAT backend (control, media bridge, pipeline)
├── dsp/           SSB DSP + FM/AM demodulators + DRM (Dream subprocess)
├── vendor/radev2/ RADEv2 pure-C encoder/decoder (digital voice)
├── vendor/radev1/ RADAE v1 pure-C library (available for future use)
├── config/        Sample core.ini and user.ini
├── include/       SHM protocol headers (sbitx_io.h, radio_cmds.h)
└── tests/         Regression tests
```

## Dependencies

```bash
apt-get install libhamlib-dev libiniparser-dev libasound2-dev libfftw3-dev \
                libfftw3f-dev libssl-dev libi2c-dev libcsdr-dev libspecbleach-dev \
                libsndfile1-dev meson ninja-build pkg-config
```

**libspecbleach** provides adaptive spectral noise reduction on RX (build it first from `/home/rafael2k/files/rhizomatica/hermes/libspecbleach`).

**libcsdr** provides FM/AM modulation/demodulation, AGC, DC blocking, resampling, and filter design.

**Dream** (optional, for DRM mode): build in console mode:
```bash
cd /home/rafael2k/files/rhizomatica/hermes/dream
qmake "CONFIG+=console"
make
sudo cp dream /usr/bin/dream
```

## Compilation

```bash
make
```

## Installation

```bash
sudo make install
```

Installs to `/usr/bin/` and `/etc/sbitx/` (configs are only placed if they do not already exist):

- `/usr/bin/radio_daemon`
- `/usr/bin/radio_client`
- `/usr/bin/sbitx_client` → symlink → `radio_client`
- `/etc/sbitx/core.ini`
- `/etc/sbitx/user.ini`

## Configuration

All configuration lives in two files under `/etc/sbitx/`.

### `/etc/sbitx/core.ini` — radio hardware

```ini
[main]
radio_backend = hfsignals       ; hfsignals or hamlib

; Hardware profile: sbitx or zbitx (GPIO layout, BFO frequency, TR switching)
hw_profile = sbitx              ; hfsignals only

; Hamlib model (only for hamlib backend)
radio_model = 3070              ; IC-7100; see rigctl -l for full list

; BFO frequency in Hz (defaults to per-profile value: 40035000 sbitx, 40048000 zbitx)
bfo = 40035000

bridge_compensation = 100       ; SWR bridge calibration
serial_number = 0
reflected_threshold = 25        ; vswr * 10, 0 = disabled

; Interfaces
enable_websocket = 0
enable_shm_control = 1

; Audio bridge (works with both backends)
enable_audio_bridge = 0
audio_sample_rate = 8000

; Dream DRM receiver binary path
dream_path = /usr/bin/dream

; Hamlib-only settings (ignored by hfsignals):
rig_pathname = /dev/ttyUSB0
serial_rate = 9600
ptt_type = RIG
capture_device = default
playback_device = default
recording_dir = /var/lib/hermes-radio-daemon

; Per-band TX power calibration (scale = multiplier):
; [tx_band0]
; f_start = 5700000
; f_stop = 6800000
; scale = 1.5
```

### `/etc/sbitx/user.ini` — profiles

```ini
[main]
current_profile = 0
default_profile = 0
default_profile_fallback_timeout = -1   ; seconds, -1 = disabled
step_size = 100                         ; frequency knob step in Hz
tone_generation = 0

; ── profile0: SSB voice (full DSP) ──
[profile0]
freq = 7050000
mode = USB
operating_mode = 0
power_level_percentage = 100
mic_level = 50
rx_level = 100
speaker_level = 50
tx_level = 100
bpf_low = 300
bpf_high = 3000
agc = SLOW
compressor = ON
tx_preemphasis = ON
noise_reduction = ON
digital_voice = 0
enable_knob_volume = 1
enable_knob_frequency = 1
enable_ptt = 1

; ── profile1: digital data (flat, no voice DSP) ──
[profile1]
freq = 7050000
mode = USB
operating_mode = 0
power_level_percentage = 100
mic_level = 50
rx_level = 100
speaker_level = 50
tx_level = 100
bpf_low = 300
bpf_high = 2800
agc = OFF
compressor = OFF
tx_preemphasis = OFF
noise_reduction = OFF
digital_voice = 0
enable_knob_volume = 1
enable_knob_frequency = 1
enable_ptt = 1

; ── profile2: NBFM (5 kHz deviation, 2m band) ──
[profile2]
freq = 145500000
mode = FM
operating_mode = 0
power_level_percentage = 100
mic_level = 50
rx_level = 100
speaker_level = 50
tx_level = 100
bpf_low = 100
bpf_high = 7000         ; 14 kHz Carson bandwidth (2×5k dev + 2×2k audio)
agc = SLOW
compressor = OFF
tx_preemphasis = OFF
noise_reduction = OFF
digital_voice = 0
enable_knob_volume = 1
enable_knob_frequency = 1
enable_ptt = 1

; ── profile3: Broadcast AM (MW band) ──
[profile3]
freq = 1000000
mode = AM
operating_mode = 0
power_level_percentage = 100
mic_level = 50
rx_level = 100
speaker_level = 50
tx_level = 100
bpf_low = 50
bpf_high = 10000        ; 20 kHz AM audio bandwidth
agc = SLOW
compressor = OFF
tx_preemphasis = OFF
noise_reduction = OFF
digital_voice = 0
enable_knob_volume = 1
enable_knob_frequency = 1
enable_ptt = 1

; ── profile4: DRM (Digital Radio Mondiale) ──
[profile4]
freq = 6095000
mode = DRM
operating_mode = 0
power_level_percentage = 100
mic_level = 50
rx_level = 100
speaker_level = 50
tx_level = 100
bpf_low = 100
bpf_high = 6000         ; 12 kHz DRM bandwidth (10 kHz mode B + margin)
agc = OFF
compressor = OFF
tx_preemphasis = OFF
noise_reduction = OFF
digital_voice = 0
enable_knob_volume = 1
enable_knob_frequency = 1
enable_ptt = 1
```

**Profile fields reference:**

| Field | Values | Description |
|-------|--------|-------------|
| `freq` | Hz (integer) | Operating frequency |
| `mode` | `USB`, `LSB`, `CW`, `FM`, `AM`, `DRM` | Modulation mode |
| `operating_mode` | 0=full voice, 1=loopback, 2=controls only | I/O mode |
| `bpf_low` | Hz | Low edge of DSP bandpass filter |
| `bpf_high` | Hz | High edge of DSP bandpass filter |
| `agc` | `OFF`, `SLOW`, `MEDIUM`, `FAST` | RX automatic gain control |
| `compressor` | `OFF`, `ON` | TX wideband speech compressor |
| `tx_preemphasis` | `OFF`, `ON` | +6 dB/octave treble boost above 2 kHz |
| `noise_reduction` | `OFF`, `ON` | libspecbleach adaptive spectral denoiser |
| `digital_voice` | `0`, `1` | RADEv2 digital voice mode |
| `power_level_percentage` | 0–100 | TX RF power level |
| `mic_level`, `rx_level`, `speaker_level`, `tx_level` | 0–100 | ALSA mixer levels |

**Voice DSP (`compressor=ON`, `preemphasis=ON`, `noise_reduction=ON`) is automatically disabled when `digital_voice=1`.**

**Bandpass filter values per mode:**

| Mode | Typical bpf_low | Typical bpf_high | Bandwidth |
|------|-----------------|------------------|-----------|
| SSB voice | 300 | 3000 | 2.7 kHz |
| SSB data | 300 | 2800 | 2.5 kHz |
| FM (NBFM) | 100 | 7000 | 14 kHz (Carson rule: 2×5k dev + 2×2k audio) |
| AM | 50 | 10000 | 20 kHz (broadcast audio) |
| DRM | 100 | 6000 | 12 kHz (DRM mode B 10 kHz + margin) |

## Running

```bash
radio_daemon [-r /path/to/core.ini] [-u /path/to/user.ini] [-c cpu_nr] [-h]
```

Defaults: `-r /etc/sbitx/core.ini` `-u /etc/sbitx/user.ini`.

## Modulation Modes

### SSB (LSB / USB)

Standard single-sideband with the full voice DSP chain:
- **RX**: ADC → FFT → sideband filter → rotate → IFFT → DC block → noise reduction → AGC → limiter → DAC
- **TX**: Mic → DC block → pre-EQ → compressor → pre-emphasis → limiter → FFT → filter → sideband → rotate → IFFT → DAC

### CW

Continuous wave mode (legacy support, tone generation via VFO).

### NBFM (Narrow Band FM)

Uses csdr `fmdemod_quadri_cf` and `fmmod_fc`:
- **RX**: IFFT IQ → quadri-correlator demod → 50µs IIR de-emphasis → AGC → limiter → output
- **TX**: Mic → DC block → IIR pre-emphasis → gain (5 kHz deviation) → fmmod → FFT → rotate → IFFT → DAC
- Deviation defaults to 5 kHz (configurable via `bpf_high` in the profile)

### Broadcast AM

Uses csdr `amdemod_cf` (envelope detection) and `add_dcoffset_cc`:
- **RX**: IFFT IQ → sqrt(I²+Q²) envelope → DC block → AGC → limiter → output
- **TX**: Mic → DC block → DSB/SC → add carrier → FFT → rotate → IFFT → DAC
- Modulation depth controlled by mic gain (keep <1.0 for <100%)

### DRM (Digital Radio Mondiale)

Launches **Dream** as a subprocess via dual Unix pipes:
- **Signal**: 48 kHz stereo S16_LE zero-IF I/Q (left=I, right=Q) → Dream stdin
- **Audio**: 8 kHz stereo S16_LE decoded audio from Dream stdout → upsample 96k → speaker + loopback

Dream is launched with:
```
dream --console -I - -O - --sigsrate 48000 --inchansel 6 --audsrate 8000
```

DRM TX is not implemented (use Dream's native transmitter if needed).

### Digital Voice (RADEv2)

Neural-network-based digital voice codec. Activated per-profile with `digital_voice = 1`. Uses the vendored RADEv2 pure-C encoder/decoder at `vendor/radev2/`. The full voice DSP chain (compressor, pre-emphasis, noise reduction) is automatically bypassed when `digital_voice = 1`.

## Audio Bridge

The audio bridge streams RX/TX audio over websocket and works with **both backends**:

- **hamlib**: `radio_media.c` captures from `capture_device` ALSA → `rx_audio_ring` → websocket broadcast; websocket TX → `tx_audio_ring` → `playback_device`
- **hfsignals/sBitx**: DSP control thread pushes RX audio to `rx_audio_ring`, pulls TX from `tx_audio_ring` (replaces mic input when websocket audio is available)

Enable with `enable_audio_bridge = 1` in `core.ini`. Sample rate configurable via `audio_sample_rate` (default 8000).

## Websocket Control

When `enable_websocket = 1`:

Text frames use compact JSON commands:
```json
{"cmd":"get_state"}
{"cmd":"get_frequency","profile":0}
{"cmd":"set_frequency","profile":0,"value":7100000}
{"cmd":"set_mode","profile":0,"value":"USB"}
{"cmd":"ptt_on"}
{"cmd":"ptt_off"}
{"cmd":"start_recording","stream":"both"}
```

Binary frames for audio and waterfall:
| Type | Direction | Payload |
|------|-----------|---------|
| `0x01` | server → client | RX audio, mono S16_LE at `audio_sample_rate` |
| `0x01` | client → server | TX audio, mono S16_LE at `audio_sample_rate` |
| `0x02` | server → client | RX spectrum: u32 sample_rate, u16 bins, float32[bins] |
| `0x03` | server → client | TX spectrum: u32 sample_rate, u16 bins, float32[bins] |

Recordings saved as `rx-*.wav` / `tx-*.wav` under `recording_dir`.

## `sbitx_client` CLI

```bash
sbitx_client -c command [-a argument] [-p profile_number]
```

Examples:
```bash
sbitx_client -c set_frequency -a 7100000 -p 0
sbitx_client -c get_frequency -p 0
sbitx_client -c set_mode -a USB -p 0
sbitx_client -c set_mode -a FM -p 2
sbitx_client -c set_mode -a DRM -p 4
sbitx_client -c ptt_on
sbitx_client -c ptt_off
sbitx_client -c get_txrx_status
sbitx_client -c set_profile -a 1
sbitx_client -c radio_reset
```

Run `sbitx_client -h` for the full command list.

## Migrating from `sbitx_controller`

### What changes

Old:
```bash
sbitx_controller [-c cpu_nr]
```

New:
```bash
radio_daemon [-r /path/to/core.ini] [-u /path/to/user.ini] [-c cpu_nr]
```

### What stays the same

- `sbitx_client` commands (now symlink → `radio_client`)
- SHM control protocol (`enable_shm_control = 1`)
- Profile settings in `user.ini`

## zBitx Hardware Profile

Set `hw_profile = zbitx` in `core.ini` to enable zBitx-specific GPIO handling:
- Extra pins: `ZBITX_RX_LINE` (GPIO 15), `ZBITX_LPF_E` (GPIO 12)
- Different BFO frequency: 40048000 Hz (vs 40035000 for sbitx)
- Modified TX/RX switching sequence (RX line polarity before TX line)

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).

## Author

Rafael Diniz @ Rhizomatica
