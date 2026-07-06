# 壁面模型 AI 实现手册

本文是给 AI 直接执行代码修改用的手册。范围只覆盖当前算例：

```text
projects/3Dcases/ChannelFlowtest/9_LES
```

本轮只实现三维槽道流上下壁面的 `NF=1` MDF 壁面模型版本，不处理 `NF>1`
两阶段迭代，不实现完整三维切平面分解。

## 0. 当前实现状态

截至 2026-07-06，本文描述的 `NF=1` 第一版已经在当前 case 中落地：

- `src/AmrCoreLBM.cpp` 的 `USE_MDF_TWO_STAGE=0` 分支调用
  `particles[i]->InterpForceWallModel(lev, rho_lev, u_lev, force_lev)`。
- `src/Kernels.H::force_wall_model()` 已改为先构造壁面模型目标速度 `UBR`，
  再使用当前 MDF 符号约定 `f = rhot * (uLag - UBR)` 扩散力。
- `src/Kernels.H::compute_eta_c_apgpl()` 已按 APGPL 连续性方程动态求解
  `eta_c`，使用初值 `11.81`、区间 `[11.78, 2.0 * 11.81]` 和最多 5 次
  牛顿加区间保护迭代。
- 旧的 `xq/xRef/tw/fTang` 直接剪切应力体力逻辑已删除。
- 结构检查脚本为 `scripts/check_wall_model_nf1.sh`。
- 编译验证命令为 `./scripts/compile.sh`，已生成 `main3d.gnu.MPI.CUDA.ex`。

本文后续章节仍保留为实现与审查手册；如果继续扩展 `NF>1` 或完整三维切平面分解，
应先更新本文范围和成功标准。

## 1. 当前代码入口

已确认当前配置：

```cpp
// src/D3Q19.H
#define NF 1
```

普通 MDF 路径的历史形式：

```text
AmrCoreLBM::ComputeParticle()
  -> AmrCoreLBM::InterpForce()
     -> NF=1 分支
        -> particles[i]->InterpForce(lev, rho_lev, u_lev, force_lev)
        -> Kernels.H::force_interp_extrap(...)
```

当前 `NF=1` 壁面模型路径：

```text
AmrCoreLBM::ComputeParticle()
  -> AmrCoreLBM::InterpForce()
     -> NF=1 分支
        -> particles[i]->InterpForceWallModel(lev, rho_lev, u_lev, force_lev)
        -> Kernels.H::force_wall_model(...)
```

`USE_MDF_TWO_STAGE` 分支不要改。

## 2. 文件修改范围

只修改这些文件：

```text
src/AmrCoreLBM.cpp
src/Kernels.H
```

通常不需要修改：

```text
src/LagrangeParticleContainer.H
src/LagrangeParticleContainer.cpp
src/D3Q19.H
```

原因：`InterpForceWallModel()` 接口已经存在，且当前 `NF=1` 已由 `D3Q19.H` 设置。

## 3. AMReX 数据接口

保留现有接口：

```cpp
void LagrangeParticleContainer::InterpForceWallModel(
    int lev,
    amrex::MultiFab& rho_lev,
    amrex::MultiFab& u_lev,
    amrex::MultiFab& force_lev);
```

在 `LagrangeParticleContainer.cpp` 中，kernel 已经拿到：

```cpp
const Array4<Real>& u = u_lev.array(pti);
const Array4<Real>& rho = rho_lev.array(pti);
const Array4<Real>& Ft = force_lev.array(pti);
```

不要新增 `grad_p` MultiFab。压力用 LBM 关系局部计算：

```cpp
p = cs2 * rho
```

## 4. 简化模型约定

即使当前是三维算例，也只分解成两个分量：

```text
法向 e_eta
切向 e_xi
```

不保留第二切向分量。

采样距离：

```cpp
etaP = 3.0 * delta;
dpdxi_half_step = delta;
```

其中：

```cpp
delta = Geom(lev).CellSize()[0]
```

`etaP` 是沿法向从壁面点 $B$ 到探测点 $P$ 的距离。`dpdxi_half_step` 是沿切向
$\mathbf{e}_{\xi}$ 做压力中心差分时，从 $P$ 到 $P_+$ 或 $P_-$ 的半步长：

```text
P_plus  = P + dpdxi_half_step * e_xi
P_minus = P - dpdxi_half_step * e_xi
```

