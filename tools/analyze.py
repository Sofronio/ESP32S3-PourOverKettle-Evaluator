#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ESP32-S3 Pour-Over Kettle Evaluator — session analysis → HTML report (stdlib only)
ESP32-S3 手冲壶评测工具 — 记录分析: 效果评估 + 打分 → HTML 报表 (纯标准库)

Usage / 用法:
    python3 analyze.py <data.csv> [--lang zh|en] [--open]
    --lang: report language / 报表语言 (default: zh)
    --open: open the report in a browser / 生成后浏览器打开
"""
import sys, re, math, subprocess
from datetime import datetime

# ---------------- 语言 / language ----------------
LANG = 'zh'
def _(zh, en):
    return zh if LANG == 'zh' else en

# ---------------- 评分参数 (可调) / scoring parameters ----------------
SCORE_RISE_WEIGHT    = 30   # 到达时间: 与理论最速比, 效率>=0.8 满分, 每低 0.05 扣 4 / rise time vs theoretical fastest
SCORE_OVERSHOOT      = 25   # 超调: <=0.5℃ 满分, 每 +0.5℃ 扣 10 / overshoot
SCORE_STEADY_BIAS    = 25   # 稳态偏差: <=0.3℃ 满分, 每 +0.3℃ 扣 10 / steady bias
SCORE_OSCILLATION    = 20   # 稳态振荡(±幅度): ±0.5℃ 好(满分), ±1℃ 一般, ±2℃ 差, ±3℃ 不可接受 / steady oscillation
OSC_GOOD             = 0.5  # ±0.5℃ 以内满分 / full marks up to ±0.5℃
OSC_FLOOR            = 3.0  # ±3℃ 及以上 0 分 / zero at ±3℃
SETTLE_BAND          = 0.5  # 到达判定容差 ℃ / arrival tolerance
RISE_MIN_RATIO       = 0.8
CP_WATER             = 4.18 # 水的比热 J/(g·K) / water heat capacity

# ---------------- 解析 / parsing ----------------
def parse(fn):
    """Parse CSV: header comment (kettle snapshot) + data rows (2/3/4 columns tolerated)."""
    meta, rows = {}, []
    with open(fn, encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith('#'):
                for k, pat in [('init', r'init:([\d.]+)c'), ('kettle', r'kettle:([^,]+)'),
                               ('cap', r'cap:([\d.]+)ml'), ('lid', r'lid:(\w+)'),
                               ('water', r'water:([\d.]+)g'), ('total', r'total:([\d.]+)g')]:
                    m = re.search(pat, line)
                    if m:
                        meta[k] = m.group(1)
                continue
            if line.lower().startswith('time'):
                continue
            p = line.split(',')
            if len(p) < 2:
                continue
            t, pv = p[0], float(p[1])
            w = float(p[2]) if len(p) > 2 and p[2] else 0.0
            tg = float(p[3]) if len(p) > 3 and p[3] else 93.0
            if t.startswith('uptime:'):
                sec = float(t.split(':')[1])
            else:
                sec = datetime.strptime(t, '%Y-%m-%d %H:%M:%S').timestamp()
            rows.append((sec, pv, w, tg))
    if not rows:
        sys.exit(_('无数据行 / no data rows in ' + fn, 'no data rows in ' + fn))
    rows.sort()
    t0 = rows[0][0]
    data = [(s - t0, pv, w, tg) for s, pv, w, tg in rows]
    init = float(meta.get('init', 0)) or data[0][1]
    targets = sorted({r[3] for r in data if r[3] > 0})
    target = targets[len(targets) // 2] or 93.0
    water = next((r[2] for r in data if r[2] > 0), 0)
    return data, meta, init, target, water

# ---------------- 指标 / metrics ----------------
def linreg(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    sxx = sum((x - mx) ** 2 for x in xs)
    return sxy / sxx if sxx else 0.0

def analyze(data, init, target, water):
    n = len(data)
    t = [r[0] for r in data]
    pv = [r[1] for r in data]

    # 升温起点 (差分法): 第一个 >=0.25℃(分辨率级)跳变、且其后 3 点仍累计升 >=0.5℃
    # rise start (differential): first 0.25℃ step that keeps rising (+0.5℃ over next 3 samples)
    RISE_D1, RISE_D3 = 0.25, 0.5
    rise0_idx = 0
    for i in range(n - 3):
        if pv[i + 1] - pv[i] >= RISE_D1 and pv[i + 3] - pv[i] >= RISE_D3:
            rise0_idx = i + 1
            break
    rise0_t = t[rise0_idx]

    # 时间平移(不裁剪): 指标以升温起点为 0, 未加热段负半轴 (绘图保留)
    # shift (keep all data): metrics zero at rise start, unheated segment goes negative
    data = [(x - rise0_t, y, w, tg) for x, y, w, tg in data]
    n = len(data)
    t = [r[0] for r in data]
    pv = [r[1] for r in data]
    span = t[-1] - t[0]

    # 到达时间 / arrival time (first entry into ±0.5℃ band)
    rise_idx = None
    for i in range(n):
        if pv[i] >= target - SETTLE_BAND:
            rise_idx = i
            break
    rise_t = t[rise_idx] if rise_idx is not None else None

    # 稳态段 ("从稳态到稳态"): 首次进入后不再跑出 ±1℃ 的完整区间
    # steady segment: from first entry until the end, never leaving ±1℃
    WIDE_BAND = 1.0
    s_idx = rise_idx if rise_idx is not None else n - 1
    if rise_idx is not None:
        for i in range(rise_idx, n):
            if all(abs(pv[j] - target) <= WIDE_BAND for j in range(i, n)):
                s_idx = i
                break
    steady_t = t[s_idx]
    steady = pv[s_idx:]
    steady_dur = t[-1] - steady_t
    steady_mean = sum(steady) / len(steady)
    steady_bias = steady_mean - target
    steady_osc = max(steady) - min(steady)

    # 超调 / overshoot
    peak = max(pv)
    overshoot = peak - target

    # 温升段线性拟合 (10%–90% 增量) → 反推功率 / infer power from rise slope × heat capacity
    lo, hi = data[int(n * 0.1)][1], data[int(n * 0.9)][1]
    seg = [(x, y) for x, y, _w, _tg in data if lo <= y <= hi]
    slope = linreg([s[0] for s in seg], [s[1] for s in seg]) if len(seg) > 2 else 0
    power = CP_WATER * water * slope if (water > 0 and slope > 0) else 0  # W
    t_theory = CP_WATER * water * (target - init) / power if power > 0 else 0  # s
    ratio = t_theory / rise_t if (rise_t and t_theory > 0) else 0

    return dict(span=span, rise_t=rise_t, overshoot=overshoot,
                steady_bias=steady_bias, steady_osc=steady_osc,
                peak=peak, steady_mean=steady_mean, power=power,
                t_theory=t_theory, ratio=ratio, steady_t=steady_t, steady_dur=steady_dur,
                data=data)

# ---------------- 评分 / scoring ----------------
def score(ms):
    s = {}
    s['rise'] = max(0, SCORE_RISE_WEIGHT - max(0, RISE_MIN_RATIO - ms['ratio']) / 0.05 * 4) if ms['rise_t'] else 0
    s['over'] = max(0, SCORE_OVERSHOOT - max(0, ms['overshoot'] - 0.5) / 0.5 * 10)
    s['bias'] = max(0, SCORE_STEADY_BIAS - max(0, abs(ms['steady_bias']) - 0.3) / 0.3 * 10)
    osc_peak = ms['steady_osc'] / 2
    s['osc'] = max(0, SCORE_OSCILLATION - max(0, osc_peak - OSC_GOOD) / (OSC_FLOOR - OSC_GOOD) * SCORE_OSCILLATION)
    s['total'] = sum(s.values())
    s['grade'] = ('excellent' if s['total'] >= 90 else 'good' if s['total'] >= 70
                  else 'fair' if s['total'] >= 50 else 'poor')
    s['color'] = '#4fd6a8' if s['total'] >= 90 else '#4fc3f7' if s['total'] >= 70 \
                 else '#ffd740' if s['total'] >= 50 else '#ff5252'
    return s

# ---------------- SVG 图表 (与 WebUI 样式一致) / SVG chart (matches WebUI) ----------------
fmt_s = lambda s: ('' if s >= 0 else '-') + f'{abs(int(s))//60}:{abs(int(s))%60:02d}'

def svg_chart(data, init, target, rise_t, overshoot, steady_t=None):
    W, H, padL, padR, padT, padB = 940, 420, 60, 20, 30, 40
    xs = [r[0] for r in data]; ys = [r[1] for r in data]
    dmin = min(min(ys), init); dmax = max(max(ys), target)
    ypad = max(0.5, (dmax - dmin) * 0.05)   # 跨度上下各 5% 余量 / 5% padding each side
    ymin = math.floor(dmin - ypad); ymax = math.ceil(dmax + ypad)
    xmin, xmax = xs[0], xs[-1] or 1
    X = lambda x: padL + (W - padL - padR) * (x - xmin) / (xmax - xmin)
    Y = lambda y: padT + (H - padT - padB) * (ymax - y) / (ymax - ymin)

    out = [f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg" style="width:100%;height:auto;background:#0d1117;border-radius:8px;display:block">']
    # 网格 / grid
    step = max(1, round((ymax - ymin) / 8))
    for v in range(ymin, ymax + 1, step):
        y = Y(v)
        out.append(f'<line x1="{padL}" y1="{y:.1f}" x2="{W-padR}" y2="{y:.1f}" stroke="#21262d" stroke-width="1"/>')
        out.append(f'<text x="{padL-8}" y="{y+4:.1f}" fill="#8b949e" font-size="12" text-anchor="end">{v}°</text>')
    for s in range(int(math.floor(xmin / 60)) * 60, int(xmax) + 60, 60):
        x = X(s)
        out.append(f'<text x="{x:.1f}" y="{H-14:.1f}" fill="#8b949e" font-size="11" text-anchor="middle">{fmt_s(s)}</text>')
    # 0:00 升温起点: 红色竖虚线 / rise start red dashed line
    x0 = X(0)
    out.append(f'<line x1="{x0:.1f}" y1="{padT}" x2="{x0:.1f}" y2="{H-padB}" stroke="#ff5252" stroke-width="1" stroke-dasharray="2,3"/>')
    # 未加热段(蓝) / unheated (blue), label left-aligned at y-axis
    if x0 > padL + 2:
        out.append(f'<rect x="{padL}" y="{padT}" width="{x0-padL:.1f}" height="{H-padT-padB}" fill="rgba(79,195,247,.07)"/>')
        out.append(f'<text x="{padL+6}" y="{H-padB-8}" fill="#4fc3f7" font-size="11">{_("未加热", "Unheated")} {fmt_s(xmin)} → 0:00</text>')
    # 升温段(红) / rise (red), label centered
    if rise_t:
        xr = X(rise_t)
        out.append(f'<rect x="{x0:.1f}" y="{padT}" width="{xr-x0:.1f}" height="{H-padT-padB}" fill="rgba(255,82,82,.08)"/>')
        out.append(f'<text x="{(x0+xr)/2:.1f}" y="{H-padB-8}" fill="#ff5252" font-size="11" text-anchor="middle">{_("升温段", "Rise")} 0:00 → {fmt_s(rise_t)}</text>')
        out.append(f'<line x1="{xr:.1f}" y1="{padT}" x2="{xr:.1f}" y2="{H-padB}" stroke="#4fd6a8" stroke-width="1" stroke-dasharray="2,3"/>')
        out.append(f'<text x="{W-padR-6}" y="{H-padB-8}" fill="#4fd6a8" font-size="11" text-anchor="end">{_("到达目标", "Target reached")} {fmt_s(rise_t)}</text>')
    # 稳态段(绿) / steady (green)
    if steady_t:
        xs0 = X(steady_t)
        out.append(f'<rect x="{xs0:.1f}" y="{padT}" width="{W-padR-xs0:.1f}" height="{H-padT-padB}" fill="rgba(79,214,168,.07)"/>')
    # 目标/初始/峰值虚线 / dashed reference lines
    out.append(f'<line x1="{padL}" y1="{Y(target):.1f}" x2="{W-padR}" y2="{Y(target):.1f}" stroke="#f0883e" stroke-width="1.2" stroke-dasharray="6,4"/>')
    out.append(f'<line x1="{padL}" y1="{Y(init):.1f}" x2="{W-padR}" y2="{Y(init):.1f}" stroke="#8b949e" stroke-width="1" stroke-dasharray="3,4"/>')
    if overshoot > 0:
        out.append(f'<line x1="{padL}" y1="{Y(max(ys)):.1f}" x2="{W-padR}" y2="{Y(max(ys)):.1f}" stroke="#ff5252" stroke-width="1" stroke-dasharray="2,3"/>')
    # 顶部居中标注 / top-center labels
    labels = [(f'{_("目标", "Target")} {target:.1f}°', '#f0883e'), (f'{_("初始", "Init")} {init:.1f}°', '#8b949e')]
    if overshoot > 0:
        labels.append((f'{_("峰值", "Peak")} {max(ys):.1f}° (+{overshoot:.1f}°)', '#ff5252'))
    total_w = sum(len(l[0]) for l in labels) * 6.5 + len(labels) * 20
    lx = (padL + W - padR) / 2 - total_w / 2
    for txt, color in labels:
        out.append(f'<text x="{lx:.1f}" y="{padT+14}" fill="{color}" font-size="11">{txt}</text>')
        lx += len(txt) * 6.5 + 20
    # 温度曲线 / temperature curve
    pts = ' '.join(f'{X(x):.1f},{Y(y):.1f}' for x, y in zip(xs, ys))
    out.append(f'<polyline points="{pts}" fill="none" stroke="#4fc3f7" stroke-width="2" stroke-linejoin="round"/>')
    out.append('</svg>')
    return '\n'.join(out)

# ---------------- HTML 报表 / report ----------------
def render(fn, meta, ms, sc):
    m = lambda v, u='': f'{v:.2f}{u}' if isinstance(v, float) else str(v)
    if LANG == 'zh':
        fmt_t = lambda s: f'{int(s//60)}分{int(s%60)}秒' if s else '未到达'
        grade_verdict = {'excellent': '无需改动', 'good': '可用, 可微调', 'fair': '建议优化', 'poor': '建议重新整定'}
        grade_word = {'excellent': '优秀', 'good': '良好', 'fair': '及格', 'poor': '需优化'}
    else:
        fmt_t = lambda s: f'{int(s//60)}m{int(s%60):02d}s' if s else 'not reached'
        grade_verdict = {'excellent': 'no changes needed', 'good': 'usable, minor tuning', 'fair': 'consider tuning', 'poor': 're-tune recommended'}
        grade_word = {'excellent': 'Excellent', 'good': 'Good', 'fair': 'Fair', 'poor': 'Poor'}
    rows = [
        (_('到达时间', 'Rise time'),
         f'{fmt_t(ms["rise_t"])} ({_("理论最速", "theoretical")} {fmt_t(ms["t_theory"])}, {_("效率", "efficiency")} {ms["ratio"]*100:.0f}%)',
         sc['rise'], SCORE_RISE_WEIGHT, '#4fd6a8'),
        (_('超调量', 'Overshoot'),
         f'{_("峰值", "peak")} {m(ms["peak"])}°, {_("超调", "overshoot")} {m(ms["overshoot"], "°")}',
         sc['over'], SCORE_OVERSHOOT, '#ff5252'),
        (_('稳态偏差', 'Steady bias'),
         f'{_("稳态", "steady")} {fmt_t(ms["steady_dur"])} {_("内均值", "mean")} {m(ms["steady_mean"])}°, {_("偏差", "bias")} {m(ms["steady_bias"], "°")}',
         sc['bias'], SCORE_STEADY_BIAS, '#ffd740'),
        (_('稳态振荡', 'Steady oscillation'),
         f'±{m(ms["steady_osc"]/2, "°")} ({_("波动范围", "range")} {m(ms["steady_osc"], "°")})',
         sc['osc'], SCORE_OSCILLATION, '#4fc3f7'),
    ]
    tr = '\n'.join(
        f'<tr><td><span style="color:{c}">■</span> {n}</td><td>{d}</td>'
        f'<td style="text-align:right"><b>{s:.0f}</b> / {w}</td></tr>' for n, d, s, w, c in rows)
    concl_ok = _('各维度表现均好, 原厂 PID 参数适合当前水量, 无需调整。',
                 'All metrics are good; the stock PID fits this water volume, no changes needed.')
    concl_fail = _('整体可用; 主要扣分: ', 'Overall usable; main deductions: ') + \
                 '、'.join(n for n, d, s, w, c in rows if s < w * 0.85) + \
                 _(', 可微调优化。', ', minor tuning may help.')
    concl = concl_ok if sc['total'] >= 90 else concl_fail
    title = _('温度控制分析报表', 'Temperature Control Analysis Report')
    html_lang = 'zh-CN' if LANG == 'zh' else 'en'
    return f'''<!DOCTYPE html>
<html lang="{html_lang}"><head><meta charset="utf-8"><title>{title}</title>
<style>
 body{{margin:0;background:#0d1117;color:#e6edf3;font-family:-apple-system,"PingFang SC",sans-serif}}
 .wrap{{max-width:980px;margin:0 auto;padding:20px}}
 h1{{font-size:20px}} .sub{{color:#8b949e;font-size:13px;margin:4px 0 16px}}
 .score{{display:flex;align-items:center;gap:16px;background:#161b22;border:1px solid #30363d;border-radius:12px;padding:18px}}
 .num{{font-size:56px;font-weight:700}}
 .grade{{font-size:18px;font-weight:600;margin-bottom:6px}}
 .meta{{color:#8b949e;font-size:13px}}
 table{{width:100%;border-collapse:collapse;margin:14px 0;background:#161b22;border-radius:12px;overflow:hidden}}
 td,th{{padding:10px 14px;border-bottom:1px solid #21262d;font-size:14px}}
 th{{text-align:left;color:#8b949e;font-weight:600;font-size:12px}}
 .card{{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:14px;margin-top:14px}}
 .card h3{{margin:0 0 10px;font-size:14px;color:#8b949e}}
 .power{{display:flex;gap:24px;flex-wrap:wrap}}
 .power div{{font-size:14px}} .power b{{font-size:22px;color:#4fc3f7}}
 .concl{{background:#161b22;border:1px solid #30363d;border-left:4px solid {sc['color']};border-radius:8px;padding:12px 16px;margin-top:14px;font-size:14px}}
</style></head><body><div class="wrap">
<h1>🌡️ {title}</h1>
<div class="sub">{fn} · {_("生成于", "generated at")} {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}</div>

<div class="score">
  <div><div class="num" style="color:{sc['color']}">{sc['total']:.0f}</div><div style="color:#8b949e;font-size:12px">{_("总分 / 100", "Score / 100")}</div></div>
  <div style="flex:1">
    <div class="grade" style="color:{sc['color']}">{grade_word[sc['grade']]} — {grade_verdict[sc['grade']]}</div>
    <div class="meta">{_("壶", "Kettle")}: {meta.get('kettle','-')} · {_("水量", "water")} {meta.get('water','0')}g · {_("目标", "target")} {target:.1f}° · {_("初始基准", "init ref")} {init:.1f}° · {_("时长", "duration")} {fmt_t(ms['span'])} · {_("样本", "samples")} {len(data)}</div>
  </div>
</div>

<table><tr><th>{_("维度", "Metric")}</th><th>{_("实测", "Measured")}</th><th>{_("得分", "Score")}</th></tr>{tr}</table>

<div class="card"><h3>{_("温度曲线 (蓝色 = 未加热, 红色 = 升温段, 绿色 = 稳态段)", "Temperature curve (blue = unheated, red = rise, green = steady)")}</h3>{svg_chart(ms['data'], init, target, ms['rise_t'], ms['overshoot'], ms['steady_t'])}</div>

<div class="card"><h3>{_("反推加热功率", "Inferred heating power")}</h3>
  <div class="power">
    <div>{_("平均加热功率", "Average power")}<b>{ms['power']:.0f} W</b><br><span style="font-size:12px;color:#8b949e">{_("由温升斜率 × 水量热容反推", "from rise slope × water heat capacity")}</span></div>
    <div>{_("理论最速升温", "Theoretical fastest")}<b>{fmt_t(ms['t_theory'])}</b><br><span style="font-size:12px;color:#8b949e">{_("满功率加热所需时间", "time at full power")}</span></div>
    <div>{_("实际效率", "Efficiency")}<b>{ms['ratio']*100:.0f}%</b><br><span style="font-size:12px;color:#8b949e">{_("理论/实际 比值", "theoretical / actual")}</span></div>
  </div>
</div>

<div class="concl"><b>{_("结论:", "Conclusion:")}</b> {grade_word[sc['grade']]}。{concl}</div>
</div></body></html>'''

if __name__ == '__main__':
    if '--lang' in sys.argv:
        LANG = sys.argv[sys.argv.index('--lang') + 1] if len(sys.argv) > sys.argv.index('--lang') + 1 else 'zh'
    fn = sys.argv[1]
    data, meta, init, target, water = parse(fn)
    ms = analyze(data, init, target, water)
    sc = score(ms)
    out = fn.rsplit('.', 1)[0] + '.html'
    with open(out, 'w', encoding='utf-8') as f:
        f.write(render(fn, meta, ms, sc))
    gw = {'excellent': _('优秀', 'Excellent'), 'good': _('良好', 'Good'),
          'fair': _('及格', 'Fair'), 'poor': _('需优化', 'Poor')}
    print(_('报表已生成: ', 'Report written: ') + out)
    print(f'{_("总分", "Score")} {sc["total"]:.0f} ({gw[sc["grade"]]}) | {_("到达", "rise")} {ms["rise_t"]:.0f}s | {_("超调", "overshoot")} {ms["overshoot"]:+.2f}° | '
          f'{_("稳态段", "steady")} {ms["steady_dur"]:.0f}s {_("偏差", "bias")} {ms["steady_bias"]:+.2f}° {_("振荡", "osc")} ±{ms["steady_osc"]/2:.2f}° | '
          f'{_("功率", "power")} {ms["power"]:.0f}W | {_("效率", "efficiency")} {ms["ratio"]*100:.0f}%')
    if '--open' in sys.argv:
        subprocess.run(['open', out])
