#pragma once
#include "can_bus.h"
#include "usb_transport.h"

// Platform-specific drivers; the rest of the application runs on a desktop too.
CanDriver &twaiCanDriver();
UsbDriver &nativeUsbDriver();
