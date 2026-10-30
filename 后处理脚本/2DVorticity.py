# state file generated using paraview version 6.2.0
import paraview
paraview.compatibility.major = 6
paraview.compatibility.minor = 2

#### import the simple module from the paraview
from paraview.simple import *
#### disable automatic camera reset on 'Show'
paraview.simple._DisableFirstRenderCameraReset()

# ----------------------------------------------------------------
# setup views used in the visualization
# ----------------------------------------------------------------

# Create a new 'Render View'
renderView1 = CreateView('RenderView')
renderView1.Set(
    ViewSize=[2066, 1066],
    InteractionMode='2D',
    AxesGrid='Grid Axes 3D Actor',
    CenterOfRotation=[0.5, 0.5, 0.5],
    UseLight=0,
    CameraPosition=[0.6024632026616571, -2.2320508075688776, 0.4543924244473395],
    CameraFocalPoint=[0.6024632026616571, 0.5, 0.4543924244473395],
    CameraViewUp=[0.0, 0.0, 1.0],
)

# init the 'Grid Axes 3D Actor' selected for 'AxesGrid'
renderView1.AxesGrid.Set(
    Visibility=1,
    XTitle='X',
    YTitle='',
    ZTitle='Z',
    XTitleBold=0,
    YTitleBold=0,
    ZTitleBold=0,
    XLabelBold=0,
    YLabelBold=0,
    ZLabelBold=0,
    ZAxisUseCustomLabels=1,
    ZAxisLabels=[0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0],
)

# Create a new 'Render View'
renderView2 = CreateView('RenderView')
renderView2.Set(
    ViewSize=[2110, 1066],
    InteractionMode='2D',
    OrientationAxesVisibility=0,
    CenterOfRotation=[0.5, 0.5, 0.5],
    UseLight=0,
    CameraPosition=[0.5, -1.5, 0.428],
    CameraFocalPoint=[0.5, 0.5, 0.428],
    CameraViewUp=[0.0, 0.0, 1.0],
    CameraParallelScale=0.72,
    CameraParallelProjection=1,
    UseColorPaletteForBackground=0,
    Background=[1.0, 1.0, 1.0],
)

# init the 'Grid Axes 3D Actor' selected for 'AxesGrid'
renderView2.AxesGrid.Set(
    Visibility=1,
    XTitle='',
    YTitle='',
    ZTitle='',
    XTitleBold=0,
    XTitleFontSize=15,
    YTitleBold=0,
    YTitleFontSize=15,
    ZTitleBold=0,
    ZTitleFontSize=15,
    XLabelBold=0,
    XLabelFontSize=13,
    YLabelBold=0,
    YLabelFontSize=13,
    ZLabelBold=0,
    ZLabelFontSize=13,
    XAxisUseCustomLabels=1,
    XAxisLabels=[0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0],
    YAxisUseCustomLabels=1,
    YAxisLabels=[0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5],
    ZAxisUseCustomLabels=1,
    ZAxisLabels=[0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0],
)

SetActiveView(None)

# ----------------------------------------------------------------
# setup view layouts
# ----------------------------------------------------------------

# create new layout object 'Layout #1'
layout1 = CreateLayout(name='Layout #1')
layout1.AssignView(0, renderView1)
layout1.SetSize(2066, 1066)

# create new layout object 'Slice3_independent_axis_titles'
slice3_independent_axis_titles = CreateLayout(name='Slice3_independent_axis_titles')
slice3_independent_axis_titles.AssignView(0, renderView2)
slice3_independent_axis_titles.SetSize(2110, 1066)

# ----------------------------------------------------------------
# restore active view
SetActiveView(renderView2)
# ----------------------------------------------------------------

# ----------------------------------------------------------------
# setup the data processing pipelines
# ----------------------------------------------------------------

# create a new 'Text'
publication_axis_0_18084_top = Text(registrationName='Publication_axis_0_18084_top')
publication_axis_0_18084_top.Text = 'X'

# create a new 'Text'
publication_axis_2_18084_right = Text(registrationName='Publication_axis_2_18084_right')
publication_axis_2_18084_right.Text = 'Z'

# create a new 'Text'
publication_axis_2_18084 = Text(registrationName='Publication_axis_2_18084')
publication_axis_2_18084.Text = 'Z'

# create a new 'Text'
publication_axis_0_18084 = Text(registrationName='Publication_axis_0_18084')
publication_axis_0_18084.Text = 'X'

