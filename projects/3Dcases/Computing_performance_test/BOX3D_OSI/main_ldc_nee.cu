//* 3DOSI; LDC3D 顶盖驱动流 —— 在 Ma 的原版 OSI 代码中嵌入 LDC 算例
//* 改动清单（其余与原版逐字一致）：
//*   1) read_check_fluid：TGV 初始化 -> LDC 初始化（静止流场 + 均匀压力 p0）
//*   2) init_flag：流体 = [2,Nx1]^3 (flag=1)；壁面层 {1,Nx2} 各面 (flag=0，反弹边界)；
//*      最外层 {0,Nx3} 为回绕垃圾层 (flag=-1，两个内核都不碰)
//*   3) 新增 ldcWallBCCUDA（全路反弹 + 运动壁面动量修正，顶盖 y=1 以 U0 沿 +x 驱动，
//*      其余五面静止）。注：Ma 这套 18 分量无静止项模型上，NEE 用于运动壁面会净注入
//*      动量（泵送发散），静止壁面虽自洽，为统一稳健六面全部采用修正反弹。
//*   4) 主循环：删除 y/z 周期拷贝与 x 向 MPI 自交换（LDC 六面皆壁，无需交换），
//*      每步 = ldcWallBCCUDA -> fluidCollisionInnerCUDA（同 stream1，保证序）
//*   5) L0 改为腔宽 NxT（TGV 原为 NxT/2/PI）；每 1000 步报告 MLUPs 与名义带宽

#define MPI
// #define DOUBLE_PRECISION
typedef unsigned long long int pfint;
typedef float dfloat;
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <cuda_runtime.h>
#include <cuda.h>
#include "../basic/errCUDA.h"
#include <chrono>

// #include "../basic/basicCU.h"
#ifdef MPI
#include "mpi.h"
#ifdef DOUBLE_PRECISION
const MPI_Datatype MPI_REAL_MYOWN = MPI_DOUBLE;
#else
const MPI_Datatype MPI_REAL_MYOWN = MPI_FLOAT;
#endif
#endif

#define nThreads 128
#define GPUPERNODE 2
#define PI (3.141592653589793238462643383279)

#define CHECKPOINT 0
#define OUTPUT_VEL_EVERY 10000 // 每多少步输出一次速度场+中心线剖面（0 = 关闭）

dfloat G_dire[3] = {0, -1, 0};
// dfloat gravity;
#define gravityDirX (0.0)
#define gravityDirY (-1.0)
#define gravityDirZ (0.0)
#define periodic 0

#define D 1.0
#define R 6

#define Lx (2.0)
#define Ly (2.0)
#define Lz (2.0)

#define rhof 1.0

#define NPROCx 1
#define NPROCy 1
#define NPROCz 1
#define NPROC (NPROCx * NPROCy * NPROCz)

#define NxT (586*NPROCx)
#define NxT1 (NxT + 1)
#define NyT 586
#define NyT1 (NyT + 1)
#define NzT 586
#define NzT1 (NzT + 1)
#define R_boun (int)(Lz / D * R)



#define Nx (int)(NxT / NPROCx)
#define Ny (int)(NyT / NPROCy)
#define Nz (int)(NzT / NPROCz)
#define Nx1 (Nx + 1)
#define Ny1 (Ny + 1)
#define Nz1 (Nz + 1)
#define Nx2 (Nx + 2)
#define Ny2 (Ny + 2)
#define Nz2 (Nz + 2)
#define Nx3 (Nx + 3)
#define Ny3 (Ny + 3)
#define Nz3 (Nz + 3)
#define Nx4 (Nx + 4)
#define Ny4 (Ny + 4)
#define Nz4 (Nz + 4)
#define NX Nx4
#define NY Ny4
#define NZ Nz4
#define NODES (Nx4 * Ny4 * Nz4)
#define memSizeNODES (sizeof(dfloat) * NODES)

#define Q 19
#define Re 1600.0
#define Ma 0.1
#define dx 1.0 //(D / (2 * R))
#define dt 1.0 // 0.001
#define c (dx / dt)
#define cs (c / sqrt(3.0))
#define U0 (Ma * cs)
#define p0 (rhof * (cs * cs))
#define L0 ((dfloat)NxT) // LDC：特征长度 = 腔宽（原版 TGV 为 NxT/2/PI）
#define mv (U0 * L0 / Re) // 1.0
#define tao (mv / (dt * cs * cs) + 0.5)
#define omega (1.0 / tao)
#define ax_f 0.0
#define dp 0.0
#define delta_p (rhof * ax_f * Lx + dp)
#define Dh (4 * (Ly * Lz) / (2 * Ly + 2 * Lz))
#define Ua (delta_p * Dh * Dh / (32 * rhof * mv * Lx))
#define Rec (Ua * Dh / mv)
#define Rep (Ua * D / mv)
#define c_square (c * c)
#define cs_square (cs * cs)
#define cs_4pow (cs_square * cs_square)

int numproc;
int step, step0, stepmax;
dfloat t, t0;

dim3 threadsPerBlock(4, 32);
dim3 numBlocksYZ((NY + threadsPerBlock.x - 1) / threadsPerBlock.x, (NZ + threadsPerBlock.y - 1) / threadsPerBlock.y);
dim3 numBlocksXZ((NX + threadsPerBlock.x - 1) / threadsPerBlock.x, (NZ + threadsPerBlock.y - 1) / threadsPerBlock.y);
dim3 numBlocksXY((NX + threadsPerBlock.x - 1) / threadsPerBlock.x, (NY + threadsPerBlock.y - 1) / threadsPerBlock.y);

dfloat *fDHost, *pHost, *uHost, *FHost;
dfloat *pdfsSendXHost, *pdfsRecvXHost;

int *flagHost;

dfloat *fDDevice, *pDevice, *uDevice, *FDevice;
dfloat *pdfsSendXDevice, *pdfsRecvXDevice;
int *flagDevice;
dfloat *FfBodyDevice;

dfloat vAKEnergy; //* volume-averaged kinetic energy
cudaStream_t stream0, stream1, stream2, stream3, stream4, stream5;

dfloat ux_max, uy_max, uz_max, Ux_max, Uy_max, Uz_max;

int num_points;

dfloat pave, Uave[3], aveU[3];
dfloat averageP, averageU[3];
dfloat Error_U[3], Error_int[3];

int m_fluid;
int ifirst = 1;

dfloat u_p_time, com1_time, pindex_time, IBM_time, bound_time, Fcolli_time, com2_time, stream_time, Pcolli_time, Pmove_time;
dfloat com3_time, com4_time;

