// ============================================================
// Recorder implementation — see recorder.h
// 记录器实现 — 见 recorder.h
// ============================================================
#include "recorder.h"
#include <time.h>
#include <vector>
#include "../include/config.h"

// ---------------- 内部工具 ----------------

// 文件名消毒: 拒绝路径穿越、隐藏文件、超长
// 注意: String::length() 按字节计, 中文文件名(UTF-8 3字节/字)需要放宽
String Recorder::sanitize(const char* name) {
    String s = name;
    s.trim();
    if (s.isEmpty() || s.length() > 100 ||
        s.indexOf('/') >= 0 || s.indexOf('\\') >= 0 ||
        s.indexOf("..") >= 0 || s.startsWith(".")) {
        return String("");
    }
    return s;
}

static String fmtTime(uint32_t t) {
    if (t < 1600000000) return String("uptime:") + t;
    struct tm tm;
    time_t tt = (time_t)t;
    localtime_r(&tt, &tm);
    char b[24];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm);
    return String(b);
}

// ---------------- 缓冲管理 ----------------

bool Recorder::begin() {
    _cap = RECORD_MAX;
    if (psramFound()) {
        _buf = (Record*)ps_malloc((size_t)_cap * sizeof(Record));
    }
    if (!_buf) {
        // 无 PSRAM 或分配失败 → 逐级缩小堆缓冲
        for (uint32_t cap = FALLBACK_RECORD_MAX; cap >= 2000; cap /= 2) {
            _buf = (Record*)malloc((size_t)cap * sizeof(Record));
            if (_buf) { _cap = cap; break; }
        }
        if (!_buf) {
            _cap = 0;
            Serial.println("!! 记录缓冲区分配失败");
            return false;
        }
        Serial.printf("!! PSRAM 不可用,回退堆内存缓冲: %u 条\n", _cap);
    }
    Serial.printf("记录缓冲: %u 条 x %u 字节 = %u KB (%s)\n",
                  _cap, (unsigned)sizeof(Record),
                  (unsigned)((size_t)_cap * sizeof(Record) / 1024),
                  psramFound() ? "PSRAM" : "堆");
    return true;
}

bool Recorder::start() {
    if (!_buf) return false;
    _count = 0;
    _rec = true;
    Serial.println(">> 记录开始 (PSRAM)");
    return true;
}

void Recorder::addSample(uint32_t t, float v, float w, float target) {
    if (!_rec || !_buf) return;
    if (_count >= _cap) {
        // 缓冲满 → 自动停止并保存
        _rec = false;
        Serial.printf(">> 缓冲区已满(%u 条),自动停止并保存\n", _cap);
        if (_count > 0) saveSession();
        return;
    }
    _buf[_count].t = t;
    _buf[_count].v = v;
    _buf[_count].w = w;
    _buf[_count].target = target;
    _count++;
}

void Recorder::stop() {
    if (!_rec) return;
    _rec = false;
    Serial.printf(">> 记录停止,共 %u 条\n", _count);
    if (_count > 0) saveSession();
}

// ---------------- 保存 ----------------

// 壶名做文件名安全化: 去路径分隔符/控制符, 空格→下划线, 限长 20
void Recorder::setKettle(const char* name) {
    String s = name;
    s.trim();
    String r;
    for (size_t i = 0; i < s.length() && r.length() < 20; i++) {
        char c = s[i];
        if (c == '/' || c == '\\' || c == ':' || c == '|' || c == '*') c = '_';
        else if ((uint8_t)c < 0x20) continue;
        if (c == ' ') c = '_';
        r += c;
    }
    _kettle = r;
}

String Recorder::makeName() {
    // <壶名或rec>_<起始温>c-<结束温>c_<时间>.csv  例: fellow_23.5c-98.4c_20260819_163000.csv
    char startT[16], endT[16];
    if (isnan(_startTemp)) strcpy(startT, "--c");
    else snprintf(startT, sizeof(startT), "%.1fc", _startTemp);
    if (isnan(_endTemp)) strcpy(endT, "--c");
    else snprintf(endT, sizeof(endT), "%.1fc", _endTemp);

    time_t t = time(nullptr);
    char ts[40];
    if (t >= 1600000000) {
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm);
    } else {
        snprintf(ts, sizeof(ts), "boot%u", (uint32_t)(millis() / 1000));
    }
    return String(_kettle.isEmpty() ? "rec" : _kettle) + "_" + startT + "-" + endT + "_" + ts + ".csv";
}

