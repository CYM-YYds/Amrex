# state file generated using paraview version 5.11.2
import paraview
paraview.compatibility.major = 5
paraview.compatibility.minor = 11

#### import the simple module from the paraview
from paraview.simple import *
#### disable automatic camera reset on 'Show'
paraview.simple._DisableFirstRenderCameraReset()

# ----------------------------------------------------------------
# setup views used in the visualization
# ----------------------------------------------------------------

# get the material library
materialLibrary1 = GetMaterialLibrary()

# Create a new 'Render View'
renderView1 = CreateView('RenderView')
renderView1.ViewSize = [1330, 713]
renderView1.AxesGrid = 'GridAxes3DActor'
renderView1.OrientationAxesVisibility = 0
renderView1.CenterOfRotation = [64.0, 62.05284309387207, 512.0]
renderView1.StereoType = 'Crystal Eyes'
renderView1.CameraPosition = [-186.02229772419057, -173.60005640075678, 311.98468370409165]
renderView1.CameraFocalPoint = [-25.509311966885715, -12.155362182060133, 221.33699937663522]
renderView1.CameraViewUp = [0.2608194181104737, 0.2623333620297291, 0.929061051978121]
renderView1.CameraFocalDisk = 1.0
renderView1.CameraParallelScale = 516.2697273325944
renderView1.UseColorPaletteForBackground = 0
renderView1.Background = [1.0, 1.0, 1.0]
renderView1.BackEnd = 'OSPRay raycaster'
renderView1.OSPRayMaterialLibrary = materialLibrary1

SetActiveView(None)

# ----------------------------------------------------------------
# setup view layouts
# ----------------------------------------------------------------

# create new layout object 'Layout #1'
layout1 = CreateLayout(name='Layout #1')
layout1.AssignView(0, renderView1)
layout1.SetSize(1330, 713)

# ----------------------------------------------------------------
# restore active view
SetActiveView(renderView1)
# ----------------------------------------------------------------

# ----------------------------------------------------------------
# setup the data processing pipelines
# ----------------------------------------------------------------

# create a new 'AMReX/BoxLib Grid Reader'
settling_buf0_amr3_06000 = AMReXBoxLibGridReader(registrationName='settling_buf0_amr3_06000', FileNames=['/home/huazkjdxmrsgjzdsyshi/whcs-share18/wangyan/amrex-test/test-3D/notdebug7/case3_06000/'])
settling_buf0_amr3_06000.Level = 4
settling_buf0_amr3_06000.CellArrayStatus = ['ux', 'uy', 'uz']

# create a new 'Calculator'
calculator1 = Calculator(registrationName='Calculator1', Input=settling_buf0_amr3_06000)
calculator1.AttributeType = 'Cell Data'
calculator1.ResultArrayName = 'velocity'
calculator1.Function = 'ux*iHat+uy*jHat+uz*kHat'

# create a new 'Slice'
slice1 = Slice(registrationName='Slice1', Input=settling_buf0_amr3_06000)
slice1.SliceType = 'Plane'
slice1.HyperTreeGridSlicer = 'Plane'
slice1.SliceOffsetValues = [0.0]

# init the 'Plane' selected for 'SliceType'
slice1.SliceType.Origin = [64.0, 64.0, 512.0]
slice1.SliceType.Normal = [0.0, 1.0, 0.0]

# init the 'Plane' selected for 'HyperTreeGridSlicer'
slice1.HyperTreeGridSlicer.Origin = [64.0, 64.0, 512.0]

# create a new 'Slice'
slice2 = Slice(registrationName='Slice2', Input=settling_buf0_amr3_06000)
slice2.SliceType = 'Plane'
slice2.HyperTreeGridSlicer = 'Plane'
slice2.SliceOffsetValues = [0.0]

# init the 'Plane' selected for 'SliceType'
slice2.SliceType.Origin = [64.0, 64.0, 512.0]

# init the 'Plane' selected for 'HyperTreeGridSlicer'
slice2.HyperTreeGridSlicer.Origin = [64.0, 64.0, 512.0]

# create a new 'Gradient'
gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
gradient1.ScalarArray = ['CELLS', 'velocity']
gradient1.ComputeGradient = 0
gradient1.ComputeVorticity = 1
gradient1.ComputeQCriterion = 1

