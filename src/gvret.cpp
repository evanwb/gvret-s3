#include "gvret.h"
#include <string.h>

namespace {
uint32_t get32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 |
           uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = value >> (8 * i);
}
}

void Gvret::reset() {
    state = Idle;
    binary = false;
    used = expected = 0;
}

void Gvret::tick(uint32_t now) {
    // Unsigned subtraction also works across micros() rollover.
    if (state != Idle && uint32_t(now - lastByte) > 250000) state = Idle;
}

void Gvret::input(uint8_t byte, uint32_t now) {
    tick(now);
    lastByte = now;
    if (state == Idle) {
        if (byte == 0xE7) binary = true;
        else if (byte == 0xF1) state = Command;
        return; // Text is deliberately silent.
    }
    if (state == Command) {
        command = byte;
        state = Idle;
        used = expected = 0;
        dispatch(now);
        if (expected) state = Payload;
        return;
    }
    // Keep only the classic header/data; drain oversized or FD packets in full.
    // F1/E7 inside payloads are ordinary data, never framing characters.
    if (used < sizeof(payload)) payload[used] = byte;
    ++used;
    if ((command == 0 || command == 11 || command == 20) && used == 6)
        expected = 7 + (command == 20 ? (byte & 0x7F) : byte);
    if (used == expected) {
        state = Idle;
        complete(now);
    }
}

void Gvret::dispatch(uint32_t now) {
    uint8_t reply[18] = {0xF1, command};
    size_t size = 0;
    switch (command) {
    case 0: case 11: case 20: expected = 6; break; // ID, bus, DLC, data, trailer
    case 1: put32(reply + 2, now); size = 6; break; // no input payload!
    case 2: size = 4; reply[3] = 0xF1 ^ 2; break; // no digital inputs
    case 3: size = 11; reply[10] = 0xF1 ^ 3; break; // four zero analog inputs
    case 4: case 8: case 10: expected = 1; break; // unsupported outputs/SW/system
    case 5: expected = 8; break; // optional legacy checksum is ignored in Idle
    case 6: {
        const CanSettings bus = io.settings();
        reply[2] = (bus.enabled ? 1 : 0) | (bus.listenOnly ? 0x10 : 0);
        put32(reply + 3, bus.bitrate);
        size = 12; // mandatory second bus slot, disabled with speed zero
        break;
    }
    case 7:
        reply[2] = 0xE8; reply[3] = 3; // local build 1000, not an upstream version
        reply[4] = 0x20;
        size = 8;
        break;
    case 9: reply[2] = 0xDE; reply[3] = 0xAD; size = 4; break;
    case 12: reply[2] = 1; size = 3; break;
    case 13: size = 17; break; // three absent extended buses: flags + speed each
    case 14: expected = 12; break;
    case 21: expected = 6; break; // consume but never enable FD
    case 22: reply[2] = 1; size = 8; break; // one bus, FD capability/mode/rate zero
    default: break;
    }
    if (size) io.send(reply, size, false);
}

void Gvret::complete(uint32_t now) {
    if (command == 5) {
        const uint32_t word = get32(payload);
        CanSettings next = io.settings();
        const uint32_t rate = word & 0xFFFFF;
        if (rate) next.bitrate = rate;
        next.enabled = word & 0x80000000 ? bool(word & 0x40000000) : word != 0;
        if (word & 0x80000000) next.listenOnly = word & 0x20000000;
        io.configure(next); // bus 1 is absent; do not interpret its settings
    } else if (command == 0 || command == 11) {
        const uint32_t wireId = get32(payload);
        Frame frame;
        frame.extended = wireId & 0x80000000;
        frame.id = wireId & 0x7FFFFFFF;
        frame.dlc = payload[5];
        if (payload[4] != 0 || frame.dlc > 8 ||
            frame.id > (frame.extended ? 0x1FFFFFFFul : 0x7FFul)) return;
        memcpy(frame.data, payload + 6, frame.dlc);
        if (command == 11) received(frame, now); // software echo, no CAN transmission
        else {
            const CanSettings bus = io.settings();
            if (bus.enabled && !bus.listenOnly) io.transmit(frame);
        }
    }
}

void Gvret::received(const Frame &frame, uint32_t timestamp) {
    if (!binary || frame.dlc > 8) return;
    uint8_t packet[20] = {0xF1, 0};
    put32(packet + 2, timestamp);
    put32(packet + 6, frame.id | (frame.extended ? 0x80000000ul : 0));
    packet[10] = frame.dlc; // bus 0 occupies the upper nibble
    memcpy(packet + 11, frame.data, frame.dlc);
    io.send(packet, 12 + frame.dlc, true); // includes the required zero trailer
}
