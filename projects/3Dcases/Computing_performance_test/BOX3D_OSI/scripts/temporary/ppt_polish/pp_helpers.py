"""PPT 精修共用工具（AI 临时脚本，任务结束后可删除）。

只新增文本框、色块和图片，沿用模板版式、主题字体与配色：
标题蓝 #045FA4、强调红 #C00000（均取自模板版式）。
"""

import copy

from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.opc.constants import RELATIONSHIP_TYPE as RT
from pptx.oxml.ns import qn
from pptx.util import Inches, Pt

BLUE = RGBColor(0x04, 0x5F, 0xA4)
NAVY = RGBColor(0x1F, 0x38, 0x64)
RED = RGBColor(0xC0, 0x00, 0x00)
INK = RGBColor(0x26, 0x26, 0x26)
GRAY = RGBColor(0x59, 0x59, 0x59)
LIGHT = RGBColor(0xED, 0xF3, 0xF9)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
CENTER, LEFT, RIGHT = PP_ALIGN.CENTER, PP_ALIGN.LEFT, PP_ALIGN.RIGHT
MID = MSO_ANCHOR.MIDDLE
A = "{http://schemas.openxmlformats.org/drawingml/2006/main}"


def _run(p, text, size, bold, color):
    r = p.add_run()
    r.text = text
    r.font.size = Pt(size)
    r.font.bold = bold
    r.font.color.rgb = color


def tb(slide, x, y, w, h, paras, size=16, color=INK, bold=False, align=LEFT,
       anchor=MSO_ANCHOR.TOP, spacing=1.1, gap=4):
    """段落可为字符串、(文本, 字号, 加粗, 颜色) 元组，或 [(片段, 加粗, 颜色), ...] 富文本。"""
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = anchor
    tf.margin_left = tf.margin_right = Inches(0.05)
    tf.margin_top = tf.margin_bottom = Inches(0.03)
    for i, spec in enumerate(paras):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        p.line_spacing = spacing
        if i:
            p.space_before = Pt(gap)
        if isinstance(spec, str):
            _run(p, spec, size, bold, color)
        elif isinstance(spec, tuple):
            t, sz, b, c = (list(spec) + [None] * 4)[:4]
            _run(p, t, sz or size, bold if b is None else b, c or color)
        else:
            for t, b, c in spec:
                _run(p, t, size, b, c or color)
    return box


def rect(slide, x, y, w, h, fill=None, line=None, lw=0.75):
    s = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(x), Inches(y), Inches(w), Inches(h))
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
    s.shadow.inherit = False
    return s


def card(slide, x, y, w, h, title, body, accent=BLUE, tsize=17, bsize=14, gap=3):
    """浅底卡片：左侧强调条 + 标题 + 正文段落列表。"""
    rect(slide, x, y, w, h, fill=LIGHT)
    rect(slide, x, y, 0.07, h, fill=accent)
    th = max(0.42, tsize * 1.3 / 72 + 0.08)
    tb(slide, x + 0.18, y + 0.07, w - 0.28, th, [title], size=tsize, bold=True, color=accent)
    if body:
        tb(slide, x + 0.18, y + 0.08 + th, w - 0.28, h - th - 0.14, body, size=bsize, spacing=1.12, gap=gap)


def row_card(slide, x, y, w, h, title, body, tw=1.45, accent=BLUE, tsize=16, bsize=13):
    """横向卡片：左侧标题、右侧说明。"""
    rect(slide, x, y, w, h, fill=LIGHT)
    rect(slide, x, y, 0.07, h, fill=accent)
    tb(slide, x + 0.15, y, tw, h, [title], size=tsize, bold=True, color=accent, anchor=MID)
    tb(slide, x + 0.15 + tw, y, w - tw - 0.25, h, body, size=bsize, anchor=MID, spacing=1.08)


def band(slide, x, y, w, h, paras, accent=RED, size=15):
    rect(slide, x, y, w, h, fill=LIGHT)
    rect(slide, x, y, 0.07, h, fill=accent)
    tb(slide, x + 0.2, y, w - 0.3, h, paras, size=size, anchor=MID, spacing=1.08, gap=2)


