#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace std;

typedef double dfloat;

#define Q 9   // 离散速度方向数量(D2Q9)
#define NX 40 // x方向格点数
#define NY 40 // y方向格点数
#define LX (NX - 1)
#define LY (NY - 1)

#define c 1.0               // 格子速度
#define cs (c / sqrt(3.0))  // 格子声速
#define cs_square (cs * cs) // 声速平方
#define dx 1.0              // 空间步长
#define dt (dx / c)         // 时间步长

// 可配置的输出目录（默认在可执行文件同级的 results 目录）
std::string output_dir = "poiseuille_IBM";
// 矩阵乘法: C = A * B (A: M×K, B: K×N, C: M×N)
void matrixMultiply(const vector<vector<dfloat>> &A,
                    const vector<vector<dfloat>> &B,
                    vector<vector<dfloat>> &C) {
  int M = A.size();
  int K = A[0].size();
  int K2 = B.size();
  int N = B[0].size();

  // 检查维度匹配
  if (K != K2) {
    cout << "错误: 矩阵乘法维度不匹配！A: " << M << "×" << K << ", B: " << K2
         << "×" << N << endl;
    throw runtime_error("矩阵乘法维度不匹配");
  }

  C.assign(M, vector<dfloat>(N, 0.0));
  for (int i = 0; i < M; ++i) {
    for (int j = 0; j < N; ++j) {
      for (int k = 0; k < K; ++k) {
        C[i][j] += A[i][k] * B[k][j];
      }
    }
  }
}

// 共轭梯度法求解 Ax=b（适用于对称正定矩阵，对奇异性更鲁棒）
vector<dfloat> solveCG(const vector<vector<dfloat>> &A, const vector<dfloat> &b,
                       int max_iter = 10000, dfloat tol = 1e-10) {
  int N = A.size();
  vector<dfloat> x(N, 0.0); // 初始解为0
  vector<dfloat> r(b);      // 初始残差 r = b - Ax = b
  vector<dfloat> p(r);      // 初始搜索方向
  dfloat rs_old = 0.0;

  // 计算初始残差范数
  for (int i = 0; i < N; ++i) {
    rs_old += r[i] * r[i];
  }

  cout << "CG求解开始，初始残差: " << scientific << sqrt(rs_old) << endl;

  for (int iter = 0; iter < max_iter; ++iter) {
    // 计算 Ap
    vector<dfloat> Ap(N, 0.0);
    for (int i = 0; i < N; ++i) {
      for (int j = 0; j < N; ++j) {
        Ap[i] += A[i][j] * p[j];
      }
    }

    // 计算步长 alpha = (r^T * r) / (p^T * Ap)
    dfloat pAp = 0.0;
    for (int i = 0; i < N; ++i) {
      pAp += p[i] * Ap[i];
    }

    if (fabs(pAp) < 1e-20) {
      cout << "警告: p^T*Ap接近零，迭代停止于第 " << iter << " 步" << endl;
      break;
    }

    dfloat alpha = rs_old / pAp;

    // 更新解 x = x + alpha * p
    for (int i = 0; i < N; ++i) {
      x[i] += alpha * p[i];
    }

    // 更新残差 r = r - alpha * Ap
    for (int i = 0; i < N; ++i) {
      r[i] -= alpha * Ap[i];
    }

    // 计算新的残差范数
    dfloat rs_new = 0.0;
    for (int i = 0; i < N; ++i) {
      rs_new += r[i] * r[i];
    }

    // 检查收敛
    if (sqrt(rs_new) < tol) {
      cout << "CG求解收敛于第 " << iter + 1 << " 步，残差: " << scientific
           << sqrt(rs_new) << endl;
      return x;
    }

    // 每100步输出一次进度
    if ((iter + 1) % 100 == 0) {
      cout << "  第 " << iter + 1 << " 步，残差: " << scientific << sqrt(rs_new)
           << endl;
    }

    // 更新搜索方向 p = r + beta * p
    dfloat beta = rs_new / rs_old;
    for (int i = 0; i < N; ++i) {
      p[i] = r[i] + beta * p[i];
    }

    rs_old = rs_new;
  }

  cout << "警告: CG未在 " << max_iter << " 步内收敛" << endl;
  return x;
}

// LU分解解线性方程组 Ax=b，返回x（带部分主元选择和改进的数值稳定性）
vector<dfloat> solveLinearLU(const vector<vector<dfloat>> &A,
                             const vector<dfloat> &b) {
  int N = A.size();
  vector<vector<dfloat>> LU(A);
  vector<int> piv(N);
  for (int i = 0; i < N; ++i)
    piv[i] = i;

  // 计算矩阵的最大元素作为缩放参考
  dfloat max_elem = 0.0;
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      max_elem = max(max_elem, fabs(LU[i][j]));
    }
  }

  // 动态设置奇异性阈值（相对于矩阵最大元素）
  dfloat tol = max_elem * 1e-10;

  // LU分解（带部分主元选择）
  for (int k = 0; k < N; ++k) {
    // 在第k列的第k行及以下寻找最大元素
    dfloat maxA = 0.0;
    int imax = k;
    for (int i = k; i < N; ++i) {
      dfloat absA = fabs(LU[i][k]);
      if (absA > maxA) {
        maxA = absA;
        imax = i;
      }
    }

    // 检查主元是否太小
    if (maxA < tol) {
      cout << "警告: 在第 " << k << " 步遇到极小主元: " << scientific << maxA
           << endl;
      cout << "矩阵最大元素: " << max_elem << ", 阈值: " << tol << endl;
      throw runtime_error("LU分解失败: 矩阵奇异或接近奇异");
    }

    // 行交换（如果需要）
    if (imax != k) {
      swap(LU[k], LU[imax]);
      swap(piv[k], piv[imax]);
    }

    // 消元
    for (int i = k + 1; i < N; ++i) {
      LU[i][k] /= LU[k][k];
      for (int j = k + 1; j < N; ++j) {
        LU[i][j] -= LU[i][k] * LU[k][j];
      }
    }
  }

  // 前向替换 Ly = Pb
  vector<dfloat> y(N);
  for (int i = 0; i < N; ++i) {
    y[i] = b[piv[i]];
    for (int j = 0; j < i; ++j)
      y[i] -= LU[i][j] * y[j];
  }

  // 回代 Ux = y
  vector<dfloat> x(N);
  for (int i = N - 1; i >= 0; --i) {
    x[i] = y[i];
    for (int j = i + 1; j < N; ++j)
      x[i] -= LU[i][j] * x[j];
    x[i] /= LU[i][i];
  }

  return x;
}

// 输出矩阵 A 到文件
void outputMatrixA(const vector<vector<dfloat>> &A, int step) {
  int N = A.size();

  // 输出稠密格式
  std::filesystem::path p(output_dir);
  p /= std::string("matrix_A_") + std::to_string(step) + std::string(".dat");
  ofstream out(p.string());

  out << "# 矩阵 A = D_I * D_E - 步数: " << step << endl;
  out << "# 矩阵大小: " << N << " × " << N << endl;
  out << "# A * drs = b, 其中 b = [h, h, ..., h]^T" << endl;
  out << "#" << endl;
  out << "# 格式: 稠密矩阵，每行一个拉格朗日点" << endl;
  out << "# 矩阵数据开始" << endl;

  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      out << setprecision(12) << scientific << A[i][j];
      if (j < N - 1)
        out << "\t";
    }
    out << endl;
  }

  out.close();
  cout << "矩阵 A 已写入文件: " << p.string() << endl;

  // 输出稀疏格式（只保存非零元素）
  std::filesystem::path p_sparse(output_dir);
  p_sparse /= std::string("matrix_A_sparse_") + std::to_string(step) +
              std::string(".dat");
  ofstream out_sparse(p_sparse.string());

  out_sparse << "# 矩阵 A (稀疏格式) - 步数: " << step << endl;
  out_sparse << "# 格式: i j A[i][j] (只包含非零元素)" << endl;
  out_sparse << "# i\tj\tA[i][j]" << endl;

  int nonzero_count = 0;
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      if (fabs(A[i][j]) > 1e-15) {
        out_sparse << i << "\t" << j << "\t" << setprecision(12) << scientific
                   << A[i][j] << endl;
        nonzero_count++;
      }
    }
  }

  out_sparse.close();
  cout << "稀疏格式矩阵 A 已写入文件: " << p_sparse.string()
       << " (非零元素: " << nonzero_count << ")" << endl;
}

