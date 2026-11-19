#include <unity.h>
#include "../fakes.h"

void setUp() {}
void tearDown() {}

void test_short_writes_and_ring_wrap_preserve_byte_order() {
    FakeUsb driver;
    UsbTransport usb(driver);
    const Bytes first(3000, 0xF1), second(1800, 0xE7);
    TEST_ASSERT_TRUE(usb.send(first.data(), first.size(), false));
    driver.writeLimit = 1000;
    usb.pump();
    TEST_ASSERT_EQUAL_UINT(1000, driver.outgoing.size());
    TEST_ASSERT_TRUE(usb.send(second.data(), second.size(), false));
    for (unsigned i = 0; i < 8; ++i) usb.pump();
    Bytes expected = first;
    expected.insert(expected.end(), second.begin(), second.end());
    TEST_ASSERT_TRUE(driver.outgoing == expected);
}

void test_full_queue_drops_whole_frames_and_reserves_reply_space() {
    FakeUsb driver;
    UsbTransport usb(driver);
    const Bytes frame(20, 0xA5), reply(100, 0x09);
    unsigned count = 0;
    while (usb.send(frame.data(), frame.size(), true)) ++count;
    TEST_ASSERT_EQUAL_UINT(198, count);
    TEST_ASSERT_TRUE(usb.canAcceptCommand());
    TEST_ASSERT_TRUE(usb.send(reply.data(), reply.size(), false));
    TEST_ASSERT_FALSE(usb.canAcceptCommand());
    TEST_ASSERT_FALSE(usb.send(reply.data(), reply.size(), false));
    const uint8_t byte = 0;
    TEST_ASSERT_FALSE(usb.send(&byte, std::numeric_limits<size_t>::max(), true));
    for (unsigned i = 0; i < 3; ++i) usb.pump();
    Bytes expected(count * frame.size(), 0xA5);
    expected.insert(expected.end(), reply.begin(), reply.end());
    TEST_ASSERT_TRUE(driver.outgoing == expected);
}

void test_disconnected_full_fifo_and_zero_progress_writes_do_not_block() {
    FakeUsb driver;
    UsbTransport usb(driver);
    const Bytes packet = {0xF1, 9, 0xDE, 0xAD};
    usb.send(packet.data(), packet.size(), false);
    driver.connected = false;
    usb.pump();
    TEST_ASSERT_TRUE(driver.outgoing.empty());
    TEST_ASSERT_EQUAL_UINT(0, driver.flushes);
    driver.connected = true;
    driver.space = 0;
    usb.pump();
    driver.space = 1;
    driver.writeLimit = 0;
    usb.pump();
    TEST_ASSERT_TRUE(driver.outgoing.empty());
    driver.space = 4096;
    driver.writeLimit = 4096;
    usb.pump();
    TEST_ASSERT_TRUE(driver.outgoing == packet);
}

void test_reset_discards_pending_output_and_stale_input() {
    FakeUsb driver;
    UsbTransport usb(driver);
    const Bytes old = {1, 2, 3, 4}, fresh = {0xF1, 9, 0xDE, 0xAD};
    usb.send(old.data(), old.size(), false);
    driver.writeLimit = 2;
    usb.pump();
    driver.input({0xF1, 0});
    usb.reset();
    TEST_ASSERT_EQUAL_UINT(1, driver.clears);
    TEST_ASSERT_EQUAL_INT(0, usb.available());
    TEST_ASSERT_TRUE(usb.canAcceptCommand());
    usb.send(fresh.data(), fresh.size(), false);
    driver.writeLimit = 4096;
    usb.pump();
    TEST_ASSERT_TRUE(driver.outgoing == Bytes({1, 2, 0xF1, 9, 0xDE, 0xAD}));
}

void test_only_1200_baud_requests_bootloader_and_request_is_consumed_once() {
    BootloaderTouch touch;
    TEST_ASSERT_FALSE(touch.take());
    for (uint32_t baud : {9600, 115200, 1000000}) {
        touch.lineCoding(baud);
        TEST_ASSERT_FALSE(touch.take());
    }
    touch.lineCoding(1200);
    touch.lineCoding(1000000); // a subsequent event must not erase the request
    TEST_ASSERT_TRUE(touch.take());
    TEST_ASSERT_FALSE(touch.take());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_short_writes_and_ring_wrap_preserve_byte_order);
    RUN_TEST(test_full_queue_drops_whole_frames_and_reserves_reply_space);
    RUN_TEST(test_disconnected_full_fifo_and_zero_progress_writes_do_not_block);
    RUN_TEST(test_reset_discards_pending_output_and_stale_input);
    RUN_TEST(test_only_1200_baud_requests_bootloader_and_request_is_consumed_once);
    return UNITY_END();
}
