"""生成并校验交付给 Windows 的单文件：桥客户端与独立 AMR Gradient 修复脚本。"""
import argparse
import ast
from pathlib import Path


DIRECTORY = Path(__file__).resolve().parent


# 运行层按名字导入、由打包器嵌入客户端的操作模块。
OPERATION_MODULES = ('paraview_operations', 'paraview_publication')
BUNDLED_MODULES = ('paraview_protocol', *OPERATION_MODULES)


def client_source():
    """静态打包，不导入 ParaView、不启动 GUI，也不复制维护操作算法。"""
    runtime = (DIRECTORY / 'paraview_bridge_runtime.py').read_text(encoding='utf-8')
    body = ast.parse(runtime).body
    lines = runtime.splitlines(keepends=True)
    replacements = []
    for module in BUNDLED_MODULES:
        imports = [node for node in body if isinstance(node, ast.ImportFrom) and node.module == module]
        if len(imports) != 1:
            raise ValueError('运行层必须有且仅有一个 %s 导入' % module)
        source = (DIRECTORY / (module + '.py')).read_text(encoding='utf-8')
        exported = {n.name for n in ast.parse(source).body if isinstance(n, ast.FunctionDef)}
        exported.update(target.id for node in ast.parse(source).body if isinstance(node, ast.Assign)
                        for target in node.targets if isinstance(target, ast.Name))
        if any(alias.name not in exported or alias.asname for alias in imports[0].names):
            raise ValueError('运行层导入了未知操作或别名')
        replacements.append((imports[0], ['# 以下定义由 %s.py 自动嵌入。\n' % module, source, '\n']))
    # 自后向前替换，行号不受前面替换影响。
    for node, text in sorted(replacements, key=lambda item: -item[0].lineno):
        lines[node.lineno-1:node.end_lineno] = text
    source = ('# 自动生成，请勿手改。修改 paraview_bridge_runtime.py 或 '
              + ' / '.join(m + '.py' for m in BUNDLED_MODULES) + '，\n'
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
