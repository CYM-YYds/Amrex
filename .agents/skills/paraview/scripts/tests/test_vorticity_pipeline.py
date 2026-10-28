"""真实 ParaView 旋度回归；普通仓库 Python 中跳过，使用编译节点 pvpython 执行。"""
from pathlib import Path
import builtins
import math
import os
import runpy
import unittest

try:
    from paraview import simple as pvs
except ImportError:
    pvs = None


@unittest.skipIf(pvs is None, 'requires ParaView pvpython')
class VorticityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        client = Path(__file__).resolve().parents[1] / 'paraview_bridge_client.py'
        cls.add_view = staticmethod(runpy.run_path(
            str(client), init_globals={'_PV_BRIDGE_DEFINITIONS_ONLY': True})['add_vorticity_view'])
        cls.publication = staticmethod(runpy.run_path(
            str(client), init_globals={'_PV_BRIDGE_DEFINITIONS_ONLY': True})['publication_vorticity'])
        cls.amr_script = runpy.run_path(
            str(client), init_globals={'_PV_BRIDGE_DEFINITIONS_ONLY': True})['AMR_CELL_VORTICITY_SCRIPT']
        cls.repair_slice = staticmethod(runpy.run_path(
            str(client), init_globals={'_PV_BRIDGE_DEFINITIONS_ONLY': True})['repair_vorticity_slice'])

    def tearDown(self):
        # 每项测试拥有独立场景，避免名称、活动视图和 MPI 过滤器生命周期互相污染。
        pvs.ResetSession()

    def test_axis_title_view_preserves_original(self):
        client = Path(__file__).resolve().parents[1] / 'paraview_bridge_client.py'
        action = runpy.run_path(str(client), init_globals={
            '_PV_BRIDGE_DEFINITIONS_ONLY': True})['add_axis_title_view']
        source = pvs.Wavelet()
        section = pvs.Slice(registrationName='axis_test_slice', Input=source)
        section.SliceType.Normal = [0, 1, 0]
        view = pvs.CreateView('RenderView')
        pvs.SetActiveView(view)
        display = pvs.Show(section, view)
        pvs.ColorBy(display, ('POINTS', 'RTData'))
        display.SetScalarBarVisibility(view, True)
        display.LookupTable.RescaleTransferFunction(0, 100)
        view.AxesGrid.Visibility = 1
        view.AxesGrid.ZTitle = 'Z custom'
        pvs.ResetCamera(view)
        pvs.Render(view)
        camera = list(view.CameraPosition)
        colors = list(display.LookupTable.RGBPoints)
        result = action({'source': 'axis_test_slice'})
        self.assertTrue(result['original_preserved'])
        self.assertNotEqual(result['view'], view.GetGlobalIDAsString())
        self.assertEqual(view.AxesGrid.ZTitle, 'Z custom')
        self.assertEqual(list(view.CameraPosition), camera)
        self.assertEqual(list(display.LookupTable.RGBPoints), colors)
        self.assertEqual(display.Visibility, 1)
        self.assertEqual(len(result['text_nodes']), 2)
        self.assertEqual(pvs.GetActiveView().AxesGrid.ZTitle, '')
        target = pvs.GetActiveView()
        target.CameraPosition = [20, 30, 40]
        target.CameraParallelScale = 9
        reset = action({'source': 'axis_test_slice', 'reset_view': result['view']})
        self.assertEqual(reset['camera_restored_from'], 'slice_bounds')
        self.assertEqual(list(target.CameraFocalPoint), [0, 0, 0])
        self.assertEqual(list(target.CameraViewUp), [0, 0, 1])
        self.assertAlmostEqual(target.CameraParallelScale, math.sqrt(800)/2)
        self.assertEqual(list(view.CameraPosition), camera)
        self.assertEqual(list(display.LookupTable.RGBPoints), colors)

    def test_existing_slice_repair_preserves_plane_display_and_camera(self):
        image = pvs.Wavelet()
        image.WholeExtent = [-4, 4, -4, 4, -4, 4]
        vector = pvs.Calculator(Input=image)
        vector.ResultArrayName = 'test_velocity'
        vector.Function = '-coordsY*iHat + coordsX*jHat'
        cells = pvs.PointDatatoCellData(Input=vector)
        gradient = pvs.Gradient(Input=cells)
        gradient.ScalarArray = ['CELLS', 'test_velocity']
        gradient.ComputeVorticity = 1
        section = pvs.Slice(registrationName='repair_test_slice', Input=gradient)
        section.SliceType.Origin = [0, 0.75, 0]
        section.SliceType.Normal = [0, 1, 0]
        view = pvs.CreateView('RenderView')
        display = pvs.Show(section, view)
        pvs.ColorBy(display, ('CELLS', 'Vorticity'))
        pvs.ResetCamera(view)
        pvs.Render(view)
        camera = list(view.CameraPosition)
        colors = list(display.ColorArrayName)
        display_id = display.GetGlobalIDAsString()
        result = self.repair_slice({'source': 'repair_test_slice'})
        self.assertEqual(result['slice'], section.GetGlobalIDAsString())
        self.assertEqual(result['previous_gradient'], gradient.GetGlobalIDAsString())
        self.assertEqual(result['vorticity_range'], [2.0, 2.0])
        self.assertEqual(list(section.SliceType.Origin), [0, 0.75, 0])
        self.assertEqual(list(section.SliceType.Normal), [0, 1, 0])
        self.assertEqual(list(view.CameraPosition), camera)
        self.assertEqual(list(display.ColorArrayName), colors)
        self.assertEqual(pvs.GetDisplayProperties(section, view).GetGlobalIDAsString(), display_id)
        self.assertNotEqual(section.GetProperty('Input')[0].GetGlobalIDAsString(), result['previous_gradient'])

    def test_empty_mpi_partition_is_not_treated_as_a_dataset(self):
        from types import SimpleNamespace
        from vtkmodules.vtkCommonDataModel import vtkDataObject
        source, output = vtkDataObject(), vtkDataObject()
        context = SimpleNamespace(GetInputDataObject=lambda *args: source,
                                  GetOutputDataObject=lambda *args: output)
        exec(self.amr_script, {'self': context})
        self.assertEqual(output.GetClassName(), 'vtkDataObject')

    def test_covered_coarse_cells_do_not_corrupt_visible_curl(self):
        import numpy as np
        from vtkmodules.util.numpy_support import vtk_to_numpy
        # 单块粗层的解析旋转场；覆盖单元仍有有效速度，掩膜只能影响显示。
        # 在各 rank 创建相同小夹具，避免旧原生 Gradient 对根 rank 专属 TrivialProducer 报错。
        from vtkmodules.vtkCommonDataModel import vtkMultiBlockDataSet
        source = pvs.ProgrammableSource()
        source.OutputDataSetType = vtkMultiBlockDataSet().GetDataObjectType()
        source.Script = '''
import numpy as np
from vtkmodules.vtkCommonDataModel import vtkUniformGrid, vtkDataSetAttributes, vtkMultiBlockDataSet
from vtkmodules.util.numpy_support import numpy_to_vtk
grid = vtkUniformGrid()
grid.SetDimensions(9, 9, 9)
grid.SetOrigin(10, -4, 2)
grid.SetSpacing(0.25, 0.25, 0.25)
k, j, i = np.mgrid[:8, :8, :8]
v = np.column_stack((-(-4+(j.ravel()+0.5)*0.25),
                     10+(i.ravel()+0.5)*0.25, np.zeros(512)))
vector = numpy_to_vtk(v, deep=True)
vector.SetName('VelocityForVorticity')
grid.GetCellData().AddArray(vector)
flags = np.zeros((8,8,8), dtype=np.uint8)
flags[2:6,2:6,2:6] = vtkDataSetAttributes.REFINEDCELL
flags[0,0,0] = vtkDataSetAttributes.DUPLICATECELL
ghosts = numpy_to_vtk(flags.ravel(), deep=True)
ghosts.SetName('vtkGhostType')
grid.GetCellData().AddArray(ghosts)
output = self.GetOutputDataObject(0)
output.SetNumberOfBlocks(2)
output.SetBlock(0, grid)
'''
        gradient = pvs.ProgrammableFilter(Input=source)
        gradient.Script = self.amr_script
        gradient.UpdatePipeline()
        original = source.GetClientSideObject().GetOutputDataObject(0).GetBlock(0)
        output = gradient.GetClientSideObject().GetOutputDataObject(0).GetBlock(0)
        flags = vtk_to_numpy(original.GetCellData().GetArray('vtkGhostType')).copy()
        self.assertEqual(int(np.count_nonzero(flags == 8)), 64)
        np.testing.assert_array_equal(
            vtk_to_numpy(output.GetCellData().GetArray('vtkGhostType')), flags)
        vorticity = vtk_to_numpy(output.GetCellData().GetArray('Vorticity'))
        np.testing.assert_allclose(vorticity, np.tile([0, 0, 2], (512, 1)), atol=1e-12)
        # 用户独立脚本应继承所有勾选项与自定义数组名，并保留旧管线。
        original_gradient = pvs.Gradient(registrationName='manual_gradient', Input=source)
        original_gradient.ScalarArray = ['CELLS', 'VelocityForVorticity']
        original_gradient.ComputeGradient = 1
        original_gradient.ComputeVorticity = 1
        original_gradient.ComputeDivergence = 1
        original_gradient.ComputeQCriterion = 1
        original_gradient.ResultArrayName = 'manual_gradient_array'
        original_gradient.VorticityArrayName = 'manual_curl'
        original_gradient.DivergenceArrayName = 'manual_divergence'
        original_gradient.QCriterionArrayName = 'manual_q'
        section = pvs.Slice(Input=original_gradient)
        standalone = Path(__file__).resolve().parents[1] / 'repair_amr_gradient.py'
        pvs.SetActiveSource(original_gradient)
        corrected = runpy.run_path(str(standalone))['repaired_gradient']
        self.assertEqual(pvs.GetActiveSource().GetGlobalIDAsString(), corrected.GetGlobalIDAsString())
        self.assertEqual(section.GetProperty('Input')[0].GetGlobalIDAsString(),
                         original_gradient.GetGlobalIDAsString())
        self.assertEqual(corrected.GetProperty('Input')[0].GetGlobalIDAsString(), source.GetGlobalIDAsString())
        corrected_data = corrected.GetClientSideObject().GetOutputDataObject(0).GetBlock(0).GetCellData()
        np.testing.assert_array_equal(vtk_to_numpy(corrected_data.GetArray('vtkGhostType')), flags)
        np.testing.assert_allclose(vtk_to_numpy(corrected_data.GetArray('manual_curl')),
                                   np.tile([0, 0, 2], (512, 1)), atol=1e-12)
        np.testing.assert_allclose(vtk_to_numpy(corrected_data.GetArray('manual_gradient_array')),
                                   np.tile([0, -1, 0, 1, 0, 0, 0, 0, 0], (512, 1)), atol=1e-12)
        np.testing.assert_allclose(vtk_to_numpy(corrected_data.GetArray('manual_divergence')), 0, atol=1e-12)
        np.testing.assert_allclose(vtk_to_numpy(corrected_data.GetArray('manual_q')), 1, atol=1e-12)
        pvs.Delete(corrected)
        pvs.Delete(section)
        pvs.Delete(original_gradient)
        pvs.Delete(gradient)
        pvs.Delete(source)

    def test_solid_body_rotation_and_original_view_preserved(self):
        # 解析场 u=(-y,x,0) 的旋度应为 (0,0,2)，避免只检查过滤器名字或成功退出。
        image = pvs.Wavelet(registrationName='rotation_grid')
        image.WholeExtent = [-4, 4, -4, 4, -4, 4]
        source = image
        for name, expression in [('ux', '-coordsY'), ('uy', 'coordsX'), ('uz', '0*coordsZ')]:
            source = pvs.Calculator(registrationName='rotation_' + name, Input=source)
            source.ResultArrayName = name
            source.Function = expression
        old_view = pvs.CreateView('RenderView')
        pvs.SetActiveView(old_view)
        old_camera = list(old_view.CameraPosition)
        result = self.add_view({'source': 'rotation_uz', 'slice_axis': 'y'})
        self.assertEqual(result['vorticity_range'], [2.0, 2.0])
        self.assertEqual(result['slice_origin'], [0.0, 0.0, 0.0])
        self.assertNotEqual(result['view'], old_view.GetGlobalIDAsString())
        self.assertEqual(list(old_view.CameraPosition), old_camera)
        gradient = next(s for s in pvs.GetSources().values()
                        if s.GetGlobalIDAsString() == result['gradient'])
        array = gradient.PointData['Vorticity']
        self.assertEqual(list(array.GetRange(0)), [0.0, 0.0])
        self.assertEqual(list(array.GetRange(1)), [0.0, 0.0])
        self.assertEqual(list(array.GetRange(2)), [2.0, 2.0])
        # 排版不得改变原旋度；新标量严格按指定 H/Uref 缩放。
        styled = self.publication({'source': 'Vorticity_slice_y',
                                   'vorticity_scale': 20, 'color_range': [0, 50]})
        self.assertEqual(styled['scaled_range'], [40.0, 40.0])
        self.assertEqual(styled['view'], result['view'])
        self.assertEqual(list(array.GetRange(2)), [2.0, 2.0])
        self.assertEqual(list(pvs.GetActiveView().Background), [1, 1, 1])
        self.assertEqual(pvs.GetActiveView().UseColorPaletteForBackground, 0)
        repeated = self.publication({'source': 'Vorticity_slice_y',
                                     'vorticity_scale': 20, 'color_range': [0, 50]})
        self.assertEqual(repeated['scalar'], styled['scalar'])
        overlay = pvs.Show(image, pvs.GetActiveView())
        reference = self.publication({'source': 'Vorticity_slice_y', 'style': 'reference',
                                      'vorticity_scale': 20, 'color_range': [0, 50]})
        self.assertEqual(reference['scalar'], styled['scalar'])
        self.assertEqual(reference['scaled_range'], [40.0, 40.0])
        self.assertEqual(overlay.Visibility, 0)
        styled_source = next(s for s in pvs.GetSources().values()
                             if s.GetGlobalIDAsString() == reference['scalar'])
        styled_display = pvs.GetDisplayProperties(styled_source, pvs.GetActiveView())
        bar = pvs.GetScalarBar(styled_display.LookupTable, pvs.GetActiveView())
        self.assertEqual(list(bar.CustomLabels), [0, 50])
        self.assertEqual(bar.TitleFontFamily, 'Arial')
        self.assertAlmostEqual(pvs.GetActiveView().CameraFocalPoint[2], -0.576)
        self.assertLess(bar.TitleFontSize, 36)
        # 导出时可用高清字号，但返回 GUI 后必须恢复窗口字号。
        import tempfile
        import struct
        with tempfile.TemporaryDirectory() as tmp:
            output = str(Path(tmp) / 'reference.png')
            before_font = bar.TitleFontSize
            pvs.Hide(styled_source, pvs.GetActiveView())
            original_slice = pvs.FindSource('Vorticity_slice_y')
            pvs.Show(original_slice, pvs.GetActiveView())
            overlay.Visibility = 1
            styled_source.Function = 'normmag(Vorticity)*20.0'
            styled_source.GetProperty('Function').SMProperty.SetUncheckedElement(0, 'normmag(Vorticity)*20.0')
            exported = self.publication({'source': 'Vorticity_slice_y', 'style': 'reference',
                                         'vorticity_scale': 20, 'color_range': [0, 50], 'output': output})
            self.assertEqual(exported['image_resolution'], [2400, 2640])
            self.assertEqual(struct.unpack('>II', Path(output).read_bytes()[16:24]), (2400, 2640))
            self.assertEqual(bar.TitleFontSize, before_font)
            self.assertEqual(exported['scaled_range'], [40.0, 40.0])
            self.assertEqual(styled_display.Visibility, 1)
            self.assertEqual(pvs.GetDisplayProperties(original_slice, pvs.GetActiveView()).Visibility, 0)
            self.assertEqual(overlay.Visibility, 0)
            self.assertEqual(str(styled_source.Function), 'mag(Vorticity)*20.0')
            self.assertEqual(styled_source.GetProperty('Function').SMProperty.GetUncheckedElement(0),
                             'mag(Vorticity)*20.0')
            fixed = self.publication({'source': 'Vorticity_slice_y', 'style': 'reference',
                                     'vorticity_scale': 20, 'color_range': [0, 50],
                                     'image_size': [2066, 1066], 'output': output})
            view = pvs.GetActiveView()
            self.assertEqual(list(view.ViewSize), [2066, 1066])
            self.assertEqual(list(pvs.GetLayout(view).PreviewMode), [0, 0])
            self.assertEqual(fixed['image_resolution'], [2066, 1066])
            alignment = fixed['title_alignment']
            left, right, bottom, top = alignment['frame_pixels']
            boxes = alignment['text_boxes_pixels']
            gap = alignment['edge_gap_pixels']
            self.assertAlmostEqual(bottom-boxes[0][3], gap)
            self.assertAlmostEqual(left-boxes[1][1], gap)
            self.assertAlmostEqual(boxes[2][2]-top, gap)
            self.assertAlmostEqual(boxes[3][0]-right, gap)
            for i in (0, 2):
                self.assertAlmostEqual((boxes[i][0]+boxes[i][1])/2, (left+right)/2)
            for i in (1, 3):
                self.assertAlmostEqual((boxes[i][2]+boxes[i][3])/2, (bottom+top)/2)
            self.assertEqual(struct.unpack('>II', Path(output).read_bytes()[16:24]), (2066, 1066))
            # 返回后的预览直接再次截图，必须与正式导出逐字节相同。
            preview = str(Path(tmp) / 'preview.png')
            pvs.SaveScreenshot(preview, view, ImageResolution=[2066, 1066],
                               FontScaling='Do not scale fonts')
            self.assertEqual(Path(output).read_bytes(), Path(preview).read_bytes())

    def test_rejects_missing_source_before_creating_pipeline(self):
        before = set(pvs.GetSources())
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            self.add_view({'source': 'not_a_registered_source'})
        self.assertEqual(set(pvs.GetSources()), before)

    def test_rejects_non_vector_component(self):
        with self.assertRaisesRegex(ValueError, 'component'):
            self.add_view({'source': 'unused', 'component': 'arbitrary expression'})

    @unittest.skipUnless(os.environ.get('PV_EXPECT_MPI_RANKS'), 'no MPI rank count requested')
    def test_mpi_controller_has_requested_process_count(self):
        from paraview import servermanager
        controller = servermanager.vtkProcessModule.GetProcessModule().GetGlobalController()
        self.assertEqual(controller.GetClassName(), 'vtkMPIController')
        self.assertEqual(controller.GetNumberOfProcesses(), int(os.environ['PV_EXPECT_MPI_RANKS']))

    @unittest.skipUnless(os.environ.get('PV_VORTICITY_TEST_INPUT'), 'no real PlotFile selected')
    def test_real_amrex_input(self):
        source = pvs.AMReXBoxLibGridReader(
            registrationName='real_vorticity_input', FileNames=[os.environ['PV_VORTICITY_TEST_INPUT']])
        source.Level = 2
        source.CellArrayStatus = ['ux', 'uy', 'uz']
        result = self.add_view({'source': 'real_vorticity_input'})
        pvs.RenameSource('real_amrex_vorticity_slice', pvs.GetActiveSource())
        # ProgrammableFilter 会向 __main__ 导入 VTK 的 all；此处不能触发 MPI 全局归约。
        self.assertTrue(builtins.all(math.isfinite(v) for v in result['vorticity_range']))
        self.assertGreater(result['vorticity_range'][1], 0)
        vector = pvs.Calculator(Input=source)
        vector.AttributeType = 'Cell Data'
        vector.ResultArrayName = 'manual_velocity'
        vector.Function = 'ux*iHat + uy*jHat + uz*kHat'
        original = pvs.Gradient(Input=vector)
        original.ScalarArray = ['CELLS', 'manual_velocity']
        original.ComputeVorticity = 1
        standalone = Path(__file__).resolve().parents[1] / 'repair_amr_gradient.py'
        create = runpy.run_path(str(standalone), init_globals={
            '_AMR_GRADIENT_DEFINITIONS_ONLY': True})['create_corrected_gradient']
        corrected = create(original)
        self.assertEqual(corrected.GetProperty('Input')[0].GetGlobalIDAsString(), vector.GetGlobalIDAsString())
        self.assertTrue(builtins.all(math.isfinite(v) for v in corrected.CellData['Vorticity'].GetRange(-1)))
        print('REAL_AMREX_VORTICITY', result)


if __name__ == '__main__':
    result = unittest.main(exit=False).result
    if not result.wasSuccessful():
        raise RuntimeError("Vorticity pipeline tests failed")
