#!/usr/bin/env python3
"""Copy AMReX learning projects into this repository, minus build artifacts."""

from __future__ import annotations

import argparse
import fnmatch
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, Sequence

REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = Path("/home/share/huazkjdxmrsgjzdsyshi/home/lijing/caiyimin/learnamerx")
PROJECTS_ROOT = REPO_ROOT / "projects"

DEFAULT_INCLUDE_PATTERNS: tuple[str, ...] = (
    "*.H",
    "*.h",
    "*.hpp",
    "*.cpp",
    "*.c",
    "*.cu",
    "*.py",
    "*.sh",
    "GNUmakefile",
    "Makefile",
    "Make.package",
    "CMakeLists.txt",
    "inputs",
    "*.inputs",
    "*.cfg",
    "*.ini",
    "*.md",
    "*.txt",
    "compile.sh",
    "clean.sh",
    "submit.sh",
    "compile_flags.txt",
    "compile_commands.json",
)

DEFAULT_EXCLUDE_PATTERNS: tuple[str, ...] = (
    ".git",
    ".git/**",
    ".vscode",
    ".vscode/**",
    ".cache",
    ".cache/**",
    "__pycache__",
    "__pycache__/**",
    "tmp_build_dir",
    "tmp_build_dir/**",
    "**/tmp_build_dir",
    "**/tmp_build_dir/**",
    "case_*",
    "case_*/**",
    "**/case_*",
    "**/case_*/**",
    "plt*",
    "plt*/**",
    "**/plt*",
    "**/plt*/**",
    "*.ex",
    "*.out",
    "*.log",
    "*.err",
    "build",
    "build/**",
    "*.tar",
    "*.tgz",
    "*.zip",
    "*.vtk",
    "*.bin",
    "*.nfs*",
    "Backtrace.*",
    "**/Backtrace.*",
)


def _glob_match(rel_path: Path, patterns: Sequence[str]) -> bool:
    rel_posix = rel_path.as_posix()
    for pattern in patterns:
        if fnmatch.fnmatch(rel_posix, pattern):
            return True
    return False


def _any_parent_matches(rel_path: Path, patterns: Sequence[str]) -> bool:
    for parent in rel_path.parents:
        if parent == Path("."):
            continue
        if _glob_match(parent, patterns):
            return True
    return False


def _make_readme(name: str, description: str) -> str:
    return f"""# {name}

## 概览
{description}。

## 目录结构
- `src/` — 原始 C++/CUDA 源码，直接从学习项目复制。
- `config/` — 构建配置与 AMReX 输入文件。
- `scripts/` — 编译、清理与提交脚本。
- `data/` — 运行结果或分析用辅助数据（如有）。
- `docs/` — 附加文档（如有）。

## 同步说明
这些文件由 `tools/sync_projects.py` 自动同步。如需再次更新，请在仓库根目录运行：

```bash
python tools/sync_projects.py --project {name}
```

加入 `--clean` 可先清空目标目录，`--extra-exclude PATTERN` 可临时排除更多文件。
"""


@dataclass(frozen=True)
class ProjectSpec:
    name: str
    description: str
    source_dir: Path
    include_patterns: Sequence[str] | None = None
    exclude_patterns: Sequence[str] | None = None
    readme_content: str | None = None

    def __post_init__(self) -> None:
        object.__setattr__(self, "include_patterns", tuple(self.include_patterns or DEFAULT_INCLUDE_PATTERNS))
        object.__setattr__(self, "exclude_patterns", tuple(self.exclude_patterns or DEFAULT_EXCLUDE_PATTERNS))

    def iter_files(self, extra_exclude: Sequence[str] | None = None) -> Iterator[tuple[Path, Path]]:
        combined_exclude = tuple(self.exclude_patterns) + tuple(extra_exclude or ())  # type: ignore[operator]
        seen: set[Path] = set()
        for pattern in self.include_patterns:  # type: ignore[arg-type]
            for candidate in self.source_dir.rglob(pattern):
                if candidate.is_dir():
                    continue
                rel = candidate.relative_to(self.source_dir)
                if rel in seen:
                    continue
                if self._should_skip(rel, combined_exclude):
                    continue
                seen.add(rel)
                yield candidate, rel

    def _should_skip(self, rel_path: Path, patterns: Sequence[str]) -> bool:
        if _glob_match(rel_path, patterns):
            return True
        if _any_parent_matches(rel_path, patterns):
            return True
        return False

    def destination_for(self, rel_path: Path) -> Path:
        name = rel_path.name
        suffix = rel_path.suffix.lower()
        parent = rel_path.parent if rel_path.parent != Path(".") else None

        def join(category: str) -> Path:
            if parent is None:
                return Path(category) / name
            relative_parent = Path(*parent.parts[1:]) if parent.parts and parent.parts[0] == category else parent
            return Path(category) / relative_parent / name if relative_parent != Path(".") else Path(category) / name

        if name.lower() == "readme.md":
            return rel_path

        if suffix in {".cpp", ".c", ".cu", ".h", ".hpp", ".hip", ".cxx"} or rel_path.suffix == ".H":
            return join("src")

        if name == "CMakeLists.txt":
            return join("config")

        if name in {"GNUmakefile", "Make.package"}:
            return join("config")

        if suffix in {".inputs", ".ini", ".cfg"} or name in {
            "inputs",
            "compile_flags.txt",
            "compile_commands.json",
        }:
            return join("config")

        if suffix == ".sh":
            return join("scripts")

        if suffix in {".txt", ".dat", ".csv"}:
            return join("data")

        if suffix == ".md":
            return join("docs")

        return rel_path

    def sync(self, clean: bool = False, extra_exclude: Sequence[str] | None = None) -> None:
        target_root = PROJECTS_ROOT / self.name
        if clean and target_root.exists():
            shutil.rmtree(target_root, ignore_errors=True)
            if target_root.exists():
                for child in target_root.iterdir():
                    if child.is_dir():
                        shutil.rmtree(child, ignore_errors=True)
                    else:
                        try:
                            child.unlink()
                        except FileNotFoundError:
                            pass
                try:
                    target_root.rmdir()
                except OSError:
                    pass

        print(f"→ Syncing {self.name} ({self.description})")
        copied = 0
        for src, rel in self.iter_files(extra_exclude=extra_exclude):
            dst_rel = self.destination_for(rel)
            dst = target_root / dst_rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            if dst_rel != rel:
                print(f"   {rel.as_posix()} -> {dst_rel.as_posix()}")
            else:
                print(f"   {rel.as_posix()}")
            copied += 1

        if copied == 0:
            print("   ! No files copied (check include/exclude patterns)")
        print(f"← Done {self.name} (copied {copied} files)\n")

        self._ensure_readme(target_root)

    def _ensure_readme(self, target_root: Path) -> None:
        if not self.readme_content:
            return
        readme_path = target_root / "README.md"
        if readme_path.exists():
            return
        readme_path.write_text(self.readme_content, encoding="utf-8")


