# state file generated using paraview version 5.11.1
import paraview
paraview.compatibility.major = 5
paraview.compatibility.minor = 11

#### import the simple module from the paraview
from paraview.simple import *

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
        Delete(GetActiveSource())
        RenderAllViews()
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
    Delete(dataReader)
    del dataReader, calculator1, gradient1, extractBlock1, clip1, cellDatatoPointData1
    
    return True


# 如果直接运行此脚本（非批量模式）
if __name__ == '__main__':
    # 默认参数
    INPUT_DATA_PATH = '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest/Cylinder2D_BTDF1/data/cyl_lev3_Re40_23200'
    OUTPUT_CSV_PATH = '/home/huazkjdxmrsgjzdsyshi/whcs-share18/caiyimin/learnamerx/Amrex/projects/Cylindertest/output_data.csv'
    AMR_LEVEL = 2
    
    process_and_export(INPUT_DATA_PATH, OUTPUT_CSV_PATH, AMR_LEVEL)