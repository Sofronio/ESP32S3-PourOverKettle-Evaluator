# ESP32-S3 手冲壶评测工具

**中文** | [**English README**](README.md)

基于 ESP32-S3 + MAX31855 热电偶的**手冲壶温控性能评测工具**(Fellow / Brewista, Timemore 等电热壶通用)。记录水温曲线,携带壶/水量元数据保存为 CSV 文件,并给温控器(PID)表现打分——完全离线可用,带出去也能用。

![WebUI](https://img.shields.io/badge/WebUI-内嵌%2C%20零依赖-blue) ![平台](https://img.shields.io/badge/PlatformIO-espressif32%406.12.0-orange)

## 功能

- **实时温度图表** — Canvas 手绘,零外部资源,AP 模式无外网也能用
- **WebUI 中英双语** — 顶栏按钮一键切换(持久化到 localStorage),图表与分析报告同步;`tools/analyze.py` 支持 `--lang zh|en`
- **手冲壶管理** — 名称 / 空壶重 / 盖重 / 标称容量,编辑与删除,持久化到 LittleFS
- **称量联动** — 有盖/无盖切换,输入当前总重 → 实时计算水量(水量 = 总重 − 空壶 − 有盖?盖重),同步到设备并写入 CSV 每一行
- **目标温度 + 初始基准** — 目标温度(默认 93℃,图表橙色虚线)与初始设定温度(默认 25℃,分析时抵消热电偶恒定偏差);均持久化
- **PSRAM 记录** — 最多 25 万条采样(约 4MB),停止时自动保存为 CSV 文件;文件名携带壶名与起止温度:
  `fellow_23.5c-98.4c_20260819_163000.csv`
- **浏览器端 CSV 分析** — 选择已保存的记录即出完整报告:分数卡、指标表、三段配色图(蓝=未加热、红=升温、绿=稳态)、反推加热功率
- **Python 分析脚本**(`tools/analyze.py`)— 纯标准库,离线生成同样的 HTML 报表;评分:到达 30 / 超调 25 / 稳态偏差 25 / 稳态振荡 20
- **双网络** — 开放 Host AP `ESP32S3-Thermo`(4.4.4.1, captive portal 自动跳转)+ 连接已保存 WiFi 用于 NTP 同步
- **串口 CLI** — 文件管理、时间、wifi、host 切换、记录、统计等全套命令

## 硬件

| 部件 | 规格 |
|---|---|
| **主控** | 乐鑫 **ESP32-S3** — Xtensa LX7 双核 240MHz,512KB SRAM,Wi-Fi 802.11 b/g/n + BLE 5 |
| **模块** | ESP32-S3-WROOM-1 — **16MB 四线 Flash + 8MB 八线 PSRAM**(YD-ESP32-S3 板) |
| **温度传感器** | **MAX31855** K 型热电偶放大器 — SPI,0.25℃ 分辨率,内置冷端补偿,故障标志(开路/对地短路/对电源短路) |
| **热电偶** | K 型,如防水探头 — 量程覆盖 15–110℃ |
| **记录按钮** | 轻触开关,低电平有效 |
| **状态 LED** | 板载 WS2812 RGB(GPIO48) |

## 引脚与接线

| 信号 | 引脚 | 接线 |
|---|---|---|
| MAX31855 **MISO**(DO) | GPIO4 | 传感器 DO → GPIO4(SPI 输入) |
| MAX31855 **SCK**(CLK) | GPIO6 | 传感器 CLK ← GPIO6(SPI 时钟, 1MHz, mode 0) |
| MAX31855 **CS** | GPIO7 | 传感器 CS ← GPIO7(片选, 手动控制) |
| 记录**按钮** | GPIO38 | 按钮接 GND,内部上拉 |
| 板载 **RGB LED** | GPIO48 | WS2812(NEO_GRB, 800kHz),亮度 10% |

接线说明:MAX31855 是 3 线 SPI(MISO/SCK/CS,无需 MOSI);传感器模块 3.3V 供电、共地;热电偶接入模块 T+ / T− 端子。

板卡:**YD-ESP32-S3**(16MB Flash / 8MB 八线 PSRAM)。板卡定义在 [`boards/YD-ESP32-S3.json`](boards/YD-ESP32-S3.json)(qio_opi / 80MHz)— **不要删除**:没有它固件在这块板上会启动循环(`rst:0x3`)。

LED 状态:默认绿 · STA 连接成功蓝 · STA 失败黄 · **AP 有设备接入琥珀** · 记录中红(最高优先)。亮度固定 10%。

## 快速开始

```bash
pio run -t upload        # 编译并烧录
pio device monitor -b 115200
```

Wi-Fi 凭据**不硬编码**。复制 `wifi-config.example.txt` → `wifi-config.txt` 填入你的 SSID/密码再构建;或留空,之后用 WebUI/串口 `wifi` 命令设置(持久化到 LittleFS)。

> macOS 烧录注意:esptool 自动复位可用(115200 同步,921600 写入)。若自动下载不响应:按住 **BOOT** → 点按 **RESET** → 松开,然后用 `--before no_reset` 以 115200 烧录。

## 使用

### 访问入口

- **AP 模式(默认)**:连接 `ESP32S3-Thermo`(开放网络)→ 自动跳转 `http://4.4.4.1/`
- **STA 模式**:同一局域网访问 `http://<dhcp分配的ip>/`(连接成功后串口会打印 IP)

### WebUI

| 区域 | 功能 |
|---|---|
| 实时 tab | 温度大数字 + 记录按钮、壶选择、有盖/无盖、总重→水量、初始/目标温度、实时图表 |
| CSV 分析 tab | 选择已保存记录 → 完整报告(评分、指标表、三段图、反推功率) |
| 手冲壶管理 | 新建/编辑/删除壶(名称、空壶重、盖重、容量) |
| CSV 文件 | 下载 / 删除 / 清空全部记录 |
| WiFi 设置 | 设置 SSID/密码并重连 |
| 网络模式 | 切换 Host(AP)⇄ STA |

### 串口命令 (115200)

| 命令 | 功能 |
|---|---|
| `help` | 列出全部命令 |
| `ls` / `del <文件>` / `clr` | 列出 / 删除 / 清空 CSV 文件 |
| `show` / `cat <文件>` | 显示当前缓冲 / 文件内容 |
| `time Y M D h m s` | 手动设置时间 |
| `sync` | 重新 NTP 同步 |
| `wifi <ssid> <pass>` | 连接并保存新 WiFi |
| `host` | 切换 Host(AP)⇄ STA |
| `rec` / `stop` | 开始 / 停止记录 |
| `temp` | 立即读一次温度 |
| `stats` | 内存 / 记录 / 网络统计 |

### CSV 格式

```
# kettle:Fellow-黑色, cap:525ml, lid:on, dry:578.2g, lid_w:65.5g, total:1169.8g, water:526.1g, init:24.0c
time,temperature_c,water_g,target_c
2026-08-19 17:20:13,27.50,526.1,93.0
...
```

文件头注释携带壶参数快照(PV/水量/SV 齐全 → 可直接做 PID 效果评估);`init` 为分析用的初始基准温度,用于抵消热电偶恒定偏差。

## 评分方法

| 指标 | 权重 | 参考 |
|---|---|---|
| 到达时间 | 30 | 与理论最速(反推功率)对比 |
| 超调 | 25 | ≤0.5℃ 满分 |
| 稳态偏差 | 25 | ≤0.3℃ 满分 |
| 稳态振荡 | 20 | ±0.5 好 · ±1 一般 · ±2 差 · ≥±3 不可接受 |

分析从第一个 0.25℃ 跳变(差分检测)开始;稳态段 = 首次进入 ±0.5℃ 且之后不再跑出 ±1℃ 的完整区间;时间轴以升温起点为 0:00。

## 目录结构

```
├── README.md / README.zh-CN.md   # 文档(英文为主, 中文镜像, 互链)
├── plan.md                       # 设计与变更记录(中文)
├── platformio.ini                # espressif32@6.12.0
├── partitions.csv                # 16MB: App 6MB + LittleFS ~9.9MB
├── boards/YD-ESP32-S3.json       # 板卡定义 — 必需, 勿删
├── wifi-config.example.txt       # 模板(复制为 wifi-config.txt, 已 gitignore)
├── include/config.h              # 引脚、网络、采样参数
├── scripts/gen_wifi_config.py    # 构建钩子: wifi-config.txt → include/wifi_config.h
├── src/
│   ├── main.cpp                  # WiFi/AP/portal/HTTP API/CLI/按钮/LED/手冲壶
│   ├── max31855.cpp/h            # 热电偶驱动
│   ├── recorder.cpp/h            # PSRAM 记录 + CSV 持久化
│   └── html.h                    # 内嵌 WebUI(含浏览器端分析)
└── tools/analyze.py              # 离线 Python 分析 → HTML 报表
```

## 排障

- **启动循环 `rst:0x3`** — 平台/板卡组合不对。使用 `espressif32@6.12.0` + `boards/YD-ESP32-S3.json`(7.0.1 + 自定义分区表在这块板上会循环)
- **中文名 CSV 下载 404** — 旧固件文件名限 40 字节,重刷即可
- **分析报 steadyMean toFixed 错误** — 重刷(已修复)
- **上传无响应** — 按住 BOOT + 点按 RESET,`--before no_reset` 上传
