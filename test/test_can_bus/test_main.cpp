#include <unity.h>
#include "../fakes.h"

void setUp() {}
void tearDown() {}

void test_default_start_and_frame_io() {
    FakeCan driver;
    CanBus bus(driver);
    TEST_ASSERT_FALSE(bus.settings().enabled);
    TEST_ASSERT_TRUE(bus.begin());
    TEST_ASSERT_EQUAL_UINT32(500000, bus.settings().bitrate);
    TEST_ASSERT_FALSE(driver.starts.back().listenOnly);
    Frame frame;
    frame.id = 0x1FFFFFFF;
    frame.extended = true;
    frame.dlc = 8;
    frame.data[7] = 0xE7;
    TEST_ASSERT_TRUE(bus.transmit(frame));
    TEST_ASSERT_EQUAL_UINT32(frame.id, driver.outgoing.back().id);
    TEST_ASSERT_EQUAL_UINT8(0xE7, driver.outgoing.back().data[7]);
    driver.incoming.push_back(frame);
    Frame received;
    TEST_ASSERT_TRUE(bus.receive(received));
    TEST_ASSERT_EQUAL_UINT32(frame.id, received.id);
    TEST_ASSERT_FALSE(bus.receive(received));
    driver.writeOK = false;
    TEST_ASSERT_FALSE(bus.transmit(frame)); // full driver queue propagates failure
}

void test_runtime_listen_only_disable_and_reenable() {
    FakeCan driver;
    CanBus bus(driver);
    bus.begin();
    CanSettings next;
    next.bitrate = 250000;
    next.listenOnly = true;
    TEST_ASSERT_TRUE(bus.configure(next));
    TEST_ASSERT_TRUE(driver.starts.back().listenOnly);
    TEST_ASSERT_EQUAL_UINT32(250000, driver.starts.back().bitrate);
    Frame frame;
    TEST_ASSERT_FALSE(bus.transmit(frame));
    TEST_ASSERT_TRUE(driver.outgoing.empty());
    next.enabled = false;
    TEST_ASSERT_TRUE(bus.configure(next));
    const unsigned reads = driver.reads;
    TEST_ASSERT_FALSE(bus.receive(frame));
    TEST_ASSERT_EQUAL_UINT(reads, driver.reads);
    TEST_ASSERT_EQUAL_UINT(2, driver.starts.size());
    next.enabled = true;
    next.listenOnly = false;
    TEST_ASSERT_TRUE(bus.configure(next));
    TEST_ASSERT_TRUE(bus.transmit(frame));
    TEST_ASSERT_TRUE(bus.shutdown());
    TEST_ASSERT_FALSE(bus.settings().enabled);
    TEST_ASSERT_FALSE(bus.transmit(frame));
}

void test_unsupported_rate_and_stop_failure_preserve_bus() {
    FakeCan driver;
    CanBus bus(driver);
    bus.begin();
    CanSettings next;
    next.bitrate = 123456;
    const unsigned stops = driver.stops;
    TEST_ASSERT_FALSE(bus.configure(next));
    TEST_ASSERT_EQUAL_UINT(stops, driver.stops);
    TEST_ASSERT_EQUAL_UINT32(500000, bus.settings().bitrate);
    next.bitrate = 250000;
    driver.stopResults.push_back(false);
    TEST_ASSERT_FALSE(bus.configure(next));
    TEST_ASSERT_TRUE(bus.settings().enabled);
    TEST_ASSERT_EQUAL_UINT(1, driver.starts.size());
    TEST_ASSERT_TRUE(bus.transmit(Frame{}));
}

void test_failed_restart_restores_previous_configuration() {
    FakeCan driver;
    CanBus bus(driver);
    bus.begin();
    driver.startResults = {false, true};
    CanSettings next;
    next.bitrate = 250000;
    next.listenOnly = true;
    TEST_ASSERT_FALSE(bus.configure(next));
    TEST_ASSERT_EQUAL_UINT(3, driver.starts.size());
    TEST_ASSERT_EQUAL_UINT32(250000, driver.starts[1].bitrate);
    TEST_ASSERT_EQUAL_UINT32(500000, driver.starts[2].bitrate);
    TEST_ASSERT_FALSE(driver.starts[2].listenOnly);
    TEST_ASSERT_TRUE(bus.settings().enabled);
    TEST_ASSERT_EQUAL_UINT32(500000, bus.settings().bitrate);
    TEST_ASSERT_TRUE(bus.transmit(Frame{}));
}

void test_failed_restore_and_failed_initial_start_report_disabled() {
    FakeCan driver;
    CanBus bus(driver);
    bus.begin();
    driver.startResults = {false, false};
    CanSettings next;
    next.bitrate = 250000;
    TEST_ASSERT_FALSE(bus.configure(next));
    TEST_ASSERT_FALSE(bus.settings().enabled);
    TEST_ASSERT_FALSE(bus.transmit(Frame{}));
    FakeCan brokenDriver;
    brokenDriver.startResults = {false};
    CanBus brokenBus(brokenDriver);
    TEST_ASSERT_FALSE(brokenBus.begin());
    TEST_ASSERT_FALSE(brokenBus.settings().enabled);
    TEST_ASSERT_EQUAL_UINT(1, brokenDriver.starts.size());
}

void test_invalid_classic_frames_never_reach_driver() {
    FakeCan driver;
    CanBus bus(driver);
    bus.begin();
    Frame frame;
    frame.id = 0x800;
    TEST_ASSERT_FALSE(bus.transmit(frame));
    frame.extended = true;
    frame.id = 0x20000000;
    TEST_ASSERT_FALSE(bus.transmit(frame));
    frame.id = 0;
    frame.dlc = 9;
    TEST_ASSERT_FALSE(bus.transmit(frame));
    TEST_ASSERT_TRUE(driver.outgoing.empty());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_default_start_and_frame_io);
    RUN_TEST(test_runtime_listen_only_disable_and_reenable);
    RUN_TEST(test_unsupported_rate_and_stop_failure_preserve_bus);
    RUN_TEST(test_failed_restart_restores_previous_configuration);
    RUN_TEST(test_failed_restore_and_failed_initial_start_report_disabled);
    RUN_TEST(test_invalid_classic_frames_never_reach_driver);
    return UNITY_END();
}
