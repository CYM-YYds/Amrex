"""OSI 适配 AMR、OSI 与 A–B 全程对比、累加优化三页（AI 临时脚本）。"""

from pptx.util import Inches

from pp_helpers import (BLUE, GRAY, INK, LIGHT, NAVY, RED, CENTER, RIGHT, MID, band, by_name, card,
                        clone_placeholder, delete, place, rect, set_layout, set_text, shift_xml, tb,
                        to_front)
from pp_slides_a import ph

MC = "{http://schemas.openxmlformats.org/markup-compatibility/2006}"


def s_osi_amr(s):
    """原第 10 页：四张卡片讲清 OSI 进入 AMR 要改的四件事。"""
    set_text(s.shapes.title, ["OSI 适配块结构动态 AMR"])
    core = by_name(s, "文本框 26")
    set_text(core, ["核心：按 Fab 局部坐标与本层 phase 计算存储地址"], size=20)
    place(core, 0.56, 0.98, w=12.2, h=0.45, keep_aspect=False)
    for name in ("文本框 22", "文本框 18", "TextBox 7", "文本框 6"):
        delete(by_name(s, name))
    for sh in [sh for sh in s.shapes if sh.name == "文本占位符 2"]:
        delete(sh)
    # ① Fab 局部环形地址：左侧公式组，右侧要点
    rect(s, 0.56, 1.5, 12.2, 2.25, fill=LIGHT)
    rect(s, 0.56, 1.5, 0.07, 2.25, fill=BLUE)
    place(by_name(s, "组合 5"), 0.75, 1.58)
    tb(s, 6.85, 1.58, 5.8, 0.42, ["① Fab 局部环形地址"], size=17, bold=True, color=BLUE)
    for alt in s.shapes._spTree:
        if alt.tag == MC + "AlternateContent":
            shift_xml(alt, 6.85 - 9.6236, 2.05 - 2.8479)
    tb(s, 6.85, 2.52, 5.8, 1.15, ["环形区域 = 该 Fab 的 valid + 2 层 ghost，越界即回绕。",
                                  "GPU 访存不跨 Fab、不跨 rank；各 Fab 长度可以不同。"], size=14)
    # ② 分层 phase
    card(s, 0.56, 3.92, 3.9, 2.8, "② 分层 phase",
         ["每层独立计数，随本层子步推进；一个粗步内："])
    for i, (lv, ph_) in enumerate((("Rectangle 8", "Rectangle 9"), ("Rectangle 10", "Rectangle 11"),
                                   ("Rectangle 12", "Rectangle 13"))):
        place(by_name(s, lv), 0.8, 5.0 + 0.55 * i, w=0.8, h=0.46, keep_aspect=False)
        place(by_name(s, ph_), 1.65, 5.0 + 0.55 * i, w=1.3, h=0.46, keep_aspect=False)
    tb(s, 3.0, 5.0, 1.4, 1.56, ["1 : 2 : 4"], size=22, bold=True, color=BLUE, align=CENTER, anchor=MID)
    card(s, 4.71, 3.92, 3.9, 2.8, "③ 插值 · 平均 · 通信",
         ["ghost 副本按逻辑坐标同步：发送端按自身 phase 解码，接收端按自己的 Fab 与 phase 重新编码。",
          "粗细传输直接在单数组布局上寻址，不构造整层 canonical 副本。"], gap=6)
    card(s, 8.86, 3.92, 3.9, 2.8, "④ 动态重网格",
         ["布局不变：保留原存储与 phase。",
          "布局变化：旧 valid 按逻辑坐标迁到新布局的 phase 0。",
          "新增区域：由粗层稀疏插值直接写入。"], gap=6)
    # 新加的卡片底色在最上层，把原公式组、L0–L2 方块和小公式提到前面
    alts = [el for el in s.shapes._spTree if el.tag == MC + "AlternateContent"]
    to_front(s, by_name(s, "组合 5"), *alts,
             *[by_name(s, f"Rectangle {i}") for i in range(8, 14)])
    set_text(ph(s, 14), ["原 OSI 只在规则单层网格上验证 [1]；本文将其扩展到多层、多 Fab、动态重网格与 MPI 并行。"],
             size=11, color=GRAY)


def kpi(s, x, y, w, h, value, label, sub, color=BLUE):
    rect(s, x, y, w, h, fill=LIGHT)
    rect(s, x, y, w, 0.06, fill=color)
    tb(s, x, y + 0.12, w, 0.55, [value], size=26, bold=True, color=color, align=CENTER)
    tb(s, x, y + 0.66, w, 0.28, [label], size=13, bold=True, color=INK, align=CENTER)
    tb(s, x, y + 0.9, w, 0.28, [sub], size=12, color=GRAY, align=CENTER)


