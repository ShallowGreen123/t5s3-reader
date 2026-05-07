#include "BoardT5S3.h"

#include <SPI.h>
#include <Wire.h>

namespace BoardT5S3 {
namespace {
constexpr uint8_t PCA_REG_INPUT0 = 0x00;
constexpr uint8_t PCA_REG_OUTPUT0 = 0x02;
constexpr uint8_t PCA_REG_CONFIG0 = 0x06;

constexpr uint8_t BQ27220_SOC_REG = 0x2C;
constexpr uint8_t BQ27220_CURRENT_REG = 0x0C;
constexpr uint8_t BQ25896_SYSTEM_STATUS_REG = 0x0B;
constexpr uint8_t BQ25896_VBUS_STAT_MASK = 0xE0;

constexpr uint16_t GT911_PRODUCT_ID_REG = 0x8140;
constexpr uint16_t GT911_STATUS_REG = 0x814E;
constexpr uint16_t GT911_POINT1_REG = 0x814F;
constexpr uint8_t GT911_STATUS_READY = 0x80;
constexpr uint8_t GT911_TOUCH_COUNT_MASK = 0x0F;
constexpr uint8_t GT911_BACKUP_ADDR = 0x14;

bool i2cWriteReg(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (data != nullptr && len > 0) {
    Wire.write(data, len);
  }
  return Wire.endTransmission() == 0;
}

bool i2cReadReg(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const uint8_t requested = static_cast<uint8_t>(len);
  if (Wire.requestFrom(addr, requested) != requested) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

bool updatePca9535Bit(uint8_t baseReg, uint8_t pin, bool high) {
  const uint8_t port = pin / 8;
  const uint8_t bit = pin % 8;
  uint8_t value = 0;
  if (!i2cReadReg(T5S3_PCA9535_ADDR, baseReg + port, &value, 1)) {
    return false;
  }
  if (high) {
    value |= static_cast<uint8_t>(1U << bit);
  } else {
    value &= static_cast<uint8_t>(~(1U << bit));
  }
  return i2cWriteReg(T5S3_PCA9535_ADDR, baseReg + port, &value, 1);
}

bool readReg16LE(uint8_t addr, uint8_t reg, uint16_t* value) {
  uint8_t data[2] = {0, 0};
  if (!i2cReadReg(addr, reg, data, sizeof(data))) {
    return false;
  }
  *value = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  return true;
}

}  // namespace

void beginI2C() {
  Wire.begin(T5S3_SDA, T5S3_SCL);
  Wire.setClock(T5S3_I2C_FREQ);
  Wire.setTimeOut(50);
}

void prepareSdBus() {
  pinMode(T5S3_LORA_CS, OUTPUT);
  digitalWrite(T5S3_LORA_CS, HIGH);
  pinMode(T5S3_SD_CS, OUTPUT);
  digitalWrite(T5S3_SD_CS, HIGH);
  SPI.begin(T5S3_SPI_SCLK, T5S3_SPI_MISO, T5S3_SPI_MOSI, T5S3_SD_CS);
}

void disableGpsLora() {
  pinMode(T5S3_LORA_CS, OUTPUT);
  digitalWrite(T5S3_LORA_CS, HIGH);
  pinMode(T5S3_LORA_RST, OUTPUT);
  digitalWrite(T5S3_LORA_RST, LOW);
  pinMode(T5S3_LORA_IRQ, INPUT);
  pinMode(T5S3_LORA_BUSY, INPUT);
  pinMode(T5S3_GPS_RXD, INPUT);
  pinMode(T5S3_GPS_TXD, INPUT);

  writePca9535Pin(PCA9535_IO00_LORA_GPS_EN, false);
  setPca9535PinMode(PCA9535_IO00_LORA_GPS_EN, OUTPUT);
}

void begin() {
  beginI2C();

  pinMode(T5S3_BOOT_BTN, INPUT_PULLUP);
  if (T5S3_PCA9535_INT > 0) {
    pinMode(T5S3_PCA9535_INT, INPUT_PULLUP);
  }

  prepareSdBus();
  disableGpsLora();
}

void deinitForSleep() {
  disableGpsLora();
  pinMode(T5S3_SD_CS, INPUT);
  pinMode(T5S3_GPS_RXD, INPUT);
  pinMode(T5S3_GPS_TXD, INPUT);
}

bool pca9535Present() {
  Wire.beginTransmission(T5S3_PCA9535_ADDR);
  return Wire.endTransmission() == 0;
}

bool setPca9535PinMode(uint8_t pin, uint8_t mode) {
  const bool inputMode = mode != OUTPUT;
  return updatePca9535Bit(PCA_REG_CONFIG0, pin, inputMode);
}

bool writePca9535Pin(uint8_t pin, bool high) {
  return updatePca9535Bit(PCA_REG_OUTPUT0, pin, high);
}

bool readPca9535Pin(uint8_t pin, bool* high) {
  if (!high) {
    return false;
  }
  const uint8_t port = pin / 8;
  const uint8_t bit = pin % 8;
  uint8_t value = 0;
  if (!i2cReadReg(T5S3_PCA9535_ADDR, PCA_REG_INPUT0 + port, &value, 1)) {
    return false;
  }
  *high = (value & (1U << bit)) != 0;
  return true;
}

bool readButton() {
  bool high = true;
  setPca9535PinMode(PCA9535_IO12_BUTTON, INPUT);
  if (!readPca9535Pin(PCA9535_IO12_BUTTON, &high)) {
    return false;
  }
  return !high;
}

bool readBQ27220Reg16(uint8_t reg, uint16_t* value) {
  if (!value) {
    return false;
  }
  return readReg16LE(T5S3_BQ27220_ADDR, reg, value);
}

bool readBQ25896Reg8(uint8_t reg, uint8_t* value) {
  if (!value) {
    return false;
  }
  return i2cReadReg(T5S3_BQ25896_ADDR, reg, value, 1);
}

bool readBatteryStateOfCharge(uint16_t* soc) {
  return readBQ27220Reg16(BQ27220_SOC_REG, soc);
}

bool readBatteryCurrentMa(int16_t* current) {
  if (!current) {
    return false;
  }
  uint16_t raw = 0;
  if (!readBQ27220Reg16(BQ27220_CURRENT_REG, &raw)) {
    return false;
  }
  *current = static_cast<int16_t>(raw);
  return true;
}

bool isUsbConnected() {
  uint8_t systemStatus = 0;
  if (readBQ25896Reg8(BQ25896_SYSTEM_STATUS_REG, &systemStatus) &&
      (systemStatus & BQ25896_VBUS_STAT_MASK) != 0) {
    return true;
  }

  int16_t currentMa = 0;
  return readBatteryCurrentMa(&currentMa) && currentMa > 0;
}

bool GT911Touch::writeReg8(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool GT911Touch::readReg(uint16_t reg, uint8_t* data, size_t len) {
  Wire.beginTransmission(address);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const uint8_t requested = static_cast<uint8_t>(len);
  if (Wire.requestFrom(address, requested) != requested) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

void GT911Touch::resetForAddress(uint8_t addr) {
  // GT911 samples INT while RESET is released to select its I2C address.
  // INT low selects 0x5D; INT high selects 0x14.
  pinMode(T5S3_TOUCH_INT, OUTPUT);
  digitalWrite(T5S3_TOUCH_INT, addr == T5S3_GT911_ADDR ? LOW : HIGH);
  pinMode(T5S3_TOUCH_RST, OUTPUT);
  digitalWrite(T5S3_TOUCH_RST, LOW);
  delay(20);
  digitalWrite(T5S3_TOUCH_RST, HIGH);
  delay(60);
  pinMode(T5S3_TOUCH_INT, INPUT);
  delay(5);
}

bool GT911Touch::probeAddress(uint8_t addr) {
  address = addr;
  uint8_t productId[4] = {0, 0, 0, 0};
  available = readReg(GT911_PRODUCT_ID_REG, productId, sizeof(productId));
  if (available) {
    writeReg8(GT911_STATUS_REG, 0);
  }
  return available;
}

bool GT911Touch::begin() {
  resetForAddress(T5S3_GT911_ADDR);
  if (probeAddress(T5S3_GT911_ADDR)) {
    return true;
  }

  resetForAddress(GT911_BACKUP_ADDR);
  if (probeAddress(GT911_BACKUP_ADDR)) {
    return true;
  }

  address = T5S3_GT911_ADDR;
  available = false;
  return false;
}

bool GT911Touch::readPoint(TouchPoint* point) {
  if (!available || point == nullptr) {
    return false;
  }

  uint8_t status = 0;
  if (!readReg(GT911_STATUS_REG, &status, 1)) {
    return false;
  }
  if ((status & GT911_STATUS_READY) == 0) {
    return false;
  }

  const uint8_t touchCount = status & GT911_TOUCH_COUNT_MASK;
  if (touchCount == 0) {
    writeReg8(GT911_STATUS_REG, 0);
    return false;
  }

  uint8_t data[8] = {0};
  const bool ok = readReg(GT911_POINT1_REG, data, sizeof(data));
  writeReg8(GT911_STATUS_REG, 0);
  if (!ok) {
    return false;
  }

  point->x = static_cast<uint16_t>(data[1]) | (static_cast<uint16_t>(data[2]) << 8);
  point->y = static_cast<uint16_t>(data[3]) | (static_cast<uint16_t>(data[4]) << 8);
  return true;
}

}  // namespace BoardT5S3
