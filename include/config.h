#pragma once
// ============ ESP32-S3 + MAX31855 温度计 全局配置 ============

// ---------- 引脚定义 ----------
#define PIN_MISO        4     // MAX31855 DO  (数据出)
#define PIN_SCK         6     // MAX31855 CLK (时钟)
#define PIN_CS          7     // MAX31855 CS  (片选)
#define PIN_BUTTON      38    // 记录按钮(内部上拉,按下接地)
#define PIN_LED         48    // YD-ESP32-S3 板载 WS2812 RGB LED

// ---------- WiFi 默认配置 ----------
// 由 scripts/gen_wifi_config.py 从 wifi-config.txt 生成 (不入库, 见 wifi-config.example.txt)
// 未配置时固件仅 Host(AP) 模式, 之后用 WebUI/串口 wifi 命令设置
#include "wifi_config.h"
#define WIFI_TIMEOUT_MS     15000   // STA 连接超时(超时判定失败→黄灯)
#define WIFI_RETRY_MS       30000   // 失败后后台重试间隔

// ---------- Host(AP) 模式(默认开启, 开放无密码) ----------
#define AP_SSID             "ESP32S3-Thermo"
#define AP_IP_1             4
#define AP_IP_2             4
#define AP_IP_3             4
#define AP_IP_4             1       // 管理页 http://4.4.4.1/
#define HOST_DEFAULT        true    // 上电默认 Host(AP) 模式

// ---------- 采样 / 输出 ----------
#define SAMPLE_MS           500     // 温度采样周期
#define PRINT_MS            1000    // 串口连续输出周期

// ---------- 记录 ----------
#define RECORD_MAX          250000  // PSRAM 缓冲最大条数(每条 16 字节 ≈ 4MB)
#define FALLBACK_RECORD_MAX 9000    // 无 PSRAM 时堆缓冲条数(约 144KB)

// ---------- 时间(NTP, 固定 UTC+8) ----------
#define NTP_SERVER1         "pool.ntp.org"
#define NTP_SERVER2         "ntp.aliyun.com"

// ---------- Web ----------
#define HTTP_PORT           80