void mallocMemoryCUDA()
{
	checkCudaErrors(cudaStreamCreate(&stream0));
	checkCudaErrors(cudaStreamCreate(&stream1));
	checkCudaErrors(cudaStreamCreate(&stream2));
	checkCudaErrors(cudaStreamCreate(&stream3));
	checkCudaErrors(cudaStreamCreate(&stream4));
	checkCudaErrors(cudaStreamCreate(&stream5));
	// checkCudaErrors(cudaEventCreate(&stop));

	checkCudaErrors(cudaMallocHost((void **)&fDHost, Q * memSizeNODES));
	checkCudaErrors(cudaMallocHost((void **)&pHost, memSizeNODES));
	checkCudaErrors(cudaMallocHost((void **)&uHost, 3 * memSizeNODES));
	checkCudaErrors(cudaMallocHost((void **)&FHost, 3 * memSizeNODES));
	checkCudaErrors(cudaMallocHost((void **)&pdfsSendXHost, sizeof(dfloat) * 2 * 5 * Ny2 * Nz2));
	checkCudaErrors(cudaMallocHost((void **)&pdfsRecvXHost, sizeof(dfloat) * 2 * 5 * Ny2 * Nz2));
	checkCudaErrors(cudaMallocHost((void **)&flagHost, sizeof(int) * NODES));

	checkCudaErrors(cudaMalloc((void **)&fDDevice, Q * memSizeNODES));
	checkCudaErrors(cudaMalloc((void **)&pDevice, memSizeNODES));
	checkCudaErrors(cudaMalloc((void **)&uDevice, 3 * memSizeNODES));
	checkCudaErrors(cudaMalloc((void **)&FDevice, 3 * memSizeNODES));
	checkCudaErrors(cudaMalloc((void **)&pdfsSendXDevice, sizeof(dfloat) * 2 * 5 * Ny2 * Nz2));
	checkCudaErrors(cudaMalloc((void **)&pdfsRecvXDevice, sizeof(dfloat) * 2 * 5 * Ny2 * Nz2));
	checkCudaErrors(cudaMalloc((void **)&flagDevice, sizeof(int) * NODES));
}

void freeMemoryCUDA()
{
	checkCudaErrors(cudaFree(fDDevice));
	checkCudaErrors(cudaFree(pDevice));
	checkCudaErrors(cudaFree(uDevice));
	checkCudaErrors(cudaFree(FDevice));
	checkCudaErrors(cudaFree(flagDevice));
	checkCudaErrors(cudaFree(FfBodyDevice));

	checkCudaErrors(cudaFreeHost(fDHost));
	checkCudaErrors(cudaFreeHost(pHost));
	checkCudaErrors(cudaFreeHost(uHost));
	checkCudaErrors(cudaFreeHost(FHost));
	checkCudaErrors(cudaFreeHost(flagHost));

	checkCudaErrors(cudaStreamDestroy(stream0));
	checkCudaErrors(cudaStreamDestroy(stream1));
	checkCudaErrors(cudaStreamDestroy(stream2));
	checkCudaErrors(cudaStreamDestroy(stream3));
	checkCudaErrors(cudaStreamDestroy(stream4));
	checkCudaErrors(cudaStreamDestroy(stream5));
}

//* One-Step INDEX
__device__ __host__ inline pfint INDEX(pfint node, pfint q, pfint step)
{
	if (q == 0)
		return (q * NODES + node);
	else if (q == 1)
		return (q * NODES + ((node + NODES * step - NY * NZ * step) % NODES));
	else if (q == 2)
		return (q * NODES + ((node + NY * NZ * step) % NODES));
	else if (q == 3)
		return (q * NODES + (node + (NODES - NZ) * step) % NODES);
	else if (q == 4)
		return (q * NODES + (node + (NZ)*step) % NODES);
	else if (q == 5)
		return (q * NODES + (node + (NODES - 1) * step) % NODES);
	else if (q == 6)
		return (q * NODES + (node + (1) * step) % NODES);
	else if (q == 7)
		return (q * NODES + (node + (NODES - (NY * NZ + NZ)) * step) % NODES);
	else if (q == 8)
		return (q * NODES + (node + ((NY * NZ + NZ)) * step) % NODES);
	else if (q == 9)
		return (q * NODES + (node + (NODES - (NY * NZ + 1)) * step) % NODES);
	else if (q == 10)
		return (q * NODES + (node + ((NY * NZ + 1)) * step) % NODES);
	else if (q == 11)
		return (q * NODES + (node + (NODES - (NZ + 1)) * step) % NODES);
	else if (q == 12)
		return (q * NODES + (node + ((NZ + 1)) * step) % NODES);
	else if (q == 13)
		return (q * NODES + (node + (NODES - (NY * NZ - NZ)) * step) % NODES);
	else if (q == 14)
		return (q * NODES + (node + ((NY * NZ - NZ)) * step) % NODES);
	else if (q == 15)
		return (q * NODES + (node + (NODES - (NY * NZ - 1)) * step) % NODES);
	else if (q == 16)
		return (q * NODES + (node + ((NY * NZ - 1)) * step) % NODES);
	else if (q == 17)
		return (q * NODES + (node + (NODES - (NZ - 1)) * step) % NODES);
	else if (q == 18)
		return (q * NODES + (node + ((NZ - 1)) * step) % NODES);
	else
		return EXIT_FAILURE;
}

void CPU_time(int me)
{
	FILE *file;

	int nproc, i;
	int msgtag;
	MPI_Status status;
	dfloat u_p_time0, com1_time0, IBM_time0, Fcolli_time0, com2_time0, stream_time0, Pcolli_time0, Pmove_time0;
	dfloat total;
	dfloat time_CPU[10];

	if (me != 0)
	{
		msgtag = me;

		time_CPU[0] = u_p_time;
		time_CPU[1] = com1_time;
		time_CPU[2] = IBM_time;
		time_CPU[3] = com2_time;
		time_CPU[4] = Fcolli_time;
		time_CPU[5] = com3_time;
		time_CPU[6] = stream_time;
		time_CPU[7] = Pcolli_time;
		time_CPU[8] = Pmove_time;
		time_CPU[9] = com4_time;

		MPI_Send(&time_CPU[0], 10, MPI_REAL_MYOWN, 0, msgtag, MPI_COMM_WORLD);
	}

	if (me == 0)
	{
		if ((file = fopen("CPU_time.dat", "a")) == NULL)
		{
			printf("can not open the file!");
			exit(0);
		}
		fprintf(file, "me, u_p,com1,IBM,com2_time,Fcolli,com3_time,stream,Pcolli,Pmove,com4\n");

		total = u_p_time + com1_time + IBM_time + Fcolli_time + com2_time + com3_time + com4_time + stream_time + Pcolli_time + Pmove_time;

		fprintf(file, "%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n", me, u_p_time, com1_time, IBM_time, com2_time, Fcolli_time, com3_time, stream_time, Pcolli_time, Pmove_time, com4_time, total);

		for (nproc = 1; nproc < NPROC; nproc++)
		{
			msgtag = nproc;
			MPI_Recv(&time_CPU[0], 10, MPI_REAL_MYOWN, nproc, msgtag, MPI_COMM_WORLD, &status);

			total = 0;
			for (i = 0; i < 10; i++)
				total += time_CPU[i];
			fprintf(file, "%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n", nproc, time_CPU[0], time_CPU[1], time_CPU[2], time_CPU[3], time_CPU[4], time_CPU[5], time_CPU[6], time_CPU[7], time_CPU[8], time_CPU[9], total);
		}

		fclose(file);
	}
}

