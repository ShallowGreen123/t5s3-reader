#include "Activity.h"

#include <array>

#include "ActivityManager.h"
#include "components/UITheme.h"

void Activity::onEnter() { LOG_DBG("ACT", "Entering activity: %s", name.c_str()); }

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome() { activityManager.goHome(); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

bool Activity::isHomeTouchTap(const int16_t x, const int16_t y) const {
  const auto bounds = UITheme::getHomeTouchBounds(UITheme::getInstance().getMetrics());
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

namespace {
Rect rotatePortraitRectToCurrentOrientation(const Rect& rect, const GfxRenderer& renderer) {
  const int portraitWidth = renderer.getDisplayVisibleWidth();
  const int portraitHeight = renderer.getDisplayVisibleHeight();

  switch (renderer.getOrientation()) {
    case GfxRenderer::Orientation::Portrait:
      return rect;
    case GfxRenderer::Orientation::LandscapeClockwise:
      return Rect{portraitHeight - rect.y - rect.height, rect.x, rect.height, rect.width};
    case GfxRenderer::Orientation::PortraitInverted:
      return Rect{portraitWidth - rect.x - rect.width, portraitHeight - rect.y - rect.height, rect.width,
                  rect.height};
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      return Rect{rect.y, portraitWidth - rect.x - rect.width, rect.height, rect.width};
  }

  return rect;
}

bool containsPoint(const Rect& rect, const int16_t x, const int16_t y) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}
}  // namespace

bool Activity::resolveTouchButtonHint(const int16_t x, const int16_t y, MappedInputManager::Button& button) const {
  if (!supportsTouchButtonHints()) {
    return false;
  }

  const auto touchBounds = GUI.getButtonHintTouchBounds(renderer);
  for (size_t i = 0; i < touchBounds.size(); ++i) {
    const Rect orientedBounds = rotatePortraitRectToCurrentOrientation(touchBounds[i], renderer);
    if (!containsPoint(orientedBounds, x, y)) {
      continue;
    }
    return mappedInput.resolveTouchFrontButton(i, button);
  }

  return false;
}

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }
