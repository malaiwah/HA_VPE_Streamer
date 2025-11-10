# Home Assistant Voice Preview Puck Firmware

This repository contains an ESP-IDF firmware project that turns the Home Assistant voice preview puck into an always-on duplex audio endpoint. The device captures microphone audio at 16 kHz and streams it over a persistent WebSocket connection while simultaneously playing audio received from the server.

## Features

- Wi-Fi provisioning via SoftAP captive portal on first boot (`Puck-Setup` / `voice-setup`).
- Persistent WebSocket client with JSON hello handshake and framed PCM audio transport.
- I²S microphone capture and speaker playback at 16 kHz mono with 20 ms framing.
- Rotary encoder, push button, and mic privacy switch handling with LED ring feedback.
- HTTP status page with current configuration, diagnostics, and factory reset.
- Optional Node.js bridge server for local loopback or custom integrations.

## Building

```bash
idf.py set-target esp32s3
idf.py menuconfig   # select pin preset and customise defaults
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### First boot provisioning

1. Power on the puck; it enters provisioning mode if no Wi-Fi credentials are stored.
2. Connect to the `Puck-Setup` network using password `voice-setup`.
3. Browse to [http://192.168.4.1](http://192.168.4.1) and enter Wi-Fi SSID/password and WebSocket server URL (`ws://aiboss.lan.home.malaiwah.com:7000/` by default).
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

## Directory structure

- `main/` – application entry point and orchestration.
- `components/portal/` – provisioning SoftAP and captive portal.
- `components/ws_audio/` – WebSocket transport logic.
- `components/audio/` – I²S audio driver setup and helpers.
- `components/controls/` – button, encoder, and mic switch handling.
- `components/ledring/` – LED ring driver and animations.
- `tools/bridge/` – optional Node.js WebSocket echo bridge.
