# hermes-radio-daemon

Radio control daemon for the [HERMES](https://github.com/Rhizomatica/hermes-net) network. Replaces the standalone `sbitx_controller` and embeds its DSP/ALSA path in-process. Supports both HF Signals (sBitx/zBitx) hardware and Hamlib-controlled CAT radios.

## Features

- **`hfsignals` backend** — embedded sBitx/zBitx DSP + ALSA path with all original hardware control (Si5351, GPIO, I2C, WM8731 codec)
- **`hamlib` backend** — Hamlib CAT/PTT control for IC-7100, IC-7300, TS-480, etc.
- **9 modulation modes**: LSB, USB, CW, **FM** (NBFM), **AM** (broadcast), **DRM** (Digital Radio Mondiale), RADEv2 digital voice, **FT8**, **RTTY**
- **Digital text modes**: FT8 (8-FSK), CW (Morse via unixcw), RTTY (Baudot FSK) with unified websocket API
- **SSB voice DSP chain**: 3-band pre-EQ, wideband compressor, pre-emphasis, DC block, limiter
- **RX voice DSP chain**: DC block, adaptive noise reduction (libspecbleach), AGC (SLOW/MEDIUM/FAST), soft limiter
- **Audio bridge** for websocket RX/TX streaming on both backends
- **Up to 9 profiles** with frequency, mode, power, timeout, and digital-voice state
- Websocket control/media API with binary audio frames, waterfall/spectrum, and **unified digital mode commands**
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
                libsndfile1-dev libcw-dev meson ninja-build pkg-config
```

**libspecbleach** provides adaptive spectral noise reduction on RX.

**libcsdr** provides FM/AM modulation/demodulation, AGC, DC blocking, resampling, and filter design.

**libunixcw** (`-lcw`) provides the CW receiver state machine (mark/space → characters).

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
bpf_low = 100
bpf_high = 6000
agc = OFF
compressor = OFF
tx_preemphasis = OFF
noise_reduction = OFF
digital_voice = 0
enable_knob_volume = 1
enable_knob_frequency = 1
enable_ptt = 1

; ── profile5: Digital Voice (RADEv2) ──
[profile5]
freq = 7050000
mode = USB
operating_mode = 0
bpf_low = 300
bpf_high = 3000
agc = OFF
compressor = OFF
tx_preemphasis = OFF
noise_reduction = OFF
digital_voice = 1
enable_knob_volume = 1
enable_knob_frequency = 1
enable_ptt = 1
```

**Profile fields reference:**

| Field | Values | Description |
|-------|--------|-------------|
| `freq` | Hz (integer) | Operating frequency |
| `mode` | `USB`, `LSB`, `CW`, `FM`, `AM`, `DRM`, `FT8`, `RTTY` | Modulation mode |
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
| SSB voice | 50 | 3000 | 3.0 kHz |
| SSB data | 50 | 3000 | 3.0 kHz |
| FM (NBFM) | 100 | 7000 | 14 kHz (Carson rule: 2×5k dev + 2×2k audio) |
| AM | 50 | 10000 | 20 kHz (broadcast audio) |
| DRM | 100 | 6000 | 12 kHz (DRM mode B 10 kHz + margin) |
| FT8 | 50 | 3000 | 3.0 kHz (SSB-based) |
| CW | 500 | 900 | ~400 Hz around pitch (700 Hz) |
| RTTY | 1300 | 1700 | ~400 Hz around mark/space (1585/1415 Hz) |

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

### CW (Morse code)

Uses `libunixcw` for the receiver state machine and a DDS tone generator for the transmitter:
- **RX**: SSB USB → Goertzel single-bin tone detector → mark/space events → unixcw decoder → spool
- **TX**: Text → Morse lookup → DDS sine + raised-cosine envelope → SSB TX
- Configurable WPM (`cw_wpm`, default 20) and pitch (`cw_pitch`, default 700 Hz)
- Narrow filter centred around the CW pitch
- End-of-message: 3× word-space silence threshold

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

### FT8 (Weak-signal digital mode)

Uses vendored `ft8_lib` for encoding and `decode_ft8` pipe for decoding:
- **TX**: Text → 77-bit LDPC → 8-FSK tones → GFSK @ 12 kHz → SSB TX
- **RX**: SSB USB demod → audio → `decode_ft8` WAV pipe → decoded message → spool
- 15-second slots, standard FT8 protocol
- Vendored library at `vendor/ft8_lib/` (MIT-licensed)

### RTTY (Radio Teletype — 45.45 baud FSK)

Uses vendored `minimodem` FSK core (FFT-based FSK detector + Baudot codec):
- **TX**: Text → Baudot encoding (5-bit + start/1.5 stop) → FSK tone generator @ 96 kHz → SSB TX
- **RX**: SSB USB → 12 kHz audio → `fsk_find_frame()` → Baudot decode → spool
- Configurable baud, mark frequency, and shift
- End-of-message: CR+LF or 3-second idle timeout
- Vendored library at `vendor/minimodem/` (GPLv3-licensed)

### Digital Voice (RADEv2)

Neural-network-based digital voice codec. Activated per-profile with `digital_voice = 1`. Uses the vendored RADEv2 pure-C encoder/decoder at `vendor/radev2/`. The full voice DSP chain (compressor, pre-emphasis, noise reduction) is automatically bypassed when `digital_voice = 1`.

### FT8 (8-FSK digital mode)

Weak-signal digital mode using vendored `ft8_lib` for encoding and the `decode_ft8` binary for reception:
- **TX**: text → 77-bit LDPC payload → 8-FSK tones → GFSK audio @ 12 kHz → SSB TX
- **RX**: SSB USB demod → audio → `decode_ft8` pipe → decoded messages → spool
- Standard 15-second slots, 50–3000 Hz SSB bandwidth
- Active tone frequency configurable (`ft8_tone`, default 1000 Hz)

### CW (Morse code)

Uses `libunixcw` for RX decoding state machine and a DDS tone generator for TX:
- **TX**: text → Morse lookup → DDS sine keyed by dot/dash timing → raised-cosine envelope → SSB TX
- **RX**: SSB USB demod → Goertzel single-bin detector → mark/space events → `cw_rec_mark_begin/end()` → decoded characters → spool
- Configurable WPM (`cw_wpm`, default 20) and pitch (`cw_pitch`, default 700 Hz)
- End-of-message detection: 3× word-space silence (~2s at 12 WPM)
- Narrow filter: bpf_low=500, bpf_high=900 (around 700 Hz pitch)

### RTTY (Radio Teletype — 45.45 baud FSK)

Uses vendored `minimodem` FSK core (FFT-based demodulator) and a Baudot FSK tone generator:
- **TX**: text → Baudot encoding (5-bit + start/1.5 stop) → FSK tones (mark/space) → SSB TX
- **RX**: SSB USB demod → 12 kHz audio → `fsk_find_frame()` → Baudot decode → spool
- Configurable baud (`rtty_baud`, default 45), mark frequency (`rtty_mark`, default 1585 Hz), shift (`rtty_shift`, default 170 Hz — space = mark - shift)
- End-of-message: CR+LF (Baudot carriage return) or 3-second idle timeout
- Standard low-tones: mark 1585 Hz, space 1415 Hz; high tones available via config

### Unified Digital Mode WebSocket API

All three digital text modes (FT8/CW/RTTY) share a common interface:

```json
// Queue a message for TX (mode selected by active profile)
{"cmd": "digi_send", "text": "CQ CQ DE CALLSIGN GRID"}

// Get recent decoded/transmitted messages
{"cmd": "digi_messages", "count": 20}

// Get current digital mode configuration
{"cmd": "digi_get_config"}

// Set digital mode parameters
{"cmd": "digi_config", "key": "cw_wpm", "value": 25}
{"cmd": "digi_config", "key": "rtty_baud", "value": 45}
{"cmd": "digi_config", "key": "rtty_mark", "value": 2125}
```

**Digi config keys:**

| Key | Applies to | Default | Range |
|-----|-----------|---------|-------|
| `cw_wpm` | CW | 20 | 5–60 |
| `cw_pitch` | CW | 700 Hz | 300–1000 |
| `rtty_baud` | RTTY | 45 | 45–300 |
| `rtty_mark` | RTTY | 1585 Hz | 500–3000 |
| `rtty_shift` | RTTY | 170 Hz | 85–850 |

**Spool**: all messages go to `/var/spool/hermes-digi/spool.log` in one-line format:
```
FT8 rx 14.074: CQ YL3JG KO26
CW rx 14.025: CQ DE CALLSIGN K
RTTY tx 14.080: CQ CQ DE HERMES TEST
```

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
{"cmd":"digi_send","text":"CQ CQ DE CALLSIGN"}
{"cmd":"digi_messages","count":10}
{"cmd":"digi_config","key":"cw_wpm","value":25}
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
