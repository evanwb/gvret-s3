#pragma once

// Only the Arduino functions used by main.cpp. Native integration tests supply
// a fixed clock and a no-op delay; firmware uses the real Arduino header.
unsigned long micros();
unsigned long millis();
void delay(unsigned long milliseconds);