bool Recorder::saveSession() {
    String name = makeName();
    File f = LittleFS.open("/" + name, "w");
    if (!f) {
        Serial.println("!! CSV 文件写入失败");
        return false;
    }
    // 文件头注释: 手冲壶信息 (壶名/标称容量/有盖无盖/空壶重/盖重/总重/水量/初始基准温度)
    f.printf("# kettle:%s, cap:%.0fml, lid:%s, dry:%.1fg, lid_w:%.1fg, total:%.1fg, water:%.1fg, init:%.1fc\n",
             _kettle.isEmpty() ? "none" : _kettle.c_str(),
             _sessionCap,
             _lidOn ? "on" : "off",
             _sessionDry, _sessionLidW, _sessionTotal, _sessionWater, _sessionInit);
    f.println("time,temperature_c,water_g,target_c");
    for (uint32_t i = 0; i < _count; i++) {
        f.printf("%s,%.2f,%.1f,%.1f\n", fmtTime(_buf[i].t).c_str(),
                 _buf[i].v, _buf[i].w, _buf[i].target);
    }
    f.close();
    Serial.printf("已保存 CSV: %s (%u 条)\n", name.c_str(), _count);
    return true;
}

// ---------------- 文件管理 ----------------

void Recorder::listFiles() {
    File root = LittleFS.open("/");
    if (!root) { Serial.println("!! LittleFS 打开失败"); return; }
    File f = root.openNextFile();
    uint32_t total = 0;
    int cnt = 0;
    Serial.println("--- CSV 文件列表 ---");
    while (f) {
        String n = String(f.name());
        if (n.startsWith("/")) n = n.substring(1);   // 部分版本带前导 '/'
        if (!f.isDirectory() && n.endsWith(".csv")) {
            cnt++;
            total += f.size();
            Serial.printf("  %s  (%u bytes)\n", n.c_str(), (unsigned)f.size());
        }
        f = root.openNextFile();
    }
    if (!cnt) Serial.println("  (空)");
    Serial.printf("共 %d 个文件, %u bytes\n", cnt, (unsigned)total);
}

bool Recorder::deleteFile(const char* name) {
    String safe = sanitize(name);
    if (safe.isEmpty()) { Serial.println("文件名无效"); return false; }
    if (LittleFS.remove("/" + safe)) {
        Serial.printf("已删除 %s\n", safe.c_str());
        return true;
    }
    Serial.printf("删除失败或文件不存在: %s\n", safe.c_str());
    return false;
}

void Recorder::clearFiles() {
    // 先收集文件名再删除:遍历中删除会破坏 LittleFS 迭代器,导致漏删
    std::vector<String> names;
    File root = LittleFS.open("/");
    if (root) {
        File f = root.openNextFile();
        while (f) {
            String n = String(f.name());
            if (n.startsWith("/")) n = n.substring(1);
            if (!f.isDirectory() && n.endsWith(".csv")) names.push_back(n);
            f = root.openNextFile();
        }
    }
    int cnt = 0;
    for (auto& n : names) {
        if (LittleFS.remove("/" + n)) cnt++;
    }
    Serial.printf("已清空 %d 个文件\n", cnt);
}

void Recorder::catFile(const char* name) {
    String safe = sanitize(name);
    if (safe.isEmpty()) { Serial.println("文件名无效"); return; }
    File f = LittleFS.open("/" + safe, "r");
    if (!f) { Serial.println("文件不存在"); return; }
    while (f.available()) Serial.write(f.read());
    Serial.println();
    f.close();
}

// ---------------- 显示 ----------------

void Recorder::show(uint32_t maxLines) {
    if (!_count) {
        Serial.println("当前无记录(按下按钮开始记录)");
        return;
    }
    uint32_t n = min(maxLines, _count);
    Serial.printf("--- 当前记录 (显示前 %u / 共 %u 条) ---\n", n, _count);
    for (uint32_t i = 0; i < n; i++) {
        Serial.printf("  %s  %.2f ℃\n", fmtTime(_buf[i].t).c_str(), _buf[i].v);
    }
}
