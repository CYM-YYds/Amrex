"""论文/参考图风格的涡量切片排版与独立标题视图；不启动桥接、线程或计时器。

与 paraview_operations.py 同样由 build_paraview_client.py 嵌入客户端，
独立成模块是为了让出图版式的改动不与数据管线操作混在一起。
"""
import sys
from contextlib import contextmanager
from paraview import simple as pvs


@contextmanager
def _publication_edit(view):
    """为原位排版保存属性和对象归属；失败恢复原对象，不重载用户场景。"""
    from paraview import servermanager
    manager = servermanager.ProxyManager()
    groups = ('scalar_bars', 'representations', 'sources', 'lookup_tables', 'piecewise_functions')

    def registered():
        return {group: {proxy.GetGlobalIDAsString(): proxy
                        for proxy in manager.GetProxiesInGroup(group).values()}
                for group in groups}

    before = registered()
    layout = pvs.GetLayout(view)
    size = tuple(layout.GetSize()) if layout is not None else None
    active_source, active_view = pvs.GetActiveSource(), pvs.GetActiveView()
    proxies = [view, view.AxesGrid]
    if layout is not None:
        proxies.append(layout)
    for group in ('scalar_bars', 'representations', 'lookup_tables', 'piecewise_functions'):
        proxies.extend(before[group].values())
    # 仅这些已有数据节点会被排版修改，原始求解数据与过滤器不纳入可写范围。
    proxies.extend(proxy for (name, _), proxy in pvs.GetSources().items()
                   if name.startswith(('Publication_vorticity_magnitude', 'Publication_axis_')))
    snapshots = []
    for proxy in proxies:
        for name in proxy.ListProperties():
            prop = proxy.GetProperty(name).SMProperty
            if prop.IsA('vtkSMVectorProperty') and not prop.GetInformationOnly():
                saved = prop.NewInstance()
                saved.Copy(prop)
                unchecked = tuple(prop.GetUncheckedElement(i)
                                  for i in range(prop.GetNumberOfUncheckedElements()))
                snapshots.append((proxy, prop, saved, unchecked))
            elif name in ('LookupTable', 'ScalarOpacityFunction') and prop.IsA('vtkSMProxyProperty'):
                saved = prop.NewInstance()
                saved.Copy(prop)
                snapshots.append((proxy, prop, saved, None))
    try:
        yield
    except Exception as failure:
        errors = []
        # 继续尝试其余恢复项，避免一个属性失败阻断全部清理，并明确报告恢复失败。
        for proxy, prop, saved, unchecked in snapshots:
            try:
                # Copy 不会清空某些空向量，且不保留用户尚未 Apply 的值。
                if unchecked is not None:
                    prop.SetNumberOfElements(saved.GetNumberOfElements())
                prop.Copy(saved)
                if unchecked is not None:
                    prop.SetNumberOfUncheckedElements(len(unchecked))
                    for index, value in enumerate(unchecked):
                        prop.SetUncheckedElement(index, value)
            except Exception as exc:
                errors.append(str(exc))
        for proxy in proxies:
            try:
                proxy.UpdateVTKObjects()
            except Exception as exc:
                errors.append(str(exc))
        after = registered()
        for group in groups:
            for identity, proxy in reversed(list(after[group].items())):
                if identity not in before[group]:
                    try:
                        pvs.Delete(proxy)
                    except Exception as exc:
                        errors.append(str(exc))
        try:
            if layout is not None:
                layout.SetSize(*size)
            pvs.SetActiveSource(active_source)
            pvs.SetActiveView(active_view)
            pvs.Render(view)
        except Exception as exc:
            errors.append(str(exc))
        if errors:
            raise RuntimeError('Publication failed: %s; state restoration incomplete: %s'
                               % (failure, '; '.join(errors))) from failure
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
    requested_image_size = image_size
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
    with _publication_edit(view):
        if image_size:
            layout = pvs.GetLayout(view)
            if layout is None or len([v for v in pvs.GetViews() if pvs.GetLayout(v) == layout]) != 1:
                raise ValueError('Fixed preview requires a single-view layout')
            # GUI 恢复普通视图；以真实可用像素导出，不再开启会缩小画面的预览模式。
            layout.PreviewMode = [0, 0]
            layout.UpdateVTKObjects()
            if 'PythonQt' in sys.modules:
                from PythonQt import QtCore
                QtCore.QCoreApplication.processEvents()
                image_size = list(view.ViewSize)
            else:
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
        title_alignment = None
        if reference_style and image_size:
            from vtkmodules.vtkRenderingCore import vtkTextProperty, vtkTextRenderer
            # 用图框的屏幕投影和字体包围盒定位；四边统一为图框到文字边缘 40 像素。
            renderer = view.GetClientSideObject().GetRenderer()
            width, height = renderer.GetSize()
            projected = []
            for x in bounds[:2]:
                for y in bounds[2:4]:
                    for z in bounds[4:6]:
                        renderer.SetWorldPoint(x, y, z, 1)
                        renderer.WorldToDisplay()
                        projected.append(renderer.GetDisplayPoint())
            left, right = min(p[0] for p in projected), max(p[0] for p in projected)
            bottom, top = min(p[1] for p in projected), max(p[1] for p in projected)
            gap = 40
            boxes = []
            for side, rep in zip(('bottom', 'left', 'top', 'right'), label_displays):
                prop = vtkTextProperty()
                prop.SetFontFamilyToArial()
                prop.SetFontSize(int(rep.FontSize))
                prop.SetJustificationToLeft()
                prop.SetVerticalJustificationToBottom()
                box = [0, 0, 0, 0]
                if not vtkTextRenderer.GetInstance().GetBoundingBox(
                        prop, str(rep.Input.Text), box, renderer.GetRenderWindow().GetDPI()):
                    raise RuntimeError('Could not measure axis title')
                if rep.GetProperty('Justification') is not None:
                    rep.Justification = 'Left'
                if rep.GetProperty('VerticalJustification') is not None:
                    rep.VerticalJustification = 'Bottom'
                x = (left+right)/2-(box[0]+box[1])/2
                y = (bottom+top)/2-(box[2]+box[3])/2
                if side == 'bottom':
                    y = bottom-gap-box[3]
                elif side == 'top':
                    y = top+gap-box[2]
                elif side == 'left':
                    x = left-gap-box[1]
                else:
                    x = right+gap-box[0]
                rep.Position = [x/width, y/height]
                boxes.append([x+box[0], x+box[1], y+box[2], y+box[3]])
            title_alignment = {'frame_pixels': [left, right, bottom, top],
                               'edge_gap_pixels': gap, 'text_boxes_pixels': boxes,
                               'order': ['bottom', 'left', 'top', 'right']}
            pvs.Render(view)
        # 导出分辨率：显式尺寸优先，否则按样式取固定画布。
        export_resolution = list(image_size) if image_size else ([2400, 2640] if reference_style else [2400, 2400])
        if output:
            try:
                if reference_style and not image_size:
                    reference_text_scale(1.0)
                pvs.SaveScreenshot(output, view, ImageResolution=export_resolution,
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
                'image_resolution': export_resolution if output else None,
                'requested_image_size': list(requested_image_size) if requested_image_size else None,
                'matched_view_size': list(image_size) if image_size else None,
                'preview_mode': list(pvs.GetLayout(view).PreviewMode),
                'camera_parallel_scale': view.CameraParallelScale,
                'camera_focal_point': list(view.CameraFocalPoint),
                'axis_title_positions': [list(d.Position) for d in label_displays],
                'title_alignment': title_alignment,
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
