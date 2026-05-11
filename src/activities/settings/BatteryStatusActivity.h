#pragma once

#include "BoardT5S3.h"
#include "activities/Activity.h"

class BatteryStatusActivity final : public Activity {
 public:
  explicit BatteryStatusActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BatteryStatus", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  BoardT5S3::BatteryState state = {};
  bool hasState = false;

  void refreshBattery();
};
