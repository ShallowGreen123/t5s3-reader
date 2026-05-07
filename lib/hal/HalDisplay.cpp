#include <HalDisplay.h>

#include <BoardT5S3.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

// Global HalDisplay instance
HalDisplay display;

namespace {
uint8_t bitAt(const uint8_t* buffer, uint32_t index) {
  return (buffer[index >> 3] >> (7 - (index & 7))) & 0x01;
}
}  // namespace

HalDisplay::HalDisplay() = default;

HalDisplay::~HalDisplay() {
  free(grayscaleLsbBuffer);
  free(grayscaleMsbBuffer);
}

uint8_t* HalDisplay::allocatePlane() {
  auto* plane = static_cast<uint8_t*>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!plane) {
    plane = static_cast<uint8_t*>(malloc(BUFFER_SIZE));
  }
  return plane;
}

void HalDisplay::begin() {
  BoardT5S3::beginI2C();

  // int rc = epaper.initPanel(BB_PANEL_EPDIY_V7);
  int rc = epaper.initPanel(BB_PANEL_EPDIY_V7, 26666666);
  // epaper.setPanelSize(960, 540);
  if (rc != BBEP_SUCCESS) {
    LOG_ERR("DSP", "FastEPD initPanel failed: %d", rc);
    return;
  }

  rc = epaper.setPanelSize(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  if (rc != BBEP_SUCCESS) {
    LOG_ERR("DSP", "FastEPD setPanelSize failed: %d", rc);
    return;
  }

  epaper.setMode(BB_MODE_1BPP);
  clearScreen(0xFF);
  syncPreviousBuffer();
  forceFullRefresh = true;
  displayReady = true;

  LOG_INF("DSP", "FastEPD T5S3 display initialized: %ux%u visible, %ux%u scan", VISIBLE_WIDTH, VISIBLE_HEIGHT,
          DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void HalDisplay::clearScreen(uint8_t color) const {
  uint8_t* frameBuffer = getFrameBuffer();
  if (frameBuffer) {
    memset(frameBuffer, color, BUFFER_SIZE);
  }
}

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  uint8_t* frameBuffer = getFrameBuffer();
  if (!frameBuffer || !imageData || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }

  const uint16_t imageWidthBytes = w / 8;
  const uint16_t destByteX = x / 8;
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) {
      break;
    }
    const uint32_t destOffset = static_cast<uint32_t>(destY) * DISPLAY_WIDTH_BYTES + destByteX;
    const uint32_t srcOffset = static_cast<uint32_t>(row) * imageWidthBytes;
    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((destByteX + col) >= DISPLAY_WIDTH_BYTES) {
        break;
      }
      frameBuffer[destOffset + col] =
          fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
    }
  }
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  uint8_t* frameBuffer = getFrameBuffer();
  if (!frameBuffer || !imageData || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }

  const uint16_t imageWidthBytes = w / 8;
  const uint16_t destByteX = x / 8;
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= DISPLAY_HEIGHT) {
      break;
    }
    const uint32_t destOffset = static_cast<uint32_t>(destY) * DISPLAY_WIDTH_BYTES + destByteX;
    const uint32_t srcOffset = static_cast<uint32_t>(row) * imageWidthBytes;
    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((destByteX + col) >= DISPLAY_WIDTH_BYTES) {
        break;
      }
      const uint8_t srcByte = fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
      frameBuffer[destOffset + col] &= srcByte;
    }
  }
}

