"""超算端命令投递器；用仓库 .venv/bin/python 运行。"""
import argparse
import json
from pathlib import Path
import re
import time
import uuid


BRIDGE_ROOT = Path('/home/share/iosoeqkp/home/Liuzhaohui/.cache/paraview-bridge')


def resolve_directory(directory):
    if directory == 'current':
        registration = json.loads((BRIDGE_ROOT / 'current.json').read_text())
        path = Path(registration['directory'])
        if path.parent != BRIDGE_ROOT or not re.fullmatch(r'session-[0-9a-f]{32}', path.name):
            raise ValueError('固定入口包含无效会话路径')
        heartbeat = float((path / 'heartbeat').read_text())
        if not -5 <= time.time() - heartbeat <= 90:
            raise RuntimeError('当前控制桥心跳已过期，请检查 ParaView 是否仍打开')
        return path
    path = Path(directory)
    shared = path.parent == BRIDGE_ROOT and re.fullmatch(r'session-[0-9a-f]{32}', path.name)
    if not shared and not re.fullmatch(r'/tmp/paraview-bridge-[0-9a-f]{32}', str(path)):
        raise ValueError('请使用 current 或客户端打印的完整会话目录')
    return path


def send(directory, action, color=None, timeout=60, options=None):
    path = resolve_directory(directory)
    if not path.is_dir():
        raise ValueError('会话目录不存在；请确认 SSH 目标与当前主机共享 /tmp')
    # 单槽协议：上条命令未返回前拒绝覆盖，超时也不自动重发。
    command_file = path / 'command.json'
    result_file = path / 'result.json'
    if command_file.exists():
        previous = json.loads(command_file.read_text())
        if not result_file.exists() or json.loads(result_file.read_text())['id'] != previous['id']:
            raise RuntimeError('上一条命令尚未返回，请先检查 result.json 和客户端')
    command = {'id': uuid.uuid4().hex, 'action': action}
    if options:
        if set(options) - {'input', 'output', 'resolution', 'max_level'}:
            raise ValueError('未知导出参数')
        command.update(options)
    if color is not None:
        command['color'] = color
    temporary = path / 'command.tmp'
    temporary.write_text(json.dumps(command), encoding='utf-8')
    temporary.replace(command_file)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if result_file.exists():
            result = json.loads(result_file.read_text())
            if result['id'] == command['id']:
                return result
        time.sleep(0.2)
    raise TimeoutError('等待回传超时；命令可能稍后执行，请勿重复投递，先查看 result.json')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', nargs='?', default='current')
    parser.add_argument('action', choices=['inspect', 'background', 'restore_background', 'export_centerline'])
    parser.add_argument('--color', nargs=3, type=float)
    parser.add_argument('--input')
    parser.add_argument('--output')
    parser.add_argument('--resolution', type=int, default=1024)
    parser.add_argument('--max-level', type=int, default=2)
    parser.add_argument('--timeout', type=float, default=60)
    args = parser.parse_args()
    options = None
    if args.action == 'export_centerline':
        if not args.input or not args.output:
            parser.error('export_centerline requires --input and --output')
        options = {'input': args.input, 'output': args.output,
                   'resolution': args.resolution, 'max_level': args.max_level}
    reply = send(args.directory, args.action, args.color, timeout=args.timeout, options=options)
    print(json.dumps(reply, ensure_ascii=False, indent=2))
    raise SystemExit(0 if reply['ok'] else 1)
