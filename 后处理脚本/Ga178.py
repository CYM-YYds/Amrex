# trace generated using paraview version 5.11.2
#import paraview
#paraview.compatibility.major = 5
#paraview.compatibility.minor = 11

#### import the simple module from the paraview
from paraview.simple import *
import os
#### disable automatic camera reset on 'Show'
folder_path = "/home/huazkjdxmrsgjzdsyshi/whcs-share18/wangyan/amrex-test/test-3D/notdebug8/"
file_prefix = "case4_0"  # 文件名前缀
start_time = 1000  # 开始时间步
end_time = 8500  # 结束时间步
step_size = 500  # 时间步间隔
output_folder = "F:/data/"

file_list = [f"{file_prefix}{i}" for i in range(start_time, end_time + 1, step_size)]

for file_name in file_list:
    full_file_path = os.path.join(folder_path, file_name)
    paraview.simple._DisableFirstRenderCameraReset()

    paraview.simple._DisableFirstRenderCameraReset()

    # create a new 'AMReX/BoxLib Grid Reader'
    case4_04000 = AMReXBoxLibGridReader(registrationName='case4_04000', FileNames=[full_file_path])
    case4_04000.CellArrayStatus = []

    # get animation scene
    animationScene1 = GetAnimationScene()

    # update animation scene based on data timesteps
    animationScene1.UpdateAnimationUsingDataTimeSteps()

    # Properties modified on case4_04000
    case4_04000.Level = 4
    case4_04000.CellArrayStatus = ['ux', 'uy', 'uz']

    # get active view
    renderView1 = GetActiveViewOrCreate('RenderView')

    # show data in view
    case4_04000Display = Show(case4_04000, renderView1, 'AMRRepresentation')

    # trace defaults for the display properties.
    case4_04000Display.Representation = 'Outline'
    case4_04000Display.ColorArrayName = [None, '']
    case4_04000Display.SelectTCoordArray = 'None'
    case4_04000Display.SelectNormalArray = 'None'
    case4_04000Display.SelectTangentArray = 'None'
    case4_04000Display.OSPRayScaleFunction = 'PiecewiseFunction'
    case4_04000Display.SelectOrientationVectors = 'None'
    case4_04000Display.ScaleFactor = 102.4
    case4_04000Display.SelectScaleArray = 'None'
    case4_04000Display.GlyphType = 'Arrow'
    case4_04000Display.GlyphTableIndexArray = 'None'
    case4_04000Display.GaussianRadius = 5.12
    case4_04000Display.SetScaleArray = [None, '']
    case4_04000Display.ScaleTransferFunction = 'PiecewiseFunction'
    case4_04000Display.OpacityArray = [None, '']
    case4_04000Display.OpacityTransferFunction = 'PiecewiseFunction'
    case4_04000Display.DataAxesGrid = 'GridAxesRepresentation'
    case4_04000Display.PolarAxes = 'PolarAxesRepresentation'
    case4_04000Display.ScalarOpacityUnitDistance = 2.7900560636402596

    # reset view to fit data
    renderView1.ResetCamera(False)

    # get the material library
    materialLibrary1 = GetMaterialLibrary()

    # update animation scene based on data timesteps
    animationScene1.UpdateAnimationUsingDataTimeSteps()

    # create a new 'Calculator'
    calculator1 = Calculator(registrationName='Calculator1', Input=case4_04000)
    calculator1.AttributeType = 'Cell Data'
    calculator1.Function = ''

    # Properties modified on calculator1
    calculator1.ResultArrayName = 'Velocity'
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
    calculator1Display.SelectOrientationVectors = 'Velocity'
    calculator1Display.ScaleFactor = 102.4
    calculator1Display.SelectScaleArray = 'None'
    calculator1Display.GlyphType = 'Arrow'
    calculator1Display.GlyphTableIndexArray = 'None'
    calculator1Display.GaussianRadius = 5.12
    calculator1Display.SetScaleArray = [None, '']
    calculator1Display.ScaleTransferFunction = 'PiecewiseFunction'
    calculator1Display.OpacityArray = [None, '']
    calculator1Display.OpacityTransferFunction = 'PiecewiseFunction'
    calculator1Display.DataAxesGrid = 'GridAxesRepresentation'
    calculator1Display.PolarAxes = 'PolarAxesRepresentation'
    calculator1Display.ScalarOpacityUnitDistance = 2.7900560636402596

    # hide data in view
    Hide(case4_04000, renderView1)

    # update the view to ensure updated data information
    renderView1.Update()

    # create a new 'Gradient'
    gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
    gradient1.ScalarArray = ['CELLS', 'Velocity']

    # Properties modified on gradient1
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
    gradient1Display.SelectOrientationVectors = 'Velocity'
    gradient1Display.ScaleFactor = 102.4
    gradient1Display.SelectScaleArray = 'None'
    gradient1Display.GlyphType = 'Arrow'
    gradient1Display.GlyphTableIndexArray = 'None'
    gradient1Display.GaussianRadius = 5.12
    gradient1Display.SetScaleArray = [None, '']
    gradient1Display.ScaleTransferFunction = 'PiecewiseFunction'
    gradient1Display.OpacityArray = [None, '']
    gradient1Display.OpacityTransferFunction = 'PiecewiseFunction'
    gradient1Display.DataAxesGrid = 'GridAxesRepresentation'
    gradient1Display.PolarAxes = 'PolarAxesRepresentation'
    gradient1Display.ScalarOpacityUnitDistance = 2.7900560636402596

    # hide data in view
    Hide(calculator1, renderView1)

    # update the view to ensure updated data information
    renderView1.Update()

    # create a new 'Cell Data to Point Data'
    cellDatatoPointData1 = CellDatatoPointData(registrationName='CellDatatoPointData1', Input=gradient1)
    cellDatatoPointData1.CellDataArraytoprocess = ['Q Criterion', 'Velocity', 'Vorticity', 'ux', 'uy', 'uz', 'vtkGhostType']

    # show data in view
    cellDatatoPointData1Display = Show(cellDatatoPointData1, renderView1, 'AMRRepresentation')

    # trace defaults for the display properties.
    cellDatatoPointData1Display.Representation = 'Outline'
    cellDatatoPointData1Display.ColorArrayName = [None, '']
    cellDatatoPointData1Display.SelectTCoordArray = 'None'
    cellDatatoPointData1Display.SelectNormalArray = 'None'
    cellDatatoPointData1Display.SelectTangentArray = 'None'
    cellDatatoPointData1Display.OSPRayScaleArray = 'Q Criterion'
    cellDatatoPointData1Display.OSPRayScaleFunction = 'PiecewiseFunction'
    cellDatatoPointData1Display.SelectOrientationVectors = 'Velocity'
    cellDatatoPointData1Display.ScaleFactor = 102.4
    cellDatatoPointData1Display.SelectScaleArray = 'None'
    cellDatatoPointData1Display.GlyphType = 'Arrow'
    cellDatatoPointData1Display.GlyphTableIndexArray = 'None'
    cellDatatoPointData1Display.GaussianRadius = 5.12
    cellDatatoPointData1Display.SetScaleArray = ['POINTS', 'Q Criterion']
    cellDatatoPointData1Display.ScaleTransferFunction = 'PiecewiseFunction'
    cellDatatoPointData1Display.OpacityArray = ['POINTS', 'Q Criterion']
    cellDatatoPointData1Display.OpacityTransferFunction = 'PiecewiseFunction'
    cellDatatoPointData1Display.DataAxesGrid = 'GridAxesRepresentation'
    cellDatatoPointData1Display.PolarAxes = 'PolarAxesRepresentation'
    cellDatatoPointData1Display.ScalarOpacityUnitDistance = 2.7900560636402596

    # init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
    cellDatatoPointData1Display.ScaleTransferFunction.Points = [-0.35935178175748056, 0.0, 0.5, 0.0, 0.004453721154762003, 1.0, 0.5, 0.0]

    # init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
    cellDatatoPointData1Display.OpacityTransferFunction.Points = [-0.35935178175748056, 0.0, 0.5, 0.0, 0.004453721154762003, 1.0, 0.5, 0.0]

    # hide data in view
    Hide(gradient1, renderView1)

    # update the view to ensure updated data information
    renderView1.Update()

    # create a new 'Contour'
    contour1 = Contour(registrationName='Contour1', Input=cellDatatoPointData1)
    contour1.ContourBy = ['POINTS', 'Q Criterion']
    contour1.Isosurfaces = [-0.17744903030135928]
    contour1.PointMergeMethod = 'Uniform Binning'

    # Properties modified on contour1
    contour1.Isosurfaces = [5e-08]

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
    contour1Display.SelectOrientationVectors = 'Velocity'
    contour1Display.ScaleFactor = 23.200000000000003
    contour1Display.SelectScaleArray = 'Q Criterion'
    contour1Display.GlyphType = 'Arrow'
    contour1Display.GlyphTableIndexArray = 'Q Criterion'
    contour1Display.GaussianRadius = 1.16
    contour1Display.SetScaleArray = ['POINTS', 'Q Criterion']
    contour1Display.ScaleTransferFunction = 'PiecewiseFunction'
    contour1Display.OpacityArray = ['POINTS', 'Q Criterion']
    contour1Display.OpacityTransferFunction = 'PiecewiseFunction'
    contour1Display.DataAxesGrid = 'GridAxesRepresentation'
    contour1Display.PolarAxes = 'PolarAxesRepresentation'
    contour1Display.SelectInputVectors = ['POINTS', 'Velocity']
    contour1Display.WriteLog = ''

    # init the 'PiecewiseFunction' selected for 'ScaleTransferFunction'
    contour1Display.ScaleTransferFunction.Points = [5e-08, 0.0, 0.5, 0.0, 5.0007276541919055e-08, 1.0, 0.5, 0.0]

    # init the 'PiecewiseFunction' selected for 'OpacityTransferFunction'
    contour1Display.OpacityTransferFunction.Points = [5e-08, 0.0, 0.5, 0.0, 5.0007276541919055e-08, 1.0, 0.5, 0.0]

    # show color bar/color legend
    contour1Display.SetScalarBarVisibility(renderView1, True)

    # update the view to ensure updated data information
    renderView1.Update()

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
    vorticityLUT.RescaleTransferFunction(0.0, 0.01)

    # Rescale transfer function
    vorticityPWF.RescaleTransferFunction(0.0, 0.01)

    # Rescale 2D transfer function
    vorticityTF2D.RescaleTransferFunction(0.0, 0.01, 0.0, 1.0)

    # set active source
    SetActiveSource(case4_04000)

    # create a new 'Slice'
    slice1 = Slice(registrationName='Slice1', Input=case4_04000)
    slice1.SliceType = 'Plane'
    slice1.HyperTreeGridSlicer = 'Plane'
    slice1.SliceOffsetValues = [0.0]

    # init the 'Plane' selected for 'SliceType'
    slice1.SliceType.Origin = [64.0, 64.0, 512.0]
    slice1.SliceType.Normal = [0.0, 0.0, -1.0]

    # init the 'Plane' selected for 'HyperTreeGridSlicer'
    slice1.HyperTreeGridSlicer.Origin = [64.0, 64.0, 512.0]

    # Properties modified on slice1.SliceType
    slice1.SliceType.Normal = [0.0, 1.0, 0.0]

    # show data in view
    slice1Display = Show(slice1, renderView1, 'GeometryRepresentation')

    # trace defaults for the display properties.
    slice1Display.Representation = 'Surface'
    slice1Display.ColorArrayName = [None, '']
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

    # update the view to ensure updated data information
    renderView1.Update()

    # Rescale transfer function
    vorticityLUT.RescaleTransferFunction(0.0, 1.39569934619237)

    # Rescale transfer function
    vorticityPWF.RescaleTransferFunction(0.0, 1.39569934619237)

    # change representation type
    slice1Display.SetRepresentationType('Feature Edges')

    # toggle interactive widget visibility (only when running from the GUI)
    HideInteractiveWidgets(proxy=slice1.SliceType)

    # change solid color
    slice1Display.AmbientColor = [0.0, 0.0, 0.0]
    slice1Display.DiffuseColor = [0.0, 0.0, 0.0]

    # hide data in view
    Hide(cellDatatoPointData1, renderView1)

    # set active source
    SetActiveSource(contour1)

    # Rescale transfer function
    vorticityLUT.RescaleTransferFunction(0.0, 0.01)

    # Rescale transfer function
    vorticityPWF.RescaleTransferFunction(0.0, 0.01)

    # set active source
    SetActiveSource(case4_04000)

    # Properties modified on renderView1
    renderView1.UseColorPaletteForBackground = 0

    # Properties modified on renderView1
    renderView1.Background = [1.0, 1.0, 1.0]

    # set active source
    SetActiveSource(contour1)

    # hide color bar/color legend
    contour1Display.SetScalarBarVisibility(renderView1, False)

    # set active source
    SetActiveSource(slice1)

    # Properties modified on slice1Display
    slice1Display.LineWidth = 2.0

    # Hide orientation axes
    renderView1.OrientationAxesVisibility = 0

    # get layout
    layout1 = GetLayout()

    # layout/tab size in pixels
    layout1.SetSize(1491, 713)

    # current camera placement for renderView1
    renderView1.CameraPosition = [488.2724820254507, -468.3992098674497, 124.16974948218012]
    renderView1.CameraFocalPoint = [39.771349730686595, 98.84851490015295, 401.563961557948]
    renderView1.CameraViewUp = [-0.604733688817156, -0.6804930227588737, 0.4137951323856849]
    renderView1.CameraParallelScale = 519.9384578967015

    # # save screenshot
    # SaveScreenshot('F:/data/1.png', renderView1, ImageResolution=[1491, 713])

    # layout/tab size in pixels
    layout1.SetSize(1491, 713)

    # current camera placement for renderView1
    renderView1.CameraPosition = [386.82668084865094, -451.22964391343226, 298.6453119092971]
    renderView1.CameraFocalPoint = [55.16823924064356, 77.72906403345348, 439.80192146324555]
    renderView1.CameraViewUp = [-0.6916660315550373, -0.5565147334112078, 0.460314514532356]
    renderView1.CameraParallelScale = 519.9384578967015

    # save screenshot
    result_file_name = os.path.join(output_folder, f"{file_name}_result.png")    
    SaveScreenshot(result_file_name, renderView1, ImageResolution=[1491, 713])
    ResetSession()
    #================================================================
    # addendum: following script captures some of the application
    # state to faithfully reproduce the visualization during playback
    #================================================================

    #--------------------------------
    # saving layout sizes for layouts

    # layout/tab size in pixels
    layout1.SetSize(1491, 713)

    #-----------------------------------
    # saving camera placements for views

    # current camera placement for renderView1
    renderView1.CameraPosition = [386.82668084865094, -451.22964391343226, 298.6453119092971]
    renderView1.CameraFocalPoint = [55.16823924064356, 77.72906403345348, 439.80192146324555]
    renderView1.CameraViewUp = [-0.6916660315550373, -0.5565147334112078, 0.460314514532356]
    renderView1.CameraParallelScale = 519.9384578967015

    #--------------------------------------------
    # uncomment the following to render all views
    # RenderAllViews()
    # alternatively, if you want to write images, you can use SaveScreenshot(...).