def s_perf(s):
    """原第 11 页：修正图片拉伸和卡片数字，补充读图要点与口径。"""
    set_text(s.shapes.title, ["OSI 与 A–B：动态 AMR 全程对比"])
    keep = {"图片 10"}
    for sh in [sh for sh in s.shapes if not sh.is_placeholder and sh.name not in keep]:
        delete(sh)
    tb(s, 0.56, 0.98, 12.2, 0.4, ["单 A100 · FP32 D3Q27 · Re=1000 · 128³ 根网格 + 2 层动态加密 · "
                                  "从静止算到 t=1000（128000 粗步）"], size=13, color=GRAY)
    for i, args in enumerate((("−19.6%", "推进累计耗时", "4334 → 3483 s", RED),
                              ("+25.6%", "吞吐 MLUPS", "1520 → 1909", BLUE),
                              ("1586 → 8 s", "迁移阶段", "迁移并入碰撞", BLUE),
                              ("−41.5%", "主 Arena 使用高水位", "8635 → 5049 MiB", BLUE))):
        kpi(s, 0.56 + i * 3.1, 1.45, 2.9, 1.2, *args[:3], color=args[3])
    place(by_name(s, "图片 10"), 0.56, 2.95, w=7.7, h=7.7 / 2.407, keep_aspect=False)
    card(s, 8.45, 2.95, 4.31, 2.2, "怎么读这张图",
         ["迁移：1586 → 8 s，不再有独立 kernel",
          "碰撞：1537 → 2346 s，承担原迁移读写",
          "重构：274 → 150 s；其余分项基本持平",
          [("净效果：推进 4334 → 3483 s", True, RED)]], bsize=15, gap=6)
    band(s, 8.45, 5.27, 4.31, 0.93, [[("迁移省下", False, None), ("约 1578 s", True, RED)],
                                     [("碰撞只多", False, None), ("约 809 s", True, None)]], size=15)
    set_text(ph(s, 14), ["A–B：job625774+625855（t=250 处重启一次）；OSI：job624717；同一冻结程序。"
                         "两者网格演化与重启次数不同，差异不能全部归因于单/双数组。"], size=10.5, color=GRAY)


def step(s, x, y, w, h, title, gain, desc, color=BLUE):
    rect(s, x, y, w, h, fill=LIGHT)
    rect(s, x, y, 0.07, h, fill=color)
    tb(s, x + 0.17, y + 0.05, w - 1.5, 0.4, [title], size=15, bold=True, color=NAVY)
    tb(s, x + w - 1.35, y + 0.03, 1.25, 0.42, [gain], size=18, bold=True, color=color, align=RIGHT)
    tb(s, x + 0.17, y + 0.46, w - 0.27, 0.44, [desc], size=12, color=GRAY)


def s_ladder(s, src11, layout):
    """原第 12 页：去掉与阶梯图重叠的旧表，改用统一版式，逐项写阶梯图的增量。"""
    for sh in [sh for sh in s.shapes if sh.name != "optimization_ladder.svg"]:
        delete(sh)
    set_layout(s, layout)
    clone_placeholder(src11, s, ph_type="TITLE")
    s.shapes.title.text = "累加优化：单 GPU 吞吐 2.15 倍"
    clone_placeholder(src11, s, ph_type="SLIDE_NUMBER")
    clone_placeholder(src11, s, idx=14)
    tb(s, 0.56, 0.98, 12.2, 0.4, ["从 C 阈值 t=1000 检查点恢复，每组推进 1000 粗步（含重网格）；"
                                  "优化按顺序累加，OSI 放在最后"], size=13, color=GRAY)
    place(by_name(s, "optimization_ladder.svg"), 0.56, 1.5, w=7.4)
    for i, args in enumerate((("+ 平均与插值批量", "+32.5%", "多个盒合并为一次 kernel 启动"),
                              ("+ 边界批量薄面", "+5.5%", "物理边界按薄面批量发射，直接构面"),
                              ("+ 碰撞 256→128 线程", "+5.7%", "调整碰撞核线程块大小"),
                              ("+ OSI（含坐标复用）", "+45.2%", "单数组原地读写，迁移并入碰撞；坐标复用依赖环形地址"))):
        step(s, 8.15, 1.5 + 1.04 * i, 4.61, 0.92, *args, color=RED if i == 3 else BLUE)
    band(s, 0.56, 5.82, 12.2, 0.85, [("1132 → 2430 MLUPS：前三项工程优化合计 +47.8%，"
                                      "OSI 在其基础上再 +45.2%，是累加中最大的一项。", 15, True, INK)])
    set_text(ph(s, 14), ["job626501，单 A100；平均 MLUPS = 累计有效更新量 / 累计 perf.total；"
                         "收益依赖累加顺序，不可单独相加；各组末态逐值一致。"], size=10.5, color=GRAY)
