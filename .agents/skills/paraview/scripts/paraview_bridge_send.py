"""超算端命令投递器；用仓库 .venv/bin/python 运行。"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import runpy
import time
import uuid


BRIDGE_ROOT = Path('/home/share/iosoeqkp/home/Liuzhaohui/.cache/paraview-bridge')

# 经桥投递的动作及其命令行选项：required 缺失即报错，defaults 总是携带，
# optional 仅在给出时携带。键名同时是 argparse 属性名与命令 JSON 字段名。
# 与客户端 CAPABILITIES 的一致性由 test_action_registry 保证。
BRIDGE_ACTIONS = {
    'inspect': {},
    'background': {},
    'restore_background': {},
    'export_centerline': {'required': ('input', 'output'),
                          'defaults': ('resolution', 'max_level')},
    'add_vorticity_view': {'required': ('source',), 'defaults': ('slice_axis', 'component'),
                           'optional': ('color_range', 'slice_position')},
    'repair_vorticity_slice': {'required': ('source',)},
    'publication_vorticity': {'required': ('source', 'color_range'),
                              'defaults': ('vorticity_scale', 'style'),
                              'optional': ('image_size', 'view_id', 'output')},
    'add_axis_title_view': {'required': ('source',), 'optional': ('reset_view', 'output')},
}
# reload_client 由 run_client_status 专用流程投递，只携带期望哈希。
RELOAD_OPTIONS = ('expected_sha256',)
BRIDGE_OPTION_KEYS = frozenset(key for spec in BRIDGE_ACTIONS.values()
                               for group in spec.values() for key in group) | set(RELOAD_OPTIONS)


def client_manifest():
    """只解析固定客户端的常量，不启动ParaView或执行客户端代码。"""
    source = Path(__file__).with_name('paraview_bridge_client.py').read_bytes()
    builder = runpy.run_path(str(Path(__file__).with_name('build_paraview_client.py')))
    if source != builder['client_source']().encode('utf-8'):
        raise RuntimeError('客户端生成物未同步，请先运行 build_paraview_client.py')
    values = {}
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id in ('BRIDGE_VERSION', 'CAPABILITIES'):
                    values[target.id] = ast.literal_eval(node.value)
    return {'version': values['BRIDGE_VERSION'], 'capabilities': values['CAPABILITIES'],
            'source_sha256': hashlib.sha256(source).hexdigest()}


def client_status(reply, expected):
    """连接状态与源码版本分开报告，不能把旧客户端误报为连接断开。"""
    if not reply.get('ok'):
        return {'ok': False, 'status': 'bridge_error', 'reply': reply}
    live = reply['value']
    connected = bool(live.get('connection_uri'))
    current = (live.get('version') == expected['version'] and
               live.get('source_sha256') == expected['source_sha256'] and
               set(expected['capabilities']).issubset(live.get('capabilities', [])))
    return {'ok': connected, 'status': 'disconnected' if not connected else
            'ready' if current else 'client_update_required',
            'connection_uri': live.get('connection_uri'), 'current': live,
            'expected': expected, 'can_hot_update': 'reload_client' in live.get('capabilities', [])}


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
        if set(options) - BRIDGE_OPTION_KEYS:
            raise ValueError('未知桥接参数')
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


LOCAL_ACTIONS = ('client_manifest', 'client_status', 'reload_client')


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', nargs='?', default='current')
    parser.add_argument('action', choices=[*BRIDGE_ACTIONS, *LOCAL_ACTIONS])
    parser.add_argument('--color', nargs=3, type=float)
    parser.add_argument('--input')
    parser.add_argument('--output')
    parser.add_argument('--resolution', type=int, default=1024)
    parser.add_argument('--image-size', nargs=2, type=int, metavar=('WIDTH', 'HEIGHT'),
                        help='按正常 GUI 视图尺寸导出；批处理时使用指定尺寸')
    parser.add_argument('--view-id', help='明确指定 publication_vorticity 目标视图 ID')
    parser.add_argument('--max-level', type=int, default=2)
    parser.add_argument('--source', help='现有管线中完整的注册名称，不作通配符展开')
    parser.add_argument('--reset-view', help='复位已创建的独立标题视图 ID，按切片边界居中')
    parser.add_argument('--slice-axis', choices=['x', 'y', 'z'], default='y')
    parser.add_argument('--slice-position', type=float, help='物理坐标；默认几何中心')
    parser.add_argument('--component', choices=['Magnitude', 'X', 'Y', 'Z'], default='Magnitude')
    parser.add_argument('--vorticity-scale', type=float, default=1.0, help='论文色标的 H/Uref 乘数')
    parser.add_argument('--style', choices=['publication', 'reference'], default='publication')
    parser.add_argument('--color-range', nargs=2, type=float, metavar=('MIN', 'MAX'),
                        help='显式设置涡量色标范围；默认使用当前切片数据范围')
    parser.add_argument('--timeout', type=float, default=60)
    return parser


def bridge_options(parser, args):
    """按 BRIDGE_ACTIONS 把命令行参数拼成命令字段；缺少必填项时由 parser 报错。"""
    spec = BRIDGE_ACTIONS[args.action]
    values = {key: getattr(args, key) for group in spec.values() for key in group}
    required = spec.get('required', ())
    if any(values[key] in (None, '') for key in required):
        parser.error(args.action + ' requires ' + ' and '.join(
            '--' + key.replace('_', '-') for key in required))
    skipped = {key for key in spec.get('optional', ()) if values[key] in (None, '')}
    return {key: value for key, value in values.items() if key not in skipped} or None


def print_json(value):
    print(json.dumps(value, ensure_ascii=False, indent=2))


def run_client_status(args):
    """client_status 只查询；reload_client 在旧版本且支持热更新时才更新并复查。"""
    expected = client_manifest()
    try:
        status = client_status(send(args.directory, 'inspect', timeout=args.timeout), expected)
    except (OSError, RuntimeError, TimeoutError, ValueError) as exc:
        print_json({'ok': False, 'status': 'bridge_unreachable', 'error': str(exc),
                    'expected': expected})
        return 1
    if args.action == 'client_status' or not status['ok'] or status['status'] == 'ready':
        print_json(status)
        return 0 if status['ok'] else 1
    if not status['can_hot_update']:
        print_json(dict(status, ok=False, error='This older client needs one local reload or a normal launcher restart; the data connection is still live.'))
        return 1
    reply = send(args.directory, 'reload_client', timeout=args.timeout,
                 options={'expected_sha256': expected['source_sha256']})
    if reply['ok']:
        verification = client_status(send(args.directory, 'inspect', timeout=args.timeout), expected)
        reply['verification'] = verification
        reply['ok'] = verification['ok'] and verification['status'] == 'ready'
    print_json(reply)
    return 0 if reply['ok'] else 1


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.action == 'client_manifest':
        print(json.dumps(client_manifest(), indent=2))
        return 0
    if args.action in ('client_status', 'reload_client'):
        return run_client_status(args)
    reply = send(args.directory, args.action, args.color, timeout=args.timeout,
                 options=bridge_options(parser, args))
    print_json(reply)
    return 0 if reply['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())


