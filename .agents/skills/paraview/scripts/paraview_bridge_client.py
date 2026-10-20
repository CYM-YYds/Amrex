"""在本机 ParaView Python Shell 加载；通过 SSH 接收有限的界面操作。"""
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


BRIDGE_VERSION = '4'
RUNTIME_UPDATE_PROTOCOL = 1
REMOTE_CLIENT_SOURCE = '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/.agents/skills/paraview/scripts/paraview_bridge_client.py'
CAPABILITIES = ['inspect', 'background', 'restore_background', 'export_centerline',
                'add_vorticity_view', 'reload_client']

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


def export_centerline(command):
    # 复用已有 AMReX 读取器与标准过滤器，不执行远端传入的 Python 代码。
    import math
    import posixpath
    source = command.get('input', '')
    output = command.get('output', '')
    if not source.startswith('/') or not output.startswith('/') or not output.endswith('.csv'):
        raise ValueError('input/output require absolute remote paths; output must end in .csv')
    resolution = command.get('resolution', 1024)
    level = command.get('max_level', 2)
    if type(resolution) is not int or not 8 <= resolution <= 100000:
        raise ValueError('resolution must be an integer in [8, 100000]')
    if type(level) is not int or not 0 <= level <= 10:
        raise ValueError('max_level must be an integer in [0, 10]')
    previous = pvs.GetActiveSource()
    created = []
    try:
        reader = pvs.AMReXBoxLibGridReader(
            registrationName='bridge_' + posixpath.basename(source), FileNames=[source])
        created.append(reader)
        reader.Level = level
        reader.CellArrayStatus = ['ux', 'uy', 'uz']
        reader.UpdatePipeline()
        info = reader.GetDataInformation()
        bounds = list(info.GetBounds())
        if len(bounds) != 6 or any(not math.isfinite(v) for v in bounds) or any(
                bounds[2*i] >= bounds[2*i+1] for i in range(3)):
            raise ValueError('Reader returned invalid 3D bounds')
        center = [(bounds[2*i]+bounds[2*i+1])/2 for i in range(3)]
        blocks = pvs.ExtractBlock(registrationName='bridge_blocks', Input=reader)
        created.append(blocks)
        blocks.Selectors = ['/Root']
        points = pvs.CellDatatoPointData(registrationName='bridge_point_velocity', Input=blocks)
        created.append(points)
        line = pvs.PlotOverLine(registrationName='bridge_centerline', Input=points)
        created.append(line)
        line.Point1 = [center[0], center[1], bounds[4]]
        line.Point2 = [center[0], center[1], bounds[5]]
        line.SamplingPattern = 'Sample Uniformly'
        line.Resolution = resolution
        line.UpdatePipeline()
        pvs.SaveData(output, proxy=line, FieldAssociation='Point Data', Precision=17)
        return {'input': source, 'output': output, 'bounds': bounds,
                'point1': list(line.Point1), 'point2': list(line.Point2),
                'resolution': resolution, 'requested_max_level': level,
                'samples': line.GetDataInformation().GetNumberOfPoints(),
                'method': 'AMReX reader -> ExtractBlock /Root -> CellDataToPointData -> PlotOverLine'}
    except Exception:
        # 失败时只回收本次新建的管线，保留用户已有对象和状态。
        for proxy in reversed(created):
            pvs.Delete(proxy)
        raise
    finally:
        pvs.SetActiveSource(previous)


