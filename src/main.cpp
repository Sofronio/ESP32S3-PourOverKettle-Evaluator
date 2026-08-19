#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <time.h>
#include <sys/time.h>
#include <vector>
#include <Adafruit_NeoPixel.h>

#include "../include/config.h"
#include "max31855.h"
#include "recorder.h"
#include "html.h"

// ==================== 硬件 ====================
Max31855 thermo;
Adafruit_NeoPixel led(1, PIN_LED, NEO_GRB + NEO_KHZ800);
WebServer server(HTTP_PORT);
DNSServer dnsServer;                              // Captive Portal DNS (官方库, 同 GaggiMate)
IPAddress apIp(AP_IP_1, AP_IP_2, AP_IP_3, AP_IP_4);
IPAddress apMask(255, 255, 255, 0);

// ==================== 全局状态 ====================
char wifiSsid[65] = WIFI_DEFAULT_SSID;
char wifiPass[65] = WIFI_DEFAULT_PASS;

enum net_state_t { NET_CONNECTING, NET_CONNECTED, NET_FAILED };
net_state_t netState = NET_CONNECTING;
bool hostMode = HOST_DEFAULT;    // 默认 Host(AP) 模式
bool apRunning = false;          // AP 实际运行(host 模式或 STA 失败兜底)
uint32_t wifiStartMs = 0;
uint32_t lastWifiRetry = 0;

bool timeSynced = false;
float lastTemp = NAN;
uint8_t lastFault = 0;
uint32_t lastSampleMs = 0, lastPrintMs = 0, lastLedMs = 0;

bool btnLast = HIGH;
uint32_t btnDownMs = 0;

Recorder rec;

// ==================== 手冲壶管理 ====================
struct Kettle { String name; float dry, lid, cap; };
std::vector<Kettle> kettles;
String currentKettle;      // 当前选中壶名(未建壶时为空)
static const char* KETTLE_PATH = "/kettles.txt";

// 当前称量状态(WebUI 同步, 写入 CSV 文件头/每行水量)
float sessionTotal = 0;    // 当前总重 g
float sessionWater = 0;    // 当前水量 g (总重 - 空壶 - 有盖?盖重)
bool lidOn = true;         // 有盖 / 无盖

// 图表目标温度(默认 93℃) + 初始设定温度(基准, 默认 25℃), 持久化
// init 用于分析时排除温度计恒定偏差: 以增量(相对初始温度)评估控制
float targetTemp = 93.0;
float initTemp = 25.0;
static const char* TARGET_PATH = "/target.txt";

static void loadTarget() {
    File f = LittleFS.open(TARGET_PATH, "r");
    if (f) {
        String s = f.readStringUntil('\n'); s.trim();
        String s2 = f.readStringUntil('\n'); s2.trim();
        f.close();
        float v = s.toFloat();
        if (v >= 15 && v <= 110) targetTemp = v;
        float iv = s2.toFloat();
        if (iv >= 0 && iv <= 60) initTemp = iv;   // 旧格式单行时保持默认 25
    }
}

static void saveTarget() {
    File f = LittleFS.open(TARGET_PATH, "w");
    if (!f) return;
    f.printf("%.1f\n%.1f\n", targetTemp, initTemp);
    f.close();
}

static void loadKettles() {
    kettles.clear();
    File f = LittleFS.open(KETTLE_PATH, "r");
    if (f) {
        while (f.available()) {
            String line = f.readStringUntil('\n'); line.trim();
            if (line.isEmpty()) continue;
            // 格式: name|dry|lid|cap (旧版 3 段无 cap, 兼容)
            int p1 = line.indexOf('|');
            int p2 = line.indexOf('|', p1 + 1);
            int p3 = line.indexOf('|', p2 + 1);
            if (p1 <= 0 || p2 <= p1 + 1) continue;
            Kettle k;
            k.name = line.substring(0, p1);
            k.dry = line.substring(p1 + 1, p2).toFloat();
            k.lid = (p3 > 0) ? line.substring(p2 + 1, p3).toFloat() : line.substring(p2 + 1).toFloat();
            k.cap = (p3 > 0) ? line.substring(p3 + 1).toFloat() : 0;
            if (k.name.length() && k.name.length() <= 20) kettles.push_back(k);
        }
        f.close();
    }
    if (!kettles.empty()) currentKettle = kettles[0].name;
    rec.setKettle(currentKettle.c_str());
    Serial.printf("手冲壶: 已加载 %u 个\n", kettles.size());
}

