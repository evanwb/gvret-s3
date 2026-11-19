#include <unity.h>
#include "../fakes.h"
#include "esp32_io.h"

namespace {
FakeCan fakeCan;
FakeUsb fakeUsb;
unsigned long fakeTimeUs = 0x12345678;
}
CanDriver &twaiCanDriver() { return fakeCan; }
UsbDriver &nativeUsbDriver() { return fakeUsb; }
unsigned long micros() { return fakeTimeUs; }
unsigned long millis() { return fakeTimeUs / 1000; }
void delay(unsigned long) {}

// Compile the actual sketch here so these tests exercise setup()/loop(), rather
// than maintaining a separate copy of its orchestration logic.
#include "../../src/main.cpp"

namespace {
struct Fixture {
    FakeCan &canDriver = fakeCan;
    FakeUsb &usbDriver = fakeUsb;
    Fixture() {
        canDriver = FakeCan{};
        usbDriver = FakeUsb{};
        bleLog = BleSerial{};
        fakeTimeUs = 0x12345678;
        // Reset the sketch's protocol/queue state between independent tests.
        usbDriver.connected = false;
        loop();
        usbDriver = FakeUsb{};
        setup();
    }
};
}
void setUp() {}
void tearDown() {}

void test_protocol_commands_reconfigure_real_bus_policy_and_read_it_back() {
    Fixture f;
    TEST_ASSERT_EQUAL_UINT(1, f.usbDriver.begins);
    // 250k, enabled, listen-only, followed immediately by a query and TX request.
    f.usbDriver.input({0xE7, 0xF1, 5, 0x90, 0xD0, 3, 0xE0, 0, 0, 0, 0, 0,
                      0xF1, 6, 0xF1, 0, 0x23, 1, 0, 0, 0, 0, 0});
    loop();
    TEST_ASSERT_TRUE(f.canDriver.starts.back().listenOnly);
    TEST_ASSERT_EQUAL_UINT32(250000, f.canDriver.starts.back().bitrate);
    TEST_ASSERT_TRUE(f.canDriver.outgoing.empty());
    TEST_ASSERT_TRUE(f.usbDriver.outgoing == Bytes({0xF1, 6, 0x11, 0x90, 0xD0, 3, 0,
                                                   0, 0, 0, 0, 0}));
    // An unsupported bitrate must be reported as the previous effective settings.
    f.usbDriver.outgoing.clear();
    f.usbDriver.input({0xF1, 5, 0x40, 0xE2, 1, 0xC0, 0, 0, 0, 0, 0, 0xF1, 6});
    loop();
    TEST_ASSERT_TRUE(f.usbDriver.outgoing == Bytes({0xF1, 6, 0x11, 0x90, 0xD0, 3, 0,
                                                   0, 0, 0, 0, 0}));
}

void test_live_capture_starts_after_e7_and_reconnect_requires_new_session() {
    Fixture f;
    Frame frame;
    frame.id = 0x123;
    frame.dlc = 1;
    frame.data[0] = 0xE7;
    f.canDriver.incoming.push_back(frame);
    loop();
    TEST_ASSERT_TRUE(f.usbDriver.outgoing.empty());
    f.usbDriver.input({0xE7});
    f.canDriver.incoming.push_back(frame);
    loop();
    const Bytes received = {0xF1, 0, 0x78, 0x56, 0x34, 0x12, 0x23, 1, 0, 0, 1, 0xE7, 0};
    TEST_ASSERT_TRUE(f.usbDriver.outgoing == received);
    f.usbDriver.outgoing.clear();
    f.usbDriver.connected = false;
    f.usbDriver.input({0xF1, 0}); // unfinished/stale input must be discarded
    loop();
    TEST_ASSERT_EQUAL_UINT(1, f.usbDriver.clears);
    TEST_ASSERT_TRUE(f.usbDriver.incoming.empty());
    TEST_ASSERT_TRUE(can.settings().enabled); // USB disconnect does not stop CAN
    f.usbDriver.connected = true;
    f.canDriver.incoming.push_back(frame);
    loop();
    TEST_ASSERT_TRUE(f.usbDriver.outgoing.empty());
    f.usbDriver.input({0xE7, 0xF1, 9});
    f.canDriver.incoming.push_back(frame);
    loop();
    Bytes expected = {0xF1, 9, 0xDE, 0xAD};
    expected.insert(expected.end(), received.begin(), received.end());
    TEST_ASSERT_TRUE(f.usbDriver.outgoing == expected);
}

void test_stalled_usb_backpressures_commands_and_eventually_replies_in_order() {
    Fixture f;
    f.usbDriver.space = 0;
    const unsigned requests = 1200;
    Bytes input;
    for (unsigned i = 0; i < requests; ++i) input.insert(input.end(), {0xF1, 9});
    f.usbDriver.input(input);
    for (unsigned i = 0; i < 25; ++i) loop();
    TEST_ASSERT_FALSE(f.usbDriver.incoming.empty());
    const size_t remaining = f.usbDriver.incoming.size();
    loop();
    TEST_ASSERT_EQUAL_UINT(remaining, f.usbDriver.incoming.size());
    f.usbDriver.space = 4096;
    for (unsigned i = 0; i < 30; ++i) loop();
    TEST_ASSERT_TRUE(f.usbDriver.incoming.empty());
    Bytes expected;
    for (unsigned i = 0; i < requests; ++i) expected.insert(expected.end(), {0xF1, 9, 0xDE, 0xAD});
    TEST_ASSERT_TRUE(f.usbDriver.outgoing == expected);
}