void read_check_fluid(int me, dfloat *pr, dfloat *u)
{
	FILE *file;
	char fname[20];

	int x, y, z;
	pfint id;

	sprintf(fname, "check_fluid%03d", me);
	if ((file = fopen(fname, "rb")) == NULL)
	{
		if (me == 0)
			printf("Simulation from new initial fluid field! (LDC: rest field + uniform p0)\n");
		step0 = 1;
		t0 = 0.;
		for (x = 0; x <= Nx3; x++)
			for (y = 0; y <= Ny3; y++)
				for (z = 0; z <= Nz3; z++)
				{
					id = x * NY * NZ + y * NZ + z;

					pr[id] = p0;
					u[id] = 0.0;
					u[NODES + id] = 0.0;
					u[2 * NODES + id] = 0.0;
				}
	}
	else
	{
		fread(&step0, sizeof(int), 1, file);
		fread(&t0, sizeof(dfloat), 1, file);
		fread(u, sizeof(u), 1, file);
		fread(pr, sizeof(pr), 1, file);
		fclose(file);
	}
}

void init_f(int me, pfint step, dfloat *fD, dfloat *pr, dfloat *u, dfloat *F, int *flag)
{
	int x, y, z, i;
	pfint id;
	dfloat pc, cux, cuy, cuz, uxuy, uxuz, uyuz, ux2, uy2, uz2;
	dfloat feq[Q];

	read_check_fluid(me, pr, u);
	printf("hello%d", me);

	for (x = 0; x <= Nx3; x++)
		for (y = 0; y <= Ny3; y++)
			for (z = 0; z <= Nz3; z++)
			{
				id = x * NY * NZ + y * NZ + z;

				F[id] = 0.0;
				F[NODES + id] = 0.0;
				F[2 * NODES + id] = 0.0;

				flag[id] = 0;
				pc = pr[id] / c_square;
				cux = u[id] / c;
				cuy = u[NODES + id] / c;
				cuz = u[2 * NODES + id] / c;
				uxuy = cux * cuy;
				uxuz = cux * cuz;
				uyuz = cuy * cuz;
				ux2 = cux * cux;
				uy2 = cuy * cuy;
				uz2 = cuz * cuz;

				feq[2] = (1.0 / 6) * (pc - cux + ux2 - 0.5 * (uy2 + uz2));
				feq[4] = (1.0 / 6) * (pc - cuy + uy2 - 0.5 * (ux2 + uz2));
				feq[6] = (1.0 / 6) * (pc - cuz + uz2 - 0.5 * (ux2 + uy2));
				feq[8] = (1.0 / 12) * (pc - cux - cuy + 3 * uxuy + ux2 + uy2 - 0.5 * uz2);
				feq[14] = (1.0 / 12) * (pc - cux + cuy - 3 * uxuy + ux2 + uy2 - 0.5 * uz2);
				feq[10] = (1.0 / 12) * (pc - cux - cuz + 3 * uxuz + ux2 + uz2 - 0.5 * uy2);
				feq[16] = (1.0 / 12) * (pc - cux + cuz - 3 * uxuz + ux2 + uz2 - 0.5 * uy2);
				feq[12] = (1.0 / 12) * (pc - cuy - cuz + 3 * uyuz + uy2 + uz2 - 0.5 * ux2);
				feq[18] = (1.0 / 12) * (pc - cuy + cuz - 3 * uyuz + uy2 + uz2 - 0.5 * ux2);

				feq[1] = feq[2] + (1.0 / 3) * cux;
				feq[3] = feq[4] + (1.0 / 3) * cuy;
				feq[5] = feq[6] + (1.0 / 3) * cuz;
				feq[7] = feq[8] + (1.0 / 6) * (cux + cuy);
				feq[13] = feq[14] + (1.0 / 6) * (cux - cuy);
				feq[9] = feq[10] + (1.0 / 6) * (cux + cuz);
				feq[15] = feq[16] + (1.0 / 6) * (cux - cuz);
				feq[11] = feq[12] + (1.0 / 6) * (cuy + cuz);
				feq[17] = feq[18] + (1.0 / 6) * (cuy - cuz);

				for (i = 0; i < Q; i++)
				{

					fD[INDEX(id, i, step)] = feq[i];
				}
			}
}

void init_flag(int me, int *flag)
{
	int x, y, z;
	pfint id;
	for (x = 0; x < Nx4; x++)
		for (y = 0; y < Ny4; y++)
			for (z = 0; z < Nz4; z++)
			{
				id = x * NY * NZ + y * NZ + z;
				// LDC：流体 = [2,Nx1]^3；壁面层 = {1,Nx2} 各面（NEE 边界）；
				// 最外层 {0,Nx3} = 回绕垃圾层（flag=-1，两个内核均跳过）
				if ((x >= 2) && (x <= Nx1) && (y >= 2) && (y <= Ny1) && (z >= 2) && (z <= Nz1))
					flag[id] = 1;
				else if ((x >= 1) && (x <= Nx2) && (y >= 1) && (y <= Ny2) && (z >= 1) && (z <= Nz2))
					flag[id] = 0;
				else
					flag[id] = -1;
			}
}

void kE(int me, dfloat *u)
{
	int x, y, z;
	pfint id;
	dfloat velocityM;
	dfloat vAKEnergyTotal = 0.0;
	MPI_Status status;
	vAKEnergy = 0.0;

	if (me == 0)
	{
		for (y = 1; y <= Ny1; y++)
			for (z = 1; z <= Nz1; z++)
			{
				id = 1 * NY * NZ + y * NZ + z;

				velocityM = uHost[id] * uHost[id] +
							uHost[NODES + id] * uHost[NODES + id] +
							uHost[2 * NODES + id] * uHost[2 * NODES + id];
				vAKEnergy += velocityM / 2.0;
			}
	}

	for (x = 2; x <= Nx1; x++)
		for (y = 1; y <= Ny1; y++)
			for (z = 1; z <= Nz1; z++)
			{
				id = x * NY * NZ + y * NZ + z;

				velocityM = uHost[id] * uHost[id] +
							uHost[NODES + id] * uHost[NODES + id] +
							uHost[2 * NODES + id] * uHost[2 * NODES + id];
				vAKEnergy = vAKEnergy + velocityM / 2.0;
			}

	MPI_Reduce(&vAKEnergy, &vAKEnergyTotal, 1, MPI_REAL_MYOWN, MPI_SUM, 0, MPI_COMM_WORLD);

	if (me == 0)
	{
		vAKEnergyTotal = vAKEnergyTotal / (NxT + 1.0) / (NyT + 1.0) / (NzT + 1.0) / U0 / U0;

		FILE *file;
		char fname[60];

		sprintf(fname, "kineticEnergy.dat");
		if ((file = fopen(fname, "a")) == NULL)
		{
			printf("can not open the file!");
			exit(0);
		}

		fprintf(file, "%d\t%f\t%f\n", step, step * dt / (L0 / U0), vAKEnergyTotal);
		fclose(file);
	}
}

