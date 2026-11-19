#pragma once
#include "can_types.h"

// All driver operations are nonblocking. Tests provide an in-memory driver.
struct CanDriver {
    virtual ~CanDriver() = default;
    virtual bool supports(uint32_t bitrate) const = 0;
    virtual bool stop() = 0;
    virtual bool start(const CanSettings &settings) = 0;
    virtual bool read(Frame &frame) = 0;
    virtual bool write(const Frame &frame) = 0;
};

class CanBus {
public:
    explicit CanBus(CanDriver &driver) : driver(driver) { bus.enabled = false; }
    bool begin() { return configure(CanSettings{}); }
    bool configure(CanSettings next);
    bool shutdown();
    bool receive(Frame &frame) { return running && driver.read(frame); }
    bool transmit(const Frame &frame);
    CanSettings settings() const { return bus; }

private:
    CanDriver &driver;
    CanSettings bus;
    bool running = false;
};
