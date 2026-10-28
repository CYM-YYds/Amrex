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


BRIDGE_VERSION = '13'
RUNTIME_UPDATE_PROTOCOL = 1
REMOTE_CLIENT_SOURCE = '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/.agents/skills/paraview/scripts/paraview_bridge_client.py'
CAPABILITIES = ['inspect', 'background', 'restore_background', 'export_centerline',
                'add_vorticity_view', 'repair_vorticity_slice', 'publication_vorticity', 'add_axis_title_view', 'reload_client']

# 代码随 ProgrammableFilter 在数据所在 rank 执行；不依赖 Windows 上的仓库模块路径。
# 仅在求导副本中解除细层覆盖，输出恢复原掩膜，避免改动输入或重复显示粗细层。
AMR_CELL_VORTICITY_SCRIPT = '''
from vtkmodules.vtkCommonDataModel import vtkDataObject, vtkDataSetAttributes
from vtkmodules.vtkFiltersGeneral import vtkGradientFilter
from vtkmodules.util.numpy_support import vtk_to_numpy, numpy_to_vtk

def differentiate(block):
    work = block.NewInstance()
    work.ShallowCopy(block)
    ghosts = block.GetCellData().GetArray('vtkGhostType')
    if block.IsA('vtkUniformGrid') and ghosts is not None:
        flags = vtk_to_numpy(ghosts)
        cleared = numpy_to_vtk(flags & (255 ^ vtkDataSetAttributes.REFINEDCELL), deep=True)
        cleared.SetName('vtkGhostType')
        work.GetCellData().AddArray(cleared)
    gradient = vtkGradientFilter()
    gradient.SetInputData(work)
    gradient.SetInputScalars(vtkDataObject.FIELD_ASSOCIATION_CELLS, 'VelocityForVorticity')
    gradient.SetComputeGradient(False)
    gradient.SetComputeVorticity(True)
    gradient.SetVorticityArrayName('Vorticity')
    gradient.Update()
    result = block.NewInstance()
    result.ShallowCopy(gradient.GetOutput())
    if ghosts is not None:
        result.GetCellData().AddArray(ghosts)
    return result

source = self.GetInputDataObject(0, 0)
output = self.GetOutputDataObject(0)
output.ShallowCopy(source)
if source.IsA('vtkCompositeDataSet'):
    iterator = source.NewIterator()
    iterator.InitTraversal()
    while not iterator.IsDoneWithTraversal():
        block = iterator.GetCurrentDataObject()
        if block is not None and block.IsA('vtkDataSet'):
            output.SetDataSet(iterator, differentiate(block))
        iterator.GoToNextItem()
elif source.IsA('vtkDataSet'):
    output.ShallowCopy(differentiate(source))
'''

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
        if association == 'CELLS' and source.GetXMLName() in ('AMReXBoxLibGridReader', 'AMReXGridReader'):
            gradient = pvs.ProgrammableFilter(registrationName='Vorticity_3D', Input=vector)
            created.append(gradient)
            gradient.Script = AMR_CELL_VORTICITY_SCRIPT
        else:
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
                'limitations': 'AMReX cell derivatives use stored covered coarse velocities with refinement blanking temporarily cleared and restored on output; missing block halos and coarse/fine discretization remain limitations'}
    except Exception:
        for proxy in reversed(created):
            pvs.Delete(proxy)
        pvs.SetActiveSource(previous_source)
        pvs.SetActiveView(previous_view)
        raise