两者不是同一个物理量。第一版取 `dpdxi_half_step = delta`，意思是用一个局部欧拉网格尺度做切向压力梯度差分。

当前 Lagrangian 点由 `InitChannelParticle()` 铺在上下壁面：

```cpp
y_bot_idx = 3.5;
y_top_idx = NY - 3.5;
```

因此本手册针对的是平面槽道壁面，不是球/圆柱颗粒表面。不要用
`normalize(x_local, y_local, z_local)` 作为法向；那只适用于从颗粒中心指向表面的几何。

槽道壁面法向应直接由壁面位置判断：

```text
下壁面: e_eta = (0,  1, 0)，从下壁面指向流体内部
上壁面: e_eta = (0, -1, 0)，从上壁面指向流体内部
```

实现时推荐用 Lagrangian 点的物理 `y` 坐标判断上下壁：

```cpp
const amrex::Real y_mid = 0.5 * (3.5 + (NY - 3.5)) * dx_min;
const bool is_bottom_wall = (p.pos(1) < y_mid);

const amrex::Real nx = 0.0;
const amrex::Real ny = is_bottom_wall ? 1.0 : -1.0;
const amrex::Real nz = 0.0;
```

不要用 `ylocal` 的符号判断上下壁。当前 `InitChannelParticle()` 中 `p.pos(1)` 使用
`dx_min`，而 `ylocal = p.pos(1) - centre[1] * dx_0`，两者尺度不同，`ylocal`
不能可靠表示壁面相对槽道中心的位置。

壁面速度沿用当前 MDF 公式：

```cpp
UBx = uc[0] + wc[2] * (-y_local) + wc[1] * z_local;
UBy = uc[1] + wc[2] * x_local + wc[0] * (-z_local);
UBz = uc[2] + wc[0] * y_local + wc[1] * (-x_local);
```

## 5. 必须新增的 kernel 辅助函数

在 `src/Kernels.H` 的 IBM helper 区域增加 GPU inline 辅助函数，供 `force_wall_model()` 使用。

### 5.1 wrap index

复用普通 `force_interp_extrap()` 的周期索引逻辑：

```cpp
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
int ibm_wrap_index(int i, int n) {
    if (i < 0) return n + i;
    if (i > n - 1) return i - n;
    return i;
}
```

### 5.2 插值速度和密度

输入坐标使用格点归一化坐标，即物理坐标除以 `delta` 后的 `xp, yp, zp`。
注意不要把局部变量命名为 `rhop`：`src/D3Q19.H` 中已有宏
`#define rhop (1.14 * rho0)`，该名字会在预处理阶段被替换。当前实现使用
`rho_interp` 作为插值密度变量名。

```cpp
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
void ibm_interp_u_rho_at(amrex::Real xp, amrex::Real yp, amrex::Real zp,
                         amrex::Array4<amrex::Real> const& u,
                         amrex::Array4<amrex::Real> const& rho,
                         amrex::Real& ux,
                         amrex::Real& uy,
                         amrex::Real& uz,
                         amrex::Real& rho_interp) {
    ux = 0.0;
    uy = 0.0;
    uz = 0.0;
    rho_interp = 0.0;

    const int ix0 = static_cast<int>(amrex::Math::floor(xp));
    const int iy0 = static_cast<int>(amrex::Math::floor(yp));
    const int iz0 = static_cast<int>(amrex::Math::floor(zp));

    for (int x = -1; x <= 1; ++x) {
        const int xx = ibm_wrap_index(ix0 + x, NX);
        const amrex::Real wx = delta3p(xp - (xx + 0.5));
        for (int y = -1; y <= 1; ++y) {
            const int yy = ibm_wrap_index(iy0 + y, NY);
            const amrex::Real wy = delta3p(yp - (yy + 0.5));
            for (int z = -1; z <= 1; ++z) {
                const int zz = ibm_wrap_index(iz0 + z, NZ);
                const amrex::Real wz = delta3p(zp - (zz + 0.5));
                const amrex::Real w = wx * wy * wz;
                ux += u(xx, yy, zz, 0) * w;
                uy += u(xx, yy, zz, 1) * w;
                uz += u(xx, yy, zz, 2) * w;
                rho_interp += rho(xx, yy, zz, 0) * w;
            }
        }
    }
}
```

### 5.3 只插值密度

