# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

ESP32-C3 decibel meter (ESP-IDF, C++). It reads a sound sensor over UART, serves a captive web portal, and notifies a separate signal generator over ESP-NOW. The generator firmware lives in the sibling repo `../dd-generator`.

## Commands

Developed against ESP-IDF v5.5.1. ESP-NOW callback signatures changed in 5.5, so older versions will not compile.

```sh
source ~/esp/v5.5.1/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

`sdkconfig` is tracked and `idf.py build` rewrites it. To compile-check without touching it, build out of tree on a copy:

```sh
cp sdkconfig /tmp/sdkconfig.check
idf.py -B /tmp/build-check -D SDKCONFIG=/tmp/sdkconfig.check build
```

There is no test suite. `pytest_hello_world.py` is leftover ESP-IDF template code and does not apply to this firmware.

## Architecture

`main/main.cpp` wires everything together and runs the sensor loop in `app_main`. It samples UART1 about every 300 ms; a valid frame starts `bb aa` and carries dB×10 in bytes 3-4.

**Radio lifecycle (`main.cpp`, `AP`, `ESP_NOW`).** After boot, the visible AP and ESP-NOW run side by side for 5 minutes on fixed channel 1. The portal then closes for good until the next reboot: HTTP and DNS stop, ESP-NOW is shut down, Wi-Fi restarts LR-only with a hidden SSID, and ESP-NOW resumes. Any stats shown on the web therefore come from the AP window. ESP-NOW stays on `WIFI_IF_AP` in both phases so its source MAC does not change, and that MAC (not the base MAC) is the one to show users. `esp_now_*` calls must not run while the Wi-Fi driver restarts, which is what `suspend()`/`resume()` and the `m_radio` mutex are for.

**Captive portal (`AP`, `DNS_Server`, `HTTP_Server`).** All three parts are needed for the sign-in popup: DHCP hands out our IP (192.168.1.150) as DNS and sets option 114, `DNS_Server` answers every A query with that IP, and the HTTP 404 handler redirects unknown URLs (OS connectivity probes) to `/`.

**Web UI (`main/HTML/`).** Embedded into the binary via `EMBED_FILES` in `main/CMakeLists.txt`, and new source files must be added there too. The page polls `GET /getStats` once a second and draws the chart client-side, so history starts when the page opens. Settings go through `/getSettings` and `/setSettings`; the handshake is `POST /connectGenerator`.

**Shared state.** `SharedData` (mutex) holds sensor stats and live settings, read by the HTTP handlers and written by the main loop. `ESP_NOW` keeps its own counters under a spinlock because its callbacks run in the Wi-Fi task. Saving settings takes effect immediately, with no reboot.

**Stored config.** NVS holds JSON like `{"treshold":"85.0","MAC":"aa:bb:.."}`. The misspelled key and string-typed values are the on-flash format, so renaming them breaks existing devices.

## Generator protocol constraints

The current `../dd-generator` firmware starts a long blocking tone on **every** ESP-NOW frame it receives, whatever the payload, and never sends anything. So:

- Do not add heartbeats or other periodic frames; each one would trigger a tone.
- Handshake retries stop at the first MAC-level ACK (`ESP_NOW::taskLoop`).
- The handshake only reaches `Connected` once the generator sends a frame back, which needs a generator firmware change. Until then it ends in `NoReply`.
- The generator cycles between a b/g/n AP phase and an LR-only phase, so handshake attempts alternate between 11b and LR rates during the AP window.
