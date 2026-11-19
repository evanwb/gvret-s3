#pragma once
#include "can_bus.h"
#include "usb_transport.h"
#include <algorithm>
#include <deque>
#include <limits>
#include <vector>

using Bytes = std::vector<uint8_t>;

struct FakeCan : CanDriver {
    bool started = false, writeOK = true;
    unsigned stops = 0, reads = 0;
    std::deque<bool> startResults, stopResults;
    std::vector<CanSettings> starts;
    std::deque<Frame> incoming;
    std::vector<Frame> outgoing;
    bool supports(uint32_t rate) const override {
        return rate == 125000 || rate == 250000 || rate == 500000;
    }
    static bool result(std::deque<bool> &results) {
        if (results.empty()) return true;
        const bool value = results.front();
        results.pop_front();
        return value;
    }
    bool stop() override {
        ++stops;
        const bool ok = result(stopResults);
        if (ok) started = false;
        return ok;
    }
    bool start(const CanSettings &settings) override {
        starts.push_back(settings);
        started = result(startResults);
        return started;
    }
    bool read(Frame &frame) override {
        ++reads;
        if (incoming.empty()) return false;
        frame = incoming.front();
        incoming.pop_front();
        return true;
    }
    bool write(const Frame &frame) override {
        outgoing.push_back(frame);
        return writeOK;
    }
};

struct FakeUsb : UsbDriver {
    bool connected = true, bootRequested = false;
    unsigned begins = 0, clears = 0, flushes = 0, bootEntries = 0;
    size_t space = 4096, writeLimit = std::numeric_limits<size_t>::max();
    std::deque<uint8_t> incoming;
    Bytes outgoing;
    void begin() override { ++begins; }
    bool ready() const override { return connected; }
    int available() override { return int(incoming.size()); }
    int read() override {
        if (incoming.empty()) return -1;
        const uint8_t byte = incoming.front();
        incoming.pop_front();
        return byte;
    }
    size_t writeAvailable() override { return space; }
    size_t write(const uint8_t *data, size_t size) override {
        size = std::min(size, writeLimit);
        outgoing.insert(outgoing.end(), data, data + size);
        return size;
    }
    void flush() override { ++flushes; }
    void clear() override { ++clears; incoming.clear(); }
    bool takeBootloaderRequest() override {
        const bool requested = bootRequested;
        bootRequested = false;
        return requested;
    }
    void enterBootloader() override { ++bootEntries; }
    void input(const Bytes &bytes) { incoming.insert(incoming.end(), bytes.begin(), bytes.end()); }
};
