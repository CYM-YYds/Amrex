"""生成 CCCM2026 汇报 PPT 框架（16:9，中文）。

用途：为「自适应网格 LBM 求解器的 GPU 性能优化」口头报告搭建可编辑的排版框架。
数值来源：
  - A-B / OSI 分项：docs/MLUPS记录.md 性能表2（job 613415 A-B、613417 OSI，DOUBLE）
  - 吞吐阶梯：docs/MLUPS记录.md 与 docs/均匀网格与AMR性能优化记录.md 性能总表
  - AMR/512³：docs/MLUPS记录.md 参数表与性能表（622137/B、624717/C、623012）
缺图处用虚线占位框，框内写「要放什么」。
任务结束后可删除；不修改仓库其它文件。
"""

from __future__ import annotations

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager
from pptx import Presentation
plt.rcParams["font.family"] = ["Noto Sans CJK SC", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Emu, Inches, Pt

CASE = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
POST = os.path.join(CASE, "data_post_processing")
OUT = os.path.join(CASE, "docs", "CCCM2026_MS04_AmReX_OSI_汇报框架.pptx")
os.makedirs(ASSETS, exist_ok=True)

# ---------------------------------------------------------------- 设计常量
NAVY = RGBColor(0x15, 0x3A, 0x5C)
TEAL = RGBColor(0x2A, 0x9D, 0x8F)
ORANGE = RGBColor(0xE7, 0x6F, 0x51)
GRAY = RGBColor(0x6B, 0x7A, 0x89)
LIGHT = RGBColor(0xEE, 0xF3, 0xF6)
MID = RGBColor(0xC5, 0xD2, 0xDA)
INK = RGBColor(0x1F, 0x29, 0x33)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)

FONT_CN = "Microsoft YaHei"
FONT_EN = "Arial"

W, H = Inches(13.333), Inches(7.5)
ML = Inches(0.55)                     # 左边距
CW = Inches(12.23)                    # 内容宽
CIDX = 1                              # 页码

prs = Presentation()
prs.slide_width, prs.slide_height = W, H
BLANK = prs.slide_layouts[6]


# ---------------------------------------------------------------- 基础工具
def font_of(run, size, bold=False, color=INK, italic=False, name=FONT_CN):
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.italic = italic
    run.font.color.rgb = color
    run.font.name = name
    rpr = run._r.get_or_add_rPr()
    for tag in ("a:ea", "a:latin", "a:cs"):
        el = rpr.find(qn(tag))
        if el is None:
            el = rpr.makeelement(qn(tag), {})
            rpr.append(el)
        el.set("typeface", name)


def textbox(slide, x, y, w, h, lines, *, align=PP_ALIGN.LEFT,
            anchor=MSO_ANCHOR.TOP, wrap=True, spacing=None):
    """lines: [(文本, 字号, 加粗, 颜色, 段前磅), ...] 或 [(文本, 字号), ...]"""
    tb = slide.shapes.add_textbox(x, y, w, h)
    tf = tb.text_frame
    tf.word_wrap = wrap
    tf.margin_left = tf.margin_right = Inches(0.04)
    tf.margin_top = tf.margin_bottom = Inches(0.02)
    tf.vertical_anchor = anchor
    for i, spec in enumerate(lines):
        txt, size = spec[0], spec[1]
        bold = spec[2] if len(spec) > 2 else False
        color = spec[3] if len(spec) > 3 else INK
        before = spec[4] if len(spec) > 4 else (0 if i == 0 else 4)
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        p.space_before = Pt(before)
        if spacing:
            p.line_spacing = spacing
        r = p.add_run()
        r.text = txt
        font_of(r, size, bold, color)
    return tb


def rect(slide, x, y, w, h, fill=None, line=None, lw=1.0, shape=MSO_SHAPE.RECTANGLE,
         dash=False, radius=None):
    s = slide.shapes.add_shape(shape, x, y, w, h)
    if radius is not None and shape == MSO_SHAPE.ROUNDED_RECTANGLE:
        s.adjustments[0] = radius
    if fill is None:
        s.fill.background()
    else:
        s.fill.solid()
        s.fill.fore_color.rgb = fill
    if line is None:
        s.line.fill.background()
    else:
        s.line.color.rgb = line
        s.line.width = Pt(lw)
        if dash:
            s.line.dash_style = 4  # dash
    s.shadow.inherit = False
    return s


def box_text(shape, lines, *, align=PP_ALIGN.CENTER, anchor=MSO_ANCHOR.MIDDLE):
    tf = shape.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = anchor
    tf.margin_left = tf.margin_right = Inches(0.06)
    tf.margin_top = tf.margin_bottom = Inches(0.03)
    for i, spec in enumerate(lines):
        txt, size = spec[0], spec[1]
        bold = spec[2] if len(spec) > 2 else False
        color = spec[3] if len(spec) > 3 else INK
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        if i:
            p.space_before = Pt(2)
        r = p.add_run()
        r.text = txt
        font_of(r, size, bold, color)


def frame(slide, title, kicker=None):
    """内容页统一页眉：左侧强调条 + 标题 + 细线 + 页脚页码。"""
    global CIDX
    rect(slide, ML, Inches(0.44), Inches(0.09), Inches(0.52), fill=TEAL)
    textbox(slide, ML + Inches(0.2), Inches(0.30), Inches(10.9), Inches(0.74),
            [(title, 25, True, NAVY)], anchor=MSO_ANCHOR.MIDDLE)
    if kicker:
        textbox(slide, ML + Inches(0.2) + Inches(6.0), Inches(0.42), Inches(6.1),
                Inches(0.5), [(kicker, 12, False, GRAY)], align=PP_ALIGN.RIGHT,
                anchor=MSO_ANCHOR.MIDDLE)
    rect(slide, ML, Inches(1.06), CW, Pt(1.1), fill=MID)
    textbox(slide, ML, Inches(7.06), Inches(8.0), Inches(0.3),
            [("CCCM 2026 · MS04 计算流体/流固耦合力学", 9, False, GRAY)])
    textbox(slide, W - ML - Inches(1.2), Inches(7.06), Inches(1.2), Inches(0.3),
            [(f"{CIDX}", 10, True, GRAY)], align=PP_ALIGN.RIGHT)
    CIDX += 1


def placeholder(slide, x, y, w, h, label, sub="", tone=TEAL):
    s = rect(slide, x, y, w, h, fill=LIGHT, line=tone, lw=1.0, dash=True)
    lines = [(label, 13, True, GRAY)]
    if sub:
        lines.append((sub, 10, False, GRAY))
    box_text(s, lines)
    return s


def note(slide, text):
    slide.notes_slide.notes_text_frame.text = text


def chip(slide, x, y, w, h, num, cap, color=TEAL):
    rect(slide, x, y, w, h, fill=WHITE, line=MID, lw=1.0)
    rect(slide, x, y, Pt(3), h, fill=color)
    textbox(slide, x + Inches(0.1), y + Inches(0.08), w - Inches(0.16), Inches(0.45),
            [(num, 22, True, color)])
    textbox(slide, x + Inches(0.1), y + Inches(0.5), w - Inches(0.16), h - Inches(0.55),
            [(cap, 10.5, False, INK)], spacing=1.05)


