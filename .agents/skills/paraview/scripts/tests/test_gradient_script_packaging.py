"""验证交付给 Windows 的单文件工具始终与唯一算法源同步。"""
from pathlib import Path
import runpy
import unittest


class GradientPackagingTests(unittest.TestCase):
    def test_standalone_artifact_matches_current_algorithm_and_template(self):
        scripts = Path(__file__).resolve().parents[1]
        sender = runpy.run_path(str(scripts / 'paraview_bridge_send.py'))
        self.assertEqual((scripts / 'repair_amr_gradient.py').read_text(),
                         sender['gradient_script_source']())


if __name__ == '__main__':
    unittest.main()
