#!/usr/bin/env python3
"""ESP32-S3 温度计记录分析: 效果评估 + 打分 → HTML 报表 (纯标准库, 零依赖)

用法: python3 analyze.py <data.csv> [--open]
"""
import sys, re, math, os, subprocess
from datetime import datetime

# ---------------- 评分参数 (可调) ----------------
SCORE_RISE_WEIGHT    = 30   # 到达时间: 与理论最速比, 效率>=0.8 满分, 每低 0.05 扣 4
SCORE_OVERSHOOT      = 25   # 超调: <=0.5℃ 满分, 每 +0.5℃ 扣 10
SCORE_STEADY_BIAS    = 25   # 稳态偏差: <=0.3℃ 满分, 每 +0.3℃ 扣 10
SCORE_OSCILLATION    = 20   # 稳态振荡(±幅度): ±0.5℃ 好(满分), ±1℃ 一般, ±2℃ 差, ±3℃ 不可接受
OSC_GOOD             = 0.5  # ±0.5℃ 以内满分
OSC_FLOOR            = 3.0  # ±3℃ 及以上 0 分 (线性衰减 8 分/℃)
SETTLE_BAND          = 0.5  # 到达判定容差 ℃
RISE_MIN_RATIO       = 0.8
CP_WATER             = 4.18 # 水的比热 J/(g·K)

