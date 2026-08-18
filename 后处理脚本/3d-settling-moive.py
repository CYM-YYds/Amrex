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
renderView1.ViewSize = [1390, 713]
renderView1.AxesGrid = 'GridAxes3DActor'
renderView1.OrientationAxesVisibility = 0
renderView1.CenterOfRotation = [53.0, 53.0, 85.0]
renderView1.StereoType = 'Crystal Eyes'
renderView1.CameraPosition = [361.7779142183675, 299.3111452389069, 273.9707212995327]
renderView1.CameraFocalPoint = [53.0000000000002, 53.00000000000003, 85.00000000000041]
renderView1.CameraViewUp = [-0.3176330173183728, -0.2935470104268909, 0.9016315317127398]
renderView1.CameraFocalDisk = 1.0
renderView1.CameraParallelScale = 113.32696060514462
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
layout1.SetSize(1390, 713)

# ----------------------------------------------------------------
# restore active view
SetActiveView(renderView1)
# ----------------------------------------------------------------

# ----------------------------------------------------------------
# setup the data processing pipelines
# ----------------------------------------------------------------

# create a new 'AMReX/BoxLib Grid Reader'
settling_sphere_Re41_amr1_02500 = AMReXBoxLibGridReader(registrationName='settling_sphere_Re4.1_amr1_02500', FileNames=['F:\\data\\settling_sphere_Re4.1_amr1_02500'])
settling_sphere_Re41_amr1_02500.Level = 4
settling_sphere_Re41_amr1_02500.CellArrayStatus = ['ux', 'uy', 'uz']

# create a new 'Calculator'
calculator1 = Calculator(registrationName='Calculator1', Input=settling_sphere_Re41_amr1_02500)
calculator1.AttributeType = 'Cell Data'
calculator1.ResultArrayName = 'velocity'
calculator1.Function = 'ux*iHat+uy*jHat+uz*kHat'

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
contour1.Isosurfaces = [4e-07]
contour1.PointMergeMethod = 'Uniform Binning'

# ----------------------------------------------------------------
# setup the visualization in view 'renderView1'
# ----------------------------------------------------------------