static void saveKettles() {
    File f = LittleFS.open(KETTLE_PATH, "w");
    if (!f) return;
    for (auto& k : kettles) {
        f.printf("%s|%.1f|%.1f|%.1f\n", k.name.c_str(), k.dry, k.lid, k.cap);
    }
    f.close();
}

static String jsonEsc(const String& s) {
    String r;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') r += '\\';
        r += c;
    }
    return r;
}

// ==================== 工具 ====================

static void setLed(uint8_t r, uint8_t g, uint8_t b) {
    led.setPixelColor(0, r, g, b);
    led.show();
}

// LED: 默认绿 → WiFi 连接中; 蓝 → 连接成功; 黄 → 失败;
//      琥珀 → Host(AP) 有设备接入; 记录中 → 红(最高优先)
static void updateLed() {
    if (rec.isRecording()) { setLed(255, 0, 0); return; }
    if (apRunning && WiFi.softAPgetStationNum() > 0) { setLed(255, 191, 0); return; }
    switch (netState) {
        case NET_CONNECTED: setLed(0, 0, 255); break;
        case NET_FAILED:    setLed(255, 255, 0); break;
        default:            setLed(0, 255, 0);
    }
}

static String nowStr() {
    time_t t = time(nullptr);
    if (t < 1600000000) return String("--:--:--");
    struct tm tm;
    localtime_r(&t, &tm);
    char b[32];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm);
    return String(b);
}

// ==================== 配置持久化 (LittleFS) ====================

static const char* CONFIG_PATH = "/config.txt";

static bool saveConfig() {
    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) return false;
    f.println(wifiSsid);
    f.println(wifiPass);
    f.close();
    return true;
}

static void loadConfig() {
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) return;   // 无配置 → 不连 STA, 仅 Host(AP) 模式
    String s = f.readStringUntil('\n'); s.trim();
    String p = f.readStringUntil('\n'); p.trim();
    f.close();
    if (s.length() && p.length() && s.length() < 64 && p.length() < 64) {
        strncpy(wifiSsid, s.c_str(), 64);
        strncpy(wifiPass, p.c_str(), 64);
    }
}

// ==================== WiFi ====================

static void syncNtp(bool wait) {
    configTzTime("CST-8", NTP_SERVER1, NTP_SERVER2);
    uint32_t t0 = millis();
    while (wait && millis() - t0 < 5000) {
        if (time(nullptr) >= 1600000000) break;
        delay(100);
    }
    timeSynced = time(nullptr) >= 1600000000;
    if (timeSynced) Serial.printf("NTP: 时间同步成功 %s\n", nowStr().c_str());
    else Serial.println("NTP: 同步失败(可用 time 命令手动设置)");
}

