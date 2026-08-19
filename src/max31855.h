// ============================================================
// MAX31855 K-type thermocouple driver (hardware SPI, mode 0, 1 MHz)
// 热电偶驱动: 3-wire SPI, 故障检测, 冷端补偿已内建
// ============================================================
#pragma once
#include <Arduino.h>

// MAX31855 热电偶测温驱动(硬件 SPI, mode 0, 1MHz)
// 引脚来自 config.h: MISO=4, SCK=6, CS=7
class Max31855 {
public:
    void begin();
    // 读一次 TC 温度(已含冷端补偿)。有效返回温度(℃);故障返回 NAN 并置 fault:
    //   0xFF=无器件  0x04=热电偶开路  0x02=短路GND  0x01=短路VCC
    float read(uint8_t& fault);
    static const char* faultStr(uint8_t fault);
};
