# state file generated using paraview version 5.11.2
import os

import paraview

paraview.compatibility.major = 5
paraview.compatibility.minor = 11

#### import the simple module from the paraview
from paraview.simple import *


DEFAULT_DATA_PATH = r'/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/3Dcases/Computing_performance_test/BOX3D/data32to64/case_Re1_4p_010000'


def build_pipeline(data_path=None, source=None):
    """Build the Box3D visualization pipeline.

    Parameters
    ----------
    data_path : str, optional
        Path to the AMReX/BoxLib dataset. Ignored if ``source`` is provided.
    source : paraview proxy, optional
        An already-loaded data source (e.g. from GetActiveSource()).
        When given, the file reader is skipped and this source is used directly.
    """
    #### disable automatic camera reset on 'Show'
    paraview.simple._DisableFirstRenderCameraReset()

    # ----------------------------------------------------------------
    # setup views used in the visualization
    # ----------------------------------------------------------------

    # get the material library
    materialLibrary1 = GetMaterialLibrary()

    # Create a new 'Render View'
    renderView1 = CreateView('RenderView')
    renderView1.ViewSize = [1966, 1093]
    renderView1.InteractionMode = '2D'
    renderView1.AxesGrid = 'GridAxes3DActor'
    renderView1.CenterOfRotation = [32.0, 32.0, 32.0]
    renderView1.StereoType = 'Crystal Eyes'
    renderView1.CameraPosition = [51.68934947309561, -182.14817375687883, 30.738336987199382]
    renderView1.CameraFocalPoint = [51.68934947309561, 32.0, 30.738336987199382]
    renderView1.CameraViewUp = [0.0, 0.0, 1.0]
    renderView1.CameraFocalDisk = 1.0
    renderView1.CameraParallelScale = 67.06500726906687
    # renderView1.BackEnd = 'OSPRay raycaster'
    renderView1.OSPRayMaterialLibrary = materialLibrary1

    SetActiveView(None)

    # ----------------------------------------------------------------
    # setup view layouts
    # ----------------------------------------------------------------

    # create new layout object 'Layout #1'
    layout1 = CreateLayout(name='Layout #1')
    layout1.AssignView(0, renderView1)
    layout1.SetSize(1966, 1093)

    # ----------------------------------------------------------------
    # restore active view
    SetActiveView(renderView1)
    # ----------------------------------------------------------------

    # ----------------------------------------------------------------
    # setup the data processing pipelines
    # ----------------------------------------------------------------

    if source is not None:
        # Use the already-loaded source (e.g. manually opened in ParaView)
        case_Re1_4p_010000 = source
        print(f'[Box3D] Using existing source: {source.FileNames if hasattr(source, "FileNames") else "(unknown)"}')
    else:
        # Read from disk
        data_path = data_path or os.environ.get('BOX3D_DATA_PATH', DEFAULT_DATA_PATH)
        if not os.path.exists(data_path):
            raise FileNotFoundError(
                f'Data file or directory not found: {data_path}. '
                'Set BOX3D_DATA_PATH or pass a valid path to build_pipeline().'
            )
        case_Re1_4p_010000 = AMReXBoxLibGridReader(registrationName='case_Re1_4p_010000', FileNames=[data_path])
        case_Re1_4p_010000.Level = 3
        case_Re1_4p_010000.CellArrayStatus = ['ux', 'uy', 'uz']

    # create a new 'Extract Block'
    extractBlock1 = ExtractBlock(registrationName='ExtractBlock1', Input=case_Re1_4p_010000)
    extractBlock1.Selectors = ['/Root']

    # create a new 'Calculator'
    calculator1 = Calculator(registrationName='Calculator1', Input=extractBlock1)
    calculator1.AttributeType = 'Cell Data'
    calculator1.Function = 'ux*iHat+uy*jHat+uz*kHat'

    # create a new 'Gradient'
    gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
    gradient1.ScalarArray = ['CELLS', 'Result']
    gradient1.ComputeVorticity = 1

    # create a new 'Slice'
    slice1 = Slice(registrationName='Slice1', Input=gradient1)
    slice1.SliceType = 'Plane'
    slice1.HyperTreeGridSlicer = 'Plane'
    slice1.SliceOffsetValues = [0.0]

    # init the 'Plane' selected for 'SliceType'
    slice1.SliceType.Origin = [32.0, 32.0, 32.0]
    slice1.SliceType.Normal = [0.0, 1.0, 0.0]

    # init the 'Plane' selected for 'HyperTreeGridSlicer'
    slice1.HyperTreeGridSlicer.Origin = [32.0, 32.0, 32.0]

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
    vtkBlockColorsLUT.RGBPoints = [5.669699981808667e-06, 0.231373, 0.298039, 0.752941, 0.028351334759034213, 0.865003, 0.865003, 0.865003, 0.05669699981808662, 0.705882, 0.0156863, 0.14902]
    vtkBlockColorsLUT.Annotations = ['0', '0', '1', '1', '2', '2', '3', '3', '4', '4', '5', '5', '6', '6', '7', '7', '8', '8', '9', '9', '10', '10', '11', '11']
    vtkBlockColorsLUT.ActiveAnnotatedValues = ['0', '1', '2', '3']
    vtkBlockColorsLUT.IndexedColors = [1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.63, 0.63, 1.0, 0.67, 0.5, 0.33, 1.0, 0.5, 0.75, 0.53, 0.35, 0.7, 1.0, 0.75, 0.5]

    # get opacity transfer function/opacity map for 'vtkBlockColors'
    vtkBlockColorsPWF = GetOpacityTransferFunction('vtkBlockColors')
    vtkBlockColorsPWF.Points = [5.669699981808662e-06, 0.0, 0.5, 0.0, 0.056696999818086624, 1.0, 0.5, 0.0]

    # trace defaults for the display properties.
    extractBlock1Display.Representation = 'Outline'
    extractBlock1Display.ColorArrayName = ['FIELD', 'vtkBlockColors']
    extractBlock1Display.LookupTable = vtkBlockColorsLUT
    extractBlock1Display.SelectTCoordArray = 'None'
    extractBlock1Display.SelectNormalArray = 'None'
    extractBlock1Display.SelectTangentArray = 'None'
    extractBlock1Display.OSPRayScaleFunction = 'PiecewiseFunction'
    extractBlock1Display.SelectOrientationVectors = 'None'
    extractBlock1Display.ScaleFactor = 6.4
    extractBlock1Display.SelectScaleArray = 'None'
    extractBlock1Display.GlyphType = 'Arrow'
    extractBlock1Display.GlyphTableIndexArray = 'None'
    extractBlock1Display.GaussianRadius = 0.32
    extractBlock1Display.SetScaleArray = [None, '']
    extractBlock1Display.ScaleTransferFunction = 'PiecewiseFunction'
    extractBlock1Display.OpacityArray = [None, '']
    extractBlock1Display.OpacityTransferFunction = 'PiecewiseFunction'
    extractBlock1Display.DataAxesGrid = 'GridAxesRepresentation'
    extractBlock1Display.PolarAxes = 'PolarAxesRepresentation'
    extractBlock1Display.ScalarOpacityUnitDistance = 0.4982846751525569
    extractBlock1Display.ScalarOpacityFunction = vtkBlockColorsPWF
    extractBlock1Display.TransferFunction2D = vtkBlockColorsTF2D
    extractBlock1Display.OpacityArrayName = ['CELLS', 'ux']
    extractBlock1Display.ColorArray2Name = ['CELLS', 'ux']
    extractBlock1Display.SliceFunction = 'Plane'
    extractBlock1Display.Slice = 32
    extractBlock1Display.SelectInputVectors = [None, '']
    extractBlock1Display.WriteLog = ''

    # init the 'PiecewiseFunction' selected for 'OSPRayScaleFunction'
    extractBlock1Display.OSPRayScaleFunction.Points = [5.669699981808662e-06, 0.0, 0.5, 0.0, 0.056696999818086624, 1.0, 0.5, 0.0]

    # init the 'Plane' selected for 'SliceFunction'
    extractBlock1Display.SliceFunction.Origin = [32.0, 32.0, 32.0]

    # show data from slice1
    slice1Display = Show(slice1, renderView1, 'GeometryRepresentation')

    # get 2D transfer function for 'Vorticity'
    vorticityTF2D = GetTransferFunction2D('Vorticity')
    vorticityTF2D.ScalarRangeInitialized = 1
    vorticityTF2D.Range = [-0.11431404951326067, 0.02, 0.0, 1.0]

    # get color transfer function/color map for 'Vorticity'
    vorticityLUT = GetColorTransferFunction('Vorticity')
    vorticityLUT.TransferFunction2D = vorticityTF2D
    vorticityLUT.RGBPoints = [-0.11431404951326067, 0.231373, 0.298039, 0.752941, -0.047157024756630334, 0.865003, 0.865003, 0.865003, 0.020000000000000004, 0.705882, 0.0156863, 0.14902]
    vorticityLUT.ScalarRangeInitialized = 1.0
    vorticityLUT.VectorComponent = 1
    vorticityLUT.VectorMode = 'Component'

    # trace defaults for the display properties.
    slice1Display.Representation = 'Surface'
    slice1Display.ColorArrayName = ['CELLS', 'Vorticity']
    slice1Display.LookupTable = vorticityLUT
    slice1Display.SelectTCoordArray = 'None'
    slice1Display.SelectNormalArray = 'None'
    slice1Display.SelectTangentArray = 'None'
    slice1Display.OSPRayScaleFunction = 'PiecewiseFunction'
    slice1Display.SelectOrientationVectors = 'Result'
    slice1Display.ScaleFactor = 6.4
    slice1Display.SelectScaleArray = 'None'
    slice1Display.GlyphType = 'Arrow'
    slice1Display.GlyphTableIndexArray = 'None'
    slice1Display.GaussianRadius = 0.32
    slice1Display.SetScaleArray = [None, '']
    slice1Display.ScaleTransferFunction = 'PiecewiseFunction'
    slice1Display.OpacityArray = [None, '']
    slice1Display.OpacityTransferFunction = 'PiecewiseFunction'
    slice1Display.DataAxesGrid = 'GridAxesRepresentation'
    slice1Display.PolarAxes = 'PolarAxesRepresentation'
    slice1Display.SelectInputVectors = [None, '']
    slice1Display.WriteLog = ''

    # init the 'PiecewiseFunction' selected for 'OSPRayScaleFunction'
    slice1Display.OSPRayScaleFunction.Points = [5.669699981808662e-06, 0.0, 0.5, 0.0, 0.056696999818086624, 1.0, 0.5, 0.0]

    # setup the color legend parameters for each legend in this view

    # get color legend/bar for vtkBlockColorsLUT in view renderView1
    vtkBlockColorsLUTColorBar = GetScalarBar(vtkBlockColorsLUT, renderView1)
    vtkBlockColorsLUTColorBar.WindowLocation = 'Upper Right Corner'
    vtkBlockColorsLUTColorBar.Title = 'vtkBlockColors'
    vtkBlockColorsLUTColorBar.ComponentTitle = ''

    # set color bar visibility
    vtkBlockColorsLUTColorBar.Visibility = 1

    # get color legend/bar for vorticityLUT in view renderView1
    vorticityLUTColorBar = GetScalarBar(vorticityLUT, renderView1)
    vorticityLUTColorBar.Title = 'Vorticity'
    vorticityLUTColorBar.ComponentTitle = 'Y'

    # set color bar visibility
    vorticityLUTColorBar.Visibility = 1

    # show color legend
    extractBlock1Display.SetScalarBarVisibility(renderView1, True)

    # show color legend
    slice1Display.SetScalarBarVisibility(renderView1, True)

    # ----------------------------------------------------------------
    # setup color maps and opacity mapes used in the visualization
    # note: the Get..() functions create a new object, if needed
    # ----------------------------------------------------------------

    # get opacity transfer function/opacity map for 'Vorticity'
    vorticityPWF = GetOpacityTransferFunction('Vorticity')
    vorticityPWF.Points = [-0.11431404951326067, 0.0, 0.5, 0.0, 0.020000000000000004, 1.0, 0.5, 0.0]
    vorticityPWF.ScalarRangeInitialized = 1

    # ----------------------------------------------------------------
    # restore active source
    SetActiveSource(slice1)
    # ----------------------------------------------------------------

    return {
        'renderView1': renderView1,
        'layout1': layout1,
        'case_Re1_4p_010000': case_Re1_4p_010000,
        'extractBlock1': extractBlock1,
        'calculator1': calculator1,
        'gradient1': gradient1,
        'slice1': slice1,
    }