# create a new 'AMReX/BoxLib Grid Reader'
plt_003200 = AMReXBoxLibGridReader(registrationName='plt_003200*', FileNames=['/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_003200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_006400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_009600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_012800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_016000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_019200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_022400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_025600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_028800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_032000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_035200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_038400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_041600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_044800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_048000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_051200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_054400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_057600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_060800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_064000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_067200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_070400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_073600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_076800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_080000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_083200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_086400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_089600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_092800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_096000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_099200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_102400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_105600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_108800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_112000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_115200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_118400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_121600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_124800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_128000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_131200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_134400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_137600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_140800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_144000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_147200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_150400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_153600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_156800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_160000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_163200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_166400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_169600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_172800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_176000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_179200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_182400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_185600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_188800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_192000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_195200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_198400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_201600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_204800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_208000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_211200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_214400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_217600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_220800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_224000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_227200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_230400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_233600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_236800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_240000', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_243200', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_246400', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_249600', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_252800', '/home/iosoeqkp/whcs-share47/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D_OSI/runs/20261019_124153_job615364/plt_256000'])
plt_003200.Set(
    Level=2,
    CellArrayStatus=['ux', 'uy', 'uz'],
)

# create a new 'Slice'
slice1 = Slice(registrationName='Slice1', Input=plt_003200)
slice1.Set(
    SliceType='Plane',
    SliceOffsetValues=[0.0],
    PointMergeMethod='Uniform Binning',
)

# init the 'Plane' selected for 'SliceType'
slice1.SliceType.Set(
    Origin=[0.5, 0.5, 0.5],
    Normal=[0.0, 1.0, 0.0],
)

# init the 'Plane' selected for 'HyperTreeGridSlicer'
slice1.HyperTreeGridSlicer.Origin = [0.5, 0.5, 0.5]

# create a new 'Calculator'
calculator1 = Calculator(registrationName='Calculator1', Input=plt_003200)
calculator1.Set(
    AttributeType='Cell Data',
    Function='ux*iHat+uy*jHat+uz*kHat',
)

# create a new 'Gradient'
gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
gradient1.Set(
    ScalarArray=['CELLS', 'Result'],
    ComputeVorticity=1,
)

# create a new 'Programmable Filter'
slice3_AMR_vorticity = ProgrammableFilter(registrationName='Slice3_AMR_vorticity', Input=calculator1)
slice3_AMR_vorticity.Set(
    RequestInformationScript='',
    RequestUpdateExtentScript='',
    PythonPath='',
    Script="""
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
    gradient.SetInputScalars(vtkDataObject.FIELD_ASSOCIATION_CELLS, 'Result')
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
        if block is not None:
            output.SetDataSet(iterator, differentiate(block))
        iterator.GoToNextItem()
else:
    output.ShallowCopy(differentiate(source))
""",
)

# create a new 'Slice'
slice3 = Slice(registrationName='Slice3', Input=slice3_AMR_vorticity)
slice3.Set(
    SliceType='Plane',
    SliceOffsetValues=[0.0],
    PointMergeMethod='Uniform Binning',
)

# init the 'Plane' selected for 'SliceType'
slice3.SliceType.Set(
    Origin=[0.5, 0.5, 0.5],
    Normal=[0.0, 1.0, 0.0],
)

# init the 'Plane' selected for 'HyperTreeGridSlicer'
slice3.HyperTreeGridSlicer.Origin = [0.5, 0.5, 0.5]

# create a new 'Calculator'
publication_vorticity_magnitude = Calculator(registrationName='Publication_vorticity_magnitude', Input=slice3)
publication_vorticity_magnitude.Set(
    AttributeType='Cell Data',
    ResultArrayName='PublicationVorticityMagnitude',
    Function='mag(Vorticity)*1.0',
)

# ----------------------------------------------------------------
# setup the visualization in view 'renderView1'
# ----------------------------------------------------------------

# show data from slice3
slice3Display = Show(slice3, renderView1, 'GeometryRepresentation')

# get color transfer function/color map for 'Vorticity'
vorticityLUT = GetColorTransferFunction('Vorticity')
vorticityLUT.Set(
    AutomaticRescaleRangeMode='Never',
    RGBPoints=GenerateRGBPoints(
        preset_name='Blue to Red Rainbow',
        range_min=0.0,
        range_max=0.5,
    ),
    ColorSpace='HSV',
    NanColor=[0.498039215686, 0.498039215686, 0.498039215686],
    ScalarRangeInitialized=1.0,
)