# ---------------- 解析 ----------------
def parse(fn):
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
            if len(p) < 4:
                continue
            t, pv, w, tg = p[0], float(p[1]), float(p[2] or 0), float(p[3] or 0)
            if t.startswith('uptime:'):
                sec = float(t.split(':')[1])
            else:
                sec = datetime.strptime(t, '%Y-%m-%d %H:%M:%S').timestamp()
            rows.append((sec, pv, w, tg))
    if not rows:
        sys.exit('无数据行')
    rows.sort()
    t0 = rows[0][0]
    data = [(s - t0, pv, w, tg) for s, pv, w, tg in rows]
    init = float(meta.get('init', 0)) or data[0][1]
    target = sorted({r[3] for r in data if r[3] > 0})[len({r[3] for r in data if r[3] > 0}) // 2] or 93.0
    water = next((r[2] for r in data if r[2] > 0), 0)
    return data, meta, init, target, water

# ---------------- 指标 ----------------
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

    # 升温起点 (差分法): 第一个出现 >=0.25℃(传感器分辨率级)跳变、
    # 且其后 3 个样本仍累计上升 >=0.5℃ 的点 → 真正开始升温的那条数据
    # (窗口法会把"窗口内包含升温"误判成"窗口起点是升温", 提前好几秒)
    RISE_D1, RISE_D3 = 0.25, 0.5
    rise0_idx = 0
    for i in range(n - 3):
        if pv[i + 1] - pv[i] >= RISE_D1 and pv[i + 3] - pv[i] >= RISE_D3:
            rise0_idx = i + 1
            break
    rise0_t = t[rise0_idx]

    # 时间平移(不裁剪): 全部数据保留用于绘图, 指标以升温起点为 0
    data = [(x - rise0_t, y, w, tg) for x, y, w, tg in data]
    n = len(data)
    t = [r[0] for r in data]
    pv = [r[1] for r in data]
    span = t[-1] - t[0]   # 总记录时长 (含未加热段)

    # 到达时间 (首次进入 ±0.5℃ 带)
    rise_idx = None
    for i in range(n):
        if pv[i] >= target - SETTLE_BAND:
            rise_idx = i
            break
    rise_t = t[rise_idx] if rise_idx is not None else None

    # 稳态段识别 ("从稳态到稳态"): 自首次进入带起, 找第一个点,
    # 使其之后所有数据都保持在 target±1℃ 宽容差内 → 该点起为完整稳态段
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

    # 超调
    peak = max(pv)
    overshoot = peak - target

    # 温升段线性拟合 (10%–90% 增量) → 反推功率
    span_t = target - init
    lo, hi = data[int(n * 0.1)][1], data[int(n * 0.9)][1]
    seg = [(x, y) for x, y, _w, _tg in data if lo <= y <= hi]
    slope = linreg([s[0] for s in seg], [s[1] for s in seg]) if len(seg) > 2 else 0
    power = CP_WATER * water * slope if (water > 0 and slope > 0) else 0  # W
    t_theory = CP_WATER * water * span_t / power if power > 0 else 0      # s
    ratio = t_theory / rise_t if (rise_t and t_theory > 0) else 0

    return dict(span=span, rise_t=rise_t, overshoot=overshoot,
                steady_bias=steady_bias, steady_osc=steady_osc,
                peak=peak, steady_mean=steady_mean, power=power,
                t_theory=t_theory, ratio=ratio, steady_t=steady_t, steady_dur=steady_dur,
                data=data)   # 平移后数据 (升温起点=0, 未加热段为负) — 供图表使用

# ---------------- 评分 ----------------
def score(ms):
    s = {}
    s['rise'] = max(0, SCORE_RISE_WEIGHT - max(0, RISE_MIN_RATIO - ms['ratio']) / 0.05 * 4) if ms['rise_t'] else 0
    s['over'] = max(0, SCORE_OVERSHOOT - max(0, ms['overshoot'] - 0.5) / 0.5 * 10)
    s['bias'] = max(0, SCORE_STEADY_BIAS - max(0, abs(ms['steady_bias']) - 0.3) / 0.3 * 10)
    osc_peak = ms['steady_osc'] / 2   # 峰值幅度 ±x℃
    s['osc']  = max(0, SCORE_OSCILLATION - max(0, osc_peak - OSC_GOOD) / (OSC_FLOOR - OSC_GOOD) * SCORE_OSCILLATION)
    s['total'] = sum(s.values())
    s['grade'] = ('优秀' if s['total'] >= 90 else '良好' if s['total'] >= 70
                  else '及格' if s['total'] >= 50 else '需优化')
    s['color'] = '#4fd6a8' if s['total'] >= 90 else '#4fc3f7' if s['total'] >= 70 \
                 else '#ffd740' if s['total'] >= 50 else '#ff5252'
    return s

# ---------------- SVG 图表 ----------------
fmt_s = lambda s: ('' if s >= 0 else '-') + f'{abs(int(s))//60}:{abs(int(s))%60:02d}'

def svg_chart(data, init, target, rise_t, overshoot, steady_t=None):
    W, H, padL, padR, padT, padB = 940, 420, 60, 20, 30, 40
    xs = [r[0] for r in data]; ys = [r[1] for r in data]
    # y 轴范围: 数据跨度上下各留 5% 余量(合计约 10%), 比固定 ±2° 更紧凑
    dmin = min(min(ys), init); dmax = max(max(ys), target)
    ypad = max(0.5, (dmax - dmin) * 0.05)
    ymin = math.floor(dmin - ypad); ymax = math.ceil(dmax + ypad)
    xmin, xmax = xs[0], xs[-1] or 1
    X = lambda x: padL + (W - padL - padR) * (x - xmin) / (xmax - xmin)
    Y = lambda y: padT + (H - padT - padB) * (ymax - y) / (ymax - ymin)
    def esc(s): return str(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

    out = [f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg" style="width:100%;height:auto;background:#0d1117;border-radius:8px;display:block">']
    # 网格
    step = max(1, round((ymax - ymin) / 8))
    for v in range(ymin, ymax + 1, step):
        y = Y(v)
        out.append(f'<line x1="{padL}" y1="{y:.1f}" x2="{W-padR}" y2="{y:.1f}" stroke="#21262d" stroke-width="1"/>')
        out.append(f'<text x="{padL-8}" y="{y+4:.1f}" fill="#8b949e" font-size="12" text-anchor="end">{v}°</text>')
    for s in range(int(math.floor(xmin / 60)) * 60, int(xmax) + 60, 60):
        x = X(s)
        out.append(f'<text x="{x:.1f}" y="{H-14:.1f}" fill="#8b949e" font-size="11" text-anchor="middle">{fmt_s(s)}</text>')
    # 目标虚线 + init 线
    out.append(f'<line x1="{padL}" y1="{Y(target):.1f}" x2="{W-padR}" y2="{Y(target):.1f}" stroke="#f0883e" stroke-width="1.6" stroke-dasharray="7,5"/>')
    out.append(f'<text x="{padL+6}" y="{Y(target)-6:.1f}" fill="#f0883e" font-size="12">目标 {target:.1f}°</text>')
    out.append(f'<line x1="{padL}" y1="{Y(init):.1f}" x2="{W-padR}" y2="{Y(init):.1f}" stroke="#8b949e" stroke-width="1" stroke-dasharray="3,4"/>')
    out.append(f'<text x="{W-padR-6}" y="{Y(init)-6:.1f}" fill="#8b949e" font-size="11" text-anchor="end">初始 {init:.1f}°</text>')
    # 超调标注
    if overshoot > 0:
        peak = max(ys); yp = Y(peak)
        out.append(f'<line x1="{padL}" y1="{yp:.1f}" x2="{W-padR}" y2="{yp:.1f}" stroke="#ff5252" stroke-width="1" stroke-dasharray="2,3"/>')
        out.append(f'<text x="{W-padR-6}" y="{yp-6:.1f}" fill="#ff5252" font-size="11" text-anchor="end">峰值 {peak:.1f}° (超调 +{overshoot:.1f}°)</text>')
    # 未加热段阴影 (升温起点之前, 蓝色; 在时间轴负半轴, 不占有效时间)
    x0 = X(0)
    if x0 > padL + 2:
        out.append(f'<rect x="{padL}" y="{padT}" width="{x0-padL:.1f}" height="{H-padT-padB}" fill="rgba(79,195,247,.07)"/>')
        out.append(f'<text x="{padL+6}" y="{H-padB-8}" fill="#4fc3f7" font-size="11">未加热 {fmt_s(xmin)} → 0:00</text>')
    # 0:00 升温起点: 红色竖虚线(升温段左边界, 与到达绿线样式一致)
    out.append(f'<line x1="{x0:.1f}" y1="{padT}" x2="{x0:.1f}" y2="{H-padB}" stroke="#ff5252" stroke-width="1" stroke-dasharray="2,3"/>')
    # 升温段阴影 (0:00 到到达目标, 红色)
    if rise_t:
        xr = X(rise_t)
        out.append(f'<rect x="{x0:.1f}" y="{padT}" width="{xr-x0:.1f}" height="{H-padT-padB}" fill="rgba(255,82,82,.08)"/>')
        out.append(f'<text x="{x0+6:.1f}" y="{H-padB-8}" fill="#ff5252" font-size="11">升温段 0:00 → {fmt_s(rise_t)}</text>')
        out.append(f'<line x1="{xr:.1f}" y1="{padT}" x2="{xr:.1f}" y2="{H-padB}" stroke="#4fd6a8" stroke-width="1" stroke-dasharray="2,3"/>')
        out.append(f'<text x="{xr+5:.1f}" y="{padT+14}" fill="#4fd6a8" font-size="11">到达 {fmt_s(rise_t)}</text>')
    # 稳态段阴影 (从稳态起点到结束, 绿色)
    if steady_t:
        xs0 = X(steady_t)
        out.append(f'<rect x="{xs0:.1f}" y="{padT}" width="{W-padR-xs0:.1f}" height="{H-padT-padB}" fill="rgba(79,214,168,.07)"/>')
        out.append(f'<text x="{xs0+6:.1f}" y="{padT+14}" fill="#4fd6a8" font-size="11">稳态段开始 {fmt_s(steady_t)}</text>')
    # 温度曲线
    pts = ' '.join(f'{X(x):.1f},{Y(y):.1f}' for x, y in zip(xs, ys))
    out.append(f'<polyline points="{pts}" fill="none" stroke="#4fc3f7" stroke-width="2" stroke-linejoin="round"/>')
    out.append('</svg>')
    return '\n'.join(out)

# ---------------- HTML 报表 ----------------
def render(fn, meta, ms, sc):
    m = lambda v, u='': f'{v:.2f}{u}' if isinstance(v, float) else str(v)
    fmt_t = lambda s: f'{int(s//60)}分{int(s%60)}秒' if s else '未到达'
    grade_zh = {'优秀': '无需改动', '良好': '可用, 可微调', '及格': '建议优化', '需优化': '建议重新整定'}
    rows = [
        ('到达时间', f'{fmt_t(ms["rise_t"])} (理论最速 {fmt_t(ms["t_theory"])}, 效率 {ms["ratio"]*100:.0f}%)', sc['rise'], SCORE_RISE_WEIGHT, '#4fd6a8'),
        ('超调量', f'峰值 {m(ms["peak"])}°, 超调 {m(ms["overshoot"], "°")}', sc['over'], SCORE_OVERSHOOT, '#ff5252'),
        ('稳态偏差', f'稳态 {fmt_t(ms["steady_dur"])} 内均值 {m(ms["steady_mean"])}°, 偏差 {m(ms["steady_bias"], "°")}',
         sc['bias'], SCORE_STEADY_BIAS, '#ffd740'),
        ('稳态振荡', f'稳态段 ±{m(ms["steady_osc"]/2, "°")} (波动范围 {m(ms["steady_osc"], "°")})', sc['osc'], SCORE_OSCILLATION, '#4fc3f7'),
    ]
    tr = '\n'.join(
        f'<tr><td><span style="color:{c}">■</span> {n}</td><td>{d}</td>'
        f'<td style="text-align:right"><b>{s:.0f}</b> / {w}</td></tr>' for n, d, s, w, c in rows)
    return f'''<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8"><title>温度控制分析报表</title>
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
<h1>🌡️ 温度控制分析报表</h1>
<div class="sub">{fn} · 生成于 {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}</div>

<div class="score">
  <div><div class="num" style="color:{sc['color']}">{sc['total']:.0f}</div><div style="color:#8b949e;font-size:12px">总分 / 100</div></div>
  <div style="flex:1">
    <div class="grade" style="color:{sc['color']}">{sc['grade']} — {grade_zh[sc['grade']]}</div>
    <div class="meta">壶: {meta.get('kettle','-')} · 水量 {meta.get('water','0')}g · 目标 {target:.1f}° · 初始基准 {init:.1f}° · 时长 {fmt_t(ms['span'])} · 样本 {len(data)} 条</div>
  </div>
</div>

<table><tr><th>维度</th><th>实测</th><th>得分</th></tr>{tr}</table>

<div class="card"><h3>温度曲线 (蓝色 = 未加热, 红色 = 升温段, 绿色 = 稳态段, 时间从升温起点 0:00 计)</h3>{svg_chart(ms['data'], init, target, ms['rise_t'], ms['overshoot'], ms['steady_t'])}</div>

<div class="card"><h3>反推加热功率</h3>
  <div class="power">
    <div>平均加热功率<b>{ms['power']:.0f} W</b><br><span style="font-size:12px;color:#8b949e">由温升斜率 × 水量热容反推</span></div>
    <div>理论最速升温<b>{fmt_t(ms['t_theory'])}</b><br><span style="font-size:12px;color:#8b949e">满功率加热所需时间</span></div>
    <div>实际效率<b>{ms['ratio']*100:.0f}%</b><br><span style="font-size:12px;color:#8b949e">理论/实际 比值</span></div>
  </div>
</div>

<div class="concl"><b>结论:</b> {sc['grade']}。{(
 '各维度表现均好, 原厂 PID 参数适合当前水量, 无需调整。' if sc['total']>=90 else
 '整体可用' + ('; 主要扣分: ' + '、'.join(n for n,d,s,w,c in rows if s < w*0.85) + ', 可微调优化。' if any(s<w*0.85 for n,d,s,w,c in rows) else '。'))}</div>
</div></body></html>'''

if __name__ == '__main__':
    fn = sys.argv[1]
    data, meta, init, target, water = parse(fn)
    ms = analyze(data, init, target, water)
    sc = score(ms)
    out = fn.rsplit('.', 1)[0] + '.html'
    with open(out, 'w', encoding='utf-8') as f:
        f.write(render(fn, meta, ms, sc))
    print(f'报表已生成: {out}')
    print(f'总分 {sc["total"]:.0f} ({sc["grade"]}) | 到达 {ms["rise_t"]:.0f}s | 超调 {ms["overshoot"]:+.2f}° | '
          f'稳态段 {ms["steady_dur"]:.0f}s 内偏差 {ms["steady_bias"]:+.2f}° 振荡 ±{ms["steady_osc"]/2:.2f}° | '
          f'功率 {ms["power"]:.0f}W | 效率 {ms["ratio"]*100:.0f}%')
    if '--open' in sys.argv:
        subprocess.run(['open', out])
