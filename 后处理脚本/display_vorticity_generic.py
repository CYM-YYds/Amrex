# trace generated using paraview version 5.11.2
#import paraview
#paraview.compatibility.major = 5
#paraview.compatibility.minor = 11

#### import the simple module from the paraview
from paraview.simple import *
#### disable automatic camera reset on 'Show'
paraview.simple._DisableFirstRenderCameraReset()

# get active source.
data_source = GetActiveSource()

# Properties modified on data_source
data_source.Level = 3
data_source.CellArrayStatus = ['ux', 'uy']

# get active view
renderView1 = GetActiveViewOrCreate('RenderView')

# show data in view
data_sourceDisplay = Show(data_source, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
data_sourceDisplay.Representation = 'Outline'
data_sourceDisplay.ColorArrayName = [None, '']
data_sourceDisplay.SelectTCoordArray = 'None'
data_sourceDisplay.SelectNormalArray = 'None'
data_sourceDisplay.SelectTangentArray = 'None'
data_sourceDisplay.OSPRayScaleFunction = 'PiecewiseFunction'
data_sourceDisplay.SelectOrientationVectors = 'None'
data_sourceDisplay.ScaleFactor = 0.1
data_sourceDisplay.SelectScaleArray = 'None'
data_sourceDisplay.GlyphType = 'Arrow'
data_sourceDisplay.GlyphTableIndexArray = 'None'
data_sourceDisplay.GaussianRadius = 0.005
data_sourceDisplay.SetScaleArray = [None, '']
data_sourceDisplay.ScaleTransferFunction = 'PiecewiseFunction'
data_sourceDisplay.OpacityArray = [None, '']
data_sourceDisplay.OpacityTransferFunction = 'PiecewiseFunction'
data_sourceDisplay.DataAxesGrid = 'GridAxesRepresentation'
data_sourceDisplay.PolarAxes = 'PolarAxesRepresentation'
data_sourceDisplay.ScalarOpacityUnitDistance = 0.008769234752416978

# reset view to fit data
renderView1.ResetCamera(False)

#changing interaction mode based on data extents
renderView1.InteractionMode = '2D'
renderView1.CameraPosition = [0.5, 0.5, 3.35]
renderView1.CameraFocalPoint = [0.5, 0.5, 0.0]

# get the material library
materialLibrary1 = GetMaterialLibrary()

# get animation scene
animationScene1 = GetAnimationScene()

# update animation scene based on data timesteps
animationScene1.UpdateAnimationUsingDataTimeSteps()

# create a new 'Calculator'
calculator1 = Calculator(registrationName='Calculator1', Input=data_source)
calculator1.AttributeType = 'Cell Data'
calculator1.Function = ''

# Properties modified on calculator1
calculator1.ResultArrayName = 'Velocity'
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
calculator1Display.SelectOrientationVectors = 'Velocity'
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
calculator1Display.ScalarOpacityUnitDistance = 0.008769234752416978

# hide data in view
Hide(data_source, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# create a new 'Gradient'
gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
gradient1.ScalarArray = ['CELLS', 'Velocity']

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
gradient1Display.SelectOrientationVectors = 'Velocity'
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
gradient1Display.ScalarOpacityUnitDistance = 0.008769234752416978

# hide data in view
Hide(calculator1, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# create a new 'Calculator'
calculator2 = Calculator(registrationName='Calculator2', Input=gradient1)
calculator2.AttributeType = 'Cell Data'
calculator2.Function = ''

# Properties modified on calculator2
calculator2.ResultArrayName = 'vorticity'
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
calculator2Display.SelectOrientationVectors = 'Velocity'
calculator2Display.ScaleFactor = 0.1
calculator2Display.SelectScaleArray = 'vorticity'
calculator2Display.GlyphType = 'Arrow'
calculator2Display.GlyphTableIndexArray = 'vorticity'
calculator2Display.GaussianRadius = 0.005
calculator2Display.SetScaleArray = [None, '']
calculator2Display.ScaleTransferFunction = 'PiecewiseFunction'
calculator2Display.OpacityArray = [None, '']
calculator2Display.OpacityTransferFunction = 'PiecewiseFunction'
calculator2Display.DataAxesGrid = 'GridAxesRepresentation'
calculator2Display.PolarAxes = 'PolarAxesRepresentation'
calculator2Display.ScalarOpacityUnitDistance = 0.008769234752416978

# hide data in view
Hide(gradient1, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# set active source
SetActiveSource(gradient1)

# set scalar coloring
ColorBy(gradient1Display, ('CELLS', 'Vorticity', 'Magnitude'))

# rescale color and/or opacity maps used to include current data range
gradient1Display.RescaleTransferFunctionToDataRange(True, False)

# get color transfer function/color map for 'Vorticity'
vorticityLUT = GetColorTransferFunction('Vorticity')

# get opacity transfer function/opacity map for 'Vorticity'
vorticityPWF = GetOpacityTransferFunction('Vorticity')

# get 2D transfer function for 'Vorticity'
vorticityTF2D = GetTransferFunction2D('Vorticity')

# set scalar coloring
ColorBy(gradient1Display, ('CELLS', 'Vorticity', 'Z'))

# rescale color and/or opacity maps used to exactly fit the current data range
gradient1Display.RescaleTransferFunctionToDataRange(False, False)

# Update a scalar bar component title.
UpdateScalarBarsComponentTitle(vorticityLUT, gradient1Display)

# change representation type
gradient1Display.SetRepresentationType('Surface')

# set active source
SetActiveSource(gradient1)

# show data in view
gradient1Display = Show(gradient1, renderView1, 'AMRRepresentation')

# show color bar/color legend
gradient1Display.SetScalarBarVisibility(renderView1, True)

# Rescale transfer function
vorticityLUT.RescaleTransferFunction(-1.0, 1.0)

# Rescale transfer function
vorticityPWF.RescaleTransferFunction(-1.0, 1.0)

# Rescale 2D transfer function
vorticityTF2D.RescaleTransferFunction(-1.0, 1.0, 0.0, 1.0)

# set active source
SetActiveSource(data_source)

# change representation type
data_sourceDisplay.SetRepresentationType('Feature Edges')

# set active source
SetActiveSource(data_source)

# show data in view
data_sourceDisplay = Show(data_source, renderView1, 'AMRRepresentation')

# set scalar coloring
ColorBy(data_sourceDisplay, ('CELLS', 'vtkAMRLevel'))

# rescale color and/or opacity maps used to include current data range
data_sourceDisplay.RescaleTransferFunctionToDataRange(True, False)

# show color bar/color legend
data_sourceDisplay.SetScalarBarVisibility(renderView1, True)

# get color transfer function/color map for 'vtkAMRLevel'
vtkAMRLevelLUT = GetColorTransferFunction('vtkAMRLevel')

# get opacity transfer function/opacity map for 'vtkAMRLevel'
vtkAMRLevelPWF = GetOpacityTransferFunction('vtkAMRLevel')

# get 2D transfer function for 'vtkAMRLevel'
vtkAMRLevelTF2D = GetTransferFunction2D('vtkAMRLevel')

# set active source
SetActiveSource(calculator2)

# hide data in view
Hide(calculator2, renderView1)

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
renderView1.CameraParallelScale = 0.584385769575659

#--------------------------------------------
# uncomment the following to render all views
# RenderAllViews()
# alternatively, if you want to write images, you can use SaveScreenshot(...).
