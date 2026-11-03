#!/usr/bin/env python3
"""本次动画合成临时工具；作为结果唯一生成入口保留，任务结束且无需复现时可删除。"""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('--input', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--prefix', required=True)
parser.add_argument('--crop', type=int, nargs=4, required=True)
parser.add_argument('--frames', type=int, default=40)
parser.add_argument('--duration', type=int, default=500)
args = parser.parse_args()
files = sorted(args.input.glob('*.png'))
assert len(files) == args.frames
assert args.duration > 0
args.output.parent.mkdir(parents=True, exist_ok=True)
cropped = args.output.parent / 'frames'
cropped.mkdir(exist_ok=True)
frames, records = [], []
for i, source in enumerate(files):
    assert source.stem.endswith(f'.{i:04d}')
    with Image.open(source) as im:
        im.load()
        assert im.size == (2110, 1066)
        frame = im.convert('RGB').crop(tuple(args.crop))
    target = cropped / f'{args.prefix}.{i:04d}.png'
    assert not target.exists()
    frame.save(target)
    frames.append(frame)
    records.append({'index': i, 'source': str(source.resolve()),
                    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                    'png': str(target.resolve())})
# 全帧共用调色板，减少独立量化造成的颜色跳动。
atlas = Image.new('RGB', (128 * 8, 128 * ((len(frames) + 7) // 8)), 'white')
for i, frame in enumerate(frames):
    atlas.paste(frame.resize((128, 128)), ((i % 8) * 128, (i // 8) * 128))
palette = atlas.quantize(colors=256)
indexed = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
assert not args.output.exists()
indexed[0].save(args.output, save_all=True, append_images=indexed[1:],
                duration=args.duration, loop=0, optimize=False, disposal=2)
with Image.open(args.output) as gif:
    assert gif.n_frames == args.frames
    for i in range(gif.n_frames):
        gif.seek(i)
        assert gif.info['duration'] == args.duration
report = {'gif': str(args.output.resolve()), 'frames': records, 'size': frames[0].size,
          'crop': args.crop, 'duration_ms': args.duration, 'loop': 0,
          'gif_sha256': hashlib.sha256(args.output.read_bytes()).hexdigest()}
args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'output': str(args.output), 'frames': len(frames), 'size': frames[0].size}))