def build_specs() -> dict[str, ProjectSpec]:
    return {
        "Cavity2D": ProjectSpec(
            name="Cavity2D",
            description="2D lid-driven cavity benchmark (LBM on D2Q9)",
            source_dir=SOURCE_ROOT / "Cavity2D",
            readme_content=_make_readme("Cavity2D", "2D lid-driven cavity benchmark (LBM on D2Q9)"),
        ),
        "Cavity2D_revision": ProjectSpec(
            name="Cavity2D_revision",
            description="Revised 2D lid-driven cavity case with annotated sources",
            source_dir=SOURCE_ROOT / "Cavity2D_revision",
            readme_content=_make_readme("Cavity2D_revision", "Revised 2D lid-driven cavity case with annotated sources"),
        ),
        "Cavity3D": ProjectSpec(
            name="Cavity3D",
            description="3D cavity benchmark (LBM on D3Q19 lattice)",
            source_dir=SOURCE_ROOT / "Cavity3D",
            readme_content=_make_readme("Cavity3D", "3D cavity benchmark (LBM on D3Q19 lattice)"),
        ),
        "Cavity3D_revision": ProjectSpec(
            name="Cavity3D_revision",
            description="Revised 3D cavity simulation variant",
            source_dir=SOURCE_ROOT / "Cavity3D_revision",
            readme_content=_make_readme("Cavity3D_revision", "Revised 3D cavity simulation variant"),
        ),
        "ChannelFlow": ProjectSpec(
            name="ChannelFlow",
            description="3D channel flow with immersed particles (AMR LBM)",
            source_dir=SOURCE_ROOT / "ChannelFlow",
            readme_content=_make_readme(
                "ChannelFlow",
                "3D channel flow with immersed particles (AMR LBM)",
            ),
        ),
        "ChannelFlowtest": ProjectSpec(
            name="ChannelFlowtest",
            description="Channel flow variant for testing purposes",
            source_dir=SOURCE_ROOT / "ChannelFlowtest" / "1",
            readme_content=_make_readme("ChannelFlowtest", "Channel flow variant for testing purposes"),
        ),
        "cavity_sphere": ProjectSpec(
            name="cavity_sphere",
            description="Immersed sphere simulation inside a cavity using AMR LBM",
            source_dir=SOURCE_ROOT / "cavity_sphere",
            readme_content=_make_readme("cavity_sphere", "Immersed sphere simulation inside a cavity using AMR LBM"),
        ),
        "HelloWord_C": ProjectSpec(
            name="HelloWord_C",
            description="Hello world style AMReX learning example",
            source_dir=SOURCE_ROOT / "HelloWord_C",
            readme_content=_make_readme("HelloWord_C", "Hello world style AMReX learning example"),
        ),
        "test20": ProjectSpec(
            name="test20",
            description="Channel flow exploration variant",
            source_dir=SOURCE_ROOT / "test20",
            readme_content=_make_readme("test20", "Channel flow exploration variant"),
        ),
        "test19": ProjectSpec(
            name="test19",
            description="Channel flow long-duration run (test19)",
            source_dir=SOURCE_ROOT / "test19",
            readme_content=_make_readme("test19", "Channel flow long-duration run (test19)"),
        ),
        "test1": ProjectSpec(
            name="test1",
            description="Channel flow exploratory case (test1)",
            source_dir=SOURCE_ROOT / "test1",
            readme_content=_make_readme("test1", "Channel flow exploratory case (test1)"),
        ),
    }


def parse_args(project_names: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--project",
        action="append",
        choices=sorted(project_names),
        help="Only synchronize the specified project (can be repeated)",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Delete the existing target directory before copying",
    )
    parser.add_argument(
        "--extra-exclude",
        action="append",
        default=[],
        metavar="PATTERN",
        help="Additional glob pattern to exclude (can be repeated)",
    )
    return parser.parse_args()


def main() -> None:
    specs = build_specs()
    args = parse_args(specs.keys())
    selected = args.project or list(specs.keys())

    for name in selected:
        spec = specs[name]
        if not spec.source_dir.exists():
            print(f"! Source directory does not exist: {spec.source_dir}")
            continue
        spec.sync(clean=args.clean, extra_exclude=args.extra_exclude)


if __name__ == "__main__":
    main()
