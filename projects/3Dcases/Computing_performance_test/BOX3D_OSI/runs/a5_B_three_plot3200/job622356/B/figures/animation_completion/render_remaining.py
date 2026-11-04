# 本次动画补帧临时入口；复现副本保存在结果目录后可删除。
import argparse
import json
import time
import socket
import resource
from pathlib import Path
from paraview import simple as pvs, servermanager

parser = argparse.ArgumentParser()
parser.add_argument('--state', required=True)
parser.add_argument('--output', required=True)
parser.add_argument('--start', type=int, required=True)
parser.add_argument('--stop', type=int, required=True)
parser.add_argument('--view-scale', type=float, default=0.5, help='逻辑视图相对输出尺寸；1为原尺寸渲染')
parser.add_argument('--width', type=int, default=2080)
parser.add_argument('--height', type=int, default=1066)
parser.add_argument('--view-source', help='用唯一可见源选择视图，保留该视图显示设置')
parser.add_argument('--prefix', default='job622356_job622356__B__victory')
parser.add_argument('--axes-viewport', nargs=4, type=float, help='恢复GUI方向轴renderer的归一化位置')
args = parser.parse_args()
out = Path(args.output)
out.mkdir(parents=True, exist_ok=True)
# 保留GUI相机，禁止首次显式Render自动复位。
pvs._DisableFirstRenderCameraReset()
pvs.LoadState(args.state)
reader = next(s for s in pvs.GetSources().values() if s.GetXMLName() == 'AMReXGridReader')
scalar = pvs.FindSource('Publication_vorticity_magnitude')
view_source = pvs.FindSource(args.view_source) if args.view_source else scalar
assert view_source is not None
views = [v for v in pvs.GetViews() if any(
    r.GetProperty('Input') and r.Input == view_source and r.Visibility for r in v.Representations)]
assert len(views) == 1, views
view = views[0]
pvs.SetActiveView(view)
# 按指定的逻辑尺寸渲染；原尺寸模式不做二倍截图缩放。
view.ViewSize = [round(args.width * args.view_scale), round(args.height * args.view_scale)]
for representation in view.Representations:
    if not args.view_source and representation.GetXMLName() == 'ScalarBarWidgetRepresentation':
        representation.Visibility = int(representation.Title == 'Vorticity magnitude')
reader.UpdatePipelineInformation()
times = list(reader.TimestepValues)
files = list(reader.FileNames)
assert len(times) == len(files) == 40
assert [float(t) for t in times] == list(range(25, 1001, 25))
scene = pvs.GetAnimationScene()
scene.UpdateAnimationUsingDataTimeSteps()
keeper = pvs.GetTimeKeeper()
print(json.dumps({'host': socket.gethostname(), 'ranks': servermanager.vtkProcessModule.GetProcessModule().GetNumberOfLocalPartitions(),
                  'frames': len(times), 'size': [args.width,args.height]}), flush=True)
for i in range(args.start, args.stop + 1):
    target = out / ('%s.%04d.png' % (args.prefix, i))
    if target.exists():
        raise RuntimeError('Refusing to overwrite ' + str(target))
    started = time.monotonic()
    scene.AnimationTime = float(times[i])
    keeper.Time = float(times[i])
    view.ViewTime = float(times[i])
    scalar.UpdatePipeline(float(times[i]))
    cells = scalar.GetDataInformation().GetNumberOfCells()
    limits = list(scalar.CellData['PublicationVorticityMagnitude'].GetRange())
    assert cells > 0
    if args.axes_viewport:
        # 拖动位置不属于PVSM代理状态，渲染初始化后显式恢复方向轴renderer。
        pvs.Render(view)
        renderers = view.GetClientSideObject().GetRenderWindow().GetRenderers()
        renderers.InitTraversal()
        found = 0
        for _ in range(renderers.GetNumberOfItems()):
            renderer = renderers.GetNextItem()
            props = renderer.GetViewProps()
            props.InitTraversal()
            classes = [props.GetNextProp().GetClassName() for _ in range(props.GetNumberOfItems())]
            if 'vtkPVAxesActor' in classes:
                renderer.SetViewport(*args.axes_viewport)
                found += 1
        assert found == 1, found
    pvs.SaveScreenshot(str(target), view, ImageResolution=[args.width, args.height])
    print(json.dumps({'index': i, 'time': float(times[i]), 'input': files[i], 'output': str(target),
                      'cells': cells, 'range': limits, 'seconds': time.monotonic()-started, 'peak_rss_kib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}), flush=True)