# ---------------------------------------------------------------- 图 1：分项对比
def chart_breakdown(path):
    stages = ["插值", "平均", "碰撞", "迁移", "通信", "边界"]
    ab = [321.49, 211.34, 1940.22, 2067.44, 304.54, 382.86]
    osi = [334.47, 213.45, 2290.16, 8.56, 312.92, 453.23]
    fig, ax = plt.subplots(figsize=(7.4, 4.3), dpi=220)
    x = range(len(stages))
    bw = 0.62
    ax.bar([i - bw / 2 for i in x], ab, bw, label="A–B 双数组", color="#8FA9BD",
           edgecolor="white", linewidth=0.6)
    ax.bar([i + bw / 2 for i in x], osi, bw, label="OSI 单数组", color="#2A9D8F",
           edgecolor="white", linewidth=0.6)
    for i, (a, b) in enumerate(zip(ab, osi)):
        if i == 3:      # 迁移组左右两根差异过大，标签分列上下，避免互相遮挡
            ax.text(i - bw / 2 - 0.03, a + 55, f"{a:.0f}", ha="right", va="bottom",
                    fontsize=7.6, color="#5A6B7A")
            ax.text(i + bw / 2, 55, f"{b:.0f}", ha="center", va="bottom", fontsize=7.6,
                    color="#1F6E66")
            continue
        ax.text(i - bw / 2, a + 40, f"{a:.0f}", ha="center", va="bottom", fontsize=7.6,
                color="#5A6B7A")
        ax.text(i + bw / 2, b + 40, f"{b:.0f}", ha="center", va="bottom", fontsize=7.6,
                color="#1F6E66")
    ax.annotate("迁移并入碰撞：2067 s → 9 s", xy=(3.31, 120), xytext=(2.55, 1520),
                fontsize=9.5, color="#C8503A", ha="left", va="center",
                arrowprops=dict(arrowstyle="->", color="#C8503A", lw=1.1,
                                connectionstyle="arc3,rad=-0.2"))
    ax.set_xticks(list(x))
    ax.set_xticklabels(stages, fontsize=10)
    ax.set_ylabel("128000 步累计耗时 (s)", fontsize=9.5)
    ax.set_ylim(0, 2900)
    ax.tick_params(axis="y", labelsize=8.5)
    for sp in ("top", "right"):
        ax.spines[sp].set_visible(False)
    ax.grid(axis="y", color="#E3EAEF", lw=0.8)
    ax.set_axisbelow(True)
    ax.legend(frameon=False, fontsize=9, loc="upper right", ncol=2)
    fig.tight_layout()
    fig.savefig(path, transparent=False, facecolor="white")
    plt.close(fig)


# ---------------------------------------------------------------- 图 2：吞吐阶梯
def chart_mlups(path):
    rows = [
        ("Jaber 2025 AMR（论文参照）", 1089, "#B9C4CD"),
        ("BOX3D 初期 A–B（571393）", 379, "#8FA9BD"),
        ("BOX3D A–B 传输优化（575680）", 545, "#8FA9BD"),
        ("BOX3D A–B 传输优化（586265）", 822, "#8FA9BD"),
        ("BOX3D_OSI：A–B（双精度）", 938, "#8FA9BD"),
        ("BOX3D_OSI：OSI（双精度）", 1336, "#2A9D8F"),
        ("改为单精度（621436/B）", 1864, "#2A9D8F"),
        ("+ 碰撞核等工程优化（622137/B）", 2297, "#1F6E66"),
    ]
    labels = [r[0] for r in rows]
    vals = [r[1] for r in rows]
    cols = [r[2] for r in rows]
    fig, ax = plt.subplots(figsize=(9.4, 4.0), dpi=220)
    y = list(range(len(rows)))[::-1]
    ax.barh(y, vals, 0.62, color=cols, edgecolor="white", linewidth=0.6)
    for yi, v in zip(y, vals):
        if v == 938:      # 与参考线文字重叠，改标在柱内
            ax.text(v - 25, yi, f"{v}", va="center", ha="right", fontsize=9.5,
                    color="white", fontweight="bold")
            continue
        ax.text(v + 30, yi, f"{v}", va="center", fontsize=9.5, color="#22303C")
    ax.set_yticks(y)
    ax.set_yticklabels(labels, fontsize=10)
    ax.set_xlabel("MLUPS（有效格点更新量 / 墙钟秒）", fontsize=9.5)
    ax.set_xlim(0, 2600)
    ax.tick_params(axis="x", labelsize=8.5)
    for sp in ("top", "right", "left"):
        ax.spines[sp].set_visible(False)
    ax.grid(axis="x", color="#E3EAEF", lw=0.8)
    ax.set_axisbelow(True)
    ax.axvline(1089, color="#B9C4CD", lw=1.1, ls="--")
    ax.text(1080, 3.55, "论文参照 1089", fontsize=8.5, color="#7C8A96", ha="right",
            va="center", rotation=90)
    fig.tight_layout()
    fig.savefig(path, facecolor="white")
    plt.close(fig)


# ---------------------------------------------------------------- 示意图（原生形状）
def osi_schematic(slide, x, y, w, h):
    """A-B 双数组 vs OSI 单数组环：左右并列。"""
    half = (w - Inches(0.5)) / 2
    # 左：A-B
    rect(slide, x, y, half, Inches(0.34), fill=NAVY)
    box_text(slide.shapes[-1], [("A–B：两块 MultiFab 交替读写", 11, True, WHITE)])
    ay = y + Inches(0.5)
    for i, nm in enumerate(["f_old", "f_new"]):
        rr = rect(slide, x + Inches(0.15), ay + i * Inches(0.86), half - Inches(0.3),
                  Inches(0.66), fill=WHITE, line=MID)
        box_text(rr, [(nm, 12, True, NAVY),
                      ("每步：读 A 写 B → 交换指针", 9, False, GRAY)])
    rect(slide, x + Inches(0.15), ay + Inches(1.78), half - Inches(0.3), Inches(0.5),
         fill=RGBColor(0xFB, 0xEE, 0xEA), line=ORANGE)
    box_text(slide.shapes[-1], [("显存 ×2，数据搬运 ×2，另有独立迁移核", 9.5, True, ORANGE)])
    # 右：OSI
    rx = x + half + Inches(0.5)
    rect(slide, rx, y, half, Inches(0.34), fill=TEAL)
    box_text(slide.shapes[-1], [("OSI：单块 MultiFab，环形置换", 11, True, WHITE)])
    by = y + Inches(0.5)
    for k in range(4):
        rr = rect(slide, rx + Inches(0.15) + k * Inches(0.62), by, Inches(0.56),
                  Inches(0.66), fill=WHITE, line=MID)
        box_text(rr, [("f", 12, True, NAVY), (f"blk{k + 1}", 9, False, GRAY)])
    arrows = rect(slide, rx + Inches(0.15), by + Inches(0.09), Inches(2.3), Inches(0.1),
                  fill=TEAL)
    rect(slide, rx + Inches(0.15), by + Inches(0.86), half - Inches(0.3), Inches(0.66),
         fill=WHITE, line=MID)
    box_text(slide.shapes[-1],
             [("碰撞时从 A_q(x,p) 原地读写", 11.5, True, NAVY),
              ("phase += 1 即完成迁移，无迁移核、无数组交换", 9.5, False, GRAY)])
    rect(slide, rx + Inches(0.15), by + Inches(1.78), half - Inches(0.3), Inches(0.5),
         fill=RGBColor(0xE6, 0xF4, 0xF2), line=TEAL)
    box_text(slide.shapes[-1], [("显存 ÷2；每步只读一次、写一次", 9.5, True, TEAL)])