def repair_vorticity_slice(command):
    """只替换指定切片的旋度输入，复用求导修复并保留已有显示对象。"""
    name = command.get('source', '')
    matches = [s for (n, _), s in pvs.GetSources().items() if n == name]
    if len(matches) != 1 or matches[0].GetXMLName() != 'Cut':
        raise ValueError('source must identify exactly one existing Slice')
    section = matches[0]
    old = section.GetProperty('Input')[0]
    if old.GetXMLName() != 'Gradient':
        raise ValueError('The existing Slice must directly follow a native Gradient; no input was changed')
    association, array_name = list(old.ScalarArray)
    if association != 'CELLS':
        raise ValueError('This repair requires three-dimensional cell velocity')
    velocity = old.GetProperty('Input')[0]
    views = [v for v in pvs.GetViews() if v.GetXMLName() == 'RenderView'
             and v.SMProxy.FindRepresentation(section.SMProxy, 0) is not None]
    current_time = views[0].ViewTime if views else pvs.GetAnimationScene().AnimationTime
    velocity.UpdatePipeline(time=current_time)
    bounds = list(velocity.GetDataInformation().GetBounds())
    if any(bounds[2*i] >= bounds[2*i+1] for i in range(3)):
        raise ValueError('Velocity input is not three-dimensional; compute curl before slicing')
    if array_name not in velocity.CellData.keys() or velocity.CellData[array_name].GetNumberOfComponents() != 3:
        raise ValueError('Gradient input must contain its selected three-component cell vector')
    output_name = str(old.VorticityArrayName)
    # 只替换固定脚本中的数组名字，不执行命令传入的代码。
    script = AMR_CELL_VORTICITY_SCRIPT.replace("'VelocityForVorticity'", repr(array_name))
    script = script.replace("'Vorticity'", repr(output_name))
    fixed = pvs.ProgrammableFilter(registrationName=name + '_AMR_vorticity', Input=velocity)
    fixed.Script = script
    try:
        fixed.UpdatePipeline(time=current_time)
        if output_name not in fixed.CellData.keys():
            raise ValueError('Corrected filter did not generate the requested vorticity')
        import math
        data_range = list(fixed.CellData[output_name].GetRange(-1))
        if not all(math.isfinite(v) for v in data_range):
            raise ValueError('Corrected vorticity has a nonfinite range')
        section.Input = fixed
        section.UpdatePipeline(time=current_time)
        for view in views:
            pvs.Render(view)
        return {'source': name, 'slice': section.GetGlobalIDAsString(), 'time': current_time,
                'previous_gradient': old.GetGlobalIDAsString(), 'gradient': fixed.GetGlobalIDAsString(),
                'velocity_input': velocity.GetGlobalIDAsString(), 'velocity_array': array_name,
                'vorticity_array': output_name, 'vorticity_range': data_range,
                'views': [v.GetGlobalIDAsString() for v in views],
                'preserved': 'existing Slice, plane, displays, cameras and color maps; original Gradient retained'}
    except Exception:
        section.Input = old
        pvs.Delete(fixed)
        raise


