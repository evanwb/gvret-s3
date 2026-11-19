#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>

// Only a deliberate line-coding request enters the loader; DTR/RTS are unrelated.
// The USB event task produces requests and the application loop consumes them.
class BootloaderTouch {
public:
    void lineCoding(uint32_t baud) {
        if (baud == 1200) requested.store(true, std::memory_order_relaxed);
    }
    bool take() { return requested.exchange(false, std::memory_order_relaxed); }
private:
    std::atomic<bool> requested{false};
};

struct UsbDriver {
    virtual ~UsbDriver() = default;
    virtual void begin() = 0;
    virtual bool ready() const = 0; // USB configured, regardless of DTR/RTS
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t writeAvailable() = 0;
    virtual size_t write(const uint8_t *data, size_t size) = 0;
    virtual void flush() = 0;
    virtual void clear() = 0;
    virtual bool takeBootloaderRequest() = 0;
    virtual void enterBootloader() = 0;
};

class UsbTransport {
public:
    explicit UsbTransport(UsbDriver &driver) : driver(driver) {}
    void begin() { driver.begin(); }
    bool ready() const { return driver.ready(); }
    int available() { return driver.available(); }
    int read() { return driver.read(); }
    bool canAcceptCommand() const { return freeSpace() >= 64; }
    bool send(const uint8_t *data, size_t size, bool frame);
    void pump();
    void reset();
    bool takeBootloaderRequest() { return driver.takeBootloaderRequest(); }
    void enterBootloader() { driver.enterBootloader(); }

private:
    UsbDriver &driver;
    // Only the application loop accesses this ring. Reserve space for replies.
    uint8_t output[4096] = {};
    size_t head = 0, tail = 0, pending = 0;
    size_t freeSpace() const { return sizeof(output) - pending; }
};