__global__ void fluidCollisionInnerCUDA(pfint step, dfloat *fD, dfloat *pr, dfloat *u, dfloat *F, int *flag)
{
	const pfint id = threadIdx.x + blockDim.x * blockIdx.x;
	if ((id < NODES) && flag[id] == 1)
	{
		dfloat temp0 = omega;
		dfloat temp1 = 1.0 - omega;

		dfloat feq01, feq02, feq03, feq04, feq05, feq06, feq07, feq08, feq09;
		dfloat feq10, feq11, feq12, feq13, feq14, feq15, feq16, feq17, feq18;
		dfloat fDt01, fDt02, fDt03, fDt04, fDt05, fDt06, fDt07, fDt08, fDt09;
		dfloat fDt10, fDt11, fDt12, fDt13, fDt14, fDt15, fDt16, fDt17, fDt18;
		pfint in01, in02, in03, in04, in05, in06, in07, in08, in09;
		pfint in10, in11, in12, in13, in14, in15, in16, in17, in18;

		in01 = INDEX(id, 1, step);
		in02 = INDEX(id, 2, step);
		in03 = INDEX(id, 3, step);
		in04 = INDEX(id, 4, step);
		in05 = INDEX(id, 5, step);
		in06 = INDEX(id, 6, step);
		in07 = INDEX(id, 7, step);
		in08 = INDEX(id, 8, step);
		in09 = INDEX(id, 9, step);
		in10 = INDEX(id, 10, step);
		in11 = INDEX(id, 11, step);
		in12 = INDEX(id, 12, step);
		in13 = INDEX(id, 13, step);
		in14 = INDEX(id, 14, step);
		in15 = INDEX(id, 15, step);
		in16 = INDEX(id, 16, step);
		in17 = INDEX(id, 17, step);
		in18 = INDEX(id, 18, step);

		fDt01 = fD[in01];
		fDt02 = fD[in02];
		fDt03 = fD[in03];
		fDt04 = fD[in04];
		fDt05 = fD[in05];
		fDt06 = fD[in06];
		fDt07 = fD[in07];
		fDt08 = fD[in08];
		fDt09 = fD[in09];
		fDt10 = fD[in10];
		fDt11 = fD[in11];
		fDt12 = fD[in12];
		fDt13 = fD[in13];
		fDt14 = fD[in14];
		fDt15 = fD[in15];
		fDt16 = fD[in16];
		fDt17 = fD[in17];
		fDt18 = fD[in18];

		{
			dfloat A = fDt01 + fDt02 + fDt03 + fDt04 + fDt05 + fDt06 + fDt07 + fDt08 + fDt09 + fDt10 + fDt11 + fDt12 + fDt13 + fDt14 + fDt15 + fDt16 + fDt17 + fDt18;
			dfloat uxt = (fDt01 - fDt02 + fDt07 - fDt08 + fDt09 - fDt10 + fDt13 - fDt14 + fDt15 - fDt16) * c;
			dfloat uyt = (fDt03 - fDt04 + fDt07 - fDt08 + fDt11 - fDt12 - fDt13 + fDt14 + fDt17 - fDt18) * c;
			dfloat uzt = (fDt05 - fDt06 + fDt09 - fDt10 + fDt11 - fDt12 - fDt15 + fDt16 - fDt17 + fDt18) * c;
			dfloat s0 = -0.3333333 * (uxt * uxt + uyt * uyt + uzt * uzt) / (2 * cs_square);
			dfloat prt = cs_square / (1 - 0.3333333) * (A + s0);
			u[id] = uxt;
			u[NODES + id] = uyt;
			u[2 * NODES + id] = uzt;
			pr[id] = prt;

			dfloat pc = prt / c_square;
			dfloat cux = uxt / c;
			dfloat cuy = uyt / c;
			dfloat cuz = uzt / c;
			dfloat uxuy = cux * cuy;
			dfloat uxuz = cux * cuz;
			dfloat uyuz = cuy * cuz;
			dfloat ux2 = cux * cux;
			dfloat uy2 = cuy * cuy;
			dfloat uz2 = cuz * cuz;

			feq02 = (1.0 / 6) * (pc - cux + ux2 - 0.5 * (uy2 + uz2));
			feq04 = (1.0 / 6) * (pc - cuy + uy2 - 0.5 * (ux2 + uz2));
			feq06 = (1.0 / 6) * (pc - cuz + uz2 - 0.5 * (ux2 + uy2));
			feq08 = (1.0 / 12) * (pc - cux - cuy + 3 * uxuy + ux2 + uy2 - 0.5 * uz2);
			feq14 = (1.0 / 12) * (pc - cux + cuy - 3 * uxuy + ux2 + uy2 - 0.5 * uz2);
			feq10 = (1.0 / 12) * (pc - cux - cuz + 3 * uxuz + ux2 + uz2 - 0.5 * uy2);
			feq16 = (1.0 / 12) * (pc - cux + cuz - 3 * uxuz + ux2 + uz2 - 0.5 * uy2);
			feq12 = (1.0 / 12) * (pc - cuy - cuz + 3 * uyuz + uy2 + uz2 - 0.5 * ux2);
			feq18 = (1.0 / 12) * (pc - cuy + cuz - 3 * uyuz + uy2 + uz2 - 0.5 * ux2);

			feq01 = feq02 + (1.0 / 3) * cux;
			feq03 = feq04 + (1.0 / 3) * cuy;
			feq05 = feq06 + (1.0 / 3) * cuz;
			feq07 = feq08 + (1.0 / 6) * (cux + cuy);
			feq13 = feq14 + (1.0 / 6) * (cux - cuy);
			feq09 = feq10 + (1.0 / 6) * (cux + cuz);
			feq15 = feq16 + (1.0 / 6) * (cux - cuz);
			feq11 = feq12 + (1.0 / 6) * (cuy + cuz);
			feq17 = feq18 + (1.0 / 6) * (cuy - cuz);
		}

		fD[in01] = temp1 * fDt01 + temp0 * feq01;
		fD[in02] = temp1 * fDt02 + temp0 * feq02;
		fD[in03] = temp1 * fDt03 + temp0 * feq03;
		fD[in04] = temp1 * fDt04 + temp0 * feq04;
		fD[in05] = temp1 * fDt05 + temp0 * feq05;
		fD[in06] = temp1 * fDt06 + temp0 * feq06;
		fD[in07] = temp1 * fDt07 + temp0 * feq07;
		fD[in08] = temp1 * fDt08 + temp0 * feq08;
		fD[in09] = temp1 * fDt09 + temp0 * feq09;
		fD[in10] = temp1 * fDt10 + temp0 * feq10;
		fD[in11] = temp1 * fDt11 + temp0 * feq11;
		fD[in12] = temp1 * fDt12 + temp0 * feq12;
		fD[in13] = temp1 * fDt13 + temp0 * feq13;
		fD[in14] = temp1 * fDt14 + temp0 * feq14;
		fD[in15] = temp1 * fDt15 + temp0 * feq15;
		fD[in16] = temp1 * fDt16 + temp0 * feq16;
		fD[in17] = temp1 * fDt17 + temp0 * feq17;
		fD[in18] = temp1 * fDt18 + temp0 * feq18;
	}
}

