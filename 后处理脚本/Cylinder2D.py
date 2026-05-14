# state file generated using paraview version 5.11.1
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
renderView1.ViewSize = [1730, 1093]
renderView1.InteractionMode = '2D'
renderView1.AxesGrid = 'GridAxes3DActor'
renderView1.CenterOfRotation = [0.5, 0.5, 0.0]
renderView1.StereoType = 'Crystal Eyes'
renderView1.CameraPosition = [0.5007372910988616, 0.49920381747866505, 2.7320508075688776]
renderView1.CameraFocalPoint = [0.5007372910988616, 0.49920381747866505, 0.0]
renderView1.CameraFocalDisk = 1.0
renderView1.CameraParallelScale = 0.01890440296674554
renderView1.BackEnd = 'OSPRay raycaster'
renderView1.OSPRayMaterialLibrary = materialLibrary1

SetActiveView(None)

# ----------------------------------------------------------------
# setup view layouts
# ----------------------------------------------------------------

# create new layout object 'Layout #1'
layout1 = CreateLayout(name='Layout #1')
layout1.AssignView(0, renderView1)
layout1.SetSize(1730, 1093)

# ----------------------------------------------------------------
# restore active view
SetActiveView(renderView1)
# ----------------------------------------------------------------

# ----------------------------------------------------------------
# setup the data processing pipelines
# ----------------------------------------------------------------

# create a new 'AMReX/BoxLib Grid Reader'
cyl_lev3_Re40_ = AMReXBoxLibGridReader(registrationName='cyl_lev3_Re40_..', FileNames=[
    '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest/Cylinder2D_IDFtest/data/cyl_lev3_Re40_20000', 
    '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest/Cylinder2D_IDFtest/data/cyl_lev3_Re40_30000', 
    '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest/Cylinder2D_IDFtest/data/cyl_lev3_Re40_40000', 
    '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest/Cylinder2D_IDFtest/data/cyl_lev3_Re40_50000', 
    '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest/Cylinder2D_IDFtest/data/cyl_lev3_Re40_100000', 
    '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest/Cylinder2D_IDFtest/data/cyl_lev3_Re40_150000',
    '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest/Cylinder2D_IDFtest/data/cyl_lev3_Re40_200000',
    '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest/Cylinder2D_IDFtest/data/cyl_lev3_Re40_500000',])

cyl_lev3_Re40_.Level = 3
cyl_lev3_Re40_.CellArrayStatus = ['ux', 'uy']

# create a new 'Sphere'
sphere1 = Sphere(registrationName='Sphere1')
sphere1.Center = [0.5, 0.5, 0.0]
sphere1.Radius = 0.0125
sphere1.ThetaResolution = 360
sphere1.PhiResolution = 360

# create a new 'Calculator'
calculator1 = Calculator(registrationName='Calculator1', Input=cyl_lev3_Re40_)
calculator1.AttributeType = 'Cell Data'
calculator1.ResultArrayName = 'Velocity'
calculator1.Function = 'ux*iHat+uy*jHat'

# create a new 'Gradient'
gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
gradient1.ScalarArray = ['CELLS', 'Velocity']
gradient1.ComputeGradient = 0
gradient1.ComputeVorticity = 1

# create a new 'Extract Block'
extractBlock1 = ExtractBlock(registrationName='ExtractBlock1', Input=gradient1)
extractBlock1.Selectors = ['/Root']

# create a new 'Stream Tracer'
streamTracer1 = StreamTracer(registrationName='StreamTracer1', Input=extractBlock1,
    SeedType='Line')
streamTracer1.Vectors = ['CELLS', 'Velocity']

# init the 'Line' selected for 'SeedType'
streamTracer1.SeedType.Point1 = [0.51, 0.49, 0.0]
streamTracer1.SeedType.Point2 = [0.51, 0.51, 0.0]
streamTracer1.SeedType.Resolution = 30

# create a new 'Stream Tracer'
streamTracer3 = StreamTracer(registrationName='StreamTracer3', Input=extractBlock1,
    SeedType='Line')
streamTracer3.Vectors = ['CELLS', 'Velocity']

