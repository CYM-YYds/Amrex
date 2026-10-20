"""真实 ParaView 旋度回归；普通仓库 Python 中跳过，使用编译节点 pvpython 执行。"""
from pathlib import Path
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
        self.assertTrue(all(math.isfinite(v) for v in result['vorticity_range']))
        self.assertGreater(result['vorticity_range'][1], 0)
        print('REAL_AMREX_VORTICITY', result)


if __name__ == '__main__':
    result = unittest.main(exit=False).result
    if not result.wasSuccessful():
        raise RuntimeError("Vorticity pipeline tests failed")
