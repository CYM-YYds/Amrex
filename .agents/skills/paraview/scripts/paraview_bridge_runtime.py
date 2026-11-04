"""在本机 ParaView Python Shell 加载；通过 SSH 接收有限的界面操作。"""
import base64
import tempfile
import builtins
import ctypes
import hashlib
import json
import os
import sys
import traceback
import queue
import shlex
import subprocess
import threading
import uuid
from types import SimpleNamespace

from paraview import simple as pvs
from paraview_protocol import DEFAULT_BRIDGE_ROOT, CAPABILITIES
from paraview_operations import (AMR_CELL_VORTICITY_SCRIPT, export_centerline,
                                 add_vorticity_view, repair_vorticity_slice, update_background, render_diagnostics, disable_fxaa, set_plotfile_series, snapshot_state)
from paraview_publication import viridis, domain_outline, publication_vorticity, add_axis_title_view, align_publication, apply_2d_vorticity_trace, color_outline_levels, add_level_array_outline


BRIDGE_VERSION = '28'
RUNTIME_UPDATE_PROTOCOL = 2
REMOTE_CLIENT_SOURCE = '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/.agents/skills/paraview/scripts/paraview_bridge_client.py'



# 经操作层执行的动作；inspect、背景与热更新由运行层自身处理。
# 与 CAPABILITIES 的一致性由 test_action_registry 保证。
OPERATIONS = {'viridis': viridis, 'domain_outline': domain_outline, 'add_level_array_outline': add_level_array_outline, 'color_outline_levels': color_outline_levels, 'snapshot_state': snapshot_state, 'set_plotfile_series': set_plotfile_series, 'apply_2d_vorticity_trace': apply_2d_vorticity_trace, 'disable_fxaa': disable_fxaa, 'export_centerline': export_centerline, 'add_vorticity_view': add_vorticity_view,
              'repair_vorticity_slice': repair_vorticity_slice,
              'publication_vorticity': publication_vorticity,
              'add_axis_title_view': add_axis_title_view,
              'align_publication': align_publication}


# 跨启动脚本和交互 Shell 共用唯一实例，也保留停止后可能被系统消息引用的回调。
if not hasattr(builtins, '_amrex_paraview_bridge_runtime'):
    builtins._amrex_paraview_bridge_runtime = SimpleNamespace(bridge=None, timer_callbacks=[])
_runtime = builtins._amrex_paraview_bridge_runtime
_pv_timer_callbacks = _runtime.timer_callbacks


class WindowsGuiTimer:
    """封装原生定时器的回调引用、重入保护和停止生命周期。"""
    def __init__(self, tick):
        if threading.current_thread() is not threading.main_thread():
            raise RuntimeError('Load this script in the ParaView GUI Python Shell')
        from ctypes import wintypes
        self.active = True
        self.busy = False
        self.owner = threading.get_ident()
        self.user32 = ctypes.WinDLL('user32', use_last_error=True)
        callback_type = ctypes.WINFUNCTYPE(None, wintypes.HWND, wintypes.UINT,
                                          ctypes.c_size_t, wintypes.DWORD)
        self.user32.SetTimer.argtypes = [wintypes.HWND, ctypes.c_size_t,
                                        wintypes.UINT, callback_type]
        self.user32.SetTimer.restype = ctypes.c_size_t
        self.user32.KillTimer.argtypes = [wintypes.HWND, ctypes.c_size_t]
        self.user32.KillTimer.restype = wintypes.BOOL

        def dispatch(hwnd, message, timer_id, timestamp):
            if not self.active or self.busy:
                return
            self.busy = True
            try:
                if threading.get_ident() != self.owner:
                    raise RuntimeError('Timer callback arrived on another thread')
                tick()
            except BaseException:
                traceback.print_exc()
                self.stop()
            finally:
                self.busy = False

        self.callback = callback_type(dispatch)
        self.timer_id = self.user32.SetTimer(None, 0, 200, self.callback)
        if not self.timer_id:
            raise ctypes.WinError(ctypes.get_last_error())
        _pv_timer_callbacks.append(self.callback)

    def stop(self):
        if self.active:
            self.active = False
            self.user32.KillTimer(None, self.timer_id)


