#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

// Only the BleSerial calls used by the sketch. Tests control the connection and
// capture diagnostics while exercising the actual main.cpp without a radio.
class BleSerial {
public:
    bool linked = false;
    int ledPin = -1;
    std::string name;
    std::string pending;
    std::vector<std::string> lines;
    void begin(const char *deviceName, int pin = -1) { name = deviceName; ledPin = pin; }
    bool connected() const { return linked; }
    size_t getTransmitBufferLength() const { return pending.size(); }
    size_t write(uint8_t byte) {
        if (!linked) return 0;
        pending += char(byte);
        return 1;
    }
    void flush() {
        if (!linked || pending.empty()) return;
        lines.push_back(pending);
        pending.clear();
    }
};
