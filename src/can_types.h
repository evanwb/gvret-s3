#pragma once
#include <stdint.h>

struct CanSettings {
    uint32_t bitrate = 500000;
    bool enabled = true;
    bool listenOnly = false;
};

struct Frame {
    uint32_t id = 0;
    bool extended = false;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};