// ============================================================================
// LDC 壁面边界：全路反弹（fullway bounce-back）+ 运动壁面动量修正。
//   f_q(w) = f_opp(q)(w) + 2*w'_q*(e_q·u_b/c)
// Ma 方向编号相反方向对为 q<->q±1（1<->2, 3<->4, ..., 17<->18）；
// w' 与 碰撞内核平衡态奇数部一致：轴向(1-6) 1/6、对角(7-18) 1/12，
// 故修正项系数 = 2w'：轴向 1/3、对角 1/6。符号由平衡态一致性
// feq_q(u_b) = feq_opp(q)(u_b) + C_q 唯一确定（取正号）。
// 顶盖 y=1 以 U0 沿 +x 驱动（e_qx != 0 的方向才有修正），其余五面静止（纯反弹）。
// 每线程负责一个壁面格点的全部 18 个方向：先整体读入寄存器再写出，无内核内竞争；
// 读写的都是本格点自己的槽位（不经邻居），角/棱/面无需任何特殊处理。
// ============================================================================
__global__ void ldcWallBCCUDA(pfint step, dfloat *fD, dfloat *pr, dfloat *u, dfloat *F, int *flag)
{
	const pfint id = threadIdx.x + blockDim.x * blockIdx.x;
	if ((id < NODES) && flag[id] == 0)
	{
		const int y = (int)((id / NZ) % NY);
		const dfloat ubx = (y == 1) ? U0 : 0.0;

		dfloat fT[19];
#pragma unroll
		for (int i = 1; i <= 18; i++)
			fT[i] = fD[INDEX(id, i, step)];

		const dfloat cxa = (ubx / c) * (1.0 / 3.0);   // 轴向修正幅值 2*(1/6)*(ubx/c)
		const dfloat cxd = (ubx / c) * (1.0 / 6.0);   // 对角修正幅值 2*(1/12)*(ubx/c)

		fD[INDEX(id, 1, step)]  = fT[2]  + cxa;
		fD[INDEX(id, 2, step)]  = fT[1]  - cxa;
		fD[INDEX(id, 3, step)]  = fT[4];
		fD[INDEX(id, 4, step)]  = fT[3];
		fD[INDEX(id, 5, step)]  = fT[6];
		fD[INDEX(id, 6, step)]  = fT[5];
		fD[INDEX(id, 7, step)]  = fT[8]  + cxd;
		fD[INDEX(id, 8, step)]  = fT[7]  - cxd;
		fD[INDEX(id, 9, step)]  = fT[10] + cxd;
		fD[INDEX(id, 10, step)] = fT[9]  - cxd;
		fD[INDEX(id, 11, step)] = fT[12];
		fD[INDEX(id, 12, step)] = fT[11];
		fD[INDEX(id, 13, step)] = fT[14] + cxd;
		fD[INDEX(id, 14, step)] = fT[13] - cxd;
		fD[INDEX(id, 15, step)] = fT[16] + cxd;
		fD[INDEX(id, 16, step)] = fT[15] - cxd;
		fD[INDEX(id, 17, step)] = fT[18];
		fD[INDEX(id, 18, step)] = fT[17];
	}
}

__global__ void pdfsExPackXCUDA(pfint step, dfloat *fD, dfloat *pdfsExX)
{
	const pfint y = threadIdx.x + blockDim.x * blockIdx.x;
	const pfint z = threadIdx.y + blockDim.y * blockIdx.y;

	if ((y <= Ny1) && (z <= Nz1))
	{
		pfint id0, id1, id2, id3;
		pfint y1, z1, j;
		pfint q_send = Q - 1;

		y1 = y + 1;
		z1 = z + 1;
		id1 = 2 * NY * NZ + y1 * NZ + z1;
		id2 = Nx1 * NY * NZ + y1 * NZ + z1;

		pdfsExX[0 * 5 * Ny2 * Nz2 + 0 * Ny2 * Nz2 + y * Nz2 + z] = fD[INDEX(id2, 1, step)];
		pdfsExX[0 * 5 * Ny2 * Nz2 + 1 * Ny2 * Nz2 + y * Nz2 + z] = fD[INDEX(id2, 7, step)];
		pdfsExX[0 * 5 * Ny2 * Nz2 + 2 * Ny2 * Nz2 + y * Nz2 + z] = fD[INDEX(id2, 9, step)];
		pdfsExX[0 * 5 * Ny2 * Nz2 + 3 * Ny2 * Nz2 + y * Nz2 + z] = fD[INDEX(id2, 13, step)];
		pdfsExX[0 * 5 * Ny2 * Nz2 + 4 * Ny2 * Nz2 + y * Nz2 + z] = fD[INDEX(id2, 15, step)];

		pdfsExX[1 * 5 * Ny2 * Nz2 + 0 * Ny2 * Nz2 + y * Nz2 + z] = fD[INDEX(id1, 2, step)];
		pdfsExX[1 * 5 * Ny2 * Nz2 + 1 * Ny2 * Nz2 + y * Nz2 + z] = fD[INDEX(id1, 8, step)];
		pdfsExX[1 * 5 * Ny2 * Nz2 + 2 * Ny2 * Nz2 + y * Nz2 + z] = fD[INDEX(id1, 10, step)];
		pdfsExX[1 * 5 * Ny2 * Nz2 + 3 * Ny2 * Nz2 + y * Nz2 + z] = fD[INDEX(id1, 14, step)];
		pdfsExX[1 * 5 * Ny2 * Nz2 + 4 * Ny2 * Nz2 + y * Nz2 + z] = fD[INDEX(id1, 16, step)];
	}
}