def add_vorticity_view(command):
    """先对三维速度求旋度，再切片显示；新布局不改用户原有视图。"""
    import math
    source_name = command.get('source')
    axis = command.get('slice_axis', 'y')
    component = command.get('component', 'Magnitude')
    color_range = command.get('color_range')
    if axis not in ('x', 'y', 'z') or component not in ('Magnitude', 'X', 'Y', 'Z'):
        raise ValueError('slice_axis must be x/y/z; component must be Magnitude/X/Y/Z')
    matches = [source for (name, _), source in pvs.GetSources().items() if name == source_name]
    if len(matches) != 1:
        raise ValueError('source must identify exactly one registered pipeline source')
    source = matches[0]
    previous_source, previous_view = pvs.GetActiveSource(), pvs.GetActiveView()
    current_time = previous_view.ViewTime if previous_view is not None else pvs.GetAnimationScene().AnimationTime
    created = []
    try:
        source.UpdatePipeline(time=current_time)
        bounds = list(source.GetDataInformation().GetBounds())
        if len(bounds) != 6 or any(not math.isfinite(v) for v in bounds) or any(
                bounds[2*i] >= bounds[2*i+1] for i in range(3)):
            raise ValueError('Vorticity requires a nonempty three-dimensional source')
        center = [(bounds[2*i] + bounds[2*i+1])/2 for i in range(3)]
        position = command.get('slice_position', center['xyz'.index(axis)])
        if type(position) not in (int, float) or not math.isfinite(position) or not (
                bounds[2*'xyz'.index(axis)] <= position <= bounds[2*'xyz'.index(axis)+1]):
            raise ValueError('slice_position must be inside source bounds')
        velocity_source = source
        if source.GetXMLName() == 'AMReXBoxLibGridReader':
            velocity_source = pvs.ExtractBlock(registrationName='vorticity_blocks', Input=source)
            created.append(velocity_source)
            velocity_source.Selectors = ['/Root']
            velocity_source.UpdatePipeline(time=current_time)
        names = {'ux', 'uy', 'uz'}
        if names.issubset(velocity_source.CellData.keys()):
            association, attribute_type = 'CELLS', 'Cell Data'
        elif names.issubset(velocity_source.PointData.keys()):
            association, attribute_type = 'POINTS', 'Point Data'
        else:
            raise ValueError('Enable ux, uy, uz together as cell or point arrays on the input reader')
        vector = pvs.Calculator(registrationName='vorticity_velocity_vector', Input=velocity_source)
        created.append(vector)
        vector.AttributeType = attribute_type
        vector.ResultArrayName = 'VelocityForVorticity'
        vector.Function = 'ux*iHat + uy*jHat + uz*kHat'
        gradient = pvs.Gradient(registrationName='Vorticity_3D', Input=vector)
        created.append(gradient)
        gradient.ScalarArray = [association, 'VelocityForVorticity']
        gradient.ComputeGradient = 0
        gradient.ComputeDivergence = 0
        gradient.ComputeVorticity = 1
        gradient.VorticityArrayName = 'Vorticity'
        gradient.ComputeQCriterion = 0
        gradient.UpdatePipeline(time=current_time)
        arrays = gradient.PointData if association == 'POINTS' else gradient.CellData
        if 'Vorticity' not in arrays.keys() or arrays['Vorticity'].GetNumberOfComponents() != 3:
            raise ValueError('Gradient did not generate a three-component Vorticity array')
        data_range = list(arrays['Vorticity'].GetRange(-1 if component == 'Magnitude' else 'XYZ'.index(component)))
        if not all(math.isfinite(v) for v in data_range):
            raise ValueError('Vorticity range is nonfinite')
        if color_range is not None:
            if (not isinstance(color_range, (list, tuple)) or len(color_range) != 2 or
                    any(type(v) not in (int, float) or not math.isfinite(v) for v in color_range) or
                    color_range[0] >= color_range[1]):
                raise ValueError('color_range must contain two finite increasing values')
            color_range = [float(v) for v in color_range]
            if color_range[0] == color_range[1]:
                padding = max(abs(color_range[0])*0.05, 1.0e-12)
                color_range = [color_range[0] - padding, color_range[1] + padding]
        elif data_range[0] == data_range[1]:
            padding = max(abs(data_range[0])*0.05, 1.0e-12)
            color_range = [data_range[0] - padding, data_range[1] + padding]
        section = pvs.Slice(registrationName='Vorticity_slice_' + axis, Input=gradient)
        created.append(section)
        section.SliceType = 'Plane'
        origin = center[:]
        origin['xyz'.index(axis)] = position
        normal = [0.0, 0.0, 0.0]
        normal['xyz'.index(axis)] = 1.0
        section.SliceType.Origin = origin
        section.SliceType.Normal = normal
        section.UpdatePipeline(time=current_time)
        if section.GetDataInformation().GetNumberOfPoints() == 0:
            raise ValueError('The selected slice is empty')
        layout = pvs.CreateLayout(name='Vorticity')
        created.append(layout)
        view = pvs.CreateView('RenderView')
        created.append(view)
        pvs.AssignViewToLayout(view=view, layout=layout, hint=0)
        view.ViewTime = current_time
        display = pvs.Show(section, view)
        created.append(display)
        pvs.ColorBy(display, (association, 'Vorticity', component))
        lut = display.LookupTable
        lut.ApplyPreset('Cool to Warm (Extended)', True)
        lut.RescaleTransferFunction(*(color_range if color_range is not None else data_range))
        display.SetScalarBarVisibility(view, True)
        scalar_bar = pvs.GetScalarBar(display.LookupTable, view)
        scalar_bar.Title = 'Vorticity = curl(u)'
        scalar_bar.ComponentTitle = component
        scalar_bar.Orientation = 'Horizontal'
        scalar_bar.WindowLocation = 'Lower Center'
        scalar_bar.TitleFontSize = 14
        scalar_bar.LabelFontSize = 12
        scalar_bar.ScalarBarLength = 0.72
        scalar_bar.Visibility = 1
        block_colors = pvs.GetColorTransferFunction('vtkBlockColors')
        block_bar = pvs.GetScalarBar(block_colors, view)
        block_bar.Visibility = 0
        plane_axes = [i for i in range(3) if i != 'xyz'.index(axis)]
        slice_width = bounds[2*plane_axes[0]+1] - bounds[2*plane_axes[0]]
        slice_height = bounds[2*plane_axes[1]+1] - bounds[2*plane_axes[1]]
        distance = 1.25*max(slice_width, slice_height)
        view.CameraFocalPoint = center
        view.CameraPosition = [center[i] - distance*normal[i] for i in range(3)]
        view.CameraViewUp = [0, 1, 0] if axis == 'z' else [0, 0, 1]
        view.CameraParallelProjection = 1
        view.CameraParallelScale = 0.52*max(slice_width, slice_height)
        pvs.SetActiveView(view)
        pvs.SetActiveSource(section)
        pvs.Render(view)
        return {'source': source_name, 'source_id': source.GetGlobalIDAsString(), 'time': current_time,
                'bounds': bounds, 'slice_axis': axis, 'slice_origin': origin, 'component': component,
                'velocity_association': association,
                'vorticity_range': data_range,
                'color_range': list(color_range if color_range is not None else data_range),
                'gradient': gradient.GetGlobalIDAsString(),
                'slice': section.GetGlobalIDAsString(), 'view': view.GetGlobalIDAsString(),
                'definition': 'curl(ux,uy,uz), physical coordinates; uses native cell arrays when available and computes the three-dimensional gradient before slicing',
                'limitations': 'Block-local derivatives near AMR interfaces remain a visualization approximation'}
    except Exception:
        for proxy in reversed(created):
            pvs.Delete(proxy)
        pvs.SetActiveSource(previous_source)
        pvs.SetActiveView(previous_view)
        raise


