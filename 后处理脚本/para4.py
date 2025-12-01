# trace generated using paraview version 5.11.2
#import paraview
#paraview.compatibility.major = 5
#paraview.compatibility.minor = 11

#### import the simple module from the paraview
from paraview.simple import *
#### disable automatic camera reset on 'Show'
paraview.simple._DisableFirstRenderCameraReset()

# create a new 'AMReX/BoxLib Grid Reader'
cyl_lev0_Re100_483543_96 = AMReXBoxLibGridReader(registrationName='cyl_lev0_Re100_483543_96', FileNames=['F:\\data\\cyl_lev0_Re100_483543_96'])
cyl_lev0_Re100_483543_96.CellArrayStatus = []

# get animation scene
animationScene1 = GetAnimationScene()

# update animation scene based on data timesteps
animationScene1.UpdateAnimationUsingDataTimeSteps()

# Properties modified on cyl_lev0_Re100_483543_96
cyl_lev0_Re100_483543_96.Level = 2
cyl_lev0_Re100_483543_96.CellArrayStatus = ['ux', 'uy']

# get active view
renderView1 = GetActiveViewOrCreate('RenderView')

# show data in view
cyl_lev0_Re100_483543_96Display = Show(cyl_lev0_Re100_483543_96, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
cyl_lev0_Re100_483543_96Display.Representation = 'Outline'
cyl_lev0_Re100_483543_96Display.ColorArrayName = [None, '']
cyl_lev0_Re100_483543_96Display.SelectTCoordArray = 'None'
cyl_lev0_Re100_483543_96Display.SelectNormalArray = 'None'
cyl_lev0_Re100_483543_96Display.SelectTangentArray = 'None'
cyl_lev0_Re100_483543_96Display.OSPRayScaleFunction = 'PiecewiseFunction'
cyl_lev0_Re100_483543_96Display.SelectOrientationVectors = 'None'
cyl_lev0_Re100_483543_96Display.ScaleFactor = 0.1
cyl_lev0_Re100_483543_96Display.SelectScaleArray = 'None'
cyl_lev0_Re100_483543_96Display.GlyphType = 'Arrow'
cyl_lev0_Re100_483543_96Display.GlyphTableIndexArray = 'None'
cyl_lev0_Re100_483543_96Display.GaussianRadius = 0.005
cyl_lev0_Re100_483543_96Display.SetScaleArray = [None, '']
cyl_lev0_Re100_483543_96Display.ScaleTransferFunction = 'PiecewiseFunction'
cyl_lev0_Re100_483543_96Display.OpacityArray = [None, '']
cyl_lev0_Re100_483543_96Display.OpacityTransferFunction = 'PiecewiseFunction'
cyl_lev0_Re100_483543_96Display.DataAxesGrid = 'GridAxesRepresentation'
cyl_lev0_Re100_483543_96Display.PolarAxes = 'PolarAxesRepresentation'
cyl_lev0_Re100_483543_96Display.ScalarOpacityUnitDistance = 0.013920292470942803

# reset view to fit data
renderView1.ResetCamera(False)

#changing interaction mode based on data extents
renderView1.InteractionMode = '2D'
renderView1.CameraPosition = [0.5, 0.5, 3.35]
renderView1.CameraFocalPoint = [0.5, 0.5, 0.0]

# get the material library
materialLibrary1 = GetMaterialLibrary()

# update animation scene based on data timesteps
animationScene1.UpdateAnimationUsingDataTimeSteps()

# create a new 'Calculator'
calculator1 = Calculator(registrationName='Calculator1', Input=cyl_lev0_Re100_483543_96)
calculator1.AttributeType = 'Cell Data'
calculator1.Function = ''

# Properties modified on calculator1
calculator1.Function = 'ux*iHat+uy*jHat'

# show data in view
calculator1Display = Show(calculator1, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
calculator1Display.Representation = 'Outline'
calculator1Display.ColorArrayName = [None, '']
calculator1Display.SelectTCoordArray = 'None'
calculator1Display.SelectNormalArray = 'None'
calculator1Display.SelectTangentArray = 'None'
calculator1Display.OSPRayScaleFunction = 'PiecewiseFunction'
calculator1Display.SelectOrientationVectors = 'Result'
calculator1Display.ScaleFactor = 0.1
calculator1Display.SelectScaleArray = 'None'
calculator1Display.GlyphType = 'Arrow'
calculator1Display.GlyphTableIndexArray = 'None'
calculator1Display.GaussianRadius = 0.005
calculator1Display.SetScaleArray = [None, '']
calculator1Display.ScaleTransferFunction = 'PiecewiseFunction'
calculator1Display.OpacityArray = [None, '']
calculator1Display.OpacityTransferFunction = 'PiecewiseFunction'
calculator1Display.DataAxesGrid = 'GridAxesRepresentation'
calculator1Display.PolarAxes = 'PolarAxesRepresentation'
calculator1Display.ScalarOpacityUnitDistance = 0.013920292470942803

# hide data in view
Hide(cyl_lev0_Re100_483543_96, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# create a new 'Gradient'
gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
gradient1.ScalarArray = ['CELLS', 'Result']

# Properties modified on gradient1
gradient1.ComputeGradient = 0
gradient1.ComputeVorticity = 1

# show data in view
gradient1Display = Show(gradient1, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
gradient1Display.Representation = 'Outline'
gradient1Display.ColorArrayName = [None, '']
gradient1Display.SelectTCoordArray = 'None'
gradient1Display.SelectNormalArray = 'None'
gradient1Display.SelectTangentArray = 'None'
gradient1Display.OSPRayScaleFunction = 'PiecewiseFunction'
gradient1Display.SelectOrientationVectors = 'Result'
gradient1Display.ScaleFactor = 0.1
gradient1Display.SelectScaleArray = 'None'
gradient1Display.GlyphType = 'Arrow'
gradient1Display.GlyphTableIndexArray = 'None'
gradient1Display.GaussianRadius = 0.005
gradient1Display.SetScaleArray = [None, '']
gradient1Display.ScaleTransferFunction = 'PiecewiseFunction'
gradient1Display.OpacityArray = [None, '']
gradient1Display.OpacityTransferFunction = 'PiecewiseFunction'
gradient1Display.DataAxesGrid = 'GridAxesRepresentation'
gradient1Display.PolarAxes = 'PolarAxesRepresentation'
gradient1Display.ScalarOpacityUnitDistance = 0.013920292470942803

# hide data in view
Hide(calculator1, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# create a new 'Calculator'
calculator2 = Calculator(registrationName='Calculator2', Input=gradient1)
calculator2.AttributeType = 'Cell Data'
calculator2.Function = ''

# Properties modified on calculator2
calculator2.ResultArrayName = 'vort'
calculator2.Function = 'abs(Vorticity_Z)'

# show data in view
calculator2Display = Show(calculator2, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
calculator2Display.Representation = 'Outline'
calculator2Display.ColorArrayName = ['CELLS', '']
calculator2Display.SelectTCoordArray = 'None'
calculator2Display.SelectNormalArray = 'None'
calculator2Display.SelectTangentArray = 'None'
calculator2Display.OSPRayScaleFunction = 'PiecewiseFunction'
calculator2Display.SelectOrientationVectors = 'Result'
calculator2Display.ScaleFactor = 0.1
calculator2Display.SelectScaleArray = 'vort'
calculator2Display.GlyphType = 'Arrow'
calculator2Display.GlyphTableIndexArray = 'vort'
calculator2Display.GaussianRadius = 0.005
calculator2Display.SetScaleArray = [None, '']
calculator2Display.ScaleTransferFunction = 'PiecewiseFunction'
calculator2Display.OpacityArray = [None, '']
calculator2Display.OpacityTransferFunction = 'PiecewiseFunction'
calculator2Display.DataAxesGrid = 'GridAxesRepresentation'
calculator2Display.PolarAxes = 'PolarAxesRepresentation'
calculator2Display.ScalarOpacityUnitDistance = 0.013920292470942803

# hide data in view
Hide(gradient1, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# set scalar coloring
ColorBy(calculator2Display, ('CELLS', 'Vorticity', 'Magnitude'))

# rescale color and/or opacity maps used to include current data range
calculator2Display.RescaleTransferFunctionToDataRange(True, False)

# show color bar/color legend
calculator2Display.SetScalarBarVisibility(renderView1, True)

# get color transfer function/color map for 'Vorticity'
vorticityLUT = GetColorTransferFunction('Vorticity')

# get opacity transfer function/opacity map for 'Vorticity'
vorticityPWF = GetOpacityTransferFunction('Vorticity')

# get 2D transfer function for 'Vorticity'
vorticityTF2D = GetTransferFunction2D('Vorticity')

# change representation type
calculator2Display.SetRepresentationType('Surface')

# create a new 'Cell Data to Point Data'
cellDatatoPointData1 = CellDatatoPointData(registrationName='CellDatatoPointData1', Input=calculator2)
cellDatatoPointData1.CellDataArraytoprocess = ['Result', 'Vorticity', 'ux', 'uy', 'vort', 'vtkGhostType']

# show data in view
cellDatatoPointData1Display = Show(cellDatatoPointData1, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
cellDatatoPointData1Display.Representation = 'Outline'
cellDatatoPointData1Display.ColorArrayName = ['POINTS', '']
cellDatatoPointData1Display.SelectTCoordArray = 'None'
cellDatatoPointData1Display.SelectNormalArray = 'None'
cellDatatoPointData1Display.SelectTangentArray = 'None'
cellDatatoPointData1Display.OSPRayScaleArray = 'vort'
cellDatatoPointData1Display.OSPRayScaleFunction = 'PiecewiseFunction'
cellDatatoPointData1Display.SelectOrientationVectors = 'Result'
cellDatatoPointData1Display.ScaleFactor = 0.1
cellDatatoPointData1Display.SelectScaleArray = 'vort'
cellDatatoPointData1Display.GlyphType = 'Arrow'
cellDatatoPointData1Display.GlyphTableIndexArray = 'vort'
cellDatatoPointData1Display.GaussianRadius = 0.005
cellDatatoPointData1Display.SetScaleArray = ['POINTS', 'vort']
cellDatatoPointData1Display.ScaleTransferFunction = 'PiecewiseFunction'
cellDatatoPointData1Display.OpacityArray = ['POINTS', 'vort']
cellDatatoPointData1Display.OpacityTransferFunction = 'PiecewiseFunction'
cellDatatoPointData1Display.DataAxesGrid = 'GridAxesRepresentation'
cellDatatoPointData1Display.PolarAxes = 'PolarAxesRepresentation'
cellDatatoPointData1Display.ScalarOpacityUnitDistance = 0.013920292470942803

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
cellDatatoPointData1Display.ScaleTransferFunction.Points = [6.63126833444494e-08, 0.0, 0.5, 0.0, 5.81232653522353, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
cellDatatoPointData1Display.OpacityTransferFunction.Points = [6.63126833444494e-08, 0.0, 0.5, 0.0, 5.81232653522353, 1.0, 0.5, 0.0]

# hide data in view
Hide(calculator2, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# create a new 'Contour'
contour1 = Contour(registrationName='Contour1', Input=cellDatatoPointData1)
contour1.ContourBy = ['POINTS', 'vort']
contour1.Isosurfaces = [2.9061633007681067]
contour1.PointMergeMethod = 'Uniform Binning'

# set active source
SetActiveSource(contour1)

# show data in view
contour1Display = Show(contour1, renderView1, 'GeometryRepresentation')

# get color transfer function/color map for 'vort'
vortLUT = GetColorTransferFunction('vort')

# trace defaults for the display properties.
contour1Display.Representation = 'Surface'
contour1Display.ColorArrayName = ['POINTS', 'vort']
contour1Display.LookupTable = vortLUT
contour1Display.SelectTCoordArray = 'None'
contour1Display.SelectNormalArray = 'None'
contour1Display.SelectTangentArray = 'None'
contour1Display.OSPRayScaleArray = 'vort'
contour1Display.OSPRayScaleFunction = 'PiecewiseFunction'
contour1Display.SelectOrientationVectors = 'Result'
contour1Display.ScaleFactor = 0.008626607060432435
contour1Display.SelectScaleArray = 'vort'
contour1Display.GlyphType = 'Arrow'
contour1Display.GlyphTableIndexArray = 'vort'
contour1Display.GaussianRadius = 0.0004313303530216217
contour1Display.SetScaleArray = ['POINTS', 'vort']
contour1Display.ScaleTransferFunction = 'PiecewiseFunction'
contour1Display.OpacityArray = ['POINTS', 'vort']
contour1Display.OpacityTransferFunction = 'PiecewiseFunction'
contour1Display.DataAxesGrid = 'GridAxesRepresentation'
contour1Display.PolarAxes = 'PolarAxesRepresentation'
contour1Display.SelectInputVectors = ['POINTS', 'Result']
contour1Display.WriteLog = ''

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
contour1Display.ScaleTransferFunction.Points = [2.9061633007681067, 0.0, 0.5, 0.0, 2.906651496887207, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
contour1Display.OpacityTransferFunction.Points = [2.9061633007681067, 0.0, 0.5, 0.0, 2.906651496887207, 1.0, 0.5, 0.0]

# show color bar/color legend
contour1Display.SetScalarBarVisibility(renderView1, True)

# get opacity transfer function/opacity map for 'vort'
vortPWF = GetOpacityTransferFunction('vort')

# get 2D transfer function for 'vort'
vortTF2D = GetTransferFunction2D('vort')

# Properties modified on contour1
contour1.Isosurfaces = [0.17, 0.5, 1.0, 2.0, 3.0, 4.0]

# show data in view
contour1Display = Show(contour1, renderView1, 'GeometryRepresentation')

# show color bar/color legend
contour1Display.SetScalarBarVisibility(renderView1, True)

# update the view to ensure updated data information
renderView1.Update()

# Rescale transfer function
vortLUT.RescaleTransferFunction(0.17, 4.0)

# Rescale transfer function
vortPWF.RescaleTransferFunction(0.17, 4.0)

# turn off scalar coloring
ColorBy(contour1Display, None)

# Hide the scalar bar for this color map if no visible data is colored by it.
HideScalarBarIfNotNeeded(vortLUT, renderView1)

# change solid color
contour1Display.AmbientColor = [0.0, 0.0, 0.0]
contour1Display.DiffuseColor = [0.0, 0.0, 0.0]

# set active source
SetActiveSource(cellDatatoPointData1)

# set scalar coloring
ColorBy(cellDatatoPointData1Display, ('POINTS', 'Vorticity', 'Magnitude'))

# rescale color and/or opacity maps used to include current data range
cellDatatoPointData1Display.RescaleTransferFunctionToDataRange(True, False)

# show color bar/color legend
cellDatatoPointData1Display.SetScalarBarVisibility(renderView1, True)

# change representation type
cellDatatoPointData1Display.SetRepresentationType('Surface')

# Rescale transfer function
vorticityLUT.RescaleTransferFunction(-1.0, 1.0)

# Rescale transfer function
vorticityPWF.RescaleTransferFunction(-1.0, 1.0)

# Rescale 2D transfer function
vorticityTF2D.RescaleTransferFunction(-1.0, 1.0, 0.0, 1.0)

# set scalar coloring
ColorBy(cellDatatoPointData1Display, ('POINTS', 'Vorticity', 'Z'))

# rescale color and/or opacity maps used to exactly fit the current data range
cellDatatoPointData1Display.RescaleTransferFunctionToDataRange(False, False)

# Update a scalar bar component title.
UpdateScalarBarsComponentTitle(vorticityLUT, cellDatatoPointData1Display)

# Rescale transfer function
vorticityLUT.RescaleTransferFunction(-1.0, 1.0)

# Rescale transfer function
vorticityPWF.RescaleTransferFunction(-1.0, 1.0)

# Hide orientation axes
renderView1.OrientationAxesVisibility = 0

# get color legend/bar for vorticityLUT in view renderView1
vorticityLUTColorBar = GetScalarBar(vorticityLUT, renderView1)

# change scalar bar placement
vorticityLUTColorBar.Position = [0.9168956043956044, 0.016150740242261104]

# hide color bar/color legend
cellDatatoPointData1Display.SetScalarBarVisibility(renderView1, False)

#================================================================
# addendum: following script captures some of the application
# state to faithfully reproduce the visualization during playback
#================================================================

# get layout
layout1 = GetLayout()

#--------------------------------
# saving layout sizes for layouts

# layout/tab size in pixels
layout1.SetSize(1456, 743)

#-----------------------------------
# saving camera placements for views

# current camera placement for renderView1
renderView1.InteractionMode = '2D'
renderView1.CameraPosition = [0.5, 0.5, 3.35]
renderView1.CameraFocalPoint = [0.5, 0.5, 0.0]
renderView1.CameraParallelScale = 0.1862033156181882

#--------------------------------------------
# uncomment the following to render all views
# RenderAllViews()
# alternatively, if you want to write images, you can use SaveScreenshot(...).