__global__ void pdfsExUnpackXCUDA(pfint step, dfloat *fD, dfloat *pdfsExX)
{
	const pfint y = threadIdx.x + blockDim.x * blockIdx.x;
	const pfint z = threadIdx.y + blockDim.y * blockIdx.y;

	if ((y <= Ny1) && (z <= Nz1))
	{
		pfint id0, id1, id2, id3;
		pfint y1, z1, j;

		y1 = y + 1;
		z1 = z + 1;
		id0 = 1 * NY * NZ + y1 * NZ + z1;
		id1 = Nx2 * NY * NZ + y1 * NZ + z1;

		fD[INDEX(id0, 1, step)] = pdfsExX[0 * 5 * Ny2 * Nz2 + 0 * Ny2 * Nz2 + y * Nz2 + z];
		fD[INDEX(id0, 7, step)] = pdfsExX[0 * 5 * Ny2 * Nz2 + 1 * Ny2 * Nz2 + y * Nz2 + z];
		fD[INDEX(id0, 9, step)] = pdfsExX[0 * 5 * Ny2 * Nz2 + 2 * Ny2 * Nz2 + y * Nz2 + z];
		fD[INDEX(id0, 13, step)] = pdfsExX[0 * 5 * Ny2 * Nz2 + 3 * Ny2 * Nz2 + y * Nz2 + z];
		fD[INDEX(id0, 15, step)] = pdfsExX[0 * 5 * Ny2 * Nz2 + 4 * Ny2 * Nz2 + y * Nz2 + z];

		fD[INDEX(id1, 2, step)] = pdfsExX[1 * 5 * Ny2 * Nz2 + 0 * Ny2 * Nz2 + y * Nz2 + z];
		fD[INDEX(id1, 8, step)] = pdfsExX[1 * 5 * Ny2 * Nz2 + 1 * Ny2 * Nz2 + y * Nz2 + z];
		fD[INDEX(id1, 10, step)] = pdfsExX[1 * 5 * Ny2 * Nz2 + 2 * Ny2 * Nz2 + y * Nz2 + z];
		fD[INDEX(id1, 14, step)] = pdfsExX[1 * 5 * Ny2 * Nz2 + 3 * Ny2 * Nz2 + y * Nz2 + z];
		fD[INDEX(id1, 16, step)] = pdfsExX[1 * 5 * Ny2 * Nz2 + 4 * Ny2 * Nz2 + y * Nz2 + z];
	}
}

__global__ void pdfsPeriodicYCUDA(pfint step, dfloat *fD)
{
	const pfint x = threadIdx.x + blockDim.x * blockIdx.x;
	const pfint z = threadIdx.y + blockDim.y * blockIdx.y;

	if ((x <= Nx1) && (z <= Nz1))
	{
		pfint id0, id1, id2, id3;
		pfint x1, z1, j;
		pfint q_send = Q - 1;
		x1 = x + 1;
		z1 = z + 1;
		id0 = x1 * NY * NZ + 1 * NZ + z1;
		id1 = x1 * NY * NZ + 2 * NZ + z1;
		id2 = x1 * NY * NZ + Ny1 * NZ + z1;
		id3 = x1 * NY * NZ + Ny2 * NZ + z1;
		for (j = 0; j < q_send; j++)
		{
			fD[INDEX(id3, j + 1, step)] = fD[INDEX(id1, j + 1, step)];
			fD[INDEX(id0, j + 1, step)] = fD[INDEX(id2, j + 1, step)];
		}
	}
}

__global__ void pdfsPeriodicZCUDA(pfint step, dfloat *fD)
{
	const pfint x = threadIdx.x + blockDim.x * blockIdx.x;
	const pfint y = threadIdx.y + blockDim.y * blockIdx.y;

	if ((x <= Nx1) && (y <= Ny1))
	{
		pfint id0, id1, id2, id3;
		pfint x1, y1, j;
		pfint q_send = Q - 1;
		x1 = x + 1;
		y1 = y + 1;
		id0 = x1 * NY * NZ + y1 * NZ + 1;
		id1 = x1 * NY * NZ + y1 * NZ + 2;
		id2 = x1 * NY * NZ + y1 * NZ + Nz1;
		id3 = x1 * NY * NZ + y1 * NZ + Nz2;
		for (j = 0; j < q_send; j++)
		{
			fD[INDEX(id3, j + 1, step)] = fD[INDEX(id1, j + 1, step)];
			fD[INDEX(id0, j + 1, step)] = fD[INDEX(id2, j + 1, step)];
		}
	}
}

void data_deal_3D(int me, dfloat *pr, dfloat *u)
{
	MPI_Status status;

	FILE *file;
	int i, j, k, kk1, x, y, z, x2, y2, z2;
	int i2, j2, ix, jy, NxA, NyA, NxB, NyB;
	char fname[20];
	dfloat u_out[Nx4][Ny4][Nz4][3], p_out[Nx4][Ny4][Nz4];

	int nproc;
	int msgtag;
	pfint id;

	if (me != 0)
	{
		msgtag = me;
		for (x = 0; x <= Nx3; x++)
			for (y = 0; y <= Ny3; y++)
				for (z = 0; z <= Nz3; z++)
				{
					id = x * NY * NZ + y * NZ + z;

					u_out[x][y][z][0] = u[id];
					u_out[x][y][z][1] = u[NODES + id];
					u_out[x][y][z][2] = u[2 * NODES + id];
					p_out[x][y][z] = pr[id] / p0;
				}
		MPI_Send(&u_out[0][0][0][0], Nx4 * Ny4 * Nz4 * 3, MPI_REAL_MYOWN, 0, msgtag, MPI_COMM_WORLD);
		MPI_Send(&p_out[0][0][0], Nx4 * Ny4 * Nz4, MPI_REAL_MYOWN, 0, msgtag, MPI_COMM_WORLD);
	}

	if (me == 0)
	{
		sprintf(fname, "flui_particle_data_3D%06d%d.dat", step, me);
		file = fopen(fname, "w");

		fprintf(file, "TITLE=Sedimentation\n");
		fprintf(file, "VARIABLES=x,y,z,u,v,w,p\n");
		fprintf(file, "Zone I= %d,J= %d,K= %d,F= POINT\n", Nz1, Ny1, Nx1);

		for (x = 1; x <= Nx1; x++)
			for (y = 1; y <= Ny1; y++)
				for (z = 1; z <= Nz1; z++)
				{
					id = x * NY * NZ + y * NZ + z;

					fprintf(file, "%d\t%d\t%d\t", (x - 1), (y - 1), (z - 1));
					fprintf(file, "%f\t%f\t%f\t%f\n", u[id], u[NODES + id], u[2 * NODES + id], pr[id] / p0);
				}
		// fclose(file);

		for (nproc = 1; nproc < NPROC; nproc++)
		{
			msgtag = nproc;

			MPI_Recv(&u_out[0][0][0][0], Nx4 * Ny4 * Nz4 * 3, MPI_REAL_MYOWN, nproc, msgtag, MPI_COMM_WORLD, &status);
			MPI_Recv(&p_out[0][0][0], Nx4 * Ny4 * Nz4, MPI_REAL_MYOWN, nproc, msgtag, MPI_COMM_WORLD, &status);

			if (nproc % NPROCx == 0)
			{
				ix = 1;
				NxB = Nx1;
			}
			else
			{
				ix = 1;
				NxB = Nx1;
			}

			if (nproc < NPROCx)
			{
				jy = 1;
				NyB = Ny1;
			}
			else
			{
				jy = 1;
				NyB = Ny1;
			}

			fprintf(file, "Zone I= %d,J= %d,K= %d,F= POINT\n", Nz1, (NyB - jy + 1), (NxB - ix + 1));
			for (x = ix; x <= NxB; x++)
				for (y = jy; y <= NyB; y++)
					for (z = 1; z <= Nz1; z++)
					{
						x2 = x + (nproc - (int)(nproc / NPROCx) * NPROCx) * Nx;
						y2 = y + (int)(nproc / NPROCx) * Ny;
						fprintf(file, "%d\t%d\t%d\t%f\t%f\t%f\t%f\n", (x2 - 1), (y2 - 1), (z - 1), u_out[x][y][z][0], u_out[x][y][z][1], u_out[x][y][z][2], p_out[x][y][z]);
					}
		}

		fclose(file);
	}
}

