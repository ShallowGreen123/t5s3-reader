#include "HalTiltSensor.h"

#include <Logging.h>

HalTiltSensor halTiltSensor;

void HalTiltSensor::begin() { LOG_INF("GYR", "Tilt sensor disabled on T5S3"); }

bool HalTiltSensor::wake() { return false; }

bool HalTiltSensor::deepSleep() { return false; }

void HalTiltSensor::update(uint8_t mode, uint8_t orientation, bool inReader) {
  (void)mode;
  (void)orientation;
  (void)inReader;
}

bool HalTiltSensor::wasTiltedForward() { return false; }

bool HalTiltSensor::wasTiltedBack() { return false; }

bool HalTiltSensor::hadActivity() { return false; }

void HalTiltSensor::clearPendingEvents() {}
