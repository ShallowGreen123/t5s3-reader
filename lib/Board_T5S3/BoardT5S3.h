#pragma once

#include <Arduino.h>

#include "pin.hpp"

namespace BoardT5S3 {

void begin();
void beginI2C();
void prepareSdBus();
void disableGpsLora();
void deinitForSleep();

bool pca9535Present();
bool readPca9535Pin(uint8_t pin, bool* high);
bool writePca9535Pin(uint8_t pin, bool high);
bool setPca9535PinMode(uint8_t pin, uint8_t mode);
bool readButton();

bool readBQ27220Reg16(uint8_t reg, uint16_t* value);
bool readBQ25896Reg8(uint8_t reg, uint8_t* value);
bool readBatteryStateOfCharge(uint16_t* soc);
bool readBatteryCurrentMa(int16_t* current);
bool isUsbConnected();

struct TouchPoint {
  uint16_t x = 0;
  uint16_t y = 0;
};

class GT911Touch {
 public:
  bool begin();
  bool readPoint(TouchPoint* point);
  bool isAvailable() const { return available; }

 private:
  uint8_t address = T5S3_GT911_ADDR;
  void resetForAddress(uint8_t addr);
  bool probeAddress(uint8_t addr);
  bool available = false;
  bool writeReg8(uint16_t reg, uint8_t value);
  bool readReg(uint16_t reg, uint8_t* data, size_t len);
};

}  // namespace BoardT5S3
