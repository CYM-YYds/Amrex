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
renderView1.ViewSize = [1357, 713]
renderView1.AxesGrid = 'GridAxes3DActor'
renderView1.CenterOfRotation = [16.0006365776062, 7.962620496749878, 7.9742302894592285]
renderView1.StereoType = 'Crystal Eyes'
renderView1.CameraPosition = [14.178879581105619, -0.7832316702046864, -2.6422760107346477]
renderView1.CameraFocalPoint = [14.483719399133143, 8.29387096117975, 7.603402204672712]
renderView1.CameraViewUp = [-0.1881598737615438, -0.7323477361205194, 0.6544177987380768]
renderView1.CameraFocalDisk = 1.0
renderView1.CameraParallelScale = 9.191325188574142
renderView1.BackEnd = 'OSPRay raycaster'
renderView1.OSPRayMaterialLibrary = materialLibrary1

# Create a new 'Render View'
renderView2 = CreateView('RenderView')
renderView2.ViewSize = [1357, 713]
renderView2.AxesGrid = 'GridAxes3DActor'
renderView2.OrientationAxesVisibility = 0
renderView2.CenterOfRotation = [16.0, 8.0, 8.0]
renderView2.StereoType = 'Crystal Eyes'
renderView2.CameraPosition = [6.361426067554203, 14.367374362392862, -33.29996183964058]
renderView2.CameraFocalPoint = [12.255713724463577, 8.229036674586157, 5.973255761080096]
renderView2.CameraViewUp = [-0.20005609780230008, -0.9721688712353865, -0.12192310500077942]
renderView2.CameraViewAngle = 5.553997194950912
renderView2.CameraFocalDisk = 1.0
renderView2.CameraParallelScale = 11.445523142259598
renderView2.UseColorPaletteForBackground = 0
renderView2.Background = [1.0, 1.0, 1.0]
renderView2.BackEnd = 'OSPRay raycaster'
renderView2.OSPRayMaterialLibrary = materialLibrary1

SetActiveView(None)

# ----------------------------------------------------------------
# setup view layouts
# ----------------------------------------------------------------

# create new layout object 'Layout #1'
layout1 = CreateLayout(name='Layout #1')
layout1.AssignView(0, renderView2)
layout1.SetSize(1357, 713)

# create new layout object 'Layout #1'
layout1_1 = CreateLayout(name='Layout #1')
layout1_1.AssignView(0, renderView1)
layout1_1.SetSize(1357, 713)

# ----------------------------------------------------------------
# restore active view
SetActiveView(renderView2)
# ----------------------------------------------------------------

# ----------------------------------------------------------------
# setup the data processing pipelines
# ----------------------------------------------------------------

# create a new 'Sphere'
sphere1 = Sphere(registrationName='Sphere1')
sphere1.Center = [12.0, 8.0, 8.0]
sphere1.Radius = 1.2
sphere1.ThetaResolution = 1000

# create a new 'AMReX/BoxLib Grid Reader'
sphere_amr1_Re200_500000 = AMReXBoxLibGridReader(registrationName='sphere_amr1_Re200_500000', FileNames=['F:\\data\\sphere_amr1_Re200_500000'])
sphere_amr1_Re200_500000.CellArrayStatus = ['ux', 'uy', 'uz']

# create a new 'Calculator'
calculator1 = Calculator(registrationName='Calculator1', Input=sphere_amr1_Re200_500000)
calculator1.AttributeType = 'Cell Data'
calculator1.Function = 'ux*iHat+uy*jHat+uz*kHat'

# create a new 'Stream Tracer'
streamTracer1 = StreamTracer(registrationName='StreamTracer1', Input=calculator1,
    SeedType='Point Cloud')
streamTracer1.Vectors = ['CELLS', 'Result']
streamTracer1.MaximumStreamlineLength = 18.0
streamTracer1.ComputeVorticity = 0

# init the 'Point Cloud' selected for 'SeedType'
streamTracer1.SeedType.Center = [12.0, 8.0, 8.0]
streamTracer1.SeedType.Radius = 1.4

# create a new 'Tube'
tube1 = Tube(registrationName='Tube1', Input=streamTracer1)
tube1.Scalars = ['POINTS', 'IntegrationTime']
tube1.Vectors = ['POINTS', 'Result']
tube1.Radius = 0.005399737644195557

# ----------------------------------------------------------------
# setup the visualization in view 'renderView2'
# ----------------------------------------------------------------

# show data from tube1
tube1Display = Show(tube1, renderView2, 'GeometryRepresentation')

# get 2D transfer function for 'Result'
resultTF2D = GetTransferFunction2D('Result')