// 计算drs向量和平均值
void calculateAndOutputDrs(const vector<vector<dfloat>> &D_I,
                           const vector<vector<dfloat>> &D_E, int step) {
  int NL = D_I.size();
  int NE = D_I[0].size();

  cout << "\n=== 计算 A = D_I * D_E ===" << endl;
  cout << "D_I 大小: " << NL << " × " << NE << endl;
  cout << "D_E 大小: " << D_E.size() << " × " << D_E[0].size() << endl;

  vector<vector<dfloat>> A;
  matrixMultiply(D_I, D_E, A); // A = D_I (NL×NE) * D_E (NE×NL) = NL×NL

  cout << "A 大小: " << A.size() << " × " << A[0].size() << endl;

  // 检查矩阵 A 是否对称
  cout << "\n检查矩阵 A 的对称性..." << endl;
  dfloat max_asym = 0.0;
  int asym_count = 0;
  for (int i = 0; i < NL; i++) {
    for (int j = i + 1; j < NL; j++) {
      dfloat diff = fabs(A[i][j] - A[j][i]);
      if (diff > 1e-12) {
        asym_count++;
        if (diff > max_asym)
          max_asym = diff;
      }
    }
  }
  if (asym_count == 0) {
    cout << "✓ 矩阵 A 是对称的（误差 < 1e-12）" << endl;
  } else {
    cout << "✗ 矩阵 A 不对称！不对称元素数: " << asym_count
         << "，最大差异: " << scientific << max_asym << endl;
    cout << "注意: CG法要求矩阵对称正定，当前矩阵不满足条件" << endl;
  }

  // 诊断矩阵 A 的性质
  cout << "\n=== 诊断矩阵 A 的性质 ===" << endl;

  // 1. 检查对角线元素
  dfloat diag_min = A[0][0];
  dfloat diag_max = A[0][0];
  int zero_diag_count = 0;
  for (int i = 0; i < NL; i++) {
    if (fabs(A[i][i]) < 1e-15) {
      zero_diag_count++;
    }
    if (A[i][i] < diag_min)
      diag_min = A[i][i];
    if (A[i][i] > diag_max)
      diag_max = A[i][i];
  }

  cout << "对角线元素范围: [" << scientific << diag_min << ", " << diag_max
       << "]" << endl;
  cout << "接近零的对角线元素数量: " << zero_diag_count << " / " << NL << endl;

  if (zero_diag_count > 0) {
    cout << "警告: 发现零对角线元素，矩阵奇异！" << endl;
    cout << "前10个接近零的对角线索引:" << endl;
    int count = 0;
    for (int i = 0; i < NL && count < 10; i++) {
      if (fabs(A[i][i]) < 1e-15) {
        cout << "  A[" << i << "][" << i << "] = " << A[i][i] << endl;
        count++;
      }
    }
  }

  // 2. 估计条件数
  if (diag_min > 1e-15) {
    dfloat cond_est = diag_max / diag_min;
    cout << "条件数估计 (基于对角线): " << cond_est << endl;
    if (cond_est > 1e10) {
      cout << "警告: 矩阵病态，条件数过大！" << endl;
    }
  }

  // 3. 检查矩阵稀疏性
  int nonzero_count = 0;
  for (int i = 0; i < NL; i++) {
    for (int j = 0; j < NL; j++) {
      if (fabs(A[i][j]) > 1e-15) {
        nonzero_count++;
      }
    }
  }
  cout << "矩阵 A 非零元素: " << nonzero_count << " / " << NL * NL << " ("
       << 100.0 * nonzero_count / (NL * NL) << "%)" << endl;

  // 5. 检查对角线主导性（正定性的一个必要条件）
  cout << "\n检查对角线主导性..." << endl;
  int non_dominant_count = 0;
  for (int i = 0; i < NL; i++) {
    dfloat diag = fabs(A[i][i]);
    dfloat row_sum = 0.0;
    for (int j = 0; j < NL; j++) {
      if (i != j) {
        row_sum += fabs(A[i][j]);
      }
    }
    if (diag < row_sum) {
      non_dominant_count++;
    }
  }
  if (non_dominant_count == 0) {
    cout << "✓ 矩阵是严格对角线主导的（可能正定）" << endl;
  } else {
    cout << "✗ 有 " << non_dominant_count
         << " 行不满足对角线主导，矩阵可能不正定" << endl;
  }

  // 6. 添加正则化以改善条件数
  cout << "\n添加正则化项以改善条件数..." << endl;
  dfloat regularization = diag_min * 1e-6; // 小的正则化项
  for (int i = 0; i < NL; i++) {
    A[i][i] += regularization;
  }
  cout << "正则化参数: " << scientific << regularization << endl;

  // 输出矩阵 A 到文件
  cout << "\n输出矩阵 A 到文件..." << endl;
  outputMatrixA(A, step);

  // 4. 输出矩阵 A 的前几行前几列
  cout << "\n矩阵 A 的前5行前5列:" << endl;
  for (int i = 0; i < min(5, NL); i++) {
    cout << "  ";
    for (int j = 0; j < min(5, NL); j++) {
      cout << setw(12) << setprecision(4) << scientific << A[i][j] << " ";
    }
    cout << endl;
  }

  cout << "\n=== 开始求解 A * drs = b ===\n" << endl;
  cout << "尝试使用共轭梯度法（CG）求解..." << endl;

  vector<dfloat> b(NL, dx); // b = 1*h
  vector<dfloat> drs_vec;

  try {
    drs_vec = solveCG(A, b);
    cout << "CG求解成功！" << endl;
  } catch (const runtime_error &e) {
    cout << "CG求解失败: " << e.what() << endl;
    cout << "\n尝试使用LU分解..." << endl;
    try {
      drs_vec = solveLinearLU(A, b);
      cout << "LU分解求解成功！" << endl;
    } catch (const runtime_error &e2) {
      cout << "LU分解也失败: " << e2.what() << endl;
      cout << "\n可能的原因:" << endl;
      cout << "1. D_I 和 D_E 的维度不匹配" << endl;
      cout << "2. 拉格朗日点与欧拉点的耦合不足" << endl;
      cout << "3. 参数设置不合理（如 ds、drs 等）" << endl;
      cout << "\n建议:" << endl;
      cout << "1. 检查 D_I 大小是否为 NL×NE，D_E 大小是否为 NE×NL" << endl;
      cout << "2. 增加拉格朗日点密度或调整 delta 函数范围" << endl;
      cout << "3. 使用正则化或预条件方法" << endl;
      throw;
    }
  }

  // 输出drs向量
  std::filesystem::path p(output_dir);
  p /= std::string("drs_vector_") + std::to_string(step) + std::string(".dat");
  ofstream out(p.string());
  out << "# drs_i (边界厚度), i=1..NL\n";
  dfloat drs_sum = 0.0;
  for (int i = 0; i < NL; ++i) {
    out << drs_vec[i] << "\n";
    drs_sum += drs_vec[i];
  }
  out.close();
  dfloat drs_avg = drs_sum / NL;

  // 计算drs的统计信息
  dfloat drs_min = drs_vec[0];
  dfloat drs_max = drs_vec[0];
  for (int i = 0; i < NL; ++i) {
    if (drs_vec[i] < drs_min)
      drs_min = drs_vec[i];
    if (drs_vec[i] > drs_max)
      drs_max = drs_vec[i];
  }
  dfloat drs_std = 0.0;
  for (int i = 0; i < NL; ++i) {
    dfloat diff = drs_vec[i] - drs_avg;
    drs_std += diff * diff;
  }
  drs_std = sqrt(drs_std / NL);

  cout << "已写入drs向量文件: " << p.string() << endl;
  cout << "\n=== drs 统计信息 ===" << endl;
  cout << "drs平均值: " << fixed << setprecision(6) << drs_avg << endl;
  cout << "drs最小值: " << drs_min << endl;
  cout << "drs最大值: " << drs_max << endl;
  cout << "drs标准差: " << drs_std << endl;
  cout << "前10个drs值:" << endl;
  for (int i = 0; i < min(10, NL); ++i) {
    cout << "  drs[" << i << "] = " << drs_vec[i] << endl;
  }

  // ========== 新增：计算使用 drs 向量和平均值后的新矩阵 A ==========
  cout << "\n=== 计算使用 drs 后的新矩阵 A ===" << endl;

  // 原始的 drs 值（来自宏定义 #define drs (1.0 * dx)）
  dfloat original_drs = 1.0 * dx;
  
  // 1. 使用 drs 向量构建新的 D_E（每个拉格朗日点使用不同的 drs）
  vector<vector<dfloat>> D_E_with_drs_vec(NE, vector<dfloat>(NL, 0.0));
  
  cout << "\n构建 D_E（使用 drs 向量）..." << endl;
  cout << "原始 drs 值: " << fixed << setprecision(6) << original_drs << endl;
  cout << "drs 向量平均值: " << drs_avg << endl;
  
  for (int j = 0; j < NE; j++) {
    for (int i = 0; i < NL; i++) {
      // D_E[j][i] 对应欧拉点j到拉格朗日点i的外推
      // 使用每个拉格朗日点自己的 drs[i]
      // 新的 scale_factor = (drs_vec[i] * ds) / (dx * dx)
      // 原始的 D_E[j][i] = delta_x * delta_y * (original_drs * ds) / (dx * dx)
      // 新的 D_E[j][i] = delta_x * delta_y * (drs_vec[i] * ds) / (dx * dx)
      // 所以: D_E_new[j][i] = D_E[j][i] * (drs_vec[i] / original_drs)
      dfloat ratio = drs_vec[i] / original_drs;
      D_E_with_drs_vec[j][i] = D_E[j][i] * ratio;
    }
  }

  // 2. 使用 drs 平均值构建新的 D_E（所有拉格朗日点使用相同的 drs_avg）
  vector<vector<dfloat>> D_E_with_drs_avg(NE, vector<dfloat>(NL, 0.0));
  
  cout << "构建 D_E（使用 drs 平均值）..." << endl;
  dfloat ratio_avg = drs_avg / original_drs;
  cout << "drs 平均值比例: " << ratio_avg << endl;
  
  for (int j = 0; j < NE; j++) {
    for (int i = 0; i < NL; i++) {
      // 使用统一的 drs_avg
      D_E_with_drs_avg[j][i] = D_E[j][i] * ratio_avg;
    }
  }

  // 3. 计算新的矩阵 A_vec = D_I * D_E_with_drs_vec
  vector<vector<dfloat>> A_vec;
  matrixMultiply(D_I, D_E_with_drs_vec, A_vec);
  
  cout << "A_vec（使用 drs 向量）大小: " << A_vec.size() << " × " << A_vec[0].size() << endl;

  // 4. 计算新的矩阵 A_avg = D_I * D_E_with_drs_avg
  vector<vector<dfloat>> A_avg;
  matrixMultiply(D_I, D_E_with_drs_avg, A_avg);
  
  cout << "A_avg（使用 drs 平均值）大小: " << A_avg.size() << " × " << A_avg[0].size() << endl;

  // 5. 分析并输出新矩阵的性质
  cout << "\n=== 新矩阵 A 的对比分析 ===" << endl;

  // 计算原始 A、A_vec、A_avg 的统计信息
  dfloat A_sum = 0.0, A_vec_sum = 0.0, A_avg_sum = 0.0;
  dfloat A_diag_sum = 0.0, A_vec_diag_sum = 0.0, A_avg_diag_sum = 0.0;
  int A_nonzero = 0, A_vec_nonzero = 0, A_avg_nonzero = 0;

  for (int i = 0; i < NL; i++) {
    for (int j = 0; j < NL; j++) {
      A_sum += A[i][j];
      A_vec_sum += A_vec[i][j];
      A_avg_sum += A_avg[i][j];
      
      if (fabs(A[i][j]) > 1e-15) A_nonzero++;
      if (fabs(A_vec[i][j]) > 1e-15) A_vec_nonzero++;
      if (fabs(A_avg[i][j]) > 1e-15) A_avg_nonzero++;
      
      if (i == j) {
        A_diag_sum += A[i][i];
        A_vec_diag_sum += A_vec[i][i];
        A_avg_diag_sum += A_avg[i][i];
      }
    }
  }

  cout << "\n矩阵元素统计:" << endl;
  cout << "原始 A      : sum = " << setw(12) << setprecision(6) << fixed << A_sum 
       << ", 对角线和 = " << A_diag_sum << ", 非零元素 = " << A_nonzero << endl;
  cout << "A_vec (drs向量): sum = " << setw(12) << A_vec_sum 
       << ", 对角线和 = " << A_vec_diag_sum << ", 非零元素 = " << A_vec_nonzero << endl;
  cout << "A_avg (drs平均): sum = " << setw(12) << A_avg_sum 
       << ", 对角线和 = " << A_avg_diag_sum << ", 非零元素 = " << A_avg_nonzero << endl;

  // 6. 计算矩阵差异
  dfloat diff_vec_avg_max = 0.0;
  dfloat diff_vec_avg_avg = 0.0;
  dfloat diff_A_vec_max = 0.0;
  dfloat diff_A_avg_max = 0.0;

  for (int i = 0; i < NL; i++) {
    for (int j = 0; j < NL; j++) {
      dfloat diff_va = fabs(A_vec[i][j] - A_avg[i][j]);
      dfloat diff_Av = fabs(A[i][j] - A_vec[i][j]);
      dfloat diff_Aa = fabs(A[i][j] - A_avg[i][j]);
      
      diff_vec_avg_avg += diff_va;
      if (diff_va > diff_vec_avg_max) diff_vec_avg_max = diff_va;
      if (diff_Av > diff_A_vec_max) diff_A_vec_max = diff_Av;
      if (diff_Aa > diff_A_avg_max) diff_A_avg_max = diff_Aa;
    }
  }
  diff_vec_avg_avg /= (NL * NL);

  cout << "\n矩阵差异分析:" << endl;
  cout << "A_vec vs A_avg: 最大差异 = " << scientific << setprecision(4) << diff_vec_avg_max 
       << ", 平均差异 = " << diff_vec_avg_avg << endl;
  cout << "原始A vs A_vec: 最大差异 = " << diff_A_vec_max << endl;
  cout << "原始A vs A_avg: 最大差异 = " << diff_A_avg_max << endl;

  // 7. 输出新矩阵的前几行前几列
  cout << "\n原始矩阵 A 的前5行前5列:" << endl;
  for (int i = 0; i < min(5, NL); i++) {
    cout << "  ";
    for (int j = 0; j < min(5, NL); j++) {
      cout << setw(12) << setprecision(4) << scientific << A[i][j] << " ";
    }
    cout << endl;
  }

  cout << "\nA_vec（使用 drs 向量）的前5行前5列:" << endl;
  for (int i = 0; i < min(5, NL); i++) {
    cout << "  ";
    for (int j = 0; j < min(5, NL); j++) {
      cout << setw(12) << setprecision(4) << scientific << A_vec[i][j] << " ";
    }
    cout << endl;
  }

  cout << "\nA_avg（使用 drs 平均值）的前5行前5列:" << endl;
  for (int i = 0; i < min(5, NL); i++) {
    cout << "  ";
    for (int j = 0; j < min(5, NL); j++) {
      cout << setw(12) << setprecision(4) << scientific << A_avg[i][j] << " ";
    }
    cout << endl;
  }

  // 8. 验证 A_vec * drs_vec 和 A_avg * [drs_avg, ...] 的结果
  cout << "\n=== 验证新矩阵的方程 ===" << endl;
  
  // A_vec * drs_vec 应该 ≈ [h, h, ..., h]
  vector<dfloat> result_vec(NL, 0.0);
  for (int i = 0; i < NL; i++) {
    for (int j = 0; j < NL; j++) {
      result_vec[i] += A_vec[i][j] * drs_vec[j];
    }
  }
  
  // A_avg * [drs_avg, ...] 应该 ≈ [h, h, ..., h]
  vector<dfloat> result_avg(NL, 0.0);
  for (int i = 0; i < NL; i++) {
    for (int j = 0; j < NL; j++) {
      result_avg[i] += A_avg[i][j] * drs_avg;
    }
  }

  dfloat error_vec = 0.0, error_avg = 0.0;
  for (int i = 0; i < NL; i++) {
    error_vec += fabs(result_vec[i] - dx);
    error_avg += fabs(result_avg[i] - dx);
  }
  error_vec /= NL;
  error_avg /= NL;

  cout << "A_vec * drs_vec 的平均误差（与 h 的差）: " << scientific << error_vec << endl;
  cout << "A_avg * [drs_avg, ...] 的平均误差（与 h 的差）: " << error_avg << endl;
  
  cout << "\n前5个结果值（应接近 h = " << fixed << setprecision(6) << dx << "）:" << endl;
  cout << "i\tA_vec*drs_vec\tA_avg*drs_avg\t目标值(h)" << endl;
  for (int i = 0; i < min(5, NL); i++) {
    cout << i << "\t" << result_vec[i] << "\t" << result_avg[i] << "\t" << dx << endl;
  }

  // 9. 输出新矩阵到文件
  std::filesystem::path p_vec(output_dir);
  p_vec /= std::string("matrix_A_vec_") + std::to_string(step) + std::string(".dat");
  ofstream out_vec(p_vec.string());
  out_vec << "# 矩阵 A_vec = D_I * D_E（使用 drs 向量）- 步数: " << step << endl;
  out_vec << "# 矩阵大小: " << NL << " × " << NL << endl;
  for (int i = 0; i < NL; i++) {
    for (int j = 0; j < NL; j++) {
      out_vec << setprecision(12) << scientific << A_vec[i][j];
      if (j < NL - 1) out_vec << "\t";
    }
    out_vec << endl;
  }
  out_vec.close();
  cout << "\n矩阵 A_vec 已写入文件: " << p_vec.string() << endl;

  std::filesystem::path p_avg(output_dir);
  p_avg /= std::string("matrix_A_avg_") + std::to_string(step) + std::string(".dat");
  ofstream out_avg(p_avg.string());
  out_avg << "# 矩阵 A_avg = D_I * D_E（使用 drs 平均值）- 步数: " << step << endl;
  out_avg << "# 矩阵大小: " << NL << " × " << NL << endl;
  for (int i = 0; i < NL; i++) {
    for (int j = 0; j < NL; j++) {
      out_avg << setprecision(12) << scientific << A_avg[i][j];
      if (j < NL - 1) out_avg << "\t";
    }
    out_avg << endl;
  }
  out_avg.close();
  cout << "矩阵 A_avg 已写入文件: " << p_avg.string() << endl;
}

