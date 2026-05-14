# 批量L2范式误差计算脚本
# 用于批量计算多个CSV文件与参考解之间的L2误差

import numpy as np
import pandas as pd
import os

# ================================================================
# 参数配置区域 - 可手动修改
# ================================================================

# 参考解文件
REFERENCE_FILE = 'D:/output/Cylinder2D_IDF.csv'

# 输出目录
OUTPUT_DIR = 'D:/output'

# 要比较的文件列表（相对于输出目录）
COMPARE_FILES = [
    'Cylinder2D_BTDF1.csv',
    'Cylinder2D_BTDF2.csv',
    'Cylinder2D_BTDF3.csv',
    'Cylinder2D_BTDF4.csv',
    'Cylinder2D_BTDF5.csv',
    'Cylinder2D_BTDF10.csv',
    'Cylinder2D_BTDF15.csv',
    'Cylinder2D_BTDF20.csv',
    'Cylinder2D_DF1.csv',
    'Cylinder2D_DF2.csv',
    'Cylinder2D_DF3.csv',
    'Cylinder2D_DF4.csv',
    'Cylinder2D_DF5.csv',
    'Cylinder2D_DF10.csv',
    'Cylinder2D_DF15.csv',
    'Cylinder2D_DF20.csv',
]

# 或者自动查找所有CSV文件（除了参考文件）
AUTO_FIND_FILES = True  # 设置为True自动查找所有CSV

# 输出结果文件
RESULT_OUTPUT_FILE = 'D:/output/l2_errors.csv'

# ================================================================


def calculate_l2_error_single(file_ref, file_comp):
    """
    计算单个文件对的L2误差
    """
    try:
        # 读取数据
        df_ref = pd.read_csv(file_ref)
        df_comp = pd.read_csv(file_comp)
        
        # 取较小的行数
        n = min(len(df_ref), len(df_comp))
        
        # 提取数据
        ux_ref = df_ref['ux'].values[:n]
        uy_ref = df_ref['uy'].values[:n]
        ux_comp = df_comp['ux'].values[:n]
        uy_comp = df_comp['uy'].values[:n]
        
        # 计算L2误差
        l2_ux = np.sqrt(np.mean((ux_comp - ux_ref) ** 2))
        l2_uy = np.sqrt(np.mean((uy_comp - uy_ref) ** 2))
        l2_velocity = np.sqrt(np.mean((ux_comp - ux_ref) ** 2 + (uy_comp - uy_ref) ** 2))
        
        # 计算相对误差
        ux_ref_norm = np.sqrt(np.mean(ux_ref ** 2))
        uy_ref_norm = np.sqrt(np.mean(uy_ref ** 2))
        velocity_ref_norm = np.sqrt(np.mean(ux_ref ** 2 + uy_ref ** 2))
        
        return {
            'n': n,
            'l2_ux': l2_ux,
            'l2_uy': l2_uy,
            'l2_velocity': l2_velocity,
            'relative_l2_ux': l2_ux / ux_ref_norm if ux_ref_norm > 1e-15 else 0,
            'relative_l2_uy': l2_uy / uy_ref_norm if uy_ref_norm > 1e-15 else 0,
            'relative_l2_velocity': l2_velocity / velocity_ref_norm if velocity_ref_norm > 1e-15 else 0
        }
    except Exception as e:
        print(f"  ✗ 错误: {str(e)}")
        return None


def batch_calculate_l2_errors():
    """
    批量计算L2误差
    """
    print("=" * 70)
    print("批量L2范式误差计算工具")
    print("=" * 70)
    
    # 检查参考文件
    if not os.path.exists(REFERENCE_FILE):
        print(f"\n✗ 参考文件不存在: {REFERENCE_FILE}")
        return
    
    print(f"\n参考文件: {REFERENCE_FILE}")
    
    # 确定要比较的文件列表
    files_to_compare = []
    
    if AUTO_FIND_FILES:
        print(f"\n自动查找输出目录中的CSV文件...")
        print(f"目录: {OUTPUT_DIR}")
        
        if os.path.exists(OUTPUT_DIR):
            all_files = os.listdir(OUTPUT_DIR)
            csv_files = [f for f in all_files if f.endswith('.csv')]
            
            # 排除参考文件本身
            ref_filename = os.path.basename(REFERENCE_FILE)
            files_to_compare = [os.path.join(OUTPUT_DIR, f) for f in csv_files if f != ref_filename]
            
            print(f"  找到 {len(files_to_compare)} 个CSV文件")
        else:
            print(f"  ✗ 目录不存在")
            return
    else:
        files_to_compare = [os.path.join(OUTPUT_DIR, f) for f in COMPARE_FILES]
    
    if not files_to_compare:
        print("\n✗ 没有找到要比较的文件")
        return
    
    # 存储所有结果
    all_results = []
    
    print("\n" + "=" * 70)
    print("开始计算...")
    print("=" * 70)
    
    # 计算每个文件的L2误差
    for i, file_comp in enumerate(files_to_compare, 1):
        filename = os.path.basename(file_comp)
        print(f"\n[{i}/{len(files_to_compare)}] {filename}")
        
        if not os.path.exists(file_comp):
            print(f"  ✗ 文件不存在")
            continue
        
        result = calculate_l2_error_single(REFERENCE_FILE, file_comp)
        
        if result:
            print(f"  ✓ L2(ux)={result['l2_ux']:.6e}, L2(uy)={result['l2_uy']:.6e}")
            
            all_results.append({
                'File': filename,
                'Points': result['n'],
                'L2_ux': result['l2_ux'],
                'L2_uy': result['l2_uy'],
                'L2_velocity': result['l2_velocity'],
                'Relative_L2_ux_%': result['relative_l2_ux'] * 100,
                'Relative_L2_uy_%': result['relative_l2_uy'] * 100,
                'Relative_L2_velocity_%': result['relative_l2_velocity'] * 100
            })
    
    # 输出汇总结果
    if all_results:
        print("\n" + "=" * 70)
        print("汇总结果")
        print("=" * 70)
        
        # 创建DataFrame
        df_results = pd.DataFrame(all_results)
        
        # 显示表格
        print("\n" + df_results.to_string(index=False))
        
        # 保存到CSV
        df_results.to_csv(RESULT_OUTPUT_FILE, index=False)
        print(f"\n✓ 结果已保存到: {RESULT_OUTPUT_FILE}")
        
        print("\n" + "=" * 70)
        print(f"共计算 {len(all_results)} 个文件")
        print("=" * 70)
    else:
        print("\n✗ 没有成功计算任何文件的误差")


# 主函数
if __name__ == '__main__':
    batch_calculate_l2_errors()
