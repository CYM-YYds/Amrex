#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace std;

// 可配置的输出目录（默认在可执行文件同级的 results 目录）
std::string output_dir = "Cylinder_IBM";

typedef double dfloat;

// 格子模型基本参数
#define Q 9 // 离散速度方向数量(D2Q9)
#define D 40
#define NX (10 * D) // x方向格点数
#define NY (10 * D) // y方向格点数
#define R (D / 2.0)
#define LX (NX - 1)
#define LY (NY - 1)

#define c 1.0               // 格子速度
#define cs (c / sqrt(3.0))  // 格子声速
#define cs_square (cs * cs) // 声速平方
#define dx 1.0              // 空间步长
#define dt (dx / c)         // 时间步长

// 拉格朗日点相关参数
#define deltal (1.0) // 拉格朗日点之间的距离
#define PI (3.141592653589793238462643383279)
#define np ((int)((2.0 * PI * R) / deltal)) // 拉格朗日点数量
#define xc (NX / 2.0)
#define yc (NY / 2.0)
dfloat xl[np][2];  // 拉格朗日点坐标
dfloat fxl[np][2]; // 拉格朗日点力

#define drs (1.95) // 固体边界厚度，DF为1.0，3p函数为1.95,4p函数为2.6
#define ds0 deltal            // 表面拉氏点的距离
#define IB_weight (ds0 * drs) // IBM插值权重

// Delta函数选择: 2, 3, 或 4
#define DELTA_TYPE 3

// 根据Delta函数类型自动确定支撑范围
#if DELTA_TYPE == 2
#define DELTA_SUPPORT 1 // delta2p: [-1, 1]
#define DELTA_FUNC delta2p
#elif DELTA_TYPE == 3
#define DELTA_SUPPORT 2 // delta3p: [-1.5, 1.5] → [-2, 2]
#define DELTA_FUNC delta3p
#elif DELTA_TYPE == 4
#define DELTA_SUPPORT 2 // delta4p: [-2, 2]
#define DELTA_FUNC delta4p
#elif DELTA_TYPE == 5
#define DELTA_SUPPORT 2 // delta5p: [-2, 2] (Peskin版本)
#define DELTA_FUNC delta5p
#else
#error "DELTA_TYPE must be 2, 3, 4, or 5"
#endif

// 计算实际的流动区域高度（上下IB边界之间的距离）
#define flow_height (LY) // 实际流动区域高度（上下IB边界之间的距离）
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

// IBM强制方案选择：
// 0 = 原BTDF：在拉格朗日边界点计算力，再用delta函数扩散到欧拉网格。
// 1 = 论文3.2节思想的MLS直接强制：只在圆柱外侧界面流体点计算期望速度和体积力。
#define IBM_FORCE_SCHEME 1
#define IBM_SCHEME_BTDF 0
#define IBM_SCHEME_MLS_DIRECT 1

// MLS直接强制参数。论文中d通常取dx到2dx；这里用2dx作为界面流体层厚度。
#define MLS_INTERFACE_DISTANCE (2.0 * dx)
#define MLS_SUPPORT_RADIUS (2.0 * dx)
#define MLS_MIN_WEIGHT (1.0e-14)
#define MLS_DIRECT_FORCE_FACTOR 1.0

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

// 函数声明
dfloat feq(int k, dfloat rho, dfloat u[2]);
dfloat forceGuo(int i, dfloat u[2], dfloat Ft[2]);
dfloat delta2p(dfloat r);
dfloat delta3p(dfloat r);
dfloat delta4p(dfloat r);
dfloat delta5p(dfloat r);
void initialize();
void initializeParticle();
void stream();
void computeMacroscopic();
void applyBoundaryConditions();
void Circle_IBM();
void Circle_IBM_BTDF();
void Circle_IBM_MLSDirect();
void collision_fluid();
void output(int m);
void outputVelocityProfile(int step);
dfloat calculateMeanVelocity(int component);
bool checkConvergence(int step, dfloat &conv);
void Reducefxl(int step);