# show data from settling_sphere_Re41_amr1_02500
settling_sphere_Re41_amr1_02500Display = Show(settling_sphere_Re41_amr1_02500, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
settling_sphere_Re41_amr1_02500Display.Representation = 'AMR Blocks'
settling_sphere_Re41_amr1_02500Display.AmbientColor = [1.0, 0.0, 0.0]
settling_sphere_Re41_amr1_02500Display.ColorArrayName = ['POINTS', '']
settling_sphere_Re41_amr1_02500Display.DiffuseColor = [1.0, 0.0, 0.0]
settling_sphere_Re41_amr1_02500Display.SelectTCoordArray = 'None'
settling_sphere_Re41_amr1_02500Display.SelectNormalArray = 'None'
settling_sphere_Re41_amr1_02500Display.SelectTangentArray = 'None'
settling_sphere_Re41_amr1_02500Display.OSPRayScaleFunction = 'PiecewiseFunction'
settling_sphere_Re41_amr1_02500Display.SelectOrientationVectors = 'None'
settling_sphere_Re41_amr1_02500Display.ScaleFactor = 1.8
settling_sphere_Re41_amr1_02500Display.SelectScaleArray = 'None'
settling_sphere_Re41_amr1_02500Display.GlyphType = 'Arrow'
settling_sphere_Re41_amr1_02500Display.GlyphTableIndexArray = 'None'
settling_sphere_Re41_amr1_02500Display.GaussianRadius = 0.09
settling_sphere_Re41_amr1_02500Display.SetScaleArray = ['POINTS', '']
settling_sphere_Re41_amr1_02500Display.ScaleTransferFunction = 'PiecewiseFunction'
settling_sphere_Re41_amr1_02500Display.OpacityArray = ['POINTS', '']
settling_sphere_Re41_amr1_02500Display.OpacityTransferFunction = 'PiecewiseFunction'
settling_sphere_Re41_amr1_02500Display.DataAxesGrid = 'GridAxesRepresentation'
settling_sphere_Re41_amr1_02500Display.PolarAxes = 'PolarAxesRepresentation'
settling_sphere_Re41_amr1_02500Display.ScalarOpacityUnitDistance = 0.058591335575383104

# show data from cellDatatoPointData1
cellDatatoPointData1Display = Show(cellDatatoPointData1, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
cellDatatoPointData1Display.Representation = 'Outline'
cellDatatoPointData1Display.AmbientColor = [0.0, 0.0, 0.0]
cellDatatoPointData1Display.ColorArrayName = ['POINTS', '']
cellDatatoPointData1Display.DiffuseColor = [0.0, 0.0, 0.0]
cellDatatoPointData1Display.SelectTCoordArray = 'None'
cellDatatoPointData1Display.SelectNormalArray = 'None'
cellDatatoPointData1Display.SelectTangentArray = 'None'
cellDatatoPointData1Display.OSPRayScaleArray = 'vort'
cellDatatoPointData1Display.OSPRayScaleFunction = 'PiecewiseFunction'
cellDatatoPointData1Display.SelectOrientationVectors = 'velocity'
cellDatatoPointData1Display.ScaleFactor = 1.8
cellDatatoPointData1Display.SelectScaleArray = 'vort'
cellDatatoPointData1Display.GlyphType = 'Arrow'
cellDatatoPointData1Display.GlyphTableIndexArray = 'vort'
cellDatatoPointData1Display.GaussianRadius = 0.09
cellDatatoPointData1Display.SetScaleArray = ['POINTS', 'vort']
cellDatatoPointData1Display.ScaleTransferFunction = 'PiecewiseFunction'
cellDatatoPointData1Display.OpacityArray = ['POINTS', 'vort']
cellDatatoPointData1Display.OpacityTransferFunction = 'PiecewiseFunction'
cellDatatoPointData1Display.DataAxesGrid = 'GridAxesRepresentation'
cellDatatoPointData1Display.PolarAxes = 'PolarAxesRepresentation'
cellDatatoPointData1Display.ScalarOpacityUnitDistance = 0.058591335575383104

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
cellDatatoPointData1Display.ScaleTransferFunction.Points = [2.708409491229502e-15, 0.0, 0.5, 0.0, 0.46770352805563803, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
cellDatatoPointData1Display.OpacityTransferFunction.Points = [2.708409491229502e-15, 0.0, 0.5, 0.0, 0.46770352805563803, 1.0, 0.5, 0.0]

# show data from contour1
contour1Display = Show(contour1, renderView1, 'GeometryRepresentation')

# get 2D transfer function for 'vort'
vortTF2D = GetTransferFunction2D('vort')

# get color transfer function/color map for 'vort'
vortLUT = GetColorTransferFunction('vort')
vortLUT.TransferFunction2D = vortTF2D
vortLUT.RGBPoints = [0.0006452044993563812, 0.02, 0.3813, 0.9981, 0.0007022004392120668, 0.02000006, 0.424267768, 0.96906969, 0.0007591963790677524, 0.02, 0.467233763, 0.940033043, 0.0008161923189234379, 0.02, 0.5102, 0.911, 0.0008731882587791235, 0.02000006, 0.546401494, 0.872669438, 0.000930184198634809, 0.02, 0.582600362, 0.83433295, 0.0009871801384904945, 0.02, 0.6188, 0.796, 0.0010441760783461802, 0.02000006, 0.652535156, 0.749802434, 0.0011011720182018657, 0.02, 0.686267004, 0.703599538, 0.0011581679580575511, 0.02, 0.72, 0.6574, 0.0012151638979132368, 0.02000006, 0.757035456, 0.603735359, 0.0012721598377689225, 0.02, 0.794067037, 0.55006613, 0.001329155777624608, 0.02, 0.8311, 0.4964, 0.0013861517174802935, 0.021354336738172372, 0.8645368555261631, 0.4285579460761159, 0.0014431476573359791, 0.023312914349117714, 0.897999359924484, 0.36073871343115577, 0.0015001435971916648, 0.015976108242848862, 0.9310479513349017, 0.2925631815088092, 0.00155713953704735, 0.27421074700988196, 0.952562960995083, 0.15356836602739213, 0.0016141354769030358, 0.4933546281681699, 0.9619038625309482, 0.11119493614749336, 0.0016711314167587212, 0.6439, 0.9773, 0.0469, 0.001728127356614407, 0.762401813, 0.984669591, 0.034600153, 0.0017851232964700924, 0.880901185, 0.992033407, 0.022299877, 0.001842119236325778, 0.9995285432627147, 0.9995193706781492, 0.0134884641450013, 0.0018991151761814638, 0.999402998, 0.955036376, 0.079066628, 0.0019561111160371495, 0.9994, 0.910666223, 0.148134024, 0.0020131070558928347, 0.9994, 0.8663, 0.2172, 0.0020701029957485204, 0.999269665, 0.818035981, 0.217200652, 0.002127098935604206, 0.999133332, 0.769766184, 0.2172, 0.0021840948754598914, 0.999, 0.7215, 0.2172, 0.002241090815315577, 0.99913633, 0.673435546, 0.217200652, 0.0022980867551712627, 0.999266668, 0.625366186, 0.2172, 0.002355082695026948, 0.9994, 0.5773, 0.2172, 0.002412078634882634, 0.999402998, 0.521068455, 0.217200652, 0.0024690745747383194, 0.9994, 0.464832771, 0.2172, 0.0025260705145940046, 0.9994, 0.4086, 0.2172, 0.0025830664544496907, 0.9947599917687346, 0.33177297300202935, 0.2112309638520206, 0.002640062394305376, 0.9867129505479589, 0.2595183410914934, 0.19012239549291934, 0.0026970583341610612, 0.9912458875646419, 0.14799417507952672, 0.21078892136920357, 0.002754054274016747, 0.949903037, 0.116867171, 0.252900603, 0.0028110502138724326, 0.903199533, 0.078432949, 0.291800389, 0.0028680461537281183, 0.8565, 0.04, 0.3307, 0.0029250420935838036, 0.798902627, 0.04333345, 0.358434298, 0.0029820380334394892, 0.741299424, 0.0466667, 0.386166944, 0.003039033973295175, 0.6837, 0.05, 0.4139]
vortLUT.ColorSpace = 'RGB'
vortLUT.NanColor = [1.0, 0.0, 0.0]
vortLUT.ScalarRangeInitialized = 1.0

# trace defaults for the display properties.
contour1Display.Representation = 'Surface'
contour1Display.ColorArrayName = ['POINTS', 'vort']
contour1Display.LookupTable = vortLUT
contour1Display.SelectTCoordArray = 'None'
contour1Display.SelectNormalArray = 'Normals'
contour1Display.SelectTangentArray = 'None'
contour1Display.OSPRayScaleArray = 'vort'
contour1Display.OSPRayScaleFunction = 'PiecewiseFunction'
contour1Display.SelectOrientationVectors = 'velocity'
contour1Display.ScaleFactor = 2.3697296142578126
contour1Display.SelectScaleArray = 'vort'
contour1Display.GlyphType = 'Arrow'
contour1Display.GlyphTableIndexArray = 'vort'
contour1Display.GaussianRadius = 0.11848648071289063
contour1Display.SetScaleArray = ['POINTS', 'vort']
contour1Display.ScaleTransferFunction = 'PiecewiseFunction'
contour1Display.OpacityArray = ['POINTS', 'vort']
contour1Display.OpacityTransferFunction = 'PiecewiseFunction'
contour1Display.DataAxesGrid = 'GridAxesRepresentation'
contour1Display.PolarAxes = 'PolarAxesRepresentation'
contour1Display.SelectInputVectors = ['POINTS', 'velocity']
contour1Display.WriteLog = ''

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
contour1Display.ScaleTransferFunction.Points = [0.001555823409602715, 0.0, 0.5, 0.0, 0.0015560617903247476, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
contour1Display.OpacityTransferFunction.Points = [0.001555823409602715, 0.0, 0.5, 0.0, 0.0015560617903247476, 1.0, 0.5, 0.0]

# ----------------------------------------------------------------
# setup color maps and opacity mapes used in the visualization
# note: the Get..() functions create a new object, if needed
# ----------------------------------------------------------------

# get opacity transfer function/opacity map for 'vort'
vortPWF = GetOpacityTransferFunction('vort')
vortPWF.Points = [0.0006452044993563812, 0.0, 0.5, 0.0, 0.003039033973295175, 1.0, 0.5, 0.0]
vortPWF.ScalarRangeInitialized = 1

# ----------------------------------------------------------------
# restore active source
SetActiveSource(contour1)
# ----------------------------------------------------------------


if __name__ == '__main__':
    # generate extracts
    SaveExtracts(ExtractsOutputDirectory='extracts')