def publication_vorticity(command):
    """为已有涡量切片添加可追溯的显示标量与排版，不重新求导或平滑。"""
    import math
    from paraview import servermanager
    source_name = command.get('source')
    style = command.get('style', 'publication')
    if style not in ('publication', 'reference'):
        raise ValueError('style must be publication or reference')
    reference_style = style == 'reference'
    image_size = command.get('image_size')
    if image_size is not None and (not isinstance(image_size, (list, tuple)) or len(image_size) != 2
            or any(type(v) is not int or not 256 <= v <= 8192 for v in image_size)):
        raise ValueError('image_size must contain two integers between 256 and 8192')
    matches = [s for (name, _), s in pvs.GetSources().items() if name == source_name]
    if len(matches) != 1:
        raise ValueError('source must identify exactly one registered slice')
    source = matches[0]
    if source.GetXMLName() != 'Cut':
        raise ValueError('Select a planar Vorticity slice')
    publications = [s for (name, _), s in pvs.GetSources().items()
                    if name.startswith('Publication_vorticity_magnitude') and
                    s.GetXMLName() == 'Calculator' and s.Input == source]
    if len(publications) > 1:
        raise ValueError('Multiple publication scalars exist; select an unambiguous slice')
    visible_source = publications[0] if publications else source
    scale = command.get('vorticity_scale', 1.0)
    limits = command.get('color_range')
    if type(scale) not in (int, float) or not math.isfinite(scale) or scale <= 0:
        raise ValueError('vorticity_scale must be finite and positive (H/Uref)')
    if (not isinstance(limits, (list, tuple)) or len(limits) != 2 or
            any(type(v) not in (int, float) or not math.isfinite(v) for v in limits) or
            not 0 <= limits[0] < limits[1]):
        raise ValueError('color_range must be nonnegative and increasing')
    output = command.get('output')
    if output and (not isinstance(output, str) or not output.startswith('/') or
                   not output.lower().endswith('.png')):
        raise ValueError('output must be an absolute server PNG path')
    # 只使用已有可见视图，避免无意修改同一切片的隐藏预览。
    candidates = [(v, d) for v in pvs.GetViews() if v.GetXMLName() == 'RenderView'
                  for d in v.Representations if d.GetProperty('Input') and d.Input in ([source] + publications)]
    candidates = [(v, d) for v, d in candidates if d.Visibility]
    if command.get('view_id'):
        candidates = [(v, d) for v, d in candidates if v.GetGlobalIDAsString() == command['view_id']]
    active = pvs.GetActiveView()
    candidates = [pair for pair in candidates if pair[0] == active] or candidates
    if len(candidates) > 1 and len({v.GetGlobalIDAsString() for v, _ in candidates}) == 1:
        candidates = [next((pair for pair in candidates if pair[1].Input == visible_source), candidates[0])]
    if len(candidates) != 1:
        raise ValueError('Select the visible Vorticity slice view first')
    view, original_display = candidates[0]
    if image_size:
        layout = pvs.GetLayout(view)
        if layout is None or len([v for v in pvs.GetViews() if pvs.GetLayout(v) == layout]) != 1:
            raise ValueError('Fixed preview requires a single-view layout')
        layout.PreviewMode = list(image_size)
        layout.SetSize(*image_size)
    normal = list(source.SliceType.Normal)
    axis = max(range(3), key=lambda i: abs(normal[i]))
    if abs(abs(normal[axis]) - 1) > 1e-8 or any(abs(normal[i]) > 1e-8 for i in range(3) if i != axis):
        raise ValueError('Publication layout requires an axis-aligned slice')
    source.UpdatePipeline(time=view.ViewTime)
    association = 'CELLS' if 'Vorticity' in source.CellData.keys() else 'POINTS'
    if 'Vorticity' not in (source.CellData if association == 'CELLS' else source.PointData).keys():
        raise ValueError('The slice must contain Vorticity')
    scalar = publications[0] if publications else pvs.Calculator(
        registrationName='Publication_vorticity_magnitude', Input=source)
    scalar.AttributeType = 'Cell Data' if association == 'CELLS' else 'Point Data'
    scalar.ResultArrayName = 'PublicationVorticityMagnitude'
    scalar.Function = 'mag(Vorticity)*' + repr(float(scale))
    scalar.GetProperty('Function').SMProperty.SetUncheckedElement(0, str(scalar.Function))
    scalar.UpdatePipeline(time=view.ViewTime)
    values = scalar.CellData if association == 'CELLS' else scalar.PointData
    data_range = list(values['PublicationVorticityMagnitude'].GetRange())
    if not all(math.isfinite(x) for x in data_range):
        pvs.Delete(scalar)
        raise ValueError('Nonfinite scaled vorticity')
    original_display.SetScalarBarVisibility(view, False)
    pvs.Hide(source, view)
    display = pvs.Show(scalar, view)
    hidden_overlays = []
    if reference_style:
        for representation in list(view.Representations):
            if (representation.GetProperty('Input') and representation.Visibility
                    and representation.Input != scalar
                    and (image_size or representation.Input.GetXMLName() != 'TextSource')):
                representation.Visibility = 0
                hidden_overlays.append(representation.Input.GetGlobalIDAsString())
    display.Representation = 'Surface'
    display.Ambient = 1
    display.Diffuse = 0
    pvs.ColorBy(display, (association, 'PublicationVorticityMagnitude'))
    lut = display.LookupTable
    # 与用户参考图一致的蓝、青、绿、黄、红色带。
    lut.ColorSpace = 'RGB'
    lo, hi = limits
    lut.RGBPoints = [lo, 0, 0, 1, lo+(hi-lo)*0.25, 0, 1, 1,
                     lo+(hi-lo)*0.5, 0, 1, 0, lo+(hi-lo)*0.75, 1, 1, 0, hi, 1, 0, 0]
    lut.RescaleTransferFunction(lo, hi)
    lut.AutomaticRescaleRangeMode = 'Never'
    display.SetScalarBarVisibility(view, True)
    bar = pvs.GetScalarBar(lut, view)
    bar.Title = 'Vorticity magnitude' if scale == 1 else 'Vorticity magnitude H/Uref'
    bar.ComponentTitle = ''
    bar.WindowLocation = 'Any Location'
    bar.Orientation = 'Horizontal'
    bar.Position = [0.15, 0.025] if reference_style else [0.22, 0.045]
    bar.ScalarBarLength = 0.70 if reference_style else 0.56
    bar.ScalarBarThickness = 40 if reference_style else 24
    font = 'Arial' if reference_style else 'Times'
    bar.TitleFontFamily = font
    bar.LabelFontFamily = font
    bar.TitleFontSize = 36 if reference_style else 24
    bar.LabelFontSize = 32 if reference_style else 22
    bar.TitleColor = [0, 0, 0]
    bar.LabelColor = [0, 0, 0]
    bar.TitleBold = 0
    bar.TitleItalic = 0
    bar.TitleShadow = 0
    bar.LabelBold = 0
    bar.LabelItalic = 0
    bar.LabelShadow = 0
    bar.UseCustomLabels = 1
    bar.AutomaticLabelFormat = 0 if reference_style else 1
    bar.CustomLabels = [lo, hi] if reference_style else [lo + (hi-lo)*i/5 for i in range(6)]
    bar.RangeLabelFormat = '{:.2f}' if reference_style else '%.2g'
    bar.LabelFormat = '{:.2f}' if reference_style else '%.2g'
    bar.AddRangeLabels = 0
    view.BackgroundColorMode = 'Single Color'
    view.UseColorPaletteForBackground = 0
    view.Background = [1, 1, 1]
    view.OrientationAxesVisibility = 0
    view.CameraParallelProjection = 1
    bounds = list(source.GetDataInformation().GetBounds())
    center = [(bounds[2*i]+bounds[2*i+1])/2 for i in range(3)]
    span = max(bounds[2*i+1]-bounds[2*i] for i in range(3))
    up = [0, 1, 0] if axis == 2 else [0, 0, 1]
    camera_center = [center[i] - (0.072*span*up[i] if reference_style else 0) for i in range(3)]
    view.CameraFocalPoint = camera_center
    # y 截面从负 y 方向看，使 x 向右、z 向上。
    direction = -1 if axis == 1 else 1
    view.CameraPosition = [camera_center[i] + (direction*2*span if i == axis else 0) for i in range(3)]
    view.CameraViewUp = up
    view.CameraParallelScale = 0.72*span
    grid = view.AxesGrid
    grid.Visibility = 1
    grid.ShowGrid = 0
    grid.ShowEdges = 1
    grid.ShowTicks = 1
    grid.GridColor = [0, 0, 0]
    grid.XTitle, grid.YTitle, grid.ZTitle = '', '', ''
    for letter in 'XYZ':
        for kind in ('Title', 'Label'):
            setattr(grid, letter+kind+'Color', [0, 0, 0])
            setattr(grid, letter+kind+'FontFamily', font)
            setattr(grid, letter+kind+'FontSize', (36 if kind == 'Title' else 32)
                    if reference_style else (24 if kind == 'Title' else 22))
            for flag in ('Bold', 'Italic', 'Shadow'):
                setattr(grid, letter+kind+flag, 0)
        setattr(grid, letter+'AxisUseCustomLabels', 1)
        index = 'XYZ'.index(letter)
        divisions = 10 if reference_style else 5
        setattr(grid, letter+'AxisLabels', [bounds[2*index]+(bounds[2*index+1]-bounds[2*index])*i/divisions
                                           for i in range(divisions+1)])
    # 独立二维文字避免高分辨率截图时 CubeAxes 轴标题被自动缩成不可读大小。
    plane_axes = [i for i in range(3) if i != axis]
    labels = [(plane_axes[0], [0.48, 0.105], ''), (plane_axes[1], [0.065, 0.49], '')]
    if reference_style:
        labels = [(plane_axes[0], [0.48, 0.14], ''), (plane_axes[1], [0.035, 0.54], ''),
                  (plane_axes[0], [0.48, 0.945], '_top'), (plane_axes[1], [0.93, 0.54], '_right')]
        if image_size:
            # 横画布保留左右留白，标题与色标围绕真实的正方形图面排布。
            fraction = image_size[1]/(1.44*image_size[0])
            left, right = 0.5-fraction/2, 0.5+fraction/2
            labels = [(plane_axes[0], [0.49, 0.14], ''),
                      (plane_axes[1], [left-0.04, 0.54], ''),
                      (plane_axes[0], [0.49, 0.945], '_top'),
                      (plane_axes[1], [right+0.025, 0.54], '_right')]
            bar.Position = [0.5-0.35*fraction, 0.025]
            bar.ScalarBarLength = 0.70*fraction
    else:
        for index, suffix in ((plane_axes[0], '_top'), (plane_axes[1], '_right')):
            previous_label = pvs.FindSource('Publication_axis_' + str(index) + '_' + view.GetGlobalIDAsString() + suffix)
            if previous_label is not None:
                pvs.Hide(previous_label, view)
    label_displays = []
    for index, position, suffix in labels:
        label_name = 'Publication_axis_' + str(index) + '_' + view.GetGlobalIDAsString() + suffix
        label = pvs.FindSource(label_name)
        if label is None:
            label = pvs.Text(registrationName=label_name)
        unit_extent = abs(bounds[2*index]) < 1e-8 and abs(bounds[2*index+1]-1) < 1e-8
        label.Text = 'xyz'[index] + ('/H' if unit_extent else '')
        label_display = pvs.Show(label, view)
        label_display.WindowLocation = 'Any Location'
        label_display.Position = position
        label_display.FontFamily = font
        label_display.FontSize = 36 if reference_style else 24
        label_display.Color = [0, 0, 0]
        label_displays.append(label_display)
    # GUI 使用窗口适配的小字号；导出临时采用固定高清字号，之后恢复 GUI，避免标签挤占窗口。
    def reference_text_scale(scale):
        bar.TitleFontSize = max(8, round(36*scale))
        bar.LabelFontSize = max(8, round(32*scale))
        bar.ScalarBarThickness = max(8, round(40*scale))
        for letter in 'XYZ':
            setattr(grid, letter+'TitleFontSize', max(8, round(36*scale)))
            setattr(grid, letter+'LabelFontSize', max(8, round(32*scale)))
        for label_display in label_displays:
            label_display.FontSize = max(8, round(36*scale))
    gui_text_scale = min(1.0, max(0.35, min(view.ViewSize[0]/2400, view.ViewSize[1]/2640))) if reference_style else 1.0
    if reference_style:
        reference_text_scale(gui_text_scale)
    pvs.SetActiveView(view)
    pvs.SetActiveSource(scalar)
    pvs.Render(view)
    if output:
        try:
            if reference_style and not image_size:
                reference_text_scale(1.0)
            pvs.SaveScreenshot(output, view, ImageResolution=list(image_size) if image_size else ([2400, 2640] if reference_style else [2400, 2400]),
                               FontScaling='Do not scale fonts',
                               location=servermanager.vtkPVSession.DATA_SERVER)
        finally:
            if reference_style and not image_size:
                reference_text_scale(gui_text_scale)
                pvs.Render(view)
    return {'source': source_name, 'view': view.GetGlobalIDAsString(), 'time': view.ViewTime,
            'scalar': scalar.GetGlobalIDAsString(), 'vorticity_scale': scale,
            'scaled_range': data_range, 'color_range': list(limits), 'slice_normal': normal,
            'slice_origin': list(source.SliceType.Origin), 'output': output,
            'style': style, 'hidden_overlays': hidden_overlays,
            'gui_view_size': list(view.ViewSize), 'gui_text_scale': gui_text_scale,
            'image_resolution': (list(image_size) if image_size else ([2400, 2640] if reference_style else [2400, 2400])) if output else None,
            'fixed_preview': list(image_size) if image_size else None,
            'preview_mode': list(pvs.GetLayout(view).PreviewMode),
            'camera_parallel_scale': view.CameraParallelScale,
            'camera_focal_point': list(view.CameraFocalPoint),
            'axis_title_positions': [list(d.Position) for d in label_displays],
            'colorbar': {'position': list(bar.Position), 'length': bar.ScalarBarLength,
                         'thickness': bar.ScalarBarThickness, 'title_font_size': bar.TitleFontSize,
                         'label_font_size': bar.LabelFontSize},
            'clipping': 'Values above color maximum saturate; no numerical smoothing applied'}


