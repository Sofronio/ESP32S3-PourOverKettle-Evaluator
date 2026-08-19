# ============================================================
# Build hook: wifi-config.txt (gitignored) → include/wifi_config.h
# 构建钩子: 从本地 wifi-config.txt 生成 WiFi 默认凭据头文件(不入库)
# ============================================================
# 构建前钩子: 从项目根 wifi-config.txt 生成 include/wifi_config.h
# - wifi-config.txt 不入库(.gitignore), 模板见 wifi-config.example.txt
# - 文件不存在时生成空凭据 → 固件仅 Host(AP) 模式, 通过 WebUI/串口设置 WiFi
Import("env")

import pathlib

root = pathlib.Path(env.subst("$PROJECT_DIR"))
cfg = root / "wifi-config.txt"
out = root / "include" / "wifi_config.h"

ssid, passw = "", ""
if cfg.exists():
    for line in cfg.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("SSID="):
            ssid = line[5:].strip()
        elif line.startswith("PASS="):
            passw = line[5:].strip()


def esc(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


out.write_text(
    '#pragma once\n'
    '// 由 scripts/gen_wifi_config.py 自动生成(来源 wifi-config.txt), 勿手动编辑\n'
    f'#define WIFI_DEFAULT_SSID "{esc(ssid)}"\n'
    f'#define WIFI_DEFAULT_PASS "{esc(passw)}"\n'
)
print(f"[wifi-config] SSID={'***' if ssid else '(空, 仅AP模式)'}")
