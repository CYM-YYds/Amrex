#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <system_error>

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
