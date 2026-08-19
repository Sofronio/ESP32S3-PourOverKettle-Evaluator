# ESP32-S3 手冲壶评测工具项目计划 (plan.md)

> 更新日期:2026-08-19

## 1. 项目目标

基于 YD-ESP32-S3 开发板 + MAX31855 热电偶测温模块,构建一个带 Web UI 的实时温度计:

- 实时温度显示 + 图表(横轴时间、纵轴温度,固定 15–110 ℃)
- 按钮/WebUI 控制温度记录(存 PSRAM,停止时保存为 CSV 文件)
- CSV 文件列表:下载 / 删除 / 清空
- WiFi 连接 + NTP 时间同步
- 串口 CLI 命令控制
- STA / Host(AP)模式切换
- 板载 WS2812 RGB LED 状态指示

## 2. 硬件与引脚定义

| 信号 | 引脚 | 说明 |
|---|---|---|
| MAX31855 MISO | GPIO4  | SPI 输入 |
| MAX31855 SCK  | GPIO6  | SPI 时钟 |
| MAX31855 CS   | GPIO7  | SPI 片选 |
| 按钮           | GPIO38 | 内部上拉,接地触发;控制记录开始/停止 |
| 板载 WS2812   | GPIO48 | YD-ESP32-S3 三色 LED(NEO_GRB, 800kHz) |

- 板卡:YD-ESP32-S3(**16MB Flash / 8MB PSRAM 版**,八线 PSRAM → qio_opi)
- PlatformIO 无该板卡,使用 `esp32-s3-devkitc-1` + 自定义 16MB 分区表(单 App 6MB + LittleFS ~9.9MB)
- MAX31855 用硬件 SPI(mode 0,1MHz),CS 手动控制

## 3. LED 状态定义

| 状态 | 颜色 | 说明 |
|---|---|---|
| 默认(上电/连接中) | 绿 | LED 默认状态 |
| WiFi(STA)连接成功 | 蓝 | 已保存 WiFi 连接成功 |
| WiFi(STA)连接失败 | 黄 | STA 连接失败(Host 模式下 AP 照常运行;STA 模式下自动 AP 兜底) |
| Host(AP)有设备接入 | 琥珀 | softAP 有客户端时显示(优先于 STA 状态色) |
| 记录中(按钮按下开始) | 红 | 覆盖其他颜色,最高优先 |

- 亮度固定 **10%**(`setBrightness(26)`)
- 记录停止后 LED 回退到网络状态色

## 4. 功能设计

### 4.1 温度采集
- 硬件 SPI 读取 MAX31855 32 位帧:TC 温度 14 位有符号 ×0.25 ℃,冷端补偿已内建
- 故障位检测:开路(Open)、对地短路(Short GND)、对电源短路(Short VCC)、无器件(全 1)
- 采样周期 500 ms,串口每 1 s 输出一次温度 + 记录状态

### 4.2 WiFi / 网络模式
- **默认 Host(AP)模式(上电即开)**:开放网络无密码,SSID `ESP32S3-Thermo`,静态 IP **4.4.4.1**
  - Captive Portal(同 GaggiMate 方案):官方 `DNSServer` 库(UDP 53)通配解析所有域名 → 4.4.4.1;显式注册平台探测路径 `/generate_204`(Android)、`/hotspot-detect.html`(Apple)、`/connecttest.txt`、`/ncsi.txt`(Windows)、`/canonical.html`(Firefox)等 → 302 跳转管理页;`/success.txt`→200、`/wpad.dat`→404;其余任意路径 onNotFound 兜底 302
- 同时后台连接已保存 WiFi(**不内置默认凭据**,通过 WebUI/串口 `wifi` 命令设置后持久化到 `config.txt`):
  - 连接成功 → LED 蓝 + NTP 同步;失败 → LED 黄,AP 照常运行
  - 纯 STA 模式下失败 → 自动开 AP 兜底(同 4.4.4.1),后台每 30 s 重试
- `wifi <ssid> <pass>` 指令 / WebUI 设置页 → 保存到 LittleFS 并重连
- `host` 指令 / WebUI 按钮:切换 Host(AP) ⇄ STA 模式

### 4.3 记录与存储
- 按钮(消抖 20 ms)或 WebUI 按钮:开始/停止记录
- 记录期间 LED 变红;数据按 `{epoch秒, 温度, 水量, 目标温度}` 追加到 PSRAM 缓冲区(16 字节/条,默认 250000 条 ≈ 4 MB)
- 停止时把 PSRAM 缓冲区写成 CSV 文件,文件名带壶名和起止温度:
  `fellow_23.5c-98.4c_20260819_163000.csv`(无壶时前缀 `rec`;时间未同步时用 `boot<秒>`)
- CSV 格式:
  - 首行注释 `# kettle:.., cap:..ml, lid:on/off, dry:.., lid_w:.., total:.., water:.., init:..c`
  - 表头 `time,temperature_c,water_g,target_c`,每行记录时间、温度、当时水量、当时目标温度(设定值 SV)
