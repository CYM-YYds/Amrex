"""ParaView 数据管线与背景操作；不启动桥接、线程或计时器。出图排版见 paraview_publication.py。"""
from paraview import simple as pvs


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


def update_background(command, backgrounds):
    """背景操作；备份由桥接实例唯一持有并跨热更新保留。"""
    action = command.get('action')
    view = pvs.GetActiveView()
    if view is None or view.GetXMLName() != 'RenderView':
        raise ValueError('Select a Render View first')
    key = view.GetGlobalIDAsString()
    if action == 'background':
        color = command.get('color')
        if (not isinstance(color, list) or len(color) != 3
                or any(type(c) not in (int, float) or not 0 <= c <= 1 for c in color)):
            raise ValueError('color requires three numbers in [0, 1]')
        if key not in backgrounds:
            backgrounds[key] = (list(view.Background),
                                    view.GetProperty('BackgroundColorMode').SMProperty.GetElement(0))
        view.BackgroundColorMode = 'Single Color'
        view.Background = color
    elif key in backgrounds:
        color, mode = backgrounds[key]
        view.Background = color
        # 按底层枚举整数恢复，避免属性字符串表示带引号而无法赋回。
        view.GetProperty('BackgroundColorMode').SMProperty.SetElement(0, mode)
        view.UpdateVTKObjects()
        del backgrounds[key]
    pvs.Render(view)
    return {'view': key, 'background': list(view.Background)}


def render_diagnostics(view):
    """分别读取代理、客户端视图与实际 renderer 的 FXAA 状态。"""
    client = view.GetClientSideObject()
    renderers = []
    for role, method in [('main', 'GetRenderer'), ('overlay', 'GetNonCompositedRenderer')]:
        renderer = getattr(client, method)() if client is not None and hasattr(client, method) else None
        if renderer is not None and hasattr(renderer, 'GetUseFXAA'):
            renderers.append({'role': role, 'use_fxaa': bool(renderer.GetUseFXAA())})
    return {'proxy': int(view.UseFXAA) if view.GetProperty('UseFXAA') is not None else None,
            'client_view': bool(client.GetUseFXAA()) if client is not None and hasattr(client, 'GetUseFXAA') else None,
            'renderers': renderers}


def disable_fxaa(command):
    """只关闭现有视图的抗锯齿，保留管线、相机及显示对象。"""
    from vtkmodules.vtkCommonCore import vtkOutputWindow, vtkStringOutputWindow
    views = [v for v in pvs.GetViews() if v.GetXMLName() == 'RenderView'
             and (not command.get('view_id') or v.GetGlobalIDAsString() == command['view_id'])]
    if not views:
        raise ValueError('No matching RenderView')
    result = []
    for view in views:
        before = render_diagnostics(view)
        if view.GetProperty('UseFXAA') is not None:
            view.UseFXAA = 0
            view.UpdateVTKObjects()
        client = view.GetClientSideObject()
        if client is not None and hasattr(client, 'SetUseFXAA'):
            client.SetUseFXAA(False)
        for method in ('GetRenderer', 'GetNonCompositedRenderer'):
            renderer = getattr(client, method)() if client is not None and hasattr(client, method) else None
            if renderer is not None and hasattr(renderer, 'SetUseFXAA'):
                renderer.SetUseFXAA(False)
        # 仅捕获本次客户端重绘的新消息，结束后恢复 GUI 原有输出窗口。
        previous = vtkOutputWindow.GetInstance()
        capture = vtkStringOutputWindow()
        vtkOutputWindow.SetInstance(capture)
        try:
            pvs.Render(view)
            messages = capture.GetOutput()
        finally:
            vtkOutputWindow.SetInstance(previous)
        result.append({'view': view.GetGlobalIDAsString(), 'before': before,
                       'after': render_diagnostics(view), 'render_messages': messages,
                       'fxaa_warning_seen': 'vtkOpenGLFXAAFilter' in messages})
    return {'views': result}
