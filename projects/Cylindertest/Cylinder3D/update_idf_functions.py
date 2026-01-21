#!/usr/bin/env python3
"""
更新AmrCoreLBM.cpp中的5个IDF函数，将全局IDF变量访问改为通过IDFData结构体成员访问
"""
import re

def update_idf_functions(file_path):
    """更新文件中的IDF函数"""
    
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    original_content = content
    
    # Step 1: 更新函数签名（添加IDFData& idf_data参数）
    print("Step 1: 更新函数签名...")
    
    # 函数签名替换列表
    signature_replacements = [
        (r'void AmrCoreLBM::BuildActiveEulerSet\(int lev, int particle_idx\)',
         r'void AmrCoreLBM::BuildActiveEulerSet(int lev, int particle_idx, IDFData& idf_data)'),
        (r'void AmrCoreLBM::IDF_InterpolateEulerToLag\(int lev, int particle_idx\)',
         r'void AmrCoreLBM::IDF_InterpolateEulerToLag(int lev, int particle_idx, IDFData& idf_data)'),
        (r'void AmrCoreLBM::IDF_AssembleMatrix\(int lev, int particle_idx\)',
         r'void AmrCoreLBM::IDF_AssembleMatrix(int lev, int particle_idx, IDFData& idf_data)'),
        (r'void AmrCoreLBM::IDF_SolveSystem\(int lev, int particle_idx\)',
         r'void AmrCoreLBM::IDF_SolveSystem(int lev, int particle_idx, IDFData& idf_data)'),
        (r'void AmrCoreLBM::IDF_SpreadLagToEuler\(int lev, int particle_idx\)',
         r'void AmrCoreLBM::IDF_SpreadLagToEuler(int lev, int particle_idx, IDFData& idf_data)'),
    ]
    
    for old_sig, new_sig in signature_replacements:
        if re.search(old_sig, content):
            print(f"  更新函数签名: {old_sig[:50]}...")
            content = re.sub(old_sig, new_sig, content)
        else:
            print(f"  警告: 未找到函数签名: {old_sig[:50]}...")
    
    # Step 2: 在函数体内将idf_xxx替换为idf_data.idf_xxx
    print("\nStep 2: 更新函数体内的变量访问...")
    
    # 需要替换的IDF变量列表
    idf_variables = [
        'idf_local_NL',
        'idf_all_NL',
        'idf_NL_global',
        'idf_local_offset',
        'idf_lag_pos_global',
        'idf_active_euler_nodes_global',
        'idf_euler_index_map_global',
        'idf_NE_global',
        'idf_interp_u_x',
        'idf_interp_u_y',
        'idf_interp_u_z',
        'idf_interp_rho',
        'idf_target_u_x',
        'idf_target_u_y',
        'idf_target_u_z',
        'idf_rhs_x',
        'idf_rhs_y',
        'idf_rhs_z',
        'idf_sol_x',
        'idf_sol_y',
        'idf_sol_z',
        'idf_A',
        'idf_A_inv',
        'idf_DI_rows',
        'idf_DE_cols',
        'idf_geometry_built',
        'idf_local_count_valid',
        'idf_matrix_built',
        'idf_inverse_built',
    ]
    
    # 对每个变量执行替换
    # 使用word boundary确保只替换完整的变量名
    for var in idf_variables:
        # 只替换不是idf_data.前缀的情况
        # 负向前瞻：确保前面不是idf_data.
        pattern = r'(?<!idf_data\.)(?<!\.)\b' + re.escape(var) + r'\b'
        replacement = f'idf_data.{var}'
        
        # 计算替换次数
        matches = re.findall(pattern, content)
        if matches:
            print(f"  替换 {var}: {len(matches)} 次")
            content = re.sub(pattern, replacement, content)
    
    # Step 3: 保存文件
    if content != original_content:
        print(f"\n保存修改到文件: {file_path}")
        with open(file_path, 'w', encoding='utf-8') as f:
            f.write(content)
        print("完成！")
        return True
    else:
        print("\n未检测到任何更改")
        return False

if __name__ == '__main__':
    file_path = r'c:\Users\Caiyimin\OneDrive - whut.edu.cn\code\C++\Amrex\projects\Cylindertest\Cylinder3D\src\AmrCoreLBM.cpp'
    update_idf_functions(file_path)
