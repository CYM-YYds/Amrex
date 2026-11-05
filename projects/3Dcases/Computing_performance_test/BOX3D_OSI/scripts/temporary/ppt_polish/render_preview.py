"""粗略渲染 pptx 为 PNG 预览（AI 临时脚本，任务结束后可删除）。

服务器上没有 LibreOffice，只能按形状坐标画出图片、色块和文字，用来检查排版：
重叠、越界、留白和字号层级。不还原阴影、渐变、动画、EMF/WMF 与公式对象。
用法：.venv/bin/python render_preview.py <pptx> <输出目录> [页码...]
"""

from __future__ import annotations

import io
import os
import sys

from lxml import etree
from PIL import Image, ImageDraw, ImageFont
from pptx import Presentation
from pptx.enum.shapes import MSO_SHAPE_TYPE
from pptx.oxml.ns import qn

DPI = 110
E = 914400
FONT = "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc"
FONT_B = "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Bold.ttc"
THEME = {"tx1": "000000", "dk1": "000000", "bg1": "FFFFFF", "lt1": "FFFFFF",
         "tx2": "44546A", "dk2": "44546A", "bg2": "E7E6E6", "lt2": "E7E6E6",
         "accent1": "4472C4", "accent2": "ED7D31", "accent3": "A5A5A5",
         "accent4": "FFC000", "accent5": "5B9BD5", "accent6": "70AD47"}
NS = {"a": "http://schemas.openxmlformats.org/drawingml/2006/main",
      "p": "http://schemas.openxmlformats.org/presentationml/2006/main"}
_fonts: dict = {}


def font(px, bold=False):
    key = (max(6, int(px)), bold)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(FONT_B if bold else FONT, key[0])
    return _fonts[key]


def color_of(el):
    """从 solidFill 节点取颜色。"""
    if el is None:
        return None
    s = el.find("a:srgbClr", NS)
    if s is not None:
        return "#" + s.get("val")
    s = el.find("a:schemeClr", NS)
    if s is not None:
        return "#" + THEME.get(s.get("val"), "888888")
    s = el.find("a:sysClr", NS)
    if s is not None:
        return "#" + s.get("lastClr", "000000")
    return None


def shape_fill(sp):
    sppr = sp.find(".//p:spPr", NS)
    if sppr is None:
        return None
    if sppr.find("a:noFill", NS) is not None:
        return None
    f = sppr.find("a:solidFill", NS)
    if f is not None:
        return color_of(f)
    g = sppr.find("a:gradFill", NS)
    if g is not None:
        gs = g.find(".//a:gs", NS)
        return color_of(gs)
    st = sp.find("p:style/a:fillRef", NS)
    if st is not None and st.get("idx") not in (None, "0"):
        return color_of(st)
    return None


def shape_line(sp):
    sppr = sp.find(".//p:spPr", NS)
    if sppr is None:
        return None
    ln = sppr.find("a:ln", NS)
    if ln is not None:
        if ln.find("a:noFill", NS) is not None:
            return None
        f = ln.find("a:solidFill", NS)
        if f is not None:
            return color_of(f)
    st = sp.find("p:style/a:lnRef", NS)
    if st is not None and st.get("idx") not in (None, "0"):
        return color_of(st)
    return None


def inherited_style(sh):
    """返回 (字号pt, 颜色, 加粗)；占位符按版式 lstStyle 取，其余用默认。"""
    pt, col, b = 18, "#000000", False
    if not sh.is_placeholder:
        return pt, col, b
    try:
        lay = sh.part.slide.slide_layout
    except Exception:
        return pt, col, b
    pf = sh.placeholder_format
    is_title = "TITLE" in str(pf.type)
    if is_title:
        pt, b = 44, False
    else:
        pt = 28
    for lsh in lay.placeholders:
        lpf = lsh.placeholder_format
        if (is_title and "TITLE" in str(lpf.type)) or (not is_title and lpf.idx == pf.idx):
            lv = lsh._element.find(".//a:lstStyle/a:lvl1pPr/a:defRPr", NS)
            if lv is not None:
                if lv.get("sz"):
                    pt = int(lv.get("sz")) / 100
                if lv.get("b") == "1":
                    b = True
                c = color_of(lv.find("a:solidFill", NS))
                if c:
                    col = c
            break
    return pt, col, b