class ParaViewBridge:
    def prepare_reload(self, command):
        """后台只读取固定仓库文件；SHA256必须与投递端期望一致。"""
        import ast
        expected = command.get('expected_sha256', '')
        if not isinstance(expected, str) or len(expected) != 64 or any(
                c not in '0123456789abcdef' for c in expected):
            raise ValueError('reload_client requires a lowercase SHA256')
        source = self.remote('cat ' + shlex.quote(REMOTE_CLIENT_SOURCE))
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
        self.root = os.environ.get('PV_BRIDGE_ROOT',
            '/home/share/iosoeqkp/home/Liuzhaohui/.cache/paraview-bridge')
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

    def remote(self, command, data=None):
        # 网络调用只在后台线程运行，不触碰 ParaView 对象。
        completed = subprocess.run(self.ssh + [command], input=data, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, timeout=45,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        if completed.returncode:
            raise RuntimeError(completed.stderr.decode('utf-8', errors='replace').strip())
        return completed.stdout

    def poll(self):
        path = shlex.quote(self.directory)
        try:
            root = shlex.quote(self.root)
            self.remote('umask 077; mkdir -p ' + root + ' && mkdir -m 700 ' + path
                        + ' && date +%s > ' + path + '/heartbeat')
            if self.closed.is_set():
                return
            # 固定入口只由新会话启动时发布，旧会话心跳不会覆盖它。
            self.remote('umask 077; cat > ' + path + '/registration.tmp && mv '
                        + path + '/registration.tmp ' + root + '/current.json',
                        json.dumps({'directory': self.directory, 'protocol': 1, 'launch_id': self.launch_id,
                                    'version': BRIDGE_VERSION, 'source_sha256': self.source_sha256}).encode('utf-8'))
            print('ParaView bridge registered (GUI check pending): ' + self.directory)
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
        # 白名单只允许明确的数据导出和界面操作，不执行远端任意代码。
        action = command.get('action')
        if action == 'reload_client':
            return self.reload_client(command)
        if action == 'export_centerline':
            return export_centerline(command)
        if action == 'add_vorticity_view':
            return add_vorticity_view(command)
        if action == 'inspect':
            from paraview import servermanager
            connection = servermanager.ActiveConnection
            uri = connection.Session.GetURI() if connection is not None else None
            return {'capabilities': CAPABILITIES, 'version': BRIDGE_VERSION,
                    'source_sha256': getattr(self, 'source_sha256', None),
                    'connection_uri': uri,
                    'sources': [list(key) for key in pvs.GetSources()],
                    'source_details': [
                        {'name': name, 'id': key, 'type': source.GetXMLName(),
                         'files': list(source.FileNames) if source.GetProperty('FileNames') else []}
                        for (name, key), source in pvs.GetSources().items() if source is not None],
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