typedef double dfloat;

// 拉格朗日点相关参数
#define Nd 3.25 // 参考距离,用于确定拉格朗日点的位置
#define Num 62

// #define ds ((dfloat)NX / Num)     // 拉格朗日点之间的距离
// #define np ((int)((NX / ds) * 2)) // 拉格朗日点数量
// #define drs (1.0) // 固体边界厚度，DF为1.0，3p函数为1.95,4p函数为2.6

#define xc (NX / 2.0)
#define yc (NY / 2.0)
#define R (NX / 4.0)
#define PI (3.141592653589793238462643383279)
#define ds0 (0.8 * dx)           // 颗粒表面拉氏点的距离初设值
#define np int(2 * PI * R / ds0) // 拉格朗日点数量
#define drs (1.0 * dx)       // 固体边界厚度，DF为1.0，3p函数为1.95,4p函数为2.6
#define ds (2 * PI * R / np) // 拉格朗日点之间的距离
#define IB_weight (ds * drs) // IBM插值权重

dfloat xl[np][2]; // 拉格朗日点坐标

// 计算实际的流动区域高度（上下IB边界之间的距离）
#define flow_height (LY - 2 * Nd) // 实际流动区域高度（上下IB边界之间的距离）
#define H (flow_height / 2.0)

// 流体物理特性参数
#define U (0.1)                      // 特征速度
dfloat rho0 = 1.0;                   // 参考密度
#define tau_f (sqrt(3.0 / 16) + 0.5) // 松弛时间
#define niu ((2 * tau_f - 1.0) / 6)  // 运动粘度
#define Re (LY * U / niu)            // 雷诺数

// 体积力驱动参数
dfloat fx =
    2.0 * rho0 * niu * U / (H * H); // x方向的体积力，等效于原来的压力梯度
dfloat fy = 0.0;                    // y方向的体积力

// 模拟控制参数
#define OUT 4000      // 输出间隔步数
#define Maxstep 40001 // 最大迭代步数

// 收敛性判断参数
#define teval 100         // 评估收敛性的时间步间隔
const dfloat tol = 1e-11; // 稳态收敛容差
dfloat mean_u_old = 0.0;
dfloat mean_u = 0.0;