# trace defaults for the display properties.
slice3Display.Set(
    Representation='Surface',
    ColorArrayName=['CELLS', 'Vorticity'],
    LookupTable=vorticityLUT,
    Assembly='Hierarchy',
    DataAxesGrid='Grid Axes Representation',
    PolarAxes='Polar Axes Representation',
)

# show data from slice1
slice1Display = Show(slice1, renderView1, 'GeometryRepresentation')

# get color transfer function/color map for 'vtkBlockColors'
vtkBlockColorsLUT = GetColorTransferFunction('vtkBlockColors')
vtkBlockColorsLUT.Set(
    InterpretValuesAsCategories=1,
    AnnotationsInitialized=1,
    Annotations=['0', '0', '1', '1', '2', '2', '3', '3'],
    ActiveAnnotatedValues=['0', '1'],
    IndexedColors=[1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0],
    IndexedOpacities=[-1.0, -1.0, -1.0, -1.0],
)

# trace defaults for the display properties.
slice1Display.Set(
    Representation='Outline',
    ColorArrayName=['FIELD', 'vtkBlockColors'],
    LookupTable=vtkBlockColorsLUT,
    LineWidth=3.0,
    Assembly='Hierarchy',
    DataAxesGrid='Grid Axes Representation',
    PolarAxes='Polar Axes Representation',
)

# setup the color legend parameters for each legend in this view

# get color legend/bar for vorticityLUT in view renderView1
vorticityLUTColorBar = GetScalarBar(vorticityLUT, renderView1)
vorticityLUTColorBar.Set(
    WindowLocation='Any Location',
    Position=[0.8541580322315669, 0.08348968105065661],
    Title='Vorticity',
    ComponentTitle='Magnitude',
)

# set color bar visibility
vorticityLUTColorBar.Visibility = 1

# show color legend
slice3Display.SetScalarBarVisibility(renderView1, True)

# ----------------------------------------------------------------
# setup the visualization in view 'renderView2'
# ----------------------------------------------------------------

# show data from publication_vorticity_magnitude
publication_vorticity_magnitudeDisplay = Show(publication_vorticity_magnitude, renderView2, 'GeometryRepresentation')

# get color transfer function/color map for 'PublicationVorticityMagnitude'
publicationVorticityMagnitudeLUT = GetColorTransferFunction('PublicationVorticityMagnitude')
publicationVorticityMagnitudeLUT.Set(
    AutomaticRescaleRangeMode='Never',
    RGBPoints=[
        # scalar, red, green, blue
        0.0, 0.0, 0.0, 1.0,
        0.25, 0.0, 1.0, 1.0,
        0.5, 0.0, 1.0, 0.0,
        0.75, 1.0, 1.0, 0.0,
        1.0, 1.0, 0.0, 0.0,
    ],
    ColorSpace='RGB',
    ScalarRangeInitialized=1.0,
)

# trace defaults for the display properties.
publication_vorticity_magnitudeDisplay.Set(
    Representation='Surface',
    ColorArrayName=['CELLS', 'PublicationVorticityMagnitude'],
    LookupTable=publicationVorticityMagnitudeLUT,
    Diffuse=0.0,
    Ambient=1.0,
    Assembly='Hierarchy',
)

# show data from publication_axis_0_18084
publication_axis_0_18084Display = Show(publication_axis_0_18084, renderView2, 'TextSourceRepresentation')

# trace defaults for the display properties.
publication_axis_0_18084Display.Set(
    WindowLocation='Any Location',
    Position=[0.49739336492890995, 0.14930685845319994],
    FontSize=15,
    Justification='Left',
    VerticalJustification='Bottom',
)

# show data from publication_axis_2_18084
publication_axis_2_18084Display = Show(publication_axis_2_18084, renderView2, 'TextSourceRepresentation')

# trace defaults for the display properties.
publication_axis_2_18084Display.Set(
    WindowLocation='Any Location',
    Position=[0.3018299104791996, 0.5420262664165103],
    FontSize=15,
    Justification='Left',
    VerticalJustification='Bottom',
)

# show data from publication_axis_0_18084_top
publication_axis_0_18084_topDisplay = Show(publication_axis_0_18084_top, renderView2, 'TextSourceRepresentation')