def draw_text(d, tf, box, default_pt, default_color="#000000", default_bold=False):
    """逐 run 取字号/颜色/加粗；按段落对齐、文本框垂直锚点排版。返回文字底边。"""
    x0, y0, x1, y1 = box
    width = max(10, x1 - x0 - 6)
    lines = []                      # (片段列表[(文字, 字体, 颜色)], 行高, 对齐)
    for para in tf.paragraphs:
        runs = [r for r in para.runs if r.text]
        if not runs:
            lines.append(([], default_pt * DPI / 72 * 0.6, ""))
            continue
        al = str(para.alignment or "")
        chars = []
        for r in runs:
            pt = r.font.size.pt if r.font.size else default_pt
            bold = default_bold if r.font.bold is None else bool(r.font.bold)
            col = default_color
            rpr = r._r.find("a:rPr", NS)
            if rpr is not None:
                c = color_of(rpr.find("a:solidFill", NS))
                if c:
                    col = c
            f = font(pt * DPI / 72, bold)
            chars += [(ch, f, col, pt) for ch in r.text]
        lh = max(c[3] for c in chars) * DPI / 72 * 1.25
        cur, cw = [], 0.0
        for ch, f, col, pt in chars:
            w = d.textlength(ch, font=f)
            if cw + w > width and cur:
                lines.append((cur, lh, al))
                cur, cw = [], 0.0
            cur.append((ch, f, col))
            cw += w
        if cur:
            lines.append((cur, lh, al))
    total = sum(l[1] for l in lines)
    anchor = str(tf.vertical_anchor or "")
    y = y0 + 3
    if "MIDDLE" in anchor:
        y = y0 + (y1 - y0 - total) / 2
    elif "BOTTOM" in anchor:
        y = y1 - 3 - total
    for segs, lh, al in lines:
        tw = sum(d.textlength(ch, font=f) for ch, f, _ in segs)
        xx = x0 + 3
        if "CENTER" in al:
            xx = x0 + (x1 - x0 - tw) / 2
        elif "RIGHT" in al:
            xx = x1 - 3 - tw
        for ch, f, col in segs:
            d.text((xx, y), ch, font=f, fill=col)
            xx += d.textlength(ch, font=f)
        y += lh
    return y


def svg_to_png(blob, width_px):
    import subprocess
    r = subprocess.run(["rsvg-convert", "-w", str(max(16, int(width_px)))], input=blob,
                       capture_output=True)
    return r.stdout if r.returncode == 0 else None


def paste_picture(img, blob, box, crop=(0, 0, 0, 0)):
    try:
        im = Image.open(io.BytesIO(blob))
        im.seek(0)
        im = im.convert("RGBA")
    except Exception:
        return False
    w, h = im.size
    l, t, r, b = crop
    im = im.crop((int(w * l), int(h * t), int(w * (1 - r)), int(h * (1 - b))))
    bw, bh = max(1, box[2] - box[0]), max(1, box[3] - box[1])
    im = im.resize((int(bw), int(bh)))
    img.alpha_composite(im, (int(box[0]), int(box[1])))
    return True


MC = "{http://schemas.openxmlformats.org/markup-compatibility/2006}"


def expand(shapes):
    """按文档顺序列出形状；mc:AlternateContent（公式等）取 Fallback 中的预览形状。"""
    from pptx.shapes.shapetree import BaseShapeFactory
    out = []
    tree = getattr(shapes, "_spTree", None)
    if tree is None:
        return list(shapes)
    known = {id(sh._element): sh for sh in shapes}
    for child in tree:
        if id(child) in known:
            out.append(known[id(child)])
        elif child.tag == MC + "AlternateContent":
            fb = child.find(MC + "Fallback")
            for c in (fb if fb is not None else []):
                try:
                    out.append(BaseShapeFactory(c, shapes))
                except Exception:
                    pass
    return out


