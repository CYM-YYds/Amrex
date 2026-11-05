#!/usr/bin/env python3
"""长期维护的PNG序列裁剪与GIF合成入口；保留逐帧来源与统一调色板。"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('--input', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--prefix', required=True)
parser.add_argument('--crop', type=int, nargs=4, required=True)
parser.add_argument('--frames', type=int, help='选取帧数；省略则取起点之后全部帧')
parser.add_argument('--start-frame', type=int, default=0, help='从第几个源帧开始，0为第一帧')
parser.add_argument('--duration', type=int, default=500)
parser.add_argument('--keep-colors', nargs='+', default=[], metavar='RRGGBB',
                    help='必须原样保留的颜色（如细线示意图的标注色）；省略则保持原调色板行为')
args = parser.parse_args()
try:
    keep = [tuple(int(c.lstrip('#')[i:i + 2], 16) for i in (0, 2, 4)) for c in args.keep_colors]
except ValueError:
    parser.error('--keep-colors 须为 RRGGBB 十六进制颜色')
if len(keep) > 64 or any(len(c.lstrip('#')) != 6 for c in args.keep_colors):
    parser.error('--keep-colors 须为 RRGGBB 十六进制颜色，最多 64 个')
files = sorted(args.input.glob('*.png'))
if args.start_frame < 0 or (args.frames is not None and args.frames <= 0):
    parser.error('start-frame须非负，frames须为正整数')
if args.duration <= 0 or args.duration % 10:
    parser.error('duration须为正的10毫秒整数倍（GIF时间精度）')
stop = len(files) if args.frames is None else args.start_frame + args.frames
if not args.start_frame < stop <= len(files):
    parser.error('请求帧段超出输入PNG序列范围')
for i, source in enumerate(files):
    if not source.stem.endswith(f'.{i:04d}'):
        parser.error('输入须为从.0000开始连续编号的PNG序列')
files = files[args.start_frame:stop]
if args.output.exists() or args.output.with_suffix('.json').exists():
    parser.error('输出GIF或JSON已存在，请选择新输出名称')
for i in range(len(files)):
    if (args.output.parent / 'frames' / f'{args.prefix}.{i:04d}.png').exists():
        parser.error('裁剪PNG已存在，请选择新目录或prefix')
args.output.parent.mkdir(parents=True, exist_ok=True)
cropped = args.output.parent / 'frames'
cropped.mkdir(exist_ok=True)
frames, records = [], []
source_size = None
for i, source in enumerate(files):
    with Image.open(source) as im:
        im.load()
        if source_size is None: source_size = im.size
        assert im.size == source_size
        left, top, right, bottom = args.crop
        assert 0 <= left < right <= im.width and 0 <= top < bottom <= im.height
        frame = im.convert('RGB').crop(tuple(args.crop))
    target = cropped / f'{args.prefix}.{i:04d}.png'
    assert not target.exists()
    frame.save(target)
    frames.append(frame)
    records.append({'index': i, 'source_index': args.start_frame + i, 'source': str(source.resolve()),
                    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                    'png': str(target.resolve())})
# 全帧共用调色板，减少独立量化造成的颜色跳动。
atlas = Image.new('RGB', (128 * 8, 128 * ((len(frames) + 7) // 8)), 'white')
for i, frame in enumerate(frames):
    atlas.paste(frame.resize((128, 128)), ((i % 8) * 128, (i // 8) * 128))
palette = atlas.quantize(colors=256)
if keep:
    # 缩略图建的调色板会丢掉只占少量像素的细线颜色；把指定颜色固定放在调色板前部，
    # 其余位置沿用缩略图量化结果。
    auto = [tuple(palette.getpalette()[3 * i:3 * i + 3]) for i in range(256 - len(keep))]
    palette = Image.new('P', (1, 1))
    palette.putpalette([v for c in keep + auto for v in c])
indexed = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
assert not args.output.exists()
indexed[0].save(args.output, save_all=True, append_images=indexed[1:],
                duration=args.duration, loop=0, optimize=False, disposal=2)
with Image.open(args.output) as gif:
    assert gif.n_frames == len(files)
    for i in range(gif.n_frames):
        gif.seek(i)
        assert gif.info['duration'] == args.duration
        # 指定颜色的像素在 GIF 中必须逐像素保持原值。
        if keep:
            src, out = np.asarray(frames[i]), np.asarray(gif.convert('RGB'))
            mask = np.isin(src.view([('', src.dtype)] * 3), np.array(keep, src.dtype).view([('', src.dtype)] * 3)).squeeze(-1)
            assert (out[mask] == src[mask]).all(), f'第 {i} 帧指定颜色未保留'
report = {'gif': str(args.output.resolve()), 'frames': records, 'size': frames[0].size,
          'crop': args.crop, 'start_frame': args.start_frame, 'frame_count': len(files),
          'source_size': source_size, 'duration_ms': args.duration, 'loop': 0,
          'keep_colors': args.keep_colors,
          'gif_sha256': hashlib.sha256(args.output.read_bytes()).hexdigest()}
args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'output': str(args.output), 'frames': len(frames), 'size': frames[0].size}))