# trace defaults for the display properties.
publication_axis_0_18084_topDisplay.Set(
    WindowLocation='Any Location',
    Position=[0.49739336492890995, 0.9347456743798206],
    FontSize=15,
    Justification='Left',
    VerticalJustification='Bottom',
)

# show data from publication_axis_2_18084_right
publication_axis_2_18084_rightDisplay = Show(publication_axis_2_18084_right, renderView2, 'TextSourceRepresentation')

# trace defaults for the display properties.
publication_axis_2_18084_rightDisplay.Set(
    WindowLocation='Any Location',
    Position=[0.6943786203264877, 0.5420262664165103],
    FontSize=15,
    Justification='Left',
    VerticalJustification='Bottom',
)

# setup the color legend parameters for each legend in this view

# get color legend/bar for publicationVorticityMagnitudeLUT in view renderView2
publicationVorticityMagnitudeLUTColorBar = GetScalarBar(publicationVorticityMagnitudeLUT, renderView2)
publicationVorticityMagnitudeLUTColorBar.Set(
    WindowLocation='Any Location',
    Position=[0.3245787256450764, 0.058771106941838645],
    Title='Vorticity magnitude',
    ComponentTitle='',
    TitleFontSize=15,
    LabelFontSize=13,
    ScalarBarLength=0.35084254870984727,
    AutomaticLabelFormat=0,
    LabelFormat='{:.2f}',
    UseCustomLabels=1,
    CustomLabels=[0.0, 1.0],
    AddRangeLabels=0,
)

# set color bar visibility
publicationVorticityMagnitudeLUTColorBar.Visibility = 1

# show color legend
publication_vorticity_magnitudeDisplay.SetScalarBarVisibility(renderView2, True)

# ----------------------------------------------------------------
# setup color maps and opacity maps used in the visualization
# note: the Get..() functions create a new object, if needed
# ----------------------------------------------------------------

# get opacity transfer function/opacity map for 'vtkBlockColors'
vtkBlockColorsPWF = GetOpacityTransferFunction('vtkBlockColors')

# get opacity transfer function/opacity map for 'PublicationVorticityMagnitude'
publicationVorticityMagnitudePWF = GetOpacityTransferFunction('PublicationVorticityMagnitude')
publicationVorticityMagnitudePWF.Set(
    Points=[1.7714243768401548e-15, 0.0, 0.5, 0.0, 25.600000000000026, 1.0, 0.5, 0.0],
    ScalarRangeInitialized=1,
)

# get opacity transfer function/opacity map for 'Vorticity'
vorticityPWF = GetOpacityTransferFunction('Vorticity')
vorticityPWF.Set(
    Points=[0.0, 0.0, 0.5, 0.0, 0.5, 1.0, 0.5, 0.0],
    ScalarRangeInitialized=1,
)

# ----------------------------------------------------------------
# setup animation scene, tracks and keyframes
# note: the Get..() functions create a new object, if needed
# ----------------------------------------------------------------

# get the time-keeper
timeKeeper1 = GetTimeKeeper()

# initialize the timekeeper

# get time animation track
timeAnimationCue1 = GetTimeTrack()

# initialize the animation track

# get animation scene
animationScene1 = GetAnimationScene()

# initialize the animation scene
animationScene1.Set(
    ViewModules=[renderView1, renderView2],
    Cues=timeAnimationCue1,
    AnimationTime=2000.0,
    StartTime=25.0,
    EndTime=2000.0,
    PlayMode='Snap To TimeSteps',
)

# initialize the animation scene

# ----------------------------------------------------------------
# restore active source
SetActiveSource(publication_axis_2_18084_right)
# ----------------------------------------------------------------

# ----------------------------------------------------------------
# setup camera links
# ----------------------------------------------------------------


##--------------------------------------------
## You may need to add some code at the end of this python script depending on your usage, eg:
#
## Render all views to see them appears
# RenderAllViews()
#
## Interact with the view, usefull when running from pvpython
# Interact()
#
## Save a screenshot of the active view
# SaveScreenshot("path/to/screenshot.png")
#
## Save a screenshot of a layout (multiple splitted view)
# SaveScreenshot("path/to/screenshot.png", GetLayout())
#
## Save all "Extractors" from the pipeline browser
# SaveExtracts()
#
## Save a animation of the current active view
# SaveAnimation()
#
## Please refer to the documentation of paraview.simple
## https://www.paraview.org/paraview-docs/nightly/python/
##--------------------------------------------