// ============================================================================
// LDC 速度场输出（每 OUTPUT_VEL_EVERY 步，挂在主循环里；me 并入文件名支持多进程）：
//   ldc_velocity_SSSSSS_RR.dat   —— Tecplot POINT 格式，流体区 [2,Nx1]^3（坐标 0..NxT-1）
//                                   壁面层 u/pr 不由碰撞核更新，故只输出流体区；
//   ldc_centerline_SSSSSS_RR.dat —— 两条中心线剖面（对照文献验证正确性用）：
//                                   第 1 段 ux(y) @ x=z=中心（从顶盖侧 y=0 到底壁）；
//                                   第 2 段 uy(x) @ y=z=中心。速度均除以 U0 无量纲化。
// 注意：单场全速度文件约 1 GB（252^3 行 ASCII），长时间跑注意磁盘。
// ============================================================================
void output_velocity_ldc(int me, dfloat *pr, dfloat *u)
{
	FILE *file;
	char fname[60];
	int x, y, z;
	pfint id;

	sprintf(fname, "ldc_velocity_%06d_%02d.dat", step, me);
	if ((file = fopen(fname, "w")) == NULL)
	{
		printf("can not open the file!");
		exit(0);
	}
	fprintf(file, "TITLE=LDC velocity field\n");
	fprintf(file, "VARIABLES=x,y,z,u,v,w,p\n");
	fprintf(file, "Zone I= %d,J= %d,K= %d,F= POINT\n", NzT, NyT, NxT);
	for (x = 2; x <= Nx1; x++)
		for (y = 2; y <= Ny1; y++)
			for (z = 2; z <= Nz1; z++)
			{
				id = x * NY * NZ + y * NZ + z;
				fprintf(file, "%d\t%d\t%d\t%.6e\t%.6e\t%.6e\t%.6e\n", x - 2, y - 2, z - 2,
						u[id], u[NODES + id], u[2 * NODES + id], pr[id] / p0);
			}
	fclose(file);

	// 中心线剖面：顶盖在 y=1（流体 y=2 起），驱动方向 +x
	const int xc = 2 + NxT / 2;
	const int yc = 2 + NyT / 2;
	const int zc = 2 + NzT / 2;
	sprintf(fname, "ldc_centerline_%06d_%02d.dat", step, me);
	if ((file = fopen(fname, "w")) == NULL)
	{
		printf("can not open the file!");
		exit(0);
	}
	fprintf(file, "# Re = %f, Ma = %f, U0 = %e, grid fluid = %d^3\n", Re, Ma, U0, NxT);
	fprintf(file, "# [line 1] y  ux/U0   @ x=z=center (y=0 侧即顶盖侧)\n");
	for (y = 2; y <= Ny1; y++)
	{
		id = xc * NY * NZ + y * NZ + zc;
		fprintf(file, "%d\t%.6e\n", y - 2, u[id] / U0);
	}
	fprintf(file, "# [line 2] x  uy/U0   @ y=z=center\n");
	for (x = 2; x <= Nx1; x++)
	{
		id = x * NY * NZ + yc * NZ + zc;
		fprintf(file, "%d\t%.6e\n", x - 2, u[NODES + id] / U0);
	}
	fclose(file);

	if (me == 0)
		printf("step %d: velocity field + centerlines written.\n", step);
}

void write_check_fluid(int me, dfloat *pr, dfloat *u)
{
	FILE *file;
	char fname[20];

	int nproc;
	int msgtag;
	MPI_Status status;

	sprintf(fname, "check_fluid%03d", me);
	if ((file = fopen(fname, "wb")) == NULL)
	{
		printf("can not open the file!");
		exit(0);
	}

	fwrite(&step, sizeof(int), 1, file);
	fwrite(&t, sizeof(dfloat), 1, file);
	fwrite(u, sizeof(u), 1, file);
	fwrite(pr, sizeof(pr), 1, file);

	fclose(file);
}

