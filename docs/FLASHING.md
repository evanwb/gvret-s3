# Flashing GVRET S3 factory firmware

Target: ESP32-S3-DevKitC-1 with **8 MB flash**. The merged
`gvret-s3-factory.bin` works on erased flash and is written at **address `0x0`**.
It contains all images used by the project's PlatformIO upload:

| Offset | Contents |
| --- | --- |
| `0x0000` | ESP32-S3 bootloader |
| `0x8000` | Partition table |
| `0xe000` | Initial OTA boot metadata (`boot_app0.bin`) |
| `0x10000` | GVRET application |

Gaps between images contain `0xFF`. The binary ends after the application;
it does not need to fill all 8 MB when programming erased flash. The separate
application-only `firmware.bin` is insufficient to boot an erased device.

## Download

Open the repository's **Actions** tab, select a **Build and test** run, and
download the `gvret-s3-factory.bin` artifact. It downloads directly as a binary;
there is nothing to extract.

Use a run where both the native tests and firmware build passed. The workflow
run identifies the exact revision built; artifacts are retained for 90 days.

## Program an erased device

Connect to the ESP32-S3's **ROM download port**, using native USB or a UART
programmer. Erasing flash leaves the ROM downloader intact. The device must be
in download mode; the application's 1200-baud request handler does not exist on
erased flash. A UART programmer with automatic EN/GPIO0 control can also enter
download mode without access to the board's buttons.

Install esptool, then run this from the directory containing the binary, replacing
`PORT` with the ROM download port (`COMx`, `/dev/ttyACM0`, or `/dev/cu.usbmodem...`):

```sh
python -m pip install "esptool>=4.9,<5"
python -m esptool --chip esp32s3 --port PORT --baud 460800 write_flash 0x0 gvret-s3-factory.bin
```

Esptool requests a reset after programming. If the application port does not
appear, power-cycle the device. The application's USB CDC port may have a
different name from the ROM port. Connect SavvyCAN to the application port.

The firmware defaults to **CAN TX=GPIO5, RX=GPIO4, 500 kbit/s**, normal mode.
For later source-based updates, close SavvyCAN and use `pio run -t upload` with
the application port; the project requests the bootloader with a 1200-baud touch.
