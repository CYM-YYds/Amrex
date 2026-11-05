"""背景、现有基础、LBM 迁移、OSI 原理四页（AI 临时脚本）。"""

from pp_helpers import (BLUE, GRAY, NAVY, RED, CENTER, band, by_name, card, clone_placeholder,
                        copy_picture, delete, place, row_card, set_layout, set_text, tb)


def ph(slide, idx):
    for p in slide.placeholders:
        if p.placeholder_format.idx == idx:
            return p
    raise KeyError(idx)


def s_background(s):
    """原第 19 页：以 LBM/AMR/GPU 为主线的研究背景。"""
    delete(by_name(s, "文本框 5"), by_name(s, "文本框 21"), by_name(s, "文本框 22"))
    place(by_name(s, "Picture 3"), 0.6, 1.05, w=5.6)
    tb(s, 0.6, 4.2, 5.7, 0.75, [[("LBM", True, BLUE),
       ("：介观方法，碰撞在格点本地完成，迁移只与相邻格点交换数据，天然适合 GPU 并行。", False, None)]],
       size=16)
    tb(s, 0.6, 5.0, 5.7, 0.8, [[("GPU 瓶颈", True, RED),
       ("：网格增大后，分布函数的显存占用和访存量随之增长；D3Q27 单精度每格点每步至少读写 216 B。",
        False, None)]], size=16)
    place(by_name(s, "图片 24"), 7.5, 1.05, w=4.6)
    tb(s, 6.85, 3.66, 5.9, 0.62, [[("多尺度", True, BLUE),
       ("：边界层、剪切层与局部涡结构尺度差异大，只需在局部加密，即块结构自适应网格（AMR）。",
        False, None)]], size=16)
    place(by_name(s, "图片 26"), 7.15, 4.36, w=5.3)
    band(s, 0.56, 5.93, 12.2, 0.72,
         [("核心问题：在动态 AMR 中，如何降低分布函数的存储与每步数据搬运？", 18, True, BLUE)])


def s_foundation(s):
    """原第 4 页：现有基础；把“还有优化空间”改成具体瓶颈。"""
    t = by_name(s, "文本框 8")
    set_text(t, ["AMReX 块结构动态 AMR-IB-LBM GPU 求解器 [1]"], size=18, color=BLUE, bold=True)
    place(t, 6.9, 1.12, w=5.86, h=0.5, keep_aspect=False)
    place(by_name(s, "组合 49"), 7.9, 1.72)
    delete(by_name(s, "文本框 48"))
    band(s, 6.9, 5.68, 5.86, 1.02, [[("A–B 双数组推进", True, RED),
         ("：每层常驻两套分布函数，迁移单独成 kernel；显存与访存开销随网格规模和 AMR 层数放大。",
          False, None)]], size=14)


def s_lbm(s, ab_gif):
    """原第 8 页：只讲 LBM 迁移与两套网格的代价，OSI 内容移到下一页。"""
    set_text(s.shapes.title, ["LBM 迁移与两套网格（A–B）"])
    place(by_name(s, "文本占位符 2"), h=0.55, keep_aspect=False)
    place(by_name(s, "对象 6"), y=1.7)
    place(by_name(s, "组合 7"), y=2.2)
    delete(by_name(s, "组合 11"), by_name(s, "图片 17"), by_name(s, "文本框 19"),
           by_name(s, "文本框 20"), by_name(s, "文本框 21"), by_name(s, "文本框 24"))
    gif = s.shapes.add_picture(ab_gif, 0.0, 0.0)
    gif.crop_top, gif.crop_bottom = 55 / 792, 62 / 792
    ar = 1320 / (792 - 55 - 62)
    place(gif, 0.62, 3.85, w=2.9 * ar, h=2.9, keep_aspect=False)
    right = by_name(s, "文本占位符 4")
    set_text(right, ["两套网格的代价"])
    place(right, 6.85, 1.11, w=5.9, h=0.55, keep_aspect=False)
    card(s, 6.85, 1.7, 5.9, 1.18, "存储 ×2",
         ["f_old / f_new 两套分布函数同时常驻显存；D3Q27 单精度每格点 216 B（单数组 108 B）。"])
    card(s, 6.85, 2.98, 5.9, 1.18, "访存 ×2",
         ["迁移是独立 kernel，每步多一轮全局内存读写：每格点约 432 B（单数组 216 B）。"])
    card(s, 6.85, 4.26, 5.9, 1.18, "AMR 下放大",
         ["每层每个子步都要迁移与交换；插值、平均与 ghost 通信也要维护两套状态。"])
    band(s, 6.85, 5.56, 5.9, 1.1, [[("实测", True, RED),
         ("：C 阈值动态 AMR 全程，A–B 迁移耗时 1586 s，占推进总耗时 36.6%。", False, None)]], size=15)
    set_text(by_name(s, "文本占位符 3"),
             ["实测：job625774+625855（A–B，C 阈值，单 A100，FP32 D3Q27，Re=1000，128000 粗步）；"
              "216/432 B 为每格点理论最低读写量。"], size=10.5, color=GRAY)


def s_osi(s, src8, src11, layout):
    """原第 9 页：OSI 原理；改用统一版式，接收第 8 页的读取-碰撞-写回示意。"""
    set_layout(s, layout)
    delete(by_name(s, "标题 3"), by_name(s, "矩形 22"), by_name(s, "图片 2"))
    for sh in [sh for sh in s.shapes if sh.name == "标题 1"]:
        delete(sh)
    clone_placeholder(src11, s, ph_type="TITLE")
    s.shapes.title.text = "单步索引算法（OSI）"
    clone_placeholder(src11, s, ph_type="SLIDE_NUMBER")
    clone_placeholder(src11, s, idx=14)
    set_text(ph(s, 14), ["[1] Ma et al., Comput. Phys. Commun. 2023, 283:108603"])
    place(by_name(s, "osi_principle.gif"), 0.6, 1.05, w=4.8)
    strip = copy_picture(by_name(src8, "图片 17"), s)
    place(strip, 5.85, 1.1, w=6.9)
    for cx, label in ((6.90, "① 读取分布函数"), (9.44, "② 本地碰撞"), (11.89, "③ 写回同一地址")):
        tb(s, cx - 0.9, 3.12, 1.8, 0.36, [label], size=14, bold=True, color=NAVY, align=CENTER)
    row_card(s, 5.85, 3.6, 6.9, 0.78, "原地读写",
             ["碰撞时按地址 A_q(x, p) 读取，碰撞后写回同一地址。"], tw=1.5)
    row_card(s, 5.85, 4.48, 6.9, 0.78, "隐式迁移",
             ["A_q(x, p) = (x − p·e_q) mod L；phase 加 1 即完成迁移，无独立迁移 kernel。"], tw=1.5)
    row_card(s, 5.85, 5.36, 6.9, 0.78, "单数组",
             ["只存一套分布函数；每格点每步只读一次、写一次。"], tw=1.5)
    tb(s, 5.85, 6.24, 6.9, 0.5, [[("代价", True, RED),
       ("：地址依赖数组尺寸和时间步；在 AMR 中需要重新定义（下一页）。", False, None)]], size=13)
