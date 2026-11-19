#include "can_bus.h"

bool CanBus::configure(CanSettings next) {
    if (!driver.supports(next.bitrate)) return false;
    const CanSettings previous = bus;
    if (!driver.stop()) return false;
    running = false;
    if (!next.enabled || (running = driver.start(next))) {
        bus = next;
        return true;
    }
    // A failed restart must not disconnect USB. Restore the prior bus when
    // possible, and report disabled if restoration fails as well.
    running = driver.stop() && previous.enabled && driver.start(previous);
    bus = previous;
    bus.enabled = running;
    return false;
}

bool CanBus::shutdown() {
    if (!driver.stop()) return false;
    running = bus.enabled = false;
    return true;
}

bool CanBus::transmit(const Frame &frame) {
    if (!running || bus.listenOnly || frame.dlc > 8 ||
        frame.id > (frame.extended ? 0x1FFFFFFFul : 0x7FFul)) return false;
    return driver.write(frame);
}
