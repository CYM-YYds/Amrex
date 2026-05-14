# 批量处理脚本 - 独立版本（无需加载外部文件）
import paraview
paraview.compatibility.major = 5
paraview.compatibility.minor = 11

from paraview.simple import *
import os

# ================================================================
# 批量处理参数配置区域 - 可手动修改
# ================================================================

# 基础路径
BASE_PROJECT_DIR = '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest'
BASE_OUTPUT_DIR = '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest/output'

# 数据文件夹的固定部分
DATA_SUBFOLDER = 'data/cyl_lev3_Re40_23200'  # 每个项目下的数据路径

# AMR层级设置
AMR_LEVEL = 2

# 要处理的项目文件夹列表（Cylinder2D_BTDF1, Cylinder2D_BTDF2 等）
PROJECT_FOLDERS = [
    'Cylinder2D_IDF',
]

# ================================================================

def process_and_export(input_path, output_path, amr_level=2):
    """
    处理AMReX数据并导出到CSV
    
    参数:
        input_path: 输入数据文件夹路径
        output_path: 输出CSV文件路径
        amr_level: AMR网格层级，默认为2
    """
    print(f'开始处理: {input_path}')
    
    # 清除之前的数据
    try:
        sources = GetSources()
        for source in sources.values():
            Delete(source)
    except:
        pass
    
    # create a new 'AMReX/BoxLib Grid Reader'
    dataReader = AMReXBoxLibGridReader(registrationName='DataReader', FileNames=[input_path])
    dataReader.Level = amr_level
    dataReader.CellArrayStatus = ['ux', 'uy']
    
    # create a new 'Calculator'
    calculator1 = Calculator(registrationName='Calculator1', Input=dataReader)
    calculator1.AttributeType = 'Cell Data'
    calculator1.ResultArrayName = 'Velocity'
    calculator1.Function = 'ux*iHat+uy*jHat'
    
    # create a new 'Gradient'
    gradient1 = Gradient(registrationName='Gradient1', Input=calculator1)
    gradient1.ScalarArray = ['CELLS', 'Velocity']
    gradient1.ComputeGradient = 0
    gradient1.ComputeVorticity = 1
    
    # create a new 'Extract Block'
    extractBlock1 = ExtractBlock(registrationName='ExtractBlock1', Input=gradient1)
    extractBlock1.Selectors = ['/Root']
    
    # create a new 'Clip'
    clip1 = Clip(registrationName='Clip1', Input=extractBlock1)
    clip1.ClipType = 'Box'
    clip1.HyperTreeGridClipper = 'Plane'
    clip1.Scalars = ['CELLS', 'ux']
    clip1.Value = 0.01597193487993279
    
    # init the 'Box' selected for 'ClipType'
    clip1.ClipType.Position = [0.475, 0.475, -0.025]
    clip1.ClipType.Length = [0.05, 0.05, 0.05]
    
    # init the 'Plane' selected for 'HyperTreeGridClipper'
    clip1.HyperTreeGridClipper.Origin = [0.635, 0.5, 0.0]
    
    # create a new 'Cell Data to Point Data'
    cellDatatoPointData1 = CellDatatoPointData(registrationName='CellDatatoPointData1', Input=clip1)
    cellDatatoPointData1.CellDataArraytoprocess = ['Velocity', 'Vorticity', 'ux', 'uy', 'vtkGhostType']
    
    # 导出数据到CSV文件
    SaveData(output_path, 
             proxy=cellDatatoPointData1,
             PointDataArrays=['ux', 'uy'])
    
    print(f'✓ 数据已成功导出到: {output_path}')
    
    # 清理资源
    try:
        Delete(dataReader)
        del calculator1, gradient1, extractBlock1, clip1, cellDatatoPointData1
    except:
        pass
    
    return True


def batch_process():
    """批量处理主函数"""
    print('=' * 60)
    print('开始批量处理')
    print('=' * 60)
    print(f'项目基础目录: {BASE_PROJECT_DIR}')
    print(f'输出目录: {BASE_OUTPUT_DIR}')
    print(f'数据子文件夹: {DATA_SUBFOLDER}')
    print(f'AMR层级: {AMR_LEVEL}')
    print(f'待处理项目数量: {len(PROJECT_FOLDERS)}')
    print('=' * 60)
    
    # 检查输出目录是否存在
    if not os.path.exists(BASE_OUTPUT_DIR):
        print(f'\n✗ 错误：输出目录不存在！')
        print(f'  目录: {BASE_OUTPUT_DIR}')
        print(f'  请先手动创建此目录：')
        print(f'  $ mkdir -p {BASE_OUTPUT_DIR}')
        print('=' * 60)
        return
    
    print(f'\n✓ 输出目录存在: {BASE_OUTPUT_DIR}')
    print('=' * 60)
    
    success_count = 0
    fail_count = 0
    
    for i, project_name in enumerate(PROJECT_FOLDERS, 1):
        print(f'\n[{i}/{len(PROJECT_FOLDERS)}] 处理项目: {project_name}')
        
        # 构建完整路径
        # 输入: /path/to/Cylindertest/Cylinder2D_BTDF1/data/cyl_lev3_Re40_23200
        input_path = f'{BASE_PROJECT_DIR}/{project_name}/{DATA_SUBFOLDER}'
        
        # 输出: /path/to/output/Cylinder2D_BTDF1.csv
        output_path = f'{BASE_OUTPUT_DIR}/{project_name}.csv'
        
        print(f'  输入: {input_path}')
        print(f'  输出: {output_path}')
        
        try:
            # 调用处理函数
            process_and_export(input_path, output_path, AMR_LEVEL)
            success_count += 1
        except Exception as e:
            print(f'✗ 处理失败: {project_name}')
            print(f'  错误信息: {str(e)}')
            fail_count += 1
    
    # 输出统计信息
    print('\n' + '=' * 60)
    print('批量处理完成')
    print('=' * 60)
    print(f'成功: {success_count} 个')
    print(f'失败: {fail_count} 个')
    print(f'总计: {len(PROJECT_FOLDERS)} 个')
    print('=' * 60)


# 执行批量处理
if __name__ == '__main__':
    batch_process()

# 当作为脚本在 ParaView Python Shell 中运行时
batch_process()
