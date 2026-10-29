"""用真实文件协议验证启动回执；不依赖本机 ParaView 或 SSH。"""
import ast
import builtins
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import types
import unittest
from unittest.mock import patch

CLIENT = Path(__file__).resolve().parents[1] / 'paraview_bridge_client.py'


class Timer:
    def connect(self, *args): pass
    def start(self, *args): pass
    def stop(self): pass


class StartupTests(unittest.TestCase):
    def test_gui_roundtrip_and_isolated_launch_receipt(self):
        # 只加载定义，不启动真实会话；模拟 GUI API，传输仍执行真实 shell 文件操作。
        source = CLIENT.read_text()
        ast.parse(source)
        uri = 'cs://localhost:11115'
        connection = types.SimpleNamespace(Session=types.SimpleNamespace(GetURI=lambda: uri))
        pvs = types.SimpleNamespace(GetSources=lambda: {('sample', '12'): None},
                                    GetViews=lambda: [], GetActiveSource=lambda: None,
                                    GetTimeKeeper=lambda: types.SimpleNamespace(Time=100, TimestepValues=[25, 50, 75, 100]))
        modules = {'paraview': types.SimpleNamespace(simple=pvs,
                   servermanager=types.SimpleNamespace(ActiveConnection=connection)),
                   'PythonQt': types.SimpleNamespace(QtCore=types.SimpleNamespace(QTimer=Timer))}
        with tempfile.TemporaryDirectory() as tmp, patch.dict(sys.modules, modules):
            namespace = {}
            exec(compile(source.split('# 首次迁移时兼容')[0], str(CLIENT), 'exec'), namespace)
            runtime = builtins._amrex_paraview_bridge_runtime
            bridge_type = namespace['ParaViewBridge']

            class LocalBridge(bridge_type):
                def remote(self, command, data=None):
                    subprocess.run(['bash', '-n', '-c', command], check=True)
                    return subprocess.run(['bash', '-c', command], input=data,
                                          capture_output=True, check=True).stdout

            root = Path(tmp) / 'shared directory'
            launch = 'a' * 32
            environment = {'PV_BRIDGE_ROOT': str(root), 'PV_BRIDGE_LAUNCH_ID': launch,
                           'PV_BRIDGE_SCRIPT': str(CLIENT)}
            with patch.dict(os.environ, environment):
                bridge = LocalBridge()
            try:
                deadline = time.monotonic() + 5
                while not (root / 'current.json').exists() and time.monotonic() < deadline:
                    time.sleep(.01)
                registration = json.loads((root / 'current.json').read_text())
                self.assertEqual(registration['launch_id'], launch)
                self.assertEqual(registration['source_sha256'], hashlib.sha256(CLIENT.read_bytes()).hexdigest())
                receipt_path = root / ('launch-' + launch + '.json')
                # SSH 注册和心跳并不代表 GUI 活着；未执行 tick 时不得出现成功回执。
                self.assertFalse(receipt_path.exists())
                bridge.tick()
                while not receipt_path.exists() and time.monotonic() < deadline + 5:
                    time.sleep(.02)
                receipt = json.loads(receipt_path.read_text())
                self.assertTrue(receipt['ok'])
                self.assertEqual(receipt['id'], 'startup-' + launch)
                self.assertEqual(receipt['version'], namespace['BRIDGE_VERSION'])
                self.assertEqual(receipt['value']['connection_uri'], uri)
                self.assertIn('export_centerline', receipt['value']['capabilities'])
                self.assertIn('add_vorticity_view', receipt['value']['capabilities'])
                # 另一次启动编号不能复用这个回执。
                self.assertFalse((root / ('launch-' + 'b' * 32 + '.json')).exists())
                # 模拟不同执行上下文，仍须取到同一个运行时，不创建第二份所有者。
                other = {}
                exec(compile(source.split('# 首次迁移时兼容')[0], str(CLIENT), 'exec'), other)
                self.assertIs(other['_runtime'], runtime)
            finally:
                bridge.stop()
                self.assertFalse(bridge.worker.is_alive())

    def test_reload_stops_previous_owners_across_contexts(self):
        class FakeBridge:
            def __init__(self): self.stops = 0
            def stop(self): self.stops += 1
        runtime_owner, shell_owner, main_owner = FakeBridge(), FakeBridge(), FakeBridge()
        runtime = types.SimpleNamespace(bridge=runtime_owner)
        tail = CLIENT.read_text().split('# 首次迁移时兼容', 1)[1]
        tail = '# 首次迁移时兼容' + tail
        with patch.object(sys.modules['__main__'], 'pv_bridge', main_owner, create=True):
            first = {'_runtime': runtime, 'sys': sys, 'pv_bridge': shell_owner,
                     'ParaViewBridge': FakeBridge}
            exec(tail, first)
            for owner in (runtime_owner, shell_owner, main_owner):
                self.assertEqual(owner.stops, 1)
            created = runtime.bridge
            second = {'_runtime': runtime, 'sys': sys, 'ParaViewBridge': FakeBridge}
            exec(tail, second)
            self.assertEqual(created.stops, 1)
            self.assertIsNot(runtime.bridge, created)

    def test_batch_definition_loading_does_not_start_or_replace_bridge(self):
        import runpy
        owner = object()
        runtime = types.SimpleNamespace(bridge=owner, timer_callbacks=[])
        modules = {'paraview': types.SimpleNamespace(simple=object())}
        with patch.dict(sys.modules, modules), patch.object(
                builtins, '_amrex_paraview_bridge_runtime', runtime, create=True):
            definitions = runpy.run_path(str(CLIENT),
                init_globals={'_PV_BRIDGE_DEFINITIONS_ONLY': True})
            self.assertIs(runtime.bridge, owner)
            self.assertTrue(callable(definitions['export_centerline']))
            self.assertNotIn('pv_bridge', definitions)

    def test_gui_failure_receipt(self):
        source = CLIENT.read_text().split('# 首次迁移时兼容')[0]
        def broken_sources(): raise RuntimeError('GUI failed')
        modules = {'paraview': types.SimpleNamespace(
            simple=types.SimpleNamespace(GetSources=broken_sources,
                GetTimeKeeper=lambda: types.SimpleNamespace(Time=100, TimestepValues=[25, 50, 75, 100])),
            servermanager=types.SimpleNamespace(ActiveConnection=None))}
        with patch.dict(sys.modules, modules):
            namespace = {}
            exec(compile(source, str(CLIENT), 'exec'), namespace)
            bridge = namespace['ParaViewBridge'].__new__(namespace['ParaViewBridge'])
            bridge.closed = threading.Event()
            import queue
            bridge.incoming = queue.Queue(); bridge.outgoing = queue.Queue()
            bridge.incoming.put({'id': 'startup-test', 'action': 'inspect'})
            bridge.tick()
            result = bridge.outgoing.get_nowait()
            self.assertFalse(result['ok'])
            self.assertEqual(result['error'], 'GUI failed')


if __name__ == '__main__':
    unittest.main()
