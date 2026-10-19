"""在本机 ParaView Python Shell 加载；通过 SSH 接收有限的界面操作。"""
import ctypes
import json
import sys
import traceback
import queue
import shlex
import subprocess
import threading
import uuid

from paraview import simple as pvs


# 保留原生回调引用，包括停止后的回调，避免队列中残留消息引用已释放地址。
if '_pv_timer_callbacks' not in globals():
    _pv_timer_callbacks = []


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


class ParaViewBridge:
    def __init__(self):
        self.directory = '/tmp/paraview-bridge-' + uuid.uuid4().hex
        self.ssh = ['ssh', '-T', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                    '-p', '6022', 'Liuzhaohui@27.18.114.25']
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
        self.worker = threading.Thread(target=self.poll, daemon=True)
        self.worker.start()

    def remote(self, command, data=None):
        # 网络调用只在后台线程运行，不触碰 ParaView 对象。
        return subprocess.run(self.ssh + [command], input=data, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=15, check=True,
                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).stdout

    def poll(self):
        path = shlex.quote(self.directory)
        try:
            self.remote('umask 077; mkdir -m 700 ' + path)
            print('ParaView bridge ready: ' + self.directory)
            previous = None
            pending = None
            while not self.closed.is_set():
                try:
                    if pending is None:
                        try:
                            pending = self.outgoing.get_nowait()
                        except queue.Empty:
                            pass
                    if pending is not None:
                        self.remote('umask 077; cat > ' + path + '/result.tmp && mv '
                                    + path + '/result.tmp ' + path + '/result.json',
                                    json.dumps(pending).encode('utf-8'))
                        pending = None
                    raw = self.remote('if test -f ' + path + '/command.json; then cat '
                                      + path + '/command.json; fi')
                    if raw:
                        command = json.loads(raw)
                        if not isinstance(command.get('id'), str):
                            raise ValueError('command.id must be a string')
                        if command['id'] != previous:
                            # 同一会话中每个命令只执行一次；回传失败只重试回传。
                            previous = command['id']
                            self.incoming.put(command)
                except Exception as exc:
                    print('ParaView bridge transport: ' + str(exc))
                self.closed.wait(2)
        except Exception as exc:
            print('ParaView bridge startup failed: ' + str(exc))
            self.closed.set()

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
        # 白名单限定为读取管线、修改背景和恢复背景，不执行远端任意代码。
        action = command.get('action')
        if action == 'inspect':
            return {'sources': [list(key) for key in pvs.GetSources()],
                    'views': [v.GetXMLName() for v in pvs.GetViews()]}
        if action not in ('background', 'restore_background'):
            raise ValueError('Unsupported action: ' + str(action))
        view = pvs.GetActiveView()
        if view is None or view.GetXMLName() != 'RenderView':
            raise ValueError('Select a Render View first')
        key = view.GetGlobalIDAsString()
        if action == 'background':
            color = command.get('color')
            if (not isinstance(color, list) or len(color) != 3
                    or any(type(c) not in (int, float) or not 0 <= c <= 1 for c in color)):
                raise ValueError('color requires three numbers in [0, 1]')
            if key not in self.backgrounds:
                self.backgrounds[key] = (list(view.Background),
                                        view.GetProperty('BackgroundColorMode').SMProperty.GetElement(0))
            view.BackgroundColorMode = 'Single Color'
            view.Background = color
        elif key in self.backgrounds:
            color, mode = self.backgrounds[key]
            view.Background = color
            # 按底层枚举整数恢复，避免属性字符串表示带引号而无法赋回。
            view.GetProperty('BackgroundColorMode').SMProperty.SetElement(0, mode)
            view.UpdateVTKObjects()
            del self.backgrounds[key]
        pvs.Render(view)
        return {'view': key, 'background': list(view.Background)}

    def stop(self):
        # 停止接收命令；保留远端结果，便于诊断。背景须在停止前恢复。
        self.closed.set()
        self.timer.stop()
        print('ParaView bridge stopped')


if 'pv_bridge' in globals():
    pv_bridge.stop()
pv_bridge = ParaViewBridge()
