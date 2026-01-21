# IDF多球体重构 - 修改代码参考

## 文件位置
`c:\Users\Caiyimin\OneDrive - whut.edu.cn\code\C++\Amrex\projects\Cylindertest\Cylinder3D\src\AmrCoreLBM.cpp`

## 修改的5个IDF函数

### 函数1: ApplyIDF(int lev) - 主入口 [L1704]

```cpp
void AmrCoreLBM::ApplyIDF(int lev) {
    // 仅在最细层执行
    if (lev != finest_level) {
        return;
    }

    // 对每个球体执行独立的IDF计算
    for (int p = 0; p < particle_num; p++) {
        if (!particles[p])
            continue;

        // Step 1: 构建活跃欧拉点集合（针对第p个球体）
        BuildActiveEulerSet(lev, p);

        // Step 2: 插值欧拉速度到拉格朗日点（包括全局汇总）
        IDF_InterpolateEulerToLag(lev, p);

        // Step 3: 组装该球体的全局矩阵 A = D_I × D_E
        IDF_AssembleMatrix(lev, p);

        // Step 4: 求解线性系统得到拉格朗日力
        IDF_SolveSystem(lev, p);

        // Step 5: 传播拉格朗日力到欧拉网格
        IDF_SpreadLagToEuler(lev, p);
    }

    // 最后统一处理所有球体的力场通信
    SumForce(lev);
}
```

**关键改动**:
- 添加 `for (int p = 0; p < particle_num; p++)` 外层循环
- 为每个IDF函数传递 `particle_idx = p` 参数
- 最后统一调用 `SumForce(lev)` 进行Ghost区域通信

---

### 函数2: BuildActiveEulerSet(int lev, int particle_idx) [L1459]

**函数签名改动**:
```cpp
// 旧版本
void AmrCoreLBM::BuildActiveEulerSet(int lev) { ... }

// 新版本
void AmrCoreLBM::BuildActiveEulerSet(int lev, int particle_idx) { ... }
```

**主要改动**:
1. **参数添加**: 增加 `int particle_idx` 以标识球体
2. **局部变量化**: 将所有全局成员变量改为局部变量
3. **球体选择**: `auto& pc = particles[particle_idx];`

**数据结构局部化**:
```cpp
// 原来是全局成员变量
// int idf_local_NL;
// int idf_NL_global;
// std::vector<int> idf_all_NL;
// std::vector<Real> idf_lag_pos_global;
// std::vector<IntVect> idf_active_euler_nodes_global;
// std::map<IntVect, int> idf_euler_index_map_global;

// 现在都是局部变量
std::vector<Real> local_lag_pos;
std::vector<int> local_lag_ids;
int local_NL = pc->IDF_CollectParticleData(lev, local_lag_pos, local_lag_ids);

int nprocs = ParallelDescriptor::NProcs();
std::vector<int> all_NL(nprocs, 0);
MPI_Allgather(&local_NL, 1, MPI_INT, all_NL.data(), 1, MPI_INT,
              ParallelDescriptor::Communicator());

int NL_global = 0;
int local_offset = 0;
for (int i = 0; i < nprocs; ++i) {
    if (i < ParallelDescriptor::MyProc()) {
        local_offset += all_NL[i];
    }
    NL_global += all_NL[i];
}

// ... 继续为所有其他变量使用局部变量
std::vector<Real> lag_pos_global(NL_global * 3);
std::vector<IntVect> active_euler_nodes_global;
std::map<IntVect, int> euler_index_map_global;
```

**工作流程**:
```
Step 1: 收集本地拉格朗日点 → MPI汇总粒子数
Step 2: MPI广播拉格朗日点位置 → 按ID重排
Step 3: 本地计算欧拉点邻域 → MPI汇总欧拉点 → 去重
Step 4: 构建欧拉点映射表 (IntVect → index)
```

---

### 函数3: IDF_InterpolateEulerToLag(int lev, int particle_idx) [L1734]

**函数签名**:
```cpp
void AmrCoreLBM::IDF_InterpolateEulerToLag(int lev, int particle_idx)
```

**主要改动**:
1. 独立处理第 `particle_idx` 个球体
2. 所有插值结果存储为局部变量
3. 完整的MPI汇总流程

