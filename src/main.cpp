#include <Arduino.h>
#include <BleSerial.h>
#include <stdarg.h>
#include <stdio.h>
#include "can_bus.h"
#include "esp32_io.h"
#include "gvret.h"
#include "usb_transport.h"

namespace {
CanBus can(twaiCanDriver());
UsbTransport usb(nativeUsbDriver());
BleSerial bleLog;
uint32_t receivedFrames = 0, transmittedFrames = 0, failedTransmits = 0;
uint32_t lastStatusMs = 0;
bool lastBleConnected = false, lastUsbReady = false;

void logMessage(const char *format, ...) {
    if (!bleLog.connected()) return;
    char message[192];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    // Flush a fragment left by a disconnect before writing another message.
    if (bleLog.getTransmitBufferLength()) bleLog.flush();
    // BleSerial 3.0.1 bulk writes require a larger MTU. Its byte API also works
    // with the default 23-byte ATT MTU and splits notifications internally.
    for (const char *p = message; *p; ++p)
        if (bleLog.write(uint8_t(*p)) != 1) return;
    bleLog.flush();
}

void logStatus() {
    if (!bleLog.connected()) return;
    const CanSettings settings = can.settings();
    logMessage("CAN %s, %lu bit/s, %s; USB %s; RX=%lu TX=%lu TX_failed=%lu\r\n",
                  settings.enabled ? "enabled" : "disabled",
                  static_cast<unsigned long>(settings.bitrate),
                  settings.listenOnly ? "listen-only" : "normal",
                  usb.ready() ? "connected" : "disconnected",
                  static_cast<unsigned long>(receivedFrames),
                  static_cast<unsigned long>(transmittedFrames),
                  static_cast<unsigned long>(failedTransmits));
}

// GVRET commands operate on CAN; replies and captured frames go to USB.
struct ProtocolIO : GvretIO {
    CanSettings settings() const override { return can.settings(); }
    void configure(CanSettings next) override {
        if (!can.configure(next) && bleLog.connected())
            logMessage("CAN configuration rejected: requested %lu bit/s\r\n",
                          static_cast<unsigned long>(next.bitrate));
        logStatus();
    }
    void transmit(const Frame &frame) override {
        if (can.transmit(frame)) ++transmittedFrames;
        else ++failedTransmits;
    }
    void send(const uint8_t *data, size_t size, bool frame) override {
        usb.send(data, size, frame);
    }
} protocolIO;
Gvret protocol(protocolIO);
}

void setup() {
    usb.begin(); // native USB-OTG CDC, ignoring DTR/RTS; 1200 baud enters loader
    can.begin(); // 500 kbit/s, enabled, normal mode
    bleLog.begin("GVRET S3 Logs", -1); // Nordic UART Service; no LED GPIO
    receivedFrames = transmittedFrames = failedTransmits = lastStatusMs = 0;
    lastBleConnected = lastUsbReady = false;
}

void loop() {
    if (usb.takeBootloaderRequest()) {
        can.shutdown();
        usb.enterBootloader();
        return;
    }

    if (!usb.ready()) {
        protocol.reset();
        usb.reset(); // discard stale input/output when USB disconnects
    } else {
        usb.pump();
        // Process host commands: discovery, keepalive, settings and CAN transmit.
        // Stop reading when reply space is low, so a stalled host loses no replies.
        for (unsigned i = 0; i < 128 && usb.canAcceptCommand() && usb.available() > 0; ++i)
            protocol.input(uint8_t(usb.read()), micros());
        protocol.tick(micros());
    }

    // Drain received CAN frames. GVRET streams them only after the host sends E7.
    Frame frame;
    for (unsigned i = 0; i < 64 && can.receive(frame); ++i) {
        ++receivedFrames;
        protocol.received(frame, micros());
    }

    usb.pump();

    // Low-volume diagnostics stay on BLE. A new client gets current state,
    // followed by a snapshot every five seconds; no per-frame BLE writes.
    const bool bleConnected = bleLog.connected(), usbReady = usb.ready();
    const uint32_t nowMs = millis();
    if (bleConnected && (!lastBleConnected || usbReady != lastUsbReady ||
                         uint32_t(nowMs - lastStatusMs) >= 5000)) {
        logStatus();
        lastStatusMs = nowMs;
    }
    lastBleConnected = bleConnected;
    lastUsbReady = usbReady;
    delay(1);
}