def by_name(slide, name):
    for sh in slide.shapes:
        if sh.name == name:
            return sh
    raise KeyError(name)


def delete(*shapes):
    for sh in shapes:
        el = sh._element
        el.getparent().remove(el)


def to_front(slide, *els):
    """把形状（或 mc:AlternateContent 原始元素）移到最上层，避免被新加的底色块盖住。"""
    tree = slide.shapes._spTree
    for el in els:
        el = getattr(el, "_element", el)
        tree.remove(el)
        tree.insert_element_before(el, "p:extLst")


def place(sh, x=None, y=None, w=None, h=None, keep_aspect=True):
    """移动/缩放；只给 w 或 h 时按当前宽高比换算另一边。"""
    ar = sh.width / sh.height
    if w is not None and h is None and keep_aspect:
        h = w / ar
    if h is not None and w is None and keep_aspect:
        w = h * ar
    if x is not None:
        sh.left = Inches(x)
    if y is not None:
        sh.top = Inches(y)
    if w is not None:
        sh.width = Inches(w)
    if h is not None:
        sh.height = Inches(h)


def shift_xml(el, dx, dy):
    """平移 mc:AlternateContent 等原始元素（Choice/Fallback 两份坐标一起改）。"""
    for off in el.iter(A + "off"):
        off.set("x", str(int(off.get("x")) + int(Inches(dx))))
        off.set("y", str(int(off.get("y")) + int(Inches(dy))))


def _next_id(slide):
    ids = [int(e.get("id")) for e in slide.shapes._spTree.iter(qn("p:cNvPr")) if e.get("id")]
    return max(ids + [1]) + 1


def clone_placeholder(src, dst, ph_type=None, idx=None):
    """把 src 页的占位符复制到 dst 页（去掉坐标以继承版式），返回新元素。"""
    for sh in src.shapes:
        if not sh.is_placeholder:
            continue
        pf = sh.placeholder_format
        if (ph_type and ph_type in str(pf.type)) or (idx is not None and pf.idx == idx):
            el = copy.deepcopy(sh._element)
            xfrm = el.find(qn("p:spPr") + "/" + qn("a:xfrm"))
            if xfrm is not None:
                xfrm.getparent().remove(xfrm)
            el.find(".//" + qn("p:cNvPr")).set("id", str(_next_id(dst)))
            dst.shapes._spTree.insert_element_before(el, "p:extLst")
            return el
    raise KeyError((ph_type, idx))


def copy_picture(pic, dst):
    """把图片复制到另一页，图片部件共用（WMF/GIF/SVG 均保持原字节）。"""
    el = copy.deepcopy(pic._element)
    for node in el.iter():
        rid = node.get(qn("r:embed"))
        if rid:
            node.set(qn("r:embed"), dst.part.relate_to(pic.part.related_part(rid), RT.IMAGE))
    el.find(".//" + qn("p:cNvPr")).set("id", str(_next_id(dst)))
    dst.shapes._spTree.insert_element_before(el, "p:extLst")
    return dst.shapes[-1]


def set_layout(slide, layout):
    for rel in slide.part.rels.values():
        if rel.reltype == RT.SLIDE_LAYOUT:
            rel._target = layout.part


def set_text(sh, paras, size=None, color=None, bold=None):
    """改写已有形状文字：保留第一段/首个 run 的格式。"""
    tf = sh.text_frame
    first = tf.paragraphs[0]
    for p in list(tf.paragraphs)[1:]:
        p._p.getparent().remove(p._p)
    for r in list(first.runs)[1:]:
        r._r.getparent().remove(r._r)
    for i, t in enumerate(paras):
        p = first if i == 0 else tf.add_paragraph()
        r = p.runs[0] if (i == 0 and p.runs) else p.add_run()
        r.text = t
        if size:
            r.font.size = Pt(size)
        if color is not None:
            r.font.color.rgb = color
        if bold is not None:
            r.font.bold = bold
