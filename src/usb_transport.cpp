#include "usb_transport.h"
#include <algorithm>

bool UsbTransport::send(const uint8_t *data, size_t size, bool frame) {
    const size_t reserve = frame ? 128 : 0;
    if (size > freeSpace() || freeSpace() - size < reserve) return false;
    for (size_t i = 0; i < size; ++i) {
        output[head] = data[i];
        head = (head + 1) % sizeof(output);
    }
    pending += size;
    return true;
}

void UsbTransport::pump() {
    if (!ready()) return;
    size_t count = std::min(pending, sizeof(output) - tail);
    count = std::min(count, driver.writeAvailable());
    if (count) {
        const size_t sent = driver.write(output + tail, count);
        tail = (tail + sent) % sizeof(output);
        pending -= sent; // retain bytes after a short write
    }
    driver.flush();
}

void UsbTransport::reset() {
    head = tail = pending = 0;
    driver.clear();
}