**局部变量**:
```cpp
std::vector<Real> local_interp_ux, local_interp_uy, local_interp_uz, local_interp_rho;
std::vector<int> local_interp_ids;

pc->IDF_ReadInterpResults(lev, local_interp_ux, local_interp_uy, local_interp_uz, 
                           local_interp_rho, &local_interp_ids);

// MPI汇总后的全局向量
std::vector<Real> unsorted_interp_ux(NL_global), ...;
std::vector<int> all_interp_ids(NL_global);

// 按粒子ID重排后的最终向量
std::vector<Real> interp_u_x(NL_global), interp_u_y(NL_global), 
                   interp_u_z(NL_global), interp_rho(NL_global);

// RHS向量（后续求解使用）
std::vector<Real> rhs_x(NL_global), rhs_y(NL_global), rhs_z(NL_global);
```

**工作步骤**:
```
1. 收集本地插值数据
2. MPI汇总到全局
3. 按粒子ID重排
4. 计算RHS = 插值速度 - 目标速度
5. （目标速度对于静止球体为0）
```

---

### 函数4: IDF_AssembleMatrix(int lev, int particle_idx) [L1869]

**函数签名**:
```cpp
void AmrCoreLBM::IDF_AssembleMatrix(int lev, int particle_idx)
```

**核心功能**:
- 完整构建该球体的矩阵 A = D_I × D_E
- 计算矩阵逆 A_inv
- 使用局部变量存储所有数据

**局部变量**:
```cpp
std::vector<Real> lag_pos_global(NL_global * 3);        // 拉格朗日点位置
std::vector<IntVect> active_euler_nodes_global;          // 欧拉点集合
std::map<IntVect, int> euler_index_map_global;           // 欧拉点索引

// 稀疏矩阵表示
std::vector<std::vector<std::pair<int, Real>>> DI_rows(NL_global);    // D_I 行表示
std::vector<std::vector<std::pair<int, Real>>> DE_by_euler(NE_global); // D_E 列表示

// 密集矩阵
std::vector<amrex::Real> A_local(NL_global * NL_global, 0.0);         // 矩阵A
std::vector<amrex::Real> A_inv_local(NL_global * NL_global);          // 矩阵逆
```

**矩阵计算**:
```cpp
// D_I[p,e] = delta3p(lx) * delta3p(ly) * delta3p(lz)  (插值权重)
// D_E[e,p] = D_I[p,e]  (对称)
// A[i,j] = sum_e D_I[i,e] * D_E[e,j]

for (int i = 0; i < NL_global; ++i) {
    for (auto const& ie : DI_rows[i]) {
        int eidx = ie.first;
        Real wI = ie.second;
        for (auto const& jw : DE_by_euler[eidx]) {
            int j = jw.first;
            Real wE = jw.second;
            A_local[i * NL_global + j] += wI * wE;
        }
    }
}

// 计算逆矩阵 (高斯-约旦消元)
bool success = computeMatrixInverse(A_local, A_inv_local, NL_global);
```

---

### 函数5: IDF_SolveSystem(int lev, int particle_idx) [L2012]

**函数签名**:
```cpp
void AmrCoreLBM::IDF_SolveSystem(int lev, int particle_idx)
```

**主要改动**:
- 完全独立求解该球体的线性系统
- 重新收集和构建矩阵（确保独立性）
- 使用局部变量存储所有中间数据

**求解步骤**:
```cpp
Step 1: 收集该球体的几何数据
        ├─ local_lag_pos (本进程拉格朗日点)
        ├─ local_lag_ids (粒子ID)
        └─ local_NL (本进程粒子数)

Step 2: MPI汇总插值数据
        ├─ MPI_Allgatherv 汇总插值速度 (ux, uy, uz)
        ├─ MPI_Allgatherv 汇总插值密度 (rho)
        └─ MPI_Allgatherv 汇总粒子ID

Step 3: 按粒子ID重排整理数据
        └─ interp_u_x, interp_u_y, interp_u_z, interp_rho

Step 4: 重新构建矩阵A和A_inv
        ├─ 收集欧拉点位置
        ├─ 构建D_I和D_E稀疏表示
        ├─ 计算A = D_I × D_E
        └─ 计算A_inv

Step 5: 构建RHS向量
        └─ rhs_x/y/z = interp_u_x/y/z - target_u_x/y/z

Step 6: 求解线性系统
        ├─ sol_x = A_inv * rhs_x
        ├─ sol_y = A_inv * rhs_y
        └─ sol_z = A_inv * rhs_z

Step 7: 将解写回粒子属性
        └─ pc->IDF_WriteForceToParticles(...)
```