- WebUI:文件列表、单个下载、单个删除、清空全部(清空先收集名字再删,避免遍历中删除漏项)
- PSRAM 不可用时回退到普通堆内存并告警

### 4.7 手冲壶管理
- 左列"手冲壶管理"卡片:新建/编辑/删除壶(名称、空壶重、盖重、**标称容量**);**编辑**回填参数到输入框,保存即覆盖(同名);持久化到 LittleFS `/kettles.txt`(`名称|空壶重|盖重|容量` 每行,兼容旧 3 段格式)
- 图表顶部工具条:下拉选择当前壶 + **有盖/无盖**单选 + 输入**当前总重** → 实时计算水量(水量 = 总重 − 空壶 − 有盖?盖重,水量框只读显示)+ **目标温度**(默认 93℃,持久化 `/target.txt`,图表橙色虚线显示)
- 称量状态同步到设备(`POST /api/kettle/state`,设备端算水量)并写入 CSV
- 选中壶名用于 CSV 文件名前缀(自动做文件名安全化);默认选第一个壶
- API:`GET/POST /api/kettles`、`POST /api/kettles/delete`、`POST /api/kettles/select`、`POST /api/kettle/state`、`POST /api/target`

### 4.4 时间
- WiFi 连接成功后自动 NTP 同步(池:`pool.ntp.org` / `ntp.aliyun.com`,时区固定 UTC+8)
- 串口 `time Y M D h m s` 手动设时间;`sync` 手动 NTP 同步
- 未同步时记录用开机秒数,同步后用 epoch

### 4.5 Web UI (内嵌 HTML,无外部依赖,AP 模式无外网也能用)
- 当前温度卡片 + 记录状态指示
- 温度图表:Canvas 手绘,纵轴固定 15–110 ℃,横轴最近 5 分钟(1 s 轮询 `/api/temp`)
- 记录 / 停止按钮
- CSV 文件列表:下载 / 删除 / 清空
- SSID / 密码设置保存
- Host 模式切换按钮
- API 一览(JSON):
  - `GET /api/state` — 温度、记录状态、WiFi/AP 状态、IP、时间
  - `POST /api/record {action:start|stop}` — 开始/停止记录
  - `GET /api/files` — 已保存 CSV 列表
  - `GET /download?name=xxx` — 下载 CSV
  - `POST /api/delete {name}` / `POST /api/clear` — 删除/清空
  - `POST /api/config {ssid,pass}` — 保存并重连
  - `POST /api/host {mode:ap|sta}` — 切换 Host 模式

### 4.6 串口 CLI (115200,行式)
| 命令 | 功能 |
|---|---|
| `help` | 列出所有命令 |
| `ls` | 列出已保存 CSV 文件 |
| `del <文件名>` | 删除指定文件 |
| `clr` | 清空所有文件 |
| `show` | 显示 PSRAM 中的当前记录(最多 200 行) |
| `cat <文件名>` | 显示文件内容 |
| `time Y M D h m s` | 手动设置时间 |
| `sync` | NTP 时间同步 |
| `wifi <ssid> <pass>` | 连接新 WiFi 并保存配置 |
| `host` | 切换 STA / Host(AP)模式 |
| `rec` / `stop` | 开始/停止记录(与按钮等效) |
| `temp` | 立即读一次温度 |
| `stats` | 显示内存/PSRAM/记录统计 |

## 5. 文件结构

```
ESP32S3-PourOverKettle-Evaluator/
├── README.md / README.zh-CN.md   # 项目文档(英文为主, 中文对照, 互相链接)
├── plan.md            # 本文档
├── platformio.ini     # 平台配置 (espressif32@6.12.0)
├── partitions.csv     # 16MB 分区表: App 6MB + LittleFS ~9.9MB
├── boards/
│   └── YD-ESP32-S3.json   # 板卡定义 (qio_opi PSRAM/8MB/80MHz) — 启动必需, 勿删
├── include/
│   └── config.h       # 引脚、WiFi/AP、采样参数
├── src/
│   ├── main.cpp       # 主逻辑:WiFi/Host/Portal/WebServer/CLI/按钮/LED/手冲壶/API
│   ├── max31855.cpp/h # MAX31855 SPI 驱动
│   ├── recorder.cpp/h # PSRAM 记录 + CSV 保存 + 文件管理
│   └── html.h         # 内嵌 Web UI (HTML+JS, 含浏览器端 CSV 分析)
└── tools/
    └── analyze.py     # Python 版分析脚本 (生成 HTML 报表)
```

## 6. 实施步骤

1. ✅ 平台工程:platformio.ini(esp32-s3-devkitc-1,8MB flash,PSRAM 开启)
2. ✅ MAX31855 驱动 + 故障检测
3. ✅ 记录器:PSRAM 缓冲、CSV 保存、LittleFS 文件管理
4. ✅ WiFi(凭据由用户设置并持久化)+ 自动重连 + AP 兜底 + NTP
5. ✅ WebServer + 全部 REST API + 内嵌 WebUI
6. ✅ 串口 CLI 全部命令
7. ✅ 按钮 + WS2812 状态机
8. ✅ 编译验证、烧录、串口联调