# init the 'Line' selected for 'SeedType'
streamTracer3.SeedType.Point1 = [0.49, 0.48, 0.0]
streamTracer3.SeedType.Point2 = [0.49, 0.52, 0.0]
streamTracer3.SeedType.Resolution = 50

# ----------------------------------------------------------------
# setup the visualization in view 'renderView1'
# ----------------------------------------------------------------

# show data from extractBlock1
extractBlock1Display = Show(extractBlock1, renderView1, 'UniformGridRepresentation')

# get 2D transfer function for 'vtkBlockColors'
vtkBlockColorsTF2D = GetTransferFunction2D('vtkBlockColors')

# get color transfer function/color map for 'vtkBlockColors'
vtkBlockColorsLUT = GetColorTransferFunction('vtkBlockColors')
vtkBlockColorsLUT.InterpretValuesAsCategories = 1
vtkBlockColorsLUT.AnnotationsInitialized = 1
vtkBlockColorsLUT.TransferFunction2D = vtkBlockColorsTF2D
vtkBlockColorsLUT.Annotations = ['0', '0', '1', '1', '2', '2', '3', '3', '4', '4']
vtkBlockColorsLUT.ActiveAnnotatedValues = ['0', '1', '2', '3', '0', '1', '2', '3']
vtkBlockColorsLUT.IndexedColors = [1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0]
vtkBlockColorsLUT.IndexedOpacities = [-1.0, -1.0, -1.0, -1.0, -1.0]

# get opacity transfer function/opacity map for 'vtkBlockColors'
vtkBlockColorsPWF = GetOpacityTransferFunction('vtkBlockColors')

# trace defaults for the display properties.
extractBlock1Display.Representation = 'Outline'
extractBlock1Display.ColorArrayName = ['FIELD', 'vtkBlockColors']
extractBlock1Display.LookupTable = vtkBlockColorsLUT
extractBlock1Display.SelectTCoordArray = 'None'
extractBlock1Display.SelectNormalArray = 'None'
extractBlock1Display.SelectTangentArray = 'None'
extractBlock1Display.OSPRayScaleFunction = 'PiecewiseFunction'
extractBlock1Display.SelectOrientationVectors = 'Velocity'
extractBlock1Display.ScaleFactor = 0.1
extractBlock1Display.SelectScaleArray = 'None'
extractBlock1Display.GlyphType = 'Arrow'
extractBlock1Display.GlyphTableIndexArray = 'None'
extractBlock1Display.GaussianRadius = 0.005
extractBlock1Display.SetScaleArray = ['POINTS', '']
extractBlock1Display.ScaleTransferFunction = 'PiecewiseFunction'
extractBlock1Display.OpacityArray = ['POINTS', '']
extractBlock1Display.OpacityTransferFunction = 'PiecewiseFunction'
extractBlock1Display.DataAxesGrid = 'GridAxesRepresentation'
extractBlock1Display.PolarAxes = 'PolarAxesRepresentation'
extractBlock1Display.ScalarOpacityUnitDistance = 0.011636464267690988
extractBlock1Display.ScalarOpacityFunction = vtkBlockColorsPWF
extractBlock1Display.TransferFunction2D = vtkBlockColorsTF2D
extractBlock1Display.OpacityArrayName = ['CELLS', 'Velocity']
extractBlock1Display.ColorArray2Name = ['CELLS', 'Velocity']
extractBlock1Display.SliceFunction = 'Plane'
extractBlock1Display.SelectInputVectors = ['POINTS', '']
extractBlock1Display.WriteLog = ''

# init the 'Plane' selected for 'SliceFunction'
extractBlock1Display.SliceFunction.Origin = [0.5, 0.5, 0.0]

# show data from sphere1
sphere1Display = Show(sphere1, renderView1, 'GeometryRepresentation')

