#include "gvret.h"
#include <unity.h>
#include <vector>

using Bytes = std::vector<uint8_t>;
struct FakeIO : GvretIO {
    CanSettings bus;
    CanSettings settings() const override { return bus; }
    Bytes output;
    std::vector<Frame> transmitted;
    unsigned configurations = 0;
    void configure(CanSettings next) override { bus = next; ++configurations; }
    void transmit(const Frame &frame) override { transmitted.push_back(frame); }
    void send(const uint8_t *data, size_t size, bool) override {
        output.insert(output.end(), data, data + size);
    }
};
void feed(Gvret &protocol, const Bytes &bytes, uint32_t now = 0x12345678) {
    for (uint8_t b : bytes) protocol.input(b, now);
}
Bytes txPacket(uint32_t id, unsigned dlc, uint8_t bus = 0, uint8_t command = 0) {
    Bytes p = {0xF1, command, uint8_t(id), uint8_t(id >> 8), uint8_t(id >> 16),
               uint8_t(id >> 24), bus, uint8_t(dlc)};
    for (unsigned i = 0; i < dlc; ++i) p.push_back(i % 2 ? 0xE7 : 0xF1);
    p.push_back(0);
    return p;
}
void configure(Gvret &protocol, uint32_t word) {
    feed(protocol, {0xF1, 5, uint8_t(word), uint8_t(word >> 8), uint8_t(word >> 16),
                    uint8_t(word >> 24), 0, 0, 0, 0, 0});
}

void setUp() {}
void tearDown() {}

void test_savvycan_startup_and_capabilities() {
    FakeIO io;
    Gvret protocol(io);
    Frame frame;
    protocol.received(frame, 0);
    TEST_ASSERT_TRUE(io.output.empty()); // no stream before E7

    // Exactly the consecutive commands from SavvyCAN's deviceConnected().
    feed(protocol, {0xE7, 0xE7, 0xF1, 12, 0xF1, 6, 0xF1, 7, 0xF1, 1, 0xF1, 9});
    TEST_ASSERT_TRUE(io.output == Bytes({0xF1, 12, 1,
        0xF1, 6, 1, 0x20, 0xA1, 7, 0, 0, 0, 0, 0, 0,
        0xF1, 7, 0xE8, 3, 0x20, 0, 0, 0,
        0xF1, 1, 0x78, 0x56, 0x34, 0x12, 0xF1, 9, 0xDE, 0xAD}));
    io.output.clear();
    feed(protocol, {0xF1, 13, 0xF1, 22});
    Bytes capabilities(17, 0);
    capabilities[0] = 0xF1; capabilities[1] = 13;
    capabilities.insert(capabilities.end(), {0xF1, 22, 1, 0, 0, 0, 0, 0});
    TEST_ASSERT_TRUE(io.output == capabilities);
}

void test_all_classic_frame_lengths_and_id_widths() {
    FakeIO io;
    Gvret protocol(io);
    feed(protocol, {0xE7});
    // Both ID widths, every DLC including zero, and framing markers in payload.
    for (uint32_t wireId : {0x7FFul, 0x9FFFFFFFul}) {
        for (unsigned dlc = 0; dlc <= 8; ++dlc) {
            Bytes packet = txPacket(wireId, dlc);
            const size_t count = io.transmitted.size();
            for (size_t i = 0; i < packet.size() - 1; ++i) {
                protocol.input(packet[i], 0x12345678 + uint32_t(i));
                TEST_ASSERT_TRUE(io.transmitted.size() == count); // wait for trailer
            }
            protocol.input(packet.back(), 0x12345690);
            TEST_ASSERT_TRUE(io.transmitted.size() == count + 1);
            const Frame &sent = io.transmitted.back();
            TEST_ASSERT_TRUE(sent.id == (wireId & 0x7FFFFFFF));
            TEST_ASSERT_TRUE(sent.extended == bool(wireId & 0x80000000));
            TEST_ASSERT_TRUE(sent.dlc == dlc);
            io.output.clear();
            protocol.received(sent, 0xE7F11234);
            Bytes expected = {0xF1, 0, 0x34, 0x12, 0xF1, 0xE7,
                uint8_t(wireId), uint8_t(wireId >> 8), uint8_t(wireId >> 16),
                uint8_t(wireId >> 24), uint8_t(dlc)};
            expected.insert(expected.end(), packet.begin() + 8, packet.end());
            TEST_ASSERT_TRUE(io.output == expected);
        }
    }
}