def phase_timeline(slide, x, y, w):
    """四格 phase 推进流程。"""
    steps = [
        ("① 碰撞", "collide\n按 A_q(x,p) 原地读写"),
        ("② 同层同步", "ghost 副本\n按同逻辑坐标对齐"),
        ("③ 提交", "phase p → p+1\n隐式完成迁移"),
        ("④ 物理边界", "从新 phase 读取\nNEE 重建并写回"),
    ]
    bw = (w - Inches(0.9)) / 4
    for i, (t, s) in enumerate(steps):
        bx = x + i * (bw + Inches(0.3))
        rr = rect(slide, bx, y, bw, Inches(1.15), fill=WHITE, line=MID)
        rect(slide, bx, y, bw, Inches(0.06), fill=TEAL if i < 3 else ORANGE)
        box_text(rr, [(t, 11.5, True, NAVY), (s, 9, False, GRAY)])
        if i < 3:
            ar = rect(slide, bx + bw + Inches(0.04), y + Inches(0.48), Inches(0.22),
                      Inches(0.18), fill=MID, shape=MSO_SHAPE.RIGHT_ARROW)


# ---------------------------------------------------------------- 幻灯片
def s_title():
    s = prs.slides.add_slide(BLANK)
    rect(s, 0, 0, W, H, fill=NAVY)
    # 环形象征：四个错位的浅色方块
    for i in range(4):
        rect(s, W - Inches(3.35) + i * Inches(0.42), Inches(1.35) + i * Inches(0.5),
             Inches(1.9), Inches(1.9), fill=None, line=RGBColor(0x2C, 0x5A, 0x80), lw=1.2)
    rect(s, ML, Inches(2.15), Inches(0.13), Inches(1.55), fill=TEAL)
    textbox(s, ML + Inches(0.4), Inches(2.0), Inches(9.6), Inches(1.3),
            [("自适应网格 LBM 求解器的", 34, True, WHITE),
             ("GPU 性能优化", 40, True, WHITE)], spacing=1.05)
    textbox(s, ML + Inches(0.4), Inches(3.62), Inches(9.8), Inches(1.1),
            [("单数组 OSI 推进在块结构动态 AMR 中的实现与评测", 18, False,
              RGBColor(0xBF, 0xD4, 0xE2))])
    rect(s, ML + Inches(0.4), Inches(4.62), Inches(4.2), Pt(1), fill=RGBColor(0x46, 0x6E, 0x8E))
    textbox(s, ML + Inches(0.4), Inches(4.82), Inches(9.0), Inches(1.4),
            [("汇报人：蔡以民　　通信作者：柳朝晖", 15, False, WHITE, 0),
             ("华中科技大学", 14, False, RGBColor(0xBF, 0xD4, 0xE2), 6),
             ("中国计算力学大会 2026 · MS04 计算流体/流固耦合力学", 12, False,
              RGBColor(0x8F, 0xB3, 0xC9), 10)])
    textbox(s, ML + Inches(0.4), Inches(6.55), Inches(9.0), Inches(0.4),
            [("汇报时长约 15 min｜关键结论：单卡吞吐 +42%，动态 AMR 全程总耗时 −30%", 11,
              False, RGBColor(0x8F, 0xB3, 0xC9))])
    note(s, "开场 30 秒：本工作是在王亚宁师兄的 AMReX AMR-IB-LBM 求解器基础上，"
            "针对 GPU 单卡访存和动态 AMR 推进方式的性能优化。"
            "一句话结论：把 A–B 双数组换成 OSI 单数组，并把 OSI 真正做进动态 AMR，"
            "同一算例吞吐 +42%、总耗时 −30%。")


def s_outline():
    s = prs.slides.add_slide(BLANK)
    rect(s, 0, 0, Inches(4.2), H, fill=NAVY)
    textbox(s, ML, Inches(2.7), Inches(3.2), Inches(1.2),
            [("汇报大纲", 32, True, WHITE),
             ("OUTLINE", 12, False, RGBColor(0x8F, 0xB3, 0xC9), 8)])
    items = [
        ("01", "研究背景与问题", "为什么 AMR-LBM 在 GPU 上不够快"),
        ("02", "OSI 单数组推进原理", "单数组、隐式迁移、只读写一次"),
        ("03", "OSI 在动态 AMR 中的实现", "Fab 局部环、分层 phase、通信与重网格"),
        ("04", "配套 GPU 优化", "传输、碰撞核、边界、通信重叠"),
        ("05", "验证与性能评测", "逐值对照、方腔流、A–B/OSI、AMR/均匀网格"),
        ("06", "结论与展望", "OSI-IBM 耦合与多 GPU"),
    ]
    y0 = Inches(1.15)
    for i, (n, t, sub) in enumerate(items):
        y = y0 + i * Inches(0.92)
        textbox(s, Inches(4.95), y + Inches(0.02), Inches(0.9), Inches(0.6),
                [(n, 22, True, TEAL if i < 3 else MID)])
        textbox(s, Inches(5.85), y, Inches(5.2), Inches(0.42), [(t, 17, True, NAVY)])
        textbox(s, Inches(5.85), y + Inches(0.4), Inches(6.6), Inches(0.36),
                [(sub, 11, False, GRAY)])
        if i < len(items) - 1:
            rect(s, Inches(5.85), y + Inches(0.82), Inches(6.6), Pt(0.75), fill=LIGHT)
    note(s, "10 秒过。强调后半程是重点：03/04 是实现，05 是数据。")


def s_background():
    s = prs.slides.add_slide(BLANK)
    frame(s, "为什么还要优化：AMR-LBM 在 GPU 上的三个瓶颈", "01 研究背景")
    cols = [
        ("计算需求", "复杂流动只需局部高分辨率；全域细网格使计算量与显存迅速增长。"
                     "AMR 是必需的，不是可选项。"),
        ("访存受限", "D3Q27 单精度：有效格点每步至少读+写 27×4 B×2 = 216 B，"
                     "算术强度极低。A100 上 GPU 是带宽受限，不是算力受限。"),
        ("双数组代价", "A–B 方式维护 f_old/f_new 两套分布函数、每步交换指针、"
                       "另有独立迁移核：显存和数据搬运都翻倍。"),
    ]
    bw = (CW - Inches(0.6)) / 3
    for i, (t, body) in enumerate(cols):
        x = ML + i * (bw + Inches(0.3))
        rect(s, x, Inches(1.45), bw, Inches(3.05), fill=LIGHT, line=None)
        rect(s, x, Inches(1.45), bw, Inches(0.07), fill=TEAL if i else ORANGE)
        textbox(s, x + Inches(0.18), Inches(1.65), bw - Inches(0.36), Inches(0.5),
                [(t, 17, True, NAVY)])
        textbox(s, x + Inches(0.18), Inches(2.2), bw - Inches(0.36), Inches(2.1),
                [(body, 11.5, False, INK)], spacing=1.25)
    rect(s, ML, Inches(4.75), CW, Inches(1.05), fill=WHITE, line=NAVY, lw=1.0)
    textbox(s, ML + Inches(0.25), Inches(4.9), CW - Inches(0.5), Inches(0.85),
            [("本工作的着眼点：不改变物理模型与 AMR 网格组织，只降低「状态存储」和"
              "「每步数据搬运」的代价。", 14, True, NAVY)], anchor=MSO_ANCHOR.MIDDLE)
    placeholder(s, ML, Inches(5.95), Inches(5.9), Inches(0.95),
                "占位：GPU 上 LBM 的 roofline / 带宽示意",
                "可选：用 runs/temporary/uniform_hw_profile 的带宽上限与实测点作图")
    textbox(s, Inches(6.7), Inches(6.05), Inches(6.1), Inches(0.85),
            [("口径提醒：以上为理论下限，未取到硬件计数器；"
              "不复用 Nsight 因权限失败的数据作为带宽结论。", 10, False, GRAY)],
            spacing=1.15)
    note(s, "30 秒。这里只讲「为什么慢」，不要展开 AMReX 本身。"
            "如果被追问带宽数字，直接说没有 Nsight 计数器权限，只用理论读写量。")