# trace defaults for the display properties.
sphere1Display.Representation = 'Surface'
sphere1Display.ColorArrayName = ['POINTS', '']
sphere1Display.Opacity = 0.5
sphere1Display.SelectTCoordArray = 'None'
sphere1Display.SelectNormalArray = 'Normals'
sphere1Display.SelectTangentArray = 'None'
sphere1Display.OSPRayScaleArray = 'Normals'
sphere1Display.OSPRayScaleFunction = 'PiecewiseFunction'
sphere1Display.SelectOrientationVectors = 'None'
sphere1Display.ScaleFactor = 0.002500000037252903
sphere1Display.SelectScaleArray = 'None'
sphere1Display.GlyphType = 'Arrow'
sphere1Display.GlyphTableIndexArray = 'None'
sphere1Display.GaussianRadius = 0.00012500000186264516
sphere1Display.SetScaleArray = ['POINTS', 'Normals']
sphere1Display.ScaleTransferFunction = 'PiecewiseFunction'
sphere1Display.OpacityArray = ['POINTS', 'Normals']
sphere1Display.OpacityTransferFunction = 'PiecewiseFunction'
sphere1Display.DataAxesGrid = 'GridAxesRepresentation'
sphere1Display.PolarAxes = 'PolarAxesRepresentation'
sphere1Display.SelectInputVectors = ['POINTS', 'Normals']
sphere1Display.WriteLog = ''

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
sphere1Display.ScaleTransferFunction.Points = [-0.9999904036521912, 0.0, 0.5, 0.0, 0.9999904036521912, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
sphere1Display.OpacityTransferFunction.Points = [-0.9999904036521912, 0.0, 0.5, 0.0, 0.9999904036521912, 1.0, 0.5, 0.0]

# show data from streamTracer3
streamTracer3Display = Show(streamTracer3, renderView1, 'GeometryRepresentation')

# trace defaults for the display properties.
streamTracer3Display.Representation = 'Surface'
streamTracer3Display.ColorArrayName = ['POINTS', '']
streamTracer3Display.SelectTCoordArray = 'None'
streamTracer3Display.SelectNormalArray = 'None'
streamTracer3Display.SelectTangentArray = 'None'
streamTracer3Display.OSPRayScaleArray = 'AngularVelocity'
streamTracer3Display.OSPRayScaleFunction = 'PiecewiseFunction'
streamTracer3Display.SelectOrientationVectors = 'None'
streamTracer3Display.ScaleFactor = 0.07214184447730076
streamTracer3Display.SelectScaleArray = 'AngularVelocity'
streamTracer3Display.GlyphType = 'Arrow'
streamTracer3Display.GlyphTableIndexArray = 'AngularVelocity'
streamTracer3Display.GaussianRadius = 0.003607092223865038
streamTracer3Display.SetScaleArray = ['POINTS', 'AngularVelocity']
streamTracer3Display.ScaleTransferFunction = 'PiecewiseFunction'
streamTracer3Display.OpacityArray = ['POINTS', 'AngularVelocity']
streamTracer3Display.OpacityTransferFunction = 'PiecewiseFunction'
streamTracer3Display.DataAxesGrid = 'GridAxesRepresentation'
streamTracer3Display.PolarAxes = 'PolarAxesRepresentation'
streamTracer3Display.SelectInputVectors = ['POINTS', 'Velocity']
streamTracer3Display.WriteLog = ''

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
streamTracer3Display.ScaleTransferFunction.Points = [0.0, 0.0, 0.5, 0.0, 1.1757813367477812e-38, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
streamTracer3Display.OpacityTransferFunction.Points = [0.0, 0.0, 0.5, 0.0, 1.1757813367477812e-38, 1.0, 0.5, 0.0]

# show data from streamTracer1
streamTracer1Display = Show(streamTracer1, renderView1, 'GeometryRepresentation')

# trace defaults for the display properties.
streamTracer1Display.Representation = 'Surface'
streamTracer1Display.ColorArrayName = ['POINTS', '']
streamTracer1Display.SelectTCoordArray = 'None'
streamTracer1Display.SelectNormalArray = 'None'
streamTracer1Display.SelectTangentArray = 'None'
streamTracer1Display.OSPRayScaleArray = 'AngularVelocity'
streamTracer1Display.OSPRayScaleFunction = 'PiecewiseFunction'
streamTracer1Display.SelectOrientationVectors = 'None'
streamTracer1Display.ScaleFactor = 0.009970790147781372
streamTracer1Display.SelectScaleArray = 'AngularVelocity'
streamTracer1Display.GlyphType = 'Arrow'
streamTracer1Display.GlyphTableIndexArray = 'AngularVelocity'
streamTracer1Display.GaussianRadius = 0.0004985395073890686
streamTracer1Display.SetScaleArray = ['POINTS', 'AngularVelocity']
streamTracer1Display.ScaleTransferFunction = 'PiecewiseFunction'
streamTracer1Display.OpacityArray = ['POINTS', 'AngularVelocity']
streamTracer1Display.OpacityTransferFunction = 'PiecewiseFunction'
streamTracer1Display.DataAxesGrid = 'GridAxesRepresentation'
streamTracer1Display.PolarAxes = 'PolarAxesRepresentation'
streamTracer1Display.SelectInputVectors = ['POINTS', 'Velocity']
streamTracer1Display.WriteLog = ''

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
streamTracer1Display.ScaleTransferFunction.Points = [0.0, 0.0, 0.5, 0.0, 1.1757813367477812e-38, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
streamTracer1Display.OpacityTransferFunction.Points = [0.0, 0.0, 0.5, 0.0, 1.1757813367477812e-38, 1.0, 0.5, 0.0]

# show data from gradient1
gradient1Display = Show(gradient1, renderView1, 'AMRRepresentation')

# get 2D transfer function for 'Velocity'
velocityTF2D = GetTransferFunction2D('Velocity')
velocityTF2D.ScalarRangeInitialized = 1
velocityTF2D.Range = [4.8797625230629965e-05, 0.04070702741712184, 0.0, 1.0]

# get color transfer function/color map for 'Velocity'
velocityLUT = GetColorTransferFunction('Velocity')
velocityLUT.TransferFunction2D = velocityTF2D
velocityLUT.RGBPoints = [1.2598240019144161e-07, 0.231373, 0.298039, 0.752941, 0.017717216499238984, 0.865003, 0.865003, 0.865003, 0.035434307016077776, 0.705882, 0.0156863, 0.14902]
velocityLUT.ScalarRangeInitialized = 1.0

# get opacity transfer function/opacity map for 'Velocity'
velocityPWF = GetOpacityTransferFunction('Velocity')
velocityPWF.Points = [1.2598240019144161e-07, 0.0, 0.5, 0.0, 0.035434307016077776, 1.0, 0.5, 0.0]
velocityPWF.ScalarRangeInitialized = 1

# trace defaults for the display properties.
gradient1Display.Representation = 'Surface'
gradient1Display.ColorArrayName = ['CELLS', 'Velocity']
gradient1Display.LookupTable = velocityLUT
gradient1Display.SelectTCoordArray = 'None'
gradient1Display.SelectNormalArray = 'None'
gradient1Display.SelectTangentArray = 'None'
gradient1Display.OSPRayScaleFunction = 'PiecewiseFunction'
gradient1Display.SelectOrientationVectors = 'Velocity'
gradient1Display.ScaleFactor = 0.1
gradient1Display.SelectScaleArray = 'None'
gradient1Display.GlyphType = 'Arrow'
gradient1Display.GlyphTableIndexArray = 'None'
gradient1Display.GaussianRadius = 0.005
gradient1Display.SetScaleArray = ['POINTS', '']
gradient1Display.ScaleTransferFunction = 'PiecewiseFunction'
gradient1Display.OpacityArray = ['POINTS', '']
gradient1Display.OpacityTransferFunction = 'PiecewiseFunction'
gradient1Display.DataAxesGrid = 'GridAxesRepresentation'
gradient1Display.PolarAxes = 'PolarAxesRepresentation'
gradient1Display.ScalarOpacityUnitDistance = 0.011636464267690988
gradient1Display.ScalarOpacityFunction = velocityPWF

# ----------------------------------------------------------------
# setup color maps and opacity mapes used in the visualization
# note: the Get..() functions create a new object, if needed
# ----------------------------------------------------------------

# ----------------------------------------------------------------
# restore active source
SetActiveSource(cyl_lev3_Re40_)
# ----------------------------------------------------------------


if __name__ == '__main__':
    # generate extracts
    SaveExtracts(ExtractsOutputDirectory='extracts')