# trace generated using paraview version 5.11.2
#import paraview
#paraview.compatibility.major = 5
#paraview.compatibility.minor = 11

#### import the simple module from the paraview
from paraview.simple import *
#### disable automatic camera reset on 'Show'
paraview.simple._DisableFirstRenderCameraReset()

# get active source.
sphere_Re200_200000 = GetActiveSource()

# Properties modified on sphere_Re200_200000
sphere_Re200_200000.Level = 4
sphere_Re200_200000.CellArrayStatus = ['ux', 'uy', 'uz']

# get active view
renderView1 = GetActiveViewOrCreate('RenderView')

# show data in view
sphere_Re200_200000Display = Show(sphere_Re200_200000, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
sphere_Re200_200000Display.Representation = 'Outline'
sphere_Re200_200000Display.ColorArrayName = [None, '']
sphere_Re200_200000Display.SelectTCoordArray = 'None'
sphere_Re200_200000Display.SelectNormalArray = 'None'
sphere_Re200_200000Display.SelectTangentArray = 'None'
sphere_Re200_200000Display.OSPRayScaleFunction = 'PiecewiseFunction'
sphere_Re200_200000Display.SelectOrientationVectors = 'None'
sphere_Re200_200000Display.ScaleFactor = 1.8
sphere_Re200_200000Display.SelectScaleArray = 'None'
sphere_Re200_200000Display.GlyphType = 'Arrow'
sphere_Re200_200000Display.GlyphTableIndexArray = 'None'
sphere_Re200_200000Display.GaussianRadius = 0.09
sphere_Re200_200000Display.SetScaleArray = [None, '']
sphere_Re200_200000Display.ScaleTransferFunction = 'PiecewiseFunction'
sphere_Re200_200000Display.OpacityArray = [None, '']
sphere_Re200_200000Display.OpacityTransferFunction = 'PiecewiseFunction'
sphere_Re200_200000Display.DataAxesGrid = 'GridAxesRepresentation'
sphere_Re200_200000Display.PolarAxes = 'PolarAxesRepresentation'
sphere_Re200_200000Display.ScalarOpacityUnitDistance = 0.058591335575383104

# reset view to fit data
renderView1.ResetCamera(False)

# get the material library
materialLibrary1 = GetMaterialLibrary()

# get animation scene
animationScene1 = GetAnimationScene()

# update animation scene based on data timesteps
animationScene1.UpdateAnimationUsingDataTimeSteps()

# create a new 'Calculator'
calculator1 = Calculator(registrationName='Calculator1', Input=sphere_Re200_200000)
calculator1.AttributeType = 'Cell Data'
calculator1.Function = ''

# Properties modified on calculator1
calculator1.ResultArrayName = 'velocity'
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
calculator1Display.SelectOrientationVectors = 'velocity'
calculator1Display.ScaleFactor = 1.8
calculator1Display.SelectScaleArray = 'None'
calculator1Display.GlyphType = 'Arrow'
calculator1Display.GlyphTableIndexArray = 'None'
calculator1Display.GaussianRadius = 0.09
calculator1Display.SetScaleArray = [None, '']
calculator1Display.ScaleTransferFunction = 'PiecewiseFunction'
calculator1Display.OpacityArray = [None, '']
calculator1Display.OpacityTransferFunction = 'PiecewiseFunction'
calculator1Display.DataAxesGrid = 'GridAxesRepresentation'
calculator1Display.PolarAxes = 'PolarAxesRepresentation'
calculator1Display.ScalarOpacityUnitDistance = 0.058591335575383104

# hide data in view
Hide(sphere_Re200_200000, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# create a new 'Gradient'
gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
gradient1.ScalarArray = ['CELLS', 'ux']

# Properties modified on gradient1
gradient1.ScalarArray = ['CELLS', 'velocity']
gradient1.ComputeGradient = 0
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
gradient1Display.SelectOrientationVectors = 'velocity'
gradient1Display.ScaleFactor = 1.8
gradient1Display.SelectScaleArray = 'None'
gradient1Display.GlyphType = 'Arrow'
gradient1Display.GlyphTableIndexArray = 'None'
gradient1Display.GaussianRadius = 0.09
gradient1Display.SetScaleArray = [None, '']
gradient1Display.ScaleTransferFunction = 'PiecewiseFunction'
gradient1Display.OpacityArray = [None, '']
gradient1Display.OpacityTransferFunction = 'PiecewiseFunction'
gradient1Display.DataAxesGrid = 'GridAxesRepresentation'
gradient1Display.PolarAxes = 'PolarAxesRepresentation'
gradient1Display.ScalarOpacityUnitDistance = 0.058591335575383104

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
calculator2.Function = 'sqrt(Vorticity_X*Vorticity_X+Vorticity_Y*Vorticity_Y+Vorticity_Z*Vorticity_Z)'

# show data in view
calculator2Display = Show(calculator2, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
calculator2Display.Representation = 'Outline'
calculator2Display.ColorArrayName = ['CELLS', '']
calculator2Display.SelectTCoordArray = 'None'
calculator2Display.SelectNormalArray = 'None'
calculator2Display.SelectTangentArray = 'None'
calculator2Display.OSPRayScaleFunction = 'PiecewiseFunction'
calculator2Display.SelectOrientationVectors = 'velocity'
calculator2Display.ScaleFactor = 1.8
calculator2Display.SelectScaleArray = 'vort'
calculator2Display.GlyphType = 'Arrow'
calculator2Display.GlyphTableIndexArray = 'vort'
calculator2Display.GaussianRadius = 0.09
calculator2Display.SetScaleArray = [None, '']
calculator2Display.ScaleTransferFunction = 'PiecewiseFunction'
calculator2Display.OpacityArray = [None, '']
calculator2Display.OpacityTransferFunction = 'PiecewiseFunction'
calculator2Display.DataAxesGrid = 'GridAxesRepresentation'
calculator2Display.PolarAxes = 'PolarAxesRepresentation'
calculator2Display.ScalarOpacityUnitDistance = 0.058591335575383104

# hide data in view
Hide(gradient1, renderView1)

# update the view to ensure updated data information
renderView1.Update()

# create a new 'Cell Data to Point Data'
cellDatatoPointData1 = CellDatatoPointData(registrationName='CellDatatoPointData1', Input=calculator2)
cellDatatoPointData1.CellDataArraytoprocess = ['Q Criterion', 'Vorticity', 'ux', 'uy', 'uz', 'velocity', 'vort', 'vtkGhostType']

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

# hide data in view
Hide(calculator2, renderView1)

# update the view to ensure updated data information
renderView1.Update()

#================================================================
# addendum: following script captures some of the application
# state to faithfully reproduce the visualization during playback
#================================================================

# get layout
layout1 = GetLayout()

#--------------------------------
# saving layout sizes for layouts

# layout/tab size in pixels
layout1.SetSize(1357, 713)

#-----------------------------------
# saving camera placements for views

# current camera placement for renderView1
renderView1.CameraPosition = [9.0, 4.0, 45.07172951095599]
renderView1.CameraFocalPoint = [9.0, 4.0, 4.0]
renderView1.CameraParallelScale = 10.63014581273465

#--------------------------------------------
# uncomment the following to render all views
# RenderAllViews()
# alternatively, if you want to write images, you can use SaveScreenshot(...).