void test_bootloader_request_stops_can_before_entering_loader() {
    Fixture f;
    f.usbDriver.bootRequested = true;
    f.usbDriver.input({0xF1, 9});
    loop();
    TEST_ASSERT_FALSE(f.canDriver.started);
    TEST_ASSERT_FALSE(can.settings().enabled);
    TEST_ASSERT_EQUAL_UINT(1, f.usbDriver.bootEntries);
    TEST_ASSERT_TRUE(f.usbDriver.outgoing.empty());
    TEST_ASSERT_EQUAL_UINT(2, f.usbDriver.incoming.size());
}

void test_ble_diagnostics_are_separate_from_usb_and_repeat_at_five_seconds() {
    Fixture f;
    TEST_ASSERT_EQUAL_STRING("GVRET S3 Logs", bleLog.name.c_str());
    TEST_ASSERT_EQUAL_INT(-1, bleLog.ledPin);
    loop();
    TEST_ASSERT_TRUE(bleLog.lines.empty());
    bleLog.linked = true;
    loop();
    TEST_ASSERT_EQUAL_UINT(1, bleLog.lines.size());
    TEST_ASSERT_TRUE(bleLog.lines.back().find("500000 bit/s") != std::string::npos);
    TEST_ASSERT_TRUE(f.usbDriver.outgoing.empty());
    for (unsigned i = 0; i < 20; ++i) loop();
    TEST_ASSERT_EQUAL_UINT(1, bleLog.lines.size());
    fakeTimeUs += 5000000;
    loop();
    TEST_ASSERT_EQUAL_UINT(2, bleLog.lines.size());
    bleLog.linked = false;
    fakeTimeUs += 5000000;
    loop();
    TEST_ASSERT_EQUAL_UINT(2, bleLog.lines.size());
    bleLog.linked = true;
    loop();
    TEST_ASSERT_EQUAL_UINT(3, bleLog.lines.size());
}

void test_ble_reports_effective_can_settings_and_rejection() {
    Fixture f;
    bleLog.linked = true;
    f.usbDriver.input({0xF1, 5, 0x90, 0xD0, 3, 0xE0, 0, 0, 0, 0, 0, 0xF1, 6});
    loop();
    TEST_ASSERT_TRUE(bleLog.lines.front().find("250000 bit/s, listen-only") != std::string::npos);
    TEST_ASSERT_TRUE(f.usbDriver.outgoing == Bytes({0xF1, 6, 0x11, 0x90, 0xD0, 3, 0,
                                                   0, 0, 0, 0, 0}));
    bleLog.lines.clear();
    f.usbDriver.input({0xF1, 5, 0x40, 0xE2, 1, 0xC0, 0, 0, 0, 0, 0});
    loop();
    TEST_ASSERT_TRUE(bleLog.lines.front().find("configuration rejected: requested 123456") != std::string::npos);
    TEST_ASSERT_TRUE(bleLog.lines.back().find("250000 bit/s, listen-only") != std::string::npos);
}

void test_ble_summarizes_frames_and_failed_tx_without_per_frame_logging() {
    Fixture f;
    bleLog.linked = true;
    f.canDriver.writeOK = false;
    f.usbDriver.input({0xF1, 0, 0x23, 1, 0, 0, 0, 0, 0});
    for (unsigned i = 0; i < 20; ++i) f.canDriver.incoming.push_back(Frame{});
    loop();
    TEST_ASSERT_EQUAL_UINT(1, bleLog.lines.size());
    TEST_ASSERT_TRUE(bleLog.lines.back().find("RX=20 TX=0 TX_failed=1") != std::string::npos);
    TEST_ASSERT_TRUE(f.usbDriver.outgoing.empty());
}

void test_ble_formatting_is_bounded_and_pending_fragments_are_flushed() {
    Fixture f;
    bleLog.linked = true;
    bleLog.pending = "previous fragment";
    logMessage("message %u\r\n", 42u);
    TEST_ASSERT_EQUAL_UINT(2, bleLog.lines.size());
    TEST_ASSERT_EQUAL_STRING("previous fragment", bleLog.lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("message 42\r\n", bleLog.lines[1].c_str());
    const std::string longMessage(512, 'x');
    logMessage("%s", longMessage.c_str());
    TEST_ASSERT_EQUAL_UINT(191, bleLog.lines.back().size());
    TEST_ASSERT_TRUE(bleLog.pending.empty());
    TEST_ASSERT_TRUE(f.usbDriver.outgoing.empty());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_protocol_commands_reconfigure_real_bus_policy_and_read_it_back);
    RUN_TEST(test_live_capture_starts_after_e7_and_reconnect_requires_new_session);
    RUN_TEST(test_stalled_usb_backpressures_commands_and_eventually_replies_in_order);
    RUN_TEST(test_bootloader_request_stops_can_before_entering_loader);
    RUN_TEST(test_ble_diagnostics_are_separate_from_usb_and_repeat_at_five_seconds);
    RUN_TEST(test_ble_reports_effective_can_settings_and_rejection);
    RUN_TEST(test_ble_summarizes_frames_and_failed_tx_without_per_frame_logging);
    RUN_TEST(test_ble_formatting_is_bounded_and_pending_fragments_are_flushed);
    return UNITY_END();
}