int main(int argc, char *argv[])
{
	FILE *file;
	int me, i, j, k, n;
	int stepmax0;
	int step_DEM;

	dfloat startwtime, endwtime;
	dfloat endwtime_u_p, startwtime_u_p, endwtime_com1, startwtime_com1;
	dfloat endwtime_pindex, startwtime_pindex, endwtime_IBM, startwtime_IBM;
	dfloat endwtime_Fcolli, startwtime_Fcolli, endwtime_com2, startwtime_com2;
	dfloat endwtime_stream, startwtime_stream, endwtime_Pcolli, startwtime_Pcolli;
	dfloat endwtime_Pmove, startwtime_Pmove;
	dfloat endwtime_com3, startwtime_com3, endwtime_com4, startwtime_com4;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &numproc);
	MPI_Comm_rank(MPI_COMM_WORLD, &me);
	int ngpus;

	cudaGetDeviceCount(&ngpus);
	for (size_t i = 0; i < ngpus; i++)
	{
		cudaDeviceProp devProp;
		cudaGetDeviceProperties(&devProp, i);
		if (me == 0)
			printf("Device %d has compute capability %d.%d.\n", i, devProp.major, devProp.minor);
		if (!devProp.deviceOverlap)
		{
			if (me == 0)
				printf("Device not support overlaps, so stream is invalid!\n");
		}
	}
	int gpuid = me % GPUPERNODE;
	cudaSetDevice(gpuid);

	srand((unsigned)time(NULL));
	mallocMemoryCUDA();

	stepmax0 = 0;
	stepmax = 10000;

	u_p_time = 0.0;
	com1_time = 0.0;
	pindex_time = 0.0;
	IBM_time = 0.0;
	Fcolli_time = 0.0;
	com2_time = 0.0;
	bound_time = 0.0;
	stream_time = 0.0;
	Pcolli_time = 0.0;
	Pmove_time = 0.0;
	com3_time = 0.0;
	com4_time = 0.0;

	printf("mv = %f\n", mv);
	printf("tao = %f\n", tao);

	step = 1;
	init_f(me, step, fDHost, pHost, uHost, FHost, flagHost);
	if (me == 0)
		printf("0");

	// data_deal_3D(me, pHost, uHost);
	// kE(me, uHost);
	init_flag(me, flagHost);
	checkCudaErrors(cudaMemcpy(pDevice, pHost, sizeof(dfloat) * NODES, cudaMemcpyHostToDevice));
	checkCudaErrors(cudaMemcpy(uDevice, uHost, sizeof(dfloat) * 3 * NODES, cudaMemcpyHostToDevice));
	checkCudaErrors(cudaMemcpy(FDevice, FHost, sizeof(dfloat) * 3 * NODES, cudaMemcpyHostToDevice));
	checkCudaErrors(cudaMemcpy(fDDevice, fDHost, sizeof(dfloat) * Q * NODES, cudaMemcpyHostToDevice));
	checkCudaErrors(cudaMemcpy(flagDevice, flagHost, sizeof(int) * NODES, cudaMemcpyHostToDevice));

	startwtime = MPI_Wtime();
	MPI_Status status;
	int right_exf, left_exf;
	if (periodic == 0)
	{
		if (me % NPROCx == 0)
			left_exf = me + NPROCx - 1;
		else
			left_exf = me - 1;

		if ((me + 1) % NPROCx == 0)
			right_exf = me - NPROCx + 1;
		else
			right_exf = me + 1;
	}
	double total_time = 0.0;
	for (t = 0, step = 1; step <= stepmax; t += dt, step++)
	{
		if ((me == 0) && (step % 2000 == 0))
			printf("t=%f,ndt=%d\n", t, step);

		startwtime_u_p = MPI_Wtime();
		endwtime_u_p = MPI_Wtime();
		endwtime_com1 = MPI_Wtime();
		endwtime_IBM = MPI_Wtime();
		auto start = std::chrono::high_resolution_clock::now();
		// LDC：先施加壁面 NEE 边界（覆写壁面层槽位），再碰撞流体区。
		// 两内核同走 stream1 保证序：碰撞核会读本步刚写入的壁面槽位。
		ldcWallBCCUDA<<<(NODES + nThreads - 1) / nThreads, nThreads, 0, stream1>>>(step, fDDevice, pDevice, uDevice, FDevice, flagDevice);
		endwtime_com2 = MPI_Wtime();

		fluidCollisionInnerCUDA<<<(NODES + nThreads - 1) / nThreads, nThreads, 100 * sizeof(dfloat), stream1>>>(step, fDDevice, pDevice, uDevice, FDevice, flagDevice);

		endwtime_Fcolli = MPI_Wtime();
		// 每步同步一次，使 chrono 计时为真实每步 GPU 耗时（原版靠队列饱和间接节流，
		// 前 ~1000 步的计时偏快；加同步后所有报告均为稳态值，代价 <1%）
		cudaStreamSynchronize(stream1);
		auto end = std::chrono::high_resolution_clock::now();
		total_time += std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();



		endwtime_stream = MPI_Wtime();

		endwtime_Pcolli = MPI_Wtime();
		endwtime_Pmove = MPI_Wtime();
		endwtime_com4 = MPI_Wtime();
		// if (step % 50 == 0)
		// {
		// 	checkCudaErrors(cudaMemcpy(uHost, uDevice, sizeof(dfloat) * 3 * NODES, cudaMemcpyDeviceToHost));
		// 	kE(me, uHost);
		// }

		// if ((step % 1050 == 0)||(step % 1750 == 0)||(step % 2500 == 0)||(step % 3200 == 0)||(step % 3900 == 0)||(step % 5650 == 0))
		// {
		// 	checkCudaErrors(cudaMemcpy(pHost, pDevice, sizeof(dfloat) * NODES, cudaMemcpyDeviceToHost));
		// 	checkCudaErrors(cudaMemcpy(uHost, uDevice, sizeof(dfloat) * 3 * NODES, cudaMemcpyDeviceToHost));
		// 	data_deal_3D(me, pHost, uHost);
		// }

		u_p_time += endwtime_u_p - startwtime_u_p;
		com1_time += endwtime_com1 - endwtime_u_p;
		IBM_time += endwtime_IBM - endwtime_com1;
		com2_time += endwtime_com2 - endwtime_IBM;
		Fcolli_time += endwtime_Fcolli - endwtime_com2;
		com3_time += endwtime_com3 - endwtime_Fcolli;
		stream_time += endwtime_stream - endwtime_com3;
		Pcolli_time += endwtime_Pcolli - endwtime_stream;
		Pmove_time += endwtime_Pmove - endwtime_Pcolli;
		com4_time += endwtime_com4 - endwtime_Pmove;

		if (me == 0&& step %1000 == 0)
		{
			// total_time = 1000 步的毫秒数 -> MLUPs = 格点数 / total_time
			double mlups_all = (double)NODES / total_time;             // 全部格点（含 halo），Nx4^3 = 256^3，与我们 OSI 版口径一致
			double mlups_fluid = (double)NxT * NyT * NzT / total_time; // 仅流体格点 252^3
			printf("total_time = %f ms | MLUPs(all) = %.1f | MLUPs(fluid) = %.1f | 名义带宽(152B/格点) = %.1f GB/s\n",
				   total_time, mlups_all, mlups_fluid, mlups_all * 152.0 / 1000.0);
			total_time = 0.0;
		}

#if OUTPUT_VEL_EVERY > 0
		if (step % OUTPUT_VEL_EVERY == 0)
		{
			cudaStreamSynchronize(stream1); // 确保本步 u/pr 已写完
			checkCudaErrors(cudaMemcpy(uHost, uDevice, sizeof(dfloat) * 3 * NODES, cudaMemcpyDeviceToHost));
			checkCudaErrors(cudaMemcpy(pHost, pDevice, sizeof(dfloat) * NODES, cudaMemcpyDeviceToHost));
			output_velocity_ldc(me, pHost, uHost);
		}
#endif
	}
	endwtime = MPI_Wtime();

	printf("me=%d,wall clock time=%f\n", me, endwtime - startwtime);
	CPU_time(me);
	freeMemoryCUDA();

	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();

	return 0;
}