def main(data_path=None, source=None):
    return build_pipeline(data_path=data_path, source=source)


# =============================================================================
# In ParaView's Python Shell, __name__ is NOT '__main__' (it's '__builtin__'),
# so the usual if-__name__-guard never fires. The code below runs unconditionally
# so the script works both in the Shell and via "Run Script".
# =============================================================================

# Strategy: prefer an already-loaded source (manually opened in ParaView),
# otherwise fall back to reading from the file path.
_existing_source = None
try:
    _existing_source = GetActiveSource()
    if _existing_source is None:
        raise RuntimeError('No active source')
    print(f'[Box3D] Detected active source, using it as pipeline input.')
except Exception:
    pass

if _existing_source is not None:
    _pipeline = main(source=_existing_source)
    print('[Box3D] Pipeline built successfully from existing source.')
else:
    _data_path = os.environ.get('BOX3D_DATA_PATH', DEFAULT_DATA_PATH)
    if os.path.exists(_data_path):
        _pipeline = main(data_path=_data_path)
        print('[Box3D] Pipeline built successfully from file.')
    else:
        print(
            f'[Box3D] No active source and default data path not found:\n'
            f'         {_data_path}\n'
            f'[Box3D] Options:\n'
            f'  1. Open your dataset in ParaView first, then re-run this script.\n'
            f'  2. Set BOX3D_DATA_PATH environment variable.\n'
            f'  3. Call build_pipeline(source=your_source) or build_pipeline(data_path="...") manually.'
        )