void HalDisplay::syncPreviousBuffer() const {
  uint8_t* current = epaper.currentBuffer();
  uint8_t* previous = epaper.previousBuffer();
  if (current && previous) {
    memcpy(previous, current, BUFFER_SIZE);
  }
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  if (!displayReady) {
    return;
  }

  epaper.setMode(BB_MODE_1BPP);
  (void)turnOffScreen;
  const bool keepOn = false;
  if (forceFullRefresh && mode == FAST_REFRESH) {
    LOG_DBG("DSP", "Forcing full refresh for first display update");
    mode = FULL_REFRESH;
  }
  int rc = BBEP_SUCCESS;
  switch (mode) {
    case FULL_REFRESH:
      rc = epaper.fullUpdate(CLEAR_SLOW, keepOn);
      break;
    case HALF_REFRESH:
      rc = epaper.fullUpdate(CLEAR_FAST, keepOn);
      break;
    case FAST_REFRESH:
    default:
      rc = epaper.partialUpdate(keepOn);
      break;
  }

  if (rc != BBEP_SUCCESS) {
    LOG_ERR("DSP", "FastEPD displayBuffer failed: %d", rc);
  }
  forceFullRefresh = false;
  syncPreviousBuffer();
}

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) { displayBuffer(mode, turnOffScreen); }

void HalDisplay::deepSleep() {
  if (displayReady) {
    epaper.einkPower(0);
    epaper.deInit();
  }
  BoardT5S3::deinitForSleep();
}

uint8_t* HalDisplay::getFrameBuffer() const { return epaper.currentBuffer(); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  copyGrayscaleLsbBuffers(lsbBuffer);
  copyGrayscaleMsbBuffers(msbBuffer);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  if (!lsbBuffer) {
    return;
  }
  if (!grayscaleLsbBuffer) {
    grayscaleLsbBuffer = allocatePlane();
  }
  if (grayscaleLsbBuffer) {
    memcpy(grayscaleLsbBuffer, lsbBuffer, BUFFER_SIZE);
  }
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (!msbBuffer) {
    return;
  }
  if (!grayscaleMsbBuffer) {
    grayscaleMsbBuffer = allocatePlane();
  }
  if (grayscaleMsbBuffer) {
    memcpy(grayscaleMsbBuffer, msbBuffer, BUFFER_SIZE);
  }
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  epaper.setMode(BB_MODE_1BPP);
  uint8_t* frameBuffer = getFrameBuffer();
  if (frameBuffer && bwBuffer) {
    memcpy(frameBuffer, bwBuffer, BUFFER_SIZE);
  }
  syncPreviousBuffer();
}

void HalDisplay::displayGrayBuffer(bool turnOffScreen) {
  if (!displayReady || !grayscaleLsbBuffer || !grayscaleMsbBuffer) {
    return;
  }

  const uint8_t* bwBuffer = getFrameBuffer();
  if (!bwBuffer) {
    return;
  }

  epaper.setMode(BB_MODE_4BPP);
  uint8_t* grayBuffer = epaper.currentBuffer();
  if (!grayBuffer) {
    return;
  }

  const uint32_t pixelCount = static_cast<uint32_t>(DISPLAY_WIDTH) * DISPLAY_HEIGHT;
  for (uint32_t pixel = 0; pixel < pixelCount; pixel += 2) {
    auto grayForPixel = [&](uint32_t idx) -> uint8_t {
      if (bitAt(bwBuffer, idx)) {
        return 0x0F;
      }
      const bool lsb = bitAt(grayscaleLsbBuffer, idx);
      const bool msb = bitAt(grayscaleMsbBuffer, idx);
      if (msb && lsb) {
        return 0x05;
      }
      if (msb) {
        return 0x0A;
      }
      if (lsb) {
        return 0x05;
      }
      return 0x00;
    };

    const uint8_t hi = grayForPixel(pixel);
    const uint8_t lo = (pixel + 1 < pixelCount) ? grayForPixel(pixel + 1) : 0x0F;
    grayBuffer[pixel >> 1] = static_cast<uint8_t>((hi << 4) | lo);
  }

  const int rc = epaper.fullUpdate(CLEAR_NONE, !turnOffScreen);
  if (rc != BBEP_SUCCESS) {
    LOG_ERR("DSP", "FastEPD grayscale update failed: %d", rc);
  }
}

uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }

uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }

uint16_t HalDisplay::getVisibleWidth() const { return VISIBLE_WIDTH; }

uint16_t HalDisplay::getVisibleHeight() const { return VISIBLE_HEIGHT; }

uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }

uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
