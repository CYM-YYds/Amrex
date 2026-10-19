"""由独立 pvbatch 执行；仅构建数据管线，不启动 GUI 控制桥。"""
import argparse
import json
from pathlib import Path
import runpy
import socket
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--metadata', required=True)
    parser.add_argument('--resolution', type=int, default=1024)
    parser.add_argument('--max-level', type=int, default=2)
    args = parser.parse_args()
    started = time.monotonic()
    # 只加载定义，复用同一导出函数，不启动计时器、SSH 或会话注册。
    definitions = runpy.run_path(str(Path(__file__).with_name('paraview_bridge_client.py')),
                                init_globals={'_PV_BRIDGE_DEFINITIONS_ONLY': True})
    value = definitions['export_centerline']({'input': args.input, 'output': args.output,
                                            'resolution': args.resolution, 'max_level': args.max_level})
    from paraview.simple import GetParaViewVersion
    result = {'ok': True, 'backend': 'pvbatch', 'paraview_version': str(GetParaViewVersion()),
              'execution_host': socket.gethostname(), 'executable': sys.executable,
              'elapsed_seconds': time.monotonic()-started, 'value': value}
    Path(args.metadata).write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('PARAVIEW_BATCH_EXPORT_OK ' + args.output)


if __name__ == '__main__':
    main()
