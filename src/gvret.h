#pragma once
#include <stddef.h>
#include "can_types.h"

// The protocol has no Arduino dependencies; the adapter owns the actual settings.
struct GvretIO {
    virtual ~GvretIO() = default;
    virtual CanSettings settings() const = 0;
    virtual void configure(CanSettings next) = 0;
    virtual void transmit(const Frame &frame) = 0;
    virtual void send(const uint8_t *data, size_t size, bool frame) = 0;
};

class Gvret {
public:
    explicit Gvret(GvretIO &io) : io(io) {}
    void input(uint8_t byte, uint32_t now);
    void tick(uint32_t now);
    void reset();
    void received(const Frame &frame, uint32_t timestamp);

private:
    GvretIO &io;
    enum State { Idle, Command, Payload } state = Idle;
    bool binary = false;
    uint8_t command = 0, payload[14] = {};
    uint16_t used = 0, expected = 0;
    uint32_t lastByte = 0;
    void dispatch(uint32_t now);
    void complete(uint32_t now);
};
