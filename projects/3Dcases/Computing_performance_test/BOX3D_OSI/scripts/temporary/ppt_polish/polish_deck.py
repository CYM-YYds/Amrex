"""在用户 PPT 副本上精修排版与内容，输出新文件，原文件不改（AI 临时脚本，任务结束后可删除）。

用法（仓库根）：.venv/bin/python <本目录>/polish_deck.py
依赖：python-pptx（根 .venv）；assets/ab_principle.gif 复制自
runs/temporary/ab_principle_animation/two_blocks/（data_post_processing/plot_ab_principle.py 生成）。
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pptx import Presentation  # noqa: E402

from pp_notes import BACKUP, NOTES  # noqa: E402
from pp_slides_a import s_background, s_foundation, s_lbm, s_osi  # noqa: E402
from pp_slides_b import s_ladder, s_osi_amr, s_perf  # noqa: E402
from pp_slides_c import s_cavity, s_summary, s_toc  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.abspath(os.path.join(HERE, "..", "..", "..", "docs"))
SRC = os.path.join(DOCS, "自适应网格 LBM 求解器的 GPU 性能优化.pptx")
OUT = os.path.join(DOCS, "自适应网格 LBM 求解器的 GPU 性能优化_精修版.pptx")
AB_GIF = os.path.join(HERE, "assets", "ab_principle.gif")

# 原页码 → 新顺序；备用页放在致谢之后并隐藏
ORDER = [1, 2, 19, 4, 5, 8, 9, 10, 13, 11, 12, 15, 16, 17, 18, 3, 6, 7, 14]
HIDDEN = {3, 6, 7, 14}
TOC_ACTIVE = {2: 0, 5: 1, 13: 2, 16: 3}


def main():
    prs = Presentation(SRC)
    S = {i + 1: s for i, s in enumerate(prs.slides)}
    layout = S[11].slide_layout                       # 3_空白：统一内容页版式
    s_osi(S[9], S[8], S[11], layout)                  # 先复制第 8 页的示意条，再改第 8 页
    s_lbm(S[8], AB_GIF)
    s_background(S[19])
    s_foundation(S[4])
    s_osi_amr(S[10])
    s_perf(S[11])
    s_ladder(S[12], S[11], layout)
    s_cavity(S[15])
    s_summary(S[17])
    for k, active in TOC_ACTIVE.items():
        s_toc(S[k], active)
    for k, s in S.items():
        tf = s.notes_slide.notes_text_frame
        if k in NOTES:
            tf.text = NOTES[k]
        elif k in HIDDEN:
            tf.text = BACKUP + (tf.text or "（无）")
    lst = prs.slides._sldIdLst
    by_part = {prs.part.related_part(e.rId): e for e in lst}
    ids = [by_part[S[k].part] for k in ORDER]
    for e in list(lst):
        lst.remove(e)
    for e in ids:
        lst.append(e)
    for k in HIDDEN:
        S[k]._element.set("show", "0")
    prs.save(OUT)
    print("saved", OUT, len(prs.slides), "slides")


if __name__ == "__main__":
    main()
