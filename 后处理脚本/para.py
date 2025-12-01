# trace generated using paraview version 5.11.2
#import paraview
#paraview.compatibility.major = 5
#paraview.compatibility.minor = 11

#### import the simple module from the paraview
from paraview.simple import *
import os
#### disable automatic camera reset on 'Show'
# 设置文件夹路径和文件名前缀
folder_path = "F:\\data\\cyl_lev3_Re200_amr"
file_prefix = "cyl_lev2_Re200_"  # 文件名前缀
start_time = 10000  # 开始时间步
end_time = 500000  # 结束时间步
step_size = 10000  # 时间步间隔
output_folder = "F:/data/"

file_list = [f"{file_prefix}{i}" for i in range(start_time, end_time + 1, step_size)]

for file_name in file_list:
    full_file_path = os.path.join(folder_path, file_name)
    paraview.simple._DisableFirstRenderCameraReset()

# create a new 'AMReX/BoxLib Grid Reader'
    cyl_lev2_Re200_260000 = AMReXBoxLibGridReader(FileNames=[full_file_path])
    cyl_lev2_Re200_260000.CellArrayStatus = []

# get animation scene
    animationScene1 = GetAnimationScene()

# update animation scene based on data timesteps
    animationScene1.UpdateAnimationUsingDataTimeSteps()

# Properties modified on cyl_lev2_Re200_260000
    cyl_lev2_Re200_260000.Level = 3
    cyl_lev2_Re200_260000.CellArrayStatus = ['ux', 'uy']

# get active view
    renderView1 = GetActiveViewOrCreate('RenderView')

# show data in view
    cyl_lev2_Re200_260000Display = Show(cyl_lev2_Re200_260000, renderView1, 'AMRRepresentation')

# trace defaults for the display properties.
    cyl_lev2_Re200_260000Display.Representation = 'Outline'
    cyl_lev2_Re200_260000Display.ColorArrayName = [None, '']
    cyl_lev2_Re200_260000Display.SelectTCoordArray = 'None'
    cyl_lev2_Re200_260000Display.SelectNormalArray = 'None'
    cyl_lev2_Re200_260000Display.SelectTangentArray = 'None'
    cyl_lev2_Re200_260000Display.OSPRayScaleFunction = 'PiecewiseFunction'
    cyl_lev2_Re200_260000Display.SelectOrientationVectors = 'None'
    cyl_lev2_Re200_260000Display.ScaleFactor = 0.2
    cyl_lev2_Re200_260000Display.SelectScaleArray = 'None'
    cyl_lev2_Re200_260000Display.GlyphType = 'Arrow'
    cyl_lev2_Re200_260000Display.GlyphTableIndexArray = 'None'
    cyl_lev2_Re200_260000Display.GaussianRadius = 0.01
    cyl_lev2_Re200_260000Display.SetScaleArray = [None, '']
    cyl_lev2_Re200_260000Display.ScaleTransferFunction = 'PiecewiseFunction'
    cyl_lev2_Re200_260000Display.OpacityArray = [None, '']
    cyl_lev2_Re200_260000Display.OpacityTransferFunction = 'PiecewiseFunction'
    cyl_lev2_Re200_260000Display.DataAxesGrid = 'GridAxesRepresentation'
    cyl_lev2_Re200_260000Display.PolarAxes = 'PolarAxesRepresentation'
    cyl_lev2_Re200_260000Display.ScalarOpacityUnitDistance = 0.010868691608213958

# reset view to fit data
    renderView1.ResetCamera(False)

#changing interaction mode based on data extents
    renderView1.InteractionMode = '2D'
    renderView1.CameraPosition = [1.0, 0.5, 6.7]
    renderView1.CameraFocalPoint = [1.0, 0.5, 0.0]

# get the material library
    materialLibrary1 = GetMaterialLibrary()

# update animation scene based on data timesteps
    animationScene1.UpdateAnimationUsingDataTimeSteps()