void test_runtime_configuration_and_transmit_guards() {
    FakeIO io;
    Gvret protocol(io);
    feed(protocol, {0xE7});
    // Runtime flags and legacy bitrate-only format. No transmit in listen-only.
    configure(protocol, 0xE003D090); // enabled, listen-only, 250000
    TEST_ASSERT_TRUE(io.bus.bitrate == 250000 && io.bus.enabled && io.bus.listenOnly);
    const size_t count = io.transmitted.size();
    feed(protocol, txPacket(0x123, 8));
    TEST_ASSERT_TRUE(io.transmitted.size() == count);
    io.output.clear();
    feed(protocol, {0xF1, 6});
    TEST_ASSERT_TRUE(io.output == Bytes({0xF1, 6, 0x11, 0x90, 0xD0, 3, 0, 0, 0, 0, 0, 0}));
    configure(protocol, 0x80000000); // disabled, keep last bitrate
    TEST_ASSERT_TRUE(!io.bus.enabled && !io.bus.listenOnly && io.bus.bitrate == 250000);
    feed(protocol, txPacket(0x123, 0));
    TEST_ASSERT_TRUE(io.transmitted.size() == count);
    configure(protocol, 500000);
    TEST_ASSERT_TRUE(io.bus.enabled && io.bus.bitrate == 500000);
}

void test_invalid_and_fd_frames_drain_without_losing_sync() {
    FakeIO io;
    Gvret protocol(io);
    feed(protocol, {0xE7});
    const size_t count = 0;
    // Reject IDs, other buses, oversized classic and all FD packets. Drain their
    // embedded F1/E7 bytes without treating them as commands, then reply normally.
    for (const Bytes &p : {txPacket(0x800, 1), txPacket(0xA0000000, 1),
                          txPacket(1, 1, 1), txPacket(1, 255), txPacket(1, 64, 0, 20)}) {
        io.output.clear();
        feed(protocol, p);
        TEST_ASSERT_TRUE(io.transmitted.size() == count);
        TEST_ASSERT_TRUE(io.output.empty());
        feed(protocol, {0xF1, 9});
        TEST_ASSERT_TRUE(io.output == Bytes({0xF1, 9, 0xDE, 0xAD}));
    }
    io.output.clear();
    feed(protocol, txPacket(0x123, 0, 0, 11));
    TEST_ASSERT_TRUE(io.transmitted.size() == count && io.output.size() == 12); // echo only
}

void test_timeout_rollover_and_session_reset() {
    FakeIO io;
    Gvret protocol(io);
    feed(protocol, {0xE7});
    Frame frame;
    // Truncated input expires across timer rollover. Long quiet capture remains
    // binary; hosts can disable validation. USB reset explicitly ends the session.
    feed(protocol, {0xF1, 0, 0xAA}, 0xFFFFFF00);
    protocol.tick(0x40000);
    io.output.clear();
    feed(protocol, {0xF1, 9}, 0x40001);
    TEST_ASSERT_TRUE(io.output == Bytes({0xF1, 9, 0xDE, 0xAD}));
    protocol.tick(0x1000000);
    io.output.clear();
    protocol.received(frame, 0);
    TEST_ASSERT_TRUE(io.output.size() == 12);
    protocol.reset();
    io.output.clear();
    protocol.received(frame, 0);
    TEST_ASSERT_TRUE(io.output.empty());
    feed(protocol, {0xE7});
    protocol.received(frame, 0);
    TEST_ASSERT_TRUE(io.output.size() == 12);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_savvycan_startup_and_capabilities);
    RUN_TEST(test_all_classic_frame_lengths_and_id_widths);
    RUN_TEST(test_runtime_configuration_and_transmit_guards);
    RUN_TEST(test_invalid_and_fd_frames_drain_without_losing_sync);
    RUN_TEST(test_timeout_rollover_and_session_reset);
    return UNITY_END();
}