**关键计算**:
```cpp
// RHS计算
std::vector<Real> target_u_x(NL_global, 0.0);  // 目标速度 = 0（静止边界）
std::vector<Real> target_u_y(NL_global, 0.0);
std::vector<Real> target_u_z(NL_global, 0.0);

std::vector<Real> rhs_x(NL_global), rhs_y(NL_global), rhs_z(NL_global);
for (int i = 0; i < NL_global; ++i) {
    rhs_x[i] = interp_u_x[i] - target_u_x[i];
    rhs_y[i] = interp_u_y[i] - target_u_y[i];
    rhs_z[i] = interp_u_z[i] - target_u_z[i];
}

// 求解
std::vector<Real> sol_x(NL_global), sol_y(NL_global), sol_z(NL_global);
denseMatVec(A_inv_local, rhs_x, sol_x);
denseMatVec(A_inv_local, rhs_y, sol_y);
denseMatVec(A_inv_local, rhs_z, sol_z);

// 写回粒子
pc->IDF_WriteForceToParticles(lev, sol_x, sol_y, sol_z, NL_global);
```

---

### 函数6: IDF_SpreadLagToEuler(int lev, int particle_idx) [L2250]

**函数签名**:
```cpp
void AmrCoreLBM::IDF_SpreadLagToEuler(int lev, int particle_idx)
```

**主要改动**:
- 单独处理第 `particle_idx` 个球体的力传播
- 使用粒子容器接口执行力传播

**实现**:
```cpp
if (particles.size() <= static_cast<size_t>(particle_idx) || !particles[particle_idx])
    return;

auto& pc = particles[particle_idx];

// 检查该球体是否有拉格朗日点
std::vector<Real> local_lag_pos;
std::vector<int> local_lag_ids;
int local_NL = pc->IDF_CollectParticleData(lev, local_lag_pos, local_lag_ids);

if (local_NL == 0) {
    return;
}

// 使用 LagrangeParticleContainer 接口传播力
amrex::MultiFab& force_lev = force[lev];
pc->IDF_SpreadForce(lev, force_lev);
```

---

## 数据流图

```
外层循环 (for each particle_idx)
│
├─► BuildActiveEulerSet(lev, particle_idx)
│   └─ 输出: NL_global, NE_global, 欧拉点映射
│
├─► IDF_InterpolateEulerToLag(lev, particle_idx)
│   ├─ 输入: 拉格朗日点位置
│   └─ 输出: interp_u_x/y/z, interp_rho, rhs_x/y/z
│
├─► IDF_AssembleMatrix(lev, particle_idx)
│   ├─ 输入: 拉格朗日/欧拉点位置
│   └─ 输出: A_matrix, A_inverse
│
├─► IDF_SolveSystem(lev, particle_idx)
│   ├─ 输入: RHS, 矩阵A和A_inv
│   └─ 输出: 拉格朗日力 (sol_x/y/z)
│
└─► IDF_SpreadLagToEuler(lev, particle_idx)
    ├─ 输入: 拉格朗日力
    └─ 输出: 更新欧拉力场
```

---

## 编译检查清单

- [x] 所有5个函数已添加 `int particle_idx` 参数
- [x] BuildActiveEulerSet 使用完全局部变量
- [x] IDF_InterpolateEulerToLag 使用完全局部变量
- [x] IDF_AssembleMatrix 使用完全局部变量
- [x] IDF_SolveSystem 使用完全局部变量
- [x] IDF_SpreadLagToEuler 支持单球体处理
- [x] ApplyIDF 具有外层循环
- [x] 所有MPI操作保留
- [x] 编译通过（在AMReX环境中）

---

## 可选优化方向

1. **矩阵缓存**: 对于静止球体，可缓存A和A_inv
2. **并行化**: 使用OpenMP并行处理多个球体
3. **GPU加速**: 矩阵操作移至GPU
4. **内存优化**: 减少临时变量分配