# create a new 'Calculator'
    calculator1 = Calculator(registrationName='Calculator1', Input=cyl_lev2_Re200_260000)
    calculator1.AttributeType = 'Cell Data'
    calculator1.Function = ''

# Properties modified on calculator1
    calculator1.ResultArrayName = 'velocity'
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
    calculator1Display.SelectOrientationVectors = 'velocity'
    calculator1Display.ScaleFactor = 0.2
    calculator1Display.SelectScaleArray = 'None'
    calculator1Display.GlyphType = 'Arrow'
    calculator1Display.GlyphTableIndexArray = 'None'
    calculator1Display.GaussianRadius = 0.01
    calculator1Display.SetScaleArray = [None, '']
    calculator1Display.ScaleTransferFunction = 'PiecewiseFunction'
    calculator1Display.OpacityArray = [None, '']
    calculator1Display.OpacityTransferFunction = 'PiecewiseFunction'
    calculator1Display.DataAxesGrid = 'GridAxesRepresentation'
    calculator1Display.PolarAxes = 'PolarAxesRepresentation'
    calculator1Display.ScalarOpacityUnitDistance = 0.010868691608213958

    # hide data in view
    Hide(cyl_lev2_Re200_260000, renderView1)

    # update the view to ensure updated data information
    renderView1.Update()

    # create a new 'Gradient'
    gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
    gradient1.ScalarArray = ['CELLS', 'ux']

    # Properties modified on gradient1
    gradient1.ScalarArray = ['CELLS', 'velocity']
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
    gradient1Display.SelectOrientationVectors = 'velocity'
    gradient1Display.ScaleFactor = 0.2
    gradient1Display.SelectScaleArray = 'None'
    gradient1Display.GlyphType = 'Arrow'
    gradient1Display.GlyphTableIndexArray = 'None'
    gradient1Display.GaussianRadius = 0.01
    gradient1Display.SetScaleArray = [None, '']
    gradient1Display.ScaleTransferFunction = 'PiecewiseFunction'
    gradient1Display.OpacityArray = [None, '']
    gradient1Display.OpacityTransferFunction = 'PiecewiseFunction'
    gradient1Display.DataAxesGrid = 'GridAxesRepresentation'
    gradient1Display.PolarAxes = 'PolarAxesRepresentation'
    gradient1Display.ScalarOpacityUnitDistance = 0.010868691608213958

    # hide data in view
    Hide(calculator1, renderView1)

    # update the view to ensure updated data information
    renderView1.Update()

    # set scalar coloring
    ColorBy(gradient1Display, ('CELLS', 'Vorticity', 'Magnitude'))

    # rescale color and/or opacity maps used to include current data range
    gradient1Display.RescaleTransferFunctionToDataRange(True, False)

    # show color bar/color legend
    gradient1Display.SetScalarBarVisibility(renderView1, True)

    # get color transfer function/color map for 'Vorticity'
    vorticityLUT = GetColorTransferFunction('Vorticity')

    # get opacity transfer function/opacity map for 'Vorticity'
    vorticityPWF = GetOpacityTransferFunction('Vorticity')

    # get 2D transfer function for 'Vorticity'
    vorticityTF2D = GetTransferFunction2D('Vorticity')

    # change representation type
    gradient1Display.SetRepresentationType('Surface')

    # set scalar coloring
    ColorBy(gradient1Display, ('CELLS', 'Vorticity', 'Z'))

    # rescale color and/or opacity maps used to exactly fit the current data range
    gradient1Display.RescaleTransferFunctionToDataRange(False, False)

    # Update a scalar bar component title.
    UpdateScalarBarsComponentTitle(vorticityLUT, gradient1Display)

    # Rescale transfer function
    vorticityLUT.RescaleTransferFunction(-1.0, 1.0)

    # Rescale transfer function
    vorticityPWF.RescaleTransferFunction(-1.0, 1.0)

    # Rescale 2D transfer function
    vorticityTF2D.RescaleTransferFunction(-1.0, 1.0, 0.0, 1.0)

    # Apply a preset using its name. Note this may not work as expected when presets have duplicate names.
    vorticityLUT.ApplyPreset('Cold and Hot', True)

    # get color legend/bar for vorticityLUT in view renderView1
    vorticityLUTColorBar = GetScalarBar(vorticityLUT, renderView1)

    # change scalar bar placement
    vorticityLUTColorBar.WindowLocation = 'Any Location'
    vorticityLUTColorBar.Position = [0.8324175824175823, 0.3243606998654105]
    vorticityLUTColorBar.ScalarBarLength = 0.32999999999999996

    # set active source
    SetActiveSource(cyl_lev2_Re200_260000)

    # set active source
    SetActiveSource(cyl_lev2_Re200_260000)

    # show data in view
    cyl_lev2_Re200_260000Display = Show(cyl_lev2_Re200_260000, renderView1, 'AMRRepresentation')

    # change representation type
    cyl_lev2_Re200_260000Display.SetRepresentationType('Feature Edges')

    # set scalar coloring
    ColorBy(cyl_lev2_Re200_260000Display, ('CELLS', 'vtkAMRLevel'))

    # rescale color and/or opacity maps used to include current data range
    cyl_lev2_Re200_260000Display.RescaleTransferFunctionToDataRange(True, False)

    # show color bar/color legend
    cyl_lev2_Re200_260000Display.SetScalarBarVisibility(renderView1, True)

    # get color transfer function/color map for 'vtkAMRLevel'
    vtkAMRLevelLUT = GetColorTransferFunction('vtkAMRLevel')

    # get opacity transfer function/opacity map for 'vtkAMRLevel'
    vtkAMRLevelPWF = GetOpacityTransferFunction('vtkAMRLevel')

    # get 2D transfer function for 'vtkAMRLevel'
    vtkAMRLevelTF2D = GetTransferFunction2D('vtkAMRLevel')

    # Apply a preset using its name. Note this may not work as expected when presets have duplicate names.
    vtkAMRLevelLUT.ApplyPreset('Black-Body Radiation', True)

    # Apply a preset using its name. Note this may not work as expected when presets have duplicate names.
    vtkAMRLevelLUT.ApplyPreset('Cold and Hot', True)

    # hide color bar/color legend
    cyl_lev2_Re200_260000Display.SetScalarBarVisibility(renderView1, False)

    # set active source
    SetActiveSource(gradient1)

    # hide color bar/color legend
    gradient1Display.SetScalarBarVisibility(renderView1, False)

    # get layout
    layout1 = GetLayout()

    # layout/tab size in pixels
    layout1.SetSize(1456, 743)

    # current camera placement for renderView1
    renderView1.InteractionMode = '2D'
    renderView1.CameraPosition = [1.0429310055545522, 0.4779543484990136, 6.7]
    renderView1.CameraFocalPoint = [1.0429310055545522, 0.4779543484990136, 0.0]
    renderView1.CameraParallelScale = 0.43105050171665416

    # save screenshot
    result_file_name = os.path.join(output_folder, f"{file_name}_result.png")    
    SaveScreenshot(result_file_name, renderView1, ImageResolution=[1456, 743])
    ResetSession()
#================================================================
# addendum: following script captures some of the application
# state to faithfully reproduce the visualization during playback
#================================================================

    #--------------------------------
    # saving layout sizes for layouts

    # layout/tab size in pixels
    layout1.SetSize(1456, 743)

    #-----------------------------------
    # saving camera placements for views

    # current camera placement for renderView1
    renderView1.InteractionMode = '2D'
    renderView1.CameraPosition = [1.0429310055545522, 0.4779543484990136, 6.7]
    renderView1.CameraFocalPoint = [1.0429310055545522, 0.4779543484990136, 0.0]
    renderView1.CameraParallelScale = 0.43105050171665416

#--------------------------------------------
# uncomment the following to render all views
# RenderAllViews()
# alternatively, if you want to write images, you can use SaveScreenshot(...).