```cpp
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
amrex::Real ibm_interp_rho_at(amrex::Real xp, amrex::Real yp, amrex::Real zp,
                              amrex::Array4<amrex::Real> const& rho) {
    amrex::Real rho_interp = 0.0;

    const int ix0 = static_cast<int>(amrex::Math::floor(xp));
    const int iy0 = static_cast<int>(amrex::Math::floor(yp));
    const int iz0 = static_cast<int>(amrex::Math::floor(zp));

    for (int x = -1; x <= 1; ++x) {
        const int xx = ibm_wrap_index(ix0 + x, NX);
        const amrex::Real wx = delta3p(xp - (xx + 0.5));
        for (int y = -1; y <= 1; ++y) {
            const int yy = ibm_wrap_index(iy0 + y, NY);
            const amrex::Real wy = delta3p(yp - (yy + 0.5));
            for (int z = -1; z <= 1; ++z) {
                const int zz = ibm_wrap_index(iz0 + z, NZ);
                const amrex::Real wz = delta3p(zp - (zz + 0.5));
                rho_interp += rho(xx, yy, zz, 0) * wx * wy * wz;
            }
        }
    }

    return rho_interp;
}
```

## 6. 摩擦速度辅助函数

在 `Kernels.H` 中增加三个 inline 函数。

### 6.1 Werner-Wengle

```cpp
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
amrex::Real compute_u_tau_werner_wengle(amrex::Real ur_xi,
                                        amrex::Real nu,
                                        amrex::Real etaP) {
    constexpr amrex::Real A = 8.3;
    constexpr amrex::Real B = 1.0 / 7.0;
    constexpr amrex::Real eta_c = 11.81;

    const amrex::Real u_tau_lam = std::sqrt(ur_xi * nu / etaP);
    const amrex::Real eta_plus_lam = etaP * u_tau_lam / nu;

    if (eta_plus_lam <= eta_c) {
        return u_tau_lam;
    }

    return std::pow(nu / etaP, B / (B + 1.0))
         * std::pow(ur_xi / A, 1.0 / (B + 1.0));
}
```

### 6.2 APGPL 修正量

```cpp
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
amrex::Real compute_uD_apgpl(amrex::Real ur_xi,
                             amrex::Real nu,
                             amrex::Real rhoP,
                             amrex::Real etaP,
                             amrex::Real dpdxi) {
    constexpr amrex::Real a = 7.5789;
    constexpr amrex::Real b = -1.4489;
    constexpr amrex::Real c_apgpl = 191.1799;

    const amrex::Real term1 = a * std::sqrt((etaP / rhoP) * dpdxi);
    const amrex::Real log_arg = c_apgpl * etaP * etaP * etaP * dpdxi
                              / (rhoP * nu * nu);

    if (log_arg <= 0.0) {
        return -1.0;
    }

    const amrex::Real term2 = b * std::pow((nu / rhoP) * dpdxi, 1.0 / 3.0)
                            * std::log(log_arg);

    return ur_xi - term1 - term2;
}
```

### 6.3 APGPL 摩擦速度

