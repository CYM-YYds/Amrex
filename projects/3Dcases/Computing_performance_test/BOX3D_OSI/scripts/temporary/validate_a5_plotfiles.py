#!/usr/bin/env python3
"""复核本次 A/B PlotFile 完整性与可读性；保留用于复现，不再需要复现时可删除。"""
import argparse
import gc
import json
import math
from pathlib import Path
import re
import socket


BOX = r'\(\(([-\d,]+)\) \(([-\d,]+)\) \(([-\d,]+)\)\)'


def inspect_files(path, names, step):
    lines = (path / 'Header').read_text().splitlines()
    count = int(lines[1])
    assert lines[2:2+count] == names, (path, lines[2:2+count])
    assert int(lines[2+count]) == 3
    assert float(lines[3+count]) == step/128
    assert int(lines[4+count]) == 2
    assert list(map(float, lines[5+count].split())) == [0, 0, 0]
    assert list(map(float, lines[6+count].split())) == [1, 1, 1]
    prefixes = re.findall(r'Level_\d+/Cell', '\n'.join(lines))
    assert prefixes == ['Level_0/Cell', 'Level_1/Cell', 'Level_2/Cell']
    fabs = 0
    precisions = set()
    for prefix in prefixes:
        header = (path / (prefix+'_H')).read_text()
        records = re.findall(r'FabOnDisk: (\S+) (\d+)', header)
        boxes = re.findall(BOX, header)
        assert records and len(records) == len(boxes), path / prefix
        for filename, offset in records:
            binary = path / Path(prefix).parent / filename
            with binary.open('rb') as stream:
                stream.seek(int(offset))
                fab = stream.readline().decode('ascii').strip()
                # 第一组的 8 是格式描述项数，第二组才是实际存储字节数。
                real = re.match(r'FAB \(\(\d+,\s*\([^)]*\)\),\((\d+),', fab)
                box = re.search(BOX + r' (\d+)$', fab)
                assert real and int(real[1]) in (4, 8) and box, (binary, offset, fab)
                assert int(box[4]) == count
                lo, hi = [tuple(map(int, box[i].split(','))) for i in (1, 2)]
                values = math.prod(b-a+1 for a, b in zip(lo, hi))*count
                assert binary.stat().st_size >= stream.tell()+values*int(real[1]), binary
                precisions.add(int(real[1]))
            fabs += 1
    return dict(path=str(path), step=step, time=step/128, arrays=names,
                finest_level=2, fabs=fabs, bytes_per_real=sorted(precisions))


def inspect_reader(path, names):
    # 常规中心线入口仅处理速度；这里还需要逐层检查原生涡量数组。
    from paraview import simple as pvs
    import numpy as np
    from vtkmodules.util.numpy_support import vtk_to_numpy
    # 与现有中心线入口一致，读取器参数使用 PlotFile 目录。
    reader = pvs.AMReXBoxLibGridReader(FileNames=[str(path)])
    reader.Level = 2
    reader.CellArrayStatus = names
    reader.UpdatePipeline()
    data = reader.GetClientSideObject().GetOutputDataObject(0)
    assert data.GetNumberOfLevels() == 3
    bounds = list(reader.GetDataInformation().GetBounds())
    assert all(abs(a-b) < 1e-12 for a, b in zip(bounds, [0, 1, 0, 1, 0, 1]))
    ranges = {name: [math.inf, -math.inf] for name in names}
    counts = []
    for level in range(3):
        cells = 0
        for index in range(data.GetNumberOfDataSets(level)):
            block = data.GetDataSet(level, index)
            assert block is not None
            cells += block.GetNumberOfCells()
            for name in names:
                array = block.GetCellData().GetArray(name)
                assert array is not None and array.GetNumberOfTuples() > 0, (level, index, name)
                values = vtk_to_numpy(array)
                assert np.isfinite(values).all(), (path, level, index, name)
                ranges[name][0] = min(ranges[name][0], float(values.min()))
                ranges[name][1] = max(ranges[name][1], float(values.max()))
        assert cells > 0, level
        counts.append(cells)
    pvs.Delete(reader)
    del data, reader, block, array, values
    gc.collect()
    return dict(bounds=bounds, cells_per_level=counts, finite_ranges=ranges)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--cases', nargs='+', default=['A', 'B'])
    parser.add_argument('--steps', nargs='+', type=int, default=[3200, 6400, 9600, 12800])
    parser.add_argument('--read', action='store_true')
    args = parser.parse_args()
    records = []
    for name in args.cases:
        for step in args.steps:
            for prefix, arrays in [('plt_', ['ux', 'uy', 'uz']),
                                   ('plt_vort_', ['Q', 'Vorticity'])]:
                path = args.run.resolve() / name / f'{prefix}{step:06d}'
                item = inspect_files(path, arrays, step)
                item['case'] = name
                if args.read:
                    item['reader'] = inspect_reader(path, arrays)
                records.append(item)
                print('PASS', path, flush=True)
    result = dict(host=socket.gethostname(), records=records)
    if args.read:
        from paraview.simple import GetParaViewVersion
        result['paraview_version'] = str(GetParaViewVersion())
    destination = args.run / ('plotfiles_reader_validation.json' if args.read else 'plotfiles_layout_validation.json')
    destination.write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__':
    main()
