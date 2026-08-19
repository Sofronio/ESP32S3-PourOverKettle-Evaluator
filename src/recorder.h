#pragma once
#include <Arduino.h>
#include <LittleFS.h>

// 一条记录: 时间戳 + 温度(℃) + 当前水量(g) + 目标温度(℃, PID 分析用 SV)
struct Record {
    uint32_t t;
    float v;
    float w;
    float target;
};

// 温度记录器: PSRAM 大缓冲 → 停止时保存为 LittleFS CSV 文件
class Recorder {
public:
    bool begin();                  // 分配 PSRAM(优先)/堆 缓冲
    bool start();                  // 清空缓冲并开始记录
    void stop();                   // 停止并自动保存 CSV
    void addSample(uint32_t t, float v, float w, float target);
    bool isRecording() const { return _rec; }
    uint32_t count() const { return _count; }
    uint32_t capacity() const { return _cap; }

    // 会话信息(用于 CSV 文件名/文件头): 起止温度 + 手冲壶名 + 盖状态
    void setStartTemp(float t) { _startTemp = t; }
    void setEndTemp(float t) { _endTemp = t; }
    void setKettle(const char* name);   // 内部做文件名安全化
    void setLidOn(bool on) { _lidOn = on; }
    void setSessionInfo(float dry, float lidW, float cap, float total, float water, float init) {
        _sessionDry = dry; _sessionLidW = lidW; _sessionCap = cap;
        _sessionTotal = total; _sessionWater = water; _sessionInit = init;
    }

    void show(uint32_t maxLines = 200);   // 串口显示当前记录

    // LittleFS 文件管理
    static void listFiles();
    static bool deleteFile(const char* name);
    static void clearFiles();
    static void catFile(const char* name);
    static String sanitize(const char* name);

private:
    String makeName();
    bool saveSession();

    Record* _buf = nullptr;
    uint32_t _cap = 0;
    uint32_t _count = 0;
    bool _rec = false;
    float _startTemp = NAN;
    float _endTemp = NAN;
    String _kettle;   // 文件名安全化后的壶名
    bool _lidOn = true;
    float _sessionDry = 0;    // 会话开始时的壶参数快照 (CSV 文件头)
    float _sessionLidW = 0;
    float _sessionCap = 0;
    float _sessionTotal = 0;
    float _sessionWater = 0;
    float _sessionInit = 25;  // 初始设定温度(分析基准, 排除温度计恒偏)
};