```cpp
AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
amrex::Real residual_eta_c_apgpl(amrex::Real eta, amrex::Real rhs) {
    constexpr amrex::Real A = 8.3;
    constexpr amrex::Real B = 1.0 / 7.0;

    return eta * eta - A * std::pow(eta, B + 1.0) - rhs;
}

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
amrex::Real derivative_eta_c_apgpl(amrex::Real eta) {
    constexpr amrex::Real A = 8.3;
    constexpr amrex::Real B = 1.0 / 7.0;

    return 2.0 * eta - A * (B + 1.0) * std::pow(eta, B);
}

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
amrex::Real compute_eta_c_apgpl(amrex::Real rhoP,
                                amrex::Real nu,
                                amrex::Real etaP,
                                amrex::Real dpdxi) {
    constexpr amrex::Real a = 7.5789;
    constexpr amrex::Real b = -1.4489;
    constexpr amrex::Real c_apgpl = 191.1799;
    constexpr amrex::Real eta_c_ww = 11.81;

    const amrex::Real K = etaP * etaP * etaP * dpdxi / (rhoP * nu * nu);
    if (K <= 0.0) {
        return eta_c_ww;
    }

    const amrex::Real rhs = a * std::sqrt(K)
                          + b * std::pow(K, 1.0 / 3.0) * std::log(c_apgpl * K);

    amrex::Real lo = 11.78;
    amrex::Real hi = 2.0 * eta_c_ww;
    amrex::Real f_lo = residual_eta_c_apgpl(lo, rhs);
    amrex::Real f_hi = residual_eta_c_apgpl(hi, rhs);

    if (f_lo > 0.0) {
        return eta_c_ww;
    }

    if (f_hi < 0.0) {
        return hi;
    }

    amrex::Real eta = eta_c_ww;
    for (int iter = 0; iter < 5; ++iter) {
        const amrex::Real f_eta = residual_eta_c_apgpl(eta, rhs);
        const amrex::Real df_eta = derivative_eta_c_apgpl(eta);
        amrex::Real eta_next = 0.5 * (lo + hi);

        if (df_eta != 0.0) {
            eta_next = eta - f_eta / df_eta;
        }

        if (eta_next <= lo || eta_next >= hi || eta_next != eta_next) {
            eta_next = 0.5 * (lo + hi);
        }

        const amrex::Real f_next = residual_eta_c_apgpl(eta_next, rhs);
        if (f_next <= 0.0) {
            lo = eta_next;
        } else {
            hi = eta_next;
        }

        eta = eta_next;
    }

    return eta;
}

AMREX_GPU_HOST_DEVICE AMREX_FORCE_INLINE
amrex::Real compute_u_tau_apgpl(amrex::Real ur_xi,
                                amrex::Real uD,
                                amrex::Real rhoP,
                                amrex::Real nu,
                                amrex::Real etaP,
                                amrex::Real dpdxi) {
    constexpr amrex::Real A = 8.3;
    constexpr amrex::Real B = 1.0 / 7.0;

    const amrex::Real u_tau_lam = std::sqrt(ur_xi * nu / etaP);
    const amrex::Real eta_plus_lam = etaP * u_tau_lam / nu;
    const amrex::Real eta_c = compute_eta_c_apgpl(rhoP, nu, etaP, dpdxi);

    if (eta_plus_lam <= eta_c) {
        return u_tau_lam;
    }

    return std::pow(nu / etaP, B / (B + 1.0))
         * std::pow(uD / A, 1.0 / (B + 1.0));
}
```

## 7. 替换 `force_wall_model()` 的算法

删除旧 `force_wall_model()` 内“剪切应力直接体力”的逻辑，改成下面的步骤。

### 7.1 几何、槽道法向和目标速度

```cpp
const amrex::Real xt = p.pos(0) / delta;
const amrex::Real yt = p.pos(1) / delta;
const amrex::Real zt = p.pos(2) / delta;

const amrex::Real IB_weight = LP_dr * area;

const amrex::Real y_mid = 0.5 * (3.5 + (NY - 3.5)) * dx_min;
const bool is_bottom_wall = (p.pos(1) < y_mid);

const amrex::Real nx = 0.0;
const amrex::Real ny = is_bottom_wall ? 1.0 : -1.0;
const amrex::Real nz = 0.0;

const amrex::Real UBx = uc[0] + wc[2] * (-y_local) + wc[1] * z_local;
const amrex::Real UBy = uc[1] + wc[2] * x_local + wc[0] * (-z_local);
const amrex::Real UBz = uc[2] + wc[0] * y_local + wc[1] * (-x_local);
```

对于固定上下壁面，当前 `uc` 和 `wc` 通常为零，所以上式会退化为 `UB=(0,0,0)`。
保留该写法是为了和现有 MDF 目标速度计算保持一致。

### 7.2 在边界点和探测点插值

```cpp
amrex::Real uLagx, uLagy, uLagz, rhoLag;
ibm_interp_u_rho_at(xt, yt, zt, u, rho, uLagx, uLagy, uLagz, rhoLag);

const amrex::Real etaP = 3.0 * delta;
const amrex::Real probe_offset = etaP / delta;

const amrex::Real xP = xt + probe_offset * nx;
const amrex::Real yP = yt + probe_offset * ny;
const amrex::Real zP = zt + probe_offset * nz;

amrex::Real UPx, UPy, UPz, rhoP;
ibm_interp_u_rho_at(xP, yP, zP, u, rho, UPx, UPy, UPz, rhoP);
```

这里 `uLagx/uLagy/uLagz` 对应普通 MDF 代码里的 `uxt/uyt/uzt`，含义是欧拉速度
插值到真实 Lagrangian 边界点 `B` 后的当前速度。它不是壁面目标速度。

