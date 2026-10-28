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
        modules = builder['BUNDLED_MODULES']
        self.assertFalse(any(isinstance(n, ast.ImportFrom) and n.module in modules
                             for n in ast.walk(tree)))
        # 每个操作的函数体在独立模块与单文件交付物中必须完全相同。
        functions = {n.name: ast.dump(n) for n in tree.body if isinstance(n, ast.FunctionDef)}
        for module in modules:
            for node in ast.parse((scripts / (module + '.py')).read_text()).body:
                if isinstance(node, ast.FunctionDef):
                    self.assertEqual(functions[node.name], ast.dump(node))

    def test_operation_modules_do_not_share_function_names(self):
        """嵌入同一命名空间的操作模块若重名，后者会静默覆盖前者。"""
        scripts = Path(__file__).resolve().parents[1]
        builder = runpy.run_path(str(scripts / 'build_paraview_client.py'))
        seen = {}
        for module in builder['BUNDLED_MODULES']:
            for node in ast.parse((scripts / (module + '.py')).read_text()).body:
                if isinstance(node, (ast.FunctionDef, ast.Assign)):
                    names = [node.name] if isinstance(node, ast.FunctionDef) else [
                        t.id for t in node.targets if isinstance(t, ast.Name)]
                    for name in names:
                        self.assertNotIn(name, seen, name + ' 在 %s 与 %s 中重复' % (seen.get(name), module))
                        seen[name] = module

    def test_standalone_artifact_matches_current_algorithm_and_template(self):
        scripts = Path(__file__).resolve().parents[1]
        builder = runpy.run_path(str(scripts / 'build_paraview_client.py'))
        self.assertEqual((scripts / 'repair_amr_gradient.py').read_text(),
                         builder['gradient_script_source']())


if __name__ == '__main__':
    unittest.main()