## 7. 构建与烧录

```bash
pio run -t upload      # 编译并烧录
pio device monitor -b 115200   # 串口监视
```

## 4.8 PID 控制分析(规划)

**现有数据**(CSV 已含): `time` / `temperature_c`(PV) / `water_g` / `target_c`(SV)— 足够做**效果评估**

**待补充的两个重点(做参数诊断必需)**:
1. **加热输出量**(加热占空比 0-100% / PID 输出)— 归因 P/I/D 参数、做模型辨识的关键
   - 首选方案:光耦读原厂加热驱动信号(继电器/SSR 通断)到 ESP32 GPIO,每 100ms 记录 on/off
   - 备选:带数据输出的功率计,或电流互感器(ACS712/ZMPT101B)自建
   - 若 ESP32 接管加热:直接记录 PID 输出量,最简
2. **壶壁/环境温度**(第二路热电偶)— 散热项建模,区分稳态偏差来源

**无输出量时仍可做的效果评估**:到达时间、首次到达时间、超调量、稳态偏差、稳态振荡幅度、收敛性 → 加权打分(验收级结论,非参数诊断)

## 8. 待确认 / 备注

- 实际板卡为 16MB Flash / 8MB PSRAM;若日后换四线 PSRAM 板,改 `board_build.arduino.memory_type = qio_qspi`
- AP 名 `ESP32S3-Thermo`、管理页 IP `4.4.4.1`、时区 UTC+8 均可改 `config.h`
- MAX31855 模块如为 K 型热电偶,测温范围覆盖 15–110 ℃ 无压力
- 管理页无外网依赖(图表手绘 Canvas),AP 模式(无互联网)下功能完整

## 9. 新增功能记录 (2026-08-19)

按开发顺序记录后续需求迭代:

1. **手冲壶管理**:新建/编辑/删除壶(名称、空壶重、盖重、标称容量),编辑回填输入框、同名保存即覆盖;持久化 `/kettles.txt`
2. **图表顶部工具条**:壶选择下拉 + 有盖/无盖单选 + 当前总重输入 → 水量实时计算(水量 = 总重 − 空壶 − 有盖?盖重,水量框只读)+ 目标温度(默认 93℃)+ 初始设定温度(默认 25℃)
   - 称量状态 `POST /api/kettle/state` 同步到设备(设备端算水量,写入 CSV)
3. **CSV 记录扩展**:
   - 文件名:`<壶名>_<起始温>c-<结束温>c_<时间>.csv`
   - 文件头注释含壶信息(壶名/容量/盖状态/空壶/盖重/总重/水量/初始基准温度)
   - 列:`time, temperature_c, water_g, target_c`(PV/水量/SV 齐全,支持 PID 效果评估)
4. **WebUI 双模式**:`实时` / `CSV 分析` tab(严格互斥显示);分析模式选择文件自动出报告
   - 浏览器端完整移植分析逻辑(差分升温起点、稳态段识别、到达/超调/稳态偏差/振荡、反推功率、加权评分)
   - 报告 = 分数卡 + 指标表 + 三段配色图(蓝=未加热、红=升温、绿=稳态)
   - 错误页内文字提示(不弹窗);兼容 2/3/4 列旧 CSV 格式
5. **分析算法细节**:升温起点差分法(0.25℃ 跳变 + 后续 3 点确认);稳态段 = 首次进入 ±0.5℃ 且之后不再跑出 ±1℃ 的完整区间;时间轴以升温起点为 0:00,未加热段负半轴显示;y 轴余量 = 跨度 5%
6. **评分档位**(用户确认):到达 30 分(理论效率)、超调 25 分(≤0.5℃)、稳态偏差 25 分(≤0.3℃)、稳态振荡 20 分(±0.5 好 / ±1 一般 / ±2 差 / ±3 不可接受)
7. **图表标注**(用户确认):目标/初始/峰值文字集中在顶部居中;升温段/未加热文字段内对齐(未加热左对齐 y 轴起点);到达目标在底部最右;0:00 红色竖虚线 + 到达绿色竖虚线
8. **LED**:亮度 10%;Host(AP)有设备接入 → 琥珀色
9. **稳定性修复**:NTP 同步标志兜底、中文文件名 sanitize 放宽(UTF-8 字节数)、清空先收集再删、`[hidden]` CSS 覆盖修复、图表平移数据错位修复
10. **Python 分析脚本** `tools/analyze.py`:纯标准库,生成与 WebUI 一致的 HTML 报表(评分/三段 SVG 图/功率反推)

## 10. GitHub 整理清单

- [x] 项目改名 `ESP32S3-PourOverKettle-Evaluator`(网页标题同步为"ESP32-S3 手冲壶评测工具")
- [x] `analyze.py` 移入 `tools/`
- [x] 板卡文件改名 `boards/YD-ESP32-S3.json`(原 Gaggimate-Controller.json,内容即 YD-ESP32-S3 硬件配置)
- [x] README 中英文双份互链
- [ ] 提交前:删除测试用 CSV/HTML(`Fellow-黑色_*.csv/html`),补充 .gitignore(.pio/)