壁面目标速度是 `UBx/UBy/UBz`。壁面模型目标速度是后续计算得到的
`UBRx/UBRy/UBRz`。

### 7.3 计算切向方向

```cpp
const amrex::Real UP_eta = UPx * nx + UPy * ny + UPz * nz;
const amrex::Real tx_raw = UPx - UP_eta * nx;
const amrex::Real ty_raw = UPy - UP_eta * ny;
const amrex::Real tz_raw = UPz - UP_eta * nz;
const amrex::Real UP_xi = std::sqrt(tx_raw * tx_raw
                                  + ty_raw * ty_raw
                                  + tz_raw * tz_raw);

amrex::Real UBRx = UBx;
amrex::Real UBRy = UBy;
amrex::Real UBRz = UBz;

if (UP_xi > 1.0e-14 && rhoP > 1.0e-14 && rhoLag > 1.0e-14) {
    const amrex::Real tx = tx_raw / UP_xi;
    const amrex::Real ty = ty_raw / UP_xi;
    const amrex::Real tz = tz_raw / UP_xi;

    const amrex::Real UB_xi = UBx * tx + UBy * ty + UBz * tz;
    const amrex::Real UB_eta = UBx * nx + UBy * ny + UBz * nz;
    const amrex::Real ur_xi = UP_xi - UB_xi;

    if (ur_xi > 1.0e-14) {
        // 后续壁函数逻辑写在这里
    }
}
```

### 7.4 沿切向计算 `dpdxi`

在 `ur_xi > eps` 的分支内计算：

```cpp
const amrex::Real dpdxi_half_step = delta;
const amrex::Real h_grid = dpdxi_half_step / delta; // 等于 1

const amrex::Real rho_plus = ibm_interp_rho_at(
    xP + h_grid * tx,
    yP + h_grid * ty,
    zP + h_grid * tz,
    rho);

const amrex::Real rho_minus = ibm_interp_rho_at(
    xP - h_grid * tx,
    yP - h_grid * ty,
    zP - h_grid * tz,
    rho);

const amrex::Real dpdxi = cs2 * (rho_plus - rho_minus) / (2.0 * dpdxi_half_step);
```

### 7.5 计算 `UBR`

```cpp
amrex::Real u_tau;
bool use_wall_model = true;

if (dpdxi <= 0.0) {
    u_tau = compute_u_tau_werner_wengle(ur_xi, mv_0, etaP);
} else {
    const amrex::Real uD = compute_uD_apgpl(ur_xi, mv_0, rhoP, etaP, dpdxi);
    if (uD <= 0.0) {
        use_wall_model = false;
    } else {
        u_tau = compute_u_tau_apgpl(ur_xi, uD, rhoP, mv_0, etaP, dpdxi);
    }
}

if (use_wall_model) {
    const amrex::Real tau_w = rhoP * u_tau * u_tau;
    const amrex::Real eta_plus = etaP * u_tau / mv_0;
    const amrex::Real vd = 0.41 * eta_plus
                         * (1.0 - std::exp(-eta_plus / 19.0));
    const amrex::Real nu_t = mv_0 * vd * vd;

    const amrex::Real Gp = (tau_w + dpdxi * etaP)
                         / (rhoP * (mv_0 + nu_t));

    amrex::Real UBR_xi = UP_xi - Gp * etaP;

    if (Gp < 0.0) {
        UBR_xi = UP_xi;
    }

    if (dpdxi > 0.0 && UBR_xi < UB_xi) {
        UBR_xi = UB_xi;
    }

    UBRx = UBR_xi * tx + UB_eta * nx;
    UBRy = UBR_xi * ty + UB_eta * ny;
    UBRz = UBR_xi * tz + UB_eta * nz;
}
```

如果 `use_wall_model=false`，保持初始化值：

```cpp
UBR = UB
```

## 8. 力计算和扩散

用当前 MDF 的符号约定。普通 MDF 中已有：

```cpp
fxt = rhot * (uxt - upx);
fyt = rhot * (uyt - upy);
fzt = rhot * (uzt - upz);
```

其中 `uxt/uyt/uzt` 是欧拉速度插值到 Lagrangian 点的当前速度，
`upx/upy/upz` 是无滑移壁面目标速度。

壁面模型只替换目标速度：

```text
upx, upy, upz  ->  UBRx, UBRy, UBRz
```

所以实现中应写成：