def s_prior():
    s = prs.slides.add_slide(BLANK)
    frame(s, "研究起点：师兄的 AMReX 块结构动态 AMR-IB-LBM 求解器", "01 研究背景")
    placeholder(s, ML, Inches(1.4), Inches(6.0), Inches(2.55),
                "占位：AMReX 块结构网格 + 三层嵌套示意",
                "可复用师兄 PPT 第 10 页的 AMReX 网格图（需注明出处）")
    bullets = [
        ("已完成", "块结构动态 AMR、体积型粗细传输、BTDF-IBM 耦合、CPU/GPU 可移植"),
        ("已优化", "Dual grid 通信隐藏：性能 +45.52%，128 GPU 并行效率 97.95%"),
        ("文献", "Wang et al., Comput. Phys. Commun. 314 (2025) 109674"),
        ("未优化", "单卡推进方式（A–B 双数组）、AMR 传输与边界仍按最初实现"),
    ]
    for i, (tag, body) in enumerate(bullets):
        y = Inches(1.45) + i * Inches(0.66)
        rect(s, Inches(6.95), y + Inches(0.08), Inches(0.9), Inches(0.3), fill=TEAL)
        box_text(s.shapes[-1], [(tag, 9.5, True, WHITE)])
        textbox(s, Inches(8.0), y, Inches(4.8), Inches(0.6), [(body, 11, False, INK)],
                spacing=1.1)
    rect(s, ML, Inches(4.2), CW, Inches(1.5), fill=LIGHT)
    textbox(s, ML + Inches(0.3), Inches(4.35), CW - Inches(0.6), Inches(0.45),
            [("起点基线（同一方腔算例、动态三层 AMR、单 A100）", 13, True, NAVY)])
    stats = [("379", "MLUPS", "初期 A–B 动态 AMR（571393）"),
             ("2.88 h", "初期 A–B 累计 total", "64000 步 / 10371.9 s"),
             ("≈1/3", "相对论文同量级参照", "Jaber 2025：1089 MLUPS")]
    for i, (num, unit, cap) in enumerate(stats):
        x = ML + Inches(0.35) + i * Inches(4.0)
        textbox(s, x, Inches(4.85), Inches(3.6), Inches(0.5),
                [(f"{num} ", 26, True, ORANGE)])
        textbox(s, x, Inches(5.32), Inches(3.6), Inches(0.3), [(cap, 10, False, GRAY)])
    textbox(s, ML, Inches(5.9), CW, Inches(0.8),
            [("注意：571393 的基础网格与最高层数在当时记录中未写明，不能与后续 128³/level 2 "
              "系列直接对比；与论文参照在层级、加密判据和 MLUPS 采样窗口上也不同，"
              "本页只作起点量级说明。", 10, False, GRAY)], spacing=1.15)
    note(s, "40 秒。尊重前人工作：求解器能跑、能验证；问题是「大规模 GPU 下仍有存储/访存优化空间」。"
            "起点 379 MLUPS 来自 571393（64000 步累计 10371.9 s），约为论文参照的 1/3；"
            "该行基础网格/层数在记录中未写，不与后面 128³/level 2 系列直接比较。")


def s_osi_principle():
    s = prs.slides.add_slide(BLANK)
    frame(s, "方法一：单数组 OSI 推进", "02 OSI 原理")
    osi_schematic(s, ML, Inches(1.35), Inches(7.5), Inches(3.1))
    # 公式
    rect(s, Inches(8.45), Inches(1.35), Inches(4.33), Inches(1.35), fill=NAVY)
    textbox(s, Inches(8.6), Inches(1.45), Inches(4.05), Inches(0.4),
            [("地址映射", 11, True, RGBColor(0x8F, 0xB3, 0xC9))])
    textbox(s, Inches(8.6), Inches(1.78), Inches(4.05), Inches(0.5),
            [("A_q(x, p) = ( x − p·e_q ) mod L", 15, True, WHITE)])
    textbox(s, Inches(8.6), Inches(2.28), Inches(4.05), Inches(0.4),
            [("L：该 Fab 的循环长度；p：该层 phase", 9.5, False,
              RGBColor(0x8F, 0xB3, 0xC9))])
    rows = [
        ("一次碰撞", "读 A_q(x,p) → 恢复宏观量 → 碰撞 → 写回 A_q(x,p)"),
        ("恒等式", "A_q(x, p+1) = A_q(x − e_q, p)：迁移由 phase 表达"),
        ("收益", "存储减半；每步每格点只读写一次；无迁移核与指针交换"),
    ]
    for i, (tag, body) in enumerate(rows):
        y = Inches(2.95) + i * Inches(0.85)
        rect(s, Inches(8.45), y, Inches(4.33), Inches(0.75), fill=WHITE, line=MID)
        textbox(s, Inches(8.6), y + Inches(0.04), Inches(1.0), Inches(0.3),
                [(tag, 10, True, TEAL)])
        textbox(s, Inches(8.6), y + Inches(0.32), Inches(4.05), Inches(0.4),
                [(body, 9.5, False, INK)], spacing=1.0)
    rect(s, ML, Inches(4.7), Inches(7.5), Inches(1.05), fill=LIGHT)
    textbox(s, ML + Inches(0.2), Inches(4.8), Inches(7.1), Inches(0.85),
            [("但原论文只在规则单网格上验证。块结构 AMR 里没有「一整块连续数组」——"
              "Fab 之间、层之间、重网格前后都不连续。", 12.5, True, NAVY)],
            anchor=MSO_ANCHOR.MIDDLE)
    textbox(s, ML, Inches(5.95), Inches(12.2), Inches(0.9),
            [("Ma et al., Comput. Phys. Commun. 283 (2023) 108603 — one-step index 算法",
              10, False, GRAY),
             ("本工作的核心：把 OSI 从单层均匀网格扩展到块结构动态 AMR，并保持 GPU 局部访存。",
              12, True, TEAL, 6)], )
    note(s, "1.5 分钟。公式只讲结论：地址由逻辑坐标、方向和 phase 决定；"
            "phase 加一就等于所有量整体迁移了一格。不要逐个分量推导。")