def render_shapes(img, d, shapes, tx=lambda v: v, ty=lambda v: v, sx=1.0, sy=1.0,
                  is_layout=False):
    for sh in expand(shapes):
        if is_layout and sh.is_placeholder:
            continue
        if sh.left is None:
            continue
        x0 = tx(sh.left) / E * DPI
        y0 = ty(sh.top) / E * DPI
        x1 = x0 + sh.width * sx / E * DPI
        y1 = y0 + sh.height * sy / E * DPI
        box = (x0, y0, x1, y1)
        st = sh.shape_type
        if st == MSO_SHAPE_TYPE.GROUP:
            xfrm = sh._element.find("p:grpSpPr/a:xfrm", NS)
            off = xfrm.find("a:off", NS); ext = xfrm.find("a:ext", NS)
            choff = xfrm.find("a:chOff", NS); chext = xfrm.find("a:chExt", NS)
            gx, gy = int(off.get("x")), int(off.get("y"))
            gw, gh = int(ext.get("cx")), int(ext.get("cy"))
            cx, cy = int(choff.get("x")), int(choff.get("y"))
            cw, chh = max(1, int(chext.get("cx"))), max(1, int(chext.get("cy")))
            ksx, ksy = gw / cw, gh / chh
            ptx, pty = tx, ty
            render_shapes(img, d, sh.shapes,
                          tx=lambda v, ptx=ptx, gx=gx, cx=cx, k=ksx: ptx(gx + (v - cx) * k),
                          ty=lambda v, pty=pty, gy=gy, cy=cy, k=ksy: pty(gy + (v - cy) * k),
                          sx=sx * ksx, sy=sy * ksy)
            continue
        if st == MSO_SHAPE_TYPE.PICTURE or (st == MSO_SHAPE_TYPE.PLACEHOLDER and hasattr(sh, "image")):
            crop = (sh.crop_left, sh.crop_top, sh.crop_right, sh.crop_bottom)
            ok = False
            svg = sh._element.find(".//{http://schemas.microsoft.com/office/drawing/2016/SVG/main}svgBlip")
            if svg is not None:
                try:
                    blob = sh.part.related_part(svg.get(qn("r:embed"))).blob
                    png = svg_to_png(blob, (x1 - x0) / max(0.05, 1 - crop[0] - crop[2]))
                    ok = bool(png) and paste_picture(img, png, box, crop)
                except Exception:
                    ok = False
            if not ok:
                try:
                    ok = paste_picture(img, sh.image.blob, box, crop)
                except Exception:
                    ok = False
            if not ok:
                d.rectangle(box, outline="#999999", fill="#DDDDDD")
                d.text((x0 + 4, y0 + 4), "EMF/WMF", font=font(10), fill="#555555")
            continue
        if st == MSO_SHAPE_TYPE.MEDIA:
            blip = sh._element.find(".//a:blip", NS)
            done = False
            if blip is not None:
                rid = blip.get(qn("r:embed"))
                try:
                    part = sh.part.related_part(rid)
                    done = paste_picture(img, part.blob, box)
                except Exception:
                    pass
            if not done:
                d.rectangle(box, outline="#333", fill="#BBBBBB")
            d.text((x0 + 4, y0 + 4), "▶ 视频", font=font(11, True), fill="#C00000")
            continue
        if st == MSO_SHAPE_TYPE.EMBEDDED_OLE_OBJECT:
            blip = sh._element.find(".//a:blip", NS)
            done = False
            if blip is not None:
                rid = blip.get(qn("r:embed"))
                try:
                    done = paste_picture(img, sh.part.related_part(rid).blob, box)
                except Exception:
                    pass
            if not done:
                d.rectangle(box, outline="#7030A0", fill="#F2E6FA")
                d.text((x0 + 4, y0 + 4), "公式对象", font=font(10), fill="#7030A0")
            continue
        if getattr(sh, "has_table", False) and sh.has_table:
            tb = sh.table
            ys = [y0]
            for r in tb.rows:
                ys.append(ys[-1] + r.height * sy / E * DPI)
            xs = [x0]
            for c in tb.columns:
                xs.append(xs[-1] + c.width * sx / E * DPI)
            for ri, r in enumerate(tb.rows):
                for ci, c in enumerate(r.cells):
                    cb = (xs[ci], ys[ri], xs[ci + 1], ys[ri + 1])
                    fill = "#2F5597" if ri == 0 else ("#F2F2F2" if ri % 2 else "#FFFFFF")
                    d.rectangle(cb, outline="#AAAAAA", fill=fill)
                    draw_text(d, c.text_frame, cb, 12, "#FFFFFF" if ri == 0 else "#000000")
            continue
        bf = sh._element.find("p:spPr/a:blipFill", NS)
        if bf is not None:
            blip = bf.find("a:blip", NS)
            try:
                paste_picture(img, sh.part.related_part(blip.get(qn("r:embed"))).blob, box)
                continue
            except Exception:
                pass
        fill = shape_fill(sh._element)
        line = shape_line(sh._element)
        if st == MSO_SHAPE_TYPE.LINE or sh._element.tag.endswith("cxnSp"):
            d.line((x0, y0, x1, y1), fill=line or "#444444", width=2)
            continue
        if fill or line:
            d.rectangle(box, fill=fill, outline=line)
        if sh.has_text_frame and sh.text_frame.text.strip():
            default_pt, dc, db = inherited_style(sh)
            if fill and fill.upper() in ("#2F5597", "#153A5C", "#1F4E79", "#002060",
                                         "#4472C4", "#2A9D8F", "#045FA4", "#16365C"):
                dc = "#FFFFFF"
            bottom = draw_text(d, sh.text_frame, box, default_pt, dc, db)
            if bottom > y1 + 4 and not (fill or line):
                d.rectangle((x0, y1, x1, bottom), outline="#FF00FF")   # 文字溢出框外


def render(path, outdir, pages=None):
    prs = Presentation(path)
    W = int(prs.slide_width / E * DPI)
    H = int(prs.slide_height / E * DPI)
    os.makedirs(outdir, exist_ok=True)
    for i, s in enumerate(prs.slides, 1):
        if pages and i not in pages:
            continue
        img = Image.new("RGBA", (W, H), "white")
        d = ImageDraw.Draw(img)
        render_shapes(img, d, s.slide_layout.shapes, is_layout=True)
        render_shapes(img, d, s.shapes)

        d.text((W - 60, H - 22), f"#{i}", font=font(14, True), fill="#FF0000")
        img.convert("RGB").save(os.path.join(outdir, f"slide{i:02d}.png"))
    print("rendered", outdir)


if __name__ == "__main__":
    pg = [int(a) for a in sys.argv[3:]] or None
    render(sys.argv[1], sys.argv[2], pg)