// D2Q9模型的离散速度方向和权重
int e[Q][2] = {{0, 0}, {1, 0},  {0, 1},   {-1, 0}, {0, -1},
               {1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
dfloat w[Q] = {4.0 / 9,  1.0 / 9,  1.0 / 9,  1.0 / 9, 1.0 / 9,
               1.0 / 36, 1.0 / 36, 1.0 / 36, 1.0 / 36};

// 流体点类
class FluidPoint {
public:
  dfloat rho;      // 密度
  dfloat u[2];     // 速度
  dfloat u_old[2]; // 前一次的速度
  dfloat Ft[2];    // 体积力
  dfloat fin[Q];   // 入射分布函数
  dfloat fout[Q];  // 出射分布函数

  FluidPoint() {
    rho = rho0;
    u[0] = 0.0;
    u[1] = 0.0;
    u_old[0] = 0.0;
    u_old[1] = 0.0;
    Ft[0] = 0.0;
    Ft[1] = 0.0;
  }

  // 计算压力
  dfloat pressure() const { return rho * cs_square; }
};

FluidPoint points[NX][NY]; // 构造流体质元网格阵列
int number = 0;

// 函数声明
dfloat feq(int k, dfloat rho, dfloat u[2]);
dfloat forceGuo(int i, dfloat u[2], dfloat Ft[2]);
dfloat delta3p(dfloat r);
dfloat delta2p(dfloat r);
dfloat delta4p(dfloat r);
void initialize();
void initParticle();
void initChannelParticle();
void stream();
void computeMacroscopic();
void applyBoundaryConditions();
void Circle_IBM();
void collision_fluid();
void output(int m);
void outputVelocityProfile(int step);
void outputActiveEulerPoints(const set<pair<int, int>> &activePoints, int step);
void buildInterpolationMatrix(vector<vector<dfloat>> &D_I,
                              vector<pair<int, int>> &eulerPoints);
void buildSpreadingMatrix(vector<vector<dfloat>> &D_E,
                          const vector<pair<int, int>> &eulerPoints);
void outputInterpolationMatrix(const vector<vector<dfloat>> &D_I,
                               const vector<pair<int, int>> &eulerPoints,
                               int step);
void outputSpreadingMatrix(const vector<vector<dfloat>> &D_E,
                           const vector<pair<int, int>> &eulerPoints, int step);
void verifyInterpolationSpreading(const vector<vector<dfloat>> &D_I,
                                  const vector<vector<dfloat>> &D_E,
                                  const vector<pair<int, int>> &eulerPoints);
dfloat calculateMeanVelocity(int component);
bool checkConvergence(int step, dfloat &conv);
dfloat calculateDrs(int deltaType = 3);
void gaussElimination(vector<vector<dfloat>> &A, vector<dfloat> &b,
                      vector<dfloat> &x);
void matrixVectorMultiply(const vector<vector<dfloat>> &A,
                          const vector<dfloat> &x, vector<dfloat> &result);

// 主函数
int main(int argc, char *argv[]) {
  // 打印体积力参数
  cout << "=== 模拟参数信息 ===" << endl;
  cout << "雷诺数 Re = " << Re << endl;
  cout << "粘度 niu = " << niu << endl;
  cout << "松弛时间 tau_f = " << tau_f << endl;
  cout << "体积力 fx = " << fx << endl;
  cout << "体积力 fy = " << fy << endl;
  cout << "===================" << endl << endl;

  // 如果通过命令行传入输出目录，则使用之（argv[1]）
  if (argc > 1) {
    output_dir = std::string(argv[1]);
  }

  // 创建输出目录（递归创建），如果失败则报错并退出
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    std::cerr << "无法创建输出目录: " << output_dir << " 错误: " << ec.message()
              << std::endl;
    return 1;
  }

  // 初始化
  initialize();
  initParticle();
  // initChannelParticle();

  // 构建插值矩阵 D_I 和外推矩阵 D_E
  vector<vector<dfloat>> D_I;
  vector<pair<int, int>> eulerPoints;
  buildInterpolationMatrix(D_I, eulerPoints);
  outputInterpolationMatrix(D_I, eulerPoints, 0);
  vector<vector<dfloat>> D_E;
  buildSpreadingMatrix(D_E, eulerPoints);
  outputSpreadingMatrix(D_E, eulerPoints, 0);

  // 直接计算并输出 drs 向量及平均值
  calculateAndOutputDrs(D_I, D_E, 0);

  return 0;
}

dfloat feq(int k, dfloat rho, dfloat u[2]) {
  dfloat eu = e[k][0] * u[0] + e[k][1] * u[1];
  dfloat uu = u[0] * u[0] + u[1] * u[1];
  return w[k] * rho * (1.0 + 3.0 * eu + 4.5 * eu * eu - 1.5 * uu);
}

dfloat forceGuo(int i, dfloat u[2], dfloat Ft[2]) {
  dfloat m, n, nf;

  m = ((e[i][0] - u[0]) * Ft[0] + (e[i][1] - u[1]) * Ft[1]) / cs_square;

  n = (e[i][0] * u[0] + e[i][1] * u[1]) / cs_square;

  nf = (e[i][0] * Ft[0] + e[i][1] * Ft[1]) / cs_square;

  return w[i] * (m + n * nf);
}

// 初始化函数
void initialize() {
  for (int i = 0; i <= LX; i++) {
    for (int j = 0; j <= LY; j++) {
      for (int k = 0; k <= Q - 1; k++) {
        points[i][j].fout[k] = feq(k, points[i][j].rho, points[i][j].u);
      }
    }
  }
}

void initParticle() {
  for (int i = 0; i < np; i++) {
    xl[i][0] = xc + R * cos(2.0 * PI * (i) / np);
    xl[i][1] = yc + R * sin(2.0 * PI * (i) / np);
  }
  // 打印所有拉格朗日点的坐标
  cout << "=== 拉格朗日点坐标信息 ===" << endl;
  cout << "拉格朗日点总数: " << np << endl;
  cout << "点间距 ds: " << ds << endl;
  cout << "参考距离 Nd: " << Nd << endl;
  cout << "----------------------------" << endl;
  for (int i = 0; i < np; i++) {
    cout << "点 " << setw(3) << i << ": (" << setprecision(4) << fixed
         << setw(8) << xl[i][0] << ", " << setw(8) << xl[i][1] << ")";
    if (i < (np / 2)) {
      cout << "  [上边界]";
    } else {
      cout << "  [下边界]";
    }
    cout << endl;
  }
  cout << "============================" << endl << endl;
}

void initChannelParticle() {
  // 初始化拉格朗日点坐标
  for (int i = 0; i < np; i++) {
    if (i < (np / 2)) {
      xl[i][0] = i * ds;
      xl[i][1] = LY - Nd;
    } else {
      xl[i][0] = (i - np / 2.0) * ds;
      xl[i][1] = Nd;
    }
  }

  // 打印所有拉格朗日点的坐标
  cout << "=== 拉格朗日点坐标信息 ===" << endl;
  cout << "拉格朗日点总数: " << np << endl;
  cout << "点间距 ds: " << ds << endl;
  cout << "参考距离 Nd: " << Nd << endl;
  cout << "----------------------------" << endl;
  for (int i = 0; i < np; i++) {
    cout << "点 " << setw(3) << i << ": (" << setprecision(4) << fixed
         << setw(8) << xl[i][0] << ", " << setw(8) << xl[i][1] << ")";
    if (i < (np / 2)) {
      cout << "  [上边界]";
    } else {
      cout << "  [下边界]";
    }
    cout << endl;
  }
  cout << "============================" << endl << endl;
}

// 流动步骤函数（x方向周期性，y方向半反弹）
void stream() {
  static const int Reflect[Q] = {0, 3, 4, 1, 2, 7, 8, 5, 6};
  for (int i = 0; i <= LX; i++) {
    for (int j = 0; j <= LY; j++) {
      for (int k = 0; k <= Q - 1; k++) {
        int ip = i - e[k][0];
        int jp = j - e[k][1];

        // x方向周期性
        if (ip < 0)
          ip += NX;
        if (ip > LX)
          ip -= NX;

        // y方向：内部直接搬运；越界则在本格点做半反弹
        if (jp >= 0 && jp <= LY) {
          points[i][j].fin[k] = points[ip][jp].fout[k];
        } else {
          points[i][j].fin[k] = points[i][j].fout[Reflect[k]];
        }
      }
    }
  }
}

// 计算宏观量函数
void computeMacroscopic() {
  for (int i = 0; i <= LX; i++) {
    for (int j = 0; j <= LY; j++) {
      points[i][j].rho = 0;
      points[i][j].u[0] = 0;
      points[i][j].u[1] = 0;

      for (int k = 0; k <= Q - 1; k++) {
        points[i][j].rho += points[i][j].fin[k];
        points[i][j].u[0] += e[k][0] * points[i][j].fin[k];
        points[i][j].u[1] += e[k][1] * points[i][j].fin[k];
      }
      points[i][j].u[0] /= points[i][j].rho;
      points[i][j].u[1] /= points[i][j].rho;
    }
  }
}

// 应用边界条件函数
void applyBoundaryConditions() {
  // 左右边界的周期性边界条件在这里不需要特别处理，因为流动步骤已经隐含了周期性

  // 上下边界的半反弹格式在这里不需要特别处理，因为流动步骤已经隐含了半反弹
}

// 碰撞步骤函数
void collision_fluid() {
  for (int i = 0; i <= LX; i++)
    for (int j = 0; j <= LY; j++) {

      dfloat Ftx = points[i][j].Ft[0] + fx;
      dfloat Fty = points[i][j].Ft[1] + fy;
      points[i][j].u[0] += 0.5 * dt * Ftx / points[i][j].rho;
      points[i][j].u[1] += 0.5 * dt * Fty / points[i][j].rho;

      dfloat omega = 1 / tau_f;
      dfloat Ft[2] = {Ftx, Fty};

      for (int k = 0; k <= Q - 1; k++) {
        dfloat forceEx = (1 - 0.5 * omega) * forceGuo(k, points[i][j].u, Ft);

        points[i][j].fout[k] =
            points[i][j].fin[k] +
            omega * (feq(k, points[i][j].rho, points[i][j].u) -
                     points[i][j].fin[k]) +
            forceEx * dt;
      }

      points[i][j].Ft[0] = 0.0;
      points[i][j].Ft[1] = 0.0;
    }
}

void output(int m) {
  std::filesystem::path p(output_dir);
  p /= std::string("poiseuille") + std::to_string(m) + std::string(".dat");
  ofstream out(p.string());
  out << "Title=\"LBM Poiseuille Flow\" \n"
      << "VARIABLES= \"X\", \"Y\", \"U\", \"V\", \"RHO\" \n"
      // 修改这里，确保声明的网格尺寸与实际输出匹配
      << "ZONE T=\"Flow Field\", I=" << NX << ",J=" << NY << ",F=POINT" << endl;
  for (int j = 0; j <= LY; j++) {
    for (int i = 0; i <= LX; i++) {
      out << i << " " << j << " " << points[i][j].u[0] << " "
          << points[i][j].u[1] << " " << points[i][j].rho << endl;
    }
  }
  out.close();
  cout << "已写入文件: " << p.string() << endl;
}

// 计算速度场平均值的函数实现
dfloat calculateMeanVelocity(int component) {
  dfloat sum = 0.0;
  int count = 0;

  for (int i = 0; i <= LX; i++) {
    for (int j = 1; j <= LY - 1; j++) {
      sum += points[i][j].u[component];
      count++;
    }
  }

  return (count > 0) ? sum / count : 0.0;
}

// 输出速度剖面数据的函数
void outputVelocityProfile(int step) {
  std::filesystem::path p(output_dir);
  p /= std::string("velocity_profile_") + std::to_string(step) +
       std::string(".dat");
  ofstream out(p.string());

  out << "# 速度剖面数据文件 - 步数: " << step << endl;
  out << "# 列1: y坐标位置" << endl;
  out << "# 列2: 理论解析解速度" << endl;
  out << "# 列3: 模拟结果速度 (x = NX/2处的剖面)" << endl;
  out << "# y\t u理论\t u模拟" << endl;

  // 计算每个y位置的理论解析解
  for (int j = 0; j <= LY; j++) {
    // 计算Poiseuille流理论解析解
    dfloat u_max = U; // 最大速度
    dfloat ybottom = Nd;
    dfloat ytop = LY - Nd;
    dfloat u_theory = -u_max / (H * H) * (j - ybottom) * (j - ytop);

    // 从模拟中获取在x = NX/2处的速度
    dfloat u_simulated = points[NX / 2][j].u[0];

    // 输出数据
    out << j << "\t" << u_theory << "\t" << u_simulated << endl;
  }

  out.close();
  cout << "速度剖面数据已写入文件: " << p.string() << endl;
}

// 收敛性检查函数
bool checkConvergence(int step, dfloat &conv) {
  if (step % teval == 0 && step > 0) {
    // 计算当前的平均速度（x分量）
    mean_u = calculateMeanVelocity(0);

    // 如果是第一次计算，把 mean_u_old 初始化为 mean_u，以避免除0或误报
    if (mean_u_old == 0.0) {
      conv = fabs(1.0);
    } else {
      // 相对误差： (new - old) / old
      conv = fabs(mean_u / (mean_u_old + 1e-15) - 1.0);
    }

    if (mean_u_old != 0.0 && conv < tol) {
      cout << "模拟已收敛于步数: " << step << endl;
      cout << "收敛误差: " << conv << endl;
      return true;
    } else {
      // 保存 mean_u 为下一次迭代比较的基准
      mean_u_old = mean_u;
    }
  }

  return false;
}

void Circle_IBM() {
  dfloat xt, yt, lx, ly;
  dfloat IB_Interp;
  dfloat uxt, uyt, rhot, upx, upy, fxt, fyt;
  for (int i = 0; i < np; i++) {
    uxt = 0.0;
    uyt = 0.0;
    rhot = 0.0;
    upx = 0.0;
    upy = 0.0;

    xt = xl[i][0];
    yt = xl[i][1];

    for (int x = -2; x <= 2; x++) {
      for (int y = -2; y <= 2; y++) {
        int xx = int(xt) + x; // 拉格朗日点作用范围内真实的点坐标
        int yy = int(yt) + y;

        // 处理x方向越界
        if (xx < 0)
          xx = NX - xx;
        else if (xx > LX)
          xx = xx - NX;

        // 处理y方向越界
        if (yy < 0)
          yy = -yy; // 镜像反射下边界
        else if (yy > LY)
          yy = 2 * NY - yy; // 镜像反射上边界

        // 确保镜像后的点仍在有效范围内
        xx = max(0, min(LX, xx));
        yy = max(0, min(LY, yy));

        lx = xt - xx;
        ly = yt - yy;

        IB_Interp = delta3p(lx) * delta3p(ly);

        uxt += points[xx][yy].u[0] * IB_Interp;
        uyt += points[xx][yy].u[1] * IB_Interp;
        rhot += points[xx][yy].rho * IB_Interp;
      }
    }

    fxt = 2 * rhot / dt * (uxt - upx);
    fyt = 2 * rhot / dt * (uyt - upy);

    for (int x = -2; x <= 2; x++) {
      for (int y = -2; y <= 2; y++) {
        int xx = int(xt) + x; // 拉格朗日点作用范围内真实的点坐标
        int yy = int(yt) + y;

        // 处理x方向越界
        if (xx < 0)
          xx = NX - xx;
        else if (xx > LX)
          xx = xx - NX;

        // 处理y方向越界
        if (yy < 0)
          yy = -yy; // 镜像反射下边界
        else if (yy > LY)
          yy = 2 * NY - yy; // 镜像反射上边界

        // 确保镜像后的点仍在有效范围内
        xx = max(0, min(LX, xx));
        yy = max(0, min(LY, yy));

        lx = xt - xx;
        ly = yt - yy;

        IB_Interp = delta3p(lx) * delta3p(ly) * IB_weight;

        points[xx][yy].Ft[0] -= fxt * IB_Interp;
        points[xx][yy].Ft[1] -= fyt * IB_Interp;
      }
    }
  }
}

dfloat delta3p(dfloat r) {
  r = fabs(r);
  if (r <= 0.5)
    return ((1.0 + sqrt(1.0 - 3.0 * r * r)) / 3.0);
  else if (r <= 1.5)
    return ((5.0 - 3.0 * r - sqrt(1.0 - 3.0 * (1.0 - r) * (1.0 - r))) / 6.0);
  else
    return 0.0;
}

// 2点delta函数
dfloat delta2p(dfloat r) {
  r = fabs(r);
  if (r <= 1.0)
    return (1.0 - r);
  else
    return 0.0;
}

// 4点delta函数
dfloat delta4p(dfloat r) {
  r = fabs(r);
  if (r <= 1.0)
    return (1.0 + sqrt(-3.0 * r * r + 1.0)) / 8.0;
  else if (r <= 2.0)
    return (5.0 - 2.0 * r - sqrt(-3.0 * (2.0 - r) * (2.0 - r) + 1.0)) / 8.0;
  else
    return 0.0;
}

// 高斯消元法求解线性方程组 Ax = b
void gaussElimination(vector<vector<dfloat>> &A, vector<dfloat> &b,
                      vector<dfloat> &x) {
  int n = b.size();
  const dfloat TINY = 1e-20; // 防止除零

  // 前向消元
  for (int k = 0; k < n; k++) {
    // 部分主元选择（找最大的主元以提高数值稳定性）
    int maxRow = k;
    dfloat maxVal = fabs(A[k][k]);
    for (int i = k + 1; i < n; i++) {
      if (fabs(A[i][k]) > maxVal) {
        maxVal = fabs(A[i][k]);
        maxRow = i;
      }
    }

    // 检查主元是否太小
    if (maxVal < TINY) {
      cout << "警告: 第 " << k << " 列主元接近零 (" << maxVal
           << ")，矩阵可能奇异" << endl;
      // 添加一个小的对角元素以正则化
      A[k][k] += TINY;
    }

    // 交换行
    if (maxRow != k) {
      swap(A[k], A[maxRow]);
      swap(b[k], b[maxRow]);
    }

    // 消元
    for (int i = k + 1; i < n; i++) {
      dfloat factor = A[i][k] / (A[k][k] + TINY);
      // 检查factor是否合理
      if (fabs(factor) > 1e10) {
        cout << "警告: 消元因子过大 (行" << i << ", 列" << k << "): " << factor
             << endl;
      }
      for (int j = k; j < n; j++) {
        A[i][j] -= factor * A[k][j];
      }
      b[i] -= factor * b[k];
    }
  }

  // 回代求解
  x.resize(n);
  for (int i = n - 1; i >= 0; i--) {
    x[i] = b[i];
    for (int j = i + 1; j < n; j++) {
      x[i] -= A[i][j] * x[j];
    }
    x[i] /= (A[i][i] + TINY);

    // 检查解是否合理
    if (fabs(x[i]) > 1e6) {
      cout << "警告: 解的第 " << i << " 个分量过大: " << x[i] << endl;
    }
  }
}

// 矩阵向量乘法
void matrixVectorMultiply(const vector<vector<dfloat>> &A,
                          const vector<dfloat> &x, vector<dfloat> &result) {
  int n = A.size();
  result.resize(n, 0.0);
  for (int i = 0; i < n; i++) {
    result[i] = 0.0;
    for (int j = 0; j < n; j++) {
      result[i] += A[i][j] * x[j];
    }
  }
}

// 边界增厚法计算drs
// deltaType: 1-2点函数, 2-3点函数(默认), 3-4点函数
dfloat calculateDrs(int deltaType) {
  cout << "\n=== 开始计算固体边界厚度 drs (BTDF方法) ===" << endl;

  // 选择delta函数
  dfloat (*deltaFunc)(dfloat);
  int range; // delta函数的影响范围
  switch (deltaType) {
  case 1:
    deltaFunc = delta2p;
    range = 1;
    cout << "使用2点delta函数 (drf = 2h)" << endl;
    break;
  case 3:
    deltaFunc = delta4p;
    range = 2;
    cout << "使用4点delta函数 (drf = 4h)" << endl;
    break;
  case 2:
  default:
    deltaFunc = delta3p;
    range = 2;
    cout << "使用3点delta函数 (drf = 3h, 推荐)" << endl;
    break;
  }

  // 打印参数信息
  cout << "拉格朗日点数量 np = " << np << endl;
  cout << "拉格朗日点间距 ds = " << ds << endl;
  cout << "欧拉网格步长 h = " << dx << endl;
  cout << "ds/h = " << ds / dx << endl;

  // 检查参数约束条件 ds/drf < 0.5
  dfloat drf =
      (range == 1) ? 2.0 : ((range == 2 && deltaType == 2) ? 3.0 : 4.0);
  dfloat ds_drf_ratio = ds / drf;
  cout << "ds/drf = " << ds_drf_ratio;
  if (ds_drf_ratio >= 0.5) {
    cout << " [警告: 超过推荐值0.5，可能导致流体泄漏]" << endl;
  } else {
    cout << " [满足约束条件]" << endl;
  }

  // 打印前5个拉格朗日点坐标用于调试
  cout << "\n调试: 前5个拉格朗日点坐标:" << endl;
  for (int i = 0; i < min(5, np); i++) {
    cout << "  xl[" << i << "] = (" << xl[i][0] << ", " << xl[i][1] << ")"
         << endl;
  }

  // 步骤1: 构建矩阵A (式2.21)
  cout << "\n步骤1: 构建插值-外推矩阵 A..." << endl;
  vector<vector<dfloat>> A(np, vector<dfloat>(np, 0.0));

  for (int i = 0; i < np; i++) {
    for (int j = 0; j < np; j++) {
      dfloat sum = 0.0;

      // 遍历拉格朗日点i周围的欧拉网格点
      // 修正：使用更大的搜索范围以确保覆盖所有相关网格点
      for (int dx_offset = -range - 1; dx_offset <= range + 1; dx_offset++) {
        for (int dy_offset = -range - 1; dy_offset <= range + 1; dy_offset++) {
          // 计算欧拉网格点坐标
          int xx = int(xl[i][0] + 0.5) + dx_offset; // 四舍五入到最近的网格点
          int yy = int(xl[i][1] + 0.5) + dy_offset;

          // 处理x方向周期性边界
          if (xx < 0)
            xx += NX;
          else if (xx > LX)
            xx -= NX;

          // 处理y方向镜像边界
          if (yy < 0)
            yy = -yy;
          else if (yy > LY)
            yy = 2 * LY - yy;

          // 确保在有效范围内
          if (xx < 0 || xx > LX || yy < 0 || yy > LY)
            continue;

          // 计算delta函数值
          dfloat rx_i = xl[i][0] - xx;
          dfloat ry_i = xl[i][1] - yy;
          dfloat delta_ik = deltaFunc(rx_i) * deltaFunc(ry_i);

          dfloat rx_j = xl[j][0] - xx;
          dfloat ry_j = xl[j][1] - yy;
          dfloat delta_kj = deltaFunc(rx_j) * deltaFunc(ry_j);

          // 累加贡献 (按照式2.21)
          sum += delta_ik * delta_kj * dx * ds;
        }
      }

      A[i][j] = sum / (dx * dx);
    }
  }

  // 调试：打印矩阵的对角线元素
  cout << "矩阵 A 构建完成 (大小: " << np << "x" << np << ")" << endl;
  cout << "调试: 对角线前5个元素: ";
  for (int i = 0; i < min(5, np); i++) {
    cout << A[i][i] << " ";
  }
  cout << endl;

  // 检查矩阵条件数（简单检查：对角元素的最大最小比值）
  dfloat diag_min = A[0][0], diag_max = A[0][0];
  for (int i = 0; i < np; i++) {
    if (A[i][i] < diag_min)
      diag_min = A[i][i];
    if (A[i][i] > diag_max)
      diag_max = A[i][i];
  }
  cout << "对角元素范围: [" << diag_min << ", " << diag_max << "]" << endl;
  if (diag_max / (diag_min + 1e-15) > 1e6) {
    cout << "警告: 矩阵可能病态，条件数估计 > 1e6" << endl;
  }

  // 步骤2: 求解 A * drs_vec = h * 1_vec (式2.23)
  cout << "\n步骤2: 求解线性方程组 A * drs = h * 1..." << endl;
  vector<dfloat> b(np, dx); // 右端项为h
  vector<dfloat> drs_local; // 局部drs值

  // 创建A的副本用于求解（因为高斯消元会修改矩阵）
  vector<vector<dfloat>> A_copy = A;
  vector<dfloat> b_copy = b;

  gaussElimination(A_copy, b_copy, drs_local);

  // 验证解的正确性：计算 A * drs_local，看是否等于 b
  vector<dfloat> verify;
  matrixVectorMultiply(A, drs_local, verify);
  dfloat residual = 0.0;
  for (int i = 0; i < np; i++) {
    residual += (verify[i] - b[i]) * (verify[i] - b[i]);
  }
  residual = sqrt(residual / np);
  cout << "求解残差 (RMS): " << residual << endl;

  if (residual > 0.1 * dx) {
    cout << "警告: 求解残差较大，结果可能不准确" << endl;
    cout << "建议: 1) 增加拉格朗日点间距 ds" << endl;
    cout << "      2) 直接使用文档推荐值" << endl;
  }

  cout << "局部厚度 drs_k 求解完成" << endl;

  // 步骤3: 均值化处理 (式2.24)
  cout << "\n步骤3: 对局部厚度进行均值化..." << endl;
  dfloat drs_sum = 0.0;
  dfloat drs_min = drs_local[0];
  dfloat drs_max = drs_local[0];

  for (int i = 0; i < np; i++) {
    drs_sum += drs_local[i];
    if (drs_local[i] < drs_min)
      drs_min = drs_local[i];
    if (drs_local[i] > drs_max)
      drs_max = drs_local[i];
  }

  dfloat drs_mean = drs_sum / np;
  dfloat drs_std = 0.0;
  for (int i = 0; i < np; i++) {
    dfloat diff = drs_local[i] - drs_mean;
    drs_std += diff * diff;
  }
  drs_std = sqrt(drs_std / np);

  // 输出统计信息
  cout << "\n局部厚度统计:" << endl;
  cout << "  平均值: " << setprecision(6) << drs_mean << " (= " << drs_mean / dx
       << "h)" << endl;
  cout << "  最小值: " << drs_min << " (= " << drs_min / dx << "h)" << endl;
  cout << "  最大值: " << drs_max << " (= " << drs_max / dx << "h)" << endl;
  cout << "  标准差: " << drs_std << " (= " << drs_std / dx << "h)" << endl;

  // 输出前10个局部值作为参考
  cout << "\n前10个拉格朗日点的局部drs值:" << endl;
  for (int i = 0; i < min(10, np); i++) {
    cout << "  点" << i << ": drs = " << setprecision(4) << drs_local[i]
         << " (= " << drs_local[i] / dx << "h)" << endl;
  }

  // 步骤4: 与理论值比较
  cout << "\n步骤4: 与文档推荐值比较:" << endl;
  dfloat drs_recommended;
  switch (deltaType) {
  case 1:
    drs_recommended = 1.4 * dx;
    break;
  case 3:
    drs_recommended = 2.6 * dx;
    break;
  case 2:
  default:
    drs_recommended = 1.9 * dx;
    break;
  }
  cout << "  文档推荐值: " << drs_recommended << " (= " << drs_recommended / dx
       << "h)" << endl;
  cout << "  计算均值: " << drs_mean << " (= " << drs_mean / dx << "h)" << endl;
  cout << "  相对误差: "
       << fabs(drs_mean - drs_recommended) / drs_recommended * 100 << "%"
       << endl;

  cout << "\n=== drs计算完成 ===" << endl;
  cout << "建议使用的drs值: " << drs_mean << " (或直接使用推荐值 "
       << drs_recommended << ")" << endl
       << endl;

  return drs_mean;
}

// 输出活跃的欧拉点数据
void outputActiveEulerPoints(const set<pair<int, int>> &activePoints,
                             int step) {
  std::filesystem::path p(output_dir);
  p /= std::string("active_euler_points_") + std::to_string(step) +
       std::string(".dat");
  ofstream out(p.string());

  out << "# 活跃的欧拉点数据文件 - 步数: " << step << endl;
  out << "# 活跃欧拉点数量: " << activePoints.size() << endl;
  out << "# 列1: x坐标" << endl;
  out << "# 列2: y坐标" << endl;
  out << "# 列3: 速度u" << endl;
  out << "# 列4: 速度v" << endl;
  out << "# 列5: 密度rho" << endl;
  out << "# 列6: 体积力Fx" << endl;
  out << "# 列7: 体积力Fy" << endl;
  out << "# x\ty\tu\tv\trho\tFx\tFy" << endl;

  for (const auto &point : activePoints) {
    int x = point.first;
    int y = point.second;
    out << x << "\t" << y << "\t" << points[x][y].u[0] << "\t"
        << points[x][y].u[1] << "\t" << points[x][y].rho << "\t"
        << points[x][y].Ft[0] << "\t" << points[x][y].Ft[1] << endl;
  }

  out.close();
  cout << "活跃欧拉点数据已写入文件: " << p.string() << endl;
}

// 构建插值矩阵 D_I
// D_I[j][i] = δ(x_i - X_j) * δ(y_i - Y_j)
// 其中 j 是拉格朗日点索引 (0~NL-1), i 是欧拉点索引 (0~NE-1)
// 矩阵大小: NL × NE
void buildInterpolationMatrix(vector<vector<dfloat>> &D_I,
                              vector<pair<int, int>> &eulerPoints) {
  // 首先收集所有活跃的欧拉点
  set<pair<int, int>> activeSet;

  for (int i = 0; i < np; i++) {
    dfloat xt = xl[i][0];
    dfloat yt = xl[i][1];

    // 遍历拉格朗日点周围的欧拉网格点
    for (int x = -2; x <= 2; x++) {
      for (int y = -2; y <= 2; y++) {
        int xx = int(xt) + x;
        int yy = int(yt) + y;

        // 处理x方向越界
        if (xx < 0)
          xx = NX - xx;
        else if (xx > LX)
          xx = xx - NX;

        // 处理y方向越界
        if (yy < 0)
          yy = -yy;
        else if (yy > LY)
          yy = 2 * NY - yy;

        // 确保在有效范围内
        xx = max(0, min(LX, xx));
        yy = max(0, min(LY, yy));

        // 计算delta函数值
        dfloat lx = xt - xx;
        dfloat ly = yt - yy;
        dfloat IB_Interp = delta3p(lx) * delta3p(ly);

        // 如果插值权重不为0，则记录这个欧拉点
        if (IB_Interp != 0.0) {
          activeSet.insert(make_pair(xx, yy));
        }
      }
    }
  }

  // 将set转换为vector，方便索引
  eulerPoints.clear();
  eulerPoints.assign(activeSet.begin(), activeSet.end());

  int NE = eulerPoints.size(); // 活跃欧拉点数量
  int NL = np;                 // 拉格朗日点数量

  cout << "\n=== 构建插值矩阵 D_I ===" << endl;
  cout << "拉格朗日点数量 NL = " << NL << endl;
  cout << "活跃欧拉点数量 NE = " << NE << endl;
  cout << "矩阵大小: " << NL << " × " << NE << endl;

  // 初始化矩阵 D_I (NL × NE)
  D_I.assign(NL, vector<dfloat>(NE, 0.0));

  // 构建矩阵元素
  for (int j = 0; j < NL; j++) {
    dfloat X_j = xl[j][0]; // 第j个拉格朗日点的x坐标
    dfloat Y_j = xl[j][1]; // 第j个拉格朗日点的y坐标
    for (int i = 0; i < NE; i++) {
      int x_i = eulerPoints[i].first;  // 第i个欧拉点的x坐标
      int y_i = eulerPoints[i].second; // 第i个欧拉点的y坐标

      // 计算距离
      dfloat dx_ij = x_i - X_j;
      dfloat dy_ij = y_i - Y_j;

      // 计算delta函数的乘积
      dfloat delta_x = delta3p(dx_ij);
      dfloat delta_y = delta3p(dy_ij);

      D_I[j][i] = delta_x * delta_y;
    }
  }

  // 统计非零元素
  int nonzero_count = 0;
  for (int j = 0; j < NL; j++) {
    for (int i = 0; i < NE; i++) {
      if (D_I[j][i] != 0.0) {
        nonzero_count++;
      }
    }
  }

  cout << "矩阵非零元素数量: " << nonzero_count << endl;
  cout << "矩阵稀疏度: " << setprecision(2) << fixed
       << 100.0 * (1.0 - (double)nonzero_count / (NL * NE)) << "%" << endl;
  cout << "=======================\n" << endl;

  // 输出前几行作为示例
  cout << "矩阵 D_I 的前5个拉格朗日点对应的行（前10列）:" << endl;
  for (int j = 0; j < min(5, NL); j++) {
    cout << "拉格朗日点 (" << xl[j][0] << "," << xl[j][1] << "): ";
    for (int i = 0; i < min(10, NE); i++) {
      cout << setprecision(4) << D_I[j][i] << " ";
    }
    if (NE > 10)
      cout << "...";
    cout << endl;
  }
  cout << endl;
}

// 构建外推（力分配）矩阵 D_E
// D_E[j][i] = δ(x_i - X_j) * δ(y_i - Y_j) * (drs * ds) / h^2
// 根据式(2.14): D_{E,ji} = (1/h^2) * δ((x_i-X_j)/h) * δ((y_i-Y_j)/h) * (drs*ds)
// 其中 j 是欧拉点索引 (1~NE), i 是拉格朗日点索引 (1~NL)
// 矩阵大小: NE × NL
void buildSpreadingMatrix(vector<vector<dfloat>> &D_E,
                          const vector<pair<int, int>> &eulerPoints) {
  int NE = eulerPoints.size(); // 活跃欧拉点数量
  int NL = np;                 // 拉格朗日点数量

  cout << "\n=== 构建外推矩阵 D_E ===" << endl;
  cout << "外推矩阵（力分配矩阵）" << endl;
  cout << "拉格朗日点数量 NL = " << NL << endl;
  cout << "活跃欧拉点数量 NE = " << NE << endl;
  cout << "矩阵大小: " << NE << " × " << NL << endl;

  // 打印参数
  cout << "固体边界厚度 drs = " << drs << endl;
  cout << "拉格朗日点间距 ds = " << ds << endl;
  cout << "欧拉网格步长 h = " << dx << endl;
  cout << "缩放因子 (drs*ds)/h^2 = " << (IB_weight) / (dx * dx) << endl;

  // 初始化矩阵 D_E (NE × NL)
  D_E.assign(NE, vector<dfloat>(NL, 0.0));

  // 计算缩放因子
  dfloat scale_factor = (IB_weight) / (dx * dx);

  // 构建矩阵元素
  for (int j = 0; j < NE; j++) {
    int x_j = eulerPoints[j].first;  // 第j个欧拉点的x坐标
    int y_j = eulerPoints[j].second; // 第j个欧拉点的y坐标

    for (int i = 0; i < NL; i++) {
      dfloat X_i = xl[i][0]; // 第i个拉格朗日点的x坐标
      dfloat Y_i = xl[i][1]; // 第i个拉格朗日点的y坐标

      // 计算距离（注意：按照式2.14，参数应除以h）
      dfloat dx_ji = (x_j - X_i) / dx;
      dfloat dy_ji = (y_j - Y_i) / dx;

      // 计算delta函数的乘积
      dfloat delta_x = delta3p(dx_ji);
      dfloat delta_y = delta3p(dy_ji);
      D_E[j][i] = delta_x * delta_y * scale_factor;
    }
  }

  // 统计非零元素
  int nonzero_count = 0;
  for (int j = 0; j < NE; j++) {
    for (int i = 0; i < NL; i++) {
      if (D_E[j][i] != 0.0) {
        nonzero_count++;
      }
    }
  }

  cout << "矩阵非零元素数量: " << nonzero_count << endl;
  cout << "矩阵稀疏度: " << setprecision(2) << fixed
       << 100.0 * (1.0 - (double)nonzero_count / (NE * NL)) << "%" << endl;
  cout << "=======================\n" << endl;

  // 输出前几行作为示例
  cout << "矩阵 D_E 的前5个欧拉点对应的行（前10列）:" << endl;
  for (int j = 0; j < min(5, NE); j++) {
    cout << "欧拉点 (" << eulerPoints[j].first << "," << eulerPoints[j].second
         << "): ";
    for (int i = 0; i < min(10, NL); i++) {
      cout << setprecision(6) << scientific << D_E[j][i] << " ";
    }
    if (NL > 10)
      cout << "...";
    cout << endl;
  }
  cout << endl;
}

// 输出插值矩阵到文件
void outputInterpolationMatrix(const vector<vector<dfloat>> &D_I,
                               const vector<pair<int, int>> &eulerPoints,
                               int step) {
  std::filesystem::path p(output_dir);
  p /= std::string("interpolation_matrix_") + std::to_string(step) +
       std::string(".dat");
  ofstream out(p.string());

  // 修正：D_I 是 NL × NE (拉格朗日点 × 欧拉点)
  int NL = D_I.size();                   // 第一维：拉格朗日点数量
  int NE = (NL > 0) ? D_I[0].size() : 0; // 第二维：欧拉点数量

  out << "# 插值矩阵 D_I - 步数: " << step << endl;
  out << "# 矩阵大小: NL × NE = " << NL << " × " << NE << endl;
  out << "# D_I[j][i] = δ(x_i - X_j) * δ(y_i - Y_j)" << endl;
  out << "# 其中 j 是拉格朗日点索引, i 是欧拉点索引" << endl;
  out << "#" << endl;
  out << "# 格式: 稠密矩阵，每行对应一个拉格朗日点" << endl;
  out << "#" << endl;

  // 输出欧拉点坐标映射
  out << "# 欧拉点坐标列表 (索引: x y):" << endl;
  for (int i = 0; i < NE; i++) {
    out << "# " << i << ": " << eulerPoints[i].first << " "
        << eulerPoints[i].second << endl;
  }
  out << "#" << endl;

  // 输出矩阵数据：遍历拉格朗日点（行）× 欧拉点（列）
  out << "# 矩阵数据开始" << endl;
  for (int j = 0; j < NL; j++) {   // j: 拉格朗日点索引
    for (int i = 0; i < NE; i++) { // i: 欧拉点索引
      out << setprecision(8) << scientific << D_I[j][i];
      if (i < NE - 1)
        out << "\t";
    }
    out << endl;
  }

  out.close();
  cout << "插值矩阵已写入文件: " << p.string() << endl;

  // 同时输出稀疏格式（只保存非零元素）
  std::filesystem::path p_sparse(output_dir);
  p_sparse /= std::string("interpolation_matrix_sparse_") +
              std::to_string(step) + std::string(".dat");
  ofstream out_sparse(p_sparse.string());

  out_sparse << "# 插值矩阵 D_I (稀疏格式) - 步数: " << step << endl;
  out_sparse << "# 格式: j i D_I[j][i] (只包含非零元素)" << endl;
  out_sparse << "# j: 拉格朗日点索引, i: 欧拉点索引" << endl;
  out_sparse << "# j\ti\tD_I[j][i]\tX_lagrange\tY_lagrange\tx_euler\ty_euler"
             << endl;

  for (int j = 0; j < NL; j++) {   // j: 拉格朗日点索引
    for (int i = 0; i < NE; i++) { // i: 欧拉点索引
      if (D_I[j][i] != 0.0) {
        out_sparse << j << "\t" << i << "\t" << setprecision(8) << scientific
                   << D_I[j][i] << "\t" << xl[j][0] << "\t" << xl[j][1] << "\t"
                   << eulerPoints[i].first << "\t" << eulerPoints[i].second
                   << endl;
      }
    }
  }

  out_sparse.close();
  cout << "稀疏格式插值矩阵已写入文件: " << p_sparse.string() << endl;
}

// 输出外推矩阵到文件
void outputSpreadingMatrix(const vector<vector<dfloat>> &D_E,
                           const vector<pair<int, int>> &eulerPoints,
                           int step) {
  std::filesystem::path p(output_dir);
  p /= std::string("spreading_matrix_") + std::to_string(step) +
       std::string(".dat");
  ofstream out(p.string());

  int NE = D_E.size();
  int NL = (NE > 0) ? D_E[0].size() : 0;

  out << "# 外推矩阵 D_E - 步数: " << step << endl;
  out << "# 矩阵大小: NE × NL = " << NE << " × " << NL << endl;
  out << "# D_E[j][i] = δ(x_j - X_i) * δ(y_j - Y_i) * (drs*ds/h^2)" << endl;
  out << "# 其中 j 是欧拉点索引, i 是拉格朗日点索引" << endl;
  out << "#" << endl;
  out << "# 格式: 稠密矩阵，每行对应一个欧拉点" << endl;
  out << "# 用于力分配: F_euler = D_E * F_lagrange" << endl;
  out << "#" << endl;

  // 输出欧拉点坐标映射
  out << "# 欧拉点坐标列表 (索引: x y):" << endl;
  for (int j = 0; j < NE; j++) {
    out << "# " << j << ": " << eulerPoints[j].first << " "
        << eulerPoints[j].second << endl;
  }
  out << "#" << endl;

  // 输出矩阵数据
  out << "# 矩阵数据开始" << endl;
  for (int j = 0; j < NE; j++) {
    for (int i = 0; i < NL; i++) {
      out << setprecision(8) << scientific << D_E[j][i];
      if (i < NL - 1)
        out << "\t";
    }
    out << endl;
  }

  out.close();
  cout << "外推矩阵已写入文件: " << p.string() << endl;

  // 同时输出稀疏格式
  std::filesystem::path p_sparse(output_dir);
  p_sparse /= std::string("spreading_matrix_sparse_") + std::to_string(step) +
              std::string(".dat");
  ofstream out_sparse(p_sparse.string());

  out_sparse << "# 外推矩阵 D_E (稀疏格式) - 步数: " << step << endl;
  out_sparse << "# 格式: j i D_E[j][i] (只包含非零元素)" << endl;
  out_sparse << "# j: 欧拉点索引, i: 拉格朗日点索引" << endl;
  out_sparse << "# j\ti\tD_E[j][i]\tx_euler\ty_euler\tX_lagrange\tY_lagrange"
             << endl;

  for (int j = 0; j < NE; j++) {
    for (int i = 0; i < NL; i++) {
      if (D_E[j][i] != 0.0) {
        out_sparse << j << "\t" << i << "\t" << setprecision(8) << scientific
                   << D_E[j][i] << "\t" << eulerPoints[j].first << "\t"
                   << eulerPoints[j].second << "\t" << xl[i][0] << "\t"
                   << xl[i][1] << endl;
      }
    }
  }

  out_sparse.close();
  cout << "稀疏格式外推矩阵已写入文件: " << p_sparse.string() << endl;
}

// 验证插值和外推矩阵的关系
void verifyInterpolationSpreading(const vector<vector<dfloat>> &D_I,
                                  const vector<vector<dfloat>> &D_E,
                                  const vector<pair<int, int>> &eulerPoints) {
  // 修正：D_I 是 NL × NE，D_E 是 NE × NL
  int NL = D_I.size();                   // 拉格朗日点数量
  int NE = (NL > 0) ? D_I[0].size() : 0; // 欧拉点数量

  cout << "\n=== 验证插值矩阵 D_I 和外推矩阵 D_E 的关系 ===" << endl;

  // 1. 检查矩阵大小是否匹配
  cout << "\n1. 矩阵大小检查:" << endl;
  cout << "   D_I 大小: " << NL << " × " << NE << " (拉格朗日 × 欧拉)" << endl;
  cout << "   D_E 大小: " << D_E.size() << " × "
       << (D_E.empty() ? 0 : D_E[0].size()) << " (欧拉 × 拉格朗日)" << endl;

  // D_I 是 NL × NE，D_E 是 NE × NL，它们的维度应该是转置关系
  if (D_E.size() != NE || (NE > 0 && D_E[0].size() != NL)) {
    cout << "   警告: 矩阵大小不匹配！" << endl;
    return;
  }

  // 2. 检查非零元素的位置是否对称（D_I[j][i] 对应 D_E[i][j]）
  cout << "\n2. 非零元素位置检查（转置关系）:" << endl;
  int mismatch_count = 0;
  for (int j = 0; j < NL; j++) {   // 拉格朗日点
    for (int i = 0; i < NE; i++) { // 欧拉点
      bool di_nonzero = (D_I[j][i] != 0.0);
      bool de_nonzero = (D_E[i][j] != 0.0);
      if (di_nonzero != de_nonzero) {
        mismatch_count++;
        if (mismatch_count <= 5) { // 只打印前5个不匹配
          cout << "   位置 D_I[" << j << "][" << i << "] vs D_E[" << i << "]["
               << j << "] 不匹配: D_I=" << (di_nonzero ? "非零" : "零")
               << ", D_E=" << (de_nonzero ? "非零" : "零") << endl;
        }
      }
    }
  }
  if (mismatch_count == 0) {
    cout << "   ✓ 非零元素位置完全一致" << endl;
  } else {
    cout << "   ✗ 发现 " << mismatch_count << " 个位置不匹配" << endl;
  }

  // 3. 计算D_E与D_I的比值（应该接近 drs*ds/h^2）
  cout << "\n3. 矩阵元素比值检查 (D_E / D_I):" << endl;
  dfloat expected_ratio = (drs * ds) / (dx * dx);
  cout << "   理论比值 (drs*ds/h^2) = " << setprecision(6) << fixed
       << expected_ratio << endl;

  vector<dfloat> ratios;
  for (int j = 0; j < NL; j++) {   // 拉格朗日点
    for (int i = 0; i < NE; i++) { // 欧拉点
      // 注意：D_I[j][i] 对应 D_E[i][j] (转置关系)
      if (D_I[j][i] != 0.0 && D_E[i][j] != 0.0) {
        dfloat ratio = D_E[i][j] / D_I[j][i];
        ratios.push_back(ratio);
      }
    }
  }

  if (!ratios.empty()) {
    dfloat sum_ratio = 0.0;
    for (auto r : ratios)
      sum_ratio += r;
    dfloat avg_ratio = sum_ratio / ratios.size();
    dfloat max_ratio = *max_element(ratios.begin(), ratios.end());
    dfloat min_ratio = *min_element(ratios.begin(), ratios.end());

    cout << "   实际比值统计:" << endl;
    cout << "     平均值: " << avg_ratio << endl;
    cout << "     最小值: " << min_ratio << endl;
    cout << "     最大值: " << max_ratio << endl;
    cout << "     与理论值误差: "
         << fabs(avg_ratio - expected_ratio) / expected_ratio * 100 << "%"
         << endl;

    if (fabs(avg_ratio - expected_ratio) / expected_ratio < 0.01) {
      cout << "   ✓ 比值关系正确 (误差 < 1%)" << endl;
    } else {
      cout << "   ✗ 比值关系存在偏差" << endl;
    }
  }

  // 4. 检查是否满足转置关系的性质
  // 注意：D_E 不是严格的 D_I 转置，但非零元素模式应该相同
  cout << "\n4. 结构对称性检查:" << endl;
  cout << "   D_E ≈ D_I × (drs*ds/h^2)" << endl;
  cout << "   非零模式应相同" << endl;

  // 5. 计算矩阵乘积 A = D_I * D_E^T（用于drs计算）
  cout << "\n5. 矩阵乘积 A = D_I * D_E^T 的性质:" << endl;
  cout << "   这个矩阵用于边界增厚法计算 drs" << endl;
  cout << "   A 的大小应该是: " << NE << " × " << NE << endl;

  // 计算对角线元素的样本
  cout << "   A 的对角线元素（前5个）:" << endl;
  for (int k = 0; k < min(5, NE); k++) {
    dfloat diag_elem = 0.0;
    for (int m = 0; m < NL; m++) {
      diag_elem += D_I[k][m] * D_E[k][m]; // 因为是 D_E^T，所以是 D_E[k][m]
    }
    cout << "     A[" << k << "][" << k << "] = " << scientific << diag_elem
         << endl;
  }

  cout << "\n==============================\n" << endl;
}
