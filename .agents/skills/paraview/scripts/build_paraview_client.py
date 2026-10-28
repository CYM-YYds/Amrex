"""生成并校验交付给 Windows 的单文件：桥客户端与独立 AMR Gradient 修复脚本。"""
import argparse
import ast
from pathlib import Path


DIRECTORY = Path(__file__).resolve().parent


def client_source():
    """静态打包，不导入 ParaView、不启动 GUI，也不复制维护操作算法。"""
    directory = DIRECTORY
    runtime = (directory / 'paraview_bridge_runtime.py').read_text(encoding='utf-8')
    operations = (directory / 'paraview_operations.py').read_text(encoding='utf-8')
    imports = [node for node in ast.parse(runtime).body
               if isinstance(node, ast.ImportFrom) and node.module == 'paraview_operations']
    if len(imports) != 1:
        raise ValueError('运行层必须有且仅有一个操作模块导入')
    node = imports[0]
    exported = {n.name for n in ast.parse(operations).body if isinstance(n, ast.FunctionDef)}
    exported.add('AMR_CELL_VORTICITY_SCRIPT')
    if any(alias.name not in exported or alias.asname for alias in node.names):
        raise ValueError('运行层导入了未知操作或别名')
    lines = runtime.splitlines(keepends=True)
    lines[node.lineno-1:node.end_lineno] = [
        '# 以下操作由 paraview_operations.py 自动嵌入。\n', operations,
        '\n# 以下为 paraview_bridge_runtime.py 的桥接运行逻辑。\n']
    source = ('# 自动生成，请勿手改。修改 paraview_bridge_runtime.py / paraview_operations.py，\n'
              '# 再运行 .venv/bin/python .agents/skills/paraview/scripts/build_paraview_client.py\n'
              + ''.join(lines))
    compile(source, 'paraview_bridge_client.py', 'exec')
    return source


def gradient_script_source():
    """将唯一求导算法打包为可单文件运行的 GUI 工具，避免维护第二份算法。"""
    source = (DIRECTORY / 'paraview_operations.py').read_text(encoding='utf-8')
    algorithm = next(ast.literal_eval(node.value) for node in ast.parse(source).body
                     if isinstance(node, ast.Assign) and any(
                         isinstance(target, ast.Name) and target.id == 'AMR_CELL_VORTICITY_SCRIPT'
                         for target in node.targets))
    template = (DIRECTORY.parent / 'templates/repair_amr_gradient.py.in').read_text(encoding='utf-8')
    return template.replace('__AMR_CELL_VORTICITY_SCRIPT_LITERAL__', repr(algorithm))


def deliverables():
    """交付物文件名到期望内容的映射；打包与检查共用，避免两处各列一遍。"""
    return {'paraview_bridge_client.py': client_source(),
            'repair_amr_gradient.py': gradient_script_source()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='只检查生成物是否同步')
    args = parser.parse_args()
    stale = []
    for name, content in deliverables().items():
        target = DIRECTORY / name
        expected = content.encode('utf-8')
        if args.check:
            if target.read_bytes() != expected:
                stale.append(name)
        else:
            target.write_bytes(expected)
    if stale:
        parser.exit(1, '生成物未同步：' + ', '.join(stale) + '；请先运行打包命令。\n')


if __name__ == '__main__':
    main()
