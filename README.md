# Home Assistant Voice Preview Puck Firmware

This project targets the original Home Assistant Voice Preview puck (ESP32-S3, 16 MB flash, 8 MB PSRAM) with the center push button, rotary encoder, microphone privacy switch, and LED ring. The firmware provides always-on duplex audio transport between the puck and a configurable WebSocket server using ESP-IDF v5.1+.

## Features

- Wi-Fi provisioning via SoftAP captive portal on first boot (`Puck-Setup` / `voice-setup`).
- Persistent WebSocket client with JSON hello handshake and framed PCM audio transport.
- I²S microphone capture and speaker playback at 16 kHz mono with 20 ms framing.
- Rotary encoder, push button, and mic privacy switch handling with LED ring feedback.
- HTTP status page with current configuration, diagnostics, and factory reset.
- Optional Node.js bridge server for local loopback or custom integrations.

## Continuous integration & releases

A GitHub Actions workflow (`Build and Release Firmware`) runs on every pull request and push to `main`:

- **Pull requests** build the firmware and upload `ha_voice_puck_firmware.zip` and checksum files as workflow artifacts.
- **Pushes to `main`** produce the same artifact and publish an automatic prerelease tagged `main-<run number>` that contains the ZIP bundle.

You can download the ZIP bundle from the workflow run (PR) or from the latest prerelease on `main`. Each bundle contains:

- `ha_voice_puck.bin` – application binary
- `bootloader.bin` – bootloader image
- `partition-table.bin` – partition layout
- Optional flashing argument helpers (`flasher_args.json` / `flash_args`)
- `SHA256SUMS` – checksum list for verification

## Flashing prebuilt firmware

1. Extract the downloaded ZIP file.
2. Connect the puck to your computer via USB.
3. Put the device in download mode if required (hold BOOT and tap RESET, then release BOOT).
4. Flash using either method:
   - **With `idf.py` (recommended if ESP-IDF is installed):**
     ```bash
     idf.py -p /dev/ttyACM0 flash
     ```
     Ensure the `port` argument matches your serial device.
   - **With `esptool.py` and `flasher_args.json`:**
     ```bash
     esptool.py --chip esp32s3 --port /dev/ttyACM0 --before default_reset --after hard_reset \
       write_flash @flasher_args.json
     ```
     Adjust the serial port path as necessary.

After flashing, open a serial monitor (e.g., `idf.py -p /dev/ttyACM0 monitor`) to watch boot messages.

## Building from source

Install ESP-IDF v5.1+ and then run:

```bash
idf.py set-target esp32s3
idf.py menuconfig   # select pin preset and customise defaults
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### macOS setup with Homebrew

On macOS you can install the ESP-IDF toolchain and its prerequisites using [Homebrew](https://brew.sh/):

```bash
brew update
brew install cmake ninja python@3.11 git wget
brew install --cask gnu-sed
brew install espressif/idf/esp-idf
```

After installation, export the ESP-IDF environment before building:

```bash
source "$(brew --prefix esp-idf)/export.sh"
```

You can then follow the standard build steps above. macOS typically exposes USB serial ports under `/dev/tty.usbserial-*` or `/dev/tty.usbmodem*`; substitute the correct path when flashing.

## First boot provisioning

1. Power on the puck; it enters provisioning mode if no Wi-Fi credentials are stored.
2. Connect to the `Puck-Setup` network using password `voice-setup`.
3. Browse to [http://192.168.4.1](http://192.168.4.1) and enter Wi-Fi SSID/password, WebSocket server URL (`ws://aiboss.lan.home.malaiwah.com:7000/` default), optional token, and mode.
4. The puck saves the configuration and restarts, then connects to the specified Wi-Fi network.

## LED ring states

- **Boot:** slow wipe animation
- **Wi-Fi connecting:** breathing animation
- **Online idle:** dim steady ring
- **Streaming TX:** microphone level VU meter
- **Streaming RX:** alternating VU pulses
- **Muted/privacy:** solid red
- **Error/reconnect:** orange sweep

## Button and encoder mapping

- **K0 button:** short press toggles mute; long press (2 s) toggles always-on/PTT mode.
- **Rotary encoder:** adjust speaker volume (0–100%). Push to play test beeps.
- **Mic switch:** hardware privacy mute; prevents microphone capture and forces LED ring red.

## Tools

A minimal WebSocket bridge server is provided in `tools/bridge/`.

```bash
cd tools/bridge
npm install
npm start
```

The bridge logs incoming audio frames and echoes the PCM data back to the puck.

## Repository layout

- `main/` – application entry point and orchestration.
- `components/portal/` – provisioning SoftAP and captive portal.
- `components/ws_audio/` – WebSocket transport logic.
- `components/audio/` – I²S audio driver setup and helpers.
- `components/controls/` – button, encoder, and mic switch handling.
- `components/ledring/` – LED ring driver and animations.
- `tools/bridge/` – optional Node.js WebSocket echo bridge.
