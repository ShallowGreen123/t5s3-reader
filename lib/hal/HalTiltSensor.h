#pragma once

#include <Arduino.h>

namespace CrossPointOrientation {
enum Value : uint8_t { PORTRAIT = 0, LANDSCAPE_CW = 1, INVERTED = 2, LANDSCAPE_CCW = 3 };
}

namespace CrossPointTiltPageTurn {
enum Value : uint8_t { TILT_OFF = 0, TILT_NORMAL = 1, TILT_INVERTED = 2 };
}

class HalTiltSensor;
extern HalTiltSensor halTiltSensor;

class HalTiltSensor {
 public:
  void begin();
  bool wake();
  bool deepSleep();
  bool isAvailable() const { return false; }
  void update(uint8_t mode, uint8_t orientation, bool inReader);
  bool wasTiltedForward();
  bool wasTiltedBack();
  bool hadActivity();
  void clearPendingEvents();
};