def s_amr_buffer():
    s = prs.slides.add_slide(BLANK)
    frame(s, "方法二之一：每个 Fab 一个局部环形缓冲", "03 动态 AMR 中的 OSI")
    rect(s, ML, Inches(1.32), Inches(6.1), Inches(2.6), fill=WHITE, line=MID)
    textbox(s, ML + Inches(0.2), Inches(1.42), Inches(5.7), Inches(2.4),
            [("问题", 12, True, ORANGE),
             ("AMR level 不是一块全局连续数组：一个逻辑格点可能在别的 Fab、"
              "别的 rank，或该层根本没分配。", 11, False, INK, 6),
             ("若按 Domain 长度做映射，每次读 DDF 都要查别的 Fab 或跨 rank，"
              "违背 Array4 的局部内存模型，kernel 也就失去意义。", 11, False, INK, 8)],
            spacing=1.2)
    rect(s, Inches(6.95), Inches(1.32), Inches(5.83), Inches(2.6), fill=LIGHT)
    textbox(s, Inches(7.15), Inches(1.42), Inches(5.4), Inches(2.4),
            [("做法：Fab-local 循环区", 12, True, TEAL),
             ("循环区 = 该 FArrayBox 的完整分配 Box（valid + 2 层 ghost）；"
              "映射先在全局逻辑坐标上做，再减去 smallEnd 转成局部坐标。",
              11, False, INK, 6),
             ("L 逐 Fab 不同、甚至逐方向不同都没关系：MPI 传的是「逻辑坐标 + 方向」"
              "的数值，接收端按自己的布局重新算地址。", 11, False, INK, 6)],
            spacing=1.2)
    # 小图：一个 Fab 的环
    px, py = ML + Inches(0.3), Inches(4.25)
    rect(s, px, py, Inches(4.9), Inches(2.3), fill=WHITE, line=MID, dash=True)
    textbox(s, px + Inches(0.15), py + Inches(0.08), Inches(4.6), Inches(0.3),
            [("占位：单 Fab 环形置换俯视示意（3×3 带 ghost，phase 0/1/2 三张叠放）", 9.5,
              False, GRAY)])
    textbox(s, Inches(5.95), Inches(4.3), Inches(6.8), Inches(2.3),
            [("必须持续成立的约束（有 CPU 单元测试与逐步对照）", 12, True, NAVY),
             ("· 任意 (q, p) 下所有逻辑格点映射唯一，永不越出分配 Box", 10.5, False, INK, 6),
             ("· valid 与逻辑 ghost 在同一 phase 不互相别名", 10.5, False, INK, 4),
             ("· phase 跨过多个 Fab 长度周期后仍回到原映射", 10.5, False, INK, 4),
             ("· 主机端 phase 元数据不能早于 GPU kernel 完成而自增", 10.5, False, INK, 4)],
            spacing=1.15)
    note(s, "1 分钟。关键一句：OSI 的局部性必须在 Fab 内，不能是整层全局数组。"
            "这里可以指向 CPU 单元测试（src/OsiIndex.H 有独立测试）。")


def s_amr_phase():
    s = prs.slides.add_slide(BLANK)
    frame(s, "方法二之二：分层 phase 与时间子循环解耦", "03 动态 AMR 中的 OSI")
    textbox(s, ML, Inches(1.3), Inches(6.4), Inches(0.6),
            [("每个 level 有自己的 phase，随本层推进次数增加；粗步之内出现", 12, False, INK)])
    for i, (lv, txt) in enumerate([("L0", "phase += 1"), ("L1", "phase += 2"),
                                   ("L2", "phase += 4")]):
        y = Inches(1.95) + i * Inches(0.62)
        rect(s, ML, y, Inches(1.0), Inches(0.5), fill=NAVY)
        box_text(s.shapes[-1], [(lv, 12, True, WHITE)])
        rect(s, ML + Inches(1.05), y, Inches(3.1), Inches(0.5), fill=LIGHT)
        box_text(s.shapes[-1], [(txt, 12, False, NAVY)], align=PP_ALIGN.LEFT)
    rect(s, ML, Inches(3.9), Inches(6.4), Inches(1.9), fill=WHITE, line=MID)
    textbox(s, ML + Inches(0.2), Inches(4.0), Inches(6.0), Inches(1.7),
            [("为什么不能用一个全局 step 当 phase", 12, True, ORANGE),
             ("Cycle2 中细层每个粗步推进 2 次，三层就是 1:2:4。若用一个 step，"
              "细层的环会与粗层错位，重网格后无法判断某块存储处于哪个 phase。",
              11, False, INK, 6),
             ("phase、时间步、物理时间是三个不同的量，代码里分开命名。", 11, True, TEAL, 6)],
            spacing=1.2)
    placeholder(s, Inches(7.25), Inches(1.3), Inches(5.53), Inches(4.5),
                "占位：三层时间子循环 + 各层 phase 时序图",
                "横轴粗步，纵轴 L0/L1/L2；每格标出本层 phase 数值")
    textbox(s, ML, Inches(6.05), Inches(6.4), Inches(0.9),
            [("phase 是存储元数据，不是物理时间；canonicalize 之后可以安全清零。", 10.5,
              False, GRAY)], spacing=1.15)
    note(s, "45 秒。核心是「1:2:4」。如果听众问为什么不能统一，就说细层子循环导致错位。")


def s_amr_comm():
    s = prs.slides.add_slide(BLANK)
    frame(s, "方法二之三：ghost 与 MPI 的新契约", "03 动态 AMR 中的 OSI")
    phase_timeline(s, ML, Inches(1.3), CW)
    rect(s, ML, Inches(2.75), Inches(6.05), Inches(2.55), fill=LIGHT)
    textbox(s, ML + Inches(0.2), Inches(2.85), Inches(5.65), Inches(2.35),
            [("A–B 的语义", 12, True, GRAY),
             ("ghost 里放的是固定位置的跨 Fab 数据，FillBoundary 直接拷邻居值；"
              "迁移核再把整个 valid 区往 +e_q 方向推。", 11, False, INK, 6),
             ("OSI 的语义", 12, True, TEAL, 10),
             ("同层通信 = 同步「同一逻辑坐标」的重叠副本：发送端从 A_q(x_send, p) "
              "解码完整 Q 个分量，接收端按自己的 Fab 和 p 重新编码到 "
              "A_q(x_ghost, p)。空间迁移由随后的 phase 提交完成，通信本身不搬位置。",
              11, False, INK, 6)], spacing=1.2)
    placeholder(s, Inches(6.9), Inches(2.75), Inches(5.88), Inches(2.55),
                "占位：发送端/接收端地址映射示意",
                "标出 x_send、x_ghost、两侧不同的 L 与 p")
    rect(s, ML, Inches(5.5), CW, Inches(1.35), fill=WHITE, line=ORANGE, lw=1.0)
    textbox(s, ML + Inches(0.25), Inches(5.6), CW - Inches(0.5), Inches(1.15),
            [("实现约束（踩过的坑）", 11.5, True, ORANGE),
             ("phase 只能在整层碰撞与通信都完成后提交一次；不能让不同 Fab、不同 rank "
              "各自提前加。GPU 上还必须用同一 stream 或显式同步保证 kernel 已完成，"
              "只改主机端元数据会静默错位。", 10.5, False, INK, 5)], spacing=1.15)
    note(s, "1.5 分钟。这是最容易出错的地方，也是审稿时会问的点："
            "「迁移去哪了？」答案是 phase 提交，不是某个 kernel。")