# create a new 'Calculator'
calculator2 = Calculator(registrationName='Calculator2', Input=gradient1)
calculator2.AttributeType = 'Cell Data'
calculator2.ResultArrayName = 'vort'
calculator2.Function = 'sqrt(Vorticity_X*Vorticity_X+Vorticity_Y*Vorticity_Y+Vorticity_Z*Vorticity_Z)'

# create a new 'Cell Data to Point Data'
cellDatatoPointData1 = CellDatatoPointData(registrationName='CellDatatoPointData1', Input=calculator2)
cellDatatoPointData1.CellDataArraytoprocess = ['Q Criterion', 'Vorticity', 'ux', 'uy', 'uz', 'velocity', 'vort', 'vtkGhostType']

# create a new 'Contour'
contour1 = Contour(registrationName='Contour1', Input=cellDatatoPointData1)
contour1.ContourBy = ['POINTS', 'Q Criterion']
contour1.Isosurfaces = [5e-08]
contour1.PointMergeMethod = 'Uniform Binning'

# ----------------------------------------------------------------
# setup the visualization in view 'renderView1'
# ----------------------------------------------------------------

# show data from settling_buf0_amr3_06000
settling_buf0_amr3_06000Display = Show(settling_buf0_amr3_06000, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
settling_buf0_amr3_06000Display.Representation = 'Feature Edges'
settling_buf0_amr3_06000Display.AmbientColor = [0.0, 0.0, 0.0]
settling_buf0_amr3_06000Display.ColorArrayName = ['POINTS', '']
settling_buf0_amr3_06000Display.DiffuseColor = [0.0, 0.0, 0.0]
settling_buf0_amr3_06000Display.SelectTCoordArray = 'None'
settling_buf0_amr3_06000Display.SelectNormalArray = 'None'
settling_buf0_amr3_06000Display.SelectTangentArray = 'None'
settling_buf0_amr3_06000Display.OSPRayScaleFunction = 'PiecewiseFunction'
settling_buf0_amr3_06000Display.SelectOrientationVectors = 'None'
settling_buf0_amr3_06000Display.ScaleFactor = 1.8
settling_buf0_amr3_06000Display.SelectScaleArray = 'None'
settling_buf0_amr3_06000Display.GlyphType = 'Arrow'
settling_buf0_amr3_06000Display.GlyphTableIndexArray = 'None'
settling_buf0_amr3_06000Display.GaussianRadius = 0.09
settling_buf0_amr3_06000Display.SetScaleArray = ['POINTS', '']
settling_buf0_amr3_06000Display.ScaleTransferFunction = 'PiecewiseFunction'
settling_buf0_amr3_06000Display.OpacityArray = ['POINTS', '']
settling_buf0_amr3_06000Display.OpacityTransferFunction = 'PiecewiseFunction'
settling_buf0_amr3_06000Display.DataAxesGrid = 'GridAxesRepresentation'
settling_buf0_amr3_06000Display.PolarAxes = 'PolarAxesRepresentation'
settling_buf0_amr3_06000Display.ScalarOpacityUnitDistance = 0.058591335575383104

# show data from contour1
contour1Display = Show(contour1, renderView1, 'GeometryRepresentation')

# get 2D transfer function for 'Vorticity'
vorticityTF2D = GetTransferFunction2D('Vorticity')
vorticityTF2D.ScalarRangeInitialized = 1
vorticityTF2D.Range = [0.0, 0.01, 0.0, 1.0]

# get color transfer function/color map for 'Vorticity'
vorticityLUT = GetColorTransferFunction('Vorticity')
vorticityLUT.TransferFunction2D = vorticityTF2D
vorticityLUT.RGBPoints = [0.0, 0.231373, 0.298039, 0.752941, 0.005, 0.865003, 0.865003, 0.865003, 0.01, 0.705882, 0.0156863, 0.14902]
vorticityLUT.ScalarRangeInitialized = 1.0

# trace defaults for the display properties.
contour1Display.Representation = 'Surface'
contour1Display.ColorArrayName = ['POINTS', 'Vorticity']
contour1Display.LookupTable = vorticityLUT
contour1Display.SelectTCoordArray = 'None'
contour1Display.SelectNormalArray = 'Normals'
contour1Display.SelectTangentArray = 'None'
contour1Display.OSPRayScaleArray = 'Q Criterion'
contour1Display.OSPRayScaleFunction = 'PiecewiseFunction'
contour1Display.SelectOrientationVectors = 'velocity'
contour1Display.ScaleFactor = 36.34837112426758
contour1Display.SelectScaleArray = 'Q Criterion'
contour1Display.GlyphType = 'Arrow'
contour1Display.GlyphTableIndexArray = 'Q Criterion'
contour1Display.GaussianRadius = 1.8174185562133789
contour1Display.SetScaleArray = ['POINTS', 'Q Criterion']
contour1Display.ScaleTransferFunction = 'PiecewiseFunction'
contour1Display.OpacityArray = ['POINTS', 'Q Criterion']
contour1Display.OpacityTransferFunction = 'PiecewiseFunction'
contour1Display.DataAxesGrid = 'GridAxesRepresentation'
contour1Display.PolarAxes = 'PolarAxesRepresentation'
contour1Display.SelectInputVectors = ['POINTS', 'velocity']
contour1Display.WriteLog = ''

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
contour1Display.ScaleTransferFunction.Points = [5e-08, 0.0, 0.5, 0.0, 5.0007276541919055e-08, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
contour1Display.OpacityTransferFunction.Points = [5e-08, 0.0, 0.5, 0.0, 5.0007276541919055e-08, 1.0, 0.5, 0.0]

# show data from slice1
slice1Display = Show(slice1, renderView1, 'GeometryRepresentation')

# trace defaults for the display properties.
slice1Display.Representation = 'Outline'
slice1Display.AmbientColor = [0.6666666666666666, 0.0, 0.0]
slice1Display.ColorArrayName = ['POINTS', '']
slice1Display.DiffuseColor = [0.6666666666666666, 0.0, 0.0]
slice1Display.LineWidth = 2.0
slice1Display.SelectTCoordArray = 'None'
slice1Display.SelectNormalArray = 'None'
slice1Display.SelectTangentArray = 'None'
slice1Display.OSPRayScaleFunction = 'PiecewiseFunction'
slice1Display.SelectOrientationVectors = 'None'
slice1Display.ScaleFactor = 102.4
slice1Display.SelectScaleArray = 'None'
slice1Display.GlyphType = 'Arrow'
slice1Display.GlyphTableIndexArray = 'None'
slice1Display.GaussianRadius = 5.12
slice1Display.SetScaleArray = [None, '']
slice1Display.ScaleTransferFunction = 'PiecewiseFunction'
slice1Display.OpacityArray = [None, '']
slice1Display.OpacityTransferFunction = 'PiecewiseFunction'
slice1Display.DataAxesGrid = 'GridAxesRepresentation'
slice1Display.PolarAxes = 'PolarAxesRepresentation'
slice1Display.SelectInputVectors = [None, '']
slice1Display.WriteLog = ''

# setup the color legend parameters for each legend in this view

# get color legend/bar for vorticityLUT in view renderView1
vorticityLUTColorBar = GetScalarBar(vorticityLUT, renderView1)
vorticityLUTColorBar.Title = 'Vorticity'
vorticityLUTColorBar.ComponentTitle = 'Magnitude'

# set color bar visibility
vorticityLUTColorBar.Visibility = 1

# show color legend
contour1Display.SetScalarBarVisibility(renderView1, True)

# ----------------------------------------------------------------
# setup color maps and opacity mapes used in the visualization
# note: the Get..() functions create a new object, if needed
# ----------------------------------------------------------------

# get opacity transfer function/opacity map for 'Vorticity'
vorticityPWF = GetOpacityTransferFunction('Vorticity')
vorticityPWF.Points = [0.0, 0.0, 0.5, 0.0, 0.01, 1.0, 0.5, 0.0]
vorticityPWF.ScalarRangeInitialized = 1

# ----------------------------------------------------------------
# restore active source
SetActiveSource(None)
# ----------------------------------------------------------------


if __name__ == '__main__':
    # generate extracts
    SaveExtracts(ExtractsOutputDirectory='extracts')