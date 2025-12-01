# trace generated using paraview version 5.11.2
#import paraview
#paraview.compatibility.major = 5
#paraview.compatibility.minor = 11

#### import the simple module from the paraview
from paraview.simple import *
#### disable automatic camera reset on 'Show'
paraview.simple._DisableFirstRenderCameraReset()

for step in range(200, 10001, 200):
    step_str = f"{step:05d}"

    # create a new 'AMReX/BoxLib Grid Reader'
    multi_lev3_particle_06000 = AMReXBoxLibGridReader(registrationName=f'multi_lev3_particle_{step_str}', FileNames=[f'F:\\data\\multi_lev3_particle_{step_str}'])
    multi_lev3_particle_06000.CellArrayStatus = []

    # get animation scene
    animationScene1 = GetAnimationScene()

    # update animation scene based on data timesteps
    animationScene1.UpdateAnimationUsingDataTimeSteps()

    # Properties modified on multi_lev3_particle_06000
    multi_lev3_particle_06000.Level = 3

    # get active view
    renderView1 = GetActiveViewOrCreate('RenderView')

    # show data in view
    multi_lev3_particle_06000Display = Show(multi_lev3_particle_06000, renderView1, 'AMRRepresentation')

    # trace defaults for the display properties.
    multi_lev3_particle_06000Display.Representation = 'Outline'
    multi_lev3_particle_06000Display.ColorArrayName = [None, '']
    multi_lev3_particle_06000Display.SelectTCoordArray = 'None'
    multi_lev3_particle_06000Display.SelectNormalArray = 'None'
    multi_lev3_particle_06000Display.SelectTangentArray = 'None'
    multi_lev3_particle_06000Display.OSPRayScaleFunction = 'PiecewiseFunction'
    multi_lev3_particle_06000Display.SelectOrientationVectors = 'None'
    multi_lev3_particle_06000Display.ScaleFactor = 19.200000000000003
    multi_lev3_particle_06000Display.SelectScaleArray = 'None'
    multi_lev3_particle_06000Display.GlyphType = 'Arrow'
    multi_lev3_particle_06000Display.GlyphTableIndexArray = 'None'
    multi_lev3_particle_06000Display.GaussianRadius = 0.96
    multi_lev3_particle_06000Display.SetScaleArray = [None, '']
    multi_lev3_particle_06000Display.ScaleTransferFunction = 'PiecewiseFunction'
    multi_lev3_particle_06000Display.OpacityArray = [None, '']
    multi_lev3_particle_06000Display.OpacityTransferFunction = 'PiecewiseFunction'
    multi_lev3_particle_06000Display.DataAxesGrid = 'GridAxesRepresentation'
    multi_lev3_particle_06000Display.PolarAxes = 'PolarAxesRepresentation'
    multi_lev3_particle_06000Display.ScalarOpacityUnitDistance = 1.1859999854139154

    # reset view to fit data
    renderView1.ResetCamera(False)

    # get the material library
    materialLibrary1 = GetMaterialLibrary()

    # update animation scene based on data timesteps
    animationScene1.UpdateAnimationUsingDataTimeSteps()

    # change representation type
    multi_lev3_particle_06000Display.SetRepresentationType('AMR Blocks')

    renderView1.ResetActiveCameraToPositiveY()

    # reset view to fit data
    renderView1.ResetCamera(False)

    # change solid color
    multi_lev3_particle_06000Display.AmbientColor = [0.6666666666666666, 1.0, 1.0]
    multi_lev3_particle_06000Display.DiffuseColor = [0.6666666666666666, 1.0, 1.0]

    # change solid color
    multi_lev3_particle_06000Display.AmbientColor = [0.0, 0.0, 0.0]
    multi_lev3_particle_06000Display.DiffuseColor = [0.0, 0.0, 0.0]

    # create a new 'Tecplot Reader'
    particle_data06000dat = TecplotReader(registrationName=f'particle_data{step_str}.dat', FileNames=[f'F:\\data\\particle_data{step_str}.dat'])
    particle_data06000dat.DataArrayStatus = ['ID']

    # show data in view
    particle_data06000datDisplay = Show(particle_data06000dat, renderView1, 'StructuredGridRepresentation')

    # trace defaults for the display properties.
    particle_data06000datDisplay.Representation = 'Surface'
    particle_data06000datDisplay.ColorArrayName = [None, '']
    particle_data06000datDisplay.SelectTCoordArray = 'None'
    particle_data06000datDisplay.SelectNormalArray = 'None'
    particle_data06000datDisplay.SelectTangentArray = 'None'
    particle_data06000datDisplay.OSPRayScaleArray = 'ID'
    particle_data06000datDisplay.OSPRayScaleFunction = 'PiecewiseFunction'
    particle_data06000datDisplay.SelectOrientationVectors = 'None'
    particle_data06000datDisplay.ScaleFactor = 2.6388734817504886
    particle_data06000datDisplay.SelectScaleArray = 'ID'
    particle_data06000datDisplay.GlyphType = 'Arrow'
    particle_data06000datDisplay.GlyphTableIndexArray = 'ID'
    particle_data06000datDisplay.GaussianRadius = 0.13194367408752442
    particle_data06000datDisplay.SetScaleArray = ['POINTS', 'ID']
    particle_data06000datDisplay.ScaleTransferFunction = 'PiecewiseFunction'
    particle_data06000datDisplay.OpacityArray = ['POINTS', 'ID']
    particle_data06000datDisplay.OpacityTransferFunction = 'PiecewiseFunction'
    particle_data06000datDisplay.DataAxesGrid = 'GridAxesRepresentation'
    particle_data06000datDisplay.PolarAxes = 'PolarAxesRepresentation'
    particle_data06000datDisplay.ScalarOpacityUnitDistance = 29.08728178106094
    particle_data06000datDisplay.SelectInputVectors = [None, '']
    particle_data06000datDisplay.WriteLog = ''

    # update the view to ensure updated data information
    renderView1.Update()

    # set scalar coloring
    ColorBy(particle_data06000datDisplay, ('POINTS', 'ID'))

    # rescale color and/or opacity maps used to include current data range
    particle_data06000datDisplay.RescaleTransferFunctionToDataRange(True, False)

    # show color bar/color legend
    particle_data06000datDisplay.SetScalarBarVisibility(renderView1, True)

    # get color transfer function/color map for 'ID'
    iDLUT = GetColorTransferFunction('ID')

    # get opacity transfer function/opacity map for 'ID'
    iDPWF = GetOpacityTransferFunction('ID')

    # get 2D transfer function for 'ID'
    iDTF2D = GetTransferFunction2D('ID')

    # change representation type
    particle_data06000datDisplay.SetRepresentationType('Point Gaussian')

    # Properties modified on particle_data06000datDisplay
    particle_data06000datDisplay.GaussianRadius = 4.0

    # Properties modified on renderView1
    renderView1.UseColorPaletteForBackground = 0

    # Properties modified on renderView1
    renderView1.Background = [1.0, 1.0, 1.0]

    # hide color bar/color legend
    particle_data06000datDisplay.SetScalarBarVisibility(renderView1, False)

    # Properties modified on renderView1
    renderView1.OrientationAxesLabelColor = [0.0, 0.0, 0.0]

    # get layout
    layout1 = GetLayout()

    # layout/tab size in pixels
    layout1.SetSize(618, 713)

    # current camera placement for renderView1
    renderView1.CameraPosition = [424.7328365493817, -176.54365186292173, 138.19731879172355]
    renderView1.CameraFocalPoint = [24.0, 24.0, 96.0]
    renderView1.CameraViewUp = [-0.0898596752128394, 0.029908353404466993, 0.9955052632544323]
    renderView1.CameraParallelScale = 117.47583727829321

    # save screenshot
    SaveScreenshot(f'F:/data/dkt_{step_str}.png', renderView1, ImageResolution=[618, 713])

    # layout/tab size in pixels
    layout1.SetSize(618, 713)

    # current camera placement for renderView1
    renderView1.CameraPosition = [424.7328365493817, -176.54365186292173, 138.19731879172355]
    renderView1.CameraFocalPoint = [24.0, 24.0, 96.0]
    renderView1.CameraViewUp = [-0.0898596752128394, 0.029908353404466993, 0.9955052632544323]
    renderView1.CameraParallelScale = 117.47583727829321

    # save screenshot
    SaveScreenshot(f'F:/data/dkt_{step_str}.png', renderView1, ImageResolution=[618, 713])

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
    layout1_1.SetSize(618, 713)

    #--------------------------------------------
    # uncomment the following to render all views
    # RenderAllViews()
    # alternatively, if you want to write images, you can use SaveScreenshot(...).