def add_axis_title_view(command):
    """复制显示配置到独立视图，仅在新视图用 Text 管线替代 Z 标题。"""
    import math
    from paraview import servermanager
    name = command.get('source', '')
    matches = [s for (n, _), s in pvs.GetSources().items() if n == name]
    if len(matches) != 1 or matches[0].GetXMLName() != 'Cut':
        raise ValueError('source must identify exactly one Slice')
    source = matches[0]
    reset_id = command.get('reset_view')
    if reset_id:
        target = next((v for v in pvs.GetViews() if v.GetGlobalIDAsString() == reset_id), None)
        originals = [v for v in pvs.GetViews() if v != target and v.GetXMLName() == 'RenderView'
                     and any(r.GetProperty('Input') and r.Input == source and r.Visibility
                             for r in v.Representations)]
        if target is None or len(originals) != 1:
            raise ValueError('Reset requires one independent view and one original Slice view')
        labels = [s for (n, _), s in pvs.GetSources().items()
                  if n in [name + '_Z_title_' + side + '_' + reset_id for side in ('left', 'right')]]
        if len(labels) != 2:
            raise ValueError('Independent title nodes not found')
        # 原页面也可能已被拖动；按切片几何居中，不能把当前原相机当作历史快照。
        source.UpdatePipeline(time=target.ViewTime)
        bounds = list(source.GetDataInformation().GetBounds())
        center = [(bounds[2*i]+bounds[2*i+1])/2 for i in range(3)]
        normal = list(source.SliceType.Normal)
        length = math.sqrt(sum(v*v for v in normal))
        diagonal = math.sqrt(sum((bounds[2*i+1]-bounds[2*i])**2 for i in range(3)))
        target.CameraFocalPoint = center
        target.CenterOfRotation = center
        target.CameraPosition = [center[i]-2*diagonal*normal[i]/length for i in range(3)]
        target.CameraViewUp = [0, 0, 1] if abs(normal[2]/length) < 0.9 else [0, 1, 0]
        target.CameraParallelProjection = 1
        target.CameraParallelScale = diagonal/2
        pvs.SetActiveView(target)
        pvs.Render(target)
        renderer = target.GetClientSideObject().GetRenderer()
        width, height = renderer.GetSize()
        points = []
        for x in bounds[:2]:
            for y in bounds[2:4]:
                for z in bounds[4:6]:
                    renderer.SetWorldPoint(x, y, z, 1)
                    renderer.WorldToDisplay()
                    points.append(renderer.GetDisplayPoint())
        x0, x1 = min(p[0] for p in points)/width, max(p[0] for p in points)/width
        ymid = (min(p[1] for p in points)+max(p[1] for p in points))/(2*height)
        for label in labels:
            rep = pvs.GetDisplayProperties(label, target)
            font = int(rep.FontSize)
            gap = max(28, 2.5*int(target.AxesGrid.ZLabelFontSize))/width
            tw = 0.65*font*len(str(label.Text))/width
            left = '_left_' in next(n for (n, _), s in pvs.GetSources().items() if s == label)
            rep.Position = [max(0.01, x0-gap-tw) if left else min(0.99-tw, x1+gap),
                            ymid-font/(2*height)]
        pvs.Render(target)
        output = command.get('output')
        size = list(target.ViewSize)
        if output:
            if not isinstance(output, str) or not output.startswith('/') or not output.endswith('.png'):
                raise ValueError('output must be an absolute PNG path')
            pvs.SaveScreenshot(output, target, ImageResolution=size,
                               location=servermanager.vtkPVSession.DATA_SERVER)
        return {'view': reset_id, 'camera_restored_from': 'slice_bounds',
                'camera_position': list(target.CameraPosition),
                'parallel_scale': target.CameraParallelScale, 'view_size': size,
                'output_resolution': size, 'output': output}
    candidates = [(v, r) for v in pvs.GetViews() if v.GetXMLName() == 'RenderView'
                  for r in v.Representations if r.GetProperty('Input') and r.Input == source and r.Visibility]
    active = pvs.GetActiveView()
    candidates = [pair for pair in candidates if pair[0] == active] or candidates
    if len(candidates) != 1:
        raise ValueError('Select the existing visible Slice view first')
    old_view, old_display = candidates[0]
    old_grid = old_view.AxesGrid
    view_properties = ['ViewTime', 'Background', 'Background2', 'BackgroundColorMode',
                       'UseColorPaletteForBackground', 'OrientationAxesVisibility', 'CameraPosition',
                       'CameraFocalPoint', 'CameraViewUp', 'CameraParallelProjection', 'CameraParallelScale',
                       'CenterOfRotation', 'InteractionMode']

    def values(proxy, names):
        return {key: proxy.GetProperty(key).GetData() for key in names
                if proxy.GetProperty(key) is not None
                and hasattr(proxy.GetProperty(key), 'GetData')
                and not proxy.GetProperty(key).SMProperty.GetInformationOnly()
                and not isinstance(proxy.GetProperty(key), servermanager.ProxyProperty)}

    # 原视图只读取，轴配置逐项比较；新的颜色映射独立，避免改动共享 LUT。
    def snapshot():
        proxies = [old_view, old_grid, old_display]
        if old_display.GetProperty('LookupTable') and old_display.LookupTable:
            proxies.append(old_display.LookupTable)
        return tuple(values(proxy, view_properties if proxy == old_view else proxy.ListProperties())
                     for proxy in proxies)

    before = snapshot()
    def restore_original_lut():
        # Show 的自动着色可能先重标定共享色带，立即恢复用户原值。
        if len(before) > 3:
            lut = old_display.LookupTable
            for key, value in before[3].items():
                if lut.GetProperty(key).GetData() != value:
                    lut.GetProperty(key).SetData(value)

    layout = pvs.CreateLayout(name=name + '_independent_axis_titles')
    view = pvs.CreateView('RenderView')
    created = []
    try:
        pvs.AssignViewToLayout(view=view, layout=layout, hint=0)
        for key, value in before[0].items():
            view.GetProperty(key).SetData(value)
        for key, value in before[1].items():
            view.AxesGrid.GetProperty(key).SetData(value)
        title = str(old_grid.ZTitle).strip() or 'Z'
        view.AxesGrid.ZTitle = ''
        display = pvs.Show(source, view, UseSeparateColorMap=True)
        restore_original_lut()
        for key in ('Representation', 'Ambient', 'Diffuse', 'Opacity', 'DiffuseColor',
                    'AmbientColor', 'LineWidth', 'PointSize', 'InterpolateScalarsBeforeMapping'):
            if old_display.GetProperty(key) is not None:
                setattr(display, key, old_display.GetProperty(key).GetData())
        color = list(old_display.ColorArrayName)
        if len(color) >= 2 and color[1]:
            pvs.ColorBy(display, tuple(color), separate=True)
            for key, value in values(old_display.LookupTable, old_display.LookupTable.ListProperties()).items():
                if display.LookupTable.GetProperty(key) is not None:
                    display.LookupTable.GetProperty(key).SetData(value)
            original_bars = [r for r in old_view.Representations if r.GetProperty('Title')
                             and r.GetProperty('LookupTable') and r.LookupTable == old_display.LookupTable and r.Visibility]
            if original_bars:
                bar = pvs.GetScalarBar(display.LookupTable, view)
                bar_properties = ['Title', 'ComponentTitle', 'Orientation', 'WindowLocation',
                                  'Position', 'ScalarBarLength', 'ScalarBarThickness', 'TitleColor',
                                  'LabelColor', 'TitleFontFamily', 'LabelFontFamily', 'TitleFontSize',
                                  'LabelFontSize', 'AutomaticLabelFormat', 'LabelFormat',
                                  'RangeLabelFormat', 'UseCustomLabels', 'CustomLabels',
                                  'DrawTickLabels', 'DrawAnnotations', 'DrawNanAnnotation']
                for key, value in values(original_bars[0], bar_properties).items():
                    if bar.GetProperty(key) is not None:
                        bar.GetProperty(key).SetData(value)
                bar.Visibility = 1
        pvs.SetActiveView(view)
        pvs.Render(view)
        source.UpdatePipeline(time=view.ViewTime)
        bounds = list(source.GetDataInformation().GetBounds())
        renderer = view.GetClientSideObject().GetRenderer()
        width, height = renderer.GetSize()
        projected = []
        for x in bounds[0:2]:
            for y in bounds[2:4]:
                for z in bounds[4:6]:
                    renderer.SetWorldPoint(x, y, z, 1)
                    renderer.WorldToDisplay()
                    projected.append(renderer.GetDisplayPoint())
        x0, x1 = min(p[0] for p in projected)/width, max(p[0] for p in projected)/width
        ymid = (min(p[1] for p in projected)+max(p[1] for p in projected))/(2*height)
        font = max(12, min(18, int(old_grid.ZLabelFontSize)))
        text_width = 0.65*font*len(title)/width
        gap = max(28, 2.5*int(old_grid.ZLabelFontSize))/width
        positions = [[max(0.01, x0-gap-text_width), ymid-font/(2*height)],
                     [min(0.99-text_width, x1+gap), ymid-font/(2*height)]]
        for side, position in zip(('left', 'right'), positions):
            label = pvs.Text(registrationName=name + '_Z_title_' + side + '_' + view.GetGlobalIDAsString())
            created.append(label)
            label.Text = title
            label_display = pvs.Show(label, view)
            label_display.WindowLocation = 'Any Location'
            label_display.Position = position
            label_display.FontFamily = 'Arial'
            label_display.FontSize = font
            label_display.Color = list(old_grid.ZTitleColor)
        pvs.Render(view)
        after = snapshot()
        if before != after:
            changes = [(i, key, value, after[i].get(key)) for i, item in enumerate(before)
                       for key, value in item.items() if value != after[i].get(key)]
            raise RuntimeError('Original view changed unexpectedly: ' + repr(changes))
        pvs.SetActiveSource(created[0])
        output = command.get('output')
        if output:
            if not isinstance(output, str) or not output.startswith('/') or not output.endswith('.png'):
                raise ValueError('output must be an absolute PNG path')
            pvs.SaveScreenshot(output, view, ImageResolution=list(view.ViewSize),
                               location=servermanager.vtkPVSession.DATA_SERVER)
        return {'source': name, 'original_view': old_view.GetGlobalIDAsString(),
                'view': view.GetGlobalIDAsString(), 'layout': layout.GetGlobalIDAsString(),
                'original_preserved': True, 'title': title, 'positions': positions,
                'text_nodes': [s.GetGlobalIDAsString() for s in created], 'view_size': list(view.ViewSize),
                'time': view.ViewTime, 'output': output}
    except Exception:
        restore_original_lut()
        for proxy in reversed(created):
            pvs.Delete(proxy)
        pvs.Delete(view)
        pvs.RemoveLayout(layout)
        pvs.SetActiveView(old_view)
        pvs.SetActiveSource(source)
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
        if action == 'repair_vorticity_slice':
            return repair_vorticity_slice(command)
        if action == 'publication_vorticity':
            return publication_vorticity(command)
        if action == 'add_axis_title_view':
            return add_axis_title_view(command)
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
                         'level': int(source.Level) if source.GetProperty('Level') else None,
                         'function': str(source.Function) if source.GetProperty('Function') else None,
                         'unchecked_function': source.GetProperty('Function').SMProperty.GetUncheckedElement(0)
                                               if source.GetProperty('Function') and source.GetProperty('Function').SMProperty.GetNumberOfUncheckedElements() else None,
                         'inputs': [source.GetProperty('Input').SMProperty.GetProxy(i).GetGlobalIDAsString()
                                    for i in range(source.GetProperty('Input').SMProperty.GetNumberOfProxies())]
                                   if source.GetProperty('Input') else [],
                         'files': list(source.FileNames) if source.GetProperty('FileNames') else []}
                        for (name, key), source in pvs.GetSources().items() if source is not None],
                    'views': [v.GetXMLName() for v in pvs.GetViews()],
                    'view_details': [
                        {'id': v.GetGlobalIDAsString(), 'size': list(v.ViewSize),
                         'visible_sources': [r.GetProperty('Input').SMProperty.GetProxy(0).GetGlobalIDAsString()
                                             for r in v.Representations if r.GetProperty('Input')
                                             and r.GetProperty('Input').SMProperty.GetNumberOfProxies() and r.Visibility]}
                        for v in pvs.GetViews() if v.GetXMLName() == 'RenderView']}
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