def s_amr_transfer():
    s = prs.slides.add_slide(BLANK)
    frame(s, "方法二之四：粗细传输、边界与动态重网格", "03 动态 AMR 中的 OSI")
    cards = [
        ("插值与平均", "粗细界面与新建层的插值、细→粗的 valid 平均改成批量算子，"
                       "直接在 twisted 布局上按 phase 寻址；静态与重网格两条路径共用建表与发射器。"),
        ("物理边界", "边界值从新 phase 的内部逻辑地址读参考值，用 NEE 重建完整 Q 分量，"
                     "直接写回 A_q(x_b, p+1)；批量化薄面发射，避免逐面启动。"),
        ("重网格", "布局不变 → 保留 raw state 与 phase；布局变化 → ParallelCopyOsi 把旧 valid "
                   "按 CPC 标签迁到新布局 phase 0；新增区只写稀疏 valid，不构造整层 canonical。"),
    ]
    bw = (CW - Inches(0.5)) / 3
    for i, (t, body) in enumerate(cards):
        x = ML + i * (bw + Inches(0.25))
        rect(s, x, Inches(1.3), bw, Inches(2.05), fill=WHITE, line=MID)
        rect(s, x, Inches(1.3), bw, Inches(0.06), fill=TEAL)
        textbox(s, x + Inches(0.16), Inches(1.45), bw - Inches(0.32), Inches(0.35),
                [(t, 14, True, NAVY)])
        textbox(s, x + Inches(0.16), Inches(1.85), bw - Inches(0.32), Inches(1.4),
                [(body, 10, False, INK)], spacing=1.2)
    placeholder(s, Inches(6.9), Inches(3.55), Inches(5.88), Inches(3.3),
                "占位：重网格三种情形（不变 / 变化 / 新增区）示意",
                "三种箭头颜色对应三种路径")
    rect(s, ML, Inches(3.55), Inches(6.05), Inches(3.3), fill=LIGHT)
    textbox(s, ML + Inches(0.2), Inches(3.68), Inches(5.65), Inches(0.4),
            [("为什么不让 AMReX 直接处理 twisted 布局", 12, True, ORANGE)])
    textbox(s, ML + Inches(0.2), Inches(4.1), Inches(5.65), Inches(2.6),
            [("FillBoundary、ParallelCopy、FillPatch、average_down、plotfile、checkpoint "
              "都默认输入是 canonical 布局。", 10.5, False, INK, 4),
             ("两个显式适配器：GatherCanonical（twisted → canonical）与 "
              "ScatterCanonical（canonical → 指定 phase）。", 10.5, False, INK, 8),
             ("第一版绝不让现有函数「猜」布局；适配器必须显式声明作用区域是 "
              "valid-only 还是 valid+ghost。", 10.5, True, NAVY, 4),
             ("开销只在 checkpoint / 重网格边界上发生，不在每步推进里。", 10.5, False,
              TEAL, 8)], spacing=1.2)
    note(s, "1.5 分钟。三层信息：传输要对齐 phase；边界从新 phase 读；重网格尽量保留 phase。"
            "适配器只在 I/O 和重网格调用，不在每步。")


def s_gpu_opt():
    s = prs.slides.add_slide(BLANK)
    frame(s, "方法三：配套的 GPU 优化（工程完善）", "04 优化措施")
    rows = [
        ("碰撞核", "128 线程/block ＋ 每个格点复用九个轴向坐标增量，减少静态地址生成指令",
         "单层 +30%，真实 AMR +24%"),
        ("批量传输", "插值/平均按 Q 选择线程映射（Q27 半 warp、16 父单元/lane），合并盒启动",
         "插值 −54%，平均 −43%"),
        ("边界处理", "批量薄面发射、直接面重建", "阶段筛选后接入"),
        ("同层通信", "MPI 缓冲按 [q][cell] 排列；先碰发送源 Fab，其余 collide 与 staging/MPI 重叠",
         "total −5.5%（2 GPU）/ −4.2%（4 GPU）"),
        ("重网格建表", "restriction 任务展开移到 GPU", "total −0.94%（单卡短测）"),
    ]
    # 表头
    xs = [ML, ML + Inches(1.55), ML + Inches(6.6), ML + Inches(9.9)]
    ws = [Inches(1.55), Inches(5.05), Inches(3.3), Inches(2.33)]
    heads = ["环节", "做法", "该环节收益", ""]
    for i, h in enumerate(heads[:3]):
        rect(s, xs[i], Inches(1.3), ws[i], Inches(0.42), fill=NAVY)
        box_text(s.shapes[-1], [(h, 11, True, WHITE)], align=PP_ALIGN.LEFT)
    for r, (a, b, c) in enumerate(rows):
        y = Inches(1.72) + r * Inches(0.83)
        rect(s, xs[0], y, CW, Inches(0.83), fill=LIGHT if r % 2 == 0 else WHITE)
        textbox(s, xs[0] + Inches(0.12), y + Inches(0.1), ws[0] - Inches(0.2), Inches(0.66),
                [(a, 12, True, NAVY)], anchor=MSO_ANCHOR.MIDDLE)
        textbox(s, xs[1] + Inches(0.1), y + Inches(0.06), ws[1] - Inches(0.2), Inches(0.72),
                [(b, 10.5, False, INK)], anchor=MSO_ANCHOR.MIDDLE, spacing=1.1)
        textbox(s, xs[2] + Inches(0.1), y + Inches(0.1), ws[2] - Inches(0.2), Inches(0.66),
                [(c, 11, True, TEAL)], anchor=MSO_ANCHOR.MIDDLE, spacing=1.1)
    textbox(s, ML, Inches(5.95), CW, Inches(0.95),
            [("这些都是针对原实现的工程优化，不是新算法；每项都单独做过同节点交替 A/B "
              "与逐值回归，收益互不相加。", 11, False, GRAY),
             ("碰撞核的隔离实测（621730，256³ 单 Fab）：原路径 2822 → 仅 128 线程 3122 → "
              "仅坐标复用 2988 → 组合 3685 MLUPS。", 11, False, INK, 6)], spacing=1.15)
    note(s, "1 分钟。定位要清楚：这一页是「工程完善」，主创新在前面三页。"
            "数字都是各自冻结版本的对照，不要相乘或相加。")


def s_validation():
    s = prs.slides.add_slide(BLANK)
    frame(s, "正确性：OSI 必须与 A–B 逐值一致", "05 验证")
    left = [
        ("锁步逐值对照", "同一初始场、同一步数，A–B 与 OSI 对比全部 DDF 分量"),
        ("量级", "max Linf ~ 1e-15（双精度）；FP32 下大量情形逐位为 0"),
        ("覆盖范围", "单层/多层、单/多 Fab、单/双 rank、周期/非周期、动态重网格"),
        ("计数", "多轮回归累计数十份逐层 valid/active 范数与宏观场比较"),
    ]
    for i, (t, b) in enumerate(left):
        y = Inches(1.35) + i * Inches(0.82)
        rect(s, ML, y, Inches(5.6), Inches(0.74), fill=LIGHT)
        textbox(s, ML + Inches(0.15), y + Inches(0.06), Inches(1.55), Inches(0.6),
                [(t, 11, True, NAVY)], anchor=MSO_ANCHOR.MIDDLE)
        textbox(s, ML + Inches(1.78), y + Inches(0.04), Inches(3.7), Inches(0.66),
                [(b, 9.5, False, INK)], anchor=MSO_ANCHOR.MIDDLE, spacing=1.05)
    rect(s, ML, Inches(4.75), Inches(5.6), Inches(0.95), fill=WHITE, line=ORANGE)
    textbox(s, ML + Inches(0.18), Inches(4.82), Inches(5.25), Inches(0.8),
            [("边界：covered 存储中存在与 A–B 同源的 NaN 模式，结论按 active/valid 统计，"
              "不宣称全 valid 正确。", 9.5, False, ORANGE)], spacing=1.1,
            anchor=MSO_ANCHOR.MIDDLE)
    # 两张真实图
    fig1 = os.path.join(POST, "images",
                        "job624717_C_job623012_uniform512__centerline_t1000_with_Cortes.png")
    fig2 = os.path.join(POST, "images",
                        "job_unknown_data_post_processing__Re3200_3D_overlap_288000_fig18b.png")
    s.shapes.add_picture(fig1, Inches(6.45), Inches(1.35), height=Inches(3.05))
    s.shapes.add_picture(fig2, Inches(9.85), Inches(1.35), height=Inches(3.05))
    textbox(s, Inches(6.45), Inches(4.5), Inches(5.85), Inches(0.95),
            [("左：Re=1000 方腔流中心线（AMR B/C 与均匀 512³，t=1000，Cortes & Miller 参考）；"
              "右：Re=3200，step 288000 与 Jaber Fig.18(b) 参考。", 9, False, GRAY)],
            spacing=1.1)
    rect(s, Inches(6.45), Inches(5.55), Inches(5.78), Inches(1.25), fill=LIGHT)
    textbox(s, Inches(6.62), Inches(5.64), Inches(5.45), Inches(1.1),
            [("两条曲线的正式收敛标志仍为 converged=0，只能说明计算结果与参考趋势一致，"
              "不能写成已收敛或全场精度验收。", 9.5, False, NAVY)], spacing=1.15)
    note(s, "1 分钟。先讲逐值一致（这是算法正确性的硬证据），再讲物理验证。"
            "主动说明 covered NaN 的边界，不要等提问。")