def inspect_state(source_sha256):
    """当前连接、管线与视图快照；不修改任何对象。"""
    from paraview import servermanager
    connection = servermanager.ActiveConnection
    uri = connection.Session.GetURI() if connection is not None else None
    return {'capabilities': CAPABILITIES, 'version': BRIDGE_VERSION,
            'source_sha256': source_sha256,
            'connection_uri': uri,
            'timekeeper': {'time': float(pvs.GetTimeKeeper().Time),
                           'timestep_values': list(pvs.GetTimeKeeper().TimestepValues)} if connection is not None else None,
            'sources': [list(key) for key in pvs.GetSources()],
            'source_details': [
                {'name': name, 'id': key, 'type': source.GetXMLName(),
                 'level': int(source.Level) if source.GetProperty('Level') else None,
                 'function': str(source.Function) if source.GetProperty('Function') else None,
                 'unchecked_function': source.GetProperty('Function').SMProperty.GetUncheckedElement(0)
                                       if source.GetProperty('Function') and source.GetProperty('Function').SMProperty.GetNumberOfUncheckedElements() else None,
                 'inputs': [source.GetProperty('Input').SMProperty.GetProxy(i).GetGlobalIDAsString()
                            for i in range(source.GetProperty('Input').SMProperty.GetNumberOfProxies())]
                           if source.GetProperty('Input') else [],
                 'files': list(source.FileNames) if source.GetProperty('FileNames') else [],
                 'time_properties': {key: str(source.GetProperty(key)) for key in source.ListProperties()
                                     if 'Time' in key or 'Timestep' in key}}
                for (name, key), source in pvs.GetSources().items() if source is not None],
            'views': [v.GetXMLName() for v in pvs.GetViews()],
            'view_details': [
                {'id': v.GetGlobalIDAsString(), 'size': list(v.ViewSize),
                 'fxaa': render_diagnostics(v), 'view_time': float(v.ViewTime),
                 'visible_sources': [r.GetProperty('Input').SMProperty.GetProxy(0).GetGlobalIDAsString()
                                     for r in v.Representations if r.GetProperty('Input')
                                     and r.GetProperty('Input').SMProperty.GetNumberOfProxies() and r.Visibility]}
                for v in pvs.GetViews() if v.GetXMLName() == 'RenderView']}