```cpp
const amrex::Real rhot = 2.0 * rhoLag / dt_min;

const amrex::Real fxt = rhot * (uLagx - UBRx);
const amrex::Real fyt = rhot * (uLagy - UBRy);
const amrex::Real fzt = rhot * (uLagz - UBRz);

fx = fxt * IB_weight * dx_min * dx_min * dx_min;
fy = fyt * IB_weight * dx_min * dx_min * dx_min;
fz = fzt * IB_weight * dx_min * dx_min * dx_min;

tx = (fzt * y_local - fyt * z_local) * IB_weight * dx_min * dx_min * dx_min;
ty = (fxt * z_local - fzt * x_local) * IB_weight * dx_min * dx_min * dx_min;
tz = (fyt * x_local - fxt * y_local) * IB_weight * dx_min * dx_min * dx_min;
```

如果该点不使用壁面模型，则保持：

```cpp
UBRx = UBx;
UBRy = UBy;
UBRz = UBz;
```

此时力公式自动退回普通无滑移 MDF：

```cpp
fxt = rhot * (uLagx - UBx);
fyt = rhot * (uLagy - UBy);
fzt = rhot * (uLagz - UBz);
```

扩散位置使用真实边界点 `B`，不要使用旧代码的 `xq = B + 0.25 * normal`。

扩散 stencil 使用普通 `force_interp_extrap()` 的 `-1..1` 循环和负号：

```cpp
Atomic::AddNoRet(&Ft(xx, yy, zz, 0), -fxt * IB_Interp);
Atomic::AddNoRet(&Ft(xx, yy, zz, 1), -fyt * IB_Interp);
Atomic::AddNoRet(&Ft(xx, yy, zz, 2), -fzt * IB_Interp);
```

## 9. 切换 NF=1 调用

在 `src/AmrCoreLBM.cpp` 的 `AmrCoreLBM::InterpForce(int lev)` 中，只改 `#else` 的 `NF=1` 分支：

```cpp
for (int i = 0; i < particle_num; i++) {
    // particles[i]->InterpForce(lev, rho_lev, u_lev, force_lev);
    particles[i]->InterpForceWallModel(lev, rho_lev, u_lev, force_lev);
}
```

不要改 `#if USE_MDF_TWO_STAGE` 分支。

## 10. 旧 `force_wall_model()` 必须删除的逻辑

不要保留这些旧逻辑：

```text
normal = normalize(xlocal, ylocal, zlocal)
xq = B + 0.25 * normal
xRef = B + 2.5 * normal
tw = rhoRef * pow(...)
fTang = 2.0 * tw / dx_min
fNormal = rhoRef * velNormal
f = fNormal * normal + fTang * tangent
```

原因：本实现不是“剪切应力直接构造体力”，而是“壁面模型先构造 UBR，再进入 MDF 目标速度修正”。

## 11. 验证步骤

先做结构检查：

```bash
./scripts/check_wall_model_nf1.sh
```

也可以用 `rg` 人工检查核心符号：

```bash
rg -n "InterpForceWallModel|force_wall_model|compute_u_tau|compute_uD|ibm_interp_rho_at" src
```

再编译：

```bash
./scripts/compile.sh
```

如果编译失败，优先检查：

```text
1. AMREX_GPU_HOST_DEVICE 函数中是否用了不允许的 host-only API
2. ibm_interp_rho_at 是否错误创建了无效的 Array4 临时对象
3. rhoP、rhoLag 是否可能为 0，需要保护
4. std::exp/std::log/std::pow 在当前 CUDA 编译路径是否可用；本文件已有类似用法时可沿用
5. force_wall_model 是否还引用了未初始化的 uxRef/uyRef/uzRef/rhoRef
6. force_wall_model 是否还用 normalize(xlocal, ylocal, zlocal) 推断法向
```

## 12. 成功标准

代码修改后应满足：

```text
1. NF=1 分支调用 InterpForceWallModel。
2. NF>1 分支不变。
3. force_wall_model 不再直接用 tw 构造 fTang。
4. force_wall_model 先计算 UBR，再用 f = rhot * (uLag - UBR)。
5. dpdxi 由 rho 沿 e_xi 的一维中心差分得到。
6. 扩散位置为真实边界点 B。
7. 槽道下壁面使用法向 (0, 1, 0)，上壁面使用法向 (0, -1, 0)。
8. APGPL 分支动态求解 eta_c，不再固定使用 11.81。
9. ./scripts/compile.sh 编译通过。
```