# get color transfer function/color map for 'Result'
resultLUT = GetColorTransferFunction('Result')
resultLUT.TransferFunction2D = resultTF2D
resultLUT.RGBPoints = [4.738068703081242e-06, 0.231373, 0.298039, 0.752941, 0.01704807246559965, 0.865003, 0.865003, 0.865003, 0.03409140686249622, 0.705882, 0.0156863, 0.14902]
resultLUT.ScalarRangeInitialized = 1.0

# trace defaults for the display properties.
tube1Display.Representation = 'Surface'
tube1Display.ColorArrayName = ['POINTS', 'Result']
tube1Display.LookupTable = resultLUT
tube1Display.SelectTCoordArray = 'None'
tube1Display.SelectNormalArray = 'TubeNormals'
tube1Display.SelectTangentArray = 'None'
tube1Display.OSPRayScaleArray = 'IntegrationTime'
tube1Display.OSPRayScaleFunction = 'PiecewiseFunction'
tube1Display.SelectOrientationVectors = 'None'
tube1Display.ScaleFactor = 1.7999423980712892
tube1Display.SelectScaleArray = 'IntegrationTime'
tube1Display.GlyphType = 'Arrow'
tube1Display.GlyphTableIndexArray = 'IntegrationTime'
tube1Display.GaussianRadius = 0.08999711990356446
tube1Display.SetScaleArray = ['POINTS', 'IntegrationTime']
tube1Display.ScaleTransferFunction = 'PiecewiseFunction'
tube1Display.OpacityArray = ['POINTS', 'IntegrationTime']
tube1Display.OpacityTransferFunction = 'PiecewiseFunction'
tube1Display.DataAxesGrid = 'GridAxesRepresentation'
tube1Display.PolarAxes = 'PolarAxesRepresentation'
tube1Display.SelectInputVectors = ['POINTS', 'Result']
tube1Display.WriteLog = ''

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
tube1Display.ScaleTransferFunction.Points = [-32199.302690283144, 0.0, 0.5, 0.0, 52325.83697632044, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
tube1Display.OpacityTransferFunction.Points = [-32199.302690283144, 0.0, 0.5, 0.0, 52325.83697632044, 1.0, 0.5, 0.0]

# show data from sphere1
sphere1Display = Show(sphere1, renderView2, 'GeometryRepresentation')

# trace defaults for the display properties.
sphere1Display.Representation = 'Surface'
sphere1Display.AmbientColor = [1.0, 0.6666666666666666, 0.0]
sphere1Display.ColorArrayName = ['POINTS', '']
sphere1Display.DiffuseColor = [1.0, 0.6666666666666666, 0.0]
sphere1Display.SelectTCoordArray = 'None'
sphere1Display.SelectNormalArray = 'Normals'
sphere1Display.SelectTangentArray = 'None'
sphere1Display.OSPRayScaleArray = 'Normals'
sphere1Display.OSPRayScaleFunction = 'PiecewiseFunction'
sphere1Display.SelectOrientationVectors = 'None'
sphere1Display.ScaleFactor = 2.0000338554382325e-05
sphere1Display.SelectScaleArray = 'None'
sphere1Display.GlyphType = 'Arrow'
sphere1Display.GlyphTableIndexArray = 'None'
sphere1Display.GaussianRadius = 1.0000169277191162e-06
sphere1Display.SetScaleArray = ['POINTS', 'Normals']
sphere1Display.ScaleTransferFunction = 'PiecewiseFunction'
sphere1Display.OpacityArray = ['POINTS', 'Normals']
sphere1Display.OpacityTransferFunction = 'PiecewiseFunction'
sphere1Display.DataAxesGrid = 'GridAxesRepresentation'
sphere1Display.PolarAxes = 'PolarAxesRepresentation'
sphere1Display.SelectInputVectors = ['POINTS', 'Normals']
sphere1Display.WriteLog = ''

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
sphere1Display.ScaleTransferFunction.Points = [-0.9749279022216797, 0.0, 0.5, 0.0, 0.9749279022216797, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
sphere1Display.OpacityTransferFunction.Points = [-0.9749279022216797, 0.0, 0.5, 0.0, 0.9749279022216797, 1.0, 0.5, 0.0]

# ----------------------------------------------------------------
# setup color maps and opacity mapes used in the visualization
# note: the Get..() functions create a new object, if needed
# ----------------------------------------------------------------

# get opacity transfer function/opacity map for 'Result'
resultPWF = GetOpacityTransferFunction('Result')
resultPWF.Points = [4.738068703081242e-06, 0.0, 0.5, 0.0, 0.03409140686249622, 1.0, 0.5, 0.0]
resultPWF.ScalarRangeInitialized = 1

# ----------------------------------------------------------------
# restore active source
SetActiveSource(sphere1)
# ----------------------------------------------------------------


if __name__ == '__main__':
    # generate extracts
    SaveExtracts(ExtractsOutputDirectory='extracts')