class ParaViewBridge:
    def prepare_reload(self, command):
        """后台只读取固定仓库文件；SHA256必须与投递端期望一致。"""
        import ast
        expected = command.get('expected_sha256', '')
        if not isinstance(expected, str) or len(expected) != 64 or any(
                c not in '0123456789abcdef' for c in expected):
            raise ValueError('reload_client requires a lowercase SHA256')
        source_path = os.environ.get('PV_BRIDGE_CLIENT_SOURCE', REMOTE_CLIENT_SOURCE)
        source = self.remote('cat ' + shlex.quote(source_path))
        if hashlib.sha256(source).hexdigest() != expected:
            raise ValueError('Repository client changed or download is incomplete; refresh status before retrying')
        tree = ast.parse(source)
        protocol = next((ast.literal_eval(node.value) for node in tree.body
                         if isinstance(node, ast.Assign) and any(
                             isinstance(t, ast.Name) and t.id == 'RUNTIME_UPDATE_PROTOCOL'
                             for t in node.targets)), None)
        if protocol != RUNTIME_UPDATE_PROTOCOL:
            raise ValueError('Runtime protocol changed; restart with the current launcher instead of hot update')
        compile(source, REMOTE_CLIENT_SOURCE, 'exec')
        return source

    def reload_client(self, command):
        """在GUI主线程替换命令实现，保持同一传输线程、计时器与状态所有者。"""
        from pathlib import Path
        source = command.get('_client_source')
        expected = command.get('expected_sha256')
        if not isinstance(source, bytes) or hashlib.sha256(source).hexdigest() != expected:
            raise ValueError('Client source must be prepared by the transport worker')
        script = os.environ.get('PV_BRIDGE_SCRIPT')
        if not script:
            raise ValueError('PV_BRIDGE_SCRIPT is missing; reload from the local launcher first')
        before = self.execute({'action': 'inspect'})
        if expected == self.source_sha256:
            return {'updated': False, 'version': before['version'], 'source_sha256': expected,
                    'connection_uri': before['connection_uri'], 'session': self.directory}
        namespace = {'__name__': '_updated_paraview_bridge', '__file__': script,
                     '_PV_BRIDGE_DEFINITIONS_ONLY': True}
        exec(compile(source, script, 'exec'), namespace)
        if namespace.get('RUNTIME_UPDATE_PROTOCOL') != RUNTIME_UPDATE_PROTOCOL:
            raise ValueError('Incompatible runtime protocol')
        candidate = namespace['ParaViewBridge']
        after = candidate.execute(self, {'action': 'inspect'})
        if any(before[key] != after[key] for key in ('connection_uri', 'sources', 'views')):
            raise ValueError('Update changed the existing connection or pipeline; not activating it')
        original_class = self.__class__
        temporary = Path(script).with_suffix('.py.update')
        try:
            temporary.write_bytes(source)
            self.__class__ = candidate
            os.replace(temporary, script)
        except Exception:
            self.__class__ = original_class
            temporary.unlink(missing_ok=True)
            raise
        self.source_sha256 = expected
        return {'updated': True, 'version': after['version'], 'source_sha256': expected,
                'capabilities': after['capabilities'], 'connection_uri': after['connection_uri'],
                'session': self.directory, 'preserved_sources': after['sources'],
                'preserved_views': after['views']}

    def __init__(self):
        self.root = os.environ.get('PV_BRIDGE_ROOT', DEFAULT_BRIDGE_ROOT)
        self.directory = self.root + '/session-' + uuid.uuid4().hex
        self.launch_id = os.environ.get('PV_BRIDGE_LAUNCH_ID') or uuid.uuid4().hex
        if len(self.launch_id) != 32 or any(c not in '0123456789abcdef' for c in self.launch_id):
            raise ValueError('PV_BRIDGE_LAUNCH_ID must be a lowercase UUID hex string')
        self.source_sha256 = None
        script_path = os.environ.get('PV_BRIDGE_SCRIPT')
        if script_path:
            with open(script_path, 'rb') as stream:
                self.source_sha256 = hashlib.sha256(stream.read()).hexdigest()
        self.startup_id = 'startup-' + self.launch_id
        self.ssh = ['ssh', '-T', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                    '-o', 'ServerAliveInterval=15', '-o', 'ServerAliveCountMax=2',
                    '-p', os.environ.get('PV_BRIDGE_SSH_PORT', '6022'),
                    os.environ.get('PV_BRIDGE_SSH_TARGET', 'Liuzhaohui@27.18.114.25')]
        self.incoming = queue.Queue()
        self.outgoing = queue.Queue()
        self.closed = threading.Event()
        self.backgrounds = {}
        if sys.platform == 'win32':
            self.timer = WindowsGuiTimer(self.tick)
            print('ParaView bridge timer: Windows SetTimer')
        else:
            from PythonQt import QtCore
            self.timer = QtCore.QTimer()
            self.timer.connect('timeout()', self.tick)
            self.timer.start(200)
        self.incoming.put({'id': self.startup_id, 'action': 'inspect'})
        self.worker = threading.Thread(target=self.poll, daemon=True)
        self.worker.start()

    def close_transport(self):
        # 仅后台线程拥有传输；断线后下次调用新建连接，不重放本次请求。
        transport = getattr(self, '_transport', None)
        if transport is None:
            return
        self._transport = None
        process, replies, errors, reader = transport
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        reader.join(timeout=5)
        try:
            process.stdin.close()
        except OSError:
            # 连接已断开时flush可能再次报BrokenPipe，仍须关闭其他资源。
            pass
        process.stdout.close()
        errors.close()

    def remote(self, command, data=None):
        # 单个 SSH 长连接承载逐行请求，远端子进程仍单独执行原有固定命令。
        # stdout 使用 JSON/base64 定界；stderr 单独落临时文件，避免管道阻塞。
        if getattr(self, '_transport', None) is None:
            server = """import sys,json,base64,subprocess
for line in sys.stdin.buffer:
    request=json.loads(line)
    result=subprocess.run(request['command'],shell=True,input=base64.b64decode(request['data']),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=35)
    print(json.dumps({'code':result.returncode,'out':base64.b64encode(result.stdout).decode(),'err':base64.b64encode(result.stderr).decode()}),flush=True)
"""
            python = '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/.venv/bin/python'
            errors = tempfile.TemporaryFile()
            try:
                process = subprocess.Popen(self.ssh + [shlex.quote(python) + ' -u -c ' + shlex.quote(server)],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors,
                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            except Exception:
                errors.close()
                raise
            replies = queue.Queue()
            def read_replies():
                try:
                    for line in process.stdout:
                        replies.put(line)
                finally:
                    replies.put(None)
            reader = threading.Thread(target=read_replies, daemon=True)
            self._transport = (process, replies, errors, reader)
            reader.start()
        process, replies, errors, reader = self._transport
        try:
            request = json.dumps({'command': command, 'data': base64.b64encode(data or b'').decode()})
            process.stdin.write(request.encode() + b'\n')
            process.stdin.flush()
            line = replies.get(timeout=45)
            if line is None:
                errors.seek(0)
                message = errors.read().decode('utf-8', errors='replace').strip()
                if any(value in message.lower() for value in (
                        'permission denied', 'host key verification failed', 'host identification has changed')):
                    raise RuntimeError(message)
                raise ConnectionError(message or 'SSH connection closed')
            response = json.loads(line)
            if response['code']:
                raise RuntimeError(base64.b64decode(response['err']).decode('utf-8', errors='replace'))
            return base64.b64decode(response['out'])
        except (queue.Empty, BrokenPipeError, OSError) as exc:
            self.close_transport()
            raise ConnectionError('SSH transport interrupted or timed out: ' + str(exc)) from exc
        except Exception:
            self.close_transport()
            raise

    def poll(self):
        path = shlex.quote(self.directory)
        try:
            root = shlex.quote(self.root)
            registration = json.dumps({'directory': self.directory, 'protocol': 1,
                                       'launch_id': self.launch_id, 'version': BRIDGE_VERSION,
                                       'source_sha256': self.source_sha256}).encode('utf-8')
            # 初始化只有建目录和原子发布同一份注册数据，丢失回执后可安全重试。
            # 不在此处重放 GUI 操作；最多三次，等待可被 stop 中断。
            for attempt in range(3):
                if self.closed.is_set():
                    return
                try:
                    self.remote('umask 077; mkdir -p ' + root + ' && mkdir -p -m 700 ' + path
                                + ' && date +%s > ' + path + '/heartbeat'
                                + ' && cat > ' + path + '/registration.tmp && mv '
                                + path + '/registration.tmp ' + root + '/current.json', registration)
                    break
                except (ConnectionError, subprocess.TimeoutExpired) as exc:
                    if attempt == 2:
                        raise
                    print('ParaView bridge startup transport retry %d/3: %s' % (attempt + 1, exc), flush=True)
                    if self.closed.wait(2 * (attempt + 1)):
                        return
            if self.closed.is_set():
                return
            print('ParaView bridge registered (GUI check pending): ' + self.directory)
            previous = None
            pending = None
            failures = 0
            while not self.closed.is_set():
                try:
                    if pending is None:
                        try:
                            pending = self.outgoing.get_nowait()
                        except queue.Empty:
                            pass
                    if pending is not None:
                        if pending['id'] == self.startup_id:
                            # 只有 GUI 主线程执行 inspect 后，才发布本次启动专属的回执。
                            receipt = dict(pending, launch_id=self.launch_id, directory=self.directory,
                                           version=BRIDGE_VERSION, source_sha256=self.source_sha256)
                            target = root + '/launch-' + self.launch_id + '.json'
                            self.remote('umask 077; cat > ' + path + '/startup.tmp && mv '
                                        + path + '/startup.tmp ' + target,
                                        json.dumps(receipt).encode('utf-8'))
                            print('ParaView bridge GUI check returned: ' + self.directory)
                        self.remote('umask 077; cat > ' + path + '/result.tmp && mv '
                                    + path + '/result.tmp ' + path + '/result.json',
                                    json.dumps(pending).encode('utf-8'))
                        pending = None
                    raw = self.remote('date +%s > ' + path + '/heartbeat.tmp && mv '
                                      + path + '/heartbeat.tmp ' + path + '/heartbeat; if test -f ' + path + '/command.json; then cat '
                                      + path + '/command.json; fi')
                    if raw:
                        command = json.loads(raw)
                        if not isinstance(command.get('id'), str):
                            raise ValueError('command.id must be a string')
                        if command['id'] != previous:
                            # 同一会话中每个命令只执行一次；回传失败只重试回传。
                            previous = command['id']
                            # 内部源码只能由后台从固定路径下载，忽略文件协议传入的同名字段。
                            command.pop('_client_source', None)
                            if command.get('action') == 'reload_client':
                                try:
                                    command['_client_source'] = self.prepare_reload(command)
                                except Exception as exc:
                                    self.outgoing.put({'id': command['id'], 'ok': False, 'error': str(exc)})
                                    continue
                            self.incoming.put(command)
                except Exception as exc:
                    failures += 1
                    delay = min(60, 2 ** min(failures + 1, 6))
                    print('ParaView bridge transport (retry in %ds): %s' % (delay, exc))
                    if isinstance(exc, RuntimeError):
                        raise
                    self.closed.wait(delay)
                    continue
                failures = 0
                self.closed.wait(2)
        except Exception as exc:
            print('ParaView bridge startup failed: ' + str(exc), flush=True)
            self.closed.set()
        finally:
            self.close_transport()

    def tick(self):
        if self.closed.is_set():
            self.timer.stop()
            return
        try:
            command = self.incoming.get_nowait()
        except queue.Empty:
            return
        result = {'id': command['id']}
        try:
            result.update(ok=True, value=self.execute(command))
        except Exception as exc:
            result.update(ok=False, error=str(exc))
        self.outgoing.put(result)

    def execute(self, command):
        # 白名单只允许明确的数据导出和界面操作，不执行远端任意代码。
        action = command.get('action')
        if action == 'reload_client':
            return self.reload_client(command)
        if action == 'inspect':
            return inspect_state(getattr(self, 'source_sha256', None))
        if action in OPERATIONS:
            return OPERATIONS[action](command)
        if action not in ('background', 'restore_background'):
            raise ValueError('Unsupported action: ' + str(action))
        return update_background(command, self.backgrounds)

    def stop(self):
        # 停止接收命令；保留远端结果，便于诊断。背景须在停止前恢复。
        self.closed.set()
        self.timer.stop()
        if threading.current_thread() is not self.worker:
            self.worker.join(timeout=50)
            if self.worker.is_alive():
                raise RuntimeError('Old bridge worker has not stopped; retry after its SSH call returns')
        print('ParaView bridge stopped')


# 首次迁移时兼容旧版 __main__ 或交互上下文中的实例；后续统一从 builtins 定位。
if not globals().get('_PV_BRIDGE_DEFINITIONS_ONLY', False):
    _previous_bridges = [_runtime.bridge, globals().get('pv_bridge'),
                         getattr(sys.modules.get('__main__'), 'pv_bridge', None)]
    _stopped_ids = set()
    for _previous_bridge in _previous_bridges:
        if _previous_bridge is not None and id(_previous_bridge) not in _stopped_ids:
            _previous_bridge.stop()
            _stopped_ids.add(id(_previous_bridge))
    pv_bridge = ParaViewBridge()
    _runtime.bridge = pv_bridge