// 主函数
int main(int argc, char *argv[]) {
  // 打印体积力参数
  cout << "=== 模拟参数信息 ===" << endl;
  cout << "雷诺数 Re = " << Re << endl;
  cout << "粘度 niu = " << niu << endl;
  cout << "松弛时间 tau_f = " << tau_f << endl;
  cout << "体积力 fx = " << fx << endl;
  cout << "体积力 fy = " << fy << endl;
  cout << "IBM_FORCE_SCHEME = "
       << (IBM_FORCE_SCHEME == IBM_SCHEME_MLS_DIRECT ? "MLS_DIRECT" : "BTDF")
       << endl;
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
  initializeParticle();

  // 演化循环
  for (int step = 0; step < Maxstep; step++) {
    // 流动步骤
    stream();

    // 计算宏观量
    computeMacroscopic();

    // 应用边界条件
    applyBoundaryConditions();

    Circle_IBM();

    // 计算并输出Cd/Cl
    Reducefxl(step);

    // 碰撞步骤
    collision_fluid();

    // 收敛性检查
    dfloat conv;
    if (checkConvergence(step, conv)) {
      output(step);
      // outputVelocityProfile(step);
      break; // 跳出循环，模拟结束
    }

    // 输出结果
    if (step % OUT == 0) {
      cout << "The " << step << " the computation result:" << endl
           << "The mean_u is:" << setprecision(6) << mean_u << endl
           << "The mean_u_old is:" << setprecision(6) << mean_u_old << endl
           << "The conv is:" << setprecision(6) << conv << endl;
      output(step);

      // 每OUT步输出一次速度剖面数据
      if (step == Maxstep - 1) {
        // outputVelocityProfile(step);
      }
    }
  }

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

void initializeParticle() {
  // 初始化拉格朗日点坐标
  double sita;
  for (int i = 0; i < np; i++) {
    sita = (i * 2.0 * PI / np);
    xl[i][0] = xc + R * cos(sita);
    xl[i][1] = yc + R * sin(sita);
  }
  // 打印所有拉格朗日点的坐标
  // cout << "=== 拉格朗日点坐标信息 ===" << endl;
  // cout << "拉格朗日点总数: " << np << endl;
  // cout << "点间距 deltal: " << deltal << endl;
  // cout << "参考距离 Nd: " << Nd << endl;
  // cout << "----------------------------" << endl;
  // for (int i = 0; i < np; i++) {
  //   cout << "点 " << setw(3) << i << ": (" << setprecision(4) << fixed
  //        << setw(8) << xl[i][0] << ", " << setw(8) << xl[i][1] << ")";
  //   if (i < (np / 2)) {
  //     cout << "  [上边界]";
  //   } else {
  //     cout << "  [下边界]";
  //   }
  //   cout << endl;
  // }
  // cout << "============================" << endl << endl;
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
    dfloat ybottom = 0.0;
    dfloat ytop = LY;
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

// MDF迭代次数（可调整，NF=1为原始IBM，NF>1为多重迭代）
#define NF 1

void Circle_IBM() {
#if IBM_FORCE_SCHEME == IBM_SCHEME_MLS_DIRECT
  Circle_IBM_MLSDirect();
#elif IBM_FORCE_SCHEME == IBM_SCHEME_BTDF
  Circle_IBM_BTDF();
#else
#error "Unsupported IBM_FORCE_SCHEME"
#endif
}

// 原BTDF方法保留：边界点上求力，再通过delta函数扩散到欧拉网格。
// 注意：不要删除该函数；它是和论文MLS直接强制方法对比的基准实现。
void Circle_IBM_BTDF() {
  // 辅助函数：处理欧拉网格点的周期/镜像边界条件
  auto applyBoundary = [](int xx, int yy) -> std::pair<int, int> {
    // 处理x方向周期性边界
    if (xx < 0)
      xx = NX - xx;
    else if (xx > LX)
      xx = xx - NX;

    // 处理y方向镜像反射边界
    if (yy < 0)
      yy = -yy;
    else if (yy > LY)
      yy = 2 * NY - yy;

    // 确保在有效范围内
    xx = max(0, min(LX, xx));
    yy = max(0, min(LY, yy));

    return {xx, yy};
  };

  // ========== 初始化：清零欧拉力场和粒子力 ==========
  for (int i = 0; i <= LX; i++) {
    for (int j = 0; j <= LY; j++) {
      points[i][j].Ft[0] = 0.0;
      points[i][j].Ft[1] = 0.0;
    }
  }
  for (int i = 0; i < np; i++) {
    fxl[i][0] = 0.0;
    fxl[i][1] = 0.0;
  }

  // 遍历每个拉格朗日点
  for (int i = 0; i < np; i++) {
    dfloat xt = xl[i][0];
    dfloat yt = xl[i][1];

    // 拉格朗日点的速度和密度（插值结果）
    dfloat uxt = 0.0;
    dfloat uyt = 0.0;
    dfloat rhot = 0.0;

    // 边界点速度（对于固定边界为0）
    dfloat upx = 0.0;
    dfloat upy = 0.0;

    // ========== 步骤1: 欧拉→拉格朗日插值（流程图步骤 b, f） ==========
    // 使用自适应循环范围（根据DELTA_TYPE自动调整）
    for (int x = -DELTA_SUPPORT; x <= DELTA_SUPPORT; x++) {
      for (int y = -DELTA_SUPPORT; y <= DELTA_SUPPORT; y++) {
        int xx = int(xt) + x;
        int yy = int(yt) + y;

        // 应用边界条件
        auto [xx_bc, yy_bc] = applyBoundary(xx, yy);

        // 计算距离和插值权重
        dfloat lx = xt - xx;
        dfloat ly = yt - yy;
        dfloat IB_Interp = DELTA_FUNC(lx) * DELTA_FUNC(ly);

        // MDF关键：使用力修正后的速度（流程图步骤 e）
        // u^(m) = u^(0) + Δt/(2ρ) * F_累积
        dfloat force_to_vel = 0.5 * dt / points[xx_bc][yy_bc].rho;
        dfloat u_corr_x = points[xx_bc][yy_bc].u[0] +
                          force_to_vel * points[xx_bc][yy_bc].Ft[0];
        dfloat u_corr_y = points[xx_bc][yy_bc].u[1] +
                          force_to_vel * points[xx_bc][yy_bc].Ft[1];

        // 累加插值（使用修正后的速度）
        uxt += u_corr_x * IB_Interp;
        uyt += u_corr_y * IB_Interp;
        rhot += points[xx_bc][yy_bc].rho * IB_Interp;
      }
    }

    // ========== 步骤2: 计算IBM力（流程图步骤 c） ==========
    // F_b^(m) = 2ρ(U_b - u_b^(m-1)) / Δt
    dfloat fxt = 2.0 * rhot / dt * (uxt - upx);
    dfloat fyt = 2.0 * rhot / dt * (uyt - upy);

    // ========== 步骤3: 拉格朗日→欧拉外推（流程图步骤 d） ==========
    // F_ij^(m) = Σ F_b^(m) * D(x_ij - x_b) * Δs_b
    for (int x = -DELTA_SUPPORT; x <= DELTA_SUPPORT; x++) {
      for (int y = -DELTA_SUPPORT; y <= DELTA_SUPPORT; y++) {
        int xx = int(xt) + x;
        int yy = int(yt) + y;

        // 应用边界条件
        auto [xx_bc, yy_bc] = applyBoundary(xx, yy);

        // 计算距离和外推权重
        dfloat lx = xt - xx;
        dfloat ly = yt - yy;
        dfloat IB_Interp = DELTA_FUNC(lx) * DELTA_FUNC(ly) * IB_weight;

        // 分配力到欧拉网格点（累加）
        points[xx_bc][yy_bc].Ft[0] -= fxt * IB_Interp;
        points[xx_bc][yy_bc].Ft[1] -= fyt * IB_Interp;
      }
    }

    // 累加粒子力（流程图最终：F_b = Σ F_b^(m)）
    fxl[i][0] += fxt * IB_weight * dx * dx;
    fxl[i][1] += fyt * IB_weight * dx * dx;
  }
}

// 论文3.2节MLS direct forcing的单CPU网格版教学实现。
//
// 和上面的BTDF不同：
// - BTDF：在拉格朗日边界点上算F_b，再把F_b通过delta函数扩散到欧拉点。
// - MLS direct forcing：先找界面流体点i，在这些点上用MLS计算期望速度u_d，
//   然后直接在界面点上计算 f_i = rho_i * (u_d - u_i*) / dt。
//
// 这里的近似：
// - 论文是SPH粒子法；本文件是欧拉网格LBM，所以“流体粒子”用网格点代替。
// - 只处理圆柱外侧 0 <= signed_distance <= d
// 的界面网格点，避免圆柱内外两侧同时受同一边界点扩散影响。
// - MLS数据源由支撑域内的内层流体网格点(phi >
// d)和拉格朗日边界点组成；边界点速度为0。
/**
 * @brief 使用移动最小二乘法(MLS)直接施加圆IBM力的函数
 * 该函数通过MLS方法计算界面附近的流体点所受的力，并施加到流体点上
 */
/**
 * @brief 使用移动最小二乘法(MLS)直接施加圆周力场的函数
 * 该函数用于在流体模拟中施加圆周力场，实现圆周运动的直接数值模拟
 */
void Circle_IBM_MLSDirect() {
  // 边界条件处理函数，确保坐标在有效范围内
  auto applyBoundary = [](int xx, int yy) -> std::pair<int, int> {
    // 处理x方向边界条件：周期性边界
    if (xx < 0)
      xx += NX;
    else if (xx > LX)
      xx -= NX;

    // 处理y方向边界条件：反射边界
    if (yy < 0)
      yy = -yy;
    else if (yy > LY)
      yy = 2 * NY - yy;

    // 确保坐标在有效范围内
    xx = max(0, min(LX, xx));
    yy = max(0, min(LY, yy));
    return {xx, yy};
  };

  // 计算点到圆的符号距离函数
  auto signedDistance = [](dfloat x, dfloat y) -> dfloat {
    dfloat rx = x - xc;                 // x方向相对圆心距离
    dfloat ry = y - yc;                 // y方向相对圆心距离
    return sqrt(rx * rx + ry * ry) - R; // 返回点到圆的距离（圆内为负）
  };

  // 计算最近的拉格朗日点索引
  auto nearestLagIndex = [](dfloat x, dfloat y) -> int {
    // 计算相对于圆心的角度
    dfloat theta = atan2(y - yc, x - xc);
    if (theta < 0.0)
      theta += 2.0 * PI; // 将角度转换为[0, 2π]范围
    // 计算最近的拉格朗日点索引
    int idx = static_cast<int>(theta / (2.0 * PI) * np + 0.5);
    if (idx >= np)
      idx -= np; // 确保索引在有效范围内
    return idx;
  };

  // MLS核函数计算
  auto mlsKernel = [](dfloat dxij, dfloat dyij) -> dfloat {
    dfloat wx = DELTA_FUNC(dxij / dx); // x方向核函数值
    dfloat wy = DELTA_FUNC(dyij / dx); // y方向核函数值
    return wx * wy;                    // 返回乘积作为核函数值
  };

  // 3x3矩阵求逆函数
  auto invert3x3 = [](const dfloat A[3][3], dfloat invA[3][3]) -> bool {
    // 计算行列式
    dfloat det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) -
                 A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
                 A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);

    // 如果行列式接近0，矩阵不可逆
    if (fabs(det) < 1.0e-14)
      return false;

    // 计算逆矩阵
    dfloat inv_det = 1.0 / det;
    invA[0][0] = (A[1][1] * A[2][2] - A[1][2] * A[2][1]) * inv_det;
    invA[0][1] = (A[0][2] * A[2][1] - A[0][1] * A[2][2]) * inv_det;
    invA[0][2] = (A[0][1] * A[1][2] - A[0][2] * A[1][1]) * inv_det;
    invA[1][0] = (A[1][2] * A[2][0] - A[1][0] * A[2][2]) * inv_det;
    invA[1][1] = (A[0][0] * A[2][2] - A[0][2] * A[2][0]) * inv_det;
    invA[1][2] = (A[0][2] * A[1][0] - A[0][0] * A[1][2]) * inv_det;
    invA[2][0] = (A[1][0] * A[2][1] - A[1][1] * A[2][0]) * inv_det;
    invA[2][1] = (A[0][1] * A[2][0] - A[0][0] * A[2][1]) * inv_det;
    invA[2][2] = (A[0][0] * A[1][1] - A[0][1] * A[1][0]) * inv_det;
    return true;
  };

  // MLS源点结构体，包含位置、速度和权重
  struct MlsSource {
    dfloat x;  // x坐标
    dfloat y;  // y坐标
    dfloat ux; // x方向速度
    dfloat uy; // y方向速度
    dfloat w0; // 权重
  };

  // 初始化流体力数组
  for (int i = 0; i <= LX; i++) {
    for (int j = 0; j <= LY; j++) {
      points[i][j].Ft[0] = 0.0; // x方向流体力初始化
      points[i][j].Ft[1] = 0.0; // y方向流体力初始化
    }
  }
  // 初始化升力/阻力数组
  for (int p = 0; p < np; p++) {
    fxl[p][0] = 0.0; // x方向力初始化
    fxl[p][1] = 0.0; // y方向力初始化
  }

  // 计算支撑域大小
  const int support_cells = static_cast<int>(ceil(MLS_SUPPORT_RADIUS / dx)) + 1;

  // 遍历所有网格点
  for (int i = 0; i <= LX; i++) {
    for (int j = 0; j <= LY; j++) {
      // 计算当前点的符号距离
      dfloat phi_i = signedDistance(i, j);
      // 如果点不在界面附近，跳过
      if (phi_i < 0.0 || phi_i > MLS_INTERFACE_DISTANCE)
        continue;

      // 存储MLS源点的容器
      std::vector<MlsSource> sources;
      sources.reserve(64);

      // 内层流体点：圆柱外侧、且不在界面强制层内的网格点。
      for (int di = -support_cells; di <= support_cells; di++) {
        for (int dj = -support_cells; dj <= support_cells; dj++) {
          int ii = i + di;
          int jj = j + dj;
          auto [ii_bc, jj_bc] = applyBoundary(ii, jj);

          dfloat rx = i - ii;
          dfloat ry = j - jj;
          if (rx * rx + ry * ry >
              MLS_SUPPORT_RADIUS *
                  MLS_SUPPORT_RADIUS) // 避免sqrt计算，直接比较距离平方和支持域半径平方
            continue;

          dfloat phi_j = signedDistance(ii_bc, jj_bc);
          if (phi_j <= MLS_INTERFACE_DISTANCE)
            continue;

          dfloat w0 = mlsKernel(rx, ry);
          if (w0 <= MLS_MIN_WEIGHT)
            continue;

          sources.push_back({static_cast<dfloat>(ii), static_cast<dfloat>(jj),
                             points[ii_bc][jj_bc].u[0],
                             points[ii_bc][jj_bc].u[1], w0});
        }
      }

      // 结构边界点：速度为静止壁面速度(0,0)。
      for (int p = 0; p < np; p++) {
        dfloat rx = i - xl[p][0];
        dfloat ry = j - xl[p][1];
        if (rx * rx + ry * ry > MLS_SUPPORT_RADIUS * MLS_SUPPORT_RADIUS)
          continue;

        dfloat w0 = mlsKernel(rx, ry);
        if (w0 <= MLS_MIN_WEIGHT)
          continue;

        sources.push_back({xl[p][0], xl[p][1], 0.0, 0.0, w0});
      }

      if (sources.size() < 3)
        continue;

      dfloat A[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
      for (const auto &src : sources) {
        dfloat basis[3] = {1.0, i - src.x, j - src.y};
        for (int r = 0; r < 3; r++) {
          for (int col = 0; col < 3; col++) {
            A[r][col] += basis[r] * basis[col] * src.w0;
          }
        }
      }

      dfloat invA[3][3];
      dfloat udx = 0.0;
      dfloat udy = 0.0;

      if (invert3x3(A, invA)) {
        // 2D MLS权重：W_j^MLS = [1,0,0] A^{-1} [1, x_ij, y_ij]^T W_j
        for (const auto &src : sources) {
          dfloat basis[3] = {1.0, i - src.x, j - src.y};
          dfloat beta_dot_basis = invA[0][0] * basis[0] +
                                  invA[0][1] * basis[1] + invA[0][2] * basis[2];
          dfloat w_mls = beta_dot_basis * src.w0;
          udx += w_mls * src.ux;
          udy += w_mls * src.uy;
        }
      } else {
        // 支撑域退化时保守回退到归一化核插值，避免单CPU测试直接中断。
        dfloat wsum = 0.0;
        for (const auto &src : sources) {
          udx += src.w0 * src.ux;
          udy += src.w0 * src.uy;
          wsum += src.w0;
        }
        if (wsum <= MLS_MIN_WEIGHT)
          continue;
        udx /= wsum;
        udy /= wsum;
      }

      // 论文式直接强制：f_i = rho_i * (u_d - u_i*) / dt。
      // 这里u_i*取当前宏观速度；如果以后加入半步预测，可在这里替换。
      dfloat ftx = MLS_DIRECT_FORCE_FACTOR * points[i][j].rho *
                   (udx - points[i][j].u[0]) / dt;
      dfloat fty = MLS_DIRECT_FORCE_FACTOR * points[i][j].rho *
                   (udy - points[i][j].u[1]) / dt;

      points[i][j].Ft[0] += ftx;
      points[i][j].Ft[1] += fty;

      // 输出Cd/Cl仍沿用fxl数组：把流体所受力的反作用力记到最近边界点。
      int p_near = nearestLagIndex(i, j);
      fxl[p_near][0] -= ftx * dx * dx;
      fxl[p_near][1] -= fty * dx * dx;
    }
  }
}

dfloat delta2p(dfloat r) {
  r = fabs(r);
  if (r <= 1.0)
    return (1.0 - r);
  else
    return 0.0;
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

dfloat delta4p(dfloat r) {
  r = fabs(r);
  if (r <= 1.0)
    return (3.0 - 2.0 * r + sqrt(1.0 + 4.0 * r - 4.0 * r * r)) / 8.0;
  else if (r <= 2.0)
    return (5.0 - 2.0 * r - sqrt(-7.0 + 12.0 * r - 4.0 * r * r)) / 8.0;
  else
    return 0.0;
}

dfloat delta5p(dfloat r) {
  r = fabs(r);

  // 5点delta函数（支撑范围 [-2.5, 2.5]）
  // 修正版本：确保在分段点处连续
  // 参考: Peskin (2002), "The immersed boundary method"

  if (r <= 1.0) {
    // 内部区域 |r| <= 1
    dfloat r2 = r * r;
    return (1.0 / 12.0) *
           (5.0 - 3.0 * r - sqrt(-3.0 * (1.0 - r) * (1.0 - r) + 1.0));
  } else if (r <= 2.0) {
    // 中间区域 1 < |r| <= 2
    dfloat r2 = r * r;
    return (1.0 / 12.0) * (3.0 - r + sqrt(-3.0 * (2.0 - r) * (2.0 - r) + 1.0));
  } else {
    // 外部区域 |r| > 2
    return 0.0;
  }
}

// 备用版本（多项式形式，如果上面的平方根版本有问题）
// dfloat delta5p(dfloat r) {
//   r = fabs(r);
//
//   if (r <= 0.5)
//     return (115.0 - 240.0 * r * r + 144.0 * r * r * r * r) / 192.0;
//   else if (r <= 1.5)
//     return (115.0 - 120.0 * r - 240.0 * r * r + 240.0 * r * r * r - 64.0 * r
//     * r * r * r) / 384.0;
//   else if (r <= 2.5)
//     return (1575.0 - 2880.0 * r + 1920.0 * r * r - 512.0 * r * r * r + 64.0 *
//     r * r * r * r) / 768.0;
//   else
//     return 0.0;
// }

// 计算并输出阻力系数和升力系数
void Reducefxl(int step) {
  // 累加所有拉格朗日点的力
  dfloat fx_sum = 0.0;
  dfloat fy_sum = 0.0;

  for (int i = 0; i < np; i++) {
    fx_sum += fxl[i][0];
    fy_sum += fxl[i][1];
  }

  // 计算阻力系数和升力系数
  // Cd = Fx / (0.5 * rho * U^2 * D)
  // Cl = Fy / (0.5 * rho * U^2 * D)
  // 在LBM中，参考面积为 D * 1（单位宽度）
  dfloat ref_force = 0.5 * rho0 * U * U * D * dx; // 参考力

  dfloat Cd = fx_sum / ref_force;
  dfloat Cl = fy_sum / ref_force;

  // 输出到控制台
  cout << "Step " << step << ": Cd = " << setprecision(6) << Cd
       << ", Cl = " << setprecision(6) << Cl << " (fx_sum = " << fx_sum
       << ", fy_sum = " << fy_sum << ")" << endl;

  // 输出到文件
  static bool first_write = true;
  std::filesystem::path p(output_dir);
  p /= "CdCl.dat";

  ofstream out;
  if (first_write) {
    out.open(p.string(), ios::trunc);
    out << "# Step\tCd\tCl\tfx_sum\tfy_sum" << endl;
    first_write = false;
  } else {
    out.open(p.string(), ios::app);
  }

  out << step << "\t" << setprecision(10) << Cd << "\t" << Cl << "\t" << fx_sum
      << "\t" << fy_sum << endl;
  out.close();
}
