"""三维方腔、总结、目录页（AI 临时脚本）。"""

import copy

from pptx.util import Inches, Pt

from pp_helpers import (BLUE, GRAY, INK, NAVY, RED, band, by_name, card, delete, place, set_text, tb)
from pp_slides_a import ph

TOC = ["研究背景及现状", "研究方法", "性能测试与算例验证", "总结和展望"]


def _cell(cell, text, size, bold):
    p = cell.text_frame.paragraphs[0]
    for extra in list(cell.text_frame.paragraphs)[1:]:
        extra._p.getparent().remove(extra._p)
    for r in list(p.runs)[1:]:
        r._r.getparent().remove(r._r)
    r = p.runs[0] if p.runs else p.add_run()
    r.text = text
    r.font.size = Pt(size)
    r.font.bold = bold


def s_cavity(s):
    """原第 15 页：清掉复制残留，重排四个面板，修复零行高表格。"""
    set_text(s.shapes.title, ["三维顶盖驱动方腔流：精度与网格代价"])
    for name in ("直接连接符 25", "直接连接符 26", "直接箭头连接符 27", "文本框 28", "组合 29",
                 "文本框 30", "lid-Re3200"):
        delete(by_name(s, name))
    timing = s._element.find("{http://schemas.openxmlformats.org/presentationml/2006/main}timing")
    if timing is not None:
        timing.getparent().remove(timing)        # 只控制已删除视频的播放
    place(by_name(s, "图片 5"), 0.56, 1.05, w=3.0)
    dx = 3.66 - 3.48
    for sh in s.shapes:
        if sh.name == "文本框 45" or sh.name.startswith(("Shape ", "Text ")):
            sh.left = sh.left + Inches(dx)
    place(by_name(s, "图片 7"), 6.98, 1.0, h=2.95)
    place(by_name(s, "timing_pie.svg"), 9.88, 1.48, w=2.88)
    place(by_name(s, "文本框 13"), 9.88, 1.06, w=2.88, h=0.36, keep_aspect=False)
    tb(s, 9.88, 3.4, 2.88, 0.8, ["碰撞约占 OSI 推进耗时 2/3；插值、平均、重构与通信合计约 20%"],
       size=12, color=GRAY)
    table = by_name(s, "表格 3")
    rows = [("指标", "均匀网格 512³", "自适应网格（C 阈值）", "对比"),
            ("推进累计耗时", "17173 s", "3483 s", "约为均匀网格的 1/4.9"),
            ("网格单元数", "134,217,728", "17,313,792", "减少 87.1%"),
            ("主 Arena 使用高水位", "37,166 MiB", "5,049 MiB", "减少 86.4%")]
    widths = (2.9, 2.75, 3.1, 3.45)
    tbl = table.table
    for j, w in enumerate(widths):
        tbl.columns[j].width = Inches(w)
    for i, row in enumerate(rows):
        tbl.rows[i].height = Inches(0.4)
        for j, txt in enumerate(row):
            _cell(tbl.cell(i, j), txt, 14, i == 0 or j == 3)
    place(table, 0.56, 4.25, w=sum(widths), h=1.6, keep_aspect=False)
    band(s, 0.56, 5.98, 12.2, 0.72, [("相同最细分辨率下，AMR 的中心线与均匀网格、参考解吻合；"
                                      "推进耗时约 1/5，显存约 1/7。", 15, True, INK)], accent=BLUE)
    set_text(ph(s, 14), ["均匀：job623012（512000 步，分 40 段重启）；AMR：job624717（128000 粗步）；"
                         "耗时为 perf.total 累加，不含初始化与输出。程序版本不同，不是严格等精度加速比；"
                         "参考解 Cortes & Miller。"], size=10, color=GRAY)


def s_summary(s):
    """原第 17 页：总结与展望改为两张卡片，替换师兄原文。"""
    for sh in [sh for sh in s.shapes if sh.name == "文本占位符 5"]:
        delete(sh)
    card(s, 0.56, 1.1, 6.6, 4.65, "工作总结",
         ["① 把单步索引（OSI）扩展到块结构动态 AMR：Fab 局部环形地址、分层 phase、"
          "按逻辑坐标的插值/通信与重网格迁移。",
          "② C 阈值动态 AMR 全程：推进耗时 −19.6%，主 Arena 高水位 −41.5%，迁移 1586 s → 8 s。",
          "③ 叠加批量传输、边界与碰撞核优化，单 GPU 吞吐达到基线的 2.15 倍。",
          "④ t=1000 方腔中心线与均匀 512³、参考解吻合，网格单元减少 87%。"], tsize=22, bsize=17, gap=12)
    card(s, 7.35, 1.1, 5.41, 4.65, "工作展望",
         ["① OSI 与浸没边界法耦合：静止 IBM 已接入 A–B/OSI，下一步做圆柱、圆球绕流的物理验证。",
          "② 碰撞仍约占耗时 2/3，继续降低碰撞核访存开销。",
          "③ 多 GPU/多节点扩展：通信流水与 GPU-aware MPI。"], tsize=22, bsize=17, gap=12)
    band(s, 0.56, 5.95, 12.2, 0.75, [("OSI 不改物理模型，只降低分布函数的存储与数据搬运；"
                                      "这一收益在动态 AMR 中同样成立。", 16, True, BLUE)], accent=BLUE)


def s_toc(s, active):
    """目录页：保留原段落格式（当前项默认色、其余灰色），改成四项。"""
    body = [p for p in s.placeholders if p.placeholder_format.idx == 13][0]
    paras = body.text_frame._txBody.findall("{http://schemas.openxmlformats.org/drawingml/2006/main}p")
    plain = [p for p in body.text_frame.paragraphs if p.runs and p.runs[0]._r.find(
        "{http://schemas.openxmlformats.org/drawingml/2006/main}rPr/"
        "{http://schemas.openxmlformats.org/drawingml/2006/main}solidFill") is None][0]._p
    gray = [p for p in body.text_frame.paragraphs if p._p is not plain][0]._p
    parent = plain.getparent()
    for p in paras:
        parent.remove(p)
    for i, name in enumerate(TOC):
        p = copy.deepcopy(plain if i == active else gray)
        p.findall(".//{http://schemas.openxmlformats.org/drawingml/2006/main}t")[0].text = name
        parent.append(p)
