# trace generated using paraview version 5.11.2
#import paraview
#paraview.compatibility.major = 5
#paraview.compatibility.minor = 11

#### import the simple module from the paraview
from paraview.simple import *
#### disable automatic camera reset on 'Show'
paraview.simple._DisableFirstRenderCameraReset()

# get active source.
sphere_cumu_Re20000_02000 = GetActiveSource()

# Properties modified on sphere_cumu_Re20000_02000
sphere_cumu_Re20000_02000.Level = 8
sphere_cumu_Re20000_02000.CellArrayStatus = ['ux', 'uy', 'uz']

# get active view
renderView1 = GetActiveViewOrCreate('RenderView')

# show data in view
sphere_cumu_Re20000_02000Display = Show(sphere_cumu_Re20000_02000, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
sphere_cumu_Re20000_02000Display.Representation = 'Outline'
sphere_cumu_Re20000_02000Display.ColorArrayName = [None, '']
sphere_cumu_Re20000_02000Display.SelectTCoordArray = 'None'
sphere_cumu_Re20000_02000Display.SelectNormalArray = 'None'
sphere_cumu_Re20000_02000Display.SelectTangentArray = 'None'
sphere_cumu_Re20000_02000Display.OSPRayScaleFunction = 'PiecewiseFunction'
sphere_cumu_Re20000_02000Display.SelectOrientationVectors = 'None'
sphere_cumu_Re20000_02000Display.ScaleFactor = 1.6
sphere_cumu_Re20000_02000Display.SelectScaleArray = 'None'
sphere_cumu_Re20000_02000Display.GlyphType = 'Arrow'
sphere_cumu_Re20000_02000Display.GlyphTableIndexArray = 'None'
sphere_cumu_Re20000_02000Display.GaussianRadius = 0.08
sphere_cumu_Re20000_02000Display.SetScaleArray = [None, '']
sphere_cumu_Re20000_02000Display.ScaleTransferFunction = 'PiecewiseFunction'
sphere_cumu_Re20000_02000Display.OpacityArray = [None, '']
sphere_cumu_Re20000_02000Display.OpacityTransferFunction = 'PiecewiseFunction'
sphere_cumu_Re20000_02000Display.DataAxesGrid = 'GridAxesRepresentation'
sphere_cumu_Re20000_02000Display.PolarAxes = 'PolarAxesRepresentation'
sphere_cumu_Re20000_02000Display.ScalarOpacityUnitDistance = 0.03431090639382939

# reset view to fit data
renderView1.ResetCamera(False)

# get the material library
materialLibrary1 = GetMaterialLibrary()

# get animation scene
animationScene1 = GetAnimationScene()

# update animation scene based on data timesteps
animationScene1.UpdateAnimationUsingDataTimeSteps()

# create a new 'Calculator'
calculator1 = Calculator(registrationName='Calculator1', Input=sphere_cumu_Re20000_02000)
calculator1.AttributeType = 'Cell Data'
calculator1.Function = ''

# Properties modified on calculator1
calculator1.ResultArrayName = 'Vel'
calculator1.Function = 'ux*iHat+uy*jHat+uz*kHat'

# show data in view
calculator1Display = Show(calculator1, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
calculator1Display.Representation = 'Outline'
calculator1Display.ColorArrayName = [None, '']
calculator1Display.SelectTCoordArray = 'None'
calculator1Display.SelectNormalArray = 'None'
calculator1Display.SelectTangentArray = 'None'
calculator1Display.OSPRayScaleFunction = 'PiecewiseFunction'
calculator1Display.SelectOrientationVectors = 'Vel'
calculator1Display.ScaleFactor = 1.6
calculator1Display.SelectScaleArray = 'None'
calculator1Display.GlyphType = 'Arrow'
calculator1Display.GlyphTableIndexArray = 'None'
calculator1Display.GaussianRadius = 0.08
calculator1Display.SetScaleArray = [None, '']
calculator1Display.ScaleTransferFunction = 'PiecewiseFunction'
calculator1Display.OpacityArray = [None, '']
calculator1Display.OpacityTransferFunction = 'PiecewiseFunction'
calculator1Display.DataAxesGrid = 'GridAxesRepresentation'
calculator1Display.PolarAxes = 'PolarAxesRepresentation'
calculator1Display.ScalarOpacityUnitDistance = 0.03431090639382939

# hide data in view
Hide(sphere_cumu_Re20000_02000, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# create a new 'Gradient'
gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
gradient1.ScalarArray = ['CELLS', 'Vel']

# Properties modified on gradient1
gradient1.ComputeVorticity = 1
gradient1.ComputeQCriterion = 1

# show data in view
gradient1Display = Show(gradient1, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
gradient1Display.Representation = 'Outline'
gradient1Display.ColorArrayName = [None, '']
gradient1Display.SelectTCoordArray = 'None'
gradient1Display.SelectNormalArray = 'None'
gradient1Display.SelectTangentArray = 'None'
gradient1Display.OSPRayScaleFunction = 'PiecewiseFunction'
gradient1Display.SelectOrientationVectors = 'Vel'
gradient1Display.ScaleFactor = 1.6
gradient1Display.SelectScaleArray = 'None'
gradient1Display.GlyphType = 'Arrow'
gradient1Display.GlyphTableIndexArray = 'None'
gradient1Display.GaussianRadius = 0.08
gradient1Display.SetScaleArray = [None, '']
gradient1Display.ScaleTransferFunction = 'PiecewiseFunction'
gradient1Display.OpacityArray = [None, '']
gradient1Display.OpacityTransferFunction = 'PiecewiseFunction'
gradient1Display.DataAxesGrid = 'GridAxesRepresentation'
gradient1Display.PolarAxes = 'PolarAxesRepresentation'
gradient1Display.ScalarOpacityUnitDistance = 0.03431090639382939

# hide data in view
Hide(calculator1, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# create a new 'Cell Data to Point Data'
cellDatatoPointData1 = CellDatatoPointData(registrationName='CellDatatoPointData1', Input=gradient1)
cellDatatoPointData1.CellDataArraytoprocess = ['Gradient', 'Q Criterion', 'Vel', 'Vorticity', 'ux', 'uy', 'uz', 'vtkGhostType']

# show data in view
cellDatatoPointData1Display = Show(cellDatatoPointData1, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
cellDatatoPointData1Display.Representation = 'Outline'
cellDatatoPointData1Display.ColorArrayName = [None, '']
cellDatatoPointData1Display.SelectTCoordArray = 'None'
cellDatatoPointData1Display.SelectNormalArray = 'None'
cellDatatoPointData1Display.SelectTangentArray = 'None'
cellDatatoPointData1Display.OSPRayScaleArray = 'Gradient'
cellDatatoPointData1Display.OSPRayScaleFunction = 'PiecewiseFunction'
cellDatatoPointData1Display.SelectOrientationVectors = 'Vel'
cellDatatoPointData1Display.ScaleFactor = 1.6
cellDatatoPointData1Display.SelectScaleArray = 'None'
cellDatatoPointData1Display.GlyphType = 'Arrow'
cellDatatoPointData1Display.GlyphTableIndexArray = 'Gradient'
cellDatatoPointData1Display.GaussianRadius = 0.08
cellDatatoPointData1Display.SetScaleArray = ['POINTS', 'Gradient']
cellDatatoPointData1Display.ScaleTransferFunction = 'PiecewiseFunction'
cellDatatoPointData1Display.OpacityArray = ['POINTS', 'Gradient']
cellDatatoPointData1Display.OpacityTransferFunction = 'PiecewiseFunction'
cellDatatoPointData1Display.DataAxesGrid = 'GridAxesRepresentation'
cellDatatoPointData1Display.PolarAxes = 'PolarAxesRepresentation'
cellDatatoPointData1Display.ScalarOpacityUnitDistance = 0.03431090639382939

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
cellDatatoPointData1Display.ScaleTransferFunction.Points = [-6.570676557876331, 0.0, 0.5, 0.0, 3.7901733986796664, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
cellDatatoPointData1Display.OpacityTransferFunction.Points = [-6.570676557876331, 0.0, 0.5, 0.0, 3.7901733986796664, 1.0, 0.5, 0.0]

# hide data in view
Hide(gradient1, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# create a new 'Contour'
contour1 = Contour(registrationName='Contour1', Input=cellDatatoPointData1)
contour1.ContourBy = ['POINTS', 'Q Criterion']
contour1.Isosurfaces = [40.42551212490254]
contour1.PointMergeMethod = 'Uniform Binning'

# Properties modified on contour1
contour1.Isosurfaces = [0.001]

# show data in view
contour1Display = Show(contour1, renderView1, 'GeometryRepresentation')

# get color transfer function/color map for 'QCriterion'
qCriterionLUT = GetColorTransferFunction('QCriterion')

# trace defaults for the display properties.
contour1Display.Representation = 'Surface'
contour1Display.ColorArrayName = ['POINTS', 'Q Criterion']
contour1Display.LookupTable = qCriterionLUT
contour1Display.SelectTCoordArray = 'None'
contour1Display.SelectNormalArray = 'Normals'
contour1Display.SelectTangentArray = 'None'
contour1Display.OSPRayScaleArray = 'Q Criterion'
contour1Display.OSPRayScaleFunction = 'PiecewiseFunction'
contour1Display.SelectOrientationVectors = 'Vel'
contour1Display.ScaleFactor = 0.6003211975097656
contour1Display.SelectScaleArray = 'Q Criterion'
contour1Display.GlyphType = 'Arrow'
contour1Display.GlyphTableIndexArray = 'Q Criterion'
contour1Display.GaussianRadius = 0.03001605987548828
contour1Display.SetScaleArray = ['POINTS', 'Q Criterion']
contour1Display.ScaleTransferFunction = 'PiecewiseFunction'
contour1Display.OpacityArray = ['POINTS', 'Q Criterion']
contour1Display.OpacityTransferFunction = 'PiecewiseFunction'
contour1Display.DataAxesGrid = 'GridAxesRepresentation'
contour1Display.PolarAxes = 'PolarAxesRepresentation'
contour1Display.SelectInputVectors = ['POINTS', 'Vel']
contour1Display.WriteLog = ''

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
contour1Display.ScaleTransferFunction.Points = [0.001, 0.0, 0.5, 0.0, 0.0010002384660765529, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
contour1Display.OpacityTransferFunction.Points = [0.001, 0.0, 0.5, 0.0, 0.0010002384660765529, 1.0, 0.5, 0.0]

# show color bar/color legend
contour1Display.SetScalarBarVisibility(renderView1, True)

# update the view to ensure updated data information
renderView1.Update()

# get opacity transfer function/opacity map for 'QCriterion'
qCriterionPWF = GetOpacityTransferFunction('QCriterion')

# get 2D transfer function for 'QCriterion'
qCriterionTF2D = GetTransferFunction2D('QCriterion')

# set active source
SetActiveSource(cellDatatoPointData1)

# set active source
SetActiveSource(contour1)

# set scalar coloring
ColorBy(contour1Display, ('POINTS', 'Vel', 'Magnitude'))

# Hide the scalar bar for this color map if no visible data is colored by it.
HideScalarBarIfNotNeeded(qCriterionLUT, renderView1)

# rescale color and/or opacity maps used to include current data range
contour1Display.RescaleTransferFunctionToDataRange(True, False)

# show color bar/color legend
contour1Display.SetScalarBarVisibility(renderView1, True)

# get color transfer function/color map for 'Vel'
velLUT = GetColorTransferFunction('Vel')

# get opacity transfer function/opacity map for 'Vel'
velPWF = GetOpacityTransferFunction('Vel')

# get 2D transfer function for 'Vel'
velTF2D = GetTransferFunction2D('Vel')

# Rescale transfer function
velLUT.RescaleTransferFunction(1.689427725706336e-05, 0.01)

# Rescale transfer function
velPWF.RescaleTransferFunction(1.689427725706336e-05, 0.01)

# Rescale 2D transfer function
velTF2D.RescaleTransferFunction(1.689427725706336e-05, 0.01, 0.0, 1.0)

# Rescale transfer function
velLUT.RescaleTransferFunction(1.689427725706336e-05, 0.08)

# Rescale transfer function
velPWF.RescaleTransferFunction(1.689427725706336e-05, 0.08)

# Rescale 2D transfer function
velTF2D.RescaleTransferFunction(1.689427725706336e-05, 0.08, 0.0, 1.0)

# Rescale transfer function
velLUT.RescaleTransferFunction(0.0, 1.0)

# Rescale transfer function
velPWF.RescaleTransferFunction(0.0, 1.0)

# Rescale 2D transfer function
velTF2D.RescaleTransferFunction(0.0, 1.0, 0.0, 1.0)

# Rescale transfer function
velLUT.RescaleTransferFunction(0.0, 0.1)

# Rescale transfer function
velPWF.RescaleTransferFunction(0.0, 0.1)

# Rescale 2D transfer function
velTF2D.RescaleTransferFunction(0.0, 0.1, 0.0, 1.0)

renderView1.ResetActiveCameraToNegativeZ()

# reset view to fit data
renderView1.ResetCamera(False)

#================================================================
# addendum: following script captures some of the application
# state to faithfully reproduce the visualization during playback
#================================================================

# get layout
layout1 = GetLayout()

#--------------------------------
# saving layout sizes for layouts

# layout/tab size in pixels
layout1.SetSize(1328, 724)

#-----------------------------------
# saving camera placements for views

# current camera placement for renderView1
renderView1.CameraPosition = [3.0949852634380655, -1.0917013818832553, 5.135130628264363]
renderView1.CameraFocalPoint = [5.687460126191309, 5.859907964550177, 5.21596450617113]
renderView1.CameraViewUp = [0.07755558728336176, -0.0405064154803184, 0.9961648263144353]
renderView1.CameraParallelScale = 10.677078252031311

#--------------------------------------------
# uncomment the following to render all views
# RenderAllViews()
# alternatively, if you want to write images, you can use SaveScreenshot(...).