def s_perf_main():
    s = prs.slides.add_slide(BLANK)
    frame(s, "主结果：动态 AMR 全程 128000 步，A–B vs OSI", "05 性能")
    img = os.path.join(ASSETS, "chart_breakdown.png")
    chart_breakdown(img)
    s.shapes.add_picture(img, ML, Inches(1.28), width=Inches(7.6))
    stats = [("−30.3%", "累计 total 耗时", "5306 → 3701 s", ORANGE),
             ("+42%", "吞吐", "938 → 1336 MLUPS", TEAL),
             ("2067 → 9 s", "迁移阶段", "隐式迁移进入碰撞", TEAL),
             ("≈0", "A–B/OSI 数值差", "max Linf ~1e-15", NAVY)]
    for i, (num, cap, sub, col) in enumerate(stats):
        y = Inches(1.35) + i * Inches(1.15)
        rect(s, Inches(8.45), y, Inches(4.33), Inches(1.0), fill=LIGHT)
        rect(s, Inches(8.45), y, Pt(4), Inches(1.0), fill=col)
        textbox(s, Inches(8.62), y + Inches(0.08), Inches(4.05), Inches(0.45),
                [(num, 19, True, col)])
        textbox(s, Inches(8.62), y + Inches(0.5), Inches(4.05), Inches(0.45),
                [(f"{cap}（{sub}）", 9.5, False, INK)])
    textbox(s, ML, Inches(5.75), Inches(7.6), Inches(1.2),
            [("口径：job 613415（A–B）与 613417（OSI），双精度，Re=1000，128³ 基础网格、"
              "最高 level 2、每 32 粗步重网格，两轮使用相同网格演化过程。", 9, False, GRAY),
             ("碰撞从 1940 s 升到 2290 s 是预期的：隐式迁移被并入碰撞的读写，"
              "迁移核消失（2067 s → 9 s），净收益来自这里。", 9.5, False, NAVY, 5)],
            spacing=1.1)
    note(s, "2 分钟，全片最重的一页。先说总账（−30%、+42%），再pointer到迁移柱消失。"
            "主动解释碰撞变贵：不是变差，是把迁移搬进了 collide。")


def s_perf_steps():
    s = prs.slides.add_slide(BLANK)
    frame(s, "优化历程：从 379 到 2297 MLUPS", "05 性能")
    img = os.path.join(ASSETS, "chart_mlups.png")
    chart_mlups(img)
    s.shapes.add_picture(img, ML, Inches(1.22), width=Inches(9.6))
    textbox(s, Inches(10.35), Inches(1.35), Inches(2.5), Inches(4.2),
            [("怎么读这张图", 11.5, True, NAVY),
             ("前五条都是 A–B 双数组程序，其中后两条是 OSI 分支自己的 A–B 基线；"
              "灰色柱是论文参照。", 9.5, False, INK, 6),
             ("OSI 单数组是第一次跨台阶（938 → 1336），且与上一柱同程序、同网格。", 9.5,
              False, INK, 6),
             ("再往上是改为单精度（1864，此时尚未接入碰撞核），最后是累积了碰撞核、"
              "传输、边界与通信优化的当前程序（2297）。", 9.5, False, INK, 6)],
            spacing=1.15)
    textbox(s, ML, Inches(5.75), CW, Inches(1.15),
            [("灰色参考：Jaber et al., CPC 311 (2025) 109543，AMR 三层、单精度、64³ 基础网格。",
              9, False, GRAY),
             ("不同柱的层级、阈值、精度、采样窗口不完全相同，只表示性能台阶，不构成严格的单因素加速比。",
              9.5, False, GRAY, 4)], spacing=1.1)
    note(s, "1.5 分钟。台阶叙事：初期标定与传输优化 → OSI → FP32/碰撞核 → 当前程序。"
            "如果有人细究某一根柱子，回到 docs/MLUPS记录.md 的具体 job。")


def s_amr_vs_uniform():
    s = prs.slides.add_slide(BLANK)
    rows = [
        ("网格布置", "128³ 基础 + L1/L2（最细等效 512³）", "512³ 单 Fab"),
        ("t=1000 累计 total", "3483 s（C 阈值，性能优化前程序）", "17173 s（含 39 次重启）"),
        ("吞吐（末窗口）", "1909 MLUPS", "4003 MLUPS"),
        ("有效网格", "约 17.7M（512³ 的 1/7.6）", "134M"),
        ("Arena 使用高水位", "5049 MiB", "34094 MiB（另一 job）"),
    ]
    tw = Inches(8.2)
    for i, (a, b, c) in enumerate(rows):
        y = Inches(1.32) + i * Inches(0.62)
        rect(s, ML, y, tw * 0.34, Inches(0.6), fill=NAVY if i == 0 else LIGHT)
        rect(s, ML + tw * 0.34, y, tw * 0.33, Inches(0.6), fill=NAVY if i == 0 else WHITE)
        rect(s, ML + tw * 0.67, y, tw * 0.33, Inches(0.6), fill=NAVY if i == 0 else WHITE)
        for j, txt in enumerate([a, b, c]):
            col = WHITE if i == 0 else (NAVY if j == 0 else INK)
            bold = i == 0 or j == 0
            textbox(s, ML + j * tw * (0.33 if j else 0.34) + Inches(0.12),
                    y + Inches(0.04), tw * 0.32 - Inches(0.2), Inches(0.52),
                    [(txt, 10 if i == 0 else 9.5, bold, col)], anchor=MSO_ANCHOR.MIDDLE)
    fig = os.path.join(POST, "images", "gif-B", "job622356_job622356__B__victory.0039.png")
    s.shapes.add_picture(fig, Inches(9.15), Inches(1.3), height=Inches(3.85))
    textbox(s, Inches(9.15), Inches(5.22), Inches(3.6), Inches(1.5),
            [("Re=1000 方腔流 y=0.5 切片涡量模长，step 128000；"
              "框线为 AMR level 1/2 块。", 8.5, False, GRAY),
             ("该图与左侧表不是同一 job，只示意网格面貌。", 8.5, False, GRAY, 3)],
            spacing=1.05)
    textbox(s, ML, Inches(4.75), Inches(8.3), Inches(2.1),
            [("读法：AMR 单步吞吐低于均匀网格（1909 vs 4003 MLUPS），但同一物理时间下"
              "总耗时少约 5 倍、有效网格少 7.6 倍。", 11, False, INK),
             ("均匀 512³ 的 DDF 常驻约 134M×27×4 B ≈ 13.5 GiB，加上 Arena 与临时缓冲在 40 GB "
              "卡上已接近上限；AMR 版约 5 GB。", 10, False, INK, 5),
             ("口径：C/624717 是性能优化前的冻结程序、每 3200 步输出；512³/623012 分 40 段重启、"
              "输出更重；34094 MiB 一项来自 622016 短测，均不能当作严格等精度加速比。",
              9, False, GRAY, 6)],
            spacing=1.15)
    note(s, "1.5 分钟。这页回答「为什么要 AMR」：不是为了单步更快，是为了同一道题能跑得完、"
            "能放进一张卡。数字差异要主动交代版本与重启口径。")
    s = prs.slides[-1]


