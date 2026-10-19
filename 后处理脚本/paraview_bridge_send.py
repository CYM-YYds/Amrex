"""超算端命令投递器；用仓库 .venv/bin/python 运行。"""
import argparse
import json
from pathlib import Path
import re
import time
import uuid


def send(directory, action, color=None, timeout=30):
    path = Path(directory)
    if not re.fullmatch(r'/tmp/paraview-bridge-[0-9a-f]{32}', str(path)):
        raise ValueError('请使用客户端打印的完整会话目录')
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
    parser.add_argument('directory')
    parser.add_argument('action', choices=['inspect', 'background', 'restore_background'])
    parser.add_argument('--color', nargs=3, type=float)
    args = parser.parse_args()
    reply = send(args.directory, args.action, args.color)
    print(json.dumps(reply, ensure_ascii=False, indent=2))
    raise SystemExit(0 if reply['ok'] else 1)
