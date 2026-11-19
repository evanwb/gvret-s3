#include <Arduino.h>
#include "can_bus.h"
#include "esp32_io.h"
#include "gvret.h"
#include "usb_transport.h"

namespace {
CanBus can(twaiCanDriver());
UsbTransport usb(nativeUsbDriver());

// GVRET commands operate on CAN; replies and captured frames go to USB.
struct ProtocolIO : GvretIO {
    CanSettings settings() const override { return can.settings(); }
    void configure(CanSettings next) override { can.configure(next); }
    void transmit(const Frame &frame) override { can.transmit(frame); }
    void send(const uint8_t *data, size_t size, bool frame) override {
        usb.send(data, size, frame);
    }
} protocolIO;
Gvret protocol(protocolIO);
}

void setup() {
    usb.begin(); // native USB-OTG CDC, ignoring DTR/RTS; 1200 baud enters loader
    can.begin(); // 500 kbit/s, enabled, normal mode
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
    for (unsigned i = 0; i < 64 && can.receive(frame); ++i)
        protocol.received(frame, micros());

    usb.pump();
    delay(1);
}