def s_multigpu():
    s = prs.slides.add_slide(BLANK)
    frame(s, "多 GPU：只报相同卡数下的优化，不报扩展性", "05 性能")
    rect(s, ML, Inches(1.3), Inches(6.3), Inches(3.6), fill=LIGHT)
    textbox(s, ML + Inches(0.22), Inches(1.42), Inches(5.9), Inches(3.4),
            [("已验收", 13, True, TEAL),
             ("· 碰撞核 128 线程＋坐标复用扩展到多 rank：2 GPU / 4 GPU 的均匀网格与"
              "真实 AMR，同节点三轮交替", 10.5, False, INK, 8),
             ("· total 下降：均匀 5.9% / 6.9%，AMR 6.8% / 8.1%（2 GPU / 4 GPU）", 10.5,
              False, INK, 4),
             ("· 碰撞与通信重叠：2 GPU 5.5%、4 GPU 4.2%", 10.5, False, INK, 4),
             ("· 全部伴随逐层 DDF 范数 0 的数值回归", 10.5, False, INK, 4)],
            spacing=1.2)
    placeholder(s, Inches(7.1), Inches(1.3), Inches(5.68), Inches(3.6),
                "占位：2 / 4 GPU 强扩展曲线",
                "待补：也需先决定是否展示；固定 AMR 下 1→2→4 GPU 目前不是单调变好")
    rect(s, ML, Inches(5.1), CW, Inches(1.65), fill=WHITE, line=ORANGE, lw=1.1)
    textbox(s, ML + Inches(0.25), Inches(5.2), CW - Inches(0.5), Inches(1.45),
            [("必须主动说明的边界", 12, True, ORANGE),
             ("固定三层布局下，1 / 2 / 4 GPU 的吞吐分别是 2256 / 1477 / 1716 MLUPS——"
              "多卡没有随卡数线性提高，说明通信与负载划分仍是瓶颈（与师兄 Dual grid 的"
              "单层均匀网格结论不同）。", 10.5, False, INK, 5),
             ("本页只主张：在相同卡数下，优化前后的对比是稳定的。", 11, True, NAVY, 5)],
            spacing=1.15)
    note(s, "1 分钟。这页是防守页。师兄讲过 97.95% 并行效率，一定会有人对比；"
            "主动承认多卡扩展没做好，把它放进展望，比被问出来好。")


def s_conclusion():
    s = prs.slides.add_slide(BLANK)
    frame(s, "结论与展望", "06 总结")
    rect(s, ML, Inches(1.3), Inches(6.0), Inches(4.4), fill=LIGHT)
    textbox(s, ML + Inches(0.22), Inches(1.42), Inches(5.6), Inches(4.2),
            [("结论", 14, True, TEAL),
             ("① 把 OSI 单数组推进从规则单网格扩展到块结构动态 AMR："
              "Fab 局部环、分层 phase、ghost/通信新契约、重网格 phase 迁移。", 10.5,
              False, INK, 8),
             ("② 同一算例动态 AMR 全程：总耗时 −30.3%，吞吐 +42%，迁移阶段 2067 → 9 s。",
              10.5, False, INK, 6),
             ("③ 同程序、同网格下 OSI 相对 A–B 提升 42%；叠加单精度与碰撞核等工程优化后，"
              "单卡吞吐达到 2297 MLUPS，高于论文同量级参照。", 10.5, False, INK, 6),
             ("④ 全部推进都有 A–B/OSI 逐值对照（~1e-15）与方腔流验证。", 10.5, False,
              INK, 6)], spacing=1.2)
    rect(s, Inches(6.85), Inches(1.3), Inches(5.93), Inches(4.4), fill=WHITE, line=NAVY)
    textbox(s, Inches(7.07), Inches(1.42), Inches(5.5), Inches(4.2),
            [("展望", 14, True, ORANGE),
             ("① OSI 与浸入边界力耦合：当前 OSI 生产路径用的是无外力、无 SGS 的 D3Q27 "
              "专用碰撞核，IBM 需重新接入并验证。", 10.5, False, INK, 8),
             ("② 多 GPU 扩展：多节点、CUDA-aware MPI、通信与计算的更细粒度流水。", 10.5,
              False, INK, 6),
             ("③ 动态重网格成本：regrid 仍占可观比例，需与加密判据、块尺寸联合调优。",
              10.5, False, INK, 6),
             ("④ 峰值显存与容量：在单卡上跑更大规模。", 10.5, False, INK, 6)],
            spacing=1.2)
    box = rect(s, ML, Inches(5.95), CW, Inches(0.95), fill=NAVY)
    box_text(box, [("本工作不是重新提出 AMR-LBM 理论，而是把 OSI 做进动态 AMR 并给出可复现的性能证据。",
                    15, True, WHITE)])
    note(s, "1 分钟。最后一句是防御性定位，也是实话。留 3 分钟提问。")


def s_thanks():
    s = prs.slides.add_slide(BLANK)
    rect(s, 0, 0, W, H, fill=NAVY)
    textbox(s, ML, Inches(2.75), Inches(9.0), Inches(0.9),
            [("谢谢，敬请批评指正", 34, True, WHITE)])
    rect(s, ML, Inches(3.75), Inches(2.4), Pt(1.2), fill=TEAL)
    textbox(s, ML, Inches(3.95), Inches(9.0), Inches(1.4),
            [("联系方式：[填写邮箱]", 13, False, RGBColor(0xBF, 0xD4, 0xE2)),
             ("算例与性能记录：BOX3D_OSI / docs/MLUPS记录.md", 11, False,
              RGBColor(0x8F, 0xB3, 0xC9), 6)])
    note(s, "备用：如果被问到具体数值，直接翻到 MLUPS记录.md 对应 job。")


for fn in (s_title, s_outline, s_background, s_prior, s_osi_principle, s_amr_buffer,
           s_amr_phase, s_amr_comm, s_amr_transfer, s_gpu_opt, s_validation,
           s_perf_main, s_perf_steps, s_amr_vs_uniform, s_multigpu, s_conclusion,
           s_thanks):
    fn()

prs.save(OUT)
print("saved", OUT, len(prs.slides._sldIdLst), "slides")
