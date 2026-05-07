#include <HalGPIO.h>

#include <Logging.h>
#include <esp_sleep.h>

// Global HalGPIO instance
HalGPIO gpio;

namespace {
constexpr uint16_t TOUCH_SWIPE_THRESHOLD = 70;
constexpr unsigned long TOUCH_RELEASE_GRACE_MS = 120;
constexpr unsigned long TOUCH_HOME_BUTTON_REPEAT_MS = 250;
constexpr uint64_t POWER_WAKE_MASK = 1ULL << T5S3_BOOT_BTN;
constexpr uint64_t TOUCH_WAKE_MASK = 1ULL << T5S3_TOUCH_INT;

uint8_t buttonBit(uint8_t button) { return static_cast<uint8_t>(1U << button); }

void rotatePhysicalTouchToLogical(uint16_t* x, uint16_t* y) {
  const uint16_t physicalX = *x;
  const uint16_t physicalY = *y;

  // if (physicalX < T5S3_WIDTH && physicalY < T5S3_HEIGHT) {
  //   *x = T5S3_HEIGHT - 1 - physicalY;
  //   *y = physicalX;
  // }
}
}  // namespace

void HalGPIO::begin() {
  BoardT5S3::begin();
  const bool touchReady = touch.begin();
  LOG_INF("HW", "Board init: pca9535=%d touch=%d usb=%d", BoardT5S3::pca9535Present(), touchReady,
          BoardT5S3::isUsbConnected());

  lastUsbConnected = isUsbConnected();
  update();
}

void HalGPIO::readTouchState() {
  BoardT5S3::TouchPoint point;
  bool touchHomeButtonPressed = false;
  if (!touch.readPoint(&point, &touchHomeButtonPressed)) {
    if (touchHomeButtonPressed && millis() - lastTouchHomeButtonEventTime >= TOUCH_HOME_BUTTON_REPEAT_MS) {
      touchHomeButtonEvent = true;
      lastTouchHomeButtonEventTime = millis();
    }
    if (touchActive && millis() - lastTouchSeenTime > TOUCH_RELEASE_GRACE_MS) {
      if (!touchMoved) {
        touchTapPoint = {touchStartX, touchStartY};
        touchTapEvent = true;
      }
      touchActive = false;
    }
    return;
  }

  if (touchHomeButtonPressed && millis() - lastTouchHomeButtonEventTime >= TOUCH_HOME_BUTTON_REPEAT_MS) {
    touchHomeButtonEvent = true;
    lastTouchHomeButtonEventTime = millis();
  }

  uint16_t x = point.x;
  uint16_t y = point.y;
  rotatePhysicalTouchToLogical(&x, &y);
  lastTouchSeenTime = millis();

  if (!touchActive) {
    touchActive = true;
    touchStartX = x;
    touchStartY = y;
    touchMoved = false;
    LOG_DBG("HW", "Touch raw=(%u,%u) logical=(%u,%u)", point.x, point.y, x, y);
  } else {
    const int dx = static_cast<int>(x) - static_cast<int>(touchStartX);
    const int dy = static_cast<int>(y) - static_cast<int>(touchStartY);
    if (abs(dx) >= TOUCH_SWIPE_THRESHOLD || abs(dy) >= TOUCH_SWIPE_THRESHOLD) {
      touchMoved = true;
    }
  }
}

uint8_t HalGPIO::getState() {
  uint8_t state = 0;

  if (BoardT5S3::readButton()) {
    state |= buttonBit(BTN_CONFIRM);
  }
  if (digitalRead(T5S3_BOOT_BTN) == LOW) {
    state |= buttonBit(BTN_POWER);
  }

  readTouchState();
  return state;
}

void HalGPIO::update() {
  const unsigned long currentTime = millis();
  touchTapEvent = false;
  touchHomeButtonEvent = false;
  const uint8_t state = getState();

  pressedEvents = 0;
  releasedEvents = 0;

  if (state != lastState) {
    lastDebounceTime = currentTime;
    lastState = state;
  }

  if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (state != currentState) {
      pressedEvents = state & ~currentState;
      releasedEvents = currentState & ~state;

      if (pressedEvents > 0 && currentState == 0) {
        buttonPressStart = currentTime;
      }
      if (releasedEvents > 0 && state == 0) {
        buttonPressFinish = currentTime;
      }

      currentState = state;
    }
  }

  const bool connected = isUsbConnected();
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return currentState & buttonBit(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return pressedEvents & buttonBit(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return pressedEvents > 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return releasedEvents & buttonBit(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return releasedEvents > 0; }

bool HalGPIO::hadTouchActivity() const { return touchActive || touchTapEvent || touchHomeButtonEvent; }

bool HalGPIO::getTouchTap(TouchPoint& point) const {
  if (!touchTapEvent) {
    return false;
  }
  point = touchTapPoint;
  return true;
}

bool HalGPIO::wasTouchHomeButtonPressed() const { return touchHomeButtonEvent; }

unsigned long HalGPIO::getHeldTime() const {
  if (currentState > 0) {
    return millis() - buttonPressStart;
  }
  return buttonPressFinish - buttonPressStart;
}

void HalGPIO::startDeepSleep() {
  while (isPressed(BTN_POWER)) {
    delay(50);
    update();
  }

  BoardT5S3::deinitForSleep();
  pinMode(T5S3_BOOT_BTN, INPUT_PULLUP);
  pinMode(T5S3_TOUCH_INT, INPUT_PULLUP);
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_enable_gpio_wakeup(POWER_WAKE_MASK | TOUCH_WAKE_MASK, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  esp_sleep_enable_ext1_wakeup(POWER_WAKE_MASK | TOUCH_WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
  esp_deep_sleep_start();
}

void HalGPIO::verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed) {
  if (shortPressAllowed) {
    return;
  }

  const uint16_t calibration = millis();
  const uint16_t calibratedDuration = calibration < requiredDurationMs ? requiredDurationMs - calibration : 1;

  const auto start = millis();
  update();
  while (!isPressed(BTN_POWER) && millis() - start < 1000) {
    delay(10);
    update();
  }

  if (isPressed(BTN_POWER)) {
    do {
      delay(10);
      update();
    } while (isPressed(BTN_POWER) && getHeldTime() < calibratedDuration);
    if (getHeldTime() < calibratedDuration) {
      startDeepSleep();
    }
  } else {
    startDeepSleep();
  }
}

bool HalGPIO::isUsbConnected() const { return BoardT5S3::isUsbConnected(); }

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();
  const bool usbConnected = isUsbConnected();

  if (wakeupCause == ESP_SLEEP_WAKEUP_EXT1) {
    const uint64_t wakeStatus = esp_sleep_get_ext1_wakeup_status();
    if ((wakeStatus & POWER_WAKE_MASK) != 0) {
      return WakeupReason::PowerButton;
    }
    if ((wakeStatus & TOUCH_WAKE_MASK) != 0) {
      return WakeupReason::Touch;
    }
  }

  if (wakeupCause == ESP_SLEEP_WAKEUP_GPIO) {
    if (digitalRead(T5S3_BOOT_BTN) == LOW) {
      return WakeupReason::PowerButton;
    }
    if (digitalRead(T5S3_TOUCH_INT) == LOW) {
      return WakeupReason::Touch;
    }
    return WakeupReason::Other;
  }

  if (resetReason == ESP_RST_POWERON && !usbConnected) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}
