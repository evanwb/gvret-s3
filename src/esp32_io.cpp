#include "esp32_io.h"
#include <Arduino.h>
#include <USB.h>
#include <USBCDC.h>
#include <ESP32-TWAI-CAN.hpp>
#include <esp32-hal-tinyusb.h>

#if ARDUINO_USB_MODE != 0 || ARDUINO_USB_CDC_ON_BOOT
#error Use the platformio.ini USB-OTG configuration and explicit CDC startup
#endif

namespace {
BootloaderTouch bootloaderTouch;
void onLineCoding(void *, esp_event_base_t, int32_t, void *eventData) {
    const auto *event = static_cast<const arduino_usb_cdc_event_data_t *>(eventData);
    bootloaderTouch.lineCoding(event->line_coding.bit_rate);
}

class NativeUsb : public UsbDriver {
public:
    void begin() override {
        // Set policy and register the 1200-baud handler BEFORE enumeration.
        cdc.enableReboot(false);
        cdc.onEvent(ARDUINO_USB_CDC_LINE_CODING_EVENT, onLineCoding);
        cdc.setRxBufferSize(1024);
        cdc.begin(115200); // USB line coding does not set CAN bitrate
        USB.productName("GVRET S3");
        USB.begin();
    }
    bool ready() const override { return tud_ready(); }
    int available() override { return cdc.available(); }
    int read() override { return cdc.read(); }
    size_t writeAvailable() override { return tud_cdc_n_write_available(0); }
    size_t write(const uint8_t *data, size_t size) override {
        // USBCDC::write/availableForWrite require DTR, which SavvyCAN clears.
        // The transport checks FIFO space to avoid DTR-low overwrite mode.
        return tud_cdc_n_write(0, data, size);
    }
    void flush() override { tud_cdc_n_write_flush(0); }
    void clear() override {
        tud_cdc_n_write_clear(0);
        while (cdc.available() > 0) cdc.read();
    }
    bool takeBootloaderRequest() override { return bootloaderTouch.take(); }
    void enterBootloader() override { usb_persist_restart(RESTART_BOOTLOADER); }
private:
    USBCDC cdc{0};
};

bool timingFor(uint32_t rate, twai_timing_config_t &timing) {
    switch (rate) {
    case 12500: timing = TWAI_TIMING_CONFIG_12_5KBITS(); return true;
    case 16000: timing = TWAI_TIMING_CONFIG_16KBITS(); return true;
    case 20000: timing = TWAI_TIMING_CONFIG_20KBITS(); return true;
    case 25000: timing = TWAI_TIMING_CONFIG_25KBITS(); return true;
    case 50000: timing = TWAI_TIMING_CONFIG_50KBITS(); return true;
    case 100000: timing = TWAI_TIMING_CONFIG_100KBITS(); return true;
    case 125000: timing = TWAI_TIMING_CONFIG_125KBITS(); return true;
    case 250000: timing = TWAI_TIMING_CONFIG_250KBITS(); return true;
    case 500000: timing = TWAI_TIMING_CONFIG_500KBITS(); return true;
    case 800000: timing = TWAI_TIMING_CONFIG_800KBITS(); return true;
    case 1000000: timing = TWAI_TIMING_CONFIG_1MBITS(); return true;
    default: return false; // do not silently map unsupported rates to 500k
    }
}

class TwaiCan : public CanDriver {
public:
    bool supports(uint32_t bitrate) const override {
        twai_timing_config_t timing;
        return timingFor(bitrate, timing);
    }
    bool stop() override {
        if (ESP32Can.end()) return true;
        // Library 1.0.1 can report failure after a successful uninstall (BUS_OFF).
        // Reset its bookkeeping only after verifying that no driver remains.
        twai_status_info_t status;
        if (twai_get_status_info(&status) != ESP_ERR_INVALID_STATE) return false;
        ESP32Can = TwaiCAN();
        return true;
    }
    bool start(const CanSettings &settings) override {
        twai_timing_config_t timing;
        if (!timingFor(settings.bitrate, timing)) return false;
        twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
            gpio_num_t(CAN_TX_PIN), gpio_num_t(CAN_RX_PIN),
            settings.listenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
        general.tx_queue_len = 32;
        general.rx_queue_len = 128;
        // Explicit timing also supplies 25k/50k, absent from library 1.0.1's enum.
        return ESP32Can.begin(TWAI_SPEED_500KBPS, CAN_TX_PIN, CAN_RX_PIN, 32, 128,
                              nullptr, &general, &timing);
    }
    bool read(Frame &frame) override {
        CanFrame rx;
        for (unsigned i = 0; i < 64 && ESP32Can.readFrame(rx, 0); ++i) {
            // Drain remote requests too, so they cannot starve data frames.
            // Classic GVRET has no RTR flag; never mislabel a remote request.
            if (rx.rtr || rx.data_length_code > 8) continue;
            frame.id = rx.identifier;
            frame.extended = rx.extd;
            frame.dlc = rx.data_length_code;
            memcpy(frame.data, rx.data, frame.dlc);
            return true;
        }
        return false;
    }
    bool write(const Frame &frame) override {
        CanFrame tx = {};
        tx.identifier = frame.id;
        tx.extd = frame.extended;
        tx.data_length_code = frame.dlc;
        memcpy(tx.data, frame.data, frame.dlc);
        return ESP32Can.writeFrame(tx, 0); // full queues must not block keepalive
    }
};
}

CanDriver &twaiCanDriver() {
    static TwaiCan driver;
    return driver;
}
UsbDriver &nativeUsbDriver() {
    static NativeUsb driver;
    return driver;
}