// 连接已保存 WiFi(保持当前 AP 状态)
static void startSta() {
    netState = NET_CONNECTING;
    if (strlen(wifiSsid) == 0) {   // 未配置凭据 → 仅 Host(AP) 模式
        netState = NET_FAILED;
        Serial.println("未配置 WiFi SSID(仅 Host/AP 模式;请在 WebUI 或 wifi 命令中设置)");
        updateLed();
        return;
    }
    WiFi.mode((hostMode || apRunning) ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(wifiSsid, wifiPass);
    wifiStartMs = millis();
    Serial.printf("WiFi: 正在连接 '%s' ...\n", wifiSsid);
    updateLed();
}

// 启动 Host(AP) 模式: 开放无密码, IP 4.4.4.1
static void startHost() {
    hostMode = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(apIp, apIp, apMask);
    WiFi.softAP(AP_SSID);
    apRunning = true;
    Serial.printf("Host模式: AP '%s'(开放无密码) 管理页 http://4.4.4.1/\n", AP_SSID);
    if (WiFi.status() != WL_CONNECTED) startSta();   // 同时后台连接已保存 WiFi
    else updateLed();
}

// 退出 Host 模式 → STA 模式(失败时自动 AP 兜底)
static void stopHost() {
    hostMode = false;
    if (apRunning) {
        WiFi.softAPdisconnect(true);
        apRunning = false;
    }
    Serial.println("已切换到 STA 模式");
    if (WiFi.status() != WL_CONNECTED) startSta();
    else updateLed();
}

// 周期任务: STA 状态跟踪 / 超时判定 / 后台重连 / AP 兜底
static void wifiTick() {
    if (strlen(wifiSsid) == 0) return;   // 未配置 → 不尝试 STA
    bool connected = (WiFi.status() == WL_CONNECTED);
    if (connected) {
        if (netState != NET_CONNECTED) {
            netState = NET_CONNECTED;
            Serial.printf("WiFi: 连接成功 '%s', IP %s\n", wifiSsid, WiFi.localIP().toString().c_str());
            syncNtp(true);
            updateLed();
        }
        return;
    }
    if (netState == NET_CONNECTED) {          // 刚掉线 → 重新计时
        netState = NET_CONNECTING;
        wifiStartMs = millis();
        updateLed();
    }
    if (millis() - wifiStartMs > WIFI_TIMEOUT_MS && netState != NET_FAILED) {
        netState = NET_FAILED;
        Serial.println("WiFi: 连接失败(超时)");
        if (!hostMode && !apRunning) {        // STA 模式失败 → AP 兜底保证 WebUI 可达
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAPConfig(apIp, apIp, apMask);
            WiFi.softAP(AP_SSID);
            apRunning = true;
            Serial.printf("已开启 AP 兜底: http://4.4.4.1/ (黄灯)\n");
        }
        updateLed();
    }
    if (millis() - lastWifiRetry > WIFI_RETRY_MS) {
        lastWifiRetry = millis();
        if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    }
}

// ==================== 记录控制 ====================

static void snapshotSession();   // 定义见下方 API 区

static void toggleRecord() {
    if (rec.isRecording()) {
        rec.setEndTemp(lastTemp);   // 结束温度 → CSV 文件名
        rec.stop();                 // 内部自动保存为 CSV 文件
    } else {
        rec.setStartTemp(lastTemp); // 起始温度 → CSV 文件名
        snapshotSession();
        if (!rec.start()) {
            Serial.println("!! 记录失败: 缓冲区不可用");
        }
    }
    updateLed();
}

// ==================== 按钮 (GPIO38, 内部上拉) ====================

static void buttonTick() {
    bool btn = digitalRead(PIN_BUTTON);
    if (btn == HIGH && btnLast == LOW) {          // 释放
        uint32_t d = millis() - btnDownMs;
        if (d >= 20 && d <= 3000) {               // 消抖 20ms
            Serial.println("按钮按下");
            toggleRecord();
        }
    } else if (btn == LOW && btnLast == HIGH) {
        btnDownMs = millis();
    }
    btnLast = btn;
}

// ==================== Web API ====================

static void apiState() {
    String ip = apRunning ? apIp.toString() : WiFi.localIP().toString();
    String json = "{";
    json += "\"t\":" + String(isnan(lastTemp) ? "null" : String(lastTemp, 2));
    json += ",\"fault\":" + String(lastFault);
    json += ",\"faultStr\":\"" + String(Max31855::faultStr(lastFault)) + "\"";
    json += ",\"rec\":" + String(rec.isRecording() ? "true" : "false");
    json += ",\"recCount\":" + String(rec.count());
    json += ",\"host\":" + String(apRunning ? "true" : "false");
    json += ",\"apSsid\":\"" + String(AP_SSID) + "\"";
    json += ",\"staNum\":" + String(apRunning ? WiFi.softAPgetStationNum() : 0);
    json += ",\"conn\":" + String(netState == NET_CONNECTED ? "true" : "false");
    json += ",\"ssid\":\"" + String(wifiSsid) + "\"";
    json += ",\"ip\":\"" + ip + "\"";
    json += ",\"rssi\":" + String(netState == NET_CONNECTED ? WiFi.RSSI() : 0);
    json += ",\"time\":" + String((unsigned long)time(nullptr));
    json += ",\"synced\":" + String(timeSynced ? "true" : "false");
    json += ",\"psram\":" + String(ESP.getFreePsram());
    json += ",\"kettle\":\"" + jsonEsc(currentKettle) + "\"";
    json += ",\"water\":" + String(sessionWater, 1);
    json += ",\"total\":" + String(sessionTotal, 1);
    json += ",\"lid\":" + String(lidOn ? "true" : "false");
    json += ",\"target\":" + String(targetTemp, 1);
    json += ",\"init\":" + String(initTemp, 1);
    json += "}";
    server.send(200, "application/json", json);
}

// 开始记录时快照当前壶参数/称量状态 (CSV 文件头)
static void snapshotSession() {
    float dry = 0, lidW = 0, cap = 0;
    for (auto& k : kettles) {
        if (k.name == currentKettle) { dry = k.dry; lidW = k.lid; cap = k.cap; break; }
    }
    rec.setLidOn(lidOn);
    rec.setSessionInfo(dry, lidW, cap, sessionTotal, sessionWater, initTemp);
}

static void apiRecord() {
    String action = server.arg("action");
    if (action == "start") {
        rec.setStartTemp(lastTemp);
        snapshotSession();
        if (rec.start()) Serial.println(">> WebUI: 记录开始");
    } else if (action == "stop") {
        if (rec.isRecording()) {
            rec.setEndTemp(lastTemp);
            rec.stop();   // 自动保存
        }
    }
    updateLed();
    server.send(200, "application/json",
                "{\"ok\":true,\"rec\":" + String(rec.isRecording() ? "true" : "false") + "}");
}

static void apiFiles() {
    String out = "[";
    File root = LittleFS.open("/");
    if (root) {
        File f = root.openNextFile();
        while (f) {
            String n = String(f.name());
            if (n.startsWith("/")) n = n.substring(1);   // 部分版本带前导 '/'
            if (!f.isDirectory() && n.endsWith(".csv")) {
                if (out.length() > 1) out += ",";
                out += "{\"name\":\"" + n + "\",\"size\":" + String(f.size()) + "}";
            }
            f = root.openNextFile();
        }
    }
    out += "]";
    server.send(200, "application/json", out);
}

static void apiDownload() {
    String name = Recorder::sanitize(server.arg("name").c_str());
    if (name.isEmpty() || !LittleFS.exists("/" + name)) {
        server.send(404, "text/plain", "404 Not Found");
        return;
    }
    File f = LittleFS.open("/" + name, "r");
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    server.streamFile(f, "text/csv");
    f.close();
}

static void apiDelete() {
    bool ok = Recorder::deleteFile(server.arg("name").c_str());
    server.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

static void apiClear() {
    Recorder::clearFiles();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void apiConfig() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.isEmpty() || ssid.length() >= 64 || pass.length() >= 64) {
        server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    strncpy(wifiSsid, ssid.c_str(), 64);
    strncpy(wifiPass, pass.c_str(), 64);
    saveConfig();
    Serial.printf("WebUI: 保存 WiFi 配置 %s, 重新连接...\n", wifiSsid);
    startSta();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void apiHost() {
    String mode = server.arg("mode");
    if (mode == "sta") stopHost();
    else startHost();
    server.send(200, "application/json",
                "{\"ok\":true,\"host\":" + String(apRunning ? "true" : "false") + "}");
}

static void apiKettles() {
    String out = "{\"kettles\":[";
    for (size_t i = 0; i < kettles.size(); i++) {
        if (i) out += ",";
        out += "{\"name\":\"" + jsonEsc(kettles[i].name) + "\",\"dry\":" +
               String(kettles[i].dry, 1) + ",\"lid\":" + String(kettles[i].lid, 1) +
               ",\"cap\":" + String(kettles[i].cap, 1) + "}";
    }
    out += "],\"cur\":\"" + jsonEsc(currentKettle) + "\"}";
    server.send(200, "application/json", out);
}

static void apiKettleSave() {
    String name = server.arg("name"); name.trim();
    float dry = server.arg("dry").toFloat();
    float lid = server.arg("lid").toFloat();
    float cap = server.arg("cap").toFloat();
    if (name.isEmpty() || name.length() > 20 || dry < 0 || lid < 0 || cap < 0) {
        server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    bool found = false;
    for (auto& k : kettles) {
        if (k.name == name) { k.dry = dry; k.lid = lid; k.cap = cap; found = true; break; }
    }
    if (!found) kettles.push_back({name, dry, lid, cap});
    saveKettles();
    Serial.printf("手冲壶已保存: %s (空壶 %.1fg, 盖 %.1fg, 容量 %.1fml)\n",
                  name.c_str(), dry, lid, cap);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void apiKettleDelete() {
    String name = server.arg("name");
    for (auto it = kettles.begin(); it != kettles.end(); ++it) {
        if (it->name == name) {
            kettles.erase(it);
            saveKettles();
            if (currentKettle == name) {
                currentKettle = kettles.empty() ? "" : kettles[0].name;
                rec.setKettle(currentKettle.c_str());
            }
            Serial.printf("手冲壶已删除: %s\n", name.c_str());
            break;
        }
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void apiKettleSelect() {
    String name = server.arg("name");
    for (auto& k : kettles) {
        if (k.name == name) {
            currentKettle = name;
            rec.setKettle(currentKettle.c_str());
            break;
        }
    }
    server.send(200, "application/json",
                "{\"ok\":true,\"cur\":\"" + jsonEsc(currentKettle) + "\"}");
}

// 设置图表目标温度(15–110℃) 与初始设定温度(0–60℃ 基准)
static void apiTarget() {
    float v = server.arg("t").toFloat();
    if (v < 15 || v > 110) {
        server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    targetTemp = v;
    String initStr = server.arg("init");
    if (initStr.length()) {
        float iv = initStr.toFloat();
        if (iv >= 0 && iv <= 60) initTemp = iv;
    }
    saveTarget();
    Serial.printf("目标 %.1f ℃, 初始基准 %.1f ℃\n", targetTemp, initTemp);
    server.send(200, "application/json",
                "{\"ok\":true,\"target\":" + String(targetTemp, 1) +
                ",\"init\":" + String(initTemp, 1) + "}");
}

// 同步当前称量状态: 有盖/无盖 + 当前总重 → 设备端算水量
static void apiKettleState() {
    String lid = server.arg("lid");
    if (lid == "on") lidOn = true;
    else if (lid == "off") lidOn = false;
    String totalStr = server.arg("total");
    if (totalStr.length()) sessionTotal = totalStr.toFloat();

    float dry = 0, lidW = 0;
    for (auto& k : kettles) {
        if (k.name == currentKettle) { dry = k.dry; lidW = k.lid; break; }
    }
    sessionWater = (sessionTotal > 0) ? max(0.0f, sessionTotal - dry - (lidOn ? lidW : 0)) : 0;
    rec.setLidOn(lidOn);

    String json = String("{\"ok\":true,\"water\":") + String(sessionWater, 1) +
                  ",\"total\":" + String(sessionTotal, 1) +
                  ",\"lid\":" + (lidOn ? "true" : "false") + "}";
    server.send(200, "application/json", json);
}

static void setupServer() {
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", INDEX_HTML);
    });
    server.on("/api/state", HTTP_GET, apiState);
    server.on("/api/record", HTTP_POST, apiRecord);
    server.on("/api/files", HTTP_GET, apiFiles);
    server.on("/download", HTTP_GET, apiDownload);
    server.on("/api/delete", HTTP_POST, apiDelete);
    server.on("/api/clear", HTTP_POST, apiClear);
    server.on("/api/config", HTTP_POST, apiConfig);
    server.on("/api/host", HTTP_POST, apiHost);
    server.on("/api/kettles", HTTP_GET, apiKettles);
    server.on("/api/kettles", HTTP_POST, apiKettleSave);
    server.on("/api/kettles/delete", HTTP_POST, apiKettleDelete);
    server.on("/api/kettles/select", HTTP_POST, apiKettleSelect);
    server.on("/api/kettle/state", HTTP_POST, apiKettleState);
    server.on("/api/target", HTTP_POST, apiTarget);
    // Captive Portal: 显式注册各平台探测路径(同 GaggiMate 方案), 全部跳转管理页
    auto portalRedirect = []() {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/html", "");
    };
    server.on("/generate_204", portalRedirect);        // Android
    server.on("/redirect", portalRedirect);            // Microsoft
    server.on("/hotspot-detect.html", portalRedirect); // Apple
    server.on("/canonical.html", portalRedirect);      // Firefox
    server.on("/ncsi.txt", portalRedirect);            // Windows
    server.on("/connecttest.txt", portalRedirect);     // Windows 11
    server.on("/success.txt", []() { server.send(200); });  // Firefox 判定有网
    server.on("/wpad.dat", []() { server.send(404); });     // Windows 10 反复请求防护
    // 兜底: 任意域名/路径 → 跳转管理页
    server.onNotFound([]() {
        String uri = server.uri();
        if (uri.startsWith("/api/") || uri.startsWith("/download")) {
            server.send(404, "text/plain", "404 Not Found");
            return;
        }
        server.sendHeader("Location", "/", true);
        server.send(302, "text/html", "");
    });
    server.begin();
}

// ==================== 串口 CLI ====================

static void cliHelp() {
    Serial.println("--- 串口命令 ---");
    Serial.println("  ls                列出已保存 CSV 文件");
    Serial.println("  del <文件名>      删除指定文件");
    Serial.println("  clr               清空所有文件");
    Serial.println("  show              显示 PSRAM 中当前记录(前200条)");
    Serial.println("  cat <文件名>      显示文件内容");
    Serial.println("  time Y M D h m s  手动设置时间");
    Serial.println("  sync              NTP 时间同步");
    Serial.println("  wifi <ssid> <pass>  连接新 WiFi 并保存");
    Serial.println("  host              切换 Host(AP) / STA 模式");
    Serial.println("  rec / stop        开始 / 停止记录");
    Serial.println("  temp              立即读取一次温度");
    Serial.println("  stats             内存 / 记录 / WiFi 统计");
    Serial.println("  help              显示本帮助");
}

static void cliRun(char* line) {
    char* args[8];
    int n = 0;
    char* p = strtok(line, " ");
    while (p && n < 8) { args[n++] = p; p = strtok(NULL, " "); }
    if (n == 0) return;

    String cmd = args[0];
    if (cmd == "help") {
        cliHelp();
    } else if (cmd == "ls") {
        Recorder::listFiles();
    } else if (cmd == "del" && n >= 2) {
        Recorder::deleteFile(args[1]);
    } else if (cmd == "clr") {
        Recorder::clearFiles();
    } else if (cmd == "show") {
        rec.show(200);
    } else if (cmd == "cat" && n >= 2) {
        Recorder::catFile(args[1]);
    } else if (cmd == "time" && n == 7) {
        int y = atoi(args[1]), mo = atoi(args[2]), d = atoi(args[3]);
        int h = atoi(args[4]), mi = atoi(args[5]), s = atoi(args[6]);
        if (y >= 2020 && y <= 2099 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 &&
            h >= 0 && h < 24 && mi >= 0 && mi < 60 && s >= 0 && s < 60) {
            struct tm tmv = {};
            tmv.tm_year = y - 1900; tmv.tm_mon = mo - 1; tmv.tm_mday = d;
            tmv.tm_hour = h; tmv.tm_min = mi; tmv.tm_sec = s;
            time_t e = mktime(&tmv);
            struct timeval tv = { e, 0 };
            settimeofday(&tv, NULL);
            timeSynced = true;
            Serial.printf("时间已设置: %s\n", nowStr().c_str());
        } else {
            Serial.println("参数无效: time Y M D h m s");
        }
    } else if (cmd == "sync") {
        syncNtp(true);
    } else if (cmd == "wifi" && n >= 3) {
        strncpy(wifiSsid, args[1], 64);
        strncpy(wifiPass, args[2], 64);
        saveConfig();
        Serial.printf("已保存并连接 WiFi: %s\n", wifiSsid);
        startSta();
    } else if (cmd == "host") {
        if (hostMode) stopHost(); else startHost();
    } else if (cmd == "rec") {
        if (!rec.isRecording()) { rec.start(); updateLed(); }
    } else if (cmd == "stop") {
        if (rec.isRecording()) { rec.stop(); updateLed(); }
    } else if (cmd == "temp") {
        uint8_t f;
        float t = thermo.read(f);
        if (isnan(t)) Serial.printf("温度: 故障 (%s)\n", Max31855::faultStr(f));
        else Serial.printf("温度: %.2f ℃\n", t);
    } else if (cmd == "stats") {
        Serial.printf("堆: 空闲 %u / %u bytes; PSRAM: 空闲 %u / %u bytes\n",
                      ESP.getFreeHeap(), ESP.getHeapSize(),
                      ESP.getFreePsram(), ESP.getPsramSize());
        Serial.printf("记录: 缓冲 %u 条, 当前 %u 条, 状态 %s\n",
                      rec.capacity(), rec.count(), rec.isRecording() ? "记录中" : "未记录");
        Serial.printf("网络: %s %s, RSSI %d dBm, IP %s, 时间 %s\n",
                      hostMode ? "Host模式" : "STA模式",
                      (netState == NET_CONNECTED) ? "已连接" : (netState == NET_FAILED ? "连接失败" : "连接中"),
                      WiFi.RSSI(),
                      (apRunning ? apIp.toString() : WiFi.localIP().toString()).c_str(),
                      nowStr().c_str());
    } else {
        Serial.printf("未知命令: %s (输入 help 查看)\n", cmd.c_str());
    }
}

static void cliTick() {
    static char cliBuf[160];
    static uint8_t cliLen = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            cliBuf[cliLen] = 0;
            if (cliLen) cliRun(cliBuf);
            cliLen = 0;
        } else if (c != '\r' && cliLen < sizeof(cliBuf) - 1) {
            cliBuf[cliLen++] = c;
        }
    }
}

// ==================== 串口状态输出 ====================

static void printStatus() {
    String tstr = isnan(lastTemp) ? String("--") : String(lastTemp, 2);
    String fstr = Max31855::faultStr(lastFault);
    Serial.printf("[%s] 温度:%s ℃ | %s | 缓冲:%u%s\n",
                  nowStr().c_str(), tstr.c_str(),
                  rec.isRecording() ? "记录中" : "未记录", rec.count(),
                  fstr.isEmpty() ? "" : (" | 故障:" + fstr).c_str());
}

// ==================== setup / loop ====================

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("===== ESP32-S3 手冲壶评测工具 (MAX31855) =====");

    pinMode(PIN_BUTTON, INPUT_PULLUP);

    if (!LittleFS.begin(true)) Serial.println("!! LittleFS 挂载失败");

    led.begin();
    led.setBrightness(26);        // 10% 亮度, 防止过亮刺眼
    setLed(0, 255, 0);            // 默认绿色

    thermo.begin();
    rec.begin();

    loadConfig();
    Serial.printf("WiFi 配置: %s / %s\n", wifiSsid, wifiPass);
    loadKettles();
    loadTarget();
    Serial.printf("目标温度: %.1f ℃\n", targetTemp);

    setenv("TZ", "CST-8", 1);     // 固定 UTC+8, 无夏令时
    tzset();

    startHost();                  // 默认 Host(AP) 模式
    setupServer();
    dnsServer.setTTL(3600);
    dnsServer.start(53, "*", apIp);   // 通配域名 → 4.4.4.1 (Captive Portal)
    Serial.println("Captive Portal: DNS(53) 通配 → 4.4.4.1");

    Serial.println("Web 服务已启动 (输入 help 查看串口命令)");
    cliHelp();
}

void loop() {
    server.handleClient();
    dnsServer.processNextRequest();
    wifiTick();
    buttonTick();
    cliTick();

    // NTP 兜底: 启动时 5s 窗口内没同步成功, 但时间已悄然正确 → 补记标志
    if (!timeSynced && time(nullptr) >= 1600000000) {
        timeSynced = true;
        Serial.printf("NTP: 时间同步成功 %s\n", nowStr().c_str());
    }

    uint32_t now = millis();
    if (now - lastSampleMs >= SAMPLE_MS) {
        lastSampleMs = now;
        lastTemp = thermo.read(lastFault);
    }
    if (now - lastPrintMs >= PRINT_MS) {
        lastPrintMs = now;
        if (rec.isRecording() && !isnan(lastTemp)) {
            rec.addSample(timeSynced ? (uint32_t)time(nullptr) : (uint32_t)(millis() / 1000),
                          lastTemp, sessionWater, targetTemp);
        }
        printStatus();
    }
    if (now - lastLedMs >= 200) {
        lastLedMs = now;
        updateLed();
    }
}
