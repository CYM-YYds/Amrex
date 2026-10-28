"""验证交付给 Windows 的单文件工具始终与唯一算法源同步。"""
from pathlib import Path
import runpy
import unittest
import ast


class GradientPackagingTests(unittest.TestCase):
    def test_client_bundle_matches_sources_without_runtime_import(self):
        scripts = Path(__file__).resolve().parents[1]
        builder = runpy.run_path(str(scripts / 'build_paraview_client.py'))
        bundle = (scripts / 'paraview_bridge_client.py').read_text()
        self.assertEqual(bundle, builder['client_source']())
        tree = ast.parse(bundle)
        self.assertFalse(any(isinstance(n, ast.ImportFrom) and n.module == 'paraview_operations'
                             for n in ast.walk(tree)))
        # 每个操作的函数体在独立模块与单文件交付物中必须完全相同。
        operations = ast.parse((scripts / 'paraview_operations.py').read_text())
        functions = {n.name: ast.dump(n) for n in tree.body if isinstance(n, ast.FunctionDef)}
        for node in operations.body:
            if isinstance(node, ast.FunctionDef):
                self.assertEqual(functions[node.name], ast.dump(node))

    def test_standalone_artifact_matches_current_algorithm_and_template(self):
        scripts = Path(__file__).resolve().parents[1]
        sender = runpy.run_path(str(scripts / 'paraview_bridge_send.py'))
        self.assertEqual((scripts / 'repair_amr_gradient.py').read_text(),
                         sender['gradient_script_source']())


if __name__ == '__main__':
    unittest.main()
