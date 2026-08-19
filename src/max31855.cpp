#include "max31855.h"
#include <SPI.h>
#include "../include/config.h"

void Max31855::begin() {
    pinMode(PIN_CS, OUTPUT);
    digitalWrite(PIN_CS, HIGH);
    SPI.begin(PIN_SCK, PIN_MISO, -1, -1);
}

float Max31855::read(uint8_t& fault) {
    fault = 0;

    digitalWrite(PIN_CS, LOW);
    delayMicroseconds(2);
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) raw = (raw << 8) | SPI.transfer(0x00);
    SPI.endTransaction();
    digitalWrite(PIN_CS, HIGH);

    // 全 1 或全 0 → 传感器未连接/损坏
    if (raw == 0xFFFFFFFF || raw == 0x00000000) { fault = 0xFF; return NAN; }

    // 故障位: bit2 开路, bit1 对地短路, bit0 对电源短路
    if (raw & 0x07) { fault = (uint8_t)(raw & 0x07); return NAN; }

    // TC 温度: bit31..18 为 14 位有符号数 × 0.25 ℃
    int16_t t = (int16_t)((raw >> 18) & 0x3FFF);
    if (t & 0x2000) t |= (int16_t)0xC000;   // 符号扩展
    return t * 0.25f;
}

const char* Max31855::faultStr(uint8_t fault) {
    switch (fault) {
        case 0xFF: return "传感器未连接";
        case 0x04: return "热电偶开路";
        case 0x02: return "热电偶短路GND";
        case 0x01: return "热电偶短路VCC";
        default:   return "";
    }
}
