# trace generated using paraview version 5.11.2
#import paraview
#paraview.compatibility.major = 5
#paraview.compatibility.minor = 11

#### import the simple module from the paraview
from paraview.simple import *
#### disable automatic camera reset on 'Show'
paraview.simple._DisableFirstRenderCameraReset()

# create a new 'AMReX/BoxLib Grid Reader'
car_Re300w_010000 = AMReXBoxLibGridReader(registrationName='car_Re300w_010000', FileNames=['/home/huazkjdxmrsgjzdsyshi/whcs-share18/wangyan/amrex-test/test-3D/cumu_object/test03/car_Re300w_010000'])
car_Re300w_010000.CellArrayStatus = []

# get animation scene
animationScene1 = GetAnimationScene()

# update animation scene based on data timesteps
animationScene1.UpdateAnimationUsingDataTimeSteps()

# get active view
renderView1 = GetActiveViewOrCreate('RenderView')

# show data in view
car_Re300w_010000Display = Show(car_Re300w_010000, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
car_Re300w_010000Display.Representation = 'Outline'
car_Re300w_010000Display.ColorArrayName = [None, '']
car_Re300w_010000Display.SelectTCoordArray = 'None'
car_Re300w_010000Display.SelectNormalArray = 'None'
car_Re300w_010000Display.SelectTangentArray = 'None'
car_Re300w_010000Display.OSPRayScaleFunction = 'PiecewiseFunction'
car_Re300w_010000Display.SelectOrientationVectors = 'None'
car_Re300w_010000Display.ScaleFactor = 86.4
car_Re300w_010000Display.SelectScaleArray = 'None'
car_Re300w_010000Display.GlyphType = 'Arrow'
car_Re300w_010000Display.GlyphTableIndexArray = 'None'
car_Re300w_010000Display.GaussianRadius = 4.32
car_Re300w_010000Display.SetScaleArray = [None, '']
car_Re300w_010000Display.ScaleTransferFunction = 'PiecewiseFunction'
car_Re300w_010000Display.OpacityArray = [None, '']
car_Re300w_010000Display.OpacityTransferFunction = 'PiecewiseFunction'
car_Re300w_010000Display.DataAxesGrid = 'GridAxesRepresentation'
car_Re300w_010000Display.PolarAxes = 'PolarAxesRepresentation'
car_Re300w_010000Display.ScalarOpacityUnitDistance = 1.4861385799036926

# reset view to fit data
renderView1.ResetCamera(False)

# update animation scene based on data timesteps
animationScene1.UpdateAnimationUsingDataTimeSteps()

# create a new 'Calculator'
calculator1 = Calculator(registrationName='Calculator1', Input=car_Re300w_010000)
calculator1.AttributeType = 'Cell Data'
calculator1.Function = ''

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
calculator1Display.ScaleFactor = 86.4
calculator1Display.SelectScaleArray = 'None'
calculator1Display.GlyphType = 'Arrow'
calculator1Display.GlyphTableIndexArray = 'None'
calculator1Display.GaussianRadius = 4.32
calculator1Display.SetScaleArray = [None, '']
calculator1Display.ScaleTransferFunction = 'PiecewiseFunction'
calculator1Display.OpacityArray = [None, '']
calculator1Display.OpacityTransferFunction = 'PiecewiseFunction'
calculator1Display.DataAxesGrid = 'GridAxesRepresentation'
calculator1Display.PolarAxes = 'PolarAxesRepresentation'
calculator1Display.ScalarOpacityUnitDistance = 1.4861385799036926

# hide data in view
Hide(car_Re300w_010000, renderView1)

# create a new 'Gradient'
gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
gradient1.ScalarArray = ['CELLS', 'Vel']

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
gradient1Display.ScaleFactor = 86.4
gradient1Display.SelectScaleArray = 'None'
gradient1Display.GlyphType = 'Arrow'
gradient1Display.GlyphTableIndexArray = 'None'
gradient1Display.GaussianRadius = 4.32
gradient1Display.SetScaleArray = [None, '']
gradient1Display.ScaleTransferFunction = 'PiecewiseFunction'
gradient1Display.OpacityArray = [None, '']
gradient1Display.OpacityTransferFunction = 'PiecewiseFunction'
gradient1Display.DataAxesGrid = 'GridAxesRepresentation'
gradient1Display.PolarAxes = 'PolarAxesRepresentation'
gradient1Display.ScalarOpacityUnitDistance = 1.4861385799036926

# hide data in view
Hide(calculator1, renderView1)

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
cellDatatoPointData1Display.ScaleFactor = 86.4
cellDatatoPointData1Display.SelectScaleArray = 'None'
cellDatatoPointData1Display.GlyphType = 'Arrow'
cellDatatoPointData1Display.GlyphTableIndexArray = 'Gradient'
cellDatatoPointData1Display.GaussianRadius = 4.32
cellDatatoPointData1Display.SetScaleArray = ['POINTS', 'Gradient']
cellDatatoPointData1Display.ScaleTransferFunction = 'PiecewiseFunction'
cellDatatoPointData1Display.OpacityArray = ['POINTS', 'Gradient']
cellDatatoPointData1Display.OpacityTransferFunction = 'PiecewiseFunction'
cellDatatoPointData1Display.DataAxesGrid = 'GridAxesRepresentation'
cellDatatoPointData1Display.PolarAxes = 'PolarAxesRepresentation'
cellDatatoPointData1Display.ScalarOpacityUnitDistance = 1.4861385799036926

# init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
cellDatatoPointData1Display.ScaleTransferFunction.Points = [-0.4627602196552848, 0.0, 0.5, 0.0, 0.35938563063306783, 1.0, 0.5, 0.0]

# init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
cellDatatoPointData1Display.OpacityTransferFunction.Points = [-0.4627602196552848, 0.0, 0.5, 0.0, 0.35938563063306783, 1.0, 0.5, 0.0]

# hide data in view
Hide(gradient1, renderView1)

# create a new 'Contour'
contour1 = Contour(registrationName='Contour1', Input=cellDatatoPointData1)
contour1.ContourBy = ['POINTS', 'Q Criterion']
contour1.Isosurfaces = [0.07639123807455608]
contour1.PointMergeMethod = 'Uniform Binning'

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
contour1Display.ScaleFactor = 13.458163452148439
contour1Display.SelectScaleArray = 'Q Criterion'
contour1Display.GlyphType = 'Arrow'
contour1Display.GlyphTableIndexArray = 'Q Criterion'
contour1Display.GaussianRadius = 0.6729081726074219
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

# set active source
SetActiveSource(cellDatatoPointData1)

# set active source
SetActiveSource(contour1)

# set scalar coloring
ColorBy(contour1Display, ('POINTS', 'Vel', 'Magnitude'))

# rescale color and/or opacity maps used to exactly fit the current data range
contour1Display.RescaleTransferFunctionToDataRange(False, True)

# Hide the scalar bar for this color map if no visible data is colored by it.
HideScalarBarIfNotNeeded(qCriterionLUT, renderView1)

# rescale color and/or opacity maps used to include current data range
contour1Display.RescaleTransferFunctionToDataRange(True, False)

# show color bar/color legend
contour1Display.SetScalarBarVisibility(renderView1, True)

# get color transfer function/color map for 'Vel'
velLUT = GetColorTransferFunction('Vel')

# Rescale transfer function
velLUT.RescaleTransferFunction(1.689427725706336e-05, 0.01)

# get opacity transfer function/opacity map for 'Vel'
velPWF = GetOpacityTransferFunction('Vel')

# Rescale transfer function
velPWF.RescaleTransferFunction(1.689427725706336e-05, 0.01)

# get 2D transfer function for 'Vel'
velTF2D = GetTransferFunction2D('Vel')

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

# reset view to fit data bounds
renderView1.ResetCamera(109.11973571777344, 147.06016540527344, 225.41836547851562, 360.0, 0.02872588112950325, 21.816560745239258, False)

# create a new 'STL Reader'
teslamodelSCar_mesh_finestl = STLReader(registrationName='Tesla-model-S-Car_mesh_fine.stl', FileNames=['/home/huazkjdxmrsgjzdsyshi/whcs-share18/wangyan/amrex-test/object/Tesla-model-S-Car_mesh_fine.stl'])

# show data in view
teslamodelSCar_mesh_finestlDisplay = Show(teslamodelSCar_mesh_finestl, renderView1, 'GeometryRepresentation')

# get color transfer function/color map for 'STLSolidLabeling'
sTLSolidLabelingLUT = GetColorTransferFunction('STLSolidLabeling')

# trace defaults for the display properties.
teslamodelSCar_mesh_finestlDisplay.Representation = 'Surface'
teslamodelSCar_mesh_finestlDisplay.ColorArrayName = ['CELLS', 'STLSolidLabeling']
teslamodelSCar_mesh_finestlDisplay.LookupTable = sTLSolidLabelingLUT
teslamodelSCar_mesh_finestlDisplay.SelectTCoordArray = 'None'
teslamodelSCar_mesh_finestlDisplay.SelectNormalArray = 'None'
teslamodelSCar_mesh_finestlDisplay.SelectTangentArray = 'None'
teslamodelSCar_mesh_finestlDisplay.OSPRayScaleFunction = 'PiecewiseFunction'
teslamodelSCar_mesh_finestlDisplay.SelectOrientationVectors = 'None'
teslamodelSCar_mesh_finestlDisplay.ScaleFactor = 496.91597976684574
teslamodelSCar_mesh_finestlDisplay.SelectScaleArray = 'STLSolidLabeling'
teslamodelSCar_mesh_finestlDisplay.GlyphType = 'Arrow'
teslamodelSCar_mesh_finestlDisplay.GlyphTableIndexArray = 'STLSolidLabeling'
teslamodelSCar_mesh_finestlDisplay.GaussianRadius = 24.845798988342285
teslamodelSCar_mesh_finestlDisplay.SetScaleArray = [None, '']
teslamodelSCar_mesh_finestlDisplay.ScaleTransferFunction = 'PiecewiseFunction'
teslamodelSCar_mesh_finestlDisplay.OpacityArray = [None, '']
teslamodelSCar_mesh_finestlDisplay.OpacityTransferFunction = 'PiecewiseFunction'
teslamodelSCar_mesh_finestlDisplay.DataAxesGrid = 'GridAxesRepresentation'
teslamodelSCar_mesh_finestlDisplay.PolarAxes = 'PolarAxesRepresentation'
teslamodelSCar_mesh_finestlDisplay.SelectInputVectors = [None, '']
teslamodelSCar_mesh_finestlDisplay.WriteLog = ''

# show color bar/color legend
teslamodelSCar_mesh_finestlDisplay.SetScalarBarVisibility(renderView1, True)

# show data in view
car_Re300w_010000Display = Show(car_Re300w_010000, renderView1, 'AMRRepresentation')

# update the view to ensure updated data information
renderView1.Update()

# Rescale transfer function
velLUT.RescaleTransferFunction(0.0, 0.1723443002349146)

# Rescale transfer function
velPWF.RescaleTransferFunction(0.0, 0.1723443002349146)

# get opacity transfer function/opacity map for 'STLSolidLabeling'
sTLSolidLabelingPWF = GetOpacityTransferFunction('STLSolidLabeling')

# get 2D transfer function for 'STLSolidLabeling'
sTLSolidLabelingTF2D = GetTransferFunction2D('STLSolidLabeling')

# Properties modified on teslamodelSCar_mesh_finestlDisplay
teslamodelSCar_mesh_finestlDisplay.Scale = [0.013, 1.0, 1.0]

# Properties modified on teslamodelSCar_mesh_finestlDisplay.DataAxesGrid
teslamodelSCar_mesh_finestlDisplay.DataAxesGrid.Scale = [0.013, 1.0, 1.0]

# Properties modified on teslamodelSCar_mesh_finestlDisplay.PolarAxes
teslamodelSCar_mesh_finestlDisplay.PolarAxes.Scale = [0.013, 1.0, 1.0]

# Properties modified on teslamodelSCar_mesh_finestlDisplay
teslamodelSCar_mesh_finestlDisplay.Scale = [0.013, 0.013, 1.0]

# Properties modified on teslamodelSCar_mesh_finestlDisplay.DataAxesGrid
teslamodelSCar_mesh_finestlDisplay.DataAxesGrid.Scale = [0.013, 0.013, 1.0]

# Properties modified on teslamodelSCar_mesh_finestlDisplay.PolarAxes
teslamodelSCar_mesh_finestlDisplay.PolarAxes.Scale = [0.013, 0.013, 1.0]

# Properties modified on teslamodelSCar_mesh_finestlDisplay
teslamodelSCar_mesh_finestlDisplay.Scale = [0.013, 0.013, 0.013]

# Properties modified on teslamodelSCar_mesh_finestlDisplay.DataAxesGrid
teslamodelSCar_mesh_finestlDisplay.DataAxesGrid.Scale = [0.013, 0.013, 0.013]

# Properties modified on teslamodelSCar_mesh_finestlDisplay.PolarAxes
teslamodelSCar_mesh_finestlDisplay.PolarAxes.Scale = [0.013, 0.013, 0.013]

# Properties modified on teslamodelSCar_mesh_finestlDisplay
teslamodelSCar_mesh_finestlDisplay.Origin = [130.0, 0.0, 0.0]

# Properties modified on teslamodelSCar_mesh_finestlDisplay
teslamodelSCar_mesh_finestlDisplay.Origin = [130.0, 293.5, 0.0]

# Properties modified on teslamodelSCar_mesh_finestlDisplay
teslamodelSCar_mesh_finestlDisplay.Origin = [130.0, 293.5, 2.0]

# turn off scalar coloring
ColorBy(teslamodelSCar_mesh_finestlDisplay, None)

# Hide the scalar bar for this color map if no visible data is colored by it.
HideScalarBarIfNotNeeded(sTLSolidLabelingLUT, renderView1)

# Properties modified on renderView1
renderView1.UseColorPaletteForBackground = 0

# get the material library
materialLibrary1 = GetMaterialLibrary()

# Properties modified on renderView1
renderView1.Background = [0.0, 0.0, 0.0]

# change solid color
teslamodelSCar_mesh_finestlDisplay.AmbientColor = [0.0, 1.0, 1.0]
teslamodelSCar_mesh_finestlDisplay.DiffuseColor = [0.0, 1.0, 1.0]

# set active source
SetActiveSource(contour1)

# set scalar coloring
ColorBy(contour1Display, ('POINTS', 'Q Criterion'))

# Hide the scalar bar for this color map if no visible data is colored by it.
HideScalarBarIfNotNeeded(velLUT, renderView1)

# rescale color and/or opacity maps used to include current data range
contour1Display.RescaleTransferFunctionToDataRange(True, False)

# show color bar/color legend
contour1Display.SetScalarBarVisibility(renderView1, True)

# get opacity transfer function/opacity map for 'QCriterion'
qCriterionPWF = GetOpacityTransferFunction('QCriterion')

# get 2D transfer function for 'QCriterion'
qCriterionTF2D = GetTransferFunction2D('QCriterion')

# set scalar coloring
ColorBy(contour1Display, ('POINTS', 'Vorticity', 'Magnitude'))

# Hide the scalar bar for this color map if no visible data is colored by it.
HideScalarBarIfNotNeeded(qCriterionLUT, renderView1)

# rescale color and/or opacity maps used to include current data range
contour1Display.RescaleTransferFunctionToDataRange(True, False)

# show color bar/color legend
contour1Display.SetScalarBarVisibility(renderView1, True)

# get color transfer function/color map for 'Vorticity'
vorticityLUT = GetColorTransferFunction('Vorticity')

# get opacity transfer function/opacity map for 'Vorticity'
vorticityPWF = GetOpacityTransferFunction('Vorticity')

# get 2D transfer function for 'Vorticity'
vorticityTF2D = GetTransferFunction2D('Vorticity')

# Rescale transfer function
vorticityLUT.RescaleTransferFunction(0.05, 0.13)

# Rescale transfer function
vorticityPWF.RescaleTransferFunction(0.05, 0.13)

# Rescale 2D transfer function
vorticityTF2D.RescaleTransferFunction(0.05, 0.13, 0.0, 1.0)

# hide color bar/color legend
contour1Display.SetScalarBarVisibility(renderView1, False)

# show color bar/color legend
contour1Display.SetScalarBarVisibility(renderView1, True)

# get color legend/bar for vorticityLUT in view renderView1
vorticityLUTColorBar = GetScalarBar(vorticityLUT, renderView1)

# change scalar bar placement
vorticityLUTColorBar.WindowLocation = 'Any Location'
vorticityLUTColorBar.Position = [0.7957181088314006, 0.10799438990182328]
vorticityLUTColorBar.ScalarBarLength = 0.32999999999999996

# change scalar bar placement
vorticityLUTColorBar.Position = [0.7885816235504015, 0.08976157082748945]

# get layout
layout1 = GetLayout()

# layout/tab size in pixels
layout1.SetSize(1121, 713)

# current camera placement for renderView1
renderView1.CameraPosition = [216.5193978899488, 172.87285266109237, 55.90205311873936]
renderView1.CameraFocalPoint = [121.24810524227753, 279.5332760502632, -2.0743135732058153]
renderView1.CameraViewUp = [-0.2172493668004229, 0.30920304306956103, 0.9258489027813008]
renderView1.CameraParallelScale = 70.75733584479464

# save screenshot
SaveScreenshot('F:/data/t=010000.png', renderView1, ImageResolution=[1121, 713])

ResetSession()

# get animation scene
animationScene1_1 = GetAnimationScene()

# get the time-keeper
timeKeeper1 = GetTimeKeeper()

# get active view
renderView1_1 = GetActiveViewOrCreate('RenderView')

#================================================================
# addendum: following script captures some of the application
# state to faithfully reproduce the visualization during playback
#================================================================

# get layout
layout1_1 = GetLayout()

#--------------------------------
# saving layout sizes for layouts

# layout/tab size in pixels
layout1_1.SetSize(1121, 713)

#--------------------------------------------
# uncomment the following to render all views
# RenderAllViews()
# alternatively, if you want to write images, you can use SaveScreenshot(...).