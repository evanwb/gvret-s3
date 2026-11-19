# GVRET S3

A small, single-bus classic CAN adapter for SavvyCAN. Target:
`esp32-s3-devkitc-1`, Arduino-ESP32 2.0.17 through PlatformIO's pinned
`espressif32@6.12.0`, and `handmade0octopus/ESP32-TWAI-CAN@1.0.1`.
Power-on settings are **500 kbit/s, enabled, normal mode**. Standard 11-bit and
extended 29-bit data frames support DLC 0–8. Settings live in RAM.

**[Install firmware in your browser](https://evanwb.github.io/gvret-s3/)**

The GitHub Pages installer uses ESP Web Tools to flash the latest tested
default-branch build. Open it in a browser with Web Serial, such as desktop
Chrome or Edge, and close SavvyCAN first. For an existing GVRET device, use
**Enter bootloader**, then **Install firmware** and select the ROM download
port. An erased device already in download mode can go straight to installation.

## Wiring

Use a **3.3 V CAN transceiver**, for example an SN65HVD230 breakout. The ESP32's
TWAI controller needs this external physical-layer device.

| ESP32-S3 | Transceiver |
| --- | --- |
| 3V3 | VCC (3.3 V) |
| GND | GND |
| GPIO5 (`CAN_TX_PIN`) | D / TXD |
| GPIO4 (`CAN_RX_PIN`) | R / RXD |
| — | CANH → bus CANH |
| — | CANL → bus CANL |

Connect a common signal ground to the other CAN nodes. On an SN65HVD230, tie
RS low for normal high-speed operation; check whether your module already does
this. Other transceivers may have STB/EN pins that also need setting. Avoid
modules that drive RXD at 5 V. CANH/CANL never connect directly to ESP32 GPIO.
Change the two pin defines in `platformio.ini` if needed. Reserve GPIO19 (USB D−)
and GPIO20 (USB D+).

Use twisted pair for CANH/CANL and keep stubs short. Fit **120 Ω across CANH/CANL
at each of the two physical ends**, including this adapter only when it is an
end. Count resistors already installed on breakout boards. A correctly terminated,
unpowered bus measures about 60 Ω between CANH and CANL.

## Build and flash

```sh
pio run
pio run -t upload --upload-port /dev/cu.YOUR_NATIVE_USB_PORT
```

To flash a device with erased flash without building locally, download
`gvret-s3-factory.bin` from a workflow run in the repository's Actions tab. It
is a merged bootloader/partition/OTA-metadata/application image written
at **`0x0`**. See the [flashing instructions](docs/FLASHING.md).

Close SavvyCAN before uploading. With this firmware running, PlatformIO opens
the native CDC port at **1200 baud** to request the ROM bootloader, waits for
the download port to enumerate, then flashes it. BOOT/RESET buttons are not
needed. The application's native CDC port can have a different name from the
ROM download port. On Windows use `COMx`; on Linux expect `/dev/ttyACM*`.

The first upload from other firmware depends on that firmware's bootloader-entry
support. If it already permits native USB uploads, use that existing procedure
to install this version. A UART bridge with DTR/RTS wired to EN/GPIO0 can also
enter download mode without buttons; when using it, remove the
`board_upload.use_1200bps_touch` and `board_upload.wait_for_upload_port` entries
from `platformio.ini` so PlatformIO uses the UART reset procedure directly.

For SavvyCAN, move/connect a data cable to the board's **ESP32-S3 native USB
connector**, usually marked USB, rather than the USB-to-UART bridge connector.
Connector labels vary among clones; the native connector goes to GPIO19/20.

## Why USB-OTG

The S3 offers a fixed USB Serial/JTAG peripheral (`ARDUINO_USB_MODE=1`) and a
programmable USB-OTG peripheral running TinyUSB (`ARDUINO_USB_MODE=0`). This
project deliberately selects **USB-OTG**, removes the board's mode=1 default,
and manually starts one CDC-ACM interface named `GVRET S3`.

`cdc.enableReboot(false)` runs before enumeration, disabling Arduino's combined
DTR/RTS and line-baud reboot handling. A separate line-coding event handler
restores **only the 1200-baud bootloader request**, so firmware updates work
without buttons while DTR/RTS changes cannot reboot the device. SavvyCAN uses
1 Mbaud and its ESP32 serial mode lowers both DTR and RTS. Arduino's CDC
`write()` checks DTR, so output instead uses the
raw TinyUSB FIFO API with USB readiness and free-space checks. Neither DTR nor
RTS gates protocol traffic. There is no `while (!Serial)` and no text logging
on the protocol port. The USB line baud (SavvyCAN uses 1 Mbaud) does not set CAN
speed. Selecting 1200 baud in a serial monitor deliberately enters the
bootloader too; use another baud for normal access. This software entry path
requires a working application and USB stack. Recovery from an application
that cannot start still requires an accessible hardware programming/reset path.

Closing and reopening SavvyCAN leaves the USB device and CAN configuration alive.
There is no reliable close notification when DTR was already low. An unread USB
port therefore fills a bounded queue and drops subsequent CAN packets whole;
reopening may show a short backlog. Unplug/re-enumeration resets the protocol
session. Incomplete commands expire after a 250 ms inter-byte gap.

## BLE logging

Application diagnostics use
[`avinabmalla/ESP32_BleSerial@3.0.1`](https://github.com/avinabmalla/ESP32_BleSerial).
The device advertises as **GVRET S3 Logs** using the Nordic UART Service:

| Item | UUID |
| --- | --- |
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| Device TX notifications | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

Connect with a BLE UART terminal and enable notifications on the TX
characteristic. USB continues carrying GVRET frames while BLE shows the
effective bitrate, enable/listen-only state, USB state, received/transmitted
frame counts and failed transmit attempts. A snapshot is sent on connection,
USB state changes, and every five seconds. CAN configuration changes and
rejections are also logged. If the first notification arrives before your
terminal subscribes, the next periodic snapshot supplies the current state.

The `logMessage()` helper in `main.cpp` adds bounded, formatted application
messages. It uses BleSerial's byte API and flushes each message to support the
default ATT MTU. Messages are best effort while connected, with no retained
history; individual CAN frames are not logged. BLE terminal input is not
forwarded to CAN. This is application logging, not ROM boot or crash output.

## SavvyCAN

1. Open **Connection → Open Connection Window**, then **Add New Device**.
2. Select **Serial Connection (GVRET)** and the application's native USB CDC port
   (`/dev/cu.usbmodem*`, `/dev/ttyACM*`, or `COMx`). Close any serial monitor using
   that port. Leave connection validation enabled for normal keepalive checks.
3. Connect. The firmware answers discovery, bus settings, device info, time sync,
   and keepalive. SavvyCAN should show **one bus (bus 0)** and a connected status.
4. In the connection's bus settings, enable bus 0 and select the actual bus
   bitrate. Enable **Listen Only** for passive capture. Apply the settings;
   the controller is stopped and reinstalled in the requested mode without an
   MCU reboot or USB disconnect.
5. Generate traffic with another CAN node at the same bitrate. Frames appear in
   the capture window. To transmit, disable Listen Only and use SavvyCAN's frame
   sender. A normal CAN transmission needs another active node to acknowledge it.

Supported nominal rates (bit/s): **12500, 16000, 20000, 25000, 50000, 100000,
125000, 250000, 500000, 800000, 1000000**. Unsupported rates leave the previous
configuration intact; command 6 reads back the effective settings. A zero rate
retains the last rate. Disabling/re-enabling the bus and listen-only changes are
also runtime operations. Listen-only neither transmits nor acknowledges frames;
the other nodes must form a working bus themselves.

No CAN FD, RTR representation, second bus, single-wire CAN, hardware I/O,
storage, or network GVRET transport is advertised. RTR frames are discarded because
the classic GVRET packet has no RTR flag. CAN timestamps are `micros()` at
dequeue, not hardware arrival timestamps, and wrap every ~71.6 minutes as in GVRET.
Capture is best effort: driver/USB queues are finite, full TX queues reject
frames, and GVRET has no transmit acknowledgement or drop-report command here.
If bad wiring/bitrate drives TWAI bus-off, fix the bus and reapply bus settings
to restart it; automatic bus-off recovery is outside this small implementation.

## Wire format and verification

All multibyte fields are little-endian. `E7` enables binary streaming; other
unframed text is ignored. Commands begin with `F1`:

| Command | Behavior |
| --- | --- |
| `00` | TX: ID[4], bus[1], DLC[1], data[DLC], trailing byte[1] |
| `01` | Time sync: reply `F1 01 micros[4]`; consumes no payload |
| `05` | Configure: two 32-bit bus words (second ignored); optional zero trailer ignored |
| `06` | Reply with two flag/speed slots; absent bus 1 is all zero |
| `07` | Six device-info bytes; local build number 1000 |
| `09` | Keepalive: reply `F1 09 DE AD` |
| `0B` | Software frame echo, same input shape as TX; never sends on CAN |
| `0C`, `0D` | One implemented bus; three absent extended-bus slots |
| `16` (decimal 22) | One bus with zero FD capabilities/mode/data rate |

Configuration word: low 20 bits = bitrate; bit 31 means flags are present,
bit 30 = enabled, bit 29 = listen-only. With bit 31 clear, a nonzero legacy
speed word enables the bus and retains the listen-only setting; zero disables.
RX packets are `F1 00 timestamp[4] ID[4] (bus<<4 | DLC) data[DLC] 00`.
ID bit 31 signals extended format in both directions. Oversized frames and FD
TX commands are drained and discarded rather than truncated or transmitted.

## Code layout

`main.cpp` shows the program's flow directly: start USB and CAN, handle
bootloader requests and disconnects, process host commands, forward received
CAN frames, and flush USB output. The supporting modules hold the details:

| Module | Responsibility |
| --- | --- |
| `main.cpp` | Startup, GVRET callbacks, and the complete polling loop |
| `gvret.h/.cpp` | Parse GVRET commands and pack replies and received frames |
| `can_bus.h/.cpp` | Apply runtime settings, restore settings after failure, guard transmission |
| `usb_transport.h/.cpp` | Buffer whole packets, reserve reply space, handle partial writes and 1200-baud requests |
| `esp32_io.h/.cpp` | Arduino CDC/TinyUSB and TWAI library calls |
| `can_types.h` | Shared classic CAN frame and settings types |

The portable modules use small driver interfaces. Desktop tests supply fake
drivers; firmware supplies the ESP32 drivers. No Arduino headers or board are
needed for the portable code.

## Tests without hardware

With PlatformIO and a desktop C++ compiler on your PATH, run:

```sh
pio test -e native
```

PlatformIO installs the Unity test framework on the first run. This command
builds and executes four suites on your computer; it does not access a serial
port or flash a board:

- `test_gvret`: SavvyCAN startup, exact replies, both ID widths, every DLC,
  embedded framing bytes, malformed/FD packet draining, echo and timer rollover.
- `test_can_bus`: runtime bitrate/listen-only/enable changes, unsupported rates,
  stop/start failures, configuration restoration and disabled transmission.
- `test_usb_transport`: partial and zero-progress writes, ring wraparound,
  whole-packet drops, reply reserves, disconnect cleanup and 1200-baud requests.
- `test_main_loop`: the actual `main.cpp` compiled with fake drivers and Arduino
  timing functions; checks commands, capture, reconnects, stalled-host
  backpressure and bootloader handling.

Run an individual suite with `pio test -e native -f test_usb_transport`.
The installer also has hardware-free JavaScript tests, run with Node.js 24:

```sh
node --test test/web_installer.test.mjs
```

`pio run` still builds only the ESP32-S3 firmware by default. The fake drivers
exercise application behavior; physical enumeration, DTR/RTS signaling, ROM
bootloader transitions and live CAN still require an ESP32-S3 and transceiver.
For hardware verification: capture, reopen the port several times, change
500k ↔ 250k on both ends, check that listen-only suppresses TX/ACK, and test a
native USB firmware upload without buttons.

## Continuous integration

[GitHub Actions](.github/workflows/ci.yml) runs on every push and pull request,
with a manual run option in the Actions tab. It runs `pio test -e native` and
`pio run -e esp32-s3-devkitc-1` as independent checks on Ubuntu, using PlatformIO
6.2.0 and cached dependencies. Both checks run without hardware or secrets.
Every successful firmware build also uploads `gvret-s3-factory.bin` directly
as the only artifact file, without ZIP packaging.
Artifacts are retained for 90 days. Choose a run with both checks passing;
the independent firmware job can publish an image even if native tests fail.

After both checks (including the web installer tests) pass on the default
branch, the workflow deploys the installer and that build's merged binary to
[GitHub Pages](https://evanwb.github.io/gvret-s3/). Pull requests and other
branches build and test without publishing the site.

For the first deployment, set **Settings → Pages → Build and deployment →
Source → GitHub Actions** in the repository, then push to the default branch
or run **Build and test** manually on that branch. The Pages job uses the normal
workflow token; no additional secret is required.

The static page is in `web/`; `scripts/build_installer.py` stages it with the
CI binary and commit version. To preview it locally after creating
`dist/gvret-s3-factory.bin`:

```sh
python3 scripts/build_installer.py --firmware dist/gvret-s3-factory.bin --version local
python3 -m http.server 8000 --directory _site
```

Open `http://localhost:8000`. Browser flashing requires HTTPS or localhost.

Protocol references (implementation written here, not vendored):
[ESP32RET handler](https://github.com/collin80/ESP32RET/blob/master/src/gvret_comm.cpp),
[frame packing](https://github.com/collin80/ESP32RET/blob/master/src/commbuffer.cpp),
[SavvyCAN host](https://github.com/collin80/SavvyCAN/blob/master/connections/gvretserial.cpp),
[M2RET protocol](https://github.com/collin80/M2RET/blob/master/CommProtocol.txt).
USB behavior can be checked in [Arduino's CDC implementation](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/USBCDC.cpp).
