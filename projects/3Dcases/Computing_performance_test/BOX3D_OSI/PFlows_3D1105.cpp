//---------------------------------------------------------------------------------
// Code for Fully Resolved Simulation of 3D particle-laden flows, 
// based on Lattice-Boltzmann method coupling with Immersed Boundary method and Discrete Element method. 
// LBM: SRT-D3Q19model; IBM:Boundary-thickening direct forcing Boundary thickening; DEM: three-equation linear spring-dashpot model.
// High performance computing: MPI parallel version
//
// To compile using intel compiler 
//     mpicc *.cpp -O3 -ip -ipo -xT
// 
// --------------------------------------------------------------------------------
//2018.1.1, V1.0
//    LBM+(DF)IBM,���ܹ̱ڵĲ۵��������ζ��棬�����������ѹ���ݶ���������������Poiseuille����
//    LBM:LBGK,D3Q9; IBM: BTDF,��ֵ��������3������delta������
//    MPI���У������У���x���򻮿飬y��z�����С�ܵ����ƣ�ֻ��ģ��ϸ����ֱͨ����
//    �����̳����ã���������ģ�ͣ�������Һ���������ڴ��ܶȱȵ��������Ƿ����δ֪��
//2018.2.14, V1.1
//    DEMģ�ͼ��룬����ģ������������.������Hertz���ۣ�������Mindlin and Deresiewicz����.
//    DEMģ�Ͳ������������Ե���-����ģ�ͣ����ǿ���֮�䷨��Ӵ���������Ӵ����Լ�����Ħ�����ء�
//    ÿ�����������Լ�Ϊһ�����ɣ�һ���������Լ�һ������������ϡ�
//    �����е���DEM����ʱ�䲽���Ա�֤dt(LBM)=5-10*dt(DEM)
//    Cundall&Strack,Geotechnique,1979(��������); Iwashita&Oda,J.Eng.Mech,1998(���ǹ���); Zhao&Jiang,et al.,AIChE J,2009(����ģ��).
// --------------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <omp.h>
#include <vector>
#include <algorithm>
//#include <stdatomic.h>
#include "mpi.h"
#include <gptl.h>
#define PI   (3.141592653589793238462643383279)
#define CHECKPOINT  0
#define NF    1                //number of multi-forcing
#define G     0.0008528815 //1.071 //183 //3.194 // 981.0            //gravity, cm/s2
//#define alpha_G   (PI/7.)            
//#define sita_G    (PI/8.)  
//double  G_dire[3] = { -cos(alpha_G)*sin(sita_G), -cos(alpha_G)*cos(sita_G), -sin(alpha_G) };  //direction of gravity
double  G_dire[3] = { 0, -1, 0 };
#define periodic 1                //����������Ϊ0��������Ϊ1
#define periodic_x	1
#define periodic_y	1
#define periodic_z	1

// particle diameter, unit:cm
#define D     1.0 //0.0625           //5 micron
#define R     6               //number of grids in sphere radius direction

// fluid zone, unit:cm         //40D*4D*4D
#define Lx    (2.0)          //150 micron
#define Ly    (2.0)          //50 micron
#define Lz    (2.0)          //50 micron
//fluid properties
#define rhof   1  //1.0           // fluid density, g/cm3

// particle arrangement  
//�����ɹ�������,�������У��������
#define x_np   32  //15              
#define y_np   32  //30
#define z_np   3  //4
#define np     1451 //57 //19  //(x_np*y_np*z_np)  //number of spheres, random distribution in this channel flow.
#define fai    0.05
//������������������Ĳ���
#define Ngap   10                // 1.376, 1.736, 2.187 and 3.74 D for volume fraction of 0.2, 0.1, 0.05 and 0.01 respectively
#define gapx   (1.15*D)            //distance between two particles centers
#define gapy   (1.15*D)            //distance between two particles centers
#define gapz   (1.15*D)            //distance between two particles centers
#define g_angle (180.0)          //direction of gravity, -z direction

#define rhop    320.0 //2 //1.0             // particle density, g/cm3  
#define Vp      (PI*D*D*D/6.) 
#define mp      (Vp*rhop)      //particle mass, g
#define Mp_iner (mp*D*D/10.)          //moment of particle  inertia
#define mf      (Vp*rhof)      //virtual fluid mass inside particle, g
#define Mf_iner (mf*D*D/10.)          //virtual moment of fluid inertia inside particle

//Global Eulerian grids and nodes
#define NxT   300 //(int)(Lx/D*2*R)        // 600*200*200 = 2400 0000 
#define NxT1  (NxT+1)
#define NyT   300  //(int)(Ly/D*2*R+4) 
#define NyT1  (NyT+1)
#define NzT   300 //(int)(Lz/D*2*R+4)        
#define NzT1  (NzT+1)
#define R_boun  (int)(Lz/D*R)      //Բ���߽����

//MPI partitions��Ŀǰ����ά�����з�
#define NPROCx  2          // slices for x direction
#define NPROCy  2       // slices for y direction
#define NPROCz  1              // slices for z direction
#define NPROC   (NPROCx*NPROCy*NPROCz)

//Local Eulerian grids and nodes
#define Nx    (int)(NxT/NPROCx)     //grids 40*200*200=160,0000
#define Ny    (int)(NyT/NPROCy) 
#define Nz    (int)(NzT/NPROCz)      
#define Nx1   (Nx+1)              // nodes
#define Ny1   (Ny+1)
#define Nz1   (Nz+1)
#define Nx2   (Nx+2)              // extension of ghost for data exchange between adjacent slices
#define Ny2   (Ny+2)
#define Nz2   (Nz+2) 
#define Nx3   (Nx+3) 
#define Ny3   (Ny+3)
#define Nz3   (Nz+3)
#define Nx4   (Nx+4)     //��������������˫������2�㻺���
#define Ny4   (Ny+4)
#define Nz4   (Nz+4)

//parameters for LB D3Q19 scheme
#define Q      19
#define IBM_DEBUG_OUTPUT 0
#define IBM_DEBUG_OUTPUT_POLY 0

//
#define if_stationary 1		//0:dynamic 1:stationary
#if if_stationary
	#define Res_target (81.)
#endif
//boundary condition
#if (periodic_x == 0) 
#define boundary_x1	1	//if 0 neumann;if 1 dirichlet
#define boundary_x2	1
double boundary_x1_u[3] = { 0., 0., 0. };
double boundary_x2_u[3] = { 0., 0., 0. };
#endif
#if (periodic_y == 0) 
#define boundary_y1	1
#define boundary_y2	0
double boundary_y1_u[3] = { 0., 0.6, 0. };
double boundary_y2_u[3] = { 0., 0., 0. };
#endif
#if (periodic_z == 0) 
#define boundary_z1	1
#define boundary_z2	1
double boundary_z1_u[3] = { 0., 0., 0. };
double boundary_z2_u[3] = { 0., 0., 0. };
#endif

//Polydisperse particle parameters
#define if_polydisperse 1	//1:polydisperse 0:not polydisperse
#define dx_poly (1.0 / 20.)	//gird size at LBM unit,

//LB parameters
#define dx   (D/(2*R))        //grid size, cm, =0.25micron =0.000,000,25 m 
#define dt   0.001        //time step :s   =1.0xE(-7) s
//#define c    (dx/dt)          //grid velocity, cm/s
#if if_polydisperse
	#define c    (dx_poly/dt)
#else
	#define c    (dx/dt)
#endif
#define cs   (c / sqrt(3.0))
#define p0   (rhof*(cs*cs))
#define mv   0.005 	//1.0 // 0.1  //0.0101            //kinematic viscosity: cm2/s, =1.0xE(-6)m2/s, 0.01 for Re200, 0.009 for Re108
#define tao  (mv / (dt*cs*cs) + 0.5)            // tao is relaxation time of collision step, should between 0.5 and 1
#define MA_WARN 0.10				// Mach-number warning threshold
#define omega (1.0 / tao)            // tao is relaxation time of collision step, should between 0.5 and 1
#define ax_f  0.0          //forcing acceleration of fluids, cm/s2, to induce Poiseuille flow with large pressure difference
#define dp    0.0             //pressure drop due to the difference between inlet pressure and outlet pressure, Pa=g/s2
#define delta_p   (rhof*ax_f*Lx+dp)    //apparent pressure drop, Pa
#define Dh   (4*(Ly*Lz)/(2*Ly+2*Lz))   //hydraulic diameter of the channel, cm
#define Ua   (delta_p*Dh*Dh/(32*rhof*mv*Lx))    //cm/s
#define Rec  (Ua*Dh/mv)           //channel Reynolds number
#define Rep  (Ua*D/mv)           //particle Reynolds number
#define c_square  (c*c)  
#define cs_square  (cs*cs)  
#define cs_4pow  (cs_square*cs_square)  

//distribution parameters of Lagrangian points at particle surface������ʽ�ֲ�
//#define rdr  (0.636067453)                 //retraction distance of particle surface
//#define rb   (R-rdr)               //calibrated radius
#define rb    5.545083978 	//7.533186273  //11.52169816  //5.544988997   //(pow((R*R*R + (R - 1)*(R - 1)*(R - 1)) / 2.0, 1 / 3.0))  //Feng�ă�����ʽ
#define ns   (int)(PI*rb/0.8+1)    //����ִ���
#define nt1  (int)(2*PI*rb/0.8)    //�������point����
#define LP_dr 1.50                  //����߽羶����, 12~1.5 for 2p-delta function, 1.5~1.9 for 3p-delta function, 2.0~2.6 for delta function
#define LP_dr_2p 1.25              //����߽羶����, 12~1.5 for 2p-delta function, 1.5~1.9 for 3p-delta function, 2.0~2.6 for delta function
#define drr  (rb+1.5)              //auxiliary parameter to determine nodes of surrounding fluid 
#define L_IB (int)(2*drr)

//polydisperse particle parameters
int		R_poly[] = { 20, 10 };	//polydisperse particle parameters by grid unit
double	R_poly_ratio[] = { 161., 1290. };	//polydisperse particle 
double	rhop_poly[] = { 1472., 3200. };
//int R_poly_num, R_poly_min, R_poly_max;	//polydisperse particle parameters
#define R_poly_num 2
#define R_poly_max 20
#define R_poly_min 10
#define rb_poly_max 19.5093281//5.545083978 		//function call is not allowed in a constant expression,so need to type the value here:pow((pow(R_poly_max,3)+pow((R_poly_max-1),3)/2),1/3)
#define ns_max (int)(PI * rb_poly_max / 0.8 + 1.)
#define nt1_max (int)(2. * PI * rb_poly_max / 0.8 + 1.)
double rb_poly[R_poly_num];						//storage of retraction radius by each radius, size is [R_poly_num] 
double xb_poly[R_poly_num][ns_max][nt1_max], yb_poly[R_poly_num][ns_max][nt1_max], zb_poly[R_poly_num][ns_max][nt1_max];	//storage of points at particle surface by each radius, size is [R_poly_num][ns_max][nt1max]
double LP_area_poly[R_poly_num][ns_max];		//storage of area of points at each band by each radius, size is [R_poly_num][ns_max]
double nt_poly[R_poly_num][ns_max];			//storage of number of points at each band by each radius, size is [R_poly_num][ns_max]
int	   num_points_poly[R_poly_num];	//storage of number of points by each radius, size is [R_poly_num]

//DEM parameters
#define dt_DEM      (dt/2)        //time step :s   =1.0xE(-7) s
//#define E_Young     2.6E8          //�������������ϵ���ģ��2.6x10^8Pa
#define v_Poisson   0.25           //���ɱ�,����
#define G_Shear    (E_Young / (2 * (1 + v_Poisson)))        //����ģ��
#define e_p         0.9            //�ָ�ϵ��,coefficient of restitution
#define fs_DEM      0.3            //����Ħ��ϵ��,coefficient of sliding friction
#define fr_DEM      0.00005          //����Ħ��ϵ��,coefficient of rolling friction

//non-dimensional parameters, see course note for <fundamentals of multiphase flow>
#define Ar    ( G*D*D*D/mv/mv*(rhop/rhof-1.0) )   //Archimids number
#define C1    ( log10(Ar) )
#define R1    ( 0.99947+0.01853*sin(1.848*C1-3.14) )
#define P1    ( ((0.0017795*C1-0.0573)*C1+1.0315)*C1-1.26222  )
#define Re0   ( pow(10.,P1+log10(R1))  )         //Single Particle Reynolds number in infinite fluid, determined by relations of Hartman et al. (Chem. Eng. Sci. 1992, 47, 3162) much better.
#define Cd0   ( 4.*Ar/3./Re0/Re0       )         //drag coef.
#define Ud0   ( Re0*mv/D          )         //Single particle sedimentation velocity, cm/s
#define Trp0  ( Ud0/(G*(1.-rhof/rhop)) )         //Single particle relaxation time, s
#define St    ( Ud0/D                  ) 

#define fai_p (Vp*np/(NxT*NyT*NzT*dx*dx*dx))  //�����������
#define Ff    (G*(rhop/rhof - 1.0)*fai_p)     //body force for balancing the particle gravity
#define u_sed (1.)    //���������������ٶ�
#if if_stationary == 0
	#define u_init_x	(u_sed * G_dire[0] * pow((1 - fai_p), 5))		//0.0
	#define u_init_y	(u_sed * G_dire[1] * pow((1 - fai_p), 5))	//-pow((1 - fai), 5)	//
	#define u_init_z	(u_sed * G_dire[2] * pow((1 - fai_p), 5))	//0.0
	#define u_init_x_poly (u_sed * G_dire[0] * pow((1 - phi_p_poly), 5))
	#define u_init_y_poly (u_sed * G_dire[1] * pow((1 - phi_p_poly), 5))
	#define u_init_z_poly (u_sed * G_dire[2] * pow((1 - phi_p_poly), 5))
#else
	#define u_init_x	0.
	#define u_init_y	0.
	#define u_init_z	0.
	#define u_init_x_poly 0.
	#define u_init_y_poly 0.
	#define u_init_z_poly 0.
#endif
#define t_stokes	(u_sed*dt/D)   
#define step_stokes (int)(150/t_stokes)   //150��stokesʱ����ʼʱ�䲽
double Ff_body[3] = { Ff*G_dire[0], Ff*G_dire[1], Ff*G_dire[2]};
double Ff_body_poly[3];
double Ff_poly;
double R_sauter_mean;

//lubrication force parameters
double k1_elas, k2_elas;
//k_elas = 1000000;
//#define k_elas 1000000
#define epp1  0.5
#define epp2  0.05
#define epw1  0.025
#define epw2  0.0025
#define safe  (1.0) //(0.025*R) //(1.0*dx)   //(2/3*dx)
#define safe1  (0.005*R) //(1.0*dx)   //(2/3*dx)

//lubrication force and par-par collision(DEM) parameters //by TanzFe 2025.06
#define e_restitution	0.97	//the dry coefficient of restitution
#define alpha_DEM	(2.24 - 2.26 * pow(e_restitution, 0.4))	//a constant related to e_restitution for calculating eta_n and eta_t
#define miu_c_DEM		0.3		//the firction coefficient
#define sigma_Poisson	0.33	//Poisson ratio
#define E_Young			(10000000.0 * rhof * u_sed * u_sed)//(100000.0 * rhof * (4*D/3*(rhop/rhof-1)*G))   //Young's modulus 2.6x10^8Pa
#define G_shear			(E_Young / (2 * (1 + sigma_Poisson)))	//particle shear modulus
#define E_Young_poly	(10000000.0 * rhof * (4*2*R_poly_max*dx_poly/3*(rhop_poly[0]/rhof-1)*G))
#define G_shear_poly	(E_Young_poly / (2 * (1 + sigma_Poisson)))
#define	K_n		2000.				//Normal Spring Stiffness linear
#define K_t		(K_n *2. /7.)		//tangential spring stiffness linear
#define K_n2	(4/3/(2*(1 - pow(sigma_Poisson,2))/E_Young)/sqrt(2/(D/2)))	//Normal Spring Stiffness
#define K_t2	(8/(2*(2-sigma_Poisson)/G_shear)/sqrt(2/(D/2)))	//tangential spring stiffness
#define eta_n	(-2.*log(e_restitution)*sqrt(K_n*mp/2/(PI*PI+log(e_restitution)*log(e_restitution))))	//Normal damping coefficient linear
#define eta_t	(eta_n/2.)			//tangential damping coefficient linear
#define eta_n2	(alpha_DEM*sqrt(mp/2*K_n2))	//Normal damping coefficient
#define eta_t2	(alpha_DEM*sqrt(mp/2*K_t2))	//tangential damping coefficient
#define iflub	1				//if =1 lubrication force will be contained
#define iflub_DEM_test	0		//if = 1 will printf the contact information
#define iflub_DEM_output 0		//if = 1 will printf the contact information for every process
#define if_linear_DEM	0		//if = 1 will use linear DEM else use non-linear DEM
#define epsilon_act		(dx/(D/2.))	//the threshold that lubrication force act
#define epsilon_max		0.001	//the threshold that lubrication force increase to max
#define epsilon_dis		0.001	//the threshold that lubrication force disappear
#define max_contact_num 12		//max num of the contact particle at one time  

#if iflub_DEM_output == 1
	static FILE* shared_contact_file = NULL;      // 所有线程共享
	static int    shared_contact_interval = -1;   // 当前打开文件对应的千步区间
#endif

#define if_gptl 1 

//PID parameters
#define K_pid (0.5)
#define T_inte (0.)
#define T_deri (0.5)

#define Vp_dense (4.*PI* R* R* R/3)
#define np_local (int)(0.7*(Nx+4*R+2*safe)*(Ny+4*R+2*safe)*(Nz+4*R+2*safe)/Vp_dense) 
#define np_comm_yface (int)(0.7*(Nx+2*R+safe)*(4*R+2*safe)*(Nz+2*R+safe)/Vp_dense)          
#define np_comm_xface (int)(0.7*(4*R+2*safe)*(Ny+2*R+safe)*(Nz+2*R+safe)/Vp_dense)         
#define np_comm_corner (int)(0.7*(4*R+2*safe)*(4*R+2*safe)*(Nz+2*R+safe)/Vp_dense) 
#define np_local_poly (int)(0.7*(Nx+4*R_poly_max+2*safe)*(Ny+4*R_poly_max+2*safe)*(Nz+4*R_poly_max+2*safe)/(4*PI*R_poly_min*R_poly_min*R_poly_min/3))
#define np_comm_yface_poly (int)(0.7*(Nx+2*R_poly_max+safe)*(4*R_poly_max+2*safe)*(Nz+2*R_poly_max+safe)/(4*PI*R_poly_min*R_poly_min*R_poly_min/3))          
#define np_comm_xface_poly (int)(0.7*(4*R_poly_max+2*safe)*(Ny+2*R_poly_max+safe)*(Nz+2*R_poly_max+safe)/(4*PI*R_poly_min*R_poly_min*R_poly_min/3))         
#define np_comm_corner_poly (int)(0.7*(4*R_poly_max+2*safe)*(4*R_poly_max+2*safe)*(Nz+2*R_poly_max+safe)/(4*PI*R_poly_min*R_poly_min*R_poly_min/3))
#define Data_folder "./"
// declare of subroutines
//Preprocessing
void init_LB(int me);   //D3Q19������ʼ��
void init_f(int me);    //�ֲ�������ʼ��
void init_p(int me);    //������Ϣ��ʼ��
void init_DEM(int me);  //DEM������ʼ��
void init_LP(int me);   //�������յ㵱�����꼰�ֲ���ʼ��
void init_LP_poly(int me);	//init polydisperse particle parameters
void init_process(int me);  //

void read_check_fluid(int me);     //��ʼ�������ٶȺ�ѹ�����ݻ��ߴ��ļ�����
void read_check_fluid2(int me);
void read_check_particle(int me);  //��ʼ������λ�ú��ٶȡ����ٶ����ݻ��ߴ��ļ�����
void read_check_particle_poly(int me);
void read_check_particle_contact(int me);

//fluid model
void Power_law(int me);   //Generalized Newtonian fluid: shear thinning, shear thicken
void Oldrod_B(int me);    //viscoelasticity fluid

//Solving
void Fluid_u_p(int me);                 //�����ٶȺ�ѹ������
void Fluid_data_exchange(int me);       //����˽����洦�����ݽ���
void Fluid_collision(int me);           //������ײ����������-�������������������������������ݻ���
void Fluid_collision1(int me);           //������ײ����������-�������������������������������ݻ���
void Particle_index(int me);            //������Ǵ�����ȷ����Щ��������Щ���ڣ��ر��ǿ����ڿ��Ľ����洦����Ҫ�ֲ�������������
void IBM_Particle(int me);              //�����߽���㣬DF-IBM
void IBM_Particle1(int me);              //�����߽���㣬DF-IBM
void IBM_Particle_less(int me);			//尝试修改其中forcef数组的更新方式
void IBM_Particle_less2(int me);			//尝试修改其中forcef数组的更新方式
void IBM_Particle_less_forcef_local(int me);
void IBM_Particle_less_forcef_local2(int me);
void IBM_Particle_less_forcef_local3(int me);
void IBM_Particle_less_forcef_local2_MDF(int me);
void IBM_Particle_less_forcef_local3_MDF(int me);
void IBM_Particle_poly(int me);			//calculate each particle using DF-IBM
void IBM_Particle_poly_less(int me);
void IBM_Particle_poly_less_forcef_local(int me);
void IBM_Particle_poly_less_forcef_local2(int me);
void IBM_Particle_poly_less_forcef_local3(int me);
void IBM_Particle_poly_less_forcef_local2_MDF(int me);
void IBM_Particle_poly_less_forcef_local3_MDF(int me);
void Particle_IB_comm(int me);
void Particle_IB_comm_poly(int me);
void Fluid_data_exchange_f(int me);          //����ƽ��̬�ֲ����ݽ���
void Fluid_data_exchange_f_less(int me);	//尝试精确发送需要使用的分布函数
void Fluid_stream(int me);                    //��������Ǩ��
void Fluid_stream_test(int me);
void Fluid_collision_swap(int me);

void Fluid_Boundary_rectangle(int me);              //���ν��������߽����������������ڣ���Χ����
void Particle_Boundary_rectangle(int me);            //���ν�������߽����������������ڣ���Χ����
void Fluid_Boundary_circle(int me);                  //Բ�ν��������߽����������������ڣ���Χ����
void Particle_Boundary_circle(int me);               //Բ�ν�������߽����������������ڣ���Χ����
void Fluid_Boundary(int me);

void lub_force_particle(int me);                     //��������
void lub_and_DEM_force_particle(int me);				//calculate lubrication and DEM collision force
void lub_and_DEM_force_particle_poly(int me);
void DEM_particle(int me);                           //����-������ײ������ģ��
void particle_evolvement(int me);                    //�����˶�
void particle_evolvement_poly(int me);
void Particle_index_update(int me);
void Particle_index_update_poly(int me);

//Postprocessing
void save_fx_fy(int me);      //��������������ϵ������
void save_fx_fy_poly(int me);
void Statistics_f(int me);    //��������ͳ�ƣ�ƽ���ٶȡ�ѹ��
void Statistics_p(int me);    //pressure
void log_data(int me);        //���ֿ���ͳ���������
void p_statistics(int me);    //particle
void FIB_particle(int me);
void static_fluid_velocity(int me);
void static_fluid_velocity_2(int me);
void bodyforce_in_particle(int me);

void data_deal_3D(int me);            //3D�����������������
void data_deal_3D_with_forcef(int me);
void data_deal_with_forcef_3D_local(int me);
void data_deal_3D1(int me);            //3D�����������������
void data_deal_2D(int me, int yid);   //2D������Ƭ�����ֿ����������
void data_deal_particle(int me);      //�������������
void data_deal_particle_contact(int me);
void data_deal_statistics(int me);    //ͳ���������
void save_particle_surface(int me);
void save_surface_pressure(int me);
void save_average_U(int me);
void save_fluid_boundary(int me);
void CPU_time(int me);
void CPU_time2(int me);
void CPU_time_gptl(int me);
void data_par_prepare_output(int me);
void data_par_contact_prepare_output(int me);
void relative_2p(int me);
void data_particle_pro(int me); //����������ݹ�����

void write_check_fluid(int me);       //�ϵ������������
void write_check_fluid2(int me);
void write_check_particle(int me);    //�ϵ�����������

void random_init_particle_position_generated_by_Monte_Carlo_method();	//initialize particle position when the particle fraction is larger than 0.2;
void random_init_poly_particle_position_generated_by_Monte_Carlo_method();

static void compute_particle_local_position(double* xc_key_position, const double* particle_pos, int me_x_local, int me_y_local, int Nx_local, int Ny_local);

//declare of functions
double delta_2p(double r);                         //DF-IB interpolation and spreading function
double delta_3p(double r);
void delta_3p_less(double r, double result[3]);
double delta_4p(double r);
double delta_4pcos(double r);
double fequation(double u[3], double rho, int i);          //euqilibrium pdf
static inline void change(double *val1, double *val2);

//declare of global variables
/*  begin global data      */
int     numproc;                      //�����������
int     step, step0, stepmax, step_DEM;         //���㲽����
double  t, t0;                        //����ʱ�����
int Left_me, Right_me, Low_me, Up_me, Lowleft_me, Lowright_me, Upleft_me, Upright_me;
double xx, xx1, xx2, xx3, xx4, xxn, xxn1, xxn2, xxn3, xxn4;
double yy, yy1, yy2, yy3, yy4, yyn, yyn1, yyn2, yyn3, yyn4;

//LB model parameters
//double  cs, p0, tao, ma;              //���١���ʼѹ��ѹ�����������ɳ�ϵ����������
double  w[Q];                         //����Ȩ��ϵ��
int     e[Q][3];                      //���ӷ���

//fluid variables
double  f[Nx4][Ny4][Nz4][Q];                       //pdf of LB, fp is f at previous time step
double  p[Nx4][Ny4][Nz4], u[Nx4][Ny4][Nz4][3];     //�����ܶȡ�ѹ�����ٶȷ���ռ�
double  fluidu_temp[Nx4][Ny4][Nz4][3];
double  forcef[Nx4][Ny4][Nz4][3];                  //����IB������ռ�
int     flag[Nx4][Ny4][Nz4];                       //����IB�����
double	fluid_ux_max, fluid_uy_max, fluid_uz_max, fluid_ma_max;

//particle variables                                  
double  xc[np][3], uc[np][3], ucc[np][3], wc[np][3], wcc[np][3];  //����λ�ã��ٶȣ����ٶ�                                  
double  F_IBM[np][3], T_IBM[np][3], F_lub[np_local][3], F_n_DEM[np_local][3], F_t_DEM[np_local][3], T_DEM[np_local][3];			//��������the last two store the Fbylub FbyDEM,Respectively
double  F_lub_poly[np_local_poly][3], F_n_DEM_poly[np_local_poly][3], F_t_DEM_poly[np_local_poly][3], T_DEM_poly[np_local_poly][3];
double  sitac[np][3];                                                  //����������ת��
double  tangential_overlap[np][max_contact_num][3];		//storage the index of the collision pair
int     tangential_contact_index[np][max_contact_num];
int     flag_p[np];
int     cross_p[np][3];
int     num_p[np_local];
int     num_p_poly[np_local_poly];
int		KeyProcess[np_local];   //2ά����
int		KeyProcess_poly[np_local_poly];
double ux_max, uy_max, uz_max, Ux_max, Uy_max, Uz_max;
double particle_ma_max;

//DEM variables
double  e_DEM, EY_DEM, G_DEM, R_DEM;
double  m_inertia, mppn_DEM, mppt_DEM, Ipp_DEM, mpwn_DEM, mpwt_DEM, Ipw_DEM;
//double 	F_DEM[np][3], T_DEM[np][3];
double  kn_DEM, kt_DEM, kr_DEM;

//Variables on Lagrangian points of sphere surface, for calculation of drag force
double xb[ns][nt1], yb[ns][nt1], zb[ns][nt1];     //�����߽��ֲ�����
double LP_area[ns];              //area of points at each band
double nt[ns];                   //number of points at each band
int num_points;                   //total number of points

double rhoc_poly[np];			//particle density
int    rc_poly[np];				//particle radius index of R_poly
double phi_p_poly, par_volume_poly;


//Statistics of fluid/particle variables
double  pave, Uave[3], aveU[3];             //fluid's u, p of local slice
double  averageP, averageU[3];     //fluid's u, p over all slices
double  avepU[3], avepW[3], Uavep[3], Wavep[3], Uave_stokes[3];     // particle's u,w 
double  ppdf[31][31], rdf[31], us[3], urms[3], cd[3];     // ppdf,RDF,Us,Cd������ͳ�Ʒ�������
double  IBforce[3], forceIB[3];

//double Ff_body[3];
double Error_U[3], Error_int[3];

int m_fluid;               //total fluid grids number
int     ifirst = 1;

double u_p_time, com1_time, pindex_time, IBM_time, bound_time, Fcolli_time, com2_time, stream_time, Pcolli_time, Pmove_time;  //ʱ��ͳ��
double com3_time, com4_time;
double collision_change_start_time, collision_change_time;
double end_dem_force_in_time;
double f_pack_start_time, f_pack_time, f_change_start_time, f_change_time, f_unpack_start_time, f_unpack_end_time, f_unpack_time;
#if if_gptl
	// All GPTL calls run on the main thread, outside OpenMP parallel regions.
	enum GptlTimer {
		GT_u_p, GT_com1, GT_IBM, GT_com2, GT_Fcolli, GT_com3,
		GT_stream, GT_Pcolli, GT_Pmove, GT_com4,
		GT_f_pack, GT_f_change, GT_f_unpack, GT_collision_change,
		GT_wall, GT_count
	};
	static const char *const gptl_timer_names[GT_count] = {
		"u_p", "com1", "IBM", "com2", "Fcolli", "com3_time",
		"stream", "Pcolli", "Pmove", "com4",
		"f_pack", "f_change", "f_unpack", "collision_change",
		"wall_clock"
	};
	static bool gptl_timer_used[GT_count] = {};
	static void gptl_check(int result, const char *operation, const char *name);
	static void gptl_start(GptlTimer timer);
	static void gptl_stop(GptlTimer timer);
	static double gptl_elapsed(GptlTimer timer);
#endif

int main(int argc, char *argv[])
{
	FILE *file;
	int me, i, j, k, n;
	int stepmax0;

	double startwtime, endwtime;
	double endwtime_u_p, startwtime_u_p, endwtime_com1, startwtime_com1;
	double endwtime_pindex, startwtime_pindex, endwtime_IBM, startwtime_IBM;
	double endwtime_Fcolli, startwtime_Fcolli, endwtime_com2, startwtime_com2;
	double endwtime_stream, startwtime_stream, endwtime_Pcolli, startwtime_Pcolli;
	double endwtime_Pmove, startwtime_Pmove;
	double endwtime_com3, startwtime_com3, endwtime_com4, startwtime_com4;

	/*DEM loop time static*/
	double dem_force_time = 0.0, dem_evolve_time = 0.0, dem_index_time = 0.0;
	double start_dem_force, end_dem_force, start_dem_evolve, end_dem_evolve, start_dem_index, end_dem_index;

	/** MPI  **************************/
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &numproc);
	MPI_Comm_rank(MPI_COMM_WORLD, &me);
#if if_gptl
	gptl_check(GPTLinitialize(), "GPTLinitialize", "all");
#endif
	srand((unsigned)time(NULL));

	stepmax0 = 0;
	stepmax =  1000;//3750;
	//printf("me=%d: Step parameters set (stepmax=%d)\n", me, stepmax);

	u_p_time = 0.0;
	com1_time = 0.0;       //�����ٶ�ͨѶ
	pindex_time = 0.0;
	IBM_time = 0.0;
	Fcolli_time = 0.0;
	com2_time = 0.0;       //�����ֲ�����ͨѶ
	bound_time = 0.0;
	stream_time = 0.0;
	Pcolli_time = 0.0;
	Pmove_time = 0.0;
	com3_time = 0.0;
	com4_time = 0.0;

	collision_change_time= 0.0;
	f_pack_time = 0.0;
    f_change_time = 0.0;
    f_unpack_time = 0.0;

	//initialize LB model parameters, fluid and particle variables
	init_LB(me);
	init_f(me);
#if ((periodic_x == 0) || (periodic_y == 0) ||(periodic_z == 0))
	Fluid_Boundary(me);
#endif
	init_p(me);
	if (if_polydisperse == 1)
	{
		init_LP_poly(me);
	}
	else
	{
		init_LP(me);
	}
	//init_DEM(me);    //������Ϣ��ʼ��
	init_process(me);

	//data_deal_3D(me);
	data_deal_particle(me);    // output initial particle distribution
	//data_deal_3D(me);
	//data_deal_particle_contact(me);
	//save_surface_pressure(me);

	save_particle_surface(me);
	//save_fluid_boundary(me);

	Particle_index(me);

	startwtime = MPI_Wtime();
#if if_gptl
	gptl_start(GT_wall);
#endif
	//printf("me=%d: Starting main loop (stepmax=%d)\n", me, stepmax);
#if if_stationary == 1
	if (me == 0)
	{
		if (if_polydisperse == 0)
			printf("Stationary simulation,traget Res = %f ,fluid velocity = %f\n", Res_target, (Res_target * mv / D / (1-fai_p)));
		else if (if_polydisperse == 1)
			printf("Stationary simulation,traget Res = %f ,fluid velocity = %f\n", Res_target, (Res_target * mv / (R_sauter_mean * 2 * dx_poly )/(1- phi_p_poly)));
	}
#endif

	for (t = 0, step = 1; step <= stepmax; t += dt, step++)
	{
#if (if_stationary == 1)
		if(me == 0)
            printf("Step %d, t = %f\n", step, t);
		
#endif
		if (me ==0 )
			if (t == 0)
			{
				if (if_polydisperse == 1)
					printf("E_Young = %f\tG_shear = %f\talpha_DEM=%f\n", E_Young, G_shear, alpha_DEM);
				else
                    printf("E_Young_poly = %f\tG_shear_poly = %f\talpha_DEM=%f\n", E_Young_poly, G_shear_poly, alpha_DEM);
				printf("K_n = %f\tK_t = %f\teta_n = %f\teta_t = %f\n", (K_n*1.0), K_t, eta_n, eta_t);
				printf("K_n2 = %f\tK_t2 = %f\teta_n2 = %f\teta_t2 = %f\n", K_n2, K_t2, eta_n2, eta_t2);
			}

		if (step < 10000)
		{
			k1_elas = 100000;
			k2_elas = 200000;
		}
		else
		{
			k1_elas = 200000;
			k2_elas = 1000000;
		}

		//solving
		startwtime_u_p = MPI_Wtime();
		#if if_gptl
			gptl_start(GT_u_p);
		#endif
		Fluid_u_p(me);          // calculate fluid variables: u, p, m, uave and pave
		endwtime_u_p = MPI_Wtime();
		#if if_gptl
			gptl_stop(GT_u_p);
			gptl_start(GT_com1);
		#endif
		Fluid_data_exchange(me);          // exchange fluid variables (f, u, p) at slice interface using MPI
		
		endwtime_com1 = MPI_Wtime();
		#if if_gptl
			gptl_stop(GT_com1);
		#endif
		
#if ((periodic_x == 0) || (periodic_y == 0) ||(periodic_z == 0))
		Fluid_Boundary(me);
#endif
		if (step % 100 == 0)
		{
			data_par_prepare_output(me);
			//data_par_contact_prepare_output(me);
			//data_particle_pro(me);

			//if (step % 1000 == 0)
			data_deal_particle(me);
			//data_deal_particle_contact(me);
		}

		if (step % 5000 == 0)
		{
			//data_deal_3D_with_forcef(me);
			data_deal_3D(me);
			//data_par_contact_prepare_output(me);
			//data_deal_particle_contact(me);
		}
		
		if (step < 1500000)
		{
			if (step % 100000 == 0)// data_deal_3D(me);
			{
				data_deal_3D(me);
				data_par_contact_prepare_output(me);
				data_deal_particle_contact(me);
			}
		}
		else
		{
			if (step % 20000 == 0)// data_deal_3D(me);
			{
				data_deal_3D(me);
				data_par_contact_prepare_output(me);
				data_deal_particle_contact(me);
			}
		}
		
		
		#if if_gptl
			gptl_start(GT_IBM);
		#endif

		//Fluid_Boundary_rectangle(me);         // fluid boundary treatment -- neglected for full perodic boundary condition		
		//Fluid_Boundary_circle(me);
		startwtime_IBM = MPI_Wtime();
		if (if_polydisperse == 1)
		{
			IBM_Particle_poly_less_forcef_local3_MDF(me);
		}
		else
		{
			IBM_Particle_less_forcef_local2_MDF(me);     // Calculate P, Fi, Ti on each particle acoording to Lagrangian points value uf, up, force etc.	
		}
		endwtime_IBM = MPI_Wtime();
		#if if_gptl
			gptl_stop(GT_IBM);
			gptl_start(GT_com2);
		#endif
		if (if_polydisperse == 1)
		{
			Particle_IB_comm_poly(me);
		}
		else
		{
			Particle_IB_comm(me);
		}
		endwtime_com2 = MPI_Wtime();
		#if if_gptl
			gptl_stop(GT_com2);
			gptl_start(GT_Fcolli);
		#endif

        //bodyforce_in_particle(me);
		Fluid_collision(me);
		endwtime_Fcolli = MPI_Wtime();
		#if if_gptl
			gptl_stop(GT_Fcolli);
			gptl_start(GT_com3);
		#endif
		Fluid_data_exchange_f_less(me);
		endwtime_com3 = MPI_Wtime();
		#if if_gptl
			gptl_stop(GT_com3);
			gptl_start(GT_stream);
		#endif
		Fluid_stream(me);
		//Fluid_stream_test(me);
		endwtime_stream = MPI_Wtime();
		#if if_gptl
			gptl_stop(GT_stream);
		#endif

		dem_force_time = 0.;
		dem_evolve_time = 0.;
        dem_index_time = 0.;

#if (if_stationary==0)
		for (step_DEM = 1; step_DEM <= (int)(dt / dt_DEM); step_DEM++)
		{
			if (me == 0)
				printf("t=%f,ndt=%d,step_DEM=%d\n", t + step_DEM * dt_DEM, step, step_DEM);
			if (if_polydisperse == 1)
			{
				start_dem_force = MPI_Wtime();
				#if if_gptl
					gptl_start(GT_Pcolli);
				#endif
				lub_and_DEM_force_particle_poly(me);
                end_dem_force = MPI_Wtime();
				dem_force_time += end_dem_force_in_time - start_dem_force;
				#if if_gptl
					//gptl_stop(GT_Pcolli);
					gptl_start(GT_Pmove);
				#endif
                start_dem_evolve = MPI_Wtime();
				particle_evolvement_poly(me);           // particle movement and update
                end_dem_evolve = MPI_Wtime();
                dem_evolve_time += end_dem_evolve - start_dem_evolve;
				#if if_gptl
					gptl_stop(GT_Pmove);
					gptl_start(GT_com4);
				#endif
                start_dem_index = MPI_Wtime();
				Particle_index_update_poly(me);
                end_dem_index = MPI_Wtime();
                dem_index_time += end_dem_index - start_dem_index;
				#if if_gptl
					gptl_stop(GT_com4);
				#endif
			}
			else
			{
                start_dem_force = MPI_Wtime();
				#if if_gptl
					gptl_start(GT_Pcolli);
				#endif
				//lub_force_particle(me);
				lub_and_DEM_force_particle(me);
                end_dem_force = MPI_Wtime();
                dem_force_time += end_dem_force_in_time - start_dem_force;
				#if if_gptl
					//gptl_stop(GT_Pcolli);
					gptl_start(GT_Pmove);
				#endif
				//endwtime_Pcolli = MPI_Wtime();
				start_dem_evolve = MPI_Wtime();
				particle_evolvement(me);           // particle movement and update
                end_dem_evolve = MPI_Wtime();
                dem_evolve_time += end_dem_evolve - start_dem_evolve;
				#if if_gptl
					gptl_stop(GT_Pmove);
					gptl_start(GT_com4);
				#endif
				//endwtime_Pmove = MPI_Wtime();
                start_dem_index = MPI_Wtime();
				Particle_index_update(me);
                end_dem_index = MPI_Wtime();
                dem_index_time += end_dem_index - start_dem_index;
				#if if_gptl
					gptl_stop(GT_com4);
				#endif
				//endwtime_com4 = MPI_Wtime();
			}
		}
#endif
		//endwtime_Pcolli = MPI_Wtime();  // DEM力计算结束时间
		//endwtime_Pmove = endwtime_Pcolli + dem_evolve_time;  // 粒子演化结束时间
		//endwtime_com4 = endwtime_Pmove + dem_index_time;     // 索引更新结束时间
#if (if_stationary == 1)
		if (if_polydisperse == 1)
		{
			Particle_index_update_poly(me);
		}
		else
		{
			Particle_index_update(me);
		}
#endif
		//if (step % 1000 == 0)
		{
			//data_par_prepare_output(me);
			//data_deal_particle(me);
		}
		//if (step % 500 == 0) save_surface_pressure(me); 
		//if (step % 100000 == 0) save_particle_surface(me);
		//static_fluid_velocity(me);
		static_fluid_velocity_2(me);
		if (if_polydisperse == 1)
		{
			save_fx_fy_poly(me);
		}
		else
		{
			save_fx_fy(me);
		}
		
		//postprocessing
		//if (step % 50 == 0)				log_data(me);			// output result to files
		//if (step % 50 == 0 && t*St >= 40)	p_statistics(me);		// particle statistics after stable  
		/*
		if (step % 1000 == 0)	// ouput field data of fluid in tecplot format -- parallel and sequential version 
		{
			MPI_Barrier(MPI_COMM_WORLD);
			data_deal_3D(me);     // 3D fluid & particle field (large)
			data_deal_2D(me,50);  // 2D slice of fluid & particle field, at NY=90
			data_deal_particle(me);
			
			if(t*St>=40)
			{
			data_deal_statistics(me);  // particle statistics
			ifirst = 0;		// switch on under relaxation
			}
	
		}*/	

		// Checkpoint
		/*
		if (step % 1000 == 0 && CHECKPOINT)
		{
			write_check_fluid(me);       // fluid field 
			write_check_particle(me);    // particle field
		}
		*/

		u_p_time += endwtime_u_p - startwtime_u_p;
		com1_time += endwtime_com1 - endwtime_u_p;
		IBM_time += endwtime_IBM - startwtime_IBM;
		com2_time += endwtime_com2 - endwtime_IBM;
		Fcolli_time += endwtime_Fcolli - endwtime_com2;
		com3_time += endwtime_com3 - endwtime_Fcolli;
		stream_time += endwtime_stream - endwtime_com3;
		//Pcolli_time += endwtime_Pcolli - endwtime_stream;
		//Pmove_time += endwtime_Pmove - endwtime_Pcolli;
		//com4_time += endwtime_com4 - endwtime_Pmove;
		Pcolli_time += dem_force_time;
        Pmove_time += dem_evolve_time;
        com4_time += dem_index_time;

		collision_change_time += endwtime_Fcolli - collision_change_start_time;

		if (step % 100 == 0)
		{
			//CPU_time(me);
			CPU_time2(me);
			#if if_gptl
				CPU_time_gptl(me);
			#endif
		}
	}
	endwtime = MPI_Wtime();	
	#if if_gptl
		gptl_stop(GT_wall);
		//gptl_finalize();
	#endif
	
	printf("me=%d,wall clock time=%f\n", me, endwtime - startwtime);
	//CPU_time(me);
	CPU_time2(me);
	# if if_gptl
		printf("me=%d,wall clock time=%f\n", me, gptl_elapsed(GT_wall));
		CPU_time_gptl(me);
	#endif
#if iflub_DEM_output
	if (shared_contact_file) {
		fclose(shared_contact_file);
		shared_contact_file = NULL;
	}
#endif
	//data_deal_3D(me);
	//save_particle_surface(me);
	//data_deal_particle(me);

	/* End MPI*******************/
	MPI_Barrier(MPI_COMM_WORLD);
	gptl_check(GPTLfinalize(), "GPTLfinalize", "all");
	MPI_Finalize();
	/* End MPI*******************/

	return 0;
}

//--------------------------------------------------------------------------------------------------------
//Subroutine
//--------------------------------------------------------------------------------------------------------
//initialize LB model parameters 
void init_LB(int me)
{	
	w[0] = 1.0 / 3;
	w[1] = 1.0 / 18;  w[2] = 1.0 / 18;  w[3] = 1.0 / 18;  w[4] = 1.0 / 36;  w[5] = 1.0 / 36;  w[6] = 1.0 / 36;
	w[7] = 1.0 / 36;  w[8] = 1.0 / 36;  w[9] = 1.0 / 36;  w[10] = 1.0 / 18; w[11] = 1.0 / 18; w[12] = 1.0 / 18;
	w[13] = 1.0 / 36; w[14] = 1.0 / 36; w[15] = 1.0 / 36; w[16] = 1.0 / 36; w[17] = 1.0 / 36; w[18] = 1.0 / 36;

	e[0][0] = 0;   e[0][1] = 0;   e[0][2] = 0;
	e[1][0] = -1;   e[1][1] = 0;   e[1][2] = 0;
	e[2][0] = 0;  e[2][1] = -1;   e[2][2] = 0;
	e[3][0] = 0;   e[3][1] = 0;   e[3][2] = -1;
	e[4][0] = -1;   e[4][1] = -1;  e[4][2] = 0;
	e[5][0] = -1;   e[5][1] = 1;   e[5][2] = 0;
	e[6][0] = -1;   e[6][1] = 0;   e[6][2] = -1;
	e[7][0] = -1;   e[7][1] = 0;   e[7][2] = 1;
	e[8][0] = 0;  e[8][1] = -1;  e[8][2] = -1;
	e[9][0] = 0;   e[9][1] = -1;  e[9][2] = 1;
	e[10][0] = 1; e[10][1] = 0;  e[10][2] = 0;
	e[11][0] = 0;  e[11][1] = 1;  e[11][2] = 0;
	e[12][0] = 0; e[12][1] = 0;  e[12][2] = 1;
	e[13][0] = 1;  e[13][1] = 1;  e[13][2] = 0;
	e[14][0] = 1; e[14][1] = -1;  e[14][2] = 0;
	e[15][0] = 1;  e[15][1] = 0;  e[15][2] = 1;
	e[16][0] = 1;  e[16][1] = 0; e[16][2] = -1;
	e[17][0] = 0;  e[17][1] = 1;  e[17][2] = 1;
	e[18][0] = 0;  e[18][1] = 1; e[18][2] = -1;
}

//--------------------------------------------------------------------------------------------------------
//initialize fluid variables
void init_f(int me)
{
	int x, y, z, i;

	read_check_fluid2(me);

	for (x = 0; x <= Nx3; x++)
	for (y = 0; y <= Ny3; y++)
	for (z = 0; z <= Nz3; z++)
	{
		for (i = 0; i < 3; i++)
			forcef[x][y][z][i] = 0.0;

		flag[x][y][z] = 0;

		for (i = 0; i < Q; i++)
		{
			f[x][y][z][i] = fequation(u[x][y][z], p[x][y][z], i);   //����fequation��
		}
	}
}

//-------------------------------------------------------------------------
//particles position, velocity, acceleration
void  init_p(int me)
{
	int i, ii;
	if (if_polydisperse == 1)
	{
		read_check_particle_poly(me);
	}
	else
	{
		read_check_particle(me);
	}
	read_check_particle_contact(me);

	// particle dependent variables: uc, ac, wac
	//memset(uc, 0., sizeof(uc));
	memset(ucc, 0., sizeof(ucc));
	memset(sitac, 0., sizeof(sitac));
	//memset(wc, 0., sizeof(wc));
	memset(wcc, 0., sizeof(wcc));
	memset(F_IBM, 0., sizeof(F_IBM));
	memset(T_IBM, 0., sizeof(T_IBM));
	memset(F_lub, 0., sizeof(F_lub));
	memset(F_n_DEM, 0., sizeof(F_n_DEM));
	memset(F_t_DEM, 0., sizeof(F_t_DEM));
	memset(T_DEM, 0., sizeof(T_DEM));
	//memset(tangential_overlap, 0., sizeof(tangential_overlap));
	//memset(tangential_contact_index, 0, sizeof(tangential_contact_index));
	memset(cross_p, 0, sizeof(cross_p));
	for (i = 0; i < 3; i++)
	{
		Uave_stokes[i] = 0.0;
		Error_U[i] = 0.0;
		Error_int[i] = 0.0;
	}
	
}
		

void init_DEM(int me)    //������Ϣ��ʼ��
{
	m_inertia = 2.0 / 5.0*(1.0 / 6.0*PI*D*D*D*rhop)*(R*dx)*(R*dx);       //inertia

	e_DEM = log(1 / e_p) / sqrt(PI*PI + log(1 / e_p)*log(1 / e_p));  //��Ч�ָ�ϵ��
	EY_DEM = 0.5 * E_Young / (1 - v_Poisson*v_Poisson);            //��Ч����ģ��
	G_DEM = 0.5 * G_Shear/ (1 - v_Poisson*v_Poisson);  //��Ч����ģ��
	R_DEM = 0.5 * R*dx;                               //��Ч���Ӱ뾶
	mppn_DEM = 0.5 / 6.0*PI*D*D*D*rhop;              //�����䷨����ײ��Ч����
	mppt_DEM = (2/7)*0.5 / 6.0*PI*D*D*D*rhop;        //������������ײ��Ч����
	Ipp_DEM = 0.5*m_inertia;                          //��������ײ��Чת������
	mpwn_DEM = 1.0 / 6.0*PI*D*D*D*rhop;              //�������淨����ײ��Ч����
	mpwt_DEM = (2 / 7)*1.0 / 6.0*PI*D*D*D*rhop;      //�������淨����ײ��Ч����
	Ipw_DEM = m_inertia;                              //����������ײ��Чת������

	kn_DEM = 1.5E5;   //����նȣ�����ϵ����, N/m
	kt_DEM = 1.0E5;   //����նȣ�����ϵ����, N/m
	kr_DEM = kt_DEM*R_DEM*R_DEM;         //�����ն�, Nm/rad
}

//---------------------------------------------------------------------------------
// initialize the variables related to Lagrangian points of particle
// such as xb,yb,zb,n,LP_area_ratio etc., which is frequently called by IB 
void init_LP(int me)
{	
	int x, y, z, i, j;
	double sita, alpha;
		
	num_points = 0;
	for (i = 0; i<ns; i++)
	{
		if (i == 0)    //��������ϵ�
		{
			nt[i] = 1;
			j = 0;
			xb[i][j] = 0;
			yb[i][j] = 0;
			zb[i][j] = rb;
			LP_area[i] = 2 * PI*rb*rb*(1 - cos(PI / (2 * (ns - 1))));   //�õ�����			
		}

		else if (i == (ns - 1))
		{
			nt[i] = 1;
			j = 0;

			xb[i][j] = 0;
			yb[i][j] = 0;
			zb[i][j] = -rb;
			LP_area[i] = 2 * PI*rb*rb*(1 - cos(PI / (2 * (ns - 1))));   //�õ�����			
		}
		else 
		{
			sita = i*PI / (ns - 1);
			nt[i] = (int)(2 * PI*rb*sin(sita)/0.8);
			LP_area[i] = 2 * PI*rb*rb*(cos((i - 0.5)*PI / (ns - 1)) - cos((i + 0.5)*PI / (ns - 1))) / nt[i];
			alpha = 0;
			for (j = 0; j < nt[i]; j++)
			{
				alpha = alpha + 2 * PI / nt[i];
				xb[i][j] = rb*sin(sita)*cos(alpha);
				yb[i][j] = rb*sin(sita)*sin(alpha);
				zb[i][j] = rb*cos(sita);				
			}
		}		
		num_points += nt[i];
	}
	//if (me == 0)

	//{
	//	for (i = 0; i < ns; i++)
	//	{
	//		for (j = 0; j < nt[i]; j++)
	//		{
	//			printf("%f\t%f\t%f\n", xb[i][j], yb[i][j], zb[i][j]);
	//		}
    //        printf("%f\n",LP_area[i]);
	//	}
	//}
}

//--------------------------------------------------------------------------------------
// initialize the variables related to Lagrangian points of particle	(poly version by TanzhaoFe)
// such as xb,yb,zb,n,LP_area_ratio etc., which is frequently called by IB 
void init_LP_poly(int me)
{
	int x, y, z, i, j, k;
	double sita, alpha;
	int  hightemp, temp;
	int ns_temp, nt_temp, points_sum;
	char par_surface_file[200];
	static FILE* surface_file = NULL;

	if (me == 0)
	{
		sprintf(par_surface_file, "rank_%d_particle_surface.dat", me);
		if (surface_file == NULL)
		{
			surface_file = fopen(par_surface_file, "w");
			if (surface_file == NULL)
			{
				fprintf(stderr, "Rank %d Error: cannot open file %s\n", me, par_surface_file);
			}
		}
	}
	//R_poly_num = sizeof(R_poly) / sizeof(R_poly[0]);

	//for (i = 0; i < R_poly_num; i++)
	//{
	//	hightemp = R_poly[i];
	//	for (j = i + 1; j < R_poly_num; j++)
	//	{
	//		temp = R_poly[j];
	//		if (temp > hightemp)
	//		{
	//			R_poly[j] = hightemp;
	//			R_poly[i] = temp;
	//			hightemp = temp;
	//		}
	//	}
	//}
	//R_poly_max = R_poly[0];
    //R_poly_min = R_poly[R_poly_num - 1];

	//rb_poly = (double *)malloc(R_poly_num * sizeof(double));
	//if (rb_poly == NULL) 
	//{
	//	printf("Memory allocation failed for rb_poly.\n");
	//}

	for (i = 0; i < R_poly_num; i++)
	{
		rb_poly[i] = pow(((R_poly[i] * R_poly[i] * R_poly[i] * 1.0 + (R_poly[i]-1.0) * (R_poly[i] - 1.0) * (R_poly[i] - 1.0)) / 2.0),(1./3.));
	}
	//ns_max = (int)(PI * rb_poly[0] / 0.8 + 1);
	//nt1_max = (int)(2 * PI * rb_poly[0] / 0.8 + 1);
	//num_points_poly = (int *)malloc(R_poly_num * sizeof(int));
	//if (num_points_poly == NULL) {
	//	printf("Memory allocation failed for num_points_poly.\n");
	//	exit(1);
	//}
	//nt_poly = (double **)malloc(R_poly_num * sizeof(double *));
	//if (nt_poly == NULL) {
	//	printf("Memory allocation failed for nt_poly.\n");
	//	exit(1);
	//}
	//LP_area_poly = (double **)malloc(R_poly_num * sizeof(double *));
	//if (LP_area_poly == NULL) {
	//	printf("Memory allocation failed for LP_area_poly.\n");
	//	exit(1);
	//}
	//xb_poly = (double ***)malloc(R_poly_num * sizeof(double **));
	//yb_poly = (double ***)malloc(R_poly_num * sizeof(double **));
	//zb_poly = (double ***)malloc(R_poly_num * sizeof(double **));
	//if (xb_poly == NULL || yb_poly == NULL || zb_poly == NULL) {
	//	printf("Memory allocation failed for xb_poly, yb_poly, or zb_poly.\n");
	//	exit(1);
	//}
	//for (i = 0; i < R_poly_num; i++)
	//{
	//	nt_poly[i] = (double *)malloc(ns_max * sizeof(double));
	//	if (nt_poly[i] == NULL) {
	//		printf("Memory allocation failed for nt_poly[%d].\n", i);
	//		exit(1);
	//	}
	//	LP_area_poly[i] = (double *)malloc(ns_max * sizeof(double));
	//	if (LP_area_poly[i] == NULL) {
	//		printf("Memory allocation failed for LP_area_poly[%d].\n", i);
	//		exit(1);
	//	}
    //    xb_poly[i] = (double **)malloc(ns_max * sizeof(double *));
    //    yb_poly[i] = (double **)malloc(ns_max * sizeof(double *));
    //    zb_poly[i] = (double **)malloc(ns_max * sizeof(double *));
	//	if (xb_poly[i] == NULL || yb_poly[i] == NULL || zb_poly[i] == NULL) {
	//		printf("Memory allocation failed for xb_poly[%d], yb_poly[%d], or zb_poly[%d].\n", i, i, i);
	//		exit(1);
	//	}
    //    for (j = 0; j < ns_max; j++)
    //    {
    //        xb_poly[i][j] = (double *)malloc(nt1_max * sizeof(double));
    //        yb_poly[i][j] = (double *)malloc(nt1_max * sizeof(double));
    //        zb_poly[i][j] = (double *)malloc(nt1_max * sizeof(double));
	//		if (xb_poly[i][j] == NULL || yb_poly[i][j] == NULL || zb_poly[i][j] == NULL) {
	//			printf("Memory allocation failed for xb_poly[%d][%d], yb_poly[%d][%d], or zb_poly[%d][%d].\n", i, j, i, j, i, j);
	//			exit(1);
	//		}
    //    }
	//}

	points_sum = 0;
	//calculate lagrangi discrete points on the particle surface at each radius
	for (i = 0; i < R_poly_num; i++) 
	{ 
		num_points_poly[i] = 0;
		ns_temp = (int)(PI * rb_poly[i] / 0.8 + 1);
		nt_temp = (int)(2 * PI * rb_poly[i] / 0.8 + 1);
		for (j = 0; j < ns_temp; j++)
		{
			if (j == 0)
			{
				nt_poly[i][j] = 1;
				k = 0;
				xb_poly[i][j][k] = 0;
				yb_poly[i][j][k] = 0;
				zb_poly[i][j][k] = rb_poly[i];
				LP_area_poly[i][j] = 2 * PI * rb_poly[i] * rb_poly[i] * (1 - cos(PI / (2 * (ns_temp - 1))));
			}
			else if (j == ns_temp - 1)
			{
				nt_poly[i][j] = 1;
				k = 0;
				xb_poly[i][j][k] = 0;
				yb_poly[i][j][k] = 0;
				zb_poly[i][j][k] = -rb_poly[i];
				LP_area_poly[i][j] = 2 * PI * rb_poly[i] * rb_poly[i] * (1 - cos(PI / (2 * (ns_temp - 1))));
			}
			else
			{
				sita = j * PI / (ns_temp - 1);
				nt_poly[i][j] = (int)(2 * PI * rb_poly[i] * sin(sita) / 0.8);
				LP_area_poly[i][j] = 2 * PI * rb_poly[i] * rb_poly[i] * (cos((j - 0.5) * PI / (ns_temp - 1)) - cos((j + 0.5) * PI / (ns_temp - 1))) / nt_poly[i][j];
				alpha = 0;
				for (int k = 0; k < nt_poly[i][j]; k++)
				{
					alpha = alpha + 2 * PI / nt_poly[i][j];
					xb_poly[i][j][k] = rb_poly[i] * sin(sita) * cos(alpha);
					yb_poly[i][j][k] = rb_poly[i] * sin(sita) * sin(alpha);
					zb_poly[i][j][k] = rb_poly[i] * cos(sita);
				}
			}
			num_points_poly[i] += nt_poly[i][j];
			points_sum += nt_poly[i][j];
		}
	}

	if (me == 0)
	{
		fprintf(surface_file, "TITLE = poly_surface_p\n");
		fprintf(surface_file, "VARIABLES = x,y,z\n");
		fprintf(surface_file, "ZONE I=%d,J=3,F=POINT\n", points_sum);
		for (i = 0; i < R_poly_num; i++)
		{
			ns_temp = (int)(PI * rb_poly[i] / 0.8 + 1);
			for (j = 0; j < ns_temp; j++)
			{
				for (k = 0; k < nt_poly[i][j]; k++)
				{
					fprintf(surface_file, "%f\t%f\t%f\n", (xb_poly[i][j][k] + 2 * i * R_poly_max), yb_poly[i][j][k], zb_poly[i][j][k]);
				}
				//fprintf(surface_file, "%f\n", LP_area_poly[i][j]);
			}
		}
		fclose(surface_file);
	}
	if (me == 0)
	{
		printf("R_max=%d, R_min=%d, rb_max=%f\n", R_poly_max, R_poly_min, rb_poly_max);
		for (i = 0; i < R_poly_num; i++)
		{
			printf("R_poly[%d]=%d\t rb_poly[%d]=%f\n", i+1, R_poly[i], i+1, rb_poly[i]);
		}
	}

}

void init_process(int me)
{
	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	me_y = int(me / NPROCx);
	me_x = me - me_y*NPROCx;

	xx = me_x * Nx;             xxn = (me_x + 1) * Nx;    //�ý���x����߽�
	yy = me_y * Ny;             yyn = (me_y + 1) * Ny;    //�ý���y����߽�
	//xx1 = me_x * Nx + 2 * dx_p; xxn1 = (me_x + 1) * Nx - 2 * dx_p;  //�ý�����ײ��x�����ڱ߽�
	//yy1 = me_y * Ny + 2 * dy_p; yyn1 = (me_y + 1) * Ny - 2 * dy_p;  //�ý�����ײ��y�����ڱ߽�
	//xx2 = me_x * Nx - 2 * dx_p; xxn2 = (me_x + 1) * Nx + 2 * dx_p;  //�ý�����ײ��x������߽�
	//yy2 = me_y * Ny - 2 * dy_p; yyn2 = (me_y + 1) * Ny + 2 * dy_p;  //�ý�����ײ��y������߽�

	xx1 = me_x * Nx + 2 * R + safe; xxn1 = (me_x + 1) * Nx - 2 * R - safe;  //�ý�����ײ��x�����ڱ߽�
	yy1 = me_y * Ny + 2 * R + safe; yyn1 = (me_y + 1) * Ny - 2 * R - safe;  //�ý�����ײ��y�����ڱ߽�
	xx2 = me_x * Nx - 2 * R - safe; xxn2 = (me_x + 1) * Nx + 2 * R + safe;  //�ý�����ײ��x������߽�
	yy2 = me_y * Ny - 2 * R - safe; yyn2 = (me_y + 1) * Ny + 2 * R + safe;  //�ý�����ײ��y������߽�

	if (if_polydisperse == 1)
	{
		xx1 = me_x * Nx + 2 * R_poly_max + safe; xxn1 = (me_x + 1) * Nx - 2 * R_poly_max - safe;  //
		yy1 = me_y * Ny + 2 * R_poly_max + safe; yyn1 = (me_y + 1) * Ny - 2 * R_poly_max - safe;  //
		xx2 = me_x * Nx - 2 * R_poly_max - safe; xxn2 = (me_x + 1) * Nx + 2 * R_poly_max + safe;  //
		yy2 = me_y * Ny - 2 * R_poly_max - safe; yyn2 = (me_y + 1) * Ny + 2 * R_poly_max + safe;  //
	}

	xx3 = me_x * Nx + drr;      xxn3 = (me_x + 1) * Nx - drr;       //�ý���IB������x�����ڱ߽�
	yy3 = me_y * Ny + drr;      yyn3 = (me_y + 1) * Ny - drr;       //�ý���IB������y�����ڱ߽�
	xx4 = me_x * Nx - drr;      xxn4 = (me_x + 1) * Nx + drr;       //�ý���IB������x������߽�
	yy4 = me_y * Ny - drr;      yyn4 = (me_y + 1) * Ny + drr;       //�ý���IB������y������߽�

	/*
	----------------------------------------------------------
	| UpleftProcess  | UpProcess   | UprightProcess  |
	----------------------------------------------------------
	| LeftProcess    | KeyProcess  | RightProcess    |
	----------------------------------------------------------
	| LowleftProcess | LowProcess  | LowrightProcess |
	----------------------------------------------------------
	*/
	//case1: particle is in the center zone of the entire domain
	if ((me_x > 0) && (me_x < (NPROCx - 1)) && (me_y  > 0) && (me_y < (NPROCy - 1)))
	{
		Left_me = me - 1;
		Right_me = me + 1;
		Low_me = me - NPROCx;
		Up_me = me + NPROCx;
		Lowleft_me = Low_me - 1;
		Lowright_me = Low_me + 1;
		Upleft_me = Up_me - 1;
		Upright_me = Up_me + 1;
	}

	//case2: particle is in the center zone of left line, left are MPI_PROC_NULL
	else if ((me_x == 0) && (me_y >= 0) && (me_y <= (NPROCy - 1)))
	{
		Left_me = me + (NPROCx - 1);
		Right_me = me + 1;
		Low_me = me - NPROCx;
		Up_me = me + NPROCx;
		Lowleft_me = Low_me + (NPROCx - 1);
		Lowright_me = Low_me + 1;
		Upleft_me = Up_me + (NPROCx - 1);
		Upright_me = Up_me + 1;

		if (me_y == 0)
		{
			Low_me = NPROCx*(NPROCy - 1);
			Lowleft_me = NPROCx*NPROCy - 1;
			Lowright_me = Low_me + 1;
		}
		if (me_y == (NPROCy - 1))
		{
			Up_me = 0;
			Upleft_me = (NPROCx - 1);
			Upright_me = 1;
		}
	}

	//case3: particle is in the center zone of right line, right are MPI_PROC_NULL
	else if ((me_x == (NPROCx - 1)) && (me_y >= 0) && (me_y <= (NPROCy - 1)))
	{
		Left_me = me - 1;
		Right_me = me - (NPROCx - 1);
		Low_me = me - NPROCx;
		Up_me = me + NPROCx;
		Lowleft_me = Low_me - 1;
		Lowright_me = Low_me - (NPROCx - 1);
		Upleft_me = Up_me - 1;
		Upright_me = Up_me - (NPROCx - 1);

		if (me_y == 0)
		{
			Low_me = NPROCx*NPROCy - 1;
			Lowleft_me = Low_me - 1;
			Lowright_me = Low_me - (NPROCx - 1);
		}
		if (me_y == (NPROCy - 1))
		{
			Up_me = (NPROCx - 1);
			Upleft_me = Up_me - 1;
			Upright_me = 0;
		}
	}

	//case4: particle is in the center zone of bottom line, low are MPI_PROC_NULL
	else if ((me_x >= 0) && (me_x <= (NPROCx - 1)) && (me_y == 0))
	{
		Left_me = me - 1;
		Right_me = me + 1;
		Low_me = me + NPROCx*(NPROCy - 1);
		Up_me = me + NPROCx;
		Lowleft_me = Low_me - 1;
		Lowright_me = Low_me + 1;
		Upleft_me = Up_me - 1;
		Upright_me = Up_me + 1;

		if (me_x == 0)
		{
			Left_me = (NPROCx - 1);
			Lowleft_me = NPROCx*NPROCy - 1;
			Upleft_me = Up_me + (NPROCx - 1);
		}
		if (me_x == (NPROCx - 1))
		{
			Right_me = 0;
			Lowright_me = Low_me - (NPROCx - 1);
			Upright_me = Up_me - (NPROCx - 1);
		}
	}

	//case5: particle is in the center zone of top line, up are MPI_PROC_NULL
	else if ((me_x >= 0) && (me_x <= (NPROCx - 1)) && (me_y == (NPROCy - 1)))
	{
		Left_me = me - 1;
		Right_me = me + 1;
		Low_me = me - NPROCx;
		Up_me = me - NPROCx*(NPROCy - 1);
		Lowleft_me = Low_me - 1;
		Lowright_me = Low_me + 1;
		Upleft_me = Up_me - 1;
		Upright_me = Up_me + 1;

		if (me_x == 0)
		{
			Left_me = NPROCx*NPROCy - 1;
			Lowleft_me = Low_me + (NPROCx - 1);
			Upleft_me = (NPROCx - 1);
		}
		if (me_x == (NPROCx - 1))
		{
			Right_me = me - (NPROCx - 1);
			Lowright_me = Low_me - (NPROCx - 1);
			Upright_me = 0;
		}
	}

	if (periodic_x == 0)
	{
		if (me_x == 0)
		{
			Upleft_me = MPI_PROC_NULL;
			Left_me = MPI_PROC_NULL;
			Lowleft_me = MPI_PROC_NULL;
		}
		if (me_x == (NPROCx - 1))
		{
			Upright_me = MPI_PROC_NULL;
			Right_me = MPI_PROC_NULL;
			Lowright_me = MPI_PROC_NULL;
		}
	}
	if (periodic_y == 0)
	{
		if (me_y == 0)
		{
			Lowleft_me = MPI_PROC_NULL;
			Low_me = MPI_PROC_NULL;
			Lowright_me = MPI_PROC_NULL;
		}
		if (me_y == (NPROCy - 1))
		{
			Upleft_me = MPI_PROC_NULL;
			Up_me = MPI_PROC_NULL;
			Upright_me = MPI_PROC_NULL;
		}
	}
	// --- 输出 MPI 进程邻居关系配置 ---
	if (0)
	{
		char fname[64];
		sprintf(fname, "neighbor_config_rank_%04d.txt", me);
		FILE* fp = fopen(fname, "w");
		if (fp != NULL) {
			// 写入周期性边界条件（假设存在 periodic_z，若未定义则输出 0）
			fprintf(fp, "# Periodic Boundary Conditions\n");
			fprintf(fp, "periodic_x = %d\t", periodic_x);
			fprintf(fp, "periodic_y = %d\t", periodic_y);
			fprintf(fp, "periodic_z = %d\n", periodic_z);

			fprintf(fp, "\n# Current rank and its 8 neighbors\n");
			fprintf(fp, "  Up-Left    = %d\t", Upleft_me);
			fprintf(fp, "  Up         = %d\t", Up_me);
			fprintf(fp, "  Up-Right   = %d\n", Upright_me);
			fprintf(fp, "  Left       = %d\t", Left_me);
			fprintf(fp, "  me         = %d\t", me);
			fprintf(fp, "  Right      = %d\n", Right_me);
			fprintf(fp, "  Low-Left   = %d\t", Lowleft_me);
			fprintf(fp, "  Low        = %d\t", Low_me);
			fprintf(fp, "  Low-Right  = %d\n", Lowright_me);
			fclose(fp);
		}
	}

}

//----------------------------------------------------------------------------------------------
// update particle coordinate id, slice id and neighbore fluid id
void Particle_index(int me)
{
	int x, y, z, i, j, k;
	int me_x, me_y;          //�ý��̶�Ӧ��x�����y������
	int x_p0, x_p1, x_p2, y_p0, y_p1, y_p2, z_p0, z_p1, z_p2;

	me_y = int(me / NPROCx);
	me_x = me - me_y*NPROCx;

	if (if_polydisperse == 1)
	{
        for (i = 0; i < np_local_poly; i++)
		{
			num_p_poly[i] = -1;
			KeyProcess_poly[i] = -1;
		}
	}
	else
	{
		for (i = 0; i < np_local; i++)    //�Ծֲ�������ų�ʼ��,û�п�����Ϊ-1
		{
			num_p[i] = -1;
			KeyProcess[i] = -1;
		}
	}

	//��ʼ�������ֲ����ӳ�䵽���������ϣ�����ȫ�ֱ����ֲ���Ŷ�Ӧ
	k = 0;   //kΪ�ֲ���ţ����п���ӳ�䵽�ֲ�����������������k�������Ӧ��ȫ�ֱ�Ŷ�Ӧ
	for (j = 0; j < np; j++)
	{		
		if (if_polydisperse == 1)
		{
			if (periodic_x == 1)
			{
				if (me % NPROCx == 0)              //left 
				{
					if (xc[j][0] >= (NxT - 2 * R_poly_max - safe))
						xc[j][0] -= NxT;
				}
				if ((me + 1) % NPROCx == 0)       //right
				{
					if (xc[j][0] < (2 * R_poly_max + safe))
						xc[j][0] += NxT;
				}
			}
			if (periodic_y == 1)
			{
				if (me < NPROCx)                  //bottom
				{
					if (xc[j][1] >= (NyT - 2 * R_poly_max - safe))
						xc[j][1] -= NyT;
				}
				if (me >= (NPROCx * (NPROCy - 1)))   //top
				{
					if (xc[j][1] < (2 * R_poly_max + safe))
						xc[j][1] += NyT;
				}
			}
		}
		else
		{
			//������������ʵ��
			if (periodic_x == 1)
			{
				if (me % NPROCx == 0)              //�����һ�к�
				{
					if (xc[j][0] >= (NxT - 2 * R - safe))
						xc[j][0] -= NxT;
				}
				if ((me + 1) % NPROCx == 0)       //���ұ�һ�к�
				{
					if (xc[j][0] < (2 * R + safe))
						xc[j][0] += NxT;
				}
			}
			if (periodic_y == 1)
			{
				if (me < NPROCx)                  //������һ�ź�
				{
					if (xc[j][1] >= (NyT - 2 * R - safe))
						xc[j][1] -= NyT;
				}
				if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
				{
					if (xc[j][1] < (2 * R + safe))
						xc[j][1] += NyT;
				}
			}
		}
		//z���������Ա߽�����
		if (periodic_z == 1)
		{
			if (xc[j][2] >= NzT)
				xc[j][2] -= NzT;
			if (xc[j][2] < 0)
				xc[j][2] += NzT;
		}
		
		//if (x_p0 >= 0 && x_p0 < Nx_p4 && y_p0 >= 0 && y_p0 < Ny_p4 && z_p0 >= 0 && z_p0 < Nz_p4)   //����ײ����������
		if ((xc[j][0] >= xx2) && (xc[j][0] < xxn2) && (xc[j][1] >= yy2) && (xc[j][1] < yyn2))
		{
			if (if_polydisperse == 1)
			{
				num_p_poly[k] = j;
				if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
					KeyProcess_poly[k] = me;
			}
			else
			{
				num_p[k] = j;
				if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
					KeyProcess[k] = me;
			}

			k++;
		}	
	}
	
	MPI_Barrier(MPI_COMM_WORLD);
}


//--------------------------------------------------------------
//calculate u, p, and statistics on local slice -- m, Uave and pave
void Fluid_u_p(int me)
{
	int i, x, y, z;
	double A, s0;

	m_fluid = 0;
	//pave = 0.0;
	aveU[0] = 0.0;
	aveU[1] = 0.0;
	aveU[2] = 0.0;

	// 使用 OpenMP 并行化：对 m_fluid 和 aveU 分量做 reduction
	double aveUx = 0.0, aveUy = 0.0, aveUz = 0.0;
	int m_fluid_temp = 0;

	// local absolute maximum fluid velocity on this MPI rank
	double local_fluid_ux_max = 0.0;
	double local_fluid_uy_max = 0.0;
	double local_fluid_uz_max = 0.0;

	// local maximum Mach number and its location
	double local_fluid_ma_max = 0.0;
	double local_ma_u[3] = { 0.0, 0.0, 0.0 };
	int local_ma_x = -1;
	int local_ma_y = -1;
	int local_ma_z = -1;

	// MPI position
	int me_y = me / NPROCx;
	int me_x = me - me_y * NPROCx;

#pragma omp parallel
	{
		double thread_ma_max = 0.0;
		double thread_ma_u[3] = { 0.0, 0.0, 0.0 };
		int thread_ma_x = -1;
		int thread_ma_y = -1;
		int thread_ma_z = -1;

#pragma omp for collapse(3) private(x, y, z)
		for (x = 0; x <= Nx3; x++)
		{
			for (y = 0; y <= Ny3; y++)
			{
				for (z = 0; z <= Nz3; z++)
				{
					forcef[x][y][z][0] = 0.0;
					forcef[x][y][z][1] = 0.0;
					forcef[x][y][z][2] = 0.0;
					//flag[x][y][z] = 0;
				}
			}
		}
#pragma omp for collapse(3) private(x, y, z, i, A, s0) reduction(+ : m_fluid_temp, aveUx, aveUy, aveUz) reduction(max : local_fluid_ux_max, local_fluid_uy_max, local_fluid_uz_max)
		for (x = 2; x <= Nx1; x++)
		{
			for (y = 2; y <= Ny1; y++)
			{
				for (z = 2; z <= Nz1; z++)
				{
					//forcef[x][y][z][0] = 0.0;
					//forcef[x][y][z][1] = 0.0;
					//forcef[x][y][z][2] = 0.0;
					//flag[x][y][z] = 0;

					A = f[x][y][z][1];
					for (i = 2; i < Q; i++)
						A += f[x][y][z][i];

					//u[x][y][z][0] = -f[1][x][y][z] - f[4][x][y][z] - f[5][x][y][z] - f[6][x][y][z] - f[7][x][y][z] + f[10][x][y][z] + f[13][x][y][z] + f[14][x][y][z] + f[15][x][y][z] + f[16][x][y][z];
					//u[x][y][z][0] *= c;
					//u[x][y][z][1] = -f[2][x][y][z] - f[4][x][y][z] + f[5][x][y][z] - f[8][x][y][z] - f[9][x][y][z] + f[11][x][y][z] + f[13][x][y][z] - f[14][x][y][z] + f[17][x][y][z] + f[18][x][y][z];
					//u[x][y][z][1] *= c;
					//u[x][y][z][2] = -f[3][x][y][z] - f[6][x][y][z] + f[7][x][y][z] - f[8][x][y][z] + f[9][x][y][z] + f[12][x][y][z] + f[15][x][y][z] - f[16][x][y][z] + f[17][x][y][z] - f[18][x][y][z];
					//u[x][y][z][2] *= c;
					u[x][y][z][0] = -f[x][y][z][1] - f[x][y][z][4] - f[x][y][z][5] - f[x][y][z][6] - f[x][y][z][7] + f[x][y][z][10] + f[x][y][z][13] + f[x][y][z][14] + f[x][y][z][15] + f[x][y][z][16];
					u[x][y][z][0] *= c;
					u[x][y][z][1] = -f[x][y][z][2] - f[x][y][z][4] + f[x][y][z][5] - f[x][y][z][8] - f[x][y][z][9] + f[x][y][z][11] + f[x][y][z][13] - f[x][y][z][14] + f[x][y][z][17] + f[x][y][z][18];
					u[x][y][z][1] *= c;
					u[x][y][z][2] = -f[x][y][z][3] - f[x][y][z][6] + f[x][y][z][7] - f[x][y][z][8] + f[x][y][z][9] + f[x][y][z][12] + f[x][y][z][15] - f[x][y][z][16] + f[x][y][z][17] - f[x][y][z][18];
					u[x][y][z][2] *= c;

					// fluid velocity and Mach-number monitoring
					local_fluid_ux_max = fmax(local_fluid_ux_max, fabs(u[x][y][z][0]));
					local_fluid_uy_max = fmax(local_fluid_uy_max, fabs(u[x][y][z][1]));
					local_fluid_uz_max = fmax(local_fluid_uz_max, fabs(u[x][y][z][2]));

					double velocity_mag =
						sqrt(u[x][y][z][0] * u[x][y][z][0]
							+ u[x][y][z][1] * u[x][y][z][1]
							+ u[x][y][z][2] * u[x][y][z][2]);

					double ma_local = velocity_mag / cs;

					if (ma_local > thread_ma_max)
					{
						thread_ma_max = ma_local;

						thread_ma_x = x;
						thread_ma_y = y;
						thread_ma_z = z;

						thread_ma_u[0] = u[x][y][z][0];
						thread_ma_u[1] = u[x][y][z][1];
						thread_ma_u[2] = u[x][y][z][2];
					}
					// --------------------------------------------------------------

					s0 = -0.3333333 * (u[x][y][z][0] * u[x][y][z][0] + u[x][y][z][1] * u[x][y][z][1] + u[x][y][z][2] * u[x][y][z][2]) / (2 * cs_square);
					p[x][y][z] = cs_square / (1 - 0.33333333) * (A + s0);

					//pave += p[x][y][z];
					//aveU[0] += u[x][y][z][0];
					//aveU[1] += u[x][y][z][1];
					//aveU[2] += u[x][y][z][2];
					aveUx += u[x][y][z][0];
					aveUy += u[x][y][z][1];
					aveUz += u[x][y][z][2];
					m_fluid_temp += 1;
				}
			}
		}
#pragma omp critical(fluid_ma_max_update)
		{
			if (thread_ma_max > local_fluid_ma_max)
			{
				local_fluid_ma_max = thread_ma_max;

				local_ma_x = thread_ma_x;
				local_ma_y = thread_ma_y;
				local_ma_z = thread_ma_z;

				local_ma_u[0] = thread_ma_u[0];
				local_ma_u[1] = thread_ma_u[1];
				local_ma_u[2] = thread_ma_u[2];
			}
		}
	}
	//pave /= m_fluid;
	//aveU[0] /= m_fluid;
	//aveU[1] /= m_fluid;
	//aveU[2] /= m_fluid;

	// save local MPI-rank maximum values
	fluid_ux_max = local_fluid_ux_max;
	fluid_uy_max = local_fluid_uy_max;
	fluid_uz_max = local_fluid_uz_max;
	fluid_ma_max = local_fluid_ma_max;


	// output warning when local Mach number exceeds threshold
	if (local_fluid_ma_max > MA_WARN)
	{
		// local array index -> global Eulerian coordinate
		int global_x = me_x * Nx + local_ma_x - 1;
		int global_y = me_y * Ny + local_ma_y - 1;

		// NPROCz = 1 in the current program
		int global_z = local_ma_z - 1;

		fprintf(stderr,
			"[MA_WARNING][FLUID] "
			"step=%d rank=%d "
			"local_index=(%d,%d,%d) "
			"global_coord=(%d,%d,%d) "
			"u=(%.12e, %.12e, %.12e) "
			"|u|=%.12e Ma=%.12e threshold=%.6f\n",
			step,
			me,
			local_ma_x, local_ma_y, local_ma_z,
			global_x, global_y, global_z,
			local_ma_u[0], local_ma_u[1], local_ma_u[2],
			sqrt(local_ma_u[0] * local_ma_u[0]
				+ local_ma_u[1] * local_ma_u[1]
				+ local_ma_u[2] * local_ma_u[2]),
			local_fluid_ma_max,
			MA_WARN);

		fflush(stderr);
	}

	// 把 reduction 累加结果写回共享数组
	m_fluid = m_fluid_temp;
	aveU[0] = aveUx;
	aveU[1] = aveUy;
	aveU[2] = aveUz;

	MPI_Barrier(MPI_COMM_WORLD);
}

//---------------------------------------------------------
// exchange fluid velocity at slice interface using MPI praparing for IB calculation
void Fluid_data_exchange(int me)
{
	MPI_Status status;

	int x, y, z, j;
	int low_ex, up_ex, right_ex, left_ex;
	double  u_ex[8][Nx4][Nz4][3], p_ex[8][Nx4][Nz4];

	if (periodic_x == 1)
	{
		if (me % NPROCx == 0)              //�����һ�к�
			left_ex = me + NPROCx - 1;
		else
			left_ex = me - 1;
		if ((me + 1) % NPROCx == 0)       //���ұ�һ�к�
			right_ex = me - NPROCx + 1;
		else
			right_ex = me + 1;
	}
	else if (periodic_x == 0)
	{
		if (me % NPROCx == 0)              //�����һ�к�
			left_ex = MPI_PROC_NULL;
		else
			left_ex = me - 1;

		if ((me + 1) % NPROCx == 0)       //���ұ�һ�к�
			right_ex = MPI_PROC_NULL;
		else
			right_ex = me + 1;
	}
	if (periodic_y == 1)
	{
		if (me < NPROCx)                  //������һ�ź�
			low_ex = me + NPROCx * (NPROCy - 1);
		else
			low_ex = me - NPROCx;

		if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
			up_ex = me - NPROCx * (NPROCy - 1);
		else
			up_ex = me + NPROCx;
	}
	else if (periodic_y == 0)
	{
		if (me < NPROCx)                  //������һ�ź�
			low_ex = MPI_PROC_NULL;
		else
			low_ex = me - NPROCx;

		if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
			up_ex = MPI_PROC_NULL;
		else
			up_ex = me + NPROCx;
	}

	MPI_Sendrecv(&u[Nx][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, right_ex, 100, &u[0][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, left_ex, 100, MPI_COMM_WORLD, &status);
	MPI_Sendrecv(&p[Nx][0][0], 2 * Ny4 * Nz4, MPI_DOUBLE, right_ex, 110, &p[0][0][0], 2 * Ny4 * Nz4, MPI_DOUBLE, left_ex, 110, MPI_COMM_WORLD, &status);

	MPI_Sendrecv(&u[2][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, left_ex, 120, &u[Nx2][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, right_ex, 120, MPI_COMM_WORLD, &status);
	MPI_Sendrecv(&p[2][0][0], 2 * Ny4 * Nz4, MPI_DOUBLE, left_ex, 130, &p[Nx2][0][0], 2 * Ny4 * Nz4, MPI_DOUBLE, right_ex, 130, MPI_COMM_WORLD, &status);
	

#pragma omp parallel
	{
#pragma omp for collapse(2) private(x, z, j)
		for (x = 0; x <= Nx3; x++)
		{
			for (z = 0; z <= Nz3; z++)
			{
				for (j = 0; j < 3; j++)
				{
					//u_ex[0][x][z][j] = u[x][0][z][j];
					//u_ex[1][x][z][j] = u[x][1][z][j];
					u_ex[2][x][z][j] = u[x][2][z][j];
					u_ex[3][x][z][j] = u[x][3][z][j];
					u_ex[4][x][z][j] = u[x][Ny][z][j];
					u_ex[5][x][z][j] = u[x][Ny1][z][j];
					//u_ex[6][x][z][j] = u[x][Ny2][z][j];
					//u_ex[7][x][z][j] = u[x][Ny3][z][j];
				}
				p_ex[2][x][z] = p[x][2][z];
				p_ex[3][x][z] = p[x][3][z];
				p_ex[4][x][z] = p[x][Ny][z];
				p_ex[5][x][z] = p[x][Ny1][z];
			}
		}
	}

	MPI_Sendrecv(&u_ex[4][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, up_ex, 140, &u_ex[0][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, low_ex, 140, MPI_COMM_WORLD, &status);
	MPI_Sendrecv(&p_ex[4][0][0], 2 * Nx4 * Nz4, MPI_DOUBLE, up_ex, 150, &p_ex[0][0][0], 2 * Nx4 * Nz4, MPI_DOUBLE, low_ex, 150, MPI_COMM_WORLD, &status);
	MPI_Sendrecv(&u_ex[2][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, low_ex, 160, &u_ex[6][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, up_ex, 160, MPI_COMM_WORLD, &status);
	MPI_Sendrecv(&p_ex[2][0][0], 2 * Nx4 * Nz4, MPI_DOUBLE, low_ex, 170, &p_ex[6][0][0], 2 * Nx4 * Nz4, MPI_DOUBLE, up_ex, 170, MPI_COMM_WORLD, &status);

#pragma omp parallel
	{
#pragma omp for collapse(2) private(x, z, j)
		for (x = 0; x <= Nx3; x++)
		{
			for (z = 0; z <= Nz3; z++)
			{
				for (j = 0; j < 3; j++)
				{
					if (low_ex != MPI_PROC_NULL)
					{
						u[x][0][z][j] = u_ex[0][x][z][j];
						u[x][1][z][j] = u_ex[1][x][z][j];
					}
					//u[x][2][z][j] = u_ex[2][x][z][j];
					//u[x][3][z][j] = u_ex[3][x][z][j];
					//u[x][Ny][z][j] = u_ex[4][x][z][j];
					//u[x][Ny1][z][j] = u_ex[5][x][z][j];
					if (up_ex != MPI_PROC_NULL)
					{
						u[x][Ny2][z][j] = u_ex[6][x][z][j];
						u[x][Ny3][z][j] = u_ex[7][x][z][j];
					}
				}
				if (low_ex != MPI_PROC_NULL)
				{
					p[x][0][z] = p_ex[0][x][z];
					p[x][1][z] = p_ex[1][x][z];
				}
				if (up_ex != MPI_PROC_NULL)
				{
					p[x][Ny2][z] = p_ex[6][x][z];
					p[x][Ny3][z] = p_ex[7][x][z];
				}
			}
		}

		//z�����������
		if (periodic_z == 1)
		{
#pragma omp for collapse(2) private(x, y, j)
			for (x = 0; x <= Nx3; x++)
			{
				for (y = 0; y <= Ny3; y++)
				{
					for (j = 0; j < 3; j++)
					{
						u[x][y][0][j] = u[x][y][Nz][j];
						u[x][y][1][j] = u[x][y][Nz1][j];
						u[x][y][Nz2][j] = u[x][y][2][j];
						u[x][y][Nz3][j] = u[x][y][3][j];
					}
					p[x][y][0] = p[x][y][Nz];
					p[x][y][1] = p[x][y][Nz1];
					p[x][y][Nz2] = p[x][y][2];
					p[x][y][Nz3] = p[x][y][3];
				}
			}
		}
	}
	
	MPI_Barrier(MPI_COMM_WORLD);
}

void Fluid_Boundary(int me)
{
	MPI_Status status;
	int me_x, me_y;
	int x, y, z, i;
	double m, n;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;

#pragma omp parallel
	{
#if (periodic_x == 0)
		{
			if (me_x == 0)
			{
#pragma omp for private(y, z, i,m,n)
				for (y = 1; y <= Ny2; y++)
				{
					for (z = 1; z <= Nz2; z++)
					{
						if (boundary_x1 == 0)
						{
							u[1][y][z][0] = u[2][y][z][0];
							u[1][y][z][1] = u[2][y][z][1];
							u[1][y][z][2] = u[2][y][z][2];
							u[0][y][z][0] = u[2][y][z][0];
							u[0][y][z][1] = u[2][y][z][1];
							u[0][y][z][2] = u[2][y][z][2];
						}
						p[1][y][z] = p[2][y][z];

						for (i = 0; i < Q; i++)
						{
							m = fequation(u[1][y][z], p[1][y][z], i);
							n = fequation(u[2][y][z], p[2][y][z], i);
							f[1][y][z][i] = m + (f[2][y][z][i] - n);
						}
					}
				}
			}
			if (me_x == (NPROCx - 1))
			{
#pragma omp for private(y, z, i,m,n)
				for (y = 1; y <= Ny2; y++)
				{
					for (z = 1; z <= Nz2; z++)
					{
						if (boundary_x2 == 0)
						{
							u[Nx2][y][z][0] = u[Nx1][y][z][0];
							u[Nx2][y][z][1] = u[Nx1][y][z][1];
							u[Nx2][y][z][2] = u[Nx1][y][z][2];
							u[Nx3][y][z][0] = u[Nx1][y][z][0];
							u[Nx3][y][z][1] = u[Nx1][y][z][1];
							u[Nx3][y][z][2] = u[Nx1][y][z][2];
						}
						p[Nx2][y][z] = p[Nx1][y][z];

						for (i = 0; i < Q; i++)
						{
							m = fequation(u[Nx2][y][z], p[Nx2][y][z], i);
							n = fequation(u[Nx1][y][z], p[Nx1][y][z], i);
							f[Nx2][y][z][i] = m + (f[Nx1][y][z][i] - n);
						}
					}
				}
			}
		}
#endif

#if (periodic_y == 0)
		{
			if (me_y == 0)
			{
#pragma omp for private(x, z, i,m,n)
				for (x = 1; x <= Nx2; x++)
				{
					for (z = 1; z <= Nz2; z++)   //������	
					{
						if (boundary_y1 == 0)
						{
							u[x][1][z][0] = u[x][2][z][0];
							u[x][1][z][1] = u[x][2][z][1];
							u[x][1][z][2] = u[x][2][z][2];
							u[x][0][z][0] = u[x][2][z][0];
							u[x][0][z][1] = u[x][2][z][1];
							u[x][0][z][2] = u[x][2][z][2];
						}
						p[x][1][z] = p[x][2][z];

						for (i = 0; i < Q; i++)
						{
							m = fequation(u[x][1][z], p[x][1][z], i);
							n = fequation(u[x][2][z], p[x][2][z], i);
							f[x][1][z][i] = m + (f[x][2][z][i] - n);
						}
					}
				}
			}
			if (me_y == (NPROCy - 1))
			{
#pragma omp for private(x, z, i,m,n)
				for (x = 1; x <= Nx2; x++)
				{
					for (z = 1; z <= Nz2; z++)   //������	
					{
						if (boundary_y2 == 0)
						{
							u[x][Ny2][z][0] = u[x][Ny1][z][0];
							u[x][Ny2][z][1] = u[x][Ny1][z][1];
							u[x][Ny2][z][2] = u[x][Ny1][z][2];
							u[x][Ny3][z][0] = u[x][Ny1][z][0];
							u[x][Ny3][z][1] = u[x][Ny1][z][1];
							u[x][Ny3][z][2] = u[x][Ny1][z][2];
						}
						p[x][Ny2][z] = p[x][Ny1][z];

						for (i = 0; i < Q; i++)
						{
							m = fequation(u[x][Ny2][z], p[x][Ny2][z], i);
							n = fequation(u[x][Ny1][z], p[x][Ny1][z], i);
							f[x][Ny2][z][i] = m + (f[x][Ny1][z][i] - n);
						}
					}
				}
			}
		}
#endif

#if (periodic_z == 0)
		{
#pragma omp for private(x, y, i,m,n)
			for (x = 1; x <= Nx2; x++)
			{
				for (y = 1; y <= Ny2; y++)
				{
					if (boundary_z1 == 0)
					{
						u[x][y][1][0] = u[x][y][2][0];
						u[x][y][1][1] = u[x][y][2][1];
						u[x][y][1][2] = u[x][y][2][2];
						u[x][y][0][0] = u[x][y][2][0];
						u[x][y][0][1] = u[x][y][2][1];
						u[x][y][0][2] = u[x][y][2][2];
					}
					p[x][y][1] = p[x][y][2];

					if (boundary_z2 == 0)
					{
						u[x][y][Nz2][0] = u[x][y][Nz1][0];
						u[x][y][Nz2][1] = u[x][y][Nz1][1];
						u[x][y][Nz2][2] = u[x][y][Nz1][2];
						u[x][y][Nz3][0] = u[x][y][Nz1][0];
						u[x][y][Nz3][1] = u[x][y][Nz1][1];
						u[x][y][Nz3][2] = u[x][y][Nz1][2];
					}
					p[x][y][Nz2] = p[x][y][Nz1];

					for (i = 0; i < Q; i++)
					{
						m = fequation(u[x][y][1], p[x][y][1], i);
						n = fequation(u[x][y][2], p[x][y][2], i);
						f[x][y][1][i] = m + (f[x][y][2][i] - n);

						m = fequation(u[x][y][Nz2], p[x][y][Nz2], i);
						n = fequation(u[x][y][Nz1], p[x][y][Nz1], i);
						f[x][y][Nz2][i] = m + (f[x][y][Nz1][i] - n);
					}
				}
			}
		}
#endif
#if (periodic_x == 0)
		{
			if (me_x == 0)
			{
#pragma omp for private(y, z, i,m,n)
				for (y = 1; y <= Ny2; y++)
				{
					for (z = 1; z <= Nz2; z++)
					{
						if (boundary_x1 == 1)
						{
							u[1][y][z][0] = boundary_x1_u[0];
							u[1][y][z][1] = boundary_x1_u[1];
							u[1][y][z][2] = boundary_x1_u[2];
							u[0][y][z][0] = boundary_x1_u[0];
							u[0][y][z][1] = boundary_x1_u[1];
							u[0][y][z][2] = boundary_x1_u[2];
						}
						p[1][y][z] = p[2][y][z];

						for (i = 0; i < Q; i++)
						{
							m = fequation(u[1][y][z], p[1][y][z], i);
							n = fequation(u[2][y][z], p[2][y][z], i);
							f[1][y][z][i] = m + (f[2][y][z][i] - n);
						}
					}
				}
			}
			if (me_x == (NPROCx - 1))
			{
#pragma omp for private(y, z, i,m,n)
				for (y = 1; y <= Ny2; y++)
				{
					for (z = 1; z <= Nz2; z++)
					{
						if (boundary_x2 == 1)
						{
							u[Nx2][y][z][0] = boundary_x2_u[0];
							u[Nx2][y][z][1] = boundary_x2_u[1];
							u[Nx2][y][z][2] = boundary_x2_u[2];
							u[Nx3][y][z][0] = boundary_x2_u[0];
							u[Nx3][y][z][1] = boundary_x2_u[1];
							u[Nx3][y][z][2] = boundary_x2_u[2];
						}
						p[Nx2][y][z] = p[Nx1][y][z];

						for (i = 0; i < Q; i++)
						{
							m = fequation(u[Nx2][y][z], p[Nx2][y][z], i);
							n = fequation(u[Nx1][y][z], p[Nx1][y][z], i);
							f[Nx2][y][z][i] = m + (f[Nx1][y][z][i] - n);
						}
					}
				}
			}
		}
#endif

#if (periodic_y == 0)
		{
			if (me_y == 0)
			{
#pragma omp for private(x, z, i,m,n)
				for (x = 1; x <= Nx2; x++)
				{
					for (z = 1; z <= Nz2; z++)   //������	
					{
						if (boundary_y1 == 1)
						{

							u[x][1][z][0] = boundary_y1_u[0];
							u[x][1][z][1] = boundary_y1_u[1];
							u[x][1][z][2] = boundary_y1_u[2];
							u[x][0][z][0] = boundary_y1_u[0];
							u[x][0][z][1] = boundary_y1_u[1];
							u[x][0][z][2] = boundary_y1_u[2];
						}
						p[x][1][z] = p[x][2][z];

						for (i = 0; i < Q; i++)
						{
							m = fequation(u[x][1][z], p[x][1][z], i);
							n = fequation(u[x][2][z], p[x][2][z], i);
							f[x][1][z][i] = m + (f[x][2][z][i] - n);
						}
					}
				}
			}
			if (me_y == (NPROCy - 1))
			{
#pragma omp for private(x, z, i,m,n)
				for (x = 1; x <= Nx2; x++)
				{
					for (z = 1; z <= Nz2; z++)   //������	
					{
						if (boundary_y2 == 1)
						{

							u[x][Ny2][z][0] = boundary_y2_u[0];
							u[x][Ny2][z][1] = boundary_y2_u[1];
							u[x][Ny2][z][2] = boundary_y2_u[2];
							u[x][Ny3][z][0] = boundary_y2_u[0];
							u[x][Ny3][z][1] = boundary_y2_u[1];
							u[x][Ny3][z][2] = boundary_y2_u[2];
						}
						p[x][Ny2][z] = p[x][Ny1][z];

						for (i = 0; i < Q; i++)
						{
							m = fequation(u[x][Ny2][z], p[x][Ny2][z], i);
							n = fequation(u[x][Ny1][z], p[x][Ny1][z], i);
							f[x][Ny2][z][i] = m + (f[x][Ny1][z][i] - n);
						}
					}
				}
			}
		}
#endif

#if (periodic_z == 0)
		{
#pragma omp for private(x, y, i,m,n)
			for (x = 1; x <= Nx2; x++)
			{
				for (y = 1; y <= Ny2; y++)
				{
					if (boundary_z1 == 1)
					{
						u[x][y][1][0] = boundary_z1_u[0];
						u[x][y][1][1] = boundary_z1_u[1];
						u[x][y][1][2] = boundary_z1_u[2];
						u[x][y][0][0] = boundary_z1_u[0];
						u[x][y][0][1] = boundary_z1_u[1];
						u[x][y][0][2] = boundary_z1_u[2];
					}
					p[x][y][1] = p[x][y][2];

					if (boundary_z2 == 1)
					{
						u[x][y][Nz2][0] = boundary_z2_u[0];
						u[x][y][Nz2][1] = boundary_z2_u[1];
						u[x][y][Nz2][2] = boundary_z2_u[2];
						u[x][y][Nz3][0] = boundary_z2_u[0];
						u[x][y][Nz3][1] = boundary_z2_u[1];
						u[x][y][Nz3][2] = boundary_z2_u[2];
					}

					p[x][y][Nz2] = p[x][y][Nz1];

					for (i = 0; i < Q; i++)
					{
						m = fequation(u[x][y][1], p[x][y][1], i);
						n = fequation(u[x][y][2], p[x][y][2], i);
						f[x][y][1][i] = m + (f[x][y][2][i] - n);

						m = fequation(u[x][y][Nz2], p[x][y][Nz2], i);
						n = fequation(u[x][y][Nz1], p[x][y][Nz1], i);
						f[x][y][Nz2][i] = m + (f[x][y][Nz1][i] - n);
					}
				}
			}
		}
#endif
	}//end omp parallel
}


void IBM_Particle(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z,z1;
	double lx, ly, lz;
	double uf[ns][nt1][3], up[ns][nt1][3], xb_Local[ns][nt1], yb_Local[ns][nt1], zb_Local[ns][nt1];  //���ϵ��������ٶȣ������ٶȣ�IB����ʱ����
	double xc_key[3];
	double force_IB[3];
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;	
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;

	me_y = int(me / NPROCx);
	me_x = me - me_y*NPROCx;

#pragma omp parallel
	{
		double xc_key[3];
		double force_IB[3];
		double uf[ns][nt1][3], up[ns][nt1][3], xb_Local[ns][nt1], yb_Local[ns][nt1], zb_Local[ns][nt1];

		int num_threads = omp_get_num_threads();
		bool is_single = (num_threads == 1);

	#pragma omp for schedule(dynamic,10) private(i,j,k,kk,x,y,z,z1,lx,ly,lz,IB_interpo,temp,\
											xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local)
		for (kk = 0; kk < np_local; kk++)
		{
			if (num_p[kk] >= 0)
			{
				k = num_p[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}

				if ((xc[k][0] > xx4) && (xc[k][0] < xxn4) && (xc[k][1] > yy4) && (xc[k][1] < yyn4))
				{
					xc_key[0] = xc[k][0] + 1 - me_x * Nx;
					xc_key[1] = xc[k][1] + 1 - me_y * Ny;
					xc_key[2] = xc[k][2] + 1;

					for (i = 0; i < ns; i++)
						for (j = 0; j < nt[i]; j++)
						{
							xb_Local[i][j] = xc_key[0] + xb[i][j];
							yb_Local[i][j] = xc_key[1] + yb[i][j];
							zb_Local[i][j] = xc_key[2] + zb[i][j];

							if (xb_Local[i][j] >= 0.5 && xb_Local[i][j] <= (Nx + 2.5) && yb_Local[i][j] >= 0.5 && yb_Local[i][j] <= (Ny + 2.5))
							{
								up[i][j][0] = uc[k][0] + (wc[k][2] * (-yb[i][j]) + wc[k][1] * (zb[i][j])) * dx;
								up[i][j][1] = uc[k][1] + (wc[k][2] * (xb[i][j]) + wc[k][0] * (-zb[i][j])) * dx;
								up[i][j][2] = uc[k][2] + (wc[k][0] * (yb[i][j]) + wc[k][1] * (-xb[i][j])) * dx;
								uf[i][j][0] = 0.0; uf[i][j][1] = 0.0; uf[i][j][2] = 0.0;

								xx_Local = (xb_Local[i][j] - 1.5);
								if (xx_Local <= 0)   xx_Local = 0;
								xxn_Local = (xb_Local[i][j] + 1.5);
								if (xxn_Local >= Nx3)   xxn_Local = Nx3;
								yy_Local = (yb_Local[i][j] - 1.5);
								if (yy_Local <= 0)   yy_Local = 0;
								yyn_Local = (yb_Local[i][j] + 1.5);
								if (yyn_Local >= Ny3)   yyn_Local = Ny3;
								zz_Local = (zb_Local[i][j] - 1.5);
								zzn_Local = (zb_Local[i][j] + 1.5);

								for (x = int(xx_Local); x <= int(xxn_Local); x++)
									for (y = int(yy_Local); y <= int(yyn_Local); y++)
										for (z = int(zz_Local); z <= int(zzn_Local); z++)
										{
											lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
											IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz);

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											uf[i][j][0] += u[x][y][z1][0] * IB_interpo;
											uf[i][j][1] += u[x][y][z1][1] * IB_interpo;
											uf[i][j][2] += u[x][y][z1][2] * IB_interpo;
										}
								force_IB[0] = 2 * rhof * (uf[i][j][0] - up[i][j][0]) / dt;
								force_IB[1] = 2 * rhof * (uf[i][j][1] - up[i][j][1]) / dt;
								force_IB[2] = 2 * rhof * (uf[i][j][2] - up[i][j][2]) / dt;

								for (x = int(xx_Local); x <= int(xxn_Local); x++)
									for (y = int(yy_Local); y <= int(yyn_Local); y++)
										for (z = int(zz_Local); z <= int(zzn_Local); z++)
										{
											lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
											IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz) * (LP_dr * LP_area[i]);

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											if (is_single)
											{
												forcef[x][y][z1][0] -= force_IB[0] * IB_interpo;
												forcef[x][y][z1][1] -= force_IB[1] * IB_interpo;
												forcef[x][y][z1][2] -= force_IB[2] * IB_interpo;
											}
											else
											{
#pragma omp atomic
												forcef[x][y][z1][0] -= force_IB[0] * IB_interpo;
#pragma omp atomic
												forcef[x][y][z1][1] -= force_IB[1] * IB_interpo;
#pragma omp atomic
												forcef[x][y][z1][2] -= force_IB[2] * IB_interpo;
											}
										}

								if (xb_Local[i][j] >= 1.5 && xb_Local[i][j] <= (Nx + 1.5) && yb_Local[i][j] >= 1.5 && yb_Local[i][j] <= (Ny + 1.5))
								{
									F_IBM[k][0] += force_IB[0] * LP_area[i];
									F_IBM[k][1] += force_IB[1] * LP_area[i];
									F_IBM[k][2] += force_IB[2] * LP_area[i];
									T_IBM[k][0] += (force_IB[2] * yb[i][j] - force_IB[1] * zb[i][j]) * dx * LP_area[i];
									T_IBM[k][1] += (force_IB[0] * zb[i][j] - force_IB[2] * xb[i][j]) * dx * LP_area[i];
									T_IBM[k][2] += (force_IB[1] * xb[i][j] - force_IB[0] * yb[i][j]) * dx * LP_area[i];
								}
								//tiaoshixinxi
								//if (sqrt(force_IB[0] * force_IB[0] + force_IB[1] * force_IB[1] + force_IB[1] * force_IB[1])*LP_area[i] * LP_dr * dx*dx*dx > 1.0)
								//{
								//	printf("Rank_ID=%d\t time=%d\tpar_num=%d\tlagrangi position = %f\t%f\t%f\t%f\t%f\t%f\n", me, step, k + 1, xb[i][j], yb[i][j], zb[i][j], force_IB[0], force_IB[1], force_IB[2]);
								//}
							}
						}


					temp = LP_dr * (dx * dx * dx);
					for (i = 0; i < 3; i++)
					{
						F_IBM[k][i] *= temp;	T_IBM[k][i] *= temp;
					}
				}
				//printf("Rank:%d\t%d_th particle \tindex=%d\txc=%f\t%f\t%f\tF=%f\t%f\t%f\n", me, k + 1, kk, xc[k][0], xc[k][1], xc[k][2], F_IBM[k][0], F_IBM[k][1], F_IBM[k][2]);
			}
		}
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_less(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, z1;
	double lx, ly, lz;
	double uf[ns][nt1][3], up[ns][nt1][3], xb_Local[ns][nt1], yb_Local[ns][nt1], zb_Local[ns][nt1];  //���ϵ��������ٶȣ������ٶȣ�IB����ʱ����
	double xc_key[3];
	double force_IB[3];
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, local_forcef_size;
	double f_coeffi = 2 * rhof / dt;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;

	IB_interpo_list_size = 4; //4 for delta_3p func 
	local_forcef_size = int(2 * rb) + 4; //plus 4 for delta_3p func

#pragma omp parallel
	{
		double xc_key[3];
		double force_IB[3];
		double uf[ns][nt1][3], up[ns][nt1][3], xb_Local[ns][nt1], yb_Local[ns][nt1], zb_Local[ns][nt1];

		int num_threads = omp_get_num_threads();
		bool is_single = (num_threads == 1);

		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];

#pragma omp for schedule(dynamic,10) private(i,j,k,kk,x,y,z,z1,lx,ly,lz,IB_interpo,temp,\
											xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox, IB_interpoy, IB_interpoz)
		for (kk = 0; kk < np_local; kk++)
		{
			if (num_p[kk] >= 0)
			{
				k = num_p[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}

				if ((xc[k][0] > xx4) && (xc[k][0] < xxn4) && (xc[k][1] > yy4) && (xc[k][1] < yyn4))
				{
					xc_key[0] = xc[k][0] + 1 - me_x * Nx;
					xc_key[1] = xc[k][1] + 1 - me_y * Ny;
					xc_key[2] = xc[k][2] + 1;

					for (i = 0; i < ns; i++)
						for (j = 0; j < nt[i]; j++)
						{
							xb_Local[i][j] = xc_key[0] + xb[i][j];
							yb_Local[i][j] = xc_key[1] + yb[i][j];
							zb_Local[i][j] = xc_key[2] + zb[i][j];

							if (xb_Local[i][j] >= 0.5 && xb_Local[i][j] <= (Nx + 2.5) && yb_Local[i][j] >= 0.5 && yb_Local[i][j] <= (Ny + 2.5))
							{
								up[i][j][0] = uc[k][0] + (wc[k][2] * (-yb[i][j]) + wc[k][1] * (zb[i][j])) * dx;
								up[i][j][1] = uc[k][1] + (wc[k][2] * (xb[i][j]) + wc[k][0] * (-zb[i][j])) * dx;
								up[i][j][2] = uc[k][2] + (wc[k][0] * (yb[i][j]) + wc[k][1] * (-xb[i][j])) * dx;
								uf[i][j][0] = 0.0; uf[i][j][1] = 0.0; uf[i][j][2] = 0.0;

								xx_Local = (xb_Local[i][j] - 1.5);
								if (xx_Local <= 0)   xx_Local = 0;
								xxn_Local = (xb_Local[i][j] + 1.5);
								if (xxn_Local >= Nx3)   xxn_Local = Nx3;
								yy_Local = (yb_Local[i][j] - 1.5);
								if (yy_Local <= 0)   yy_Local = 0;
								yyn_Local = (yb_Local[i][j] + 1.5);
								if (yyn_Local >= Ny3)   yyn_Local = Ny3;
								zz_Local = (zb_Local[i][j] - 1.5);
								zzn_Local = (zb_Local[i][j] + 1.5);

								xx_Local1 = int(xx_Local);xxn_Local1 = int(xxn_Local);
                                yy_Local1 = int(yy_Local);yyn_Local1 = int(yyn_Local);
                                zz_Local1 = int(zz_Local);zzn_Local1 = int(zzn_Local);

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									lx = x - xb_Local[i][j];
									IB_interpox = delta_3p(lx);
									for (y = yy_Local1; y <= yyn_Local1; y++)
									{
										ly = y - yb_Local[i][j];
                                        IB_interpoy = IB_interpox*delta_3p(ly);
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											lz = z - zb_Local[i][j];
											IB_interpoz =  IB_interpoy*delta_3p(lz);
											IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											uf[i][j][0] += u[x][y][z1][0] * IB_interpoz;
											uf[i][j][1] += u[x][y][z1][1] * IB_interpoz;
											uf[i][j][2] += u[x][y][z1][2] * IB_interpoz;
										}
									}
								}
								force_IB[0] = f_coeffi * (uf[i][j][0] - up[i][j][0]) * LP_area[i];
								force_IB[1] = f_coeffi * (uf[i][j][1] - up[i][j][1]) * LP_area[i];
								force_IB[2] = f_coeffi * (uf[i][j][2] - up[i][j][2]) * LP_area[i];

								for (x = xx_Local1; x <= xxn_Local1; x++)
									for (y = yy_Local1; y <= yyn_Local1; y++)
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											//lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
											//IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz) * (LP_dr * LP_area[i]);
											IB_interpo = IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] * LP_dr;

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											if (is_single)
											{
												forcef[x][y][z1][0] -= force_IB[0] * IB_interpo;
												forcef[x][y][z1][1] -= force_IB[1] * IB_interpo;
												forcef[x][y][z1][2] -= force_IB[2] * IB_interpo;
											}
											else
											{
#pragma omp atomic
												forcef[x][y][z1][0] -= force_IB[0] * IB_interpo;
#pragma omp atomic
												forcef[x][y][z1][1] -= force_IB[1] * IB_interpo;
#pragma omp atomic
												forcef[x][y][z1][2] -= force_IB[2] * IB_interpo;
											}
											
										}

								if (xb_Local[i][j] >= 1.5 && xb_Local[i][j] <= (Nx + 1.5) && yb_Local[i][j] >= 1.5 && yb_Local[i][j] <= (Ny + 1.5))
								{
									F_IBM[k][0] += force_IB[0];
									F_IBM[k][1] += force_IB[1];
									F_IBM[k][2] += force_IB[2];
									T_IBM[k][0] += (force_IB[2] * yb[i][j] - force_IB[1] * zb[i][j]) * dx;
									T_IBM[k][1] += (force_IB[0] * zb[i][j] - force_IB[2] * xb[i][j]) * dx;
									T_IBM[k][2] += (force_IB[1] * xb[i][j] - force_IB[0] * yb[i][j]) * dx;
								}
								//tiaoshixinxi
								//if (sqrt(force_IB[0] * force_IB[0] + force_IB[1] * force_IB[1] + force_IB[1] * force_IB[1])*LP_area[i] * LP_dr * dx*dx*dx > 1.0)
								//{
								//	printf("Rank_ID=%d\t time=%d\tpar_num=%d\tlagrangi position = %f\t%f\t%f\t%f\t%f\t%f\n", me, step, k + 1, xb[i][j], yb[i][j], zb[i][j], force_IB[0], force_IB[1], force_IB[2]);
								//}
							}
						}


					temp = LP_dr * (dx * dx * dx);
					for (i = 0; i < 3; i++)
					{
						F_IBM[k][i] *= temp;	T_IBM[k][i] *= temp;
					}
				}
				//printf("Rank:%d\t%d_th particle \tindex=%d\txc=%f\t%f\t%f\tF=%f\t%f\t%f\n", me, k + 1, kk, xc[k][0], xc[k][1], xc[k][2], F_IBM[k][0], F_IBM[k][1], F_IBM[k][2]);
			}
		}
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_less2(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, z1;
	double lx, ly, lz;
	double uf[ns][nt1][3], up[ns][nt1][3], xb_Local[ns][nt1], yb_Local[ns][nt1], zb_Local[ns][nt1];  //���ϵ��������ٶȣ������ٶȣ�IB����ʱ����
	double xc_key[3];
	double force_IB[3];
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, local_forcef_size;
	double f_coeffi = 2 * rhof / dt;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;

	IB_interpo_list_size = 4; //4 for delta_3p func 
	local_forcef_size = int(2 * rb) + 4; //plus 4 for delta_3p func

#pragma omp parallel
	{
		double xc_key[3];
		double force_IB[3];
		double uf[ns][nt1][3], up[ns][nt1][3], xb_Local[ns][nt1], yb_Local[ns][nt1], zb_Local[ns][nt1];

		int num_threads = omp_get_num_threads();
		bool is_single = (num_threads == 1);

		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];

#pragma omp for schedule(dynamic,10) private(i,j,k,kk,x,y,z,z1,lx,ly,lz,IB_interpo,temp,\
											xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox, IB_interpoy, IB_interpoz)
		for (kk = 0; kk < np_local; kk++)
		{
			if (num_p[kk] >= 0)
			{
				k = num_p[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}

				if ((xc[k][0] > xx4) && (xc[k][0] < xxn4) && (xc[k][1] > yy4) && (xc[k][1] < yyn4))
				{
					xc_key[0] = xc[k][0] + 1 - me_x * Nx;
					xc_key[1] = xc[k][1] + 1 - me_y * Ny;
					xc_key[2] = xc[k][2] + 1;

					for (i = 0; i < ns; i++)
						for (j = 0; j < nt[i]; j++)
						{
							xb_Local[i][j] = xc_key[0] + xb[i][j];
							yb_Local[i][j] = xc_key[1] + yb[i][j];
							zb_Local[i][j] = xc_key[2] + zb[i][j];

							if (xb_Local[i][j] >= 0.5 && xb_Local[i][j] <= (Nx + 2.5) && yb_Local[i][j] >= 0.5 && yb_Local[i][j] <= (Ny + 2.5))
							{
								up[i][j][0] = uc[k][0] + (wc[k][2] * (-yb[i][j]) + wc[k][1] * (zb[i][j])) * dx;
								up[i][j][1] = uc[k][1] + (wc[k][2] * (xb[i][j]) + wc[k][0] * (-zb[i][j])) * dx;
								up[i][j][2] = uc[k][2] + (wc[k][0] * (yb[i][j]) + wc[k][1] * (-xb[i][j])) * dx;
								uf[i][j][0] = 0.0; uf[i][j][1] = 0.0; uf[i][j][2] = 0.0;

								xx_Local = (xb_Local[i][j] - 1.5);
								if (xx_Local <= 0)   xx_Local = 0;
								xxn_Local = (xb_Local[i][j] + 1.5);
								if (xxn_Local >= Nx3)   xxn_Local = Nx3;
								yy_Local = (yb_Local[i][j] - 1.5);
								if (yy_Local <= 0)   yy_Local = 0;
								yyn_Local = (yb_Local[i][j] + 1.5);
								if (yyn_Local >= Ny3)   yyn_Local = Ny3;
								zz_Local = (zb_Local[i][j] - 1.5);
								zzn_Local = (zb_Local[i][j] + 1.5);

								xx_Local1 = int(xx_Local); xxn_Local1 = int(xxn_Local);
								yy_Local1 = int(yy_Local); yyn_Local1 = int(yyn_Local);
								zz_Local1 = int(zz_Local); zzn_Local1 = int(zzn_Local);

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									lx = x - xb_Local[i][j];
									IB_interpo_listx[x-xx_Local1] = delta_3p(lx);
								}
								for (y = yy_Local1; y <= yyn_Local1; y++)
								{
									ly = y - yb_Local[i][j];
									IB_interpo_listy[y - yy_Local1] = delta_3p(ly);
								}
								for (z = zz_Local1; z <= zzn_Local1; z++)
								{
									lz = z - zb_Local[i][j];
									IB_interpo_listz[z - zz_Local1] = delta_3p(lz);
								}

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									IB_interpox = IB_interpo_listx[x - xx_Local1];
									for (y = yy_Local1; y <= yyn_Local1; y++)
									{
										IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
											IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

											z1 = z;
											if (z <= 1)   z1 = z + Nz;
											if (z >= Nz2)  z1 = z - Nz;
											uf[i][j][0] += u[x][y][z1][0] * IB_interpoz;
											uf[i][j][1] += u[x][y][z1][1] * IB_interpoz;
											uf[i][j][2] += u[x][y][z1][2] * IB_interpoz;
										}
									}
								}
								force_IB[0] = f_coeffi * (uf[i][j][0] - up[i][j][0]) * LP_area[i];
								force_IB[1] = f_coeffi * (uf[i][j][1] - up[i][j][1]) * LP_area[i];
								force_IB[2] = f_coeffi * (uf[i][j][2] - up[i][j][2]) * LP_area[i];

								for (x = xx_Local1; x <= xxn_Local1; x++)
									for (y = yy_Local1; y <= yyn_Local1; y++)
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											//lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
											//IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz) * (LP_dr * LP_area[i]);
											IB_interpo = IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] * LP_dr;

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											if (is_single)
											{
												forcef[x][y][z1][0] -= force_IB[0] * IB_interpo;
												forcef[x][y][z1][1] -= force_IB[1] * IB_interpo;
												forcef[x][y][z1][2] -= force_IB[2] * IB_interpo;
											}
											else
											{
#pragma omp atomic
												forcef[x][y][z1][0] -= force_IB[0] * IB_interpo;
#pragma omp atomic
												forcef[x][y][z1][1] -= force_IB[1] * IB_interpo;
#pragma omp atomic
												forcef[x][y][z1][2] -= force_IB[2] * IB_interpo;
											}

										}

								if (xb_Local[i][j] >= 1.5 && xb_Local[i][j] <= (Nx + 1.5) && yb_Local[i][j] >= 1.5 && yb_Local[i][j] <= (Ny + 1.5))
								{
									F_IBM[k][0] += force_IB[0];
									F_IBM[k][1] += force_IB[1];
									F_IBM[k][2] += force_IB[2];
									T_IBM[k][0] += (force_IB[2] * yb[i][j] - force_IB[1] * zb[i][j]) * dx;
									T_IBM[k][1] += (force_IB[0] * zb[i][j] - force_IB[2] * xb[i][j]) * dx;
									T_IBM[k][2] += (force_IB[1] * xb[i][j] - force_IB[0] * yb[i][j]) * dx;
								}
								//tiaoshixinxi
								//if (sqrt(force_IB[0] * force_IB[0] + force_IB[1] * force_IB[1] + force_IB[1] * force_IB[1])*LP_area[i] * LP_dr * dx*dx*dx > 1.0)
								//{
								//	printf("Rank_ID=%d\t time=%d\tpar_num=%d\tlagrangi position = %f\t%f\t%f\t%f\t%f\t%f\n", me, step, k + 1, xb[i][j], yb[i][j], zb[i][j], force_IB[0], force_IB[1], force_IB[2]);
								//}
							}
						}


					temp = LP_dr * (dx * dx * dx);
					for (i = 0; i < 3; i++)
					{
						F_IBM[k][i] *= temp;	T_IBM[k][i] *= temp;
					}
				}
				//printf("Rank:%d\t%d_th particle \tindex=%d\txc=%f\t%f\t%f\tF=%f\t%f\t%f\n", me, k + 1, kk, xc[k][0], xc[k][1], xc[k][2], F_IBM[k][0], F_IBM[k][1], F_IBM[k][2]);
			}
		}
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_less_forcef_local(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, x1, y1, z1;
	double lx, ly, lz;
	double uf[ns][nt1][3], up[ns][nt1][3], xb_Local[ns][nt1], yb_Local[ns][nt1], zb_Local[ns][nt1];  //���ϵ��������ٶȣ������ٶȣ�IB����ʱ����
	double xc_key[3];
	double force_IB[3];
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, forcef_local_size;
	int forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax;
	int xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2;
	double f_coeffi = 2 * rhof / dt;
	double lc1 = (rb + 2.6) * (rb + 2.6);
	double lc2 = (rb - 2.6) * (rb - 2.6);

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;

	IB_interpo_list_size = 4; //4 for delta_3p func 
	forcef_local_size = int(2 * rb) + 6; //plus 4 for delta_3p func
	//I dont know why it need eextra plus 6 for delta_3p func, but it works

#pragma omp parallel
	{
		double xc_key[3];
		double force_IB[3];
		double uf[ns][nt1][3], up[ns][nt1][3], xb_Local[ns][nt1], yb_Local[ns][nt1], zb_Local[ns][nt1];

		int num_threads = omp_get_num_threads();
		bool is_single = (num_threads == 1);

		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];
		double forcef_local[forcef_local_size][forcef_local_size][forcef_local_size][3];

#pragma omp for schedule(dynamic,10) private(i,j,k,kk,x,y,z,x1,y1,z1,lx,ly,lz,IB_interpo,temp,\
											xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox, IB_interpoy, IB_interpoz,\
											forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax,\
											xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2)
		for (kk = 0; kk < np_local; kk++)
		{
			if (num_p[kk] >= 0)
			{
				k = num_p[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}

				if ((xc[k][0] > xx4) && (xc[k][0] < xxn4) && (xc[k][1] > yy4) && (xc[k][1] < yyn4))
				{
					xc_key[0] = xc[k][0] + 1 - me_x * Nx;
					xc_key[1] = xc[k][1] + 1 - me_y * Ny;
					xc_key[2] = xc[k][2] + 1;

					forcef_local_xmin = int(xc_key[0] - rb - 1.5) - 1;
					forcef_local_ymin = int(xc_key[1] - rb - 1.5) - 1;
					forcef_local_zmin = int(xc_key[2] - rb - 1.5) - 1;
                    forcef_local_xmax = forcef_local_xmin + forcef_local_size-1;
                    forcef_local_ymax = forcef_local_ymin + forcef_local_size-1;
                    forcef_local_zmax = forcef_local_zmin + forcef_local_size-1;

					forcef_local_xmin = (forcef_local_xmin < 0)? 0 : forcef_local_xmin;
                    forcef_local_ymin = (forcef_local_ymin < 0)? 0 : forcef_local_ymin;
                    forcef_local_xmax = (forcef_local_xmax > Nx3)? Nx3 : forcef_local_xmax;
                    forcef_local_ymax = (forcef_local_ymax > Ny3)? Ny3 : forcef_local_ymax;

					memset(forcef_local, 0, sizeof(forcef_local));

					for (i = 0; i < ns; i++)
						for (j = 0; j < nt[i]; j++)
						{
							xb_Local[i][j] = xc_key[0] + xb[i][j];
							yb_Local[i][j] = xc_key[1] + yb[i][j];
							zb_Local[i][j] = xc_key[2] + zb[i][j];

							if (xb_Local[i][j] >= 0.5 && xb_Local[i][j] <= (Nx + 2.5) && yb_Local[i][j] >= 0.5 && yb_Local[i][j] <= (Ny + 2.5))
							{
								up[i][j][0] = uc[k][0] + (wc[k][2] * (-yb[i][j]) + wc[k][1] * (zb[i][j])) * dx;
								up[i][j][1] = uc[k][1] + (wc[k][2] * (xb[i][j]) + wc[k][0] * (-zb[i][j])) * dx;
								up[i][j][2] = uc[k][2] + (wc[k][0] * (yb[i][j]) + wc[k][1] * (-xb[i][j])) * dx;
								uf[i][j][0] = 0.0; uf[i][j][1] = 0.0; uf[i][j][2] = 0.0;

								xx_Local = (xb_Local[i][j] - 1.5);
								if (xx_Local <= 0)   xx_Local = 0;
								xxn_Local = (xb_Local[i][j] + 1.5);
								if (xxn_Local >= Nx3)   xxn_Local = Nx3;
								yy_Local = (yb_Local[i][j] - 1.5);
								if (yy_Local <= 0)   yy_Local = 0;
								yyn_Local = (yb_Local[i][j] + 1.5);
								if (yyn_Local >= Ny3)   yyn_Local = Ny3;
								zz_Local = (zb_Local[i][j] - 1.5);
								zzn_Local = (zb_Local[i][j] + 1.5);

								xx_Local1 = int(xx_Local); xxn_Local1 = int(xxn_Local);
								yy_Local1 = int(yy_Local); yyn_Local1 = int(yyn_Local);
								zz_Local1 = int(zz_Local); zzn_Local1 = int(zzn_Local);

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									lx = x - xb_Local[i][j];
									IB_interpo_listx[x - xx_Local1] = delta_3p(lx);
								}
                                for (y = yy_Local1; y <= yyn_Local1; y++)
								{
									ly = y - yb_Local[i][j];
									IB_interpo_listy[y - yy_Local1] = delta_3p(ly);
								}
                                for (z = zz_Local1; z <= zzn_Local1; z++)
								{
									lz = z - zb_Local[i][j];
									IB_interpo_listz[z - zz_Local1] = delta_3p(lz);
								}

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									IB_interpox = IB_interpo_listx[x - xx_Local1];
									for (y = yy_Local1; y <= yyn_Local1; y++)
									{
										IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
											IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											uf[i][j][0] += u[x][y][z1][0] * IB_interpoz;
											uf[i][j][1] += u[x][y][z1][1] * IB_interpoz;
											uf[i][j][2] += u[x][y][z1][2] * IB_interpoz;
										}
									}
								}
								force_IB[0] = f_coeffi * (uf[i][j][0] - up[i][j][0]) * LP_area[i];
								force_IB[1] = f_coeffi * (uf[i][j][1] - up[i][j][1]) * LP_area[i];
								force_IB[2] = f_coeffi * (uf[i][j][2] - up[i][j][2]) * LP_area[i];
								
								xx_Local2 = xx_Local1 - forcef_local_xmin;
								xxn_Local2 = xxn_Local1 - forcef_local_xmin;
								yy_Local2 = yy_Local1 - forcef_local_ymin;
								yyn_Local2 = yyn_Local1 - forcef_local_ymin;
								zz_Local2 = zz_Local1 - forcef_local_zmin;
								zzn_Local2 = zzn_Local1 - forcef_local_zmin;

								for (x = xx_Local2; x <= xxn_Local2; x++)
									for (y = yy_Local2; y <= yyn_Local2; y++)
										for (z = zz_Local2; z <= zzn_Local2; z++)
										{
											//lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
											//IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz) * (LP_dr * LP_area[i]);
											IB_interpo = IB_interpo_list[x - xx_Local2][y - yy_Local2][z - zz_Local2] * LP_dr;

											forcef_local[x][y][z][0] += force_IB[0] * IB_interpo;
											forcef_local[x][y][z][1] += force_IB[1] * IB_interpo;
											forcef_local[x][y][z][2] += force_IB[2] * IB_interpo;

										}

								if (xb_Local[i][j] >= 1.5 && xb_Local[i][j] <= (Nx + 1.5) && yb_Local[i][j] >= 1.5 && yb_Local[i][j] <= (Ny + 1.5))
								{
									F_IBM[k][0] += force_IB[0];
									F_IBM[k][1] += force_IB[1];
									F_IBM[k][2] += force_IB[2];
									T_IBM[k][0] += (force_IB[2] * yb[i][j] - force_IB[1] * zb[i][j]) * dx;
									T_IBM[k][1] += (force_IB[0] * zb[i][j] - force_IB[2] * xb[i][j]) * dx;
									T_IBM[k][2] += (force_IB[1] * xb[i][j] - force_IB[0] * yb[i][j]) * dx;
								}
								//tiaoshixinxi
								//if (sqrt(force_IB[0] * force_IB[0] + force_IB[1] * force_IB[1] + force_IB[1] * force_IB[1])*LP_area[i] * LP_dr * dx*dx*dx > 1.0)
								//{
								//	printf("Rank_ID=%d\t time=%d\tpar_num=%d\tlagrangi position = %f\t%f\t%f\t%f\t%f\t%f\n", me, step, k + 1, xb[i][j], yb[i][j], zb[i][j], force_IB[0], force_IB[1], force_IB[2]);
								//}
							}
						}


						temp = LP_dr * (dx * dx * dx);
						for (i = 0; i < 3; i++)
						{
							F_IBM[k][i] *= temp;	T_IBM[k][i] *= temp;
						}
						if (is_single)
						{
							for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
							{
								x1 = x - forcef_local_xmin;
								lx = (xc_key[0] - x) * (xc_key[0] - x);
								for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
								{
									y1 = y - forcef_local_ymin;
									ly = (xc_key[1] - y) * (xc_key[1] - y);
									for (z1 = 0; z1 < forcef_local_size; z1++)
									{

										z = z1 + forcef_local_zmin;
										lz = (xc_key[2] - z) * (xc_key[2] - z);
										if ((lx + ly + lz > lc2) && (lx + ly + lz < lc1))
										{
											if (periodic_z == 1)
											{
												if (z <= 1)
												{
													z = z + Nz;
												}
												else if (z >= Nz2)
												{
													z = z - Nz;
												}
											}

											forcef[x][y][z][0] -= forcef_local[x1][y1][z1][0];
											forcef[x][y][z][1] -= forcef_local[x1][y1][z1][1];
											forcef[x][y][z][2] -= forcef_local[x1][y1][z1][2];
										}
									}
								}
							}
						}
						else
						{
#pragma omp critical
							{
								for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
								{
									x1 = x - forcef_local_xmin;
									lx = (xc_key[0] - x) * (xc_key[0] - x);
									for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
									{
										y1 = y - forcef_local_ymin;
										ly = (xc_key[1] - y) * (xc_key[1] - y);
										for (z1 = 0; z1 < forcef_local_size; z1++)
										{

											z = z1 + forcef_local_zmin;
											lz = (xc_key[2] - z) * (xc_key[2] - z);
											if ((lx + ly + lz > lc2) && (lx + ly + lz < lc1))
											{
												if (periodic_z == 1)
												{
													if (z <= 1)
													{
														z = z + Nz;
													}
													else if (z >= Nz2)
													{
														z = z - Nz;
													}
												}

												forcef[x][y][z][0] -= forcef_local[x1][y1][z1][0];
												forcef[x][y][z][1] -= forcef_local[x1][y1][z1][1];
												forcef[x][y][z][2] -= forcef_local[x1][y1][z1][2];
											}
										}
									}
								}
							}
						}
				}
				//printf("Rank:%d\t%d_th particle \tindex=%d\txc=%f\t%f\t%f\tF=%f\t%f\t%f\n", me, k + 1, kk, xc[k][0], xc[k][1], xc[k][2], F_IBM[k][0], F_IBM[k][1], F_IBM[k][2]);
			}
		}
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_less_forcef_local2(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, x1, y1, z1;
	double lx, ly, lz;
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, forcef_local_size;
	int forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax;
	int xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2;
	double f_coeffi = 2 * rhof / dt;

	int flat_size1 = ns * nt1 * 3;
	int flat_size2 = ns * nt1;
	int idx_u0, idx_local0, idx_forcef0;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;
	const double key_offsetx = 1. - (double)(me_x) * (double)(Nx);
	const double key_offsety = 1. - (double)(me_y) * (double)(Ny);

	IB_interpo_list_size = 3; //4 for delta_3p func 
	forcef_local_size = int(2 * rb) + 6; //plus 4 for delta_3p func
	//I dont know why it need eextra plus 6 for delta_3p func, but it works

#pragma omp parallel
	{
		double xc_key[3];
		volatile double xc_key_temp[3];
		double force_IB[3];
		double* uf = (double*)malloc(flat_size1 * sizeof(double));
		double* up = (double*)malloc(flat_size1 * sizeof(double));
		double* xb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* yb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* zb_Local = (double*)malloc(flat_size2 * sizeof(double));

		int num_threads = omp_get_num_threads();
		bool is_single = (num_threads == 1);
		int thread_id = omp_get_thread_num();
#if IBM_DEBUG_OUTPUT
		char thread_filename[256];
		FILE* thread_debug_file = NULL;
#endif
		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];
		//double forcef_local[forcef_local_size][forcef_local_size][forcef_local_size][3];
		int forcef_local_total_size = forcef_local_size * forcef_local_size * forcef_local_size * 3;
		double* forcef_local = (double*)malloc(forcef_local_total_size * sizeof(double));

#pragma omp for schedule(dynamic,10) private(i,j,k,kk,x,y,z,x1,y1,z1,lx,ly,lz,IB_interpo,temp,\
											xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox, IB_interpoy, IB_interpoz,\
											forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax,\
											xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2,\
											idx_u0, idx_local0, idx_forcef0)
		for (kk = 0; kk < np_local; kk++)
		{
			if (num_p[kk] >= 0)
			{
				k = num_p[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}

				if ((xc[k][0] > xx4) && (xc[k][0] < xxn4) && (xc[k][1] > yy4) && (xc[k][1] < yyn4))
				{
					xc_key_temp[0] = xc[k][0];
					xc_key_temp[1] = xc[k][1];
					xc_key_temp[2] = xc[k][2];
					xc_key[0] = xc_key_temp[0] + key_offsetx;
					xc_key[1] = xc_key_temp[1] + key_offsety;
					xc_key[2] = xc_key_temp[2] + 1.0;

					forcef_local_xmin = int(xc_key[0] - rb - 1.5) - 1;
					forcef_local_ymin = int(xc_key[1] - rb - 1.5) - 1;
					forcef_local_zmin = int(xc_key[2] - rb - 1.5) - 1;
					forcef_local_xmax = forcef_local_xmin + forcef_local_size - 1;
					forcef_local_ymax = forcef_local_ymin + forcef_local_size - 1;
					forcef_local_zmax = forcef_local_zmin + forcef_local_size - 1;

					forcef_local_xmin = (forcef_local_xmin < 0) ? 0 : forcef_local_xmin;
					forcef_local_ymin = (forcef_local_ymin < 0) ? 0 : forcef_local_ymin;
					forcef_local_xmax = (forcef_local_xmax > Nx3) ? Nx3 : forcef_local_xmax;
					forcef_local_ymax = (forcef_local_ymax > Ny3) ? Ny3 : forcef_local_ymax;

					memset(forcef_local, 0, forcef_local_total_size * sizeof(double));

					for (i = 0; i < ns; i++)
						for (j = 0; j < nt[i]; j++)
						{
							idx_local0 = i * nt1 + j;
							xb_Local[idx_local0] = xc_key[0] + xb[i][j];
							yb_Local[idx_local0] = xc_key[1] + yb[i][j];
							zb_Local[idx_local0] = xc_key[2] + zb[i][j];

							if (xb_Local[idx_local0] >= 0.5 && xb_Local[idx_local0] <= (Nx + 2.5) && yb_Local[idx_local0] >= 0.5 && yb_Local[idx_local0] <= (Ny + 2.5))
							{
								idx_u0 = (i*nt1 + j) * 3;
								up[idx_u0]     = uc[k][0] + (wc[k][2] * (-yb[i][j]) + wc[k][1] * (zb[i][j])) * dx;
								up[idx_u0 + 1] = uc[k][1] + (wc[k][2] * (xb[i][j]) + wc[k][0] * (-zb[i][j])) * dx;
								up[idx_u0 + 2] = uc[k][2] + (wc[k][0] * (yb[i][j]) + wc[k][1] * (-xb[i][j])) * dx;
								uf[idx_u0] = 0.0; uf[idx_u0 + 1] = 0.0; uf[idx_u0 + 2] = 0.0;

								/*
								xx_Local = (xb_Local[idx_local0] - 1.5);
								if (xx_Local <= 0)   xx_Local = 0;
								xxn_Local = (xb_Local[idx_local0] + 1.5);
								if (xxn_Local >= Nx3)   xxn_Local = Nx3;
								yy_Local = (yb_Local[idx_local0] - 1.5);
								if (yy_Local <= 0)   yy_Local = 0;
								yyn_Local = (yb_Local[idx_local0] + 1.5);
								if (yyn_Local >= Ny3)   yyn_Local = Ny3;
								zz_Local = (zb_Local[idx_local0] - 1.5);
								zzn_Local = (zb_Local[idx_local0] + 1.5);

								xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = int(xxn_Local);
								yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = int(yyn_Local);
								zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = int(zzn_Local);
								*/
								xx_Local = (xb_Local[idx_local0] - 1.5);
								yy_Local = (yb_Local[idx_local0] - 1.5);
								zz_Local = (zb_Local[idx_local0] - 1.5);
								xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = xx_Local1+2;
								yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = yy_Local1+2;
								zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = zz_Local1+2;

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									lx = x - xb_Local[idx_local0];
									IB_interpo_listx[x - xx_Local1] = delta_3p(lx);
								}
								for (y = yy_Local1; y <= yyn_Local1; y++)
								{
									ly = y - yb_Local[idx_local0];
									IB_interpo_listy[y - yy_Local1] = delta_3p(ly);
								}
								for (z = zz_Local1; z <= zzn_Local1; z++)
								{
									lz = z - zb_Local[idx_local0];
									IB_interpo_listz[z - zz_Local1] = delta_3p(lz);
								}

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									IB_interpox = IB_interpo_listx[x - xx_Local1];
									for (y = yy_Local1; y <= yyn_Local1; y++)
									{
										IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
											IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											uf[idx_u0] += u[x][y][z1][0] * IB_interpoz;
											uf[idx_u0+1] += u[x][y][z1][1] * IB_interpoz;
											uf[idx_u0+2] += u[x][y][z1][2] * IB_interpoz;
										}
									}
								}
								force_IB[0] = f_coeffi * (uf[idx_u0] - up[idx_u0]) * LP_area[i];
								force_IB[1] = f_coeffi * (uf[idx_u0+1] - up[idx_u0+1]) * LP_area[i];
								force_IB[2] = f_coeffi * (uf[idx_u0+2] - up[idx_u0+2]) * LP_area[i];
#if IBM_DEBUG_OUTPUT
								// 1. 直接生成文件名 (每100步一个文件，例如 step_0-99_debug_poly_rank_0_thread_0.log)
								//    注意：step / 100 是整数除法
								sprintf(thread_filename, "IBM_lag_step_%03d-%03d_debug_poly_rank_%d_thread_%d.log",
									(step / 100) * 100, (step / 100) * 100 + 99, me, thread_id);

								// 2. 以追加模式 "a" 打开文件
								thread_debug_file = fopen(thread_filename, "a");

								if (thread_debug_file != NULL) {
									// 3. 写入信息 (全部使用 %.6f)
									fprintf(thread_debug_file,
										"Step=%d, Par_k=%d, LagPt=(%d,%d), "
										"Coord=(%.6f,%.6f,%.6f), "
										"uf=(%.6f,%.6f,%.6f), "
										"up=(%.6f,%.6f,%.6f), "
										"f_IB=(%.6f,%.6f,%.6f)\n",
										step, k, i, j,
										(xb_Local[idx_local0] - key_offsetx), (yb_Local[idx_local0] - key_offsety), (zb_Local[idx_local0] - 1.),
										uf[idx_u0], uf[idx_u0 + 1], uf[idx_u0 + 2],
										up[idx_u0], up[idx_u0 + 1], up[idx_u0 + 2],
										force_IB[0], force_IB[1], force_IB[2]);

									fclose(thread_debug_file); // 写完立即关闭，避免句柄泄漏
								}
#endif
								xx_Local2 = xx_Local1 - forcef_local_xmin;
								xxn_Local2 = xxn_Local1 - forcef_local_xmin;
								yy_Local2 = yy_Local1 - forcef_local_ymin;
								yyn_Local2 = yyn_Local1 - forcef_local_ymin;
								zz_Local2 = zz_Local1 - forcef_local_zmin;
								zzn_Local2 = zzn_Local1 - forcef_local_zmin;

								for (x = xx_Local2; x <= xxn_Local2; x++)
									for (y = yy_Local2; y <= yyn_Local2; y++)
										for (z = zz_Local2; z <= zzn_Local2; z++)
										{
											//lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
											//IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz) * (LP_dr * LP_area[i]);
											IB_interpo = IB_interpo_list[x - xx_Local2][y - yy_Local2][z - zz_Local2] * LP_dr;
											idx_forcef0 = ((x*forcef_local_size + y) * forcef_local_size + z) * 3;

											forcef_local[idx_forcef0] += force_IB[0] * IB_interpo;
											forcef_local[idx_forcef0+1] += force_IB[1] * IB_interpo;
											forcef_local[idx_forcef0+2] += force_IB[2] * IB_interpo;

										}

								if (xb_Local[idx_local0] >= 1.5 && xb_Local[idx_local0] <= (Nx + 1.5) && yb_Local[idx_local0] >= 1.5 && yb_Local[idx_local0] <= (Ny + 1.5))
								{
									F_IBM[k][0] += force_IB[0];
									F_IBM[k][1] += force_IB[1];
									F_IBM[k][2] += force_IB[2];
									T_IBM[k][0] += (force_IB[2] * yb[i][j] - force_IB[1] * zb[i][j]) * dx;
									T_IBM[k][1] += (force_IB[0] * zb[i][j] - force_IB[2] * xb[i][j]) * dx;
									T_IBM[k][2] += (force_IB[1] * xb[i][j] - force_IB[0] * yb[i][j]) * dx;
								}
							}
						}

					temp = LP_dr * (dx * dx * dx);
					for (i = 0; i < 3; i++)
					{
						F_IBM[k][i] *= temp;	T_IBM[k][i] *= temp;
					}
					if (is_single)
					{
						for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
						{
							x1 = x - forcef_local_xmin;
							for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
							{
								y1 = y - forcef_local_ymin;
								for (z1 = 0; z1 < forcef_local_size; z1++)
								{

									z = z1 + forcef_local_zmin;
									if (periodic_z == 1)
									{
										if (z <= 1)
										{
											z = z + Nz;
										}
										else if (z >= Nz2)
										{
											z = z - Nz;
										}
									}
									idx_forcef0 = ((x1 * forcef_local_size + y1) * forcef_local_size + z1) * 3;
									forcef[x][y][z][0] -= forcef_local[idx_forcef0];
									forcef[x][y][z][1] -= forcef_local[idx_forcef0+1];
									forcef[x][y][z][2] -= forcef_local[idx_forcef0+2];
								}
							}
						}
						/*
						if ((step % 50 == 0))
						{
							printf("me_x=%d me_y=%d xc_key=%f %f %f, NX=%d, NY=%d",me_x,me_y,xc_key[0], xc_key[1], xc_key[2], Nx, Ny);
							printf("true_xc_key = %f %f\n",(xc[k][0] + 1 - me_x * Nx),(xc[k][1] + 1 - me_y * Ny));
                            printf("Rank:%d\t%d_th particle index=%d\tKey=%d\txc=%f\t%f\t%f\tF=%f\t%f\t%f\tT=%f\t%f\t%f\n", me, k + 1, kk, KeyProcess[kk],xc[k][0], xc[k][1], xc[k][2], F_IBM[k][0], F_IBM[k][1], F_IBM[k][2],T_IBM[k][0], T_IBM[k][1], T_IBM[k][2]);
							printf("forcef_local xmin=%d xmax=%d ymin=%d ymax=%d zmin=%d zmax=%d\n",forcef_local_xmin,forcef_local_xmax,forcef_local_ymin,forcef_local_ymax,forcef_local_zmin,forcef_local_zmax);
							printf("temp variable idx_forcef0=%d idx_local0=%d idx_u0=%d\n",idx_forcef0,idx_local0,idx_u0);
							printf("%f\t%f\t%f\t%f\t%f\t%f\n",xx_Local,xxn_Local,yy_Local,yyn_Local,zz_Local,zzn_Local);
							printf("%d\t%d\t%d\t%d\t%d\t%d\n",xx_Local1,xxn_Local1,yy_Local1,yyn_Local1,zz_Local1,zzn_Local1);
							printf("%d\t%d\t%d\t%d\t%d\t%d\n",xx_Local2,xxn_Local2,yy_Local2,yyn_Local2,zz_Local2,zzn_Local2);
						}*/
					}
					else
					{
//#pragma omp critical
						{
							for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
							{
								x1 = x - forcef_local_xmin;
								for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
								{
									y1 = y - forcef_local_ymin;
									for (z1 = 0; z1 < forcef_local_size; z1++)
									{
										z = z1 + forcef_local_zmin;
										if (periodic_z == 1)
										{
											if (z <= 1)
											{
												z = z + Nz;
											}
											else if (z >= Nz2)
											{
												z = z - Nz;
											}
										}
										idx_forcef0 = ((x1 * forcef_local_size + y1) * forcef_local_size + z1) * 3;
#pragma omp atomic
										forcef[x][y][z][0] -= forcef_local[idx_forcef0];
#pragma omp atomic
										forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
#pragma omp atomic
										forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];
									}
								}
							}
						}
					}
				}
			}
		}
		free(uf);
		free(up);
		free(xb_Local);
		free(yb_Local);
		free(zb_Local);
		free(forcef_local);
		#pragma omp barrier
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_less_forcef_local3(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, x1, y1, z1;
	double lx, ly, lz;
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, forcef_local_size;
	int forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax;
	int xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2;
	double f_coeffi = 2 * rhof / dt;

	int flat_size1 = ns * nt1 * 3;
	int flat_size2 = ns * nt1;
	int idx_u0, idx_local0, idx_forcef0;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;
	const double key_offsetx = 1. - (double)(me_x) * (double)(Nx);
	const double key_offsety = 1. - (double)(me_y) * (double)(Ny);

	IB_interpo_list_size = 3; //4 for delta_3p func 
	forcef_local_size = int(2 * rb) + 6; //plus 4 for delta_3p func
	//I dont know why it need eextra plus 6 for delta_3p func, but it works

#pragma omp parallel
	{
		double xc_key[3];
		volatile double xc_key_temp[3];
		double force_IB[3];
		double* uf = (double*)malloc(flat_size1 * sizeof(double));
		double* up = (double*)malloc(flat_size1 * sizeof(double));
		double* xb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* yb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* zb_Local = (double*)malloc(flat_size2 * sizeof(double));
		int* zb1 = (int*)malloc(flat_size2 * sizeof(int));

		int num_threads = omp_get_num_threads();
		bool is_single = (num_threads == 1);
		int thread_id = omp_get_thread_num();
#if IBM_DEBUG_OUTPUT
		char thread_filename[256];
		FILE* thread_debug_file = NULL;
#endif
		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];
		//double forcef_local[forcef_local_size][forcef_local_size][forcef_local_size][3];
		int forcef_local_total_size = forcef_local_size * forcef_local_size * forcef_local_size * 3;
		double* forcef_local = (double*)malloc(forcef_local_total_size * sizeof(double));
		/* 每线程一份多字 z 行掩码，在不同颗粒之间复用，支持边长超过 64。 */
		size_t zmask_words_per_row = ((size_t)forcef_local_size + 63) / 64;
		size_t zmask_total_words = (size_t)forcef_local_size * forcef_local_size * zmask_words_per_row;
		uint64_t* zmask = (uint64_t*)malloc(zmask_total_words * sizeof(uint64_t));

#pragma omp for schedule(dynamic,10) private(i,j,k,kk,x,y,z,x1,y1,z1,lx,ly,lz,IB_interpo,temp,\
											xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox, IB_interpoy, IB_interpoz,\
											forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax,\
											xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2,\
											idx_u0, idx_local0, idx_forcef0)
		for (kk = 0; kk < np_local; kk++)
		{
			if (num_p[kk] >= 0)
			{
				k = num_p[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}

				if ((xc[k][0] > xx4) && (xc[k][0] < xxn4) && (xc[k][1] > yy4) && (xc[k][1] < yyn4))
				{
					xc_key_temp[0] = xc[k][0];
					xc_key_temp[1] = xc[k][1];
					xc_key_temp[2] = xc[k][2];
					xc_key[0] = xc_key_temp[0] + key_offsetx;
					xc_key[1] = xc_key_temp[1] + key_offsety;
					xc_key[2] = xc_key_temp[2] + 1.0;

					forcef_local_xmin = int(xc_key[0] - rb - 1.5) - 1;
					forcef_local_ymin = int(xc_key[1] - rb - 1.5) - 1;
					forcef_local_zmin = int(xc_key[2] - rb - 1.5) - 1;
					forcef_local_xmax = forcef_local_xmin + forcef_local_size - 1;
					forcef_local_ymax = forcef_local_ymin + forcef_local_size - 1;
					forcef_local_zmax = forcef_local_zmin + forcef_local_size - 1;

					forcef_local_xmin = (forcef_local_xmin < 0) ? 0 : forcef_local_xmin;
					forcef_local_ymin = (forcef_local_ymin < 0) ? 0 : forcef_local_ymin;
					forcef_local_xmax = (forcef_local_xmax > Nx3) ? Nx3 : forcef_local_xmax;
					forcef_local_ymax = (forcef_local_ymax > Ny3) ? Ny3 : forcef_local_ymax;

					memset(forcef_local, 0, forcef_local_total_size * sizeof(double));
					memset(zb1, 0, flat_size2 * sizeof(int));
					memset(zmask, 0, zmask_total_words * sizeof(uint64_t));

					for (i = 0; i < ns; i++)
						for (j = 0; j < nt[i]; j++)
						{
							idx_local0 = i * nt1 + j;
							xb_Local[idx_local0] = xc_key[0] + xb[i][j];
							yb_Local[idx_local0] = xc_key[1] + yb[i][j];
							zb_Local[idx_local0] = xc_key[2] + zb[i][j];
							/* 插值使用偏移后的 z，局部散布时再减去该偏移。 */
							if (periodic_z == 1)
							{
								if (zb_Local[idx_local0] <= 1)   zb1[idx_local0] = Nz;
								if (zb_Local[idx_local0] >= Nz2)  zb1[idx_local0] = -Nz;
							}

							if (xb_Local[idx_local0] >= 0.5 && xb_Local[idx_local0] <= (Nx + 2.5) && yb_Local[idx_local0] >= 0.5 && yb_Local[idx_local0] <= (Ny + 2.5))
							{
								idx_u0 = (i*nt1 + j) * 3;
								up[idx_u0]     = uc[k][0] + (wc[k][2] * (-yb[i][j]) + wc[k][1] * (zb[i][j])) * dx;
								up[idx_u0 + 1] = uc[k][1] + (wc[k][2] * (xb[i][j]) + wc[k][0] * (-zb[i][j])) * dx;
								up[idx_u0 + 2] = uc[k][2] + (wc[k][0] * (yb[i][j]) + wc[k][1] * (-xb[i][j])) * dx;
								uf[idx_u0] = 0.0; uf[idx_u0 + 1] = 0.0; uf[idx_u0 + 2] = 0.0;
								
								xx_Local = (xb_Local[idx_local0] - 1.5);
								yy_Local = (yb_Local[idx_local0] - 1.5);
								zz_Local = (zb_Local[idx_local0] + zb1[idx_local0] - 1.5);
								xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = xx_Local1+2;
								yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = yy_Local1+2;
								zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = zz_Local1+2;

								/* 每个方向的三个权重共用一次平方根。 */
								lx = xb_Local[idx_local0] - xx_Local1 - 1;
								delta_3p_less(lx, IB_interpo_listx);
								ly = yb_Local[idx_local0] - yy_Local1 - 1;
								delta_3p_less(ly, IB_interpo_listy);
								lz = zb_Local[idx_local0] - zz_Local1 - 1 + zb1[idx_local0];
								delta_3p_less(lz, IB_interpo_listz);

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									IB_interpox = IB_interpo_listx[x - xx_Local1];
									for (y = yy_Local1; y <= yyn_Local1; y++)
									{
										IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
											IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

											z1 = z;

											uf[idx_u0] += u[x][y][z1][0] * IB_interpoz;
											uf[idx_u0+1] += u[x][y][z1][1] * IB_interpoz;
											uf[idx_u0+2] += u[x][y][z1][2] * IB_interpoz;
										}
									}
								}
								force_IB[0] = f_coeffi * (uf[idx_u0] - up[idx_u0]) * LP_area[i];
								force_IB[1] = f_coeffi * (uf[idx_u0+1] - up[idx_u0+1]) * LP_area[i];
								force_IB[2] = f_coeffi * (uf[idx_u0+2] - up[idx_u0+2]) * LP_area[i];
#if IBM_DEBUG_OUTPUT
								// 1. 直接生成文件名 (每100步一个文件，例如 step_0-99_debug_poly_rank_0_thread_0.log)
								//    注意：step / 100 是整数除法
								sprintf(thread_filename, "IBM_lag_step_%03d-%03d_debug_poly_rank_%d_thread_%d.log",
									(step / 100) * 100, (step / 100) * 100 + 99, me, thread_id);

								// 2. 以追加模式 "a" 打开文件
								thread_debug_file = fopen(thread_filename, "a");

								if (thread_debug_file != NULL) {
									// 3. 写入信息 (全部使用 %.6f)
									fprintf(thread_debug_file,
										"Step=%d, Par_k=%d, LagPt=(%d,%d), "
										"Coord=(%.6f,%.6f,%.6f), "
										"uf=(%.6f,%.6f,%.6f), "
										"up=(%.6f,%.6f,%.6f), "
										"f_IB=(%.6f,%.6f,%.6f)\n",
										step, k, i, j,
										(xb_Local[idx_local0] - key_offsetx), (yb_Local[idx_local0] - key_offsety), (zb_Local[idx_local0] - 1.),
										uf[idx_u0], uf[idx_u0 + 1], uf[idx_u0 + 2],
										up[idx_u0], up[idx_u0 + 1], up[idx_u0 + 2],
										force_IB[0], force_IB[1], force_IB[2]);

									fclose(thread_debug_file); // 写完立即关闭，避免句柄泄漏
								}
#endif
								xx_Local2 = xx_Local1 - forcef_local_xmin;
								xxn_Local2 = xxn_Local1 - forcef_local_xmin;
								yy_Local2 = yy_Local1 - forcef_local_ymin;
								yyn_Local2 = yyn_Local1 - forcef_local_ymin;
								zz_Local2 = zz_Local1 - forcef_local_zmin - zb1[idx_local0];
								zzn_Local2 = zzn_Local1 - forcef_local_zmin - zb1[idx_local0];

								size_t zmask_word;
								unsigned int zmask_bit;
								uint64_t zmask_low, zmask_high;
								zmask_word = (size_t)zz_Local2 >> 6;
								zmask_bit = (unsigned int)zz_Local2 & 63u;
								zmask_low = (uint64_t)7 << zmask_bit;
								zmask_high = 0;
								if (zmask_bit >= 62u)
									zmask_high = (uint64_t)7 >> (64u - zmask_bit);

								for (x = xx_Local2; x <= xxn_Local2; x++)
									for (y = yy_Local2; y <= yyn_Local2; y++)
									{
										uint64_t* zmask_row;
										/* 每行一次标记三个 z 节点，起始位为 62/63 时同时标记下一字。 */
										zmask_row = zmask + ((size_t)x * forcef_local_size + y) * zmask_words_per_row;
										zmask_row[zmask_word] |= zmask_low;
										if (zmask_high != 0)
											zmask_row[zmask_word + 1] |= zmask_high;

										for (z = zz_Local2; z <= zzn_Local2; z++)
										{
											IB_interpo = IB_interpo_list[x - xx_Local2][y - yy_Local2][z - zz_Local2] * LP_dr;
											idx_forcef0 = ((x * forcef_local_size + y) * forcef_local_size + z) * 3;

											forcef_local[idx_forcef0] += force_IB[0] * IB_interpo;
											forcef_local[idx_forcef0 + 1] += force_IB[1] * IB_interpo;
											forcef_local[idx_forcef0 + 2] += force_IB[2] * IB_interpo;
										}
									}

								if (xb_Local[idx_local0] >= 1.5 && xb_Local[idx_local0] <= (Nx + 1.5) && yb_Local[idx_local0] >= 1.5 && yb_Local[idx_local0] <= (Ny + 1.5))
								{
									F_IBM[k][0] += force_IB[0];
									F_IBM[k][1] += force_IB[1];
									F_IBM[k][2] += force_IB[2];
									T_IBM[k][0] += (force_IB[2] * yb[i][j] - force_IB[1] * zb[i][j]) * dx;
									T_IBM[k][1] += (force_IB[0] * zb[i][j] - force_IB[2] * xb[i][j]) * dx;
									T_IBM[k][2] += (force_IB[1] * xb[i][j] - force_IB[0] * yb[i][j]) * dx;
								}
							}
						}

					temp = LP_dr * (dx * dx * dx);
					for (i = 0; i < 3; i++)
					{
						F_IBM[k][i] *= temp;	T_IBM[k][i] *= temp;
					}
					size_t active_z_words;	
					unsigned int active_z_tail;
					uint64_t last_z_mask;

					active_z_words = ((size_t)forcef_local_size + 63) / 64;
					active_z_tail = (unsigned int)forcef_local_size & 63u;
					last_z_mask = ~(uint64_t)0;

					/* 有效长度正好是 64 的整数倍时保留整字，避免左移 64 位。 */
					if (active_z_tail != 0)
						last_z_mask = ((uint64_t)1 << active_z_tail) - 1;

					if (is_single)
					{
						for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
						{
							x1 = x - forcef_local_xmin;
							for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
							{
								size_t local_row, force_row, mask_word_id;
								const uint64_t* zmask_row;

								y1 = y - forcef_local_ymin;
								local_row = (size_t)x1 * forcef_local_size + y1;
								force_row = local_row * forcef_local_size * 3;
								zmask_row = zmask + local_row * zmask_words_per_row;

								/* 只扫描当前颗粒使用的字；数组寻址仍用最大容量步长。 */
								for (mask_word_id = 0; mask_word_id < active_z_words; mask_word_id++)
								{
									uint64_t mask_word;
									mask_word = zmask_row[mask_word_id];
									if (mask_word_id + 1 == active_z_words)
										mask_word &= last_z_mask;

									while (mask_word != 0)
									{
										unsigned int bit;

										/* 输入非零时才调用 ctzll；逐次去掉最低一个置位。 */
										bit = (unsigned int)__builtin_ctzll((unsigned long long)mask_word);
										mask_word &= mask_word - 1;
										z1 = (int)(mask_word_id * 64 + bit);

										/* 标记属于未映射的局部节点，恢复全局 z 后再处理周期性。 */
										z = z1 + forcef_local_zmin;
										if (periodic_z == 1)
										{
											if (z <= 1)
												z += Nz;
											else if (z >= Nz2)
												z -= Nz;
										}
										idx_forcef0 = force_row + (size_t)z1 * 3;
										forcef[x][y][z][0] -= forcef_local[idx_forcef0];
										forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
										forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];
									}
								}
							}
						}
					}
					else
					{
						{
							for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
							{
								x1 = x - forcef_local_xmin;
								for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
								{
									size_t local_row, force_row, mask_word_id;
									const uint64_t* zmask_row;

									y1 = y - forcef_local_ymin;
									local_row = (size_t)x1 * forcef_local_size + y1;
									force_row = local_row * forcef_local_size * 3;
									zmask_row = zmask + local_row * zmask_words_per_row;

									/* 只扫描当前颗粒使用的字；数组寻址仍用最大容量步长。 */
									for (mask_word_id = 0; mask_word_id < active_z_words; mask_word_id++)
									{
										uint64_t mask_word;
										mask_word = zmask_row[mask_word_id];
										if (mask_word_id + 1 == active_z_words)
											mask_word &= last_z_mask;

										while (mask_word != 0)
										{
											unsigned int bit;

											/* 输入非零时才调用 ctzll；逐次去掉最低一个置位。 */
											bit = (unsigned int)__builtin_ctzll((unsigned long long)mask_word);
											mask_word &= mask_word - 1;
											z1 = (int)(mask_word_id * 64 + bit);

											/* 标记属于未映射的局部节点，恢复全局 z 后再处理周期性。 */
											z = z1 + forcef_local_zmin;
											if (periodic_z == 1)
											{
												if (z <= 1)
													z += Nz;
												else if (z >= Nz2)
													z -= Nz;
											}
											idx_forcef0 = force_row + (size_t)z1 * 3;
#pragma omp atomic
											forcef[x][y][z][0] -= forcef_local[idx_forcef0];
#pragma omp atomic
											forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
#pragma omp atomic
											forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];
										}
									}
								}
							}
						}
					}
				}
			}
		}
		free(uf);
		free(up);
		free(xb_Local);
		free(yb_Local);
		free(zb_Local);
		free(forcef_local);
		free(zb1);
		free(zmask);
		#pragma omp barrier
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_less_forcef_local2_MDF(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, x1, y1, z1;
	int xx, yy, zz, ii;
	double lx, ly, lz;
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, forcef_local_size;
	int forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax;
	int xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2;
	double f_coeffi = 2 * rhof / dt;

	int flat_size1 = ns * nt1 * 3;
	int flat_size2 = ns * nt1;
	int idx_u0, idx_local0, idx_forcef0;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;
	const double key_offsetx = 1. - (double)(me_x) * (double)(Nx);
	const double key_offsety = 1. - (double)(me_y) * (double)(Ny);

	IB_interpo_list_size = 3; //4 for delta_3p func 
	forcef_local_size = int(2 * rb) + 6; //plus 4 for delta_3p func
	//I dont know why it need eextra plus 6 for delta_3p func, but it works

	//for utemp exchange
	int low_ex, up_ex, right_ex, left_ex;
	double fluidu_temp_ex[8][Nx4][Nz4][3];

	if (periodic_x == 1)
	{
		if (me % NPROCx == 0)
			left_ex = me + NPROCx - 1;
		else
			left_ex = me - 1;
		if ((me + 1) % NPROCx == 0)
			right_ex = me - NPROCx + 1;
		else
			right_ex = me + 1;
	}
	else if (periodic_x == 0)
	{
		if (me % NPROCx == 0)
			left_ex = MPI_PROC_NULL;
		else
			left_ex = me - 1;
		if ((me + 1) % NPROCx == 0)
			right_ex = MPI_PROC_NULL;
		else
			right_ex = me + 1;
	}

	if (periodic_y == 1)
	{
		if (me < NPROCx)
			low_ex = me + NPROCx * (NPROCy - 1);
		else
			low_ex = me - NPROCx;
		if (me >= (NPROCx * (NPROCy - 1)))
			up_ex = me - NPROCx * (NPROCy - 1);
		else
			up_ex = me + NPROCx;
	}
	else if (periodic_y == 0)
	{
		if (me < NPROCx)
			low_ex = MPI_PROC_NULL;
		else
			low_ex = me - NPROCx;
		if (me >= (NPROCx * (NPROCy - 1)))
			up_ex = MPI_PROC_NULL;
		else
			up_ex = me + NPROCx;
	}

#pragma omp parallel
	{
		double xc_key[3];
		volatile double xc_key_temp[3];
		double force_IB[3];
		double* uf = (double*)malloc(flat_size1 * sizeof(double));
		double* up = (double*)malloc(flat_size1 * sizeof(double));
		double* xb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* yb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* zb_Local = (double*)malloc(flat_size2 * sizeof(double));

		int num_threads = omp_get_num_threads();
		bool is_single = (num_threads == 1);
		int thread_id = omp_get_thread_num();

#if IBM_DEBUG_OUTPUT
		char thread_filename[256];
		FILE* thread_debug_file = NULL;
#endif

		int iter_k;

		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];
		//double forcef_local[forcef_local_size][forcef_local_size][forcef_local_size][3];
		int forcef_local_total_size = forcef_local_size * forcef_local_size * forcef_local_size * 3;
		double* forcef_local = (double*)malloc(forcef_local_total_size * sizeof(double));

#pragma omp for schedule(static) private(k, kk, i)
		for (kk = 0; kk < np_local; kk++)
		{
			if (num_p[kk] >= 0)
			{
				k = num_p[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}
			}
		}
		for (iter_k = 0; iter_k < NF; iter_k++)
		{
#pragma omp for schedule(dynamic,10) private(i,j,k,kk,x,y,z,x1,y1,z1,lx,ly,lz,IB_interpo,temp,\
											xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox, IB_interpoy, IB_interpoz,\
											forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax,\
											xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2,\
											idx_u0, idx_local0, idx_forcef0)
			for (kk = 0; kk < np_local; kk++)
			{
				if (num_p[kk] >= 0)
				{
					k = num_p[kk];
					if ((xc[k][0] > xx4) && (xc[k][0] < xxn4) && (xc[k][1] > yy4) && (xc[k][1] < yyn4))
					{
						xc_key_temp[0] = xc[k][0];
						xc_key_temp[1] = xc[k][1];
						xc_key_temp[2] = xc[k][2];
						xc_key[0] = xc_key_temp[0] + key_offsetx;
						xc_key[1] = xc_key_temp[1] + key_offsety;
						xc_key[2] = xc_key_temp[2] + 1.0;

						forcef_local_xmin = int(xc_key[0] - rb - 1.5) - 1;
						forcef_local_ymin = int(xc_key[1] - rb - 1.5) - 1;
						forcef_local_zmin = int(xc_key[2] - rb - 1.5) - 1;
						forcef_local_xmax = forcef_local_xmin + forcef_local_size - 1;
						forcef_local_ymax = forcef_local_ymin + forcef_local_size - 1;
						forcef_local_zmax = forcef_local_zmin + forcef_local_size - 1;

						forcef_local_xmin = (forcef_local_xmin < 0) ? 0 : forcef_local_xmin;
						forcef_local_ymin = (forcef_local_ymin < 0) ? 0 : forcef_local_ymin;
						forcef_local_xmax = (forcef_local_xmax > Nx3) ? Nx3 : forcef_local_xmax;
						forcef_local_ymax = (forcef_local_ymax > Ny3) ? Ny3 : forcef_local_ymax;

						memset(forcef_local, 0, forcef_local_total_size * sizeof(double));

						for (i = 0; i < ns; i++)
							for (j = 0; j < nt[i]; j++)
							{
								idx_local0 = i * nt1 + j;
								xb_Local[idx_local0] = xc_key[0] + xb[i][j];
								yb_Local[idx_local0] = xc_key[1] + yb[i][j];
								zb_Local[idx_local0] = xc_key[2] + zb[i][j];

								if (xb_Local[idx_local0] >= 0.5 && xb_Local[idx_local0] <= (Nx + 2.5) && yb_Local[idx_local0] >= 0.5 && yb_Local[idx_local0] <= (Ny + 2.5))
								{
									idx_u0 = (i * nt1 + j) * 3;
									up[idx_u0] = uc[k][0] + (wc[k][2] * (-yb[i][j]) + wc[k][1] * (zb[i][j])) * dx;
									up[idx_u0 + 1] = uc[k][1] + (wc[k][2] * (xb[i][j]) + wc[k][0] * (-zb[i][j])) * dx;
									up[idx_u0 + 2] = uc[k][2] + (wc[k][0] * (yb[i][j]) + wc[k][1] * (-xb[i][j])) * dx;
									uf[idx_u0] = 0.0; uf[idx_u0 + 1] = 0.0; uf[idx_u0 + 2] = 0.0;
									/*		
									xx_Local = (xb_Local[idx_local0] - 1.5);
									if (xx_Local <= 0)   xx_Local = 0;
									xxn_Local = (xb_Local[idx_local0] + 1.5);
									if (xxn_Local >= Nx3)   xxn_Local = Nx3;
									yy_Local = (yb_Local[idx_local0] - 1.5);
									if (yy_Local <= 0)   yy_Local = 0;
									yyn_Local = (yb_Local[idx_local0] + 1.5);
									if (yyn_Local >= Ny3)   yyn_Local = Ny3;
									zz_Local = (zb_Local[idx_local0] - 1.5);
									zzn_Local = (zb_Local[idx_local0] + 1.5);

									xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = int(xxn_Local);
									yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = int(yyn_Local);
									zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = int(zzn_Local);
									*/
									xx_Local = (xb_Local[idx_local0] - 1.5);
									yy_Local = (yb_Local[idx_local0] - 1.5);
									zz_Local = (zb_Local[idx_local0] - 1.5);
									xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = xx_Local1+2;
									yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = yy_Local1+2;
									zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = zz_Local1+2;

									for (x = xx_Local1; x <= xxn_Local1; x++)
									{
										lx = x - xb_Local[idx_local0];
										IB_interpo_listx[x - xx_Local1] = delta_3p(lx);
									}
									for (y = yy_Local1; y <= yyn_Local1; y++)
									{
										ly = y - yb_Local[idx_local0];
										IB_interpo_listy[y - yy_Local1] = delta_3p(ly);
									}
									for (z = zz_Local1; z <= zzn_Local1; z++)
									{
										lz = z - zb_Local[idx_local0];
										IB_interpo_listz[z - zz_Local1] = delta_3p(lz);
									}
									if (iter_k > 0)
									{
										for (x = xx_Local1; x <= xxn_Local1; x++)
										{
											IB_interpox = IB_interpo_listx[x - xx_Local1];
											for (y = yy_Local1; y <= yyn_Local1; y++)
											{
												IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
												for (z = zz_Local1; z <= zzn_Local1; z++)
												{
													IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
													IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

													z1 = z;
													if (periodic_z == 1)
													{
														if (z <= 1)   z1 = z + Nz;
														if (z >= Nz2)  z1 = z - Nz;
													}
													uf[idx_u0] += fluidu_temp[x][y][z1][0] * IB_interpoz;
													uf[idx_u0 + 1] += fluidu_temp[x][y][z1][1] * IB_interpoz;
													uf[idx_u0 + 2] += fluidu_temp[x][y][z1][2] * IB_interpoz;
												}
											}
										}
									}
									else
									{
										for (x = xx_Local1; x <= xxn_Local1; x++)
										{
											IB_interpox = IB_interpo_listx[x - xx_Local1];
											for (y = yy_Local1; y <= yyn_Local1; y++)
											{
												IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
												for (z = zz_Local1; z <= zzn_Local1; z++)
												{
													IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
													IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

													z1 = z;
													if (periodic_z == 1)
													{
														if (z <= 1)   z1 = z + Nz;
														if (z >= Nz2)  z1 = z - Nz;
													}
													uf[idx_u0] += u[x][y][z1][0] * IB_interpoz;
													uf[idx_u0 + 1] += u[x][y][z1][1] * IB_interpoz;
													uf[idx_u0 + 2] += u[x][y][z1][2] * IB_interpoz;
												}
											}
										}
									}
									force_IB[0] = f_coeffi * (uf[idx_u0] - up[idx_u0]) * LP_area[i];
									force_IB[1] = f_coeffi * (uf[idx_u0 + 1] - up[idx_u0 + 1]) * LP_area[i];
									force_IB[2] = f_coeffi * (uf[idx_u0 + 2] - up[idx_u0 + 2]) * LP_area[i];

#if IBM_DEBUG_OUTPUT
									sprintf(thread_filename, "IBM_lag_step_%03d-%03d_debug_poly_rank_%d_thread_%d.log",
										(step / 100) * 100, (step / 100) * 100 + 99, me, thread_id);
									thread_debug_file = fopen(thread_filename, "a");
									if (thread_debug_file != NULL) {
										fprintf(thread_debug_file,
											"Step=%d, NF=%d, Par_k=%d, LagPt=(%d,%d), "
											"Coord=(%.6f,%.6f,%.6f), "
											"uf=(%.6f,%.6f,%.6f), "
											"up=(%.6f,%.6f,%.6f), "
											"f_IB=(%.6f,%.6f,%.6f)\n",
											step, iter_k, k, i, j,
											(xb_Local[idx_local0] - key_offsetx), (yb_Local[idx_local0] - key_offsety), (zb_Local[idx_local0] - 1.),
											uf[idx_u0], uf[idx_u0 + 1], uf[idx_u0 + 2],
											up[idx_u0], up[idx_u0 + 1], up[idx_u0 + 2],
											force_IB[0], force_IB[1], force_IB[2]);

										fclose(thread_debug_file); // 写完立即关闭，避免句柄泄漏
									}
#endif

									xx_Local2 = xx_Local1 - forcef_local_xmin;
									xxn_Local2 = xxn_Local1 - forcef_local_xmin;
									yy_Local2 = yy_Local1 - forcef_local_ymin;
									yyn_Local2 = yyn_Local1 - forcef_local_ymin;
									zz_Local2 = zz_Local1 - forcef_local_zmin;
									zzn_Local2 = zzn_Local1 - forcef_local_zmin;

									for (x = xx_Local2; x <= xxn_Local2; x++)
										for (y = yy_Local2; y <= yyn_Local2; y++)
											for (z = zz_Local2; z <= zzn_Local2; z++)
											{
												//lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
												//IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz) * (LP_dr * LP_area[i]);
												IB_interpo = IB_interpo_list[x - xx_Local2][y - yy_Local2][z - zz_Local2] * LP_dr;
												idx_forcef0 = ((x * forcef_local_size + y) * forcef_local_size + z) * 3;

												forcef_local[idx_forcef0] += force_IB[0] * IB_interpo;
												forcef_local[idx_forcef0 + 1] += force_IB[1] * IB_interpo;
												forcef_local[idx_forcef0 + 2] += force_IB[2] * IB_interpo;

											}

									if (xb_Local[idx_local0] >= 1.5 && xb_Local[idx_local0] <= (Nx + 1.5) && yb_Local[idx_local0] >= 1.5 && yb_Local[idx_local0] <= (Ny + 1.5))
									{
										F_IBM[k][0] += force_IB[0];
										F_IBM[k][1] += force_IB[1];
										F_IBM[k][2] += force_IB[2];
										T_IBM[k][0] += (force_IB[2] * yb[i][j] - force_IB[1] * zb[i][j]) * dx;
										T_IBM[k][1] += (force_IB[0] * zb[i][j] - force_IB[2] * xb[i][j]) * dx;
										T_IBM[k][2] += (force_IB[1] * xb[i][j] - force_IB[0] * yb[i][j]) * dx;
									}
								}
							}
						if (is_single)
						{
							for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
							{
								x1 = x - forcef_local_xmin;
								for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
								{
									y1 = y - forcef_local_ymin;
									for (z1 = 0; z1 < forcef_local_size; z1++)
									{

										z = z1 + forcef_local_zmin;
#if (periodic_z == 1)
												if (z <= 1)
												{
													z = z + Nz;
												}
												else if (z >= Nz2)
												{
													z = z - Nz;
												}
#endif
#if (periodic_z == 0)
												if (z >= Nz2 || z <= 1)
													continue;
#endif
#if (periodic_x == 0)
												if (me % NPROCx == 0)
												{
													if (x <= 1)
														continue;
												}
												if ((me + 1) % NPROCx == 0)
												{
													if (x >= Nx2)
														continue;
												}
#endif
#if (periodic_y == 0)
												if (me < NPROCx)
												{
													if (y <= 1)
														continue;
												}
												if (me >= (NPROCx * (NPROCy - 1)))
												{
													if (y >= Ny2)
														continue;
												}
#endif
										idx_forcef0 = ((x1 * forcef_local_size + y1) * forcef_local_size + z1) * 3;
										forcef[x][y][z][0] -= forcef_local[idx_forcef0];
										forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
										forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];
									}
								}
							}
							/*
							if ((step % 50 == 0))
							{
								printf("me_x=%d me_y=%d xc_key=%f %f %f, NX=%d, NY=%d",me_x,me_y,xc_key[0], xc_key[1], xc_key[2], Nx, Ny);
								printf("true_xc_key = %f %f\n",(xc[k][0] + 1 - me_x * Nx),(xc[k][1] + 1 - me_y * Ny));
								printf("Rank:%d\t%d_th particle index=%d\tKey=%d\txc=%f\t%f\t%f\tF=%f\t%f\t%f\tT=%f\t%f\t%f\n", me, k + 1, kk, KeyProcess[kk],xc[k][0], xc[k][1], xc[k][2], F_IBM[k][0], F_IBM[k][1], F_IBM[k][2],T_IBM[k][0], T_IBM[k][1], T_IBM[k][2]);
								printf("forcef_local xmin=%d xmax=%d ymin=%d ymax=%d zmin=%d zmax=%d\n",forcef_local_xmin,forcef_local_xmax,forcef_local_ymin,forcef_local_ymax,forcef_local_zmin,forcef_local_zmax);
								printf("temp variable idx_forcef0=%d idx_local0=%d idx_u0=%d\n",idx_forcef0,idx_local0,idx_u0);
								printf("%f\t%f\t%f\t%f\t%f\t%f\n",xx_Local,xxn_Local,yy_Local,yyn_Local,zz_Local,zzn_Local);
								printf("%d\t%d\t%d\t%d\t%d\t%d\n",xx_Local1,xxn_Local1,yy_Local1,yyn_Local1,zz_Local1,zzn_Local1);
								printf("%d\t%d\t%d\t%d\t%d\t%d\n",xx_Local2,xxn_Local2,yy_Local2,yyn_Local2,zz_Local2,zzn_Local2);
							}*/
						}
						else
						{
//#pragma omp critical
							{
								for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
								{
									x1 = x - forcef_local_xmin;
									for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
									{
										y1 = y - forcef_local_ymin;
										for (z1 = 0; z1 < forcef_local_size; z1++)
										{
											z = z1 + forcef_local_zmin;
#if (periodic_z == 1)
												if (z <= 1)
												{
													z = z + Nz;
												}
												else if (z >= Nz2)
												{
													z = z - Nz;
												}
#endif
#if (periodic_z == 0)
												if (z >= Nz2 || z <= 1)
													continue;
#endif
#if (periodic_x == 0)
												if (me % NPROCx == 0)
												{
													if (x <= 1)
														continue;
												}
												if ((me + 1) % NPROCx == 0)
												{
													if (x >= Nx2)
														continue;
												}
#endif
#if (periodic_y == 0)
												if (me < NPROCx)
												{
													if (y <= 1)
														continue;
												}
												if (me >= (NPROCx * (NPROCy - 1)))
												{
													if (y >= Ny2)
														continue;
												}
#endif
											idx_forcef0 = ((x1 * forcef_local_size + y1) * forcef_local_size + z1) * 3;
#pragma omp atomic
											forcef[x][y][z][0] -= forcef_local[idx_forcef0];
#pragma omp atomic
											forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
#pragma omp atomic
											forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];
										}
									}
								}
							}
						}
					}
				}
			}//end paralle for
#pragma omp barrier

#if (NF >1)
			if (iter_k < NF - 1)
			{
				for (kk = 0; kk < np_local; kk++)
				{
					if (num_p[kk] >= 0)
					{
						k = num_p[kk];
						xc_key_temp[0] = xc[k][0];
						xc_key_temp[1] = xc[k][1];
						xc_key_temp[2] = xc[k][2];
						xc_key[0] = xc_key_temp[0] + key_offsetx;
						xc_key[1] = xc_key_temp[1] + key_offsety;
						xc_key[2] = xc_key_temp[2] + 1.0;

						forcef_local_xmin = (int)((0. > floor(xc_key[0] - rb - 1.5)) ? 0 : floor(xc_key[0] - rb - 1.5));
						forcef_local_xmax = (int)((Nx3 < ceil(xc_key[0] + rb + 1.5)) ? Nx3 : ceil(xc_key[0] + rb + 1.5));
						forcef_local_ymin = (int)((0. > floor(xc_key[1] - rb - 1.5)) ? 0 : floor(xc_key[1] - rb - 1.5));
						forcef_local_ymax = (int)((Ny3 < ceil(xc_key[1] + rb + 1.5)) ? Ny3 : ceil(xc_key[1] + rb + 1.5));
						forcef_local_zmin = (int)(floor(xc_key[2] - rb - 1.5));
						forcef_local_zmax = (int)(ceil(xc_key[2] + rb + 1.5));
#pragma omp for schedule(static) collapse(3) private(x,y,z,z1,i)
						for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
						{
							for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
							{
								for (z = forcef_local_zmin; z <= forcef_local_zmax; z++)
								{
									z1 = z;
									if (periodic_z == 1)
									{
										if (z1 <= 1)
											z1 += Nz;
										if (z1 >= Nz2)
											z1 -= Nz;
									}
									for (i = 0; i < 3; i++)
									{
										fluidu_temp[x][y][z1][i] = u[x][y][z1][i] + forcef[x][y][z1][i] * dt / (2 * rhof);
									}

								}
							}
						}

					}
				}
			}
#pragma omp barrier

#pragma omp master
			{
				MPI_Sendrecv(&fluidu_temp[Nx][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, right_ex, 100,
					&fluidu_temp[0][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, left_ex, 100, MPI_COMM_WORLD, &status);
				MPI_Sendrecv(&fluidu_temp[2][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, left_ex, 120,
					&fluidu_temp[Nx2][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, right_ex, 120, MPI_COMM_WORLD, &status);
			}
#pragma omp barrier
#pragma omp for collapse(2) private(x, z, j)
				for (x = 0; x <= Nx3; x++)
				{
					for (z = 0; z <= Nz3; z++)
					{
						for (j = 0; j < 3; j++)
						{
							fluidu_temp_ex[2][x][z][j] = fluidu_temp[x][2][z][j];
							fluidu_temp_ex[3][x][z][j] = fluidu_temp[x][3][z][j];
							fluidu_temp_ex[4][x][z][j] = fluidu_temp[x][Ny][z][j];
							fluidu_temp_ex[5][x][z][j] = fluidu_temp[x][Ny1][z][j];
						}
					}
				}
#pragma omp barrier
#pragma omp master
				{
					MPI_Sendrecv(&fluidu_temp_ex[4][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, up_ex, 140,
						&fluidu_temp_ex[0][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, low_ex, 140, MPI_COMM_WORLD, &status);
					MPI_Sendrecv(&fluidu_temp_ex[2][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, low_ex, 160,
						&fluidu_temp_ex[6][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, up_ex, 160, MPI_COMM_WORLD, &status);
				}
#pragma omp barrier
#pragma omp for collapse(2) private(x, z, j)
				for (x = 0; x <= Nx3; x++)
				{
					for (z = 0; z <= Nz3; z++)
					{
						for (j = 0; j < 3; j++)
						{
							if (low_ex != MPI_PROC_NULL)
							{
								fluidu_temp[x][0][z][j] = fluidu_temp_ex[0][x][z][j];
								fluidu_temp[x][1][z][j] = fluidu_temp_ex[1][x][z][j];
							}
							if (up_ex != MPI_PROC_NULL)
							{
								fluidu_temp[x][Ny2][z][j] = fluidu_temp_ex[6][x][z][j];
								fluidu_temp[x][Ny3][z][j] = fluidu_temp_ex[7][x][z][j];
							}
						}
					}
				}

				if (periodic_z == 1)
				{
#pragma omp for collapse(2) private(x, y, j)
					for (x = 0; x <= Nx3; x++)
					{
						for (y = 0; y <= Ny3; y++)
						{
							for (j = 0; j < 3; j++)
							{
								fluidu_temp[x][y][0][j] = fluidu_temp[x][y][Nz][j];
								fluidu_temp[x][y][1][j] = fluidu_temp[x][y][Nz1][j];
								fluidu_temp[x][y][Nz2][j] = fluidu_temp[x][y][2][j];
								fluidu_temp[x][y][Nz3][j] = fluidu_temp[x][y][3][j];
							}
						}
					}
				}
#pragma omp barrier
#endif 
		}//end NF iter
		free(uf);
		free(up);
		free(xb_Local);
		free(yb_Local);
		free(zb_Local);
		free(forcef_local);

#pragma omp for schedule(static) private(k, kk, i)
		for (kk = 0; kk < np_local; kk++)
		{
			if (num_p[kk] >= 0)
			{
				k = num_p[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] *= LP_dr * (dx * dx * dx);
					T_IBM[k][i] *= LP_dr * (dx * dx * dx);
				}
			}
		}
	}//end openmp parallel
	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_less_forcef_local3_MDF(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, x1, y1, z1;
	int xx, yy, zz, ii;
	double lx, ly, lz;
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, forcef_local_size;
	int forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax;
	int xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2;
	double f_coeffi = 2 * rhof / dt;

	int flat_size1 = ns * nt1 * 3;
	int flat_size2 = ns * nt1;
	int idx_u0, idx_local0, idx_forcef0;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;
	const double key_offsetx = 1. - (double)(me_x) * (double)(Nx);
	const double key_offsety = 1. - (double)(me_y) * (double)(Ny);

	IB_interpo_list_size = 3; //4 for delta_3p func 
	forcef_local_size = int(2 * rb) + 6; //plus 4 for delta_3p func
	//I dont know why it need eextra plus 6 for delta_3p func, but it works

	//for utemp exchange
	int low_ex, up_ex, right_ex, left_ex;
	double fluidu_temp_ex[8][Nx4][Nz4][3];

	if (periodic_x == 1)
	{
		if (me % NPROCx == 0)
			left_ex = me + NPROCx - 1;
		else
			left_ex = me - 1;
		if ((me + 1) % NPROCx == 0)
			right_ex = me - NPROCx + 1;
		else
			right_ex = me + 1;
	}
	else if (periodic_x == 0)
	{
		if (me % NPROCx == 0)
			left_ex = MPI_PROC_NULL;
		else
			left_ex = me - 1;
		if ((me + 1) % NPROCx == 0)
			right_ex = MPI_PROC_NULL;
		else
			right_ex = me + 1;
	}

	if (periodic_y == 1)
	{
		if (me < NPROCx)
			low_ex = me + NPROCx * (NPROCy - 1);
		else
			low_ex = me - NPROCx;
		if (me >= (NPROCx * (NPROCy - 1)))
			up_ex = me - NPROCx * (NPROCy - 1);
		else
			up_ex = me + NPROCx;
	}
	else if (periodic_y == 0)
	{
		if (me < NPROCx)
			low_ex = MPI_PROC_NULL;
		else
			low_ex = me - NPROCx;
		if (me >= (NPROCx * (NPROCy - 1)))
			up_ex = MPI_PROC_NULL;
		else
			up_ex = me + NPROCx;
	}

/* MDF 速度更新阶段：所有线程共同处理同一颗粒，完成后再进入下一颗粒。
 * private 只隔离颗粒号和范围临时量，不改变颗粒更新顺序或工作共享方式。 */
#pragma omp parallel private(k,kk,\
				forcef_local_xmin,forcef_local_xmax,forcef_local_ymin,forcef_local_ymax,forcef_local_zmin,forcef_local_zmax)
	{
		double xc_key[3];
		volatile double xc_key_temp[3];
		double force_IB[3];
		double* uf = (double*)malloc(flat_size1 * sizeof(double));
		double* up = (double*)malloc(flat_size1 * sizeof(double));
		double* xb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* yb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* zb_Local = (double*)malloc(flat_size2 * sizeof(double));
		int* zb1 = (int*)malloc(flat_size2 * sizeof(int));

		int num_threads = omp_get_num_threads();
		bool is_single = (num_threads == 1);
		int thread_id = omp_get_thread_num();

#if IBM_DEBUG_OUTPUT
		char thread_filename[256];
		FILE* thread_debug_file = NULL;
#endif

		int iter_k;

		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];
		//double forcef_local[forcef_local_size][forcef_local_size][forcef_local_size][3];
		int forcef_local_total_size = forcef_local_size * forcef_local_size * forcef_local_size * 3;
		double* forcef_local = (double*)malloc(forcef_local_total_size * sizeof(double));
		/* 每线程一份多字掩码，各轮、各颗粒复用；支持边长超过 64。 */
		size_t zmask_words_per_row = ((size_t)forcef_local_size + 63) / 64;
		size_t zmask_total_words = (size_t)forcef_local_size * forcef_local_size * zmask_words_per_row;
		uint64_t* zmask = (uint64_t*)malloc(zmask_total_words * sizeof(uint64_t));

#pragma omp for schedule(static) private(k, kk, i)
		for (kk = 0; kk < np_local; kk++)
		{
			if (num_p[kk] >= 0)
			{
				k = num_p[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}
			}
		}
		for (iter_k = 0; iter_k < NF; iter_k++)
		{
#pragma omp for schedule(dynamic,10) private(i,j,k,kk,x,y,z,x1,y1,z1,lx,ly,lz,IB_interpo,temp,\
											xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox, IB_interpoy, IB_interpoz,\
											forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax,\
											xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2,\
											idx_u0, idx_local0, idx_forcef0)
			for (kk = 0; kk < np_local; kk++)
			{
				if (num_p[kk] >= 0)
				{
					k = num_p[kk];
					if ((xc[k][0] > xx4) && (xc[k][0] < xxn4) && (xc[k][1] > yy4) && (xc[k][1] < yyn4))
					{
						xc_key_temp[0] = xc[k][0];
						xc_key_temp[1] = xc[k][1];
						xc_key_temp[2] = xc[k][2];
						xc_key[0] = xc_key_temp[0] + key_offsetx;
						xc_key[1] = xc_key_temp[1] + key_offsety;
						xc_key[2] = xc_key_temp[2] + 1.0;

						forcef_local_xmin = int(xc_key[0] - rb - 1.5) - 1;
						forcef_local_ymin = int(xc_key[1] - rb - 1.5) - 1;
						forcef_local_zmin = int(xc_key[2] - rb - 1.5) - 1;
						forcef_local_xmax = forcef_local_xmin + forcef_local_size - 1;
						forcef_local_ymax = forcef_local_ymin + forcef_local_size - 1;
						forcef_local_zmax = forcef_local_zmin + forcef_local_size - 1;

						forcef_local_xmin = (forcef_local_xmin < 0) ? 0 : forcef_local_xmin;
						forcef_local_ymin = (forcef_local_ymin < 0) ? 0 : forcef_local_ymin;
						forcef_local_xmax = (forcef_local_xmax > Nx3) ? Nx3 : forcef_local_xmax;
						forcef_local_ymax = (forcef_local_ymax > Ny3) ? Ny3 : forcef_local_ymax;

						memset(forcef_local, 0, forcef_local_total_size * sizeof(double));
						memset(zb1, 0, flat_size2 * sizeof(int));
						memset(zmask, 0, zmask_total_words * sizeof(uint64_t));

						for (i = 0; i < ns; i++)
							for (j = 0; j < nt[i]; j++)
							{
								idx_local0 = i * nt1 + j;
								xb_Local[idx_local0] = xc_key[0] + xb[i][j];
								yb_Local[idx_local0] = xc_key[1] + yb[i][j];
								zb_Local[idx_local0] = xc_key[2] + zb[i][j];
								/* u 与 fluidu_temp 共用该周期映射，内层不再逐节点判断。 */
								if (periodic_z == 1)
								{
									if (zb_Local[idx_local0] <= 1)   zb1[idx_local0] = Nz;
									if (zb_Local[idx_local0] >= Nz2)  zb1[idx_local0] = -Nz;
								}

								if (xb_Local[idx_local0] >= 0.5 && xb_Local[idx_local0] <= (Nx + 2.5) && yb_Local[idx_local0] >= 0.5 && yb_Local[idx_local0] <= (Ny + 2.5))
								{
									idx_u0 = (i * nt1 + j) * 3;
									up[idx_u0] = uc[k][0] + (wc[k][2] * (-yb[i][j]) + wc[k][1] * (zb[i][j])) * dx;
									up[idx_u0 + 1] = uc[k][1] + (wc[k][2] * (xb[i][j]) + wc[k][0] * (-zb[i][j])) * dx;
									up[idx_u0 + 2] = uc[k][2] + (wc[k][0] * (yb[i][j]) + wc[k][1] * (-xb[i][j])) * dx;
									uf[idx_u0] = 0.0; uf[idx_u0 + 1] = 0.0; uf[idx_u0 + 2] = 0.0;
									
									xx_Local = (xb_Local[idx_local0] - 1.5);
									yy_Local = (yb_Local[idx_local0] - 1.5);
									zz_Local = (zb_Local[idx_local0] + zb1[idx_local0] - 1.5);
									xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = xx_Local1+2;
									yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = yy_Local1+2;
									zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = zz_Local1+2;

									/* 各方向三个权重共用一次平方根。 */
									lx = xb_Local[idx_local0] - xx_Local1 - 1;
									delta_3p_less(lx, IB_interpo_listx);
									ly = yb_Local[idx_local0] - yy_Local1 - 1;
									delta_3p_less(ly, IB_interpo_listy);
									lz = zb_Local[idx_local0] - zz_Local1 - 1 + zb1[idx_local0];
									delta_3p_less(lz, IB_interpo_listz);
									if (iter_k > 0)
									{
										for (x = xx_Local1; x <= xxn_Local1; x++)
										{
											IB_interpox = IB_interpo_listx[x - xx_Local1];
											for (y = yy_Local1; y <= yyn_Local1; y++)
											{
												IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
												for (z = zz_Local1; z <= zzn_Local1; z++)
												{
													IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
													IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

													z1 = z;

													uf[idx_u0] += fluidu_temp[x][y][z1][0] * IB_interpoz;
													uf[idx_u0 + 1] += fluidu_temp[x][y][z1][1] * IB_interpoz;
													uf[idx_u0 + 2] += fluidu_temp[x][y][z1][2] * IB_interpoz;
												}
											}
										}
									}
									else
									{
										for (x = xx_Local1; x <= xxn_Local1; x++)
										{
											IB_interpox = IB_interpo_listx[x - xx_Local1];
											for (y = yy_Local1; y <= yyn_Local1; y++)
											{
												IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
												for (z = zz_Local1; z <= zzn_Local1; z++)
												{
													IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
													IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

													z1 = z;

													uf[idx_u0] += u[x][y][z1][0] * IB_interpoz;
													uf[idx_u0 + 1] += u[x][y][z1][1] * IB_interpoz;
													uf[idx_u0 + 2] += u[x][y][z1][2] * IB_interpoz;
												}
											}
										}
									}
									force_IB[0] = f_coeffi * (uf[idx_u0] - up[idx_u0]) * LP_area[i];
									force_IB[1] = f_coeffi * (uf[idx_u0 + 1] - up[idx_u0 + 1]) * LP_area[i];
									force_IB[2] = f_coeffi * (uf[idx_u0 + 2] - up[idx_u0 + 2]) * LP_area[i];

#if IBM_DEBUG_OUTPUT
									sprintf(thread_filename, "IBM_lag_step_%03d-%03d_debug_poly_rank_%d_thread_%d.log",
										(step / 100) * 100, (step / 100) * 100 + 99, me, thread_id);
									thread_debug_file = fopen(thread_filename, "a");
									if (thread_debug_file != NULL) {
										fprintf(thread_debug_file,
											"Step=%d, NF=%d, Par_k=%d, LagPt=(%d,%d), "
											"Coord=(%.6f,%.6f,%.6f), "
											"uf=(%.6f,%.6f,%.6f), "
											"up=(%.6f,%.6f,%.6f), "
											"f_IB=(%.6f,%.6f,%.6f)\n",
											step, iter_k, k, i, j,
											(xb_Local[idx_local0] - key_offsetx), (yb_Local[idx_local0] - key_offsety), (zb_Local[idx_local0] - 1.),
											uf[idx_u0], uf[idx_u0 + 1], uf[idx_u0 + 2],
											up[idx_u0], up[idx_u0 + 1], up[idx_u0 + 2],
											force_IB[0], force_IB[1], force_IB[2]);

										fclose(thread_debug_file); // 写完立即关闭，避免句柄泄漏
									}
#endif

									xx_Local2 = xx_Local1 - forcef_local_xmin;
									xxn_Local2 = xxn_Local1 - forcef_local_xmin;
									yy_Local2 = yy_Local1 - forcef_local_ymin;
									yyn_Local2 = yyn_Local1 - forcef_local_ymin;
									zz_Local2 = zz_Local1 - forcef_local_zmin - zb1[idx_local0];
									zzn_Local2 = zzn_Local1 - forcef_local_zmin - zb1[idx_local0];

									size_t zmask_word;
									unsigned int zmask_bit;
									uint64_t zmask_low, zmask_high;
									zmask_word = (size_t)zz_Local2 >> 6;
									zmask_bit = (unsigned int)zz_Local2 & 63u;
									zmask_low = (uint64_t)7 << zmask_bit;
									zmask_high = 0;
									if (zmask_bit >= 62u)
										zmask_high = (uint64_t)7 >> (64u - zmask_bit);

									for (x = xx_Local2; x <= xxn_Local2; x++)
										for (y = yy_Local2; y <= yyn_Local2; y++)
										{
											uint64_t* zmask_row;
											/* 每行一次标记三个 z 节点，起始位为 62/63 时同时标记下一字。 */
											zmask_row = zmask + ((size_t)x * forcef_local_size + y) * zmask_words_per_row;
											zmask_row[zmask_word] |= zmask_low;
											if (zmask_high != 0)
												zmask_row[zmask_word + 1] |= zmask_high;

											for (z = zz_Local2; z <= zzn_Local2; z++)
											{
												IB_interpo = IB_interpo_list[x - xx_Local2][y - yy_Local2][z - zz_Local2] * LP_dr;
												idx_forcef0 = ((x * forcef_local_size + y) * forcef_local_size + z) * 3;

												forcef_local[idx_forcef0] += force_IB[0] * IB_interpo;
												forcef_local[idx_forcef0 + 1] += force_IB[1] * IB_interpo;
												forcef_local[idx_forcef0 + 2] += force_IB[2] * IB_interpo;
											}
										}

									if (xb_Local[idx_local0] >= 1.5 && xb_Local[idx_local0] <= (Nx + 1.5) && yb_Local[idx_local0] >= 1.5 && yb_Local[idx_local0] <= (Ny + 1.5))
									{
										F_IBM[k][0] += force_IB[0];
										F_IBM[k][1] += force_IB[1];
										F_IBM[k][2] += force_IB[2];
										T_IBM[k][0] += (force_IB[2] * yb[i][j] - force_IB[1] * zb[i][j]) * dx;
										T_IBM[k][1] += (force_IB[0] * zb[i][j] - force_IB[2] * xb[i][j]) * dx;
										T_IBM[k][2] += (force_IB[1] * xb[i][j] - force_IB[0] * yb[i][j]) * dx;
									}
								}
							}
						size_t active_z_words;	
						unsigned int active_z_tail;
						uint64_t last_z_mask;

						active_z_words = ((size_t)forcef_local_size + 63) / 64;
						active_z_tail = (unsigned int)forcef_local_size & 63u;
						last_z_mask = ~(uint64_t)0;

						/* 有效长度正好是 64 的整数倍时保留整字，避免左移 64 位。 */
						if (active_z_tail != 0)
							last_z_mask = ((uint64_t)1 << active_z_tail) - 1;

						if (is_single)
						{
							for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
							{
								x1 = x - forcef_local_xmin;
								for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
								{
									size_t local_row, force_row, mask_word_id;
									const uint64_t* zmask_row;

									y1 = y - forcef_local_ymin;
									local_row = (size_t)x1 * forcef_local_size + y1;
									force_row = local_row * forcef_local_size * 3;
									zmask_row = zmask + local_row * zmask_words_per_row;

									/* 只扫描当前颗粒使用的字；数组寻址仍用最大容量步长。 */
									for (mask_word_id = 0; mask_word_id < active_z_words; mask_word_id++)
									{
										uint64_t mask_word;
										mask_word = zmask_row[mask_word_id];
										if (mask_word_id + 1 == active_z_words)
											mask_word &= last_z_mask;

										while (mask_word != 0)
										{
											unsigned int bit;

											/* 输入非零时才调用 ctzll；逐次去掉最低一个置位。 */
											bit = (unsigned int)__builtin_ctzll((unsigned long long)mask_word);
											mask_word &= mask_word - 1;
											z1 = (int)(mask_word_id * 64 + bit);

											/* 标记属于未映射的局部节点，恢复全局 z 后再处理周期性。 */
											z = z1 + forcef_local_zmin;
											if (periodic_z == 1)
											{
												if (z <= 1)
													z += Nz;
												else if (z >= Nz2)
													z -= Nz;
											}
#if (periodic_z == 0)
											if (z >= Nz2 || z <= 1)
												continue;
#endif
#if (periodic_x == 0)
											if (me % NPROCx == 0)
											{
												if (x <= 1)
													continue;
											}
											if ((me + 1) % NPROCx == 0)
											{
												if (x >= Nx2)
													continue;
											}
#endif
#if (periodic_y == 0)
											if (me < NPROCx)
											{
												if (y <= 1)
													continue;
											}
											if (me >= (NPROCx * (NPROCy - 1)))
											{
												if (y >= Ny2)
													continue;
											}
#endif
											idx_forcef0 = force_row + (size_t)z1 * 3;
											forcef[x][y][z][0] -= forcef_local[idx_forcef0];
											forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
											forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];
										}
									}
								}
							}
						}
						else
						{
							{
								for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
								{
									x1 = x - forcef_local_xmin;
									for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
									{
										size_t local_row, force_row, mask_word_id;
										const uint64_t* zmask_row;

										y1 = y - forcef_local_ymin;
										local_row = (size_t)x1 * forcef_local_size + y1;
										force_row = local_row * forcef_local_size * 3;
										zmask_row = zmask + local_row * zmask_words_per_row;

										/* 只扫描当前颗粒使用的字；数组寻址仍用最大容量步长。 */
										for (mask_word_id = 0; mask_word_id < active_z_words; mask_word_id++)
										{
											uint64_t mask_word;
											mask_word = zmask_row[mask_word_id];
											if (mask_word_id + 1 == active_z_words)
												mask_word &= last_z_mask;

											while (mask_word != 0)
											{
												unsigned int bit;

												/* 输入非零时才调用 ctzll；逐次去掉最低一个置位。 */
												bit = (unsigned int)__builtin_ctzll((unsigned long long)mask_word);
												mask_word &= mask_word - 1;
												z1 = (int)(mask_word_id * 64 + bit);

												/* 标记属于未映射的局部节点，恢复全局 z 后再处理周期性。 */
												z = z1 + forcef_local_zmin;
												if (periodic_z == 1)
												{
													if (z <= 1)
														z += Nz;
													else if (z >= Nz2)
														z -= Nz;
												}
#if (periodic_z == 0)
												if (z >= Nz2 || z <= 1)
													continue;
#endif
#if (periodic_x == 0)
												if (me % NPROCx == 0)
												{
													if (x <= 1)
														continue;
												}
												if ((me + 1) % NPROCx == 0)
												{
													if (x >= Nx2)
														continue;
												}
#endif
#if (periodic_y == 0)
												if (me < NPROCx)
												{
													if (y <= 1)
														continue;
												}
												if (me >= (NPROCx * (NPROCy - 1)))
												{
													if (y >= Ny2)
														continue;
												}
#endif
												idx_forcef0 = force_row + (size_t)z1 * 3;
#pragma omp atomic
												forcef[x][y][z][0] -= forcef_local[idx_forcef0];
#pragma omp atomic
												forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
#pragma omp atomic
												forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];
											}
										}
									}
								}
							}
						}
					}
				}
			}//end paralle for
#pragma omp barrier

#if (NF >1)
			if (iter_k < NF - 1)
			{
				for (kk = 0; kk < np_local; kk++)
				{
					if (num_p[kk] >= 0)
					{
						k = num_p[kk];
						xc_key_temp[0] = xc[k][0];
						xc_key_temp[1] = xc[k][1];
						xc_key_temp[2] = xc[k][2];
						xc_key[0] = xc_key_temp[0] + key_offsetx;
						xc_key[1] = xc_key_temp[1] + key_offsety;
						xc_key[2] = xc_key_temp[2] + 1.0;

						forcef_local_xmin = (int)((0. > floor(xc_key[0] - rb - 1.5)) ? 0 : floor(xc_key[0] - rb - 1.5));
						forcef_local_xmax = (int)((Nx3 < ceil(xc_key[0] + rb + 1.5)) ? Nx3 : ceil(xc_key[0] + rb + 1.5));
						forcef_local_ymin = (int)((0. > floor(xc_key[1] - rb - 1.5)) ? 0 : floor(xc_key[1] - rb - 1.5));
						forcef_local_ymax = (int)((Ny3 < ceil(xc_key[1] + rb + 1.5)) ? Ny3 : ceil(xc_key[1] + rb + 1.5));
						forcef_local_zmin = (int)(floor(xc_key[2] - rb - 1.5));
						forcef_local_zmax = (int)(ceil(xc_key[2] + rb + 1.5));
#pragma omp for schedule(static) collapse(3) private(x,y,z,z1,i)
						for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
						{
							for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
							{
								for (z = forcef_local_zmin; z <= forcef_local_zmax; z++)
								{
									z1 = z;
									if (periodic_z == 1)
									{
										if (z1 <= 1)
											z1 += Nz;
										if (z1 >= Nz2)
											z1 -= Nz;
									}
									for (i = 0; i < 3; i++)
									{
										fluidu_temp[x][y][z1][i] = u[x][y][z1][i] + forcef[x][y][z1][i] * dt / (2 * rhof);
									}

								}
							}
						}

					}
				}
			}
#pragma omp barrier

#pragma omp master
			{
				MPI_Sendrecv(&fluidu_temp[Nx][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, right_ex, 100,
					&fluidu_temp[0][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, left_ex, 100, MPI_COMM_WORLD, &status);
				MPI_Sendrecv(&fluidu_temp[2][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, left_ex, 120,
					&fluidu_temp[Nx2][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, right_ex, 120, MPI_COMM_WORLD, &status);
			}
#pragma omp barrier
#pragma omp for collapse(2) private(x, z, j)
				for (x = 0; x <= Nx3; x++)
				{
					for (z = 0; z <= Nz3; z++)
					{
						for (j = 0; j < 3; j++)
						{
							fluidu_temp_ex[2][x][z][j] = fluidu_temp[x][2][z][j];
							fluidu_temp_ex[3][x][z][j] = fluidu_temp[x][3][z][j];
							fluidu_temp_ex[4][x][z][j] = fluidu_temp[x][Ny][z][j];
							fluidu_temp_ex[5][x][z][j] = fluidu_temp[x][Ny1][z][j];
						}
					}
				}
#pragma omp barrier
#pragma omp master
				{
					MPI_Sendrecv(&fluidu_temp_ex[4][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, up_ex, 140,
						&fluidu_temp_ex[0][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, low_ex, 140, MPI_COMM_WORLD, &status);
					MPI_Sendrecv(&fluidu_temp_ex[2][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, low_ex, 160,
						&fluidu_temp_ex[6][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, up_ex, 160, MPI_COMM_WORLD, &status);
				}
#pragma omp barrier
#pragma omp for collapse(2) private(x, z, j)
				for (x = 0; x <= Nx3; x++)
				{
					for (z = 0; z <= Nz3; z++)
					{
						for (j = 0; j < 3; j++)
						{
							if (low_ex != MPI_PROC_NULL)
							{
								fluidu_temp[x][0][z][j] = fluidu_temp_ex[0][x][z][j];
								fluidu_temp[x][1][z][j] = fluidu_temp_ex[1][x][z][j];
							}
							if (up_ex != MPI_PROC_NULL)
							{
								fluidu_temp[x][Ny2][z][j] = fluidu_temp_ex[6][x][z][j];
								fluidu_temp[x][Ny3][z][j] = fluidu_temp_ex[7][x][z][j];
							}
						}
					}
				}

				if (periodic_z == 1)
				{
#pragma omp for collapse(2) private(x, y, j)
					for (x = 0; x <= Nx3; x++)
					{
						for (y = 0; y <= Ny3; y++)
						{
							for (j = 0; j < 3; j++)
							{
								fluidu_temp[x][y][0][j] = fluidu_temp[x][y][Nz][j];
								fluidu_temp[x][y][1][j] = fluidu_temp[x][y][Nz1][j];
								fluidu_temp[x][y][Nz2][j] = fluidu_temp[x][y][2][j];
								fluidu_temp[x][y][Nz3][j] = fluidu_temp[x][y][3][j];
							}
						}
					}
				}
#pragma omp barrier
#endif 
		}//end NF iter
		free(uf);
		free(up);
		free(xb_Local);
		free(yb_Local);
		free(zb_Local);
		free(forcef_local);
		free(zb1);
		free(zmask);

#pragma omp for schedule(static) private(k, kk, i)
		for (kk = 0; kk < np_local; kk++)
		{
			if (num_p[kk] >= 0)
			{
				k = num_p[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] *= LP_dr * (dx * dx * dx);
					T_IBM[k][i] *= LP_dr * (dx * dx * dx);
				}
			}
		}
	}//end openmp parallel
	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_poly(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, z1;
	double lx, ly, lz;
	double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];  //���ϵ��������ٶȣ������ٶȣ�IB����ʱ����
	double xc_key[3];
	double force_IB[3];
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int ns_temp, rc_index;
	double rb_temp;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;

#pragma omp parallel
	{
		double xc_key[3];
		double force_IB[3];
		double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];

		int num_threads = omp_get_num_threads();
		bool is_single = (num_threads == 1);

	#pragma omp for schedule(dynamic,10) private(i,j,k,kk,x,y,z,z1,lx,ly,lz,IB_interpo,temp,\
												rc_index,rb_temp,ns_temp,xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local)
		for (kk = 0; kk < np_local_poly; kk++)
		{
			if (num_p_poly[kk] >= 0)
			{
				k = num_p_poly[kk];
				rc_index = rc_poly[k];
				rb_temp = rb_poly[rc_index];
				ns_temp = (int)(PI * rb_temp / 0.8 + 1);
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}

				if ((xc[k][0] > (me_x * Nx - rb_temp - 1.5)) && (xc[k][0] < ((me_x + 1) * Nx + rb_temp + 1.5)) && (xc[k][1] > (me_y * Ny - rb_temp - 1.5)) && (xc[k][1] < ((me_y + 1) * Ny + rb_temp + 1.5)))
				{
					xc_key[0] = xc[k][0] + 1 - me_x * Nx;
					xc_key[1] = xc[k][1] + 1 - me_y * Ny;
					xc_key[2] = xc[k][2] + 1;

					for (i = 0; i < ns_temp; i++)
						for (j = 0; j < nt_poly[rc_index][i]; j++)
						{
							xb_Local[i][j] = xc_key[0] + xb_poly[rc_index][i][j];
							yb_Local[i][j] = xc_key[1] + yb_poly[rc_index][i][j];
							zb_Local[i][j] = xc_key[2] + zb_poly[rc_index][i][j];

							if (xb_Local[i][j] >= 0.5 && xb_Local[i][j] <= (Nx + 2.5) && yb_Local[i][j] >= 0.5 && yb_Local[i][j] <= (Ny + 2.5))
							{
								up[i][j][0] = uc[k][0] + (wc[k][2] * (-yb_poly[rc_index][i][j]) + wc[k][1] * (zb_poly[rc_index][i][j])) * dx_poly;
								up[i][j][1] = uc[k][1] + (wc[k][2] * (xb_poly[rc_index][i][j]) + wc[k][0] * (-zb_poly[rc_index][i][j])) * dx_poly;
								up[i][j][2] = uc[k][2] + (wc[k][0] * (yb_poly[rc_index][i][j]) + wc[k][1] * (-xb_poly[rc_index][i][j])) * dx_poly;
								uf[i][j][0] = 0.0; uf[i][j][1] = 0.0; uf[i][j][2] = 0.0;

								xx_Local = (xb_Local[i][j] - 1.5);
								if (xx_Local <= 0)   xx_Local = 0;
								xxn_Local = (xb_Local[i][j] + 1.5);
								if (xxn_Local >= Nx3)   xxn_Local = Nx3;
								yy_Local = (yb_Local[i][j] - 1.5);
								if (yy_Local <= 0)   yy_Local = 0;
								yyn_Local = (yb_Local[i][j] + 1.5);
								if (yyn_Local >= Ny3)   yyn_Local = Ny3;
								zz_Local = (zb_Local[i][j] - 1.5);
								zzn_Local = (zb_Local[i][j] + 1.5);

								for (x = int(xx_Local); x <= int(xxn_Local); x++)
									for (y = int(yy_Local); y <= int(yyn_Local); y++)
										for (z = int(zz_Local); z <= int(zzn_Local); z++)
										{
											lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
											IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz);

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											uf[i][j][0] += u[x][y][z1][0] * IB_interpo;
											uf[i][j][1] += u[x][y][z1][1] * IB_interpo;
											uf[i][j][2] += u[x][y][z1][2] * IB_interpo;
										}
								force_IB[0] = 2 * rhof * (uf[i][j][0] - up[i][j][0]) / dt;
								force_IB[1] = 2 * rhof * (uf[i][j][1] - up[i][j][1]) / dt;
								force_IB[2] = 2 * rhof * (uf[i][j][2] - up[i][j][2]) / dt;

								for (x = int(xx_Local); x <= int(xxn_Local); x++)
									for (y = int(yy_Local); y <= int(yyn_Local); y++)
										for (z = int(zz_Local); z <= int(zzn_Local); z++)
										{
											lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
											IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz) * (LP_dr * LP_area_poly[rc_index][i]);

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											if (is_single)
											{
												forcef[x][y][z1][0] -= force_IB[0] * IB_interpo;
												forcef[x][y][z1][1] -= force_IB[1] * IB_interpo;
												forcef[x][y][z1][2] -= force_IB[2] * IB_interpo;
											}
											else
											{
#pragma omp atomic
												forcef[x][y][z1][0] -= force_IB[0] * IB_interpo;
#pragma omp atomic
												forcef[x][y][z1][1] -= force_IB[1] * IB_interpo;
#pragma omp atomic
												forcef[x][y][z1][2] -= force_IB[2] * IB_interpo;
											}
										}

								if (xb_Local[i][j] >= 1.5 && xb_Local[i][j] <= (Nx + 1.5) && yb_Local[i][j] >= 1.5 && yb_Local[i][j] <= (Ny + 1.5))
								{
									F_IBM[k][0] += force_IB[0] * LP_area_poly[rc_index][i];
									F_IBM[k][1] += force_IB[1] * LP_area_poly[rc_index][i];
									F_IBM[k][2] += force_IB[2] * LP_area_poly[rc_index][i];
									T_IBM[k][0] += (force_IB[2] * yb_poly[rc_index][i][j] - force_IB[1] * zb_poly[rc_index][i][j]) * dx_poly * LP_area_poly[rc_index][i];
									T_IBM[k][1] += (force_IB[0] * zb_poly[rc_index][i][j] - force_IB[2] * xb_poly[rc_index][i][j]) * dx_poly * LP_area_poly[rc_index][i];
									T_IBM[k][2] += (force_IB[1] * xb_poly[rc_index][i][j] - force_IB[0] * yb_poly[rc_index][i][j]) * dx_poly * LP_area_poly[rc_index][i];
								}
								//tiaoshixinxi
								//if (sqrt(force_IB[0] * force_IB[0] + force_IB[1] * force_IB[1] + force_IB[1] * force_IB[1])*LP_area[i] * LP_dr * dx*dx*dx > 1.0)
								//{
								//	printf("Rank_ID=%d\t time=%d\tpar_num=%d\tlagrangi position = %f\t%f\t%f\t%f\t%f\t%f\n", me, step, k + 1, xb[i][j], yb[i][j], zb[i][j], force_IB[0], force_IB[1], force_IB[2]);
								//}
							}
						}


					temp = LP_dr * (dx_poly * dx_poly * dx_poly);
					for (i = 0; i < 3; i++)
					{
						F_IBM[k][i] *= temp;	T_IBM[k][i] *= temp;
					}
				}
				//printf("Rank:%d\t%d_th particle \tindex=%d\trc=%d\txc=%f\t%f\t%f\tF=%f\t%f\t%f\n", me, k + 1,kk, rc_index,xc[k][0], xc[k][1], xc[k][2], F_IBM[k][0], F_IBM[k][1], F_IBM[k][2]);
			}
		}
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_poly_less(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, z1;
	double lx, ly, lz;
	double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];  //���ϵ��������ٶȣ������ٶȣ�IB����ʱ����
	double xc_key[3];
	double force_IB[3];
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int ns_temp, rc_index;
	double rb_temp;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox,IB_interpoy,IB_interpoz;
	int IB_interpo_list_size, local_forcef_size;
	double f_coeffi = 2 * rhof / dt;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;

	IB_interpo_list_size = 4;//4 for delta_3p func
	//local_forcef_size = int(2 * rb) + 4;//plus 4 for delta_3p func

#pragma omp parallel
	{
		double xc_key[3];
		double force_IB[3];
		double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];

		int num_threads = omp_get_num_threads();
		bool is_single = (num_threads == 1);

		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];

#pragma omp for schedule(dynamic,10) private(i,j,k,kk,x,y,z,z1,lx,ly,lz,IB_interpo,temp,\
												rc_index,rb_temp,ns_temp,xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox,IB_interpoy,IB_interpoz)
		for (kk = 0; kk < np_local_poly; kk++)
		{
			if (num_p_poly[kk] >= 0)
			{
				k = num_p_poly[kk];
				rc_index = rc_poly[k];
				rb_temp = rb_poly[rc_index];
				ns_temp = (int)(PI * rb_temp / 0.8 + 1);
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}

				if ((xc[k][0] > (me_x * Nx - rb_temp - 1.5)) && (xc[k][0] < ((me_x + 1) * Nx + rb_temp + 1.5)) && (xc[k][1] > (me_y * Ny - rb_temp - 1.5)) && (xc[k][1] < ((me_y + 1) * Ny + rb_temp + 1.5)))
				{
					xc_key[0] = xc[k][0] + 1 - me_x * Nx;
					xc_key[1] = xc[k][1] + 1 - me_y * Ny;
					xc_key[2] = xc[k][2] + 1;

					for (i = 0; i < ns_temp; i++)
						for (j = 0; j < nt_poly[rc_index][i]; j++)
						{
							xb_Local[i][j] = xc_key[0] + xb_poly[rc_index][i][j];
							yb_Local[i][j] = xc_key[1] + yb_poly[rc_index][i][j];
							zb_Local[i][j] = xc_key[2] + zb_poly[rc_index][i][j];

							if (xb_Local[i][j] >= 0.5 && xb_Local[i][j] <= (Nx + 2.5) && yb_Local[i][j] >= 0.5 && yb_Local[i][j] <= (Ny + 2.5))
							{
								up[i][j][0] = uc[k][0] + (wc[k][2] * (-yb_poly[rc_index][i][j]) + wc[k][1] * (zb_poly[rc_index][i][j])) * dx_poly;
								up[i][j][1] = uc[k][1] + (wc[k][2] * (xb_poly[rc_index][i][j]) + wc[k][0] * (-zb_poly[rc_index][i][j])) * dx_poly;
								up[i][j][2] = uc[k][2] + (wc[k][0] * (yb_poly[rc_index][i][j]) + wc[k][1] * (-xb_poly[rc_index][i][j])) * dx_poly;
								uf[i][j][0] = 0.0; uf[i][j][1] = 0.0; uf[i][j][2] = 0.0;

								xx_Local = (xb_Local[i][j] - 1.5);
								if (xx_Local <= 0)   xx_Local = 0;
								xxn_Local = (xb_Local[i][j] + 1.5);
								if (xxn_Local >= Nx3)   xxn_Local = Nx3;
								yy_Local = (yb_Local[i][j] - 1.5);
								if (yy_Local <= 0)   yy_Local = 0;
								yyn_Local = (yb_Local[i][j] + 1.5);
								if (yyn_Local >= Ny3)   yyn_Local = Ny3;
								zz_Local = (zb_Local[i][j] - 1.5);
								zzn_Local = (zb_Local[i][j] + 1.5);

								xx_Local1 = int(xx_Local); xxn_Local1 = int(xxn_Local);
								yy_Local1 = int(yy_Local); yyn_Local1 = int(yyn_Local);
								zz_Local1 = int(zz_Local); zzn_Local1 = int(zzn_Local);

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									lx = x - xb_Local[i][j];
									IB_interpo_listx[x - xx_Local1] = delta_3p(lx);
								}
								for (y = yy_Local1; y <= yyn_Local1; y++)
								{
									ly = y - yb_Local[i][j];
									IB_interpo_listy[y - yy_Local1] = delta_3p(ly);
								}
								for (z = zz_Local1; z <= zzn_Local1; z++)
								{
									lz = z - zb_Local[i][j];
									IB_interpo_listz[z - zz_Local1] = delta_3p(lz);
								}

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									IB_interpox = IB_interpo_listx[x - xx_Local1];
									for (y = yy_Local1; y <= yyn_Local1; y++)
									{
										IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
											IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											uf[i][j][0] += u[x][y][z1][0] * IB_interpoz;
											uf[i][j][1] += u[x][y][z1][1] * IB_interpoz;
											uf[i][j][2] += u[x][y][z1][2] * IB_interpoz;
										}
									}
								}
								force_IB[0] = f_coeffi * (uf[i][j][0] - up[i][j][0]) * LP_area_poly[rc_index][i];
								force_IB[1] = f_coeffi * (uf[i][j][1] - up[i][j][1]) * LP_area_poly[rc_index][i];
								force_IB[2] = f_coeffi * (uf[i][j][2] - up[i][j][2]) * LP_area_poly[rc_index][i];

								for (x = xx_Local1; x <= xxn_Local1; x++)
									for (y = yy_Local1; y <= yyn_Local1; y++)
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											//lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
											//IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz) * LP_dr;
                                            IB_interpo = IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] * LP_dr;

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											if (is_single)
											{
												forcef[x][y][z1][0] -= force_IB[0] * IB_interpo;
												forcef[x][y][z1][1] -= force_IB[1] * IB_interpo;
												forcef[x][y][z1][2] -= force_IB[2] * IB_interpo;
											}
											else
											{
#pragma omp atomic
												forcef[x][y][z1][0] -= force_IB[0] * IB_interpo;
#pragma omp atomic
												forcef[x][y][z1][1] -= force_IB[1] * IB_interpo;
#pragma omp atomic
												forcef[x][y][z1][2] -= force_IB[2] * IB_interpo;
											}
										}

								if (xb_Local[i][j] >= 1.5 && xb_Local[i][j] <= (Nx + 1.5) && yb_Local[i][j] >= 1.5 && yb_Local[i][j] <= (Ny + 1.5))
								{
									F_IBM[k][0] += force_IB[0];
									F_IBM[k][1] += force_IB[1];
									F_IBM[k][2] += force_IB[2];
									T_IBM[k][0] += (force_IB[2] * yb_poly[rc_index][i][j] - force_IB[1] * zb_poly[rc_index][i][j]) * dx_poly;
									T_IBM[k][1] += (force_IB[0] * zb_poly[rc_index][i][j] - force_IB[2] * xb_poly[rc_index][i][j]) * dx_poly;
									T_IBM[k][2] += (force_IB[1] * xb_poly[rc_index][i][j] - force_IB[0] * yb_poly[rc_index][i][j]) * dx_poly;
								}
								//tiaoshixinxi
								//if (sqrt(force_IB[0] * force_IB[0] + force_IB[1] * force_IB[1] + force_IB[1] * force_IB[1])*LP_area[i] * LP_dr * dx*dx*dx > 1.0)
								//{
								//	printf("Rank_ID=%d\t time=%d\tpar_num=%d\tlagrangi position = %f\t%f\t%f\t%f\t%f\t%f\n", me, step, k + 1, xb[i][j], yb[i][j], zb[i][j], force_IB[0], force_IB[1], force_IB[2]);
								//}
							}
						}


					temp = LP_dr * (dx_poly * dx_poly * dx_poly);
					for (i = 0; i < 3; i++)
					{
						F_IBM[k][i] *= temp;	T_IBM[k][i] *= temp;
					}
				}
				//printf("Rank:%d\t%d_th particle \tindex=%d\trc=%d\txc=%f\t%f\t%f\tF=%f\t%f\t%f\n", me, k + 1,kk, rc_index,xc[k][0], xc[k][1], xc[k][2], F_IBM[k][0], F_IBM[k][1], F_IBM[k][2]);
			}
		}
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_poly_less_forcef_local(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, x1, y1, z1;
	double lx, ly, lz;
	double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];  //���ϵ��������ٶȣ������ٶȣ�IB����ʱ����
	double xc_key[3];
	double force_IB[3];
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int ns_temp, rc_index;
	double rb_temp;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, forcef_local_size, forcef_local_size_temp;
	int xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2;
	int forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax;
	double f_coeffi = 2 * rhof / dt;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;

	IB_interpo_list_size = 4;//4 for delta_3p func
	forcef_local_size = int(2 * rb_poly_max) + 6;//plus 4 for delta_3p func

#pragma omp parallel
	{
		double xc_key[3];
		double force_IB[3];
		double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];

		int num_threads = omp_get_num_threads();
		bool is_single = (num_threads == 1);

		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];
		double forcef_local[forcef_local_size][forcef_local_size][forcef_local_size][3];

#pragma omp for schedule(dynamic,2) private(i,j,k,kk,x,y,z,x1,y1,z1,lx,ly,lz,IB_interpo,temp,\
												rc_index,rb_temp,ns_temp,xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox,IB_interpoy,IB_interpoz,\
											forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax,\
											xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2,forcef_local_size_temp)
		for (kk = 0; kk < np_local_poly; kk++)
		{
			if (num_p_poly[kk] >= 0)
			{
				k = num_p_poly[kk];
				rc_index = rc_poly[k];
				rb_temp = rb_poly[rc_index];
				ns_temp = (int)(PI * rb_temp / 0.8 + 1);
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}

				if ((xc[k][0] > (me_x * Nx - rb_temp - 1.5)) && (xc[k][0] < ((me_x + 1) * Nx + rb_temp + 1.5)) && (xc[k][1] > (me_y * Ny - rb_temp - 1.5)) && (xc[k][1] < ((me_y + 1) * Ny + rb_temp + 1.5)))
				{
					xc_key[0] = xc[k][0] + 1 - me_x * Nx;
					xc_key[1] = xc[k][1] + 1 - me_y * Ny;
					xc_key[2] = xc[k][2] + 1;

					forcef_local_size_temp = int(2 * rb_temp) + 6;
				
					memset(forcef_local, 0, sizeof(forcef_local));

					forcef_local_xmin = int(xc_key[0] - rb_temp - 1.5) - 1;
					forcef_local_ymin = int(xc_key[1] - rb_temp - 1.5) - 1;
					forcef_local_zmin = int(xc_key[2] - rb_temp - 1.5) - 1;
                    forcef_local_xmax = forcef_local_xmin + forcef_local_size_temp - 1;
                    forcef_local_ymax = forcef_local_ymin + forcef_local_size_temp - 1;
                    forcef_local_zmax = forcef_local_zmin + forcef_local_size_temp - 1;

					forcef_local_xmin = (forcef_local_xmin < 0) ? 0 : forcef_local_xmin;
                    forcef_local_ymin = (forcef_local_ymin < 0) ? 0 : forcef_local_ymin;
					forcef_local_xmax = (forcef_local_xmax > Nx3) ? Nx3 : forcef_local_xmax;
                    forcef_local_ymax = (forcef_local_ymax > Ny3) ? Ny3 : forcef_local_ymax;


					for (i = 0; i < ns_temp; i++)
						for (j = 0; j < nt_poly[rc_index][i]; j++)
						{
							xb_Local[i][j] = xc_key[0] + xb_poly[rc_index][i][j];
							yb_Local[i][j] = xc_key[1] + yb_poly[rc_index][i][j];
							zb_Local[i][j] = xc_key[2] + zb_poly[rc_index][i][j];

							if (xb_Local[i][j] >= 0.5 && xb_Local[i][j] <= (Nx + 2.5) && yb_Local[i][j] >= 0.5 && yb_Local[i][j] <= (Ny + 2.5))
							{
								up[i][j][0] = uc[k][0] + (wc[k][2] * (-yb_poly[rc_index][i][j]) + wc[k][1] * (zb_poly[rc_index][i][j])) * dx_poly;
								up[i][j][1] = uc[k][1] + (wc[k][2] * (xb_poly[rc_index][i][j]) + wc[k][0] * (-zb_poly[rc_index][i][j])) * dx_poly;
								up[i][j][2] = uc[k][2] + (wc[k][0] * (yb_poly[rc_index][i][j]) + wc[k][1] * (-xb_poly[rc_index][i][j])) * dx_poly;
								uf[i][j][0] = 0.0; uf[i][j][1] = 0.0; uf[i][j][2] = 0.0;

								xx_Local = (xb_Local[i][j] - 1.5);
								if (xx_Local <= 0)   xx_Local = 0;
								xxn_Local = (xb_Local[i][j] + 1.5);
								if (xxn_Local >= Nx3)   xxn_Local = Nx3;
								yy_Local = (yb_Local[i][j] - 1.5);
								if (yy_Local <= 0)   yy_Local = 0;
								yyn_Local = (yb_Local[i][j] + 1.5);
								if (yyn_Local >= Ny3)   yyn_Local = Ny3;
								zz_Local = (zb_Local[i][j] - 1.5);
								zzn_Local = (zb_Local[i][j] + 1.5);

								xx_Local1 = int(xx_Local); xxn_Local1 = int(xxn_Local);
								yy_Local1 = int(yy_Local); yyn_Local1 = int(yyn_Local);
								zz_Local1 = int(zz_Local); zzn_Local1 = int(zzn_Local);

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									lx = x - xb_Local[i][j];
									IB_interpo_listx[x - xx_Local1] = delta_3p(lx);
								}
								for (y = yy_Local1; y <= yyn_Local1; y++)
								{
									ly = y - yb_Local[i][j];
									IB_interpo_listy[y - yy_Local1] = delta_3p(ly);
								}
								for (z = zz_Local1; z <= zzn_Local1; z++)
								{
									lz = z - zb_Local[i][j];
									IB_interpo_listz[z - zz_Local1] = delta_3p(lz);
								}

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									IB_interpox = IB_interpo_listx[x - xx_Local1];
									for (y = yy_Local1; y <= yyn_Local1; y++)
									{
										IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
											IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

											z1 = z;
											if (periodic_z == 1)
											{
												if (z <= 1)   z1 = z + Nz;
												if (z >= Nz2)  z1 = z - Nz;
											}
											uf[i][j][0] += u[x][y][z1][0] * IB_interpoz;
											uf[i][j][1] += u[x][y][z1][1] * IB_interpoz;
											uf[i][j][2] += u[x][y][z1][2] * IB_interpoz;
										}
									}
								}
								force_IB[0] = f_coeffi * (uf[i][j][0] - up[i][j][0]) * LP_area_poly[rc_index][i];
								force_IB[1] = f_coeffi * (uf[i][j][1] - up[i][j][1]) * LP_area_poly[rc_index][i];
								force_IB[2] = f_coeffi * (uf[i][j][2] - up[i][j][2]) * LP_area_poly[rc_index][i];
								
								xx_Local2 = xx_Local1 - forcef_local_xmin;
								xxn_Local2 = xxn_Local1 - forcef_local_xmin;
                                yy_Local2 = yy_Local1 - forcef_local_ymin;
                                yyn_Local2 = yyn_Local1 - forcef_local_ymin;
                                zz_Local2 = zz_Local1 - forcef_local_zmin;
                                zzn_Local2 = zzn_Local1 - forcef_local_zmin;

								for (x = xx_Local2; x <= xxn_Local2; x++)
									for (y = yy_Local2; y <= yyn_Local2; y++)
										for (z = zz_Local2; z <= zzn_Local2; z++)
										{
											//lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
											//IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz) * LP_dr;
											IB_interpo = IB_interpo_list[x - xx_Local2][y - yy_Local2][z - zz_Local2] * LP_dr;

											forcef_local[x][y][z][0] += force_IB[0] * IB_interpo;
											forcef_local[x][y][z][1] += force_IB[1] * IB_interpo;
											forcef_local[x][y][z][2] += force_IB[2] * IB_interpo;

										}

								if (xb_Local[i][j] >= 1.5 && xb_Local[i][j] <= (Nx + 1.5) && yb_Local[i][j] >= 1.5 && yb_Local[i][j] <= (Ny + 1.5))
								{
									F_IBM[k][0] += force_IB[0];
									F_IBM[k][1] += force_IB[1];
									F_IBM[k][2] += force_IB[2];
									T_IBM[k][0] += (force_IB[2] * yb_poly[rc_index][i][j] - force_IB[1] * zb_poly[rc_index][i][j]) * dx_poly;
									T_IBM[k][1] += (force_IB[0] * zb_poly[rc_index][i][j] - force_IB[2] * xb_poly[rc_index][i][j]) * dx_poly;
									T_IBM[k][2] += (force_IB[1] * xb_poly[rc_index][i][j] - force_IB[0] * yb_poly[rc_index][i][j]) * dx_poly;
								}
								//tiaoshixinxi
								//if (sqrt(force_IB[0] * force_IB[0] + force_IB[1] * force_IB[1] + force_IB[1] * force_IB[1])*LP_area[i] * LP_dr * dx*dx*dx > 1.0)
								//{
								//	printf("Rank_ID=%d\t time=%d\tpar_num=%d\tlagrangi position = %f\t%f\t%f\t%f\t%f\t%f\n", me, step, k + 1, xb[i][j], yb[i][j], zb[i][j], force_IB[0], force_IB[1], force_IB[2]);
								//}
							}
						}


					temp = LP_dr * (dx_poly * dx_poly * dx_poly);
					for (i = 0; i < 3; i++)
					{
						F_IBM[k][i] *= temp;	T_IBM[k][i] *= temp;
					}
					if (is_single)
					{
						for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
						{
							x1 = x - forcef_local_xmin;
							for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
							{
								y1 = y - forcef_local_ymin;
								for (z1 = 0; z1 < forcef_local_size_temp; z1++)
								{

									z = z1 + forcef_local_zmin;
									if (periodic_z == 1)
									{
										if (z <= 1)
										{
											z = z + Nz;
										}
										else if (z >= Nz2)
										{
											z = z - Nz;
										}
									}

										forcef[x][y][z][0] -= forcef_local[x1][y1][z1][0];
										forcef[x][y][z][1] -= forcef_local[x1][y1][z1][1];
										forcef[x][y][z][2] -= forcef_local[x1][y1][z1][2];
									
								}
							}
						}
					}
					else
					{
#pragma omp critical
						{
							for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
							{
								x1 = x - forcef_local_xmin;
								for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
								{
									y1 = y - forcef_local_ymin;
									for (z1 = 0; z1 < forcef_local_size_temp; z1++)
									{

										z = z1 + forcef_local_zmin;
										if (periodic_z == 1)
										{
											if (z <= 1)
											{
												z = z + Nz;
											}
											else if (z >= Nz2)
											{
												z = z - Nz;
											}
										}

											forcef[x][y][z][0] -= forcef_local[x1][y1][z1][0];
											forcef[x][y][z][1] -= forcef_local[x1][y1][z1][1];
											forcef[x][y][z][2] -= forcef_local[x1][y1][z1][2];
										
									}
								}
							}
						}
					}
				}
				//printf("Rank:%d\t%d_th particle \tindex=%d\trc=%d\txc=%f\t%f\t%f\tF=%f\t%f\t%f\n", me, k + 1,kk, rc_index,xc[k][0], xc[k][1], xc[k][2], F_IBM[k][0], F_IBM[k][1], F_IBM[k][2]);
			}
		}
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_poly_less_forcef_local2(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, x1, y1, z1;
	double lx, ly, lz;
	//double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];  //���ϵ��������ٶȣ������ٶȣ�IB����ʱ����
	double xc_key[3];
	double force_IB[3];
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int ns_temp, rc_index;
	double rb_temp;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, forcef_local_size, forcef_local_size_temp;
	int xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2;
	int forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax;
	double f_coeffi = 2 * rhof / dt;

	int flat_size1 = ns_max * nt1_max * 3;
	int flat_size2 = ns_max * nt1_max;
	int idx_u0, idx_local0, idx_forcef0;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;
	const double key_offsetx = 1. - (double)(me_x) * (double)(Nx);
	const double key_offsety = 1. - (double)(me_y) * (double)(Ny);

	IB_interpo_list_size = 3;//4 for delta_3p func
	forcef_local_size = int(2 * rb_poly_max) + 6;//plus 4 for delta_3p func

#pragma omp parallel
	{
		double xc_key[3];
		volatile double xc_key_temp[3];
		double force_IB[3];
		//double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];
		double* uf = (double*)malloc(flat_size1 * sizeof(double));
		double* up = (double*)malloc(flat_size1 * sizeof(double));
		double* xb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* yb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* zb_Local = (double*)malloc(flat_size2 * sizeof(double));

		int* zb1 = (int*)malloc(flat_size2 * sizeof(int));

		int num_threads = omp_get_num_threads();
		int thread_id = omp_get_thread_num();
		bool is_single = (num_threads == 1);

		// --- 调试日志变量声明 ---
#if IBM_DEBUG_OUTPUT_POLY
		char thread_filename[256];
		FILE* thread_debug_file = NULL;
#endif

		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];
		//double forcef_local[forcef_local_size][forcef_local_size][forcef_local_size][3];
		int forcef_local_total_size = forcef_local_size * forcef_local_size * forcef_local_size * 3;
		double* forcef_local = (double*)malloc(forcef_local_total_size * sizeof(double));

#pragma omp for schedule(dynamic,2) private(i,j,k,kk,x,y,z,x1,y1,z1,lx,ly,lz,IB_interpo,temp,\
												rc_index,rb_temp,ns_temp,xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox,IB_interpoy,IB_interpoz,\
											forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax,\
											xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2,forcef_local_size_temp,\
											idx_u0, idx_local0, idx_forcef0)
		for (kk = 0; kk < np_local_poly; kk++)
		{
			if (num_p_poly[kk] >= 0)
			{
				k = num_p_poly[kk];
				rc_index = rc_poly[k];
				rb_temp = rb_poly[rc_index];
				ns_temp = (int)(PI * rb_temp / 0.8 + 1);
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}

				if ((xc[k][0] > (me_x * Nx - rb_temp - 1.5)) && (xc[k][0] < ((me_x + 1) * Nx + rb_temp + 1.5)) && (xc[k][1] > (me_y * Ny - rb_temp - 1.5)) && (xc[k][1] < ((me_y + 1) * Ny + rb_temp + 1.5)))
				{
					xc_key_temp[0] = xc[k][0];
					xc_key_temp[1] = xc[k][1];
					xc_key_temp[2] = xc[k][2];
					xc_key[0] = xc_key_temp[0] + key_offsetx;
					xc_key[1] = xc_key_temp[1] + key_offsety;
					xc_key[2] = xc_key_temp[2] + 1.0;

					forcef_local_size_temp = int(2 * rb_temp) + 6;

					memset(forcef_local, 0, forcef_local_total_size * sizeof(double));
					memset(zb1, 0, flat_size2 * sizeof(int));

					forcef_local_xmin = int(xc_key[0] - rb_temp - 1.5) - 1;
					forcef_local_ymin = int(xc_key[1] - rb_temp - 1.5) - 1;
					forcef_local_zmin = int(xc_key[2] - rb_temp - 1.5) - 1;
					forcef_local_xmax = forcef_local_xmin + forcef_local_size_temp - 1;
					forcef_local_ymax = forcef_local_ymin + forcef_local_size_temp - 1;
					forcef_local_zmax = forcef_local_zmin + forcef_local_size_temp - 1;

					forcef_local_xmin = (forcef_local_xmin < 0) ? 0 : forcef_local_xmin;
					forcef_local_ymin = (forcef_local_ymin < 0) ? 0 : forcef_local_ymin;
					forcef_local_xmax = (forcef_local_xmax > Nx3) ? Nx3 : forcef_local_xmax;
					forcef_local_ymax = (forcef_local_ymax > Ny3) ? Ny3 : forcef_local_ymax;


					for (i = 0; i < ns_temp; i++)
						for (j = 0; j < nt_poly[rc_index][i]; j++)
						{
							idx_local0 = i * nt1_max + j;
							xb_Local[idx_local0] = xc_key[0] + xb_poly[rc_index][i][j];
							yb_Local[idx_local0] = xc_key[1] + yb_poly[rc_index][i][j];
							zb_Local[idx_local0] = xc_key[2] + zb_poly[rc_index][i][j];
							if (periodic_z == 1)
							{
								if (zb_Local[idx_local0] <= 1)   zb1[idx_local0] =   Nz;
								if (zb_Local[idx_local0] >= Nz2)  zb1[idx_local0] =  - Nz;
							}

							if (xb_Local[idx_local0] >= 0.5 && xb_Local[idx_local0] <= (Nx + 2.5) && yb_Local[idx_local0] >= 0.5 && yb_Local[idx_local0] <= (Ny + 2.5))
							{
                                idx_u0 = (i * nt1_max + j)*3;
								up[idx_u0]   = uc[k][0] + (wc[k][2] * (-yb_poly[rc_index][i][j]) + wc[k][1] * (zb_poly[rc_index][i][j])) * dx_poly;
								up[idx_u0+1] = uc[k][1] + (wc[k][2] * (xb_poly[rc_index][i][j]) + wc[k][0] * (-zb_poly[rc_index][i][j])) * dx_poly;
								up[idx_u0+2] = uc[k][2] + (wc[k][0] * (yb_poly[rc_index][i][j]) + wc[k][1] * (-xb_poly[rc_index][i][j])) * dx_poly;
								uf[idx_u0] = 0.0; uf[idx_u0+1] = 0.0; uf[idx_u0+2] = 0.0;
								/*
								xx_Local = (xb_Local[idx_local0] - 1.5);
								if (xx_Local <= 0)   xx_Local = 0;
								xxn_Local = (xb_Local[idx_local0] + 1.5);
								if (xxn_Local >= Nx3)   xxn_Local = Nx3;
								yy_Local = (yb_Local[idx_local0] - 1.5);
								if (yy_Local <= 0)   yy_Local = 0;
								yyn_Local = (yb_Local[idx_local0] + 1.5);
								if (yyn_Local >= Ny3)   yyn_Local = Ny3;
								zz_Local = (zb_Local[idx_local0] - 1.5);
								zzn_Local = (zb_Local[idx_local0] + 1.5);
								
								xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = int(xxn_Local);
								yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = int(yyn_Local);
								zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = int(zzn_Local);
								*/
								xx_Local = (xb_Local[idx_local0] - 1.5);
								yy_Local = (yb_Local[idx_local0] - 1.5);
								zz_Local = (zb_Local[idx_local0] + zb1[idx_local0] - 1.5);
								xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = xx_Local1+2;
								yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = yy_Local1+2;
								zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = zz_Local1+2;

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									lx = x - xb_Local[idx_local0];
									IB_interpo_listx[x - xx_Local1] = delta_3p(lx);
								}
								for (y = yy_Local1; y <= yyn_Local1; y++)
								{
									ly = y - yb_Local[idx_local0];
									IB_interpo_listy[y - yy_Local1] = delta_3p(ly);
								}
								for (z = zz_Local1; z <= zzn_Local1; z++)
								{
									lz = z - zb_Local[idx_local0] - zb1[idx_local0];
									IB_interpo_listz[z - zz_Local1] = delta_3p(lz);
								}

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									IB_interpox = IB_interpo_listx[x - xx_Local1];
									for (y = yy_Local1; y <= yyn_Local1; y++)
									{
										IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
											IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

											z1 = z;
											//if (periodic_z == 1)
											//{
											//	if (z <= 1)   z1 = z + Nz;
											//	if (z >= Nz2)  z1 = z - Nz;
											//}
											uf[idx_u0] += u[x][y][z1][0] * IB_interpoz;
											uf[idx_u0+1] += u[x][y][z1][1] * IB_interpoz;
											uf[idx_u0+2] += u[x][y][z1][2] * IB_interpoz;
										}
									}
								}
								force_IB[0] = f_coeffi * (uf[idx_u0] - up[idx_u0]) * LP_area_poly[rc_index][i];
								force_IB[1] = f_coeffi * (uf[idx_u0+1] - up[idx_u0+1]) * LP_area_poly[rc_index][i];
								force_IB[2] = f_coeffi * (uf[idx_u0+2] - up[idx_u0+2]) * LP_area_poly[rc_index][i];

#if IBM_DEBUG_OUTPUT_POLY
								// 1. 直接生成文件名 (每100步一个文件，例如 step_0-99_debug_poly_rank_0_thread_0.log)
								//    注意：step / 100 是整数除法
								sprintf(thread_filename, "IBM_lag_step_%03d-%03d_debug_poly_rank_%d_thread_%d.log",
									(step / 100) * 100, (step / 100) * 100 + 99, me, thread_id);

								// 2. 以追加模式 "a" 打开文件
								thread_debug_file = fopen(thread_filename, "a");

								if (thread_debug_file != NULL) {
									// 3. 写入信息 (全部使用 %.6f)
									fprintf(thread_debug_file,
										"Step=%d, Par_k=%d, LagPt=(%d,%d), "
										"Coord=(%.6f,%.6f,%.6f), "
										"uf=(%.6f,%.6f,%.6f), "
										"up=(%.6f,%.6f,%.6f), "
										"f_IB=(%.6f,%.6f,%.6f)\n",
										step, k, i, j,
										(xb_Local[idx_local0]-key_offsetx), (yb_Local[idx_local0]-key_offsety), (zb_Local[idx_local0]-1.),
										uf[idx_u0], uf[idx_u0 + 1], uf[idx_u0 + 2],
										up[idx_u0], up[idx_u0 + 1], up[idx_u0 + 2],
										force_IB[0], force_IB[1], force_IB[2]);

									fclose(thread_debug_file); // 写完立即关闭，避免句柄泄漏
								}
#endif

								xx_Local2 = xx_Local1 - forcef_local_xmin;
								xxn_Local2 = xxn_Local1 - forcef_local_xmin;
								yy_Local2 = yy_Local1 - forcef_local_ymin;
								yyn_Local2 = yyn_Local1 - forcef_local_ymin;
								zz_Local2 = zz_Local1 - forcef_local_zmin - zb1[idx_local0];
								zzn_Local2 = zzn_Local1 - forcef_local_zmin - zb1[idx_local0];

								for (x = xx_Local2; x <= xxn_Local2; x++)
									for (y = yy_Local2; y <= yyn_Local2; y++)
										for (z = zz_Local2; z <= zzn_Local2; z++)
										{
											//lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
											//IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz) * LP_dr;
											IB_interpo = IB_interpo_list[x - xx_Local2][y - yy_Local2][z - zz_Local2] * LP_dr;
											idx_forcef0 = ((x * forcef_local_size + y) * forcef_local_size + z) * 3;

											forcef_local[idx_forcef0] += force_IB[0] * IB_interpo;
											forcef_local[idx_forcef0+1] += force_IB[1] * IB_interpo;
											forcef_local[idx_forcef0+2] += force_IB[2] * IB_interpo;

										}

								if (xb_Local[idx_local0] >= 1.5 && xb_Local[idx_local0] <= (Nx + 1.5) && yb_Local[idx_local0] >= 1.5 && yb_Local[idx_local0] <= (Ny + 1.5))
								{
									F_IBM[k][0] += force_IB[0];
									F_IBM[k][1] += force_IB[1];
									F_IBM[k][2] += force_IB[2];
									T_IBM[k][0] += (force_IB[2] * yb_poly[rc_index][i][j] - force_IB[1] * zb_poly[rc_index][i][j]) * dx_poly;
									T_IBM[k][1] += (force_IB[0] * zb_poly[rc_index][i][j] - force_IB[2] * xb_poly[rc_index][i][j]) * dx_poly;
									T_IBM[k][2] += (force_IB[1] * xb_poly[rc_index][i][j] - force_IB[0] * yb_poly[rc_index][i][j]) * dx_poly;
								}
								//tiaoshixinxi
								//if (sqrt(force_IB[0] * force_IB[0] + force_IB[1] * force_IB[1] + force_IB[1] * force_IB[1])*LP_area[i] * LP_dr * dx*dx*dx > 1.0)
								//{
								//	printf("Rank_ID=%d\t time=%d\tpar_num=%d\tlagrangi position = %f\t%f\t%f\t%f\t%f\t%f\n", me, step, k + 1, xb[i][j], yb[i][j], zb[i][j], force_IB[0], force_IB[1], force_IB[2]);
								//}
							}
						}


					temp = LP_dr * (dx_poly * dx_poly * dx_poly);
					for (i = 0; i < 3; i++)
					{
						F_IBM[k][i] *= temp;	T_IBM[k][i] *= temp;
					}
					if (is_single)
					{
						for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
						{
							x1 = x - forcef_local_xmin;
							for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
							{
								y1 = y - forcef_local_ymin;
								for (z1 = 0; z1 < forcef_local_size_temp; z1++)
								{

									z = z1 + forcef_local_zmin;
									if (periodic_z == 1)
									{
										if (z <= 1)
										{
											z = z + Nz;
										}
										else if (z >= Nz2)
										{
											z = z - Nz;
										}
									}
									idx_forcef0 = ((x1 * forcef_local_size + y1) * forcef_local_size + z1) * 3;
									forcef[x][y][z][0] -= forcef_local[idx_forcef0];
									forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
									forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];

								}
							}
						}
					}
					else
					{
//#pragma omp critical
						{
							for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
							{
								x1 = x - forcef_local_xmin;
								for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
								{
									y1 = y - forcef_local_ymin;
									for (z1 = 0; z1 < forcef_local_size_temp; z1++)
									{

										z = z1 + forcef_local_zmin;
										if (periodic_z == 1)
										{
											if (z <= 1)
											{
												z = z + Nz;
											}
											else if (z >= Nz2)
											{
												z = z - Nz;
											}
										}
										idx_forcef0 = ((x1 * forcef_local_size + y1) * forcef_local_size + z1) * 3;
#pragma omp atomic
										forcef[x][y][z][0] -= forcef_local[idx_forcef0];
#pragma omp atomic
										forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
#pragma omp atomic
										forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];

									}
								}
							}
						}
					}
				}
				//printf("Rank:%d\t%d_th particle \tindex=%d\trc=%d\txc=%f\t%f\t%f\tF=%f\t%f\t%f\n", me, k + 1,kk, rc_index,xc[k][0], xc[k][1], xc[k][2], F_IBM[k][0], F_IBM[k][1], F_IBM[k][2]);
			}
		}
		free(uf);
		free(up);
		free(xb_Local);
		free(yb_Local);
		free(zb_Local);
		free(forcef_local);
		free(zb1);
		#pragma omp barrier
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_poly_less_forcef_local3(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, x1, y1, z1;
	double lx, ly, lz;
	double xc_key[3];
	double force_IB[3];
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int ns_temp, rc_index;
	double rb_temp;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, forcef_local_size, forcef_local_size_temp;
	int xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2;
	int forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax;
	double f_coeffi = 2 * rhof / dt;

	int flat_size1 = ns_max * nt1_max * 3;
	int flat_size2 = ns_max * nt1_max;
	int idx_u0, idx_local0, idx_forcef0;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;
	const double key_offsetx = 1. - (double)(me_x) * (double)(Nx);
	const double key_offsety = 1. - (double)(me_y) * (double)(Ny);

	IB_interpo_list_size = 3;//4 for delta_3p func
	forcef_local_size = int(2 * rb_poly_max) + 6;//plus 4 for delta_3p func

#pragma omp parallel
	{
		double xc_key[3];
		volatile double xc_key_temp[3];
		double force_IB[3];
		double* uf = (double*)malloc(flat_size1 * sizeof(double));
		double* up = (double*)malloc(flat_size1 * sizeof(double));
		double* xb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* yb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* zb_Local = (double*)malloc(flat_size2 * sizeof(double));

		int* zb1 = (int*)malloc(flat_size2 * sizeof(int));

		int num_threads = omp_get_num_threads();
		int thread_id = omp_get_thread_num();
		bool is_single = (num_threads == 1);

		// --- 调试日志变量声明 ---
#if IBM_DEBUG_OUTPUT_POLY
		char thread_filename[256];
		FILE* thread_debug_file = NULL;
#endif

		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];
		int forcef_local_total_size = forcef_local_size * forcef_local_size * forcef_local_size * 3;
		double* forcef_local = (double*)malloc(forcef_local_total_size * sizeof(double));
		/* 掩码按最大边长分配：每个线程一份，在不同颗粒之间复用。 */
        size_t zmask_words_per_row = ((size_t)forcef_local_size + 63) / 64;
        size_t zmask_total_words = (size_t)forcef_local_size * forcef_local_size * zmask_words_per_row;
        uint64_t* zmask = (uint64_t*)malloc(zmask_total_words * sizeof(uint64_t));

#pragma omp for schedule(dynamic,2) private(i,j,k,kk,x,y,z,x1,y1,z1,lx,ly,lz,IB_interpo,temp,\
												rc_index,rb_temp,ns_temp,xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox,IB_interpoy,IB_interpoz,\
											forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax,\
											xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2,forcef_local_size_temp,\
											idx_u0, idx_local0, idx_forcef0)
		for (kk = 0; kk < np_local_poly; kk++)
		{
			if (num_p_poly[kk] >= 0)
			{
				k = num_p_poly[kk];
				rc_index = rc_poly[k];
				rb_temp = rb_poly[rc_index];
				ns_temp = (int)(PI * rb_temp / 0.8 + 1);
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}

				if ((xc[k][0] > (me_x * Nx - rb_temp - 1.5)) && (xc[k][0] < ((me_x + 1) * Nx + rb_temp + 1.5)) && (xc[k][1] > (me_y * Ny - rb_temp - 1.5)) && (xc[k][1] < ((me_y + 1) * Ny + rb_temp + 1.5)))
				{
					xc_key_temp[0] = xc[k][0];
					xc_key_temp[1] = xc[k][1];
					xc_key_temp[2] = xc[k][2];
					xc_key[0] = xc_key_temp[0] + key_offsetx;
					xc_key[1] = xc_key_temp[1] + key_offsety;
					xc_key[2] = xc_key_temp[2] + 1.0;

					forcef_local_size_temp = int(2 * rb_temp) + 6;

					memset(forcef_local, 0, forcef_local_total_size * sizeof(double));
					memset(zb1, 0, flat_size2 * sizeof(int));
					memset(zmask, 0, zmask_total_words * sizeof(uint64_t));

					forcef_local_xmin = int(xc_key[0] - rb_temp - 1.5) - 1;
					forcef_local_ymin = int(xc_key[1] - rb_temp - 1.5) - 1;
					forcef_local_zmin = int(xc_key[2] - rb_temp - 1.5) - 1;
					forcef_local_xmax = forcef_local_xmin + forcef_local_size_temp - 1;
					forcef_local_ymax = forcef_local_ymin + forcef_local_size_temp - 1;
					forcef_local_zmax = forcef_local_zmin + forcef_local_size_temp - 1;

					forcef_local_xmin = (forcef_local_xmin < 0) ? 0 : forcef_local_xmin;
					forcef_local_ymin = (forcef_local_ymin < 0) ? 0 : forcef_local_ymin;
					forcef_local_xmax = (forcef_local_xmax > Nx3) ? Nx3 : forcef_local_xmax;
					forcef_local_ymax = (forcef_local_ymax > Ny3) ? Ny3 : forcef_local_ymax;


					for (i = 0; i < ns_temp; i++)
						for (j = 0; j < nt_poly[rc_index][i]; j++)
						{
							idx_local0 = i * nt1_max + j;
							xb_Local[idx_local0] = xc_key[0] + xb_poly[rc_index][i][j];
							yb_Local[idx_local0] = xc_key[1] + yb_poly[rc_index][i][j];
							zb_Local[idx_local0] = xc_key[2] + zb_poly[rc_index][i][j];
							if (periodic_z == 1)
							{
								if (zb_Local[idx_local0] <= 1)   zb1[idx_local0] =   Nz;
								if (zb_Local[idx_local0] >= Nz2)  zb1[idx_local0] =  - Nz;
							}

							if (xb_Local[idx_local0] >= 0.5 && xb_Local[idx_local0] <= (Nx + 2.5) && yb_Local[idx_local0] >= 0.5 && yb_Local[idx_local0] <= (Ny + 2.5))
							{
                                idx_u0 = (i * nt1_max + j)*3;
								up[idx_u0]   = uc[k][0] + (wc[k][2] * (-yb_poly[rc_index][i][j]) + wc[k][1] * (zb_poly[rc_index][i][j])) * dx_poly;
								up[idx_u0+1] = uc[k][1] + (wc[k][2] * (xb_poly[rc_index][i][j]) + wc[k][0] * (-zb_poly[rc_index][i][j])) * dx_poly;
								up[idx_u0+2] = uc[k][2] + (wc[k][0] * (yb_poly[rc_index][i][j]) + wc[k][1] * (-xb_poly[rc_index][i][j])) * dx_poly;
								uf[idx_u0] = 0.0; uf[idx_u0+1] = 0.0; uf[idx_u0+2] = 0.0;
								
								xx_Local = (xb_Local[idx_local0] - 1.5);
								yy_Local = (yb_Local[idx_local0] - 1.5);
								zz_Local = (zb_Local[idx_local0] + zb1[idx_local0] - 1.5);
								xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = xx_Local1+2;
								yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = yy_Local1+2;
								zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = zz_Local1+2;
						
								lx = xb_Local[idx_local0]-xx_Local1 - 1;
								delta_3p_less(lx,IB_interpo_listx);
								ly = yb_Local[idx_local0]-yy_Local1 - 1;
                                delta_3p_less(ly,IB_interpo_listy);
								lz = zb_Local[idx_local0]-zz_Local1 - 1 + zb1[idx_local0];
                                delta_3p_less(lz,IB_interpo_listz);

								for (x = xx_Local1; x <= xxn_Local1; x++)
								{
									IB_interpox = IB_interpo_listx[x - xx_Local1];
									for (y = yy_Local1; y <= yyn_Local1; y++)
									{
										IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
										for (z = zz_Local1; z <= zzn_Local1; z++)
										{
											IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
											IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

											z1 = z;
											uf[idx_u0] += u[x][y][z1][0] * IB_interpoz;
											uf[idx_u0+1] += u[x][y][z1][1] * IB_interpoz;
											uf[idx_u0+2] += u[x][y][z1][2] * IB_interpoz;
										}
									}
								}
								force_IB[0] = f_coeffi * (uf[idx_u0] - up[idx_u0]) * LP_area_poly[rc_index][i];
								force_IB[1] = f_coeffi * (uf[idx_u0+1] - up[idx_u0+1]) * LP_area_poly[rc_index][i];
								force_IB[2] = f_coeffi * (uf[idx_u0+2] - up[idx_u0+2]) * LP_area_poly[rc_index][i];

#if IBM_DEBUG_OUTPUT_POLY
								// 1. 直接生成文件名 (每100步一个文件，例如 step_0-99_debug_poly_rank_0_thread_0.log)
								//    注意：step / 100 是整数除法
								sprintf(thread_filename, "IBM_lag_step_%03d-%03d_debug_poly_rank_%d_thread_%d.log",
									(step / 100) * 100, (step / 100) * 100 + 99, me, thread_id);

								// 2. 以追加模式 "a" 打开文件
								thread_debug_file = fopen(thread_filename, "a");

								if (thread_debug_file != NULL) {
									// 3. 写入信息 (全部使用 %.6f)
									fprintf(thread_debug_file,
										"Step=%d, Par_k=%d, LagPt=(%d,%d), "
										"Coord=(%.6f,%.6f,%.6f), "
										"uf=(%.6f,%.6f,%.6f), "
										"up=(%.6f,%.6f,%.6f), "
										"f_IB=(%.6f,%.6f,%.6f)\n",
										step, k, i, j,
										(xb_Local[idx_local0]-key_offsetx), (yb_Local[idx_local0]-key_offsety), (zb_Local[idx_local0]-1.),
										uf[idx_u0], uf[idx_u0 + 1], uf[idx_u0 + 2],
										up[idx_u0], up[idx_u0 + 1], up[idx_u0 + 2],
										force_IB[0], force_IB[1], force_IB[2]);

									fclose(thread_debug_file); // 写完立即关闭，避免句柄泄漏
								}
#endif

								xx_Local2 = xx_Local1 - forcef_local_xmin;
								xxn_Local2 = xxn_Local1 - forcef_local_xmin;
								yy_Local2 = yy_Local1 - forcef_local_ymin;
								yyn_Local2 = yyn_Local1 - forcef_local_ymin;
								zz_Local2 = zz_Local1 - forcef_local_zmin - zb1[idx_local0];
								zzn_Local2 = zzn_Local1 - forcef_local_zmin - zb1[idx_local0];
								
								size_t zmask_word;
                                unsigned int zmask_bit;
                                uint64_t zmask_low, zmask_high;
								zmask_word = (size_t)zz_Local2 >> 6;
								zmask_bit = (unsigned int)zz_Local2 & 63u;
								zmask_low = (uint64_t)7 << zmask_bit;
								zmask_high = 0;
								if (zmask_bit >= 62u)
									zmask_high = (uint64_t)7 >> (64u - zmask_bit);

								for (x = xx_Local2; x <= xxn_Local2; x++)
									for (y = yy_Local2; y <= yyn_Local2; y++)
									{
										uint64_t* zmask_row;
										zmask_row = zmask + ((size_t)x * forcef_local_size + y) * zmask_words_per_row;
										zmask_row[zmask_word] |= zmask_low;
										if (zmask_high != 0)
											zmask_row[zmask_word + 1] |= zmask_high;

										for (z = zz_Local2; z <= zzn_Local2; z++)
										{
											IB_interpo = IB_interpo_list[x - xx_Local2][y - yy_Local2][z - zz_Local2] * LP_dr;
											idx_forcef0 = ((x * forcef_local_size + y) * forcef_local_size + z) * 3;
											forcef_local[idx_forcef0] += force_IB[0] * IB_interpo;
											forcef_local[idx_forcef0+1] += force_IB[1] * IB_interpo;
											forcef_local[idx_forcef0+2] += force_IB[2] * IB_interpo;
										}
									}
								if (xb_Local[idx_local0] >= 1.5 && xb_Local[idx_local0] <= (Nx + 1.5) && yb_Local[idx_local0] >= 1.5 && yb_Local[idx_local0] <= (Ny + 1.5))
								{
									F_IBM[k][0] += force_IB[0];
									F_IBM[k][1] += force_IB[1];
									F_IBM[k][2] += force_IB[2];
									T_IBM[k][0] += (force_IB[2] * yb_poly[rc_index][i][j] - force_IB[1] * zb_poly[rc_index][i][j]) * dx_poly;
									T_IBM[k][1] += (force_IB[0] * zb_poly[rc_index][i][j] - force_IB[2] * xb_poly[rc_index][i][j]) * dx_poly;
									T_IBM[k][2] += (force_IB[1] * xb_poly[rc_index][i][j] - force_IB[0] * yb_poly[rc_index][i][j]) * dx_poly;
								}
							}
						}


					temp = LP_dr * (dx_poly * dx_poly * dx_poly);
					for (i = 0; i < 3; i++)
					{
						F_IBM[k][i] *= temp;	T_IBM[k][i] *= temp;
					}

					size_t active_z_words;	
					unsigned int active_z_tail;
					uint64_t last_z_mask;

					active_z_words = ((size_t)forcef_local_size_temp + 63) / 64;
					active_z_tail = (unsigned int)forcef_local_size_temp & 63u;
					last_z_mask = ~(uint64_t)0;

					/* 有效长度正好是 64 的整数倍时保留整字，避免左移 64 位。 */
					if (active_z_tail != 0)
						last_z_mask = ((uint64_t)1 << active_z_tail) - 1;

					if (is_single)
					{
						for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
						{
							x1 = x - forcef_local_xmin;
							for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
							{
								size_t local_row, force_row, mask_word_id;
								const uint64_t* zmask_row;

								y1 = y - forcef_local_ymin;
								local_row = (size_t)x1 * forcef_local_size + y1;
								force_row = local_row * forcef_local_size * 3;
								zmask_row = zmask + local_row * zmask_words_per_row;

								/* 只扫描当前颗粒使用的字；数组寻址仍用最大容量步长。 */
								for (mask_word_id = 0; mask_word_id < active_z_words; mask_word_id++)
								{
									uint64_t mask_word;
									mask_word = zmask_row[mask_word_id];
									if (mask_word_id + 1 == active_z_words)
										mask_word &= last_z_mask;

									while (mask_word != 0)
									{
										unsigned int bit;

										/* 输入非零时才调用 ctzll；逐次去掉最低一个置位。 */
										bit = (unsigned int)__builtin_ctzll((unsigned long long)mask_word);
										mask_word &= mask_word - 1;
										z1 = (int)(mask_word_id * 64 + bit);

										/* 标记属于未映射的局部节点，恢复全局 z 后再处理周期性。 */
										z = z1 + forcef_local_zmin;
										if (periodic_z == 1)
										{
											if (z <= 1)
												z += Nz;
											else if (z >= Nz2)
												z -= Nz;
										}
										idx_forcef0 = force_row + (size_t)z1 * 3;
										forcef[x][y][z][0] -= forcef_local[idx_forcef0];
										forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
										forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];
									}
								}
							}
						}
					}
					else
					{
						{
							for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
							{
								x1 = x - forcef_local_xmin;
								for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
								{
									size_t local_row, force_row, mask_word_id;
                                    const uint64_t* zmask_row;

                                    y1 = y - forcef_local_ymin;
                                    local_row = (size_t)x1 * forcef_local_size + y1;
                                    force_row = local_row * forcef_local_size * 3;
                                    zmask_row = zmask + local_row * zmask_words_per_row;

                                    /* 只扫描当前颗粒使用的字；数组寻址仍用最大容量步长。 */
                                    for (mask_word_id = 0; mask_word_id < active_z_words; mask_word_id++)
                                    {
                                        uint64_t mask_word;
                                        mask_word = zmask_row[mask_word_id];
                                        if (mask_word_id + 1 == active_z_words)
                                            mask_word &= last_z_mask;

                                        while (mask_word != 0)
                                        {
                                            unsigned int bit;

                                            /* 输入非零时才调用 ctzll；逐次去掉最低一个置位。 */
                                            bit = (unsigned int)__builtin_ctzll((unsigned long long)mask_word);
                                            mask_word &= mask_word - 1;
                                            z1 = (int)(mask_word_id * 64 + bit);

                                            /* 标记属于未映射的局部节点，恢复全局 z 后再处理周期性。 */
                                            z = z1 + forcef_local_zmin;
                                            if (periodic_z == 1)
                                            {
                                                if (z <= 1)
                                                    z += Nz;
                                                else if (z >= Nz2)
                                                    z -= Nz;
                                            }
                                            idx_forcef0 = force_row + (size_t)z1 * 3;
#pragma omp atomic
                                            forcef[x][y][z][0] -= forcef_local[idx_forcef0];
#pragma omp atomic
                                            forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
#pragma omp atomic
                                            forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];
                                        }
                                    }
								}
							}
						}
					}
				}
			}
		}
		free(uf);
		free(up);
		free(xb_Local);
		free(yb_Local);
		free(zb_Local);
		free(forcef_local);
		free(zb1);
		free(zmask);
		#pragma omp barrier
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_poly_less_forcef_local2_MDF(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, x1, y1, z1;
	double lx, ly, lz;
	//double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];  //���ϵ��������ٶȣ������ٶȣ�IB����ʱ����
	double xc_key[3];
	double force_IB[3];
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int ns_temp, rc_index;
	double rb_temp;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, forcef_local_size, forcef_local_size_temp;
	int xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2;
	int forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax;
	double f_coeffi = 2 * rhof / dt;

	int flat_size1 = ns_max * nt1_max * 3;
	int flat_size2 = ns_max * nt1_max;
	int idx_u0, idx_local0, idx_forcef0;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;
	const double key_offsetx = 1. - (double)(me_x) * (double)(Nx);
	const double key_offsety = 1. - (double)(me_y) * (double)(Ny);

	IB_interpo_list_size = 3;//4 for delta_3p func
	forcef_local_size = int(2 * rb_poly_max) + 6;//plus 4 for delta_3p func

	//for utemp exchange
	int low_ex, up_ex, right_ex, left_ex;
	double fluidu_temp_ex[8][Nx4][Nz4][3];

	if (periodic_x == 1)
	{
		if (me % NPROCx == 0)
			left_ex = me + NPROCx - 1;
		else
			left_ex = me - 1;
		if ((me + 1) % NPROCx == 0)
			right_ex = me - NPROCx + 1;
		else
			right_ex = me + 1;
	}
	else if (periodic_x == 0)
	{
		if (me % NPROCx == 0)
			left_ex = MPI_PROC_NULL;
		else
			left_ex = me - 1;
		if ((me + 1) % NPROCx == 0)
			right_ex = MPI_PROC_NULL;
		else
			right_ex = me + 1;
	}

	if (periodic_y == 1)
	{
		if (me < NPROCx)
			low_ex = me + NPROCx * (NPROCy - 1);
		else
			low_ex = me - NPROCx;
		if (me >= (NPROCx * (NPROCy - 1)))
			up_ex = me - NPROCx * (NPROCy - 1);
		else
			up_ex = me + NPROCx;
	}
	else if (periodic_y == 0)
	{
		if (me < NPROCx)
			low_ex = MPI_PROC_NULL;
		else
			low_ex = me - NPROCx;
		if (me >= (NPROCx * (NPROCy - 1)))
			up_ex = MPI_PROC_NULL;
		else
			up_ex = me + NPROCx;
	}

#pragma omp parallel
	{
		double xc_key[3];
		volatile double xc_key_temp[3];
		double force_IB[3];
		//double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];
		double* uf = (double*)malloc(flat_size1 * sizeof(double));
		double* up = (double*)malloc(flat_size1 * sizeof(double));
		double* xb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* yb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* zb_Local = (double*)malloc(flat_size2 * sizeof(double));

		int num_threads = omp_get_num_threads();
		int thread_id = omp_get_thread_num();
		bool is_single = (num_threads == 1);

		// --- 调试日志变量声明 ---
#if IBM_DEBUG_OUTPUT_POLY
		char thread_filename[256];
		FILE* thread_debug_file = NULL;
#endif
		int iter_k;
		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];
		//double forcef_local[forcef_local_size][forcef_local_size][forcef_local_size][3];
		int forcef_local_total_size = forcef_local_size * forcef_local_size * forcef_local_size * 3;
		double* forcef_local = (double*)malloc(forcef_local_total_size * sizeof(double));

#pragma omp for schedule(static) private(k, kk, i)
		for (kk = 0; kk < np_local_poly; kk++)
		{
			if (num_p_poly[kk] >= 0)
			{
				k = num_p_poly[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}
			}
		}
		for (iter_k = 0; iter_k < NF; iter_k++)
		{
#pragma omp for schedule(dynamic,2) private(i,j,k,kk,x,y,z,x1,y1,z1,lx,ly,lz,IB_interpo,temp,\
												rc_index,rb_temp,ns_temp,xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox,IB_interpoy,IB_interpoz,\
											forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax,\
											xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2,forcef_local_size_temp,\
											idx_u0, idx_local0, idx_forcef0)
			for (kk = 0; kk < np_local_poly; kk++)
			{
				if (num_p_poly[kk] >= 0)
				{
					k = num_p_poly[kk];
					rc_index = rc_poly[k];
					rb_temp = rb_poly[rc_index];
					ns_temp = (int)(PI * rb_temp / 0.8 + 1);

					if ((xc[k][0] > (me_x * Nx - rb_temp - 1.5)) && (xc[k][0] < ((me_x + 1) * Nx + rb_temp + 1.5)) && (xc[k][1] > (me_y * Ny - rb_temp - 1.5)) && (xc[k][1] < ((me_y + 1) * Ny + rb_temp + 1.5)))
					{
						xc_key_temp[0] = xc[k][0];
						xc_key_temp[1] = xc[k][1];
						xc_key_temp[2] = xc[k][2];
						xc_key[0] = xc_key_temp[0] + key_offsetx;
						xc_key[1] = xc_key_temp[1] + key_offsety;
						xc_key[2] = xc_key_temp[2] + 1.0;

						forcef_local_size_temp = int(2 * rb_temp) + 6;

						memset(forcef_local, 0, forcef_local_total_size * sizeof(double));

						forcef_local_xmin = int(xc_key[0] - rb_temp - 1.5) - 1;
						forcef_local_ymin = int(xc_key[1] - rb_temp - 1.5) - 1;
						forcef_local_zmin = int(xc_key[2] - rb_temp - 1.5) - 1;
						forcef_local_xmax = forcef_local_xmin + forcef_local_size_temp - 1;
						forcef_local_ymax = forcef_local_ymin + forcef_local_size_temp - 1;
						forcef_local_zmax = forcef_local_zmin + forcef_local_size_temp - 1;

						forcef_local_xmin = (forcef_local_xmin < 0) ? 0 : forcef_local_xmin;
						forcef_local_ymin = (forcef_local_ymin < 0) ? 0 : forcef_local_ymin;
						forcef_local_xmax = (forcef_local_xmax > Nx3) ? Nx3 : forcef_local_xmax;
						forcef_local_ymax = (forcef_local_ymax > Ny3) ? Ny3 : forcef_local_ymax;


						for (i = 0; i < ns_temp; i++)
							for (j = 0; j < nt_poly[rc_index][i]; j++)
							{
								idx_local0 = i * nt1_max + j;
								xb_Local[idx_local0] = xc_key[0] + xb_poly[rc_index][i][j];
								yb_Local[idx_local0] = xc_key[1] + yb_poly[rc_index][i][j];
								zb_Local[idx_local0] = xc_key[2] + zb_poly[rc_index][i][j];

								if (xb_Local[idx_local0] >= 0.5 && xb_Local[idx_local0] <= (Nx + 2.5) && yb_Local[idx_local0] >= 0.5 && yb_Local[idx_local0] <= (Ny + 2.5))
								{
									idx_u0 = (i * nt1_max + j) * 3;
									up[idx_u0] = uc[k][0] + (wc[k][2] * (-yb_poly[rc_index][i][j]) + wc[k][1] * (zb_poly[rc_index][i][j])) * dx_poly;
									up[idx_u0 + 1] = uc[k][1] + (wc[k][2] * (xb_poly[rc_index][i][j]) + wc[k][0] * (-zb_poly[rc_index][i][j])) * dx_poly;
									up[idx_u0 + 2] = uc[k][2] + (wc[k][0] * (yb_poly[rc_index][i][j]) + wc[k][1] * (-xb_poly[rc_index][i][j])) * dx_poly;
									uf[idx_u0] = 0.0; uf[idx_u0 + 1] = 0.0; uf[idx_u0 + 2] = 0.0;
									/*
									xx_Local = (xb_Local[idx_local0] - 1.5);
									if (xx_Local <= 0)   xx_Local = 0;
									xxn_Local = (xb_Local[idx_local0] + 1.5);
									if (xxn_Local >= Nx3)   xxn_Local = Nx3;
									yy_Local = (yb_Local[idx_local0] - 1.5);
									if (yy_Local <= 0)   yy_Local = 0;
									yyn_Local = (yb_Local[idx_local0] + 1.5);
									if (yyn_Local >= Ny3)   yyn_Local = Ny3;
									zz_Local = (zb_Local[idx_local0] - 1.5);
									zzn_Local = (zb_Local[idx_local0] + 1.5);

									xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = int(xxn_Local);
									yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = int(yyn_Local);
									zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = int(zzn_Local);
									*/
									xx_Local = (xb_Local[idx_local0] - 1.5);
									yy_Local = (yb_Local[idx_local0] - 1.5);
									zz_Local = (zb_Local[idx_local0] - 1.5);
									xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = xx_Local1+2;
									yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = yy_Local1+2;
									zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = zz_Local1+2;

									for (x = xx_Local1; x <= xxn_Local1; x++)
									{
										lx = x - xb_Local[idx_local0];
										IB_interpo_listx[x - xx_Local1] = delta_3p(lx);
									}
									for (y = yy_Local1; y <= yyn_Local1; y++)
									{
										ly = y - yb_Local[idx_local0];
										IB_interpo_listy[y - yy_Local1] = delta_3p(ly);
									}
									for (z = zz_Local1; z <= zzn_Local1; z++)
									{
										lz = z - zb_Local[idx_local0];
										IB_interpo_listz[z - zz_Local1] = delta_3p(lz);
									}
									if (iter_k > 0)
									{
										for (x = xx_Local1; x <= xxn_Local1; x++)
										{
											IB_interpox = IB_interpo_listx[x - xx_Local1];
											for (y = yy_Local1; y <= yyn_Local1; y++)
											{
												IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
												for (z = zz_Local1; z <= zzn_Local1; z++)
												{
													IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
													IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

													z1 = z;
													if (periodic_z == 1)
													{
														if (z <= 1)   z1 = z + Nz;
														if (z >= Nz2)  z1 = z - Nz;
													}
													uf[idx_u0] += fluidu_temp[x][y][z1][0] * IB_interpoz;
													uf[idx_u0 + 1] += fluidu_temp[x][y][z1][1] * IB_interpoz;
													uf[idx_u0 + 2] += fluidu_temp[x][y][z1][2] * IB_interpoz;
												}
											}
										}
									}
									else
									{
										for (x = xx_Local1; x <= xxn_Local1; x++)
										{
											IB_interpox = IB_interpo_listx[x - xx_Local1];
											for (y = yy_Local1; y <= yyn_Local1; y++)
											{
												IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
												for (z = zz_Local1; z <= zzn_Local1; z++)
												{
													IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
													IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

													z1 = z;
													if (periodic_z == 1)
													{
														if (z <= 1)   z1 = z + Nz;
														if (z >= Nz2)  z1 = z - Nz;
													}
													uf[idx_u0] += u[x][y][z1][0] * IB_interpoz;
													uf[idx_u0 + 1] += u[x][y][z1][1] * IB_interpoz;
													uf[idx_u0 + 2] += u[x][y][z1][2] * IB_interpoz;
												}
											}
										}
									}
									force_IB[0] = f_coeffi * (uf[idx_u0] - up[idx_u0]) * LP_area_poly[rc_index][i];
									force_IB[1] = f_coeffi * (uf[idx_u0 + 1] - up[idx_u0 + 1]) * LP_area_poly[rc_index][i];
									force_IB[2] = f_coeffi * (uf[idx_u0 + 2] - up[idx_u0 + 2]) * LP_area_poly[rc_index][i];

#if IBM_DEBUG_OUTPUT_POLY
									// 1. 直接生成文件名 (每100步一个文件，例如 step_0-99_debug_poly_rank_0_thread_0.log)
									//    注意：step / 100 是整数除法
									sprintf(thread_filename, "IBM_lag_step_%03d-%03d_debug_poly_rank_%d_thread_%d.log",
										(step / 100) * 100, (step / 100) * 100 + 99, me, thread_id);

									// 2. 以追加模式 "a" 打开文件
									thread_debug_file = fopen(thread_filename, "a");

									if (thread_debug_file != NULL) {
										// 3. 写入信息 (全部使用 %.6f)
										fprintf(thread_debug_file,
											"Step=%d, Par_k=%d, LagPt=(%d,%d), "
											"Coord=(%.6f,%.6f,%.6f), "
											"uf=(%.6f,%.6f,%.6f), "
											"up=(%.6f,%.6f,%.6f), "
											"f_IB=(%.6f,%.6f,%.6f)\n",
											step, k, i, j,
											(xb_Local[idx_local0] - key_offsetx), (yb_Local[idx_local0] - key_offsety), (zb_Local[idx_local0] - 1.),
											uf[idx_u0], uf[idx_u0 + 1], uf[idx_u0 + 2],
											up[idx_u0], up[idx_u0 + 1], up[idx_u0 + 2],
											force_IB[0], force_IB[1], force_IB[2]);

										fclose(thread_debug_file); // 写完立即关闭，避免句柄泄漏
									}
#endif

									xx_Local2 = xx_Local1 - forcef_local_xmin;
									xxn_Local2 = xxn_Local1 - forcef_local_xmin;
									yy_Local2 = yy_Local1 - forcef_local_ymin;
									yyn_Local2 = yyn_Local1 - forcef_local_ymin;
									zz_Local2 = zz_Local1 - forcef_local_zmin;
									zzn_Local2 = zzn_Local1 - forcef_local_zmin;

									for (x = xx_Local2; x <= xxn_Local2; x++)
										for (y = yy_Local2; y <= yyn_Local2; y++)
											for (z = zz_Local2; z <= zzn_Local2; z++)
											{
												//lx = x - xb_Local[i][j]; ly = y - yb_Local[i][j]; lz = z - zb_Local[i][j];
												//IB_interpo = delta_3p(lx) * delta_3p(ly) * delta_3p(lz) * LP_dr;
												IB_interpo = IB_interpo_list[x - xx_Local2][y - yy_Local2][z - zz_Local2] * LP_dr;
												idx_forcef0 = ((x * forcef_local_size + y) * forcef_local_size + z) * 3;

												forcef_local[idx_forcef0] += force_IB[0] * IB_interpo;
												forcef_local[idx_forcef0 + 1] += force_IB[1] * IB_interpo;
												forcef_local[idx_forcef0 + 2] += force_IB[2] * IB_interpo;

											}

									if (xb_Local[idx_local0] >= 1.5 && xb_Local[idx_local0] <= (Nx + 1.5) && yb_Local[idx_local0] >= 1.5 && yb_Local[idx_local0] <= (Ny + 1.5))
									{
										F_IBM[k][0] += force_IB[0];
										F_IBM[k][1] += force_IB[1];
										F_IBM[k][2] += force_IB[2];
										T_IBM[k][0] += (force_IB[2] * yb_poly[rc_index][i][j] - force_IB[1] * zb_poly[rc_index][i][j]) * dx_poly;
										T_IBM[k][1] += (force_IB[0] * zb_poly[rc_index][i][j] - force_IB[2] * xb_poly[rc_index][i][j]) * dx_poly;
										T_IBM[k][2] += (force_IB[1] * xb_poly[rc_index][i][j] - force_IB[0] * yb_poly[rc_index][i][j]) * dx_poly;
									}
								}
							}
						if (is_single)
						{
							for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
							{
								x1 = x - forcef_local_xmin;
								for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
								{
									y1 = y - forcef_local_ymin;
									for (z1 = 0; z1 < forcef_local_size_temp; z1++)
									{
										z = z1 + forcef_local_zmin;
#if (periodic_z == 1)
										if (z <= 1)
										{
											z = z + Nz;
										}
										else if (z >= Nz2)
										{
											z = z - Nz;
										}
#endif
#if (periodic_z == 0)
										if (z >= Nz2 || z <= 1)
											continue;
#endif
#if (periodic_x == 0)
										if (me % NPROCx == 0)
										{
											if (x <= 1)
												continue;
										}
										if ((me + 1) % NPROCx == 0)
										{
											if (x >= Nx2)
												continue;
										}
#endif
#if (periodic_y == 0)
										if (me < NPROCx)
										{
											if (y <= 1)
												continue;
										}
										if (me >= (NPROCx * (NPROCy - 1)))
										{
											if (y >= Ny2)
												continue;
										}
#endif
										idx_forcef0 = ((x1 * forcef_local_size + y1) * forcef_local_size + z1) * 3;
										forcef[x][y][z][0] -= forcef_local[idx_forcef0];
										forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
										forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];

									}
								}
							}
						}
						else
						{
//#pragma omp critical
							{
								for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
								{
									x1 = x - forcef_local_xmin;
									for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
									{
										y1 = y - forcef_local_ymin;
										for (z1 = 0; z1 < forcef_local_size_temp; z1++)
										{
											z = z1 + forcef_local_zmin;
#if (periodic_z == 1)
											if (z <= 1)
											{
												z = z + Nz;
											}
											else if (z >= Nz2)
											{
												z = z - Nz;
											}
#endif
#if (periodic_z == 0)
											if (z >= Nz2 || z <= 1)
												continue;
#endif
#if (periodic_x == 0)
											if (me % NPROCx == 0)
											{
												if (x <= 1)
													continue;
											}
											if ((me + 1) % NPROCx == 0)
											{
												if (x >= Nx2)
													continue;
											}
#endif
#if (periodic_y == 0)
											if (me < NPROCx)
											{
												if (y <= 1)
													continue;
											}
											if (me >= (NPROCx * (NPROCy - 1)))
											{
												if (y >= Ny2)
													continue;
											}
#endif
											idx_forcef0 = ((x1 * forcef_local_size + y1) * forcef_local_size + z1) * 3;
#pragma omp atomic
											forcef[x][y][z][0] -= forcef_local[idx_forcef0];
#pragma omp atomic
											forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
#pragma omp atomic
											forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];

										}
									}
								}
							}
						}
					}
				}
			}//end parallel for

#if (NF >1)
			if (iter_k < NF - 1)
			{
				for (kk = 0; kk < np_local_poly; kk++)
				{
					if (num_p_poly[kk] >= 0)
					{
						k = num_p_poly[kk];
						rc_index = rc_poly[k];
						rb_temp = rb_poly[rc_index];
						xc_key_temp[0] = xc[k][0];
						xc_key_temp[1] = xc[k][1];
						xc_key_temp[2] = xc[k][2];
						xc_key[0] = xc_key_temp[0] + key_offsetx;
						xc_key[1] = xc_key_temp[1] + key_offsety;
						xc_key[2] = xc_key_temp[2] + 1.0;

						forcef_local_xmin = (int)((0. > floor(xc_key[0] - rb_temp - 1.5)) ? 0 : floor(xc_key[0] - rb_temp - 1.5));
						forcef_local_xmax = (int)((Nx3 < ceil(xc_key[0] + rb_temp + 1.5)) ? Nx3 : ceil(xc_key[0] + rb_temp + 1.5));
						forcef_local_ymin = (int)((0. > floor(xc_key[1] - rb_temp - 1.5)) ? 0 : floor(xc_key[1] - rb_temp - 1.5));
						forcef_local_ymax = (int)((Ny3 < ceil(xc_key[1] + rb_temp + 1.5)) ? Ny3 : ceil(xc_key[1] + rb_temp + 1.5));
						forcef_local_zmin = (int)(floor(xc_key[2] - rb_temp - 1.5));
						forcef_local_zmax = (int)(ceil(xc_key[2] + rb_temp + 1.5));
#pragma omp for schedule(static) collapse(3) private(x,y,z,z1,i)
						for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
						{
							for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
							{
								for (z = forcef_local_zmin; z <= forcef_local_zmax; z++)
								{
									z1 = z;
									if (periodic_z == 1)
									{
										if (z1 <= 1)
											z1 += Nz;
										if (z1 >= Nz2)
											z1 -= Nz;
									}
									for (i = 0; i < 3; i++)
									{
										fluidu_temp[x][y][z1][i] = u[x][y][z1][i] + forcef[x][y][z1][i] * dt / (2 * rhof);
									}

								}
							}
						}

					}
				}
			}
#pragma omp barrier

#pragma omp master
			{
				MPI_Sendrecv(&fluidu_temp[Nx][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, right_ex, 100,
					&fluidu_temp[0][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, left_ex, 100, MPI_COMM_WORLD, &status);
				MPI_Sendrecv(&fluidu_temp[2][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, left_ex, 120,
					&fluidu_temp[Nx2][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, right_ex, 120, MPI_COMM_WORLD, &status);
			}
#pragma omp barrier
#pragma omp for collapse(2) private(x, z, j)
			for (x = 0; x <= Nx3; x++)
			{
				for (z = 0; z <= Nz3; z++)
				{
					for (j = 0; j < 3; j++)
					{
						fluidu_temp_ex[2][x][z][j] = fluidu_temp[x][2][z][j];
						fluidu_temp_ex[3][x][z][j] = fluidu_temp[x][3][z][j];
						fluidu_temp_ex[4][x][z][j] = fluidu_temp[x][Ny][z][j];
						fluidu_temp_ex[5][x][z][j] = fluidu_temp[x][Ny1][z][j];
					}
				}
			}
#pragma omp barrier
#pragma omp master
			{
				MPI_Sendrecv(&fluidu_temp_ex[4][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, up_ex, 140,
					&fluidu_temp_ex[0][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, low_ex, 140, MPI_COMM_WORLD, &status);
				MPI_Sendrecv(&fluidu_temp_ex[2][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, low_ex, 160,
					&fluidu_temp_ex[6][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, up_ex, 160, MPI_COMM_WORLD, &status);
			}
#pragma omp barrier
#pragma omp for collapse(2) private(x, z, j)
			for (x = 0; x <= Nx3; x++)
			{
				for (z = 0; z <= Nz3; z++)
				{
					for (j = 0; j < 3; j++)
					{
						if (low_ex != MPI_PROC_NULL)
						{
							fluidu_temp[x][0][z][j] = fluidu_temp_ex[0][x][z][j];
							fluidu_temp[x][1][z][j] = fluidu_temp_ex[1][x][z][j];
						}
						if (up_ex != MPI_PROC_NULL)
						{
							fluidu_temp[x][Ny2][z][j] = fluidu_temp_ex[6][x][z][j];
							fluidu_temp[x][Ny3][z][j] = fluidu_temp_ex[7][x][z][j];
						}
					}
				}
			}

			if (periodic_z == 1)
			{
#pragma omp for collapse(2) private(x, y, j)
				for (x = 0; x <= Nx3; x++)
				{
					for (y = 0; y <= Ny3; y++)
					{
						for (j = 0; j < 3; j++)
						{
							fluidu_temp[x][y][0][j] = fluidu_temp[x][y][Nz][j];
							fluidu_temp[x][y][1][j] = fluidu_temp[x][y][Nz1][j];
							fluidu_temp[x][y][Nz2][j] = fluidu_temp[x][y][2][j];
							fluidu_temp[x][y][Nz3][j] = fluidu_temp[x][y][3][j];
						}
					}
				}
			}
#pragma omp barrier
#endif 
		}//end NF iter
		free(uf);
		free(up);
		free(xb_Local);
		free(yb_Local);
		free(zb_Local);
		free(forcef_local);

#pragma omp for schedule(static) private(k, kk, i)
		for (kk = 0; kk < np_local_poly; kk++)
		{
			if (num_p_poly[kk] >= 0)
			{
				k = num_p_poly[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] *= LP_dr * (dx_poly * dx_poly * dx_poly);
					T_IBM[k][i] *= LP_dr * (dx_poly * dx_poly * dx_poly);
				}
			}
		}
#pragma omp barrier
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void IBM_Particle_poly_less_forcef_local3_MDF(int me)
{
	MPI_Status status;

	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int i, j, k, kk, x, y, z, x1, y1, z1;
	double lx, ly, lz;
	//double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];  //���ϵ��������ٶȣ������ٶȣ�IB����ʱ����
	double xc_key[3];
	double force_IB[3];
	double xx_Local, yy_Local, zz_Local, xxn_Local, yyn_Local, zzn_Local;
	double IB_interpo, temp;
	double xx_flag, yy_flag, zz_flag, xxn_flag, yyn_flag, zzn_flag;
	int ns_temp, rc_index;
	double rb_temp;
	int xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1;
	double IB_interpox, IB_interpoy, IB_interpoz;
	int IB_interpo_list_size, forcef_local_size, forcef_local_size_temp;
	int xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2;
	int forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax;
	double f_coeffi = 2 * rhof / dt;

	int flat_size1 = ns_max * nt1_max * 3;
	int flat_size2 = ns_max * nt1_max;
	int idx_u0, idx_local0, idx_forcef0;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;
	const double key_offsetx = 1. - (double)(me_x) * (double)(Nx);
	const double key_offsety = 1. - (double)(me_y) * (double)(Ny);

	IB_interpo_list_size = 3;//4 for delta_3p func
	forcef_local_size = int(2 * rb_poly_max) + 6;//plus 4 for delta_3p func

	//for utemp exchange
	int low_ex, up_ex, right_ex, left_ex;
	double fluidu_temp_ex[8][Nx4][Nz4][3];

	if (periodic_x == 1)
	{
		if (me % NPROCx == 0)
			left_ex = me + NPROCx - 1;
		else
			left_ex = me - 1;
		if ((me + 1) % NPROCx == 0)
			right_ex = me - NPROCx + 1;
		else
			right_ex = me + 1;
	}
	else if (periodic_x == 0)
	{
		if (me % NPROCx == 0)
			left_ex = MPI_PROC_NULL;
		else
			left_ex = me - 1;
		if ((me + 1) % NPROCx == 0)
			right_ex = MPI_PROC_NULL;
		else
			right_ex = me + 1;
	}

	if (periodic_y == 1)
	{
		if (me < NPROCx)
			low_ex = me + NPROCx * (NPROCy - 1);
		else
			low_ex = me - NPROCx;
		if (me >= (NPROCx * (NPROCy - 1)))
			up_ex = me - NPROCx * (NPROCy - 1);
		else
			up_ex = me + NPROCx;
	}
	else if (periodic_y == 0)
	{
		if (me < NPROCx)
			low_ex = MPI_PROC_NULL;
		else
			low_ex = me - NPROCx;
		if (me >= (NPROCx * (NPROCy - 1)))
			up_ex = MPI_PROC_NULL;
		else
			up_ex = me + NPROCx;
	}

/* MDF 速度更新阶段：所有线程共同处理同一颗粒，完成后再进入下一颗粒。
 * private 只隔离颗粒号和范围临时量，不改变颗粒更新顺序或工作共享方式。 */
#pragma omp parallel private(k,kk,rc_index,rb_temp,\
				forcef_local_xmin,forcef_local_xmax,forcef_local_ymin,forcef_local_ymax,forcef_local_zmin,forcef_local_zmax)
	{
		double xc_key[3];
		volatile double xc_key_temp[3];
		double force_IB[3];
		//double uf[ns_max][nt1_max][3], up[ns_max][nt1_max][3], xb_Local[ns_max][nt1_max], yb_Local[ns_max][nt1_max], zb_Local[ns_max][nt1_max];
		double* uf = (double*)malloc(flat_size1 * sizeof(double));
		double* up = (double*)malloc(flat_size1 * sizeof(double));
		double* xb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* yb_Local = (double*)malloc(flat_size2 * sizeof(double));
		double* zb_Local = (double*)malloc(flat_size2 * sizeof(double));
		int* zb1 = (int*)malloc(flat_size2 * sizeof(int));

		int num_threads = omp_get_num_threads();
		int thread_id = omp_get_thread_num();
		bool is_single = (num_threads == 1);

		// --- 调试日志变量声明 ---
#if IBM_DEBUG_OUTPUT_POLY
		char thread_filename[256];
		FILE* thread_debug_file = NULL;
#endif
		int iter_k;
		double IB_interpo_list[IB_interpo_list_size][IB_interpo_list_size][IB_interpo_list_size];
		double IB_interpo_listx[IB_interpo_list_size], IB_interpo_listy[IB_interpo_list_size], IB_interpo_listz[IB_interpo_list_size];
		//double forcef_local[forcef_local_size][forcef_local_size][forcef_local_size][3];
		int forcef_local_total_size = forcef_local_size * forcef_local_size * forcef_local_size * 3;
		double* forcef_local = (double*)malloc(forcef_local_total_size * sizeof(double));
		/* 每线程一份多字掩码，各轮、各颗粒复用；支持边长超过 64。 */
		size_t zmask_words_per_row = ((size_t)forcef_local_size + 63) / 64;
		size_t zmask_total_words = (size_t)forcef_local_size * forcef_local_size * zmask_words_per_row;
		uint64_t* zmask = (uint64_t*)malloc(zmask_total_words * sizeof(uint64_t));

#pragma omp for schedule(static) private(k, kk, i)
		for (kk = 0; kk < np_local_poly; kk++)
		{
			if (num_p_poly[kk] >= 0)
			{
				k = num_p_poly[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] = 0.0;	T_IBM[k][i] = 0.0;
				}
			}
		}
		for (iter_k = 0; iter_k < NF; iter_k++)
		{
#pragma omp for schedule(dynamic,2) private(i,j,k,kk,x,y,z,x1,y1,z1,lx,ly,lz,IB_interpo,temp,\
												rc_index,rb_temp,ns_temp,xx_Local,yy_Local,zz_Local,xxn_Local,yyn_Local,zzn_Local,\
											xx_Local1, yy_Local1, zz_Local1, xxn_Local1, yyn_Local1, zzn_Local1,\
											IB_interpox,IB_interpoy,IB_interpoz,\
											forcef_local_xmin, forcef_local_xmax, forcef_local_ymin, forcef_local_ymax, forcef_local_zmin, forcef_local_zmax,\
											xx_Local2, yy_Local2, zz_Local2, xxn_Local2, yyn_Local2, zzn_Local2,forcef_local_size_temp,\
											idx_u0, idx_local0, idx_forcef0)
			for (kk = 0; kk < np_local_poly; kk++)
			{
				if (num_p_poly[kk] >= 0)
				{
					k = num_p_poly[kk];
					rc_index = rc_poly[k];
					rb_temp = rb_poly[rc_index];
					ns_temp = (int)(PI * rb_temp / 0.8 + 1);

					if ((xc[k][0] > (me_x * Nx - rb_temp - 1.5)) && (xc[k][0] < ((me_x + 1) * Nx + rb_temp + 1.5)) && (xc[k][1] > (me_y * Ny - rb_temp - 1.5)) && (xc[k][1] < ((me_y + 1) * Ny + rb_temp + 1.5)))
					{
						xc_key_temp[0] = xc[k][0];
						xc_key_temp[1] = xc[k][1];
						xc_key_temp[2] = xc[k][2];
						xc_key[0] = xc_key_temp[0] + key_offsetx;
						xc_key[1] = xc_key_temp[1] + key_offsety;
						xc_key[2] = xc_key_temp[2] + 1.0;

						forcef_local_size_temp = int(2 * rb_temp) + 6;

						memset(forcef_local, 0, forcef_local_total_size * sizeof(double));
						memset(zb1, 0, flat_size2 * sizeof(int));
						memset(zmask, 0, zmask_total_words * sizeof(uint64_t));

						forcef_local_xmin = int(xc_key[0] - rb_temp - 1.5) - 1;
						forcef_local_ymin = int(xc_key[1] - rb_temp - 1.5) - 1;
						forcef_local_zmin = int(xc_key[2] - rb_temp - 1.5) - 1;
						forcef_local_xmax = forcef_local_xmin + forcef_local_size_temp - 1;
						forcef_local_ymax = forcef_local_ymin + forcef_local_size_temp - 1;
						forcef_local_zmax = forcef_local_zmin + forcef_local_size_temp - 1;

						forcef_local_xmin = (forcef_local_xmin < 0) ? 0 : forcef_local_xmin;
						forcef_local_ymin = (forcef_local_ymin < 0) ? 0 : forcef_local_ymin;
						forcef_local_xmax = (forcef_local_xmax > Nx3) ? Nx3 : forcef_local_xmax;
						forcef_local_ymax = (forcef_local_ymax > Ny3) ? Ny3 : forcef_local_ymax;


						for (i = 0; i < ns_temp; i++)
							for (j = 0; j < nt_poly[rc_index][i]; j++)
							{
								idx_local0 = i * nt1_max + j;
								xb_Local[idx_local0] = xc_key[0] + xb_poly[rc_index][i][j];
								yb_Local[idx_local0] = xc_key[1] + yb_poly[rc_index][i][j];
								zb_Local[idx_local0] = xc_key[2] + zb_poly[rc_index][i][j];
								/* u 与 fluidu_temp 共用该周期映射，内层不再逐节点判断。 */
								if (periodic_z == 1)
								{
									if (zb_Local[idx_local0] <= 1)   zb1[idx_local0] = Nz;
									if (zb_Local[idx_local0] >= Nz2)  zb1[idx_local0] = -Nz;
								}

								if (xb_Local[idx_local0] >= 0.5 && xb_Local[idx_local0] <= (Nx + 2.5) && yb_Local[idx_local0] >= 0.5 && yb_Local[idx_local0] <= (Ny + 2.5))
								{
									idx_u0 = (i * nt1_max + j) * 3;
									up[idx_u0] = uc[k][0] + (wc[k][2] * (-yb_poly[rc_index][i][j]) + wc[k][1] * (zb_poly[rc_index][i][j])) * dx_poly;
									up[idx_u0 + 1] = uc[k][1] + (wc[k][2] * (xb_poly[rc_index][i][j]) + wc[k][0] * (-zb_poly[rc_index][i][j])) * dx_poly;
									up[idx_u0 + 2] = uc[k][2] + (wc[k][0] * (yb_poly[rc_index][i][j]) + wc[k][1] * (-xb_poly[rc_index][i][j])) * dx_poly;
									uf[idx_u0] = 0.0; uf[idx_u0 + 1] = 0.0; uf[idx_u0 + 2] = 0.0;
									
									xx_Local = (xb_Local[idx_local0] - 1.5);
									yy_Local = (yb_Local[idx_local0] - 1.5);
									zz_Local = (zb_Local[idx_local0] + zb1[idx_local0] - 1.5);
									xx_Local1 = int(ceil(xx_Local)); xxn_Local1 = xx_Local1+2;
									yy_Local1 = int(ceil(yy_Local)); yyn_Local1 = yy_Local1+2;
									zz_Local1 = int(ceil(zz_Local)); zzn_Local1 = zz_Local1+2;

									/* 各方向三个权重共用一次平方根。 */
									lx = xb_Local[idx_local0] - xx_Local1 - 1;
									delta_3p_less(lx, IB_interpo_listx);
									ly = yb_Local[idx_local0] - yy_Local1 - 1;
									delta_3p_less(ly, IB_interpo_listy);
									lz = zb_Local[idx_local0] - zz_Local1 - 1 + zb1[idx_local0];
									delta_3p_less(lz, IB_interpo_listz);
									if (iter_k > 0)
									{
										for (x = xx_Local1; x <= xxn_Local1; x++)
										{
											IB_interpox = IB_interpo_listx[x - xx_Local1];
											for (y = yy_Local1; y <= yyn_Local1; y++)
											{
												IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
												for (z = zz_Local1; z <= zzn_Local1; z++)
												{
													IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
													IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

													z1 = z;

													uf[idx_u0] += fluidu_temp[x][y][z1][0] * IB_interpoz;
													uf[idx_u0 + 1] += fluidu_temp[x][y][z1][1] * IB_interpoz;
													uf[idx_u0 + 2] += fluidu_temp[x][y][z1][2] * IB_interpoz;
												}
											}
										}
									}
									else
									{
										for (x = xx_Local1; x <= xxn_Local1; x++)
										{
											IB_interpox = IB_interpo_listx[x - xx_Local1];
											for (y = yy_Local1; y <= yyn_Local1; y++)
											{
												IB_interpoy = IB_interpox * IB_interpo_listy[y - yy_Local1];
												for (z = zz_Local1; z <= zzn_Local1; z++)
												{
													IB_interpoz = IB_interpoy * IB_interpo_listz[z - zz_Local1];
													IB_interpo_list[x - xx_Local1][y - yy_Local1][z - zz_Local1] = IB_interpoz;

													z1 = z;

													uf[idx_u0] += u[x][y][z1][0] * IB_interpoz;
													uf[idx_u0 + 1] += u[x][y][z1][1] * IB_interpoz;
													uf[idx_u0 + 2] += u[x][y][z1][2] * IB_interpoz;
												}
											}
										}
									}
									force_IB[0] = f_coeffi * (uf[idx_u0] - up[idx_u0]) * LP_area_poly[rc_index][i];
									force_IB[1] = f_coeffi * (uf[idx_u0 + 1] - up[idx_u0 + 1]) * LP_area_poly[rc_index][i];
									force_IB[2] = f_coeffi * (uf[idx_u0 + 2] - up[idx_u0 + 2]) * LP_area_poly[rc_index][i];

#if IBM_DEBUG_OUTPUT_POLY
									// 1. 直接生成文件名 (每100步一个文件，例如 step_0-99_debug_poly_rank_0_thread_0.log)
									//    注意：step / 100 是整数除法
									sprintf(thread_filename, "IBM_lag_step_%03d-%03d_debug_poly_rank_%d_thread_%d.log",
										(step / 100) * 100, (step / 100) * 100 + 99, me, thread_id);

									// 2. 以追加模式 "a" 打开文件
									thread_debug_file = fopen(thread_filename, "a");

									if (thread_debug_file != NULL) {
										// 3. 写入信息 (全部使用 %.6f)
										fprintf(thread_debug_file,
											"Step=%d, Par_k=%d, LagPt=(%d,%d), "
											"Coord=(%.6f,%.6f,%.6f), "
											"uf=(%.6f,%.6f,%.6f), "
											"up=(%.6f,%.6f,%.6f), "
											"f_IB=(%.6f,%.6f,%.6f)\n",
											step, k, i, j,
											(xb_Local[idx_local0] - key_offsetx), (yb_Local[idx_local0] - key_offsety), (zb_Local[idx_local0] - 1.),
											uf[idx_u0], uf[idx_u0 + 1], uf[idx_u0 + 2],
											up[idx_u0], up[idx_u0 + 1], up[idx_u0 + 2],
											force_IB[0], force_IB[1], force_IB[2]);

										fclose(thread_debug_file); // 写完立即关闭，避免句柄泄漏
									}
#endif

									xx_Local2 = xx_Local1 - forcef_local_xmin;
									xxn_Local2 = xxn_Local1 - forcef_local_xmin;
									yy_Local2 = yy_Local1 - forcef_local_ymin;
									yyn_Local2 = yyn_Local1 - forcef_local_ymin;
									zz_Local2 = zz_Local1 - forcef_local_zmin - zb1[idx_local0];
									zzn_Local2 = zzn_Local1 - forcef_local_zmin - zb1[idx_local0];

									size_t zmask_word;
									unsigned int zmask_bit;
									uint64_t zmask_low, zmask_high;
									zmask_word = (size_t)zz_Local2 >> 6;
									zmask_bit = (unsigned int)zz_Local2 & 63u;
									zmask_low = (uint64_t)7 << zmask_bit;
									zmask_high = 0;
									if (zmask_bit >= 62u)
										zmask_high = (uint64_t)7 >> (64u - zmask_bit);

									for (x = xx_Local2; x <= xxn_Local2; x++)
										for (y = yy_Local2; y <= yyn_Local2; y++)
										{
											uint64_t* zmask_row;
											zmask_row = zmask + ((size_t)x * forcef_local_size + y) * zmask_words_per_row;
											zmask_row[zmask_word] |= zmask_low;
											if (zmask_high != 0)
												zmask_row[zmask_word + 1] |= zmask_high;

											for (z = zz_Local2; z <= zzn_Local2; z++)
											{
												IB_interpo = IB_interpo_list[x - xx_Local2][y - yy_Local2][z - zz_Local2] * LP_dr;
												idx_forcef0 = ((x * forcef_local_size + y) * forcef_local_size + z) * 3;
												forcef_local[idx_forcef0] += force_IB[0] * IB_interpo;
												forcef_local[idx_forcef0+1] += force_IB[1] * IB_interpo;
												forcef_local[idx_forcef0+2] += force_IB[2] * IB_interpo;
											}
										}
									if (xb_Local[idx_local0] >= 1.5 && xb_Local[idx_local0] <= (Nx + 1.5) && yb_Local[idx_local0] >= 1.5 && yb_Local[idx_local0] <= (Ny + 1.5))
									{
										F_IBM[k][0] += force_IB[0];
										F_IBM[k][1] += force_IB[1];
										F_IBM[k][2] += force_IB[2];
										T_IBM[k][0] += (force_IB[2] * yb_poly[rc_index][i][j] - force_IB[1] * zb_poly[rc_index][i][j]) * dx_poly;
										T_IBM[k][1] += (force_IB[0] * zb_poly[rc_index][i][j] - force_IB[2] * xb_poly[rc_index][i][j]) * dx_poly;
										T_IBM[k][2] += (force_IB[1] * xb_poly[rc_index][i][j] - force_IB[0] * yb_poly[rc_index][i][j]) * dx_poly;
									}
								}
							}
						size_t active_z_words;	
						unsigned int active_z_tail;
						uint64_t last_z_mask;

						active_z_words = ((size_t)forcef_local_size_temp + 63) / 64;
						active_z_tail = (unsigned int)forcef_local_size_temp & 63u;
						last_z_mask = ~(uint64_t)0;

						/* 有效长度正好是 64 的整数倍时保留整字，避免左移 64 位。 */
						if (active_z_tail != 0)
							last_z_mask = ((uint64_t)1 << active_z_tail) - 1;

						if (is_single)
						{
							for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
							{
								x1 = x - forcef_local_xmin;
								for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
								{
									size_t local_row, force_row, mask_word_id;
									const uint64_t* zmask_row;

									y1 = y - forcef_local_ymin;
									local_row = (size_t)x1 * forcef_local_size + y1;
									force_row = local_row * forcef_local_size * 3;
									zmask_row = zmask + local_row * zmask_words_per_row;

									/* 只扫描当前颗粒使用的字；数组寻址仍用最大容量步长。 */
									for (mask_word_id = 0; mask_word_id < active_z_words; mask_word_id++)
									{
										uint64_t mask_word;
										mask_word = zmask_row[mask_word_id];
										if (mask_word_id + 1 == active_z_words)
											mask_word &= last_z_mask;

										while (mask_word != 0)
										{
											unsigned int bit;

											/* 输入非零时才调用 ctzll；逐次去掉最低一个置位。 */
											bit = (unsigned int)__builtin_ctzll((unsigned long long)mask_word);
											mask_word &= mask_word - 1;
											z1 = (int)(mask_word_id * 64 + bit);

											/* 标记属于未映射的局部节点，恢复全局 z 后再处理周期性。 */
											z = z1 + forcef_local_zmin;
											if (periodic_z == 1)
											{
												if (z <= 1)
													z += Nz;
												else if (z >= Nz2)
													z -= Nz;
											}
#if (periodic_z == 0)
											if (z >= Nz2 || z <= 1)
												continue;
#endif
#if (periodic_x == 0)
											if (me % NPROCx == 0)
											{
												if (x <= 1)
													continue;
											}
											if ((me + 1) % NPROCx == 0)
											{
												if (x >= Nx2)
													continue;
											}
#endif
#if (periodic_y == 0)
											if (me < NPROCx)
											{
												if (y <= 1)
													continue;
											}
											if (me >= (NPROCx * (NPROCy - 1)))
											{
												if (y >= Ny2)
													continue;
											}
#endif
											idx_forcef0 = force_row + (size_t)z1 * 3;
											forcef[x][y][z][0] -= forcef_local[idx_forcef0];
											forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
											forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];
										}
									}
								}
							}
						}
						else
						{
							{
								for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
								{
									x1 = x - forcef_local_xmin;
									for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
									{
										size_t local_row, force_row, mask_word_id;
										const uint64_t* zmask_row;

										y1 = y - forcef_local_ymin;
										local_row = (size_t)x1 * forcef_local_size + y1;
										force_row = local_row * forcef_local_size * 3;
										zmask_row = zmask + local_row * zmask_words_per_row;

										/* 只扫描当前颗粒使用的字；数组寻址仍用最大容量步长。 */
										for (mask_word_id = 0; mask_word_id < active_z_words; mask_word_id++)
										{
											uint64_t mask_word;
											mask_word = zmask_row[mask_word_id];
											if (mask_word_id + 1 == active_z_words)
												mask_word &= last_z_mask;

											while (mask_word != 0)
											{
												unsigned int bit;

												/* 输入非零时才调用 ctzll；逐次去掉最低一个置位。 */
												bit = (unsigned int)__builtin_ctzll((unsigned long long)mask_word);
												mask_word &= mask_word - 1;
												z1 = (int)(mask_word_id * 64 + bit);

												/* 标记属于未映射的局部节点，恢复全局 z 后再处理周期性。 */
												z = z1 + forcef_local_zmin;
												if (periodic_z == 1)
												{
													if (z <= 1)
														z += Nz;
													else if (z >= Nz2)
														z -= Nz;
												}
#if (periodic_z == 0)
												if (z >= Nz2 || z <= 1)
													continue;
#endif
#if (periodic_x == 0)
												if (me % NPROCx == 0)
												{
													if (x <= 1)
														continue;
												}
												if ((me + 1) % NPROCx == 0)
												{
													if (x >= Nx2)
														continue;
												}
#endif
#if (periodic_y == 0)
												if (me < NPROCx)
												{
													if (y <= 1)
														continue;
												}
												if (me >= (NPROCx * (NPROCy - 1)))
												{
													if (y >= Ny2)
														continue;
												}
#endif
												idx_forcef0 = force_row + (size_t)z1 * 3;
#pragma omp atomic
												forcef[x][y][z][0] -= forcef_local[idx_forcef0];
#pragma omp atomic
												forcef[x][y][z][1] -= forcef_local[idx_forcef0 + 1];
#pragma omp atomic
												forcef[x][y][z][2] -= forcef_local[idx_forcef0 + 2];
											}
										}
									}
								}
							}
						}
					}
				}
			}//end parallel for

#if (NF >1)
			if (iter_k < NF - 1)
			{
				for (kk = 0; kk < np_local_poly; kk++)
				{
					if (num_p_poly[kk] >= 0)
					{
						k = num_p_poly[kk];
						rc_index = rc_poly[k];
						rb_temp = rb_poly[rc_index];
						xc_key_temp[0] = xc[k][0];
						xc_key_temp[1] = xc[k][1];
						xc_key_temp[2] = xc[k][2];
						xc_key[0] = xc_key_temp[0] + key_offsetx;
						xc_key[1] = xc_key_temp[1] + key_offsety;
						xc_key[2] = xc_key_temp[2] + 1.0;

						forcef_local_xmin = (int)((0. > floor(xc_key[0] - rb_temp - 1.5)) ? 0 : floor(xc_key[0] - rb_temp - 1.5));
						forcef_local_xmax = (int)((Nx3 < ceil(xc_key[0] + rb_temp + 1.5)) ? Nx3 : ceil(xc_key[0] + rb_temp + 1.5));
						forcef_local_ymin = (int)((0. > floor(xc_key[1] - rb_temp - 1.5)) ? 0 : floor(xc_key[1] - rb_temp - 1.5));
						forcef_local_ymax = (int)((Ny3 < ceil(xc_key[1] + rb_temp + 1.5)) ? Ny3 : ceil(xc_key[1] + rb_temp + 1.5));
						forcef_local_zmin = (int)(floor(xc_key[2] - rb_temp - 1.5));
						forcef_local_zmax = (int)(ceil(xc_key[2] + rb_temp + 1.5));
#pragma omp for schedule(static) collapse(3) private(x,y,z,z1,i)
						for (x = forcef_local_xmin; x <= forcef_local_xmax; x++)
						{
							for (y = forcef_local_ymin; y <= forcef_local_ymax; y++)
							{
								for (z = forcef_local_zmin; z <= forcef_local_zmax; z++)
								{
									z1 = z;
									if (periodic_z == 1)
									{
										if (z1 <= 1)
											z1 += Nz;
										if (z1 >= Nz2)
											z1 -= Nz;
									}
									for (i = 0; i < 3; i++)
									{
										fluidu_temp[x][y][z1][i] = u[x][y][z1][i] + forcef[x][y][z1][i] * dt / (2 * rhof);
									}

								}
							}
						}

					}
				}
			}
#pragma omp barrier

#pragma omp master
			{
				MPI_Sendrecv(&fluidu_temp[Nx][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, right_ex, 100,
					&fluidu_temp[0][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, left_ex, 100, MPI_COMM_WORLD, &status);
				MPI_Sendrecv(&fluidu_temp[2][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, left_ex, 120,
					&fluidu_temp[Nx2][0][0][0], 2 * Ny4 * Nz4 * 3, MPI_DOUBLE, right_ex, 120, MPI_COMM_WORLD, &status);
			}
#pragma omp barrier
#pragma omp for collapse(2) private(x, z, j)
			for (x = 0; x <= Nx3; x++)
			{
				for (z = 0; z <= Nz3; z++)
				{
					for (j = 0; j < 3; j++)
					{
						fluidu_temp_ex[2][x][z][j] = fluidu_temp[x][2][z][j];
						fluidu_temp_ex[3][x][z][j] = fluidu_temp[x][3][z][j];
						fluidu_temp_ex[4][x][z][j] = fluidu_temp[x][Ny][z][j];
						fluidu_temp_ex[5][x][z][j] = fluidu_temp[x][Ny1][z][j];
					}
				}
			}
#pragma omp barrier
#pragma omp master
			{
				MPI_Sendrecv(&fluidu_temp_ex[4][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, up_ex, 140,
					&fluidu_temp_ex[0][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, low_ex, 140, MPI_COMM_WORLD, &status);
				MPI_Sendrecv(&fluidu_temp_ex[2][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, low_ex, 160,
					&fluidu_temp_ex[6][0][0][0], 2 * Nx4 * Nz4 * 3, MPI_DOUBLE, up_ex, 160, MPI_COMM_WORLD, &status);
			}
#pragma omp barrier
#pragma omp for collapse(2) private(x, z, j)
			for (x = 0; x <= Nx3; x++)
			{
				for (z = 0; z <= Nz3; z++)
				{
					for (j = 0; j < 3; j++)
					{
						if (low_ex != MPI_PROC_NULL)
						{
							fluidu_temp[x][0][z][j] = fluidu_temp_ex[0][x][z][j];
							fluidu_temp[x][1][z][j] = fluidu_temp_ex[1][x][z][j];
						}
						if (up_ex != MPI_PROC_NULL)
						{
							fluidu_temp[x][Ny2][z][j] = fluidu_temp_ex[6][x][z][j];
							fluidu_temp[x][Ny3][z][j] = fluidu_temp_ex[7][x][z][j];
						}
					}
				}
			}

			if (periodic_z == 1)
			{
#pragma omp for collapse(2) private(x, y, j)
				for (x = 0; x <= Nx3; x++)
				{
					for (y = 0; y <= Ny3; y++)
					{
						for (j = 0; j < 3; j++)
						{
							fluidu_temp[x][y][0][j] = fluidu_temp[x][y][Nz][j];
							fluidu_temp[x][y][1][j] = fluidu_temp[x][y][Nz1][j];
							fluidu_temp[x][y][Nz2][j] = fluidu_temp[x][y][2][j];
							fluidu_temp[x][y][Nz3][j] = fluidu_temp[x][y][3][j];
						}
					}
				}
			}
#pragma omp barrier
#endif 
		}//end NF iter
		free(uf);
		free(up);
		free(xb_Local);
		free(yb_Local);
		free(zb_Local);
		free(forcef_local);
		free(zb1);
		free(zmask);

#pragma omp for schedule(static) private(k, kk, i)
		for (kk = 0; kk < np_local_poly; kk++)
		{
			if (num_p_poly[kk] >= 0)
			{
				k = num_p_poly[kk];
				for (i = 0; i < 3; i++)
				{
					F_IBM[k][i] *= LP_dr * (dx_poly * dx_poly * dx_poly);
					T_IBM[k][i] *= LP_dr * (dx_poly * dx_poly * dx_poly);
				}
			}
		}
#pragma omp barrier
	}//end openmp parallel

	MPI_Barrier(MPI_COMM_WORLD);
}

void Particle_IB_comm(int me)
{
	MPI_Status status;

	int x, y, i, j, k;
	int kk1, kk2, kk3, kk4, kk5, kk6, kk7, kk8;
	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int x_p0, y_p0;
	//4����
	double pIB1_comm[np_comm_xface][7];
	double pIB3_comm[np_comm_yface][7];
	double pIB5_comm[np_comm_xface][7];
	double pIB7_comm[np_comm_yface][7];
	//4����
	double pIB2_comm[np_comm_corner][7];
	double pIB4_comm[np_comm_corner][7];
	double pIB6_comm[np_comm_corner][7];
	double pIB8_comm[np_comm_corner][7];

	//ͨѶ��ʶ��
	int msgtag[8];

	me_y = int(me / NPROCx);
	me_x = me - me_y*NPROCx;

	//��һ����ַ�洢�����ֲ���ţ���ʼ��Ϊ-1
	for (k = 0; k < np_comm_xface; k++)
	{
		pIB1_comm[k][0] = -1.0;
		pIB5_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_yface; k++)
	{
		pIB3_comm[k][0] = -1.0;
		pIB7_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_corner; k++)
	{
		pIB2_comm[k][0] = -1.0;
		pIB4_comm[k][0] = -1.0;
		pIB6_comm[k][0] = -1.0;
		pIB8_comm[k][0] = -1.0;
	}

	kk1 = 0; kk2 = 0; kk3 = 0; kk4 = 0; kk5 = 0; kk6 = 0; kk7 = 0; kk8 = 0;
	//ͳ�Ƹ���������Ҫͨ�ŵĿ��������������뻺����
	for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
	{
		if (num_p[k] >= 0)
		{
			j = num_p[k];
			//Prepare for sending to left,0
			if ((xc[j][0] < xx) && (xc[j][0] > xx4) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
			{
				pIB1_comm[kk1][0] = 1.0*j;
				for (i = 0; i < 3; i++)
				{
					pIB1_comm[kk1][i + 1] = F_IBM[j][i];
					pIB1_comm[kk1][i + 4] = T_IBM[j][i];
				}
				kk1++;
			}
			//Prepare for sending to upleft,1
			if ((xc[j][0] < xx) && (xc[j][0] > xx4) && (xc[j][1] >= yyn) && (xc[j][1] < yyn4))
			{
				pIB2_comm[kk2][0] = 1.0*j;
				for (i = 0; i < 3; i++)
				{
					pIB2_comm[kk2][i + 1] = F_IBM[j][i];
					pIB2_comm[kk2][i + 4] = T_IBM[j][i];
				}
				kk2++;
			}
			//up,2
			if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yyn) && (xc[j][1] < yyn4))
			{
				pIB3_comm[kk3][0] = 1.0*j;
				for (i = 0; i < 3; i++)
				{
					pIB3_comm[kk3][i + 1] = F_IBM[j][i];
					pIB3_comm[kk3][i + 4] = T_IBM[j][i];
				}
				kk3++;
			}
			//upright,3
			if ((xc[j][0] >= xxn) && (xc[j][0] < xxn4) && (xc[j][1] >= yyn) && (xc[j][1] < yyn4))
			{
				pIB4_comm[kk4][0] = 1.0*j;
				for (i = 0; i < 3; i++)
				{
					pIB4_comm[kk4][i + 1] = F_IBM[j][i];
					pIB4_comm[kk4][i + 4] = T_IBM[j][i];
				}
				kk4++;
			}
			//right,4
			if ((xc[j][0] >= xxn) && (xc[j][0] < xxn4) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
			{
				pIB5_comm[kk5][0] = 1.0*j;
				for (i = 0; i < 3; i++)
				{
					pIB5_comm[kk5][i + 1] = F_IBM[j][i];
					pIB5_comm[kk5][i + 4] = T_IBM[j][i];
				}
				kk5++;
			}
			//lowright,5
			if ((xc[j][0] >= xxn) && (xc[j][0] < xxn4) && (xc[j][1] < yy) && (xc[j][1] > yy4))
			{
				pIB6_comm[kk6][0] = 1.0*j;
				for (i = 0; i < 3; i++)
				{
					pIB6_comm[kk6][i + 1] = F_IBM[j][i];
					pIB6_comm[kk6][i + 4] = T_IBM[j][i];
				}
				kk6++;
			}
			//low,6
			if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] < yy) && (xc[j][1] > yy4))
			{
				pIB7_comm[kk7][0] = 1.0*j;
				for (i = 0; i < 3; i++)
				{
					pIB7_comm[kk7][i + 1] = F_IBM[j][i];
					pIB7_comm[kk7][i + 4] = T_IBM[j][i];
				}
				kk7++;
			}
			//lowleft,7
			if ((xc[j][0] < xx) && (xc[j][0] >= xx4) && (xc[j][1] < yy) && (xc[j][1] > yy4))
			{
				pIB8_comm[kk8][0] = 1.0*j;
				for (i = 0; i < 3; i++)
				{
					pIB8_comm[kk8][i + 1] = F_IBM[j][i];
					pIB8_comm[kk8][i + 4] = T_IBM[j][i];
				}
				kk8++;
			}
		}
	}
	for (i = 0; i < 8; i++)
	{
		msgtag[i] = me * 10 + i;
	}
	MPI_Send(&pIB1_comm[0][0], kk1 * 7, MPI_DOUBLE, Left_me, msgtag[0], MPI_COMM_WORLD);
	MPI_Send(&pIB2_comm[0][0], kk2 * 7, MPI_DOUBLE, Upleft_me, msgtag[1], MPI_COMM_WORLD);
	MPI_Send(&pIB3_comm[0][0], kk3 * 7, MPI_DOUBLE, Up_me, msgtag[2], MPI_COMM_WORLD);
	MPI_Send(&pIB4_comm[0][0], kk4 * 7, MPI_DOUBLE, Upright_me, msgtag[3], MPI_COMM_WORLD);
	MPI_Send(&pIB5_comm[0][0], kk5 * 7, MPI_DOUBLE, Right_me, msgtag[4], MPI_COMM_WORLD);
	MPI_Send(&pIB6_comm[0][0], kk6 * 7, MPI_DOUBLE, Lowright_me, msgtag[5], MPI_COMM_WORLD);
	MPI_Send(&pIB7_comm[0][0], kk7 * 7, MPI_DOUBLE, Low_me, msgtag[6], MPI_COMM_WORLD);
	MPI_Send(&pIB8_comm[0][0], kk8 * 7, MPI_DOUBLE, Lowleft_me, msgtag[7], MPI_COMM_WORLD);

	int buffer_size;
	//Receive frome left, me is right
	if (Left_me != MPI_PROC_NULL) {
		MPI_Probe(Left_me, (Left_me * 10 + 4), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB5_comm[0][0], buffer_size, MPI_DOUBLE, Left_me, (Left_me * 10 + 4), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome left, me is right
			if (pIB5_comm[k][0] >= 0)
			{
				j = (int)pIB5_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB5_comm[k][i + 1];
					T_IBM[j][i] += pIB5_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Upleft, me is Lowright
	if (Upleft_me != MPI_PROC_NULL) {
		MPI_Probe(Upleft_me, (Upleft_me * 10 + 5), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB6_comm[0][0], buffer_size, MPI_DOUBLE, Upleft_me, (Upleft_me * 10 + 5), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Upleft, me is Lowright
			if (pIB6_comm[k][0] >= 0)
			{
				j = (int)pIB6_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB6_comm[k][i + 1];
					T_IBM[j][i] += pIB6_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Up, me is Low
	if (Up_me != MPI_PROC_NULL) {
		MPI_Probe(Up_me, (Up_me * 10 + 6), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB7_comm[0][0], buffer_size, MPI_DOUBLE, Up_me, (Up_me * 10 + 6), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Up, me is Low
			if (pIB7_comm[k][0] >= 0)
			{
				j = (int)pIB7_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB7_comm[k][i + 1];
					T_IBM[j][i] += pIB7_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Upright, me is Lowleft
	if (Upright_me != MPI_PROC_NULL) {
		MPI_Probe(Upright_me, (Upright_me * 10 + 7), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB8_comm[0][0], buffer_size, MPI_DOUBLE, Upright_me, (Upright_me * 10 + 7), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Upright, me is Lowleft
			if (pIB8_comm[k][0] >= 0)
			{
				j = (int)pIB8_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB8_comm[k][i + 1];
					T_IBM[j][i] += pIB8_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Right, me is Left
	if (Right_me != MPI_PROC_NULL) {
		MPI_Probe(Right_me, (Right_me * 10 + 0), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB1_comm[0][0], buffer_size, MPI_DOUBLE, Right_me, (Right_me * 10 + 0), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Right, me is Left
			if (pIB1_comm[k][0] >= 0)
			{
				j = (int)pIB1_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB1_comm[k][i + 1];
					T_IBM[j][i] += pIB1_comm[k][i + 4];
				}
			}
		}
	}
	
	//Receive frome Lowright, me is Upleft
	if (Lowright_me != MPI_PROC_NULL) {
		MPI_Probe(Lowright_me, (Lowright_me * 10 + 1), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB2_comm[0][0], buffer_size, MPI_DOUBLE, Lowright_me, (Lowright_me * 10 + 1), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Lowright, me is Upleft
			if (pIB2_comm[k][0] >= 0)
			{
				j = (int)pIB2_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB2_comm[k][i + 1];
					T_IBM[j][i] += pIB2_comm[k][i + 4];
				}
			}
		}
	}
	
	//Receive frome Low, me is Up
	if (Low_me != MPI_PROC_NULL) {
		MPI_Probe(Low_me, (Low_me * 10 + 2), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB3_comm[0][0], buffer_size, MPI_DOUBLE, Low_me, (Low_me * 10 + 2), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Low, me is Up
			if (pIB3_comm[k][0] >= 0)
			{
				j = (int)pIB3_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB3_comm[k][i + 1];
					T_IBM[j][i] += pIB3_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Lowleft, me is Upright
	if (Lowleft_me != MPI_PROC_NULL) {
		MPI_Probe(Lowleft_me, (Lowleft_me * 10 + 3), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB4_comm[0][0], buffer_size, MPI_DOUBLE, Lowleft_me, (Lowleft_me * 10 + 3), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Lowleft, me is Upright
			if (pIB4_comm[k][0] >= 0)
			{
				j = (int)pIB4_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB4_comm[k][i + 1];
					T_IBM[j][i] += pIB4_comm[k][i + 4];
				}
			}
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);
}

void Particle_IB_comm_poly(int me)
{
	MPI_Status status;

	int x, y, i, j, k;
	int kk1, kk2, kk3, kk4, kk5, kk6, kk7, kk8;
	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int x_p0, y_p0;
	//4����
	double pIB1_comm[np_comm_xface_poly][7];
	double pIB3_comm[np_comm_yface_poly][7];
	double pIB5_comm[np_comm_xface_poly][7];
	double pIB7_comm[np_comm_yface_poly][7];
	//4����
	double pIB2_comm[np_comm_corner_poly][7];
	double pIB4_comm[np_comm_corner_poly][7];
	double pIB6_comm[np_comm_corner_poly][7];
	double pIB8_comm[np_comm_corner_poly][7];

	int msgtag[8];

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;

	for (k = 0; k < np_comm_xface_poly; k++)
	{
		pIB1_comm[k][0] = -1.0;
		pIB5_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_yface_poly; k++)
	{
		pIB3_comm[k][0] = -1.0;
		pIB7_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_corner_poly; k++)
	{
		pIB2_comm[k][0] = -1.0;
		pIB4_comm[k][0] = -1.0;
		pIB6_comm[k][0] = -1.0;
		pIB8_comm[k][0] = -1.0;
	}

	kk1 = 0; kk2 = 0; kk3 = 0; kk4 = 0; kk5 = 0; kk6 = 0; kk7 = 0; kk8 = 0;
	for (k = 0; k < np_local_poly; k++)  //
	{
		if (num_p_poly[k] >= 0)
		{
			j = num_p_poly[k];
			//Prepare for sending to left,0
			if ((xc[j][0] < xx) && (xc[j][0] > (me_x * Nx - rb_poly[rc_poly[j]] - 1.5)) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
			{
				pIB1_comm[kk1][0] = 1.0 * j;
				for (i = 0; i < 3; i++)
				{
					pIB1_comm[kk1][i + 1] = F_IBM[j][i];
					pIB1_comm[kk1][i + 4] = T_IBM[j][i];
				}
				kk1++;
			}
			//Prepare for sending to upleft,1
			if ((xc[j][0] < xx) && (xc[j][0] > (me_x * Nx - rb_poly[rc_poly[j]] - 1.5)) && (xc[j][1] >= yyn) && (xc[j][1] < ((me_y + 1) * Ny + rb_poly[rc_poly[j]] + 1.5)))
			{
				pIB2_comm[kk2][0] = 1.0 * j;
				for (i = 0; i < 3; i++)
				{
					pIB2_comm[kk2][i + 1] = F_IBM[j][i];
					pIB2_comm[kk2][i + 4] = T_IBM[j][i];
				}
				kk2++;
			}
			//up,2
			if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yyn) && (xc[j][1] < ((me_y + 1) * Ny + rb_poly[rc_poly[j]] + 1.5)))
			{
				pIB3_comm[kk3][0] = 1.0 * j;
				for (i = 0; i < 3; i++)
				{
					pIB3_comm[kk3][i + 1] = F_IBM[j][i];
					pIB3_comm[kk3][i + 4] = T_IBM[j][i];
				}
				kk3++;
			}
			//upright,3
			if ((xc[j][0] >= xxn) && (xc[j][0] < ((me_x + 1) * Nx + rb_poly[rc_poly[j]] + 1.5)) && (xc[j][1] >= yyn) && (xc[j][1] < ((me_y + 1) * Ny + rb_poly[rc_poly[j]] + 1.5)))
			{
				pIB4_comm[kk4][0] = 1.0 * j;
				for (i = 0; i < 3; i++)
				{
					pIB4_comm[kk4][i + 1] = F_IBM[j][i];
					pIB4_comm[kk4][i + 4] = T_IBM[j][i];
				}
				kk4++;
			}
			//right,4
			if ((xc[j][0] >= xxn) && (xc[j][0] < ((me_x + 1) * Nx + rb_poly[rc_poly[j]] + 1.5)) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
			{
				pIB5_comm[kk5][0] = 1.0 * j;
				for (i = 0; i < 3; i++)
				{
					pIB5_comm[kk5][i + 1] = F_IBM[j][i];
					pIB5_comm[kk5][i + 4] = T_IBM[j][i];
				}
				kk5++;
			}
			//lowright,5
			if ((xc[j][0] >= xxn) && (xc[j][0] < ((me_x + 1) * Nx + rb_poly[rc_poly[j]] + 1.5)) && (xc[j][1] < yy) && (xc[j][1] > (me_y * Ny - rb_poly[rc_poly[j]] - 1.5)))
			{
				pIB6_comm[kk6][0] = 1.0 * j;
				for (i = 0; i < 3; i++)
				{
					pIB6_comm[kk6][i + 1] = F_IBM[j][i];
					pIB6_comm[kk6][i + 4] = T_IBM[j][i];
				}
				kk6++;
			}
			//low,6
			if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] < yy) && (xc[j][1] > (me_y * Ny - rb_poly[rc_poly[j]] - 1.5)))
			{
				pIB7_comm[kk7][0] = 1.0 * j;
				for (i = 0; i < 3; i++)
				{
					pIB7_comm[kk7][i + 1] = F_IBM[j][i];
					pIB7_comm[kk7][i + 4] = T_IBM[j][i];
				}
				kk7++;
			}
			//lowleft,7
			if ((xc[j][0] < xx) && (xc[j][0] >= (me_x * Nx - rb_poly[rc_poly[j]] - 1.5)) && (xc[j][1] < yy) && (xc[j][1] > (me_y * Ny - rb_poly[rc_poly[j]] - 1.5)))
			{
				pIB8_comm[kk8][0] = 1.0 * j;
				for (i = 0; i < 3; i++)
				{
					pIB8_comm[kk8][i + 1] = F_IBM[j][i];
					pIB8_comm[kk8][i + 4] = T_IBM[j][i];
				}
				kk8++;
			}
		}
	}
	for (i = 0; i < 8; i++)
	{
		msgtag[i] = me * 10 + i;
	}
	MPI_Send(&pIB1_comm[0][0], kk1 * 7, MPI_DOUBLE, Left_me, msgtag[0], MPI_COMM_WORLD);
	MPI_Send(&pIB2_comm[0][0], kk2 * 7, MPI_DOUBLE, Upleft_me, msgtag[1], MPI_COMM_WORLD);
	MPI_Send(&pIB3_comm[0][0], kk3 * 7, MPI_DOUBLE, Up_me, msgtag[2], MPI_COMM_WORLD);
	MPI_Send(&pIB4_comm[0][0], kk4 * 7, MPI_DOUBLE, Upright_me, msgtag[3], MPI_COMM_WORLD);
	MPI_Send(&pIB5_comm[0][0], kk5 * 7, MPI_DOUBLE, Right_me, msgtag[4], MPI_COMM_WORLD);
	MPI_Send(&pIB6_comm[0][0], kk6 * 7, MPI_DOUBLE, Lowright_me, msgtag[5], MPI_COMM_WORLD);
	MPI_Send(&pIB7_comm[0][0], kk7 * 7, MPI_DOUBLE, Low_me, msgtag[6], MPI_COMM_WORLD);
	MPI_Send(&pIB8_comm[0][0], kk8 * 7, MPI_DOUBLE, Lowleft_me, msgtag[7], MPI_COMM_WORLD);

	int buffer_size;
	//Receive frome left, me is right
	if (Left_me != MPI_PROC_NULL) {
		MPI_Probe(Left_me, (Left_me * 10 + 4), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB5_comm[0][0], buffer_size, MPI_DOUBLE, Left_me, (Left_me * 10 + 4), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome left, me is right
			if (pIB5_comm[k][0] >= 0)
			{
				j = (int)pIB5_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB5_comm[k][i + 1];
					T_IBM[j][i] += pIB5_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Upleft, me is Lowright
	if (Upleft_me != MPI_PROC_NULL) {
		MPI_Probe(Upleft_me, (Upleft_me * 10 + 5), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB6_comm[0][0], buffer_size, MPI_DOUBLE, Upleft_me, (Upleft_me * 10 + 5), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Upleft, me is Lowright
			if (pIB6_comm[k][0] >= 0)
			{
				j = (int)pIB6_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB6_comm[k][i + 1];
					T_IBM[j][i] += pIB6_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Up, me is Low
	if (Up_me != MPI_PROC_NULL) {
		MPI_Probe(Up_me, (Up_me * 10 + 6), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB7_comm[0][0], buffer_size, MPI_DOUBLE, Up_me, (Up_me * 10 + 6), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Up, me is Low
			if (pIB7_comm[k][0] >= 0)
			{
				j = (int)pIB7_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB7_comm[k][i + 1];
					T_IBM[j][i] += pIB7_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Upright, me is Lowleft
	if (Upright_me != MPI_PROC_NULL) {
		MPI_Probe(Upright_me, (Upright_me * 10 + 7), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB8_comm[0][0], buffer_size, MPI_DOUBLE, Upright_me, (Upright_me * 10 + 7), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Upright, me is Lowleft
			if (pIB8_comm[k][0] >= 0)
			{
				j = (int)pIB8_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB8_comm[k][i + 1];
					T_IBM[j][i] += pIB8_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Right, me is Left
	if (Right_me != MPI_PROC_NULL) {
		MPI_Probe(Right_me, (Right_me * 10 + 0), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB1_comm[0][0], buffer_size, MPI_DOUBLE, Right_me, (Right_me * 10 + 0), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Right, me is Left
			if (pIB1_comm[k][0] >= 0)
			{
				j = (int)pIB1_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB1_comm[k][i + 1];
					T_IBM[j][i] += pIB1_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Lowright, me is Upleft
	if (Lowright_me != MPI_PROC_NULL) {
		MPI_Probe(Lowright_me, (Lowright_me * 10 + 1), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB2_comm[0][0], buffer_size, MPI_DOUBLE, Lowright_me, (Lowright_me * 10 + 1), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Lowright, me is Upleft
			if (pIB2_comm[k][0] >= 0)
			{
				j = (int)pIB2_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB2_comm[k][i + 1];
					T_IBM[j][i] += pIB2_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Low, me is Up
	if (Low_me != MPI_PROC_NULL) {
		MPI_Probe(Low_me, (Low_me * 10 + 2), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB3_comm[0][0], buffer_size, MPI_DOUBLE, Low_me, (Low_me * 10 + 2), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Low, me is Up
			if (pIB3_comm[k][0] >= 0)
			{
				j = (int)pIB3_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB3_comm[k][i + 1];
					T_IBM[j][i] += pIB3_comm[k][i + 4];
				}
			}
		}
	}

	//Receive frome Lowleft, me is Upright
	if (Lowleft_me != MPI_PROC_NULL) {
		MPI_Probe(Lowleft_me, (Lowleft_me * 10 + 3), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pIB4_comm[0][0], buffer_size, MPI_DOUBLE, Lowleft_me, (Lowleft_me * 10 + 3), MPI_COMM_WORLD, &status);
		for (k = 0; k < int(buffer_size / 7); k++)
		{
			//Receive frome Lowleft, me is Upright
			if (pIB4_comm[k][0] >= 0)
			{
				j = (int)pIB4_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					F_IBM[j][i] += pIB4_comm[k][i + 1];
					T_IBM[j][i] += pIB4_comm[k][i + 4];
				}
			}
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);
}

//---------------------------------------------------------------------------------
//x direction, Periodical
//y and z direction, wall, Non-equilibrium extrapolation
void Fluid_Boundary_rectangle(int me)
{
	double m, n;
	int x, y, z, i;
	
	//����,inflow
	if (me % NPROCx == 0)
	{
		for (y = 1; y <= Ny2; y++)
		for (z = 0; z <= Nz3; z++)
		{
			u[1][y][z][0] = 0;
			u[1][y][z][1] = 0;
			u[1][y][z][2] = 0;
			p[1][y][z] = p[2][y][z];

			for (i = 0; i < Q; i++)
			{
				m = fequation(u[1][y][z], p[1][y][z], i);
				n = fequation(u[2][y][z], p[2][y][z], i);
				f[1][y][z][i] = m + (f[2][y][z][i] - n);
			}
		}
	}
	//����,outflow
	if ((me + 1) % NPROCx == 0)
	{
		for (y = 1; y <= Ny2; y++)   //����,outflow
		for (z = 0; z <= Nz3; z++)
		{
			u[Nx2][y][z][0] = 0;
			u[Nx2][y][z][1] = 0;
			u[Nx2][y][z][2] = 0;
			p[Nx2][y][z] = p[Nx1][y][z];

			for (i = 0; i < Q; i++)
			{
				m = fequation(u[Nx2][y][z], p[Nx2][y][z], i);
				n = fequation(u[Nx1][y][z], p[Nx1][y][z], i);
				f[Nx2][y][z][i] = m + (f[Nx1][y][z][i] - n);
			}
		}
	}

	//bottom bounday
	if (me < NPROCx)
	{
		for (x = 1; x <= Nx2; x++)
		for (z = 0; z <= Nz3; z++)   //������	
		{
			u[x][1][z][0] = 0.0;
			u[x][1][z][1] = 0.30;
			u[x][1][z][2] = 0.0;			
			p[x][1][z] = p[x][2][z];
			
			for (i = 0; i < Q; i++)
			{
				m = fequation(u[x][1][z], p[x][1][z], i);
				n = fequation(u[x][2][z], p[x][2][z], i);
				f[x][1][z][i] = m + (f[x][2][z][i] - n);				
			}
		}
	}

	//Top bounday
	if (me >= ((NPROCy - 1) * NPROCx))
	{
		for (x = 1; x <= Nx2; x++)
		for (z = 0; z <= Nz3; z++)   	
		{
			//u[x][Ny1][z][0] = 0.0;
			//u[x][Ny1][z][1] = 0.0;
			//u[x][Ny1][z][2] = 0.0;	
			u[x][Ny2][z][0] = u[x][Ny1][z][0];
			u[x][Ny2][z][1] = u[x][Ny1][z][1];
			u[x][Ny2][z][2] = u[x][Ny1][z][2];
			p[x][Ny2][z] = p[x][Ny1][z];

			for (i = 0; i < Q; i++)
			{
				m = fequation(u[x][Ny2][z], p[x][Ny2][z], i);
				n = fequation(u[x][Ny1][z], p[x][Ny1][z], i);
				f[x][Ny2][z][i] = m + (f[x][Ny1][z][i] - n);
			}
		}
	}
	
	//front and back face
	for (x = 1; x <= Nx2; x++)    
	for (y = 1; y <= Ny2; y++)
	{
		u[x][y][1][0] = 0.0;
		u[x][y][1][1] = 0.0;
		u[x][y][1][2] = 0.0;
		p[x][y][1] = p[x][y][2];

		u[x][y][Nz2][0] = 0.0;
		u[x][y][Nz2][1] = 0.0;
		u[x][y][Nz2][2] = 0.0;		
		p[x][y][Nz2] = p[x][y][Nz1];

		for (i = 0; i < Q; i++)
		{
			m = fequation(u[x][y][1], p[x][y][1], i);
			n = fequation(u[x][y][2], p[x][y][2], i);
			f[x][y][1][i] = m + (f[x][y][2][i] - n);

			m = fequation(u[x][y][Nz2], p[x][y][Nz2], i);
			n = fequation(u[x][y][Nz1], p[x][y][Nz1], i);
			f[x][y][Nz2][i] = m + (f[x][y][Nz1][i] - n);
		}
	}	
}

void bodyforce_in_particle(int me)
{
	
		int me_x = me % NPROCx;
		int me_y = me / NPROCx;

		if (if_polydisperse == 1)
		{
#pragma omp parallel 
			{
				int j, k, rcj;
				double xc_key[3];
				int x_min, x_max, y_min, y_max, z_min, z_max;
				double lxc2, lyc2, lzc2, lc2;
				int x, y, z, z1;

#pragma omp for schedule(dynamic, 5) 
				for (k = 0; k < np_local_poly; k++)
				{
					if (num_p_poly[k] >= 0)
					{
						j = num_p_poly[k];
						rcj = R_poly[rc_poly[j]];
						lc2 = rcj * rcj;

						// 将颗粒坐标转换到局部网格坐标
						xc_key[0] = xc[j][0] + 1 - me_x * Nx;
						xc_key[1] = xc[j][1] + 1 - me_y * Ny;
						xc_key[2] = xc[j][2] + 1;

						// 计算颗粒的边界盒
						x_min = (int)((2.0 > floor(xc_key[0] - rcj - 1)) ? 2.0 : floor(xc_key[0] - rcj - 1));
						x_max = (int)((Nx1 < ceil(xc_key[0] + rcj + 1)) ? Nx1 : ceil(xc_key[0] + rcj + 1));
						y_min = (int)((2.0 > floor(xc_key[1] - rcj - 1)) ? 2.0 : floor(xc_key[1] - rcj - 1));
						y_max = (int)((Ny1 < ceil(xc_key[1] + rcj + 1)) ? Ny1 : ceil(xc_key[1] + rcj + 1));
						z_min = (int)(floor(xc_key[2] - rcj));
						z_max = (int)(ceil(xc_key[2] + rcj));

						// 只遍历边界盒内的网格点
						for (x = x_min; x <= x_max; x++) {
							lxc2 = (x - xc_key[0]);
							lxc2 = lxc2 * lxc2;
							for (y = y_min; y <= y_max; y++) {
								lyc2 = y - xc_key[1];
								lyc2 = lyc2 * lyc2;
								for (z = z_min; z <= z_max; z++) {
									lzc2 = z - xc_key[2];
									lzc2 = lzc2 * lzc2;

									if (lxc2 + lyc2 + lzc2 <= lc2)
									{
										z1 = z;
										if (z1 <= 1)
										{
											z1 += Nz;
										}
										if (z1 >= Nz2)
										{
											z1 -= Nz;
										}
										forcef[x][y][z1][0] += Ff_body_poly[0];
										forcef[x][y][z1][1] += Ff_body_poly[0];
										forcef[x][y][z1][2] += Ff_body_poly[0];
									}
								}
							}
						}
					}
				}
			}//end openmp parallel
		}
		else
		{
#pragma omp parallel
			{
				int j, k;
				double xc_key[3];
				int x_min, x_max, y_min, y_max, z_min, z_max;
				double lxc2, lyc2, lzc2, lc2;
				int x, y, z, z1;

#pragma omp for schedule(dynamic, 5) 
				for (k = 0; k < np_local; k++)
				{
					if (num_p[k] >= 0)
					{
						j = num_p[k];
						lc2 = R * R;

						// 将颗粒坐标转换到局部网格坐标
						xc_key[0] = xc[j][0] + 1 - me_x * Nx;
						xc_key[1] = xc[j][1] + 1 - me_y * Ny;
						xc_key[2] = xc[j][2] + 1;

						// 计算颗粒的边界盒
						x_min = (int)((2.0 > floor(xc_key[0] - R - 1)) ? 2.0 : floor(xc_key[0] - R - 1));
						x_max = (int)((Nx1 < ceil(xc_key[0] + R + 1)) ? Nx1 : ceil(xc_key[0] + R + 1));
						y_min = (int)((2.0 > floor(xc_key[1] - R - 1)) ? 2.0 : floor(xc_key[1] - R - 1));
						y_max = (int)((Ny1 < ceil(xc_key[1] + R + 1)) ? Ny1 : ceil(xc_key[1] + R + 1));
						z_min = (int)(floor(xc_key[2] - R));
						z_max = (int)(ceil(xc_key[2] + R));

						// 只遍历边界盒内的网格点
						for (x = x_min; x <= x_max; x++) {
							lxc2 = (x - xc_key[0]);
							lxc2 = lxc2 * lxc2;
							for (y = y_min; y <= y_max; y++) {
								lyc2 = y - xc_key[1];
								lyc2 = lyc2 * lyc2;
								for (z = z_min; z <= z_max; z++) {
									lzc2 = z - xc_key[2];
									lzc2 = lzc2 * lzc2;

									if (lxc2 + lyc2 + lzc2 <= lc2)
									{
										z1 = z;
										if (z1 <= 1)
										{
											z1 += Nz;
										}
										if (z1 >= Nz2)
										{
											z1 -= Nz;
										}
										forcef[x][y][z1][0] += Ff_body[0];
										forcef[x][y][z1][1] += Ff_body[1];
										forcef[x][y][z1][2] += Ff_body[2];
									}
								}
							}
						}
					}
				}
			}//end openmp parallel
		}
		MPI_Barrier(MPI_COMM_WORLD);
	
}

void Fluid_collision(int me)
{
	int i, x, y, z;
	double m, n;
	double pc, cux, cuy, cuz, uxuy, uxuz, uyuz, ux2, uy2, uz2;
	double fx, fy, fz, uxfx, uxfy, uxfz, uyfx, uyfy, uyfz, uzfx, uzfy, uzfz;
	double feq[Q], F_exter[Q];
	double temp = (1 - 0.5*omega)* dt;

	const double Ff_body_local[3] = {
		(if_polydisperse == 1) ? Ff_body_poly[0] : Ff_body[0],
		(if_polydisperse == 1) ? Ff_body_poly[1] : Ff_body[1],
		(if_polydisperse == 1) ? Ff_body_poly[2] : Ff_body[2]
	};

	// collistion with external forcing term
#pragma omp parallel
	{
#pragma omp for collapse(3) private(x,y,z,i,pc,cux,cuy,cuz,uxuy,uxuz,uyuz,ux2,uy2,uz2,feq,F_exter,fx,fy,fz,uxfx,uxfy,uxfz,uyfx,uyfy,uyfz,uzfx,uzfy,uzfz)
		for (x = 1; x <= Nx2; x++)
		{
			for (y = 1; y <= Ny2; y++)
			{
				for (z = 1; z <= Nz2; z++)
				{
					//forcef[x][y][z][0] += -Ff* G_dire[0];
					//forcef[x][y][z][1] += -Ff* G_dire[1];
					//forcef[x][y][z][2] += -Ff* G_dire[2];
					//if (if_polydisperse == 1)
					//{
					//	forcef[x][y][z][0] += -Ff_body_poly[0];
					//	forcef[x][y][z][1] += -Ff_body_poly[1];
					//	forcef[x][y][z][2] += -Ff_body_poly[2];
					//}
					//else
					//{
					//	forcef[x][y][z][0] += -Ff_body[0];
					//	forcef[x][y][z][1] += -Ff_body[1];
					//	forcef[x][y][z][2] += -Ff_body[2];
					//}
#if (periodic_x == 1 && periodic_x == 1 && periodic_x == 1)
					forcef[x][y][z][0] += -Ff_body_local[0];
					forcef[x][y][z][1] += -Ff_body_local[1];
					forcef[x][y][z][2] += -Ff_body_local[2];
#endif
					if ((forcef[x][y][z][0] == 0) && (forcef[x][y][z][1] == 0) && (forcef[x][y][z][2] == 0))
					{
						pc = p[x][y][z] / c_square;
						cux = u[x][y][z][0] / c;
						cuy = u[x][y][z][1] / c;
						cuz = u[x][y][z][2] / c;
						uxuy = cux * cuy;
						uxuz = cux * cuz;
						uyuz = cuy * cuz;
						ux2 = cux * cux;
						uy2 = cuy * cuy;
						uz2 = cuz * cuz;

						feq[1] = (1.0 / 6) * (pc - cux + ux2 - 0.5 * (uy2 + uz2));
						feq[2] = (1.0 / 6) * (pc - cuy + uy2 - 0.5 * (ux2 + uz2));
						feq[3] = (1.0 / 6) * (pc - cuz + uz2 - 0.5 * (ux2 + uy2));
						feq[4] = (1.0 / 12) * (pc - cux - cuy + 3 * uxuy + ux2 + uy2 - 0.5 * uz2);
						feq[5] = (1.0 / 12) * (pc - cux + cuy - 3 * uxuy + ux2 + uy2 - 0.5 * uz2);
						feq[6] = (1.0 / 12) * (pc - cux - cuz + 3 * uxuz + ux2 + uz2 - 0.5 * uy2);
						feq[7] = (1.0 / 12) * (pc - cux + cuz - 3 * uxuz + ux2 + uz2 - 0.5 * uy2);
						feq[8] = (1.0 / 12) * (pc - cuy - cuz + 3 * uyuz + uy2 + uz2 - 0.5 * ux2);
						feq[9] = (1.0 / 12) * (pc - cuy + cuz - 3 * uyuz + uy2 + uz2 - 0.5 * ux2);

						feq[10] = feq[1] + (1.0 / 3) * cux;
						feq[11] = feq[2] + (1.0 / 3) * cuy;
						feq[12] = feq[3] + (1.0 / 3) * cuz;
						feq[13] = feq[4] + (1.0 / 6) * (cux + cuy);
						feq[14] = feq[5] + (1.0 / 6) * (cux - cuy);
						feq[15] = feq[6] + (1.0 / 6) * (cux + cuz);
						feq[16] = feq[7] + (1.0 / 6) * (cux - cuz);
						feq[17] = feq[8] + (1.0 / 6) * (cuy + cuz);
						feq[18] = feq[9] + (1.0 / 6) * (cuy - cuz);

						for (i = 1; i < Q; i++)
						{
							f[x][y][z][i] *= (1. - omega);
							f[x][y][z][i] += omega * feq[i];
						}
					}
					else
					{
						u[x][y][z][0] += 0.5 * dt * forcef[x][y][z][0] / rhof;
						u[x][y][z][1] += 0.5 * dt * forcef[x][y][z][1] / rhof;
						u[x][y][z][2] += 0.5 * dt * forcef[x][y][z][2] / rhof;

						pc = p[x][y][z] / c_square;
						cux = u[x][y][z][0] / c;
						cuy = u[x][y][z][1] / c;
						cuz = u[x][y][z][2] / c;
						uxuy = cux * cuy;
						uxuz = cux * cuz;
						uyuz = cuy * cuz;
						ux2 = cux * cux;
						uy2 = cuy * cuy;
						uz2 = cuz * cuz;

						feq[1] = (1.0 / 6) * (pc - cux + ux2 - 0.5 * (uy2 + uz2));
						feq[2] = (1.0 / 6) * (pc - cuy + uy2 - 0.5 * (ux2 + uz2));
						feq[3] = (1.0 / 6) * (pc - cuz + uz2 - 0.5 * (ux2 + uy2));
						feq[4] = (1.0 / 12) * (pc - cux - cuy + 3 * uxuy + ux2 + uy2 - 0.5 * uz2);
						feq[5] = (1.0 / 12) * (pc - cux + cuy - 3 * uxuy + ux2 + uy2 - 0.5 * uz2);
						feq[6] = (1.0 / 12) * (pc - cux - cuz + 3 * uxuz + ux2 + uz2 - 0.5 * uy2);
						feq[7] = (1.0 / 12) * (pc - cux + cuz - 3 * uxuz + ux2 + uz2 - 0.5 * uy2);
						feq[8] = (1.0 / 12) * (pc - cuy - cuz + 3 * uyuz + uy2 + uz2 - 0.5 * ux2);
						feq[9] = (1.0 / 12) * (pc - cuy + cuz - 3 * uyuz + uy2 + uz2 - 0.5 * ux2);

						feq[10] = feq[1] + (1.0 / 3) * cux;
						feq[11] = feq[2] + (1.0 / 3) * cuy;
						feq[12] = feq[3] + (1.0 / 3) * cuz;
						feq[13] = feq[4] + (1.0 / 6) * (cux + cuy);
						feq[14] = feq[5] + (1.0 / 6) * (cux - cuy);
						feq[15] = feq[6] + (1.0 / 6) * (cux + cuz);
						feq[16] = feq[7] + (1.0 / 6) * (cux - cuz);
						feq[17] = feq[8] + (1.0 / 6) * (cuy + cuz);
						feq[18] = feq[9] + (1.0 / 6) * (cuy - cuz);

						fx = forcef[x][y][z][0] / c;
						fy = forcef[x][y][z][1] / c;
						fz = forcef[x][y][z][2] / c;
						uxfx = fx * cux;
						uxfy = fy * cux;
						uxfz = fz * cux;
						uyfx = fx * cuy;
						uyfy = fy * cuy;
						uyfz = fz * cuy;
						uzfx = fx * cuz;
						uzfy = fy * cuz;
						uzfz = fz * cuz;

						F_exter[1] = (1.0 / 6) * (-fx + 2 * uxfx - uyfy - uzfz);
						F_exter[2] = (1.0 / 6) * (-fy - uxfx + 2 * uyfy - uzfz);
						F_exter[3] = (1.0 / 6) * (-fz - uxfx - uyfy + 2 * uzfz);
						F_exter[4] = (1.0 / 12) * (-fx - fy + 2 * uxfx + 2 * uyfy + 3 * uxfy + 3 * uyfx - uzfz);
						F_exter[5] = (1.0 / 12) * (-fx + fy + 2 * uxfx + 2 * uyfy - 3 * uxfy - 3 * uyfx - uzfz);
						F_exter[6] = (1.0 / 12) * (-fx - fz + 2 * uxfx + 2 * uzfz + 3 * uxfz + 3 * uzfx - uyfy);
						F_exter[7] = (1.0 / 12) * (-fx + fz + 2 * uxfx + 2 * uzfz - 3 * uxfz - 3 * uzfx - uyfy);
						F_exter[8] = (1.0 / 12) * (-fy - fz + 2 * uyfy + 2 * uzfz + 3 * uyfz + 3 * uzfy - uxfx);
						F_exter[9] = (1.0 / 12) * (-fy + fz + 2 * uyfy + 2 * uzfz - 3 * uyfz - 3 * uzfy - uxfx);
						F_exter[10] = F_exter[1] + (1.0 / 3) * fx;
						F_exter[11] = F_exter[2] + (1.0 / 3) * fy;
						F_exter[12] = F_exter[3] + (1.0 / 3) * fz;
						F_exter[13] = F_exter[4] + (1.0 / 6) * (fx + fy);
						F_exter[14] = F_exter[5] + (1.0 / 6) * (fx - fy);
						F_exter[15] = F_exter[6] + (1.0 / 6) * (fx + fz);
						F_exter[16] = F_exter[7] + (1.0 / 6) * (fx - fz);
						F_exter[17] = F_exter[8] + (1.0 / 6) * (fy + fz);
						F_exter[18] = F_exter[9] + (1.0 / 6) * (fy - fz);

						for (i = 1; i < Q; i++)
						{
							f[x][y][z][i] *= (1. - omega);
							f[x][y][z][i] += omega * feq[i] + temp * F_exter[i];
						}
					}

					for (i = 1; i <= 9; i++)
						change(&f[x][y][z][i], &f[x][y][z][i + 9]);

				}
			}
		}
	}
	collision_change_start_time = MPI_Wtime();
	/*
#pragma omp parallel
	{
#pragma omp for schedule(static) private(x,y,z,i)
		for (x = 1; x < Nx2; x++)
		{
			for (y = 1; y < Ny2; y++)
			{
				for (z = 1; z < Nz2; z++)
				{
					for (i = 1; i < 9; i++)
					{
						change(&f[x][y][z][i], &f[x][y][z][i + 9]);
					}
				}
			}
		}

	}
	*/

	MPI_Barrier(MPI_COMM_WORLD);
}

//--------------------------------------------------------------------------------------------------------
// synchronize fp across slices boundary

void Fluid_data_exchange_f(int me)
{
	MPI_Status status;

	int x, y, z, j;
	int low_exf, up_exf, right_exf, left_exf;
	double  f_ex[4][Ny2][Nz2][Q - 1], f_ey[4][Nx2][Nz2][Q - 1];
	int x1, y1, z1;
	int q_send;
	q_send = Q - 1;

	//ȫ����
	if (periodic == 1)
	{
		if (me % NPROCx == 0)              //�����һ�к�
			left_exf = me + NPROCx - 1;
		else
			left_exf = me - 1;

		if ((me + 1) % NPROCx == 0)       //���ұ�һ�к�
			right_exf = me - NPROCx + 1;
		else
			right_exf = me + 1;

		if (me < NPROCx)                  //������һ�ź�
			low_exf = me + NPROCx*(NPROCy - 1);
		else
			low_exf = me - NPROCx;

		if (me >= (NPROCx*(NPROCy - 1)))   //������һ�ź�
			up_exf = me - NPROCx*(NPROCy - 1);
		else
			up_exf = me + NPROCx;
	}

	//������
	else if (periodic == 0)
	{
		if (me % NPROCx == 0)              //�����һ�к�
			left_exf = MPI_PROC_NULL;
		else
			left_exf = me - 1;

		if ((me + 1) % NPROCx == 0)       //���ұ�һ�к�
			right_exf = MPI_PROC_NULL;
		else
			right_exf = me + 1;

		if (me < NPROCx)                  //������һ�ź�
			low_exf = MPI_PROC_NULL;
		else
			low_exf = me - NPROCx;

		if (me >= (NPROCx*(NPROCy - 1)))   //������һ�ź�
			up_exf = MPI_PROC_NULL;
		else
			up_exf = me + NPROCx;
	}

	f_pack_start_time = MPI_Wtime();
	#if if_gptl
		gptl_start(GT_f_pack);
	#endif

#pragma omp parallel
	{
        #pragma omp for collapse(2) schedule(static) private(y,z,j,y1,z1)
		for (y = 0; y <= Ny1; y++)
		{
			for (z = 0; z <= Nz1; z++)
			{
				y1 = y + 1;
				z1 = z + 1;
				for (j = 0; j < q_send; j++)
				{
					//f_ex[0][y][z][j] = f[1][y1][z1][j + 1];
					f_ex[1][y][z][j] = f[2][y1][z1][j + 1];
					f_ex[2][y][z][j] = f[Nx1][y1][z1][j + 1];
					//f_ex[3][y][z][j] = f[Nx2][y1][z1][j + 1];
				}
			}
		}
	}

	f_change_start_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_f_pack);
		gptl_start(GT_f_change);
	#endif

	MPI_Sendrecv(&f_ex[2][0][0][0], Ny2*Nz2*q_send, MPI_DOUBLE, right_exf, 210, &f_ex[0][0][0][0], Ny2*Nz2*q_send, MPI_DOUBLE, left_exf, 210, MPI_COMM_WORLD, &status);
	MPI_Sendrecv(&f_ex[1][0][0][0], Ny2*Nz2*q_send, MPI_DOUBLE, left_exf, 220, &f_ex[3][0][0][0], Ny2*Nz2*q_send, MPI_DOUBLE, right_exf, 220, MPI_COMM_WORLD, &status);

	f_unpack_start_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_f_change);
		gptl_start(GT_f_unpack);
	#endif

#pragma omp parallel
	{
#pragma omp for collapse(2) schedule(static) private(y,z,j,y1,z1)
		for (y = 0; y <= Ny1; y++)
		{
			for (z = 0; z <= Nz1; z++)
			{
				y1 = y + 1;
				z1 = z + 1;
				for (j = 0; j < q_send; j++)
				{
					f[1][y1][z1][j + 1] = f_ex[0][y][z][j];
					f[Nx2][y1][z1][j + 1] = f_ex[3][y][z][j];
				}
			}
		}
	}

	f_unpack_end_time = MPI_Wtime();
	f_pack_time += f_change_start_time - f_pack_start_time;
	f_change_time += f_unpack_start_time - f_change_start_time;
    f_unpack_time += f_unpack_end_time - f_unpack_start_time;

	f_pack_start_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_f_unpack);
		gptl_start(GT_f_pack);
	#endif

#pragma omp parallel
	{
#pragma omp for collapse(2) schedule(static) private(x,z,j,x1,z1)
		for (x = 0; x <= Nx1; x++)
		{
			for (z = 0; z <= Nz1; z++)
			{
				x1 = x + 1;
				z1 = z + 1;
				for (j = 0; j < q_send; j++)
				{
					//f_ey[0][x][z][j] = f[x1][1][z1][j + 1];
					f_ey[1][x][z][j] = f[x1][2][z1][j + 1];
					f_ey[2][x][z][j] = f[x1][Ny1][z1][j + 1];
					//f_ey[3][x][z][j] = f[x1][Ny2][z1][j + 1];
				}
			}
		}
	}

    f_change_start_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_f_pack);
		gptl_start(GT_f_change);
	#endif

	MPI_Sendrecv(&f_ey[2][0][0][0], Nx2*Nz2*q_send, MPI_DOUBLE, up_exf, 250, &f_ey[0][0][0][0], Nx2*Nz2*q_send, MPI_DOUBLE, low_exf, 250, MPI_COMM_WORLD, &status);
	MPI_Sendrecv(&f_ey[1][0][0][0], Nx2*Nz2*q_send, MPI_DOUBLE, low_exf, 260, &f_ey[3][0][0][0], Nx2*Nz2*q_send, MPI_DOUBLE, up_exf, 260, MPI_COMM_WORLD, &status);

	f_unpack_start_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_f_change);
		gptl_start(GT_f_unpack);
	#endif

#pragma omp parallel
	{
#pragma omp for collapse(2) schedule(static) private(x,z,j,x1,z1)
		for (x = 0; x <= Nx1; x++)
		{
			for (z = 0; z <= Nz1; z++)
			{
				x1 = x + 1;
				z1 = z + 1;
				for (j = 0; j < q_send; j++)
				{
					f[x1][1][z1][j + 1] = f_ey[0][x][z][j];
					f[x1][Ny2][z1][j + 1] = f_ey[3][x][z][j];
				}
			}
		}
	}

    f_unpack_end_time = MPI_Wtime();
	f_pack_time += f_change_start_time - f_pack_start_time;
	f_change_time += f_unpack_start_time - f_change_start_time;
	f_unpack_time += f_unpack_end_time - f_unpack_start_time;
	#if if_gptl
		gptl_stop(GT_f_unpack);
	#endif

	//z����������
	if (periodic == 1)
	{
        #pragma omp parallel
		{
            #pragma omp for collapse(3) schedule(static) private(x,y,j)
			for (x = 1; x <= Nx2; x++)
			{
				for (y = 1; y <= Ny2; y++)
				{
					for (j = 1; j < Q; j++)
					{
						f[x][y][1][j] = f[x][y][Nz1][j];
						f[x][y][Nz2][j] = f[x][y][2][j];
					}
				}
			}
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);
}
//--------------------------------------------------------------------------------------------------------
// synchronize fp across slices boundary
void Fluid_data_exchange_f_less(int me)
{
	MPI_Status status;

	int x, y, z, j;
	int low_exf, up_exf, right_exf, left_exf;
	//double  f_ex[4][Ny2][Nz2][Q - 1], f_ey[4][Nx2][Nz2][Q - 1];
	double  f_ex[4][Ny2][Nz2][5], f_ey[4][Nx2][Nz2][5];
	int x1, y1, z1;
	int q_send;
	q_send = 5;

	//ȫ����
	if (periodic_x == 1)
	{
		if (me % NPROCx == 0)              //�����һ�к�
			left_exf = me + NPROCx - 1;
		else
			left_exf = me - 1;
		if ((me + 1) % NPROCx == 0)       //���ұ�һ�к�
			right_exf = me - NPROCx + 1;
		else
			right_exf = me + 1;
	}
	else if (periodic_x == 0)
	{
		if (me % NPROCx == 0)              //�����һ�к�
			left_exf = MPI_PROC_NULL;
		else
			left_exf = me - 1;

		if ((me + 1) % NPROCx == 0)       //���ұ�һ�к�
			right_exf = MPI_PROC_NULL;
		else
			right_exf = me + 1;
	}
	if (periodic_y == 1)
	{
		if (me < NPROCx)                  //������һ�ź�
			low_exf = me + NPROCx * (NPROCy - 1);
		else
			low_exf = me - NPROCx;

		if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
			up_exf = me - NPROCx * (NPROCy - 1);
		else
			up_exf = me + NPROCx;
	}
	else if (periodic_y == 0)
	{
		if (me < NPROCx)                  //������һ�ź�
			low_exf = MPI_PROC_NULL;
		else
			low_exf = me - NPROCx;

		if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
			up_exf = MPI_PROC_NULL;
		else
			up_exf = me + NPROCx;
	}

	f_pack_start_time = MPI_Wtime();
	#if if_gptl
		gptl_start(GT_f_pack);
	#endif

#pragma omp parallel
	{
#pragma omp for collapse(2) schedule(static) private(y,z,j,y1,z1)
		for (y = 0; y <= Ny1; y++)
		{
			for (z = 0; z <= Nz1; z++)
			{
				y1 = y + 1;
				z1 = z + 1;

				f_ex[1][y][z][0] = f[2][y1][z1][10];
				f_ex[1][y][z][1] = f[2][y1][z1][13];
				f_ex[1][y][z][2] = f[2][y1][z1][14];
				f_ex[1][y][z][3] = f[2][y1][z1][15];
				f_ex[1][y][z][4] = f[2][y1][z1][16];

				f_ex[2][y][z][0] = f[Nx1][y1][z1][1];
				f_ex[2][y][z][1] = f[Nx1][y1][z1][4];
				f_ex[2][y][z][2] = f[Nx1][y1][z1][5];
				f_ex[2][y][z][3] = f[Nx1][y1][z1][6];
				f_ex[2][y][z][4] = f[Nx1][y1][z1][7];

			}
		}
	}

	f_change_start_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_f_pack);
		gptl_start(GT_f_change);
	#endif

	MPI_Sendrecv(&f_ex[2][0][0][0], Ny2 * Nz2 * q_send, MPI_DOUBLE, right_exf, 210, &f_ex[0][0][0][0], Ny2 * Nz2 * q_send, MPI_DOUBLE, left_exf, 210, MPI_COMM_WORLD, &status);
	MPI_Sendrecv(&f_ex[1][0][0][0], Ny2 * Nz2 * q_send, MPI_DOUBLE, left_exf, 220, &f_ex[3][0][0][0], Ny2 * Nz2 * q_send, MPI_DOUBLE, right_exf, 220, MPI_COMM_WORLD, &status);

	f_unpack_start_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_f_change);
		gptl_start(GT_f_unpack);
	#endif

#pragma omp parallel
	{
#pragma omp for collapse(2) schedule(static) private(y,z,j,y1,z1)
		for (y = 0; y <= Ny1; y++)
		{
			for (z = 0; z <= Nz1; z++)
			{
				y1 = y + 1;
				z1 = z + 1;
				if (left_exf != MPI_PROC_NULL)
				{
					f[1][y1][z1][1] = f_ex[0][y][z][0];
					f[1][y1][z1][4] = f_ex[0][y][z][1];
					f[1][y1][z1][5] = f_ex[0][y][z][2];
					f[1][y1][z1][6] = f_ex[0][y][z][3];
					f[1][y1][z1][7] = f_ex[0][y][z][4];
				}
				if (right_exf != MPI_PROC_NULL)
				{
					f[Nx2][y1][z1][10] = f_ex[3][y][z][0];
					f[Nx2][y1][z1][13] = f_ex[3][y][z][1];
					f[Nx2][y1][z1][14] = f_ex[3][y][z][2];
					f[Nx2][y1][z1][15] = f_ex[3][y][z][3];
					f[Nx2][y1][z1][16] = f_ex[3][y][z][4];
				}

			}
		}
	}

	f_unpack_end_time = MPI_Wtime();
	f_pack_time += f_change_start_time - f_pack_start_time;
	f_change_time += f_unpack_start_time - f_change_start_time;
	f_unpack_time += f_unpack_end_time - f_unpack_start_time;

	f_pack_start_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_f_unpack);
		gptl_start(GT_f_pack);
	#endif

#pragma omp parallel
	{
#pragma omp for collapse(2) schedule(static) private(x,z,j,x1,z1)
		for (x = 0; x <= Nx1; x++)
		{
			for (z = 0; z <= Nz1; z++)
			{
				x1 = x + 1;
				z1 = z + 1;

				f_ey[1][x][z][0] = f[x1][2][z1][5];
				f_ey[1][x][z][1] = f[x1][2][z1][11];
				f_ey[1][x][z][2] = f[x1][2][z1][13];
				f_ey[1][x][z][3] = f[x1][2][z1][17];
				f_ey[1][x][z][4] = f[x1][2][z1][18];

				f_ey[2][x][z][0] = f[x1][Ny1][z1][2];
				f_ey[2][x][z][1] = f[x1][Ny1][z1][4];
				f_ey[2][x][z][2] = f[x1][Ny1][z1][8];
				f_ey[2][x][z][3] = f[x1][Ny1][z1][9];
				f_ey[2][x][z][4] = f[x1][Ny1][z1][14];

			}
		}
	}

	f_change_start_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_f_pack);
		gptl_start(GT_f_change);
	#endif
	MPI_Sendrecv(&f_ey[2][0][0][0], Nx2 * Nz2 * q_send, MPI_DOUBLE, up_exf, 250, &f_ey[0][0][0][0], Nx2 * Nz2 * q_send, MPI_DOUBLE, low_exf, 250, MPI_COMM_WORLD, &status);
	MPI_Sendrecv(&f_ey[1][0][0][0], Nx2 * Nz2 * q_send, MPI_DOUBLE, low_exf, 260, &f_ey[3][0][0][0], Nx2 * Nz2 * q_send, MPI_DOUBLE, up_exf, 260, MPI_COMM_WORLD, &status);

	f_unpack_start_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_f_change);
		gptl_start(GT_f_unpack);
	#endif

#pragma omp parallel
	{
#pragma omp for collapse(2) schedule(static) private(x,z,j,x1,z1)
		for (x = 0; x <= Nx1; x++)
		{
			for (z = 0; z <= Nz1; z++)
			{
				x1 = x + 1;
				z1 = z + 1;
				if (low_exf != MPI_PROC_NULL)
				{
					f[x1][1][z1][2] = f_ey[0][x][z][0];
					f[x1][1][z1][4] = f_ey[0][x][z][1];
					f[x1][1][z1][8] = f_ey[0][x][z][2];
					f[x1][1][z1][9] = f_ey[0][x][z][3];
					f[x1][1][z1][14] = f_ey[0][x][z][4];
				}
				if (up_exf != MPI_PROC_NULL)
				{
					f[x1][Ny2][z1][5] = f_ey[3][x][z][0];
					f[x1][Ny2][z1][11] = f_ey[3][x][z][1];
					f[x1][Ny2][z1][13] = f_ey[3][x][z][2];
					f[x1][Ny2][z1][17] = f_ey[3][x][z][3];
					f[x1][Ny2][z1][18] = f_ey[3][x][z][4];
				}

			}
		}
	}

	f_unpack_end_time = MPI_Wtime();
	f_pack_time += f_change_start_time - f_pack_start_time;
	f_change_time += f_unpack_start_time - f_change_start_time;
	f_unpack_time += f_unpack_end_time - f_unpack_start_time;
	#if if_gptl
		gptl_stop(GT_f_unpack);
	#endif


	//z����������
	if (periodic_z == 1)
	{
#pragma omp parallel
		{
#pragma omp for collapse(2) schedule(static) private(x,y,j)
			for (x = 1; x <= Nx2; x++)
			{
				for (y = 1; y <= Ny2; y++)
				{
					/*
					for (j = 1; j < Q; j++)
					{
						f[x][y][1][j] = f[x][y][Nz1][j];
						f[x][y][Nz2][j] = f[x][y][2][j];
					}
					*/
					f[x][y][1][3] = f[x][y][Nz1][3];
					f[x][y][1][6] = f[x][y][Nz1][6];
					f[x][y][1][8] = f[x][y][Nz1][8];
					f[x][y][1][16] = f[x][y][Nz1][16];
					f[x][y][1][18] = f[x][y][Nz1][18];
					f[x][y][Nz2][7] = f[x][y][2][7];
					f[x][y][Nz2][9] = f[x][y][2][9];
					f[x][y][Nz2][12] = f[x][y][2][12];
					f[x][y][Nz2][15] = f[x][y][2][15];
					f[x][y][Nz2][17] = f[x][y][2][17];
				}
			}
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);
}

//--------------------------------------------------------------------------------------------------------
//stream�ĸ��Ҫ��֤��Χ��18����㶼��fp���У���˱߽�����������һ����Ĳ���stream���ɱ߽����������ݽ����õ�
void Fluid_stream(int me)
{
	int i, x, y, z, xd, yd, zd;

	#pragma omp parallel
	{
	#pragma omp for collapse(3) private(x, y, z, i, xd, yd, zd)
		for (x = 1; x <= Nx2; x++)
		{
			for (y = 1; y <= Ny2; y++)
			{
				for (z = 1; z <= Nz2; z++)
				{
					for (i = 1; i <= 9; i++)
					{
						xd = x + e[i][0];
						yd = y + e[i][1];
						zd = z + e[i][2];
						if (xd >= 1 && yd >= 1 && zd >= 1 && xd <= Nx2 && yd <= Ny2 && zd <= Nz2)
						{
							change(&f[x][y][z][i + 9], &f[xd][yd][zd][i]);
						}
					}
				}
			}
		}
	}
}

void Fluid_stream_test(int me)
{

}

static inline void change(double *val1, double *val2)
{
	double tmp = *val1;
	*val1 = *val2;
	*val2 = tmp;
}
void lub_force_particle(int me)
{
	int x, y, x1, y1, z1, z2, i, j, k, ii, jj, kk;
	int me_y, me_x;
	double Fgra, Fgra1;
	double lxc, lyc, lzc, lzc1, lc, npp1, npp2, npw1, npw2, xc_fict, yc_fict, zc_fict;
	double n, n1, n2;
	double Fpp_lub[np_local][3], Fpw_lub[np_local][3];
	double force_lub[3];
	double temp_x, temp_y, temp_z;
	int x_p0, y_p0, z_p0;
	int xx_lub, xxn_lub, yy_lub, yyn_lub, zz_lub, zzn_lub;
	double temp;

	me_y = int(me / NPROCx);
	me_x = me - me_y*NPROCx;

	// boyance and gravity
	Fgra = (1.0 - rhof / rhop)*mp*G;
	//Fgra = m*G;
	Fgra1 = (1.0 - 0.9*rhof / rhop)*mp*G;

	//����������ʼ��
	for (k = 0; k < np_local; k++)
	{
		for (i = 0; i < 3; i++)
		{
			Fpp_lub[k][i] = 0.0;
			Fpw_lub[k][i] = 0.0;
			F_lub[k][i] = 0.0;
		}
		/*
		if (num_p[k] >= 0)
		{
		j = num_p[k];
		F_lub[j][0] = 0.0; F_lub[j][1] = 0.0; F_lub[j][2] = 0.0;
		}*/
	}

	// lubrication force, see Simeonov & Calantoni, Int J. Multiphase Flow, 2012, 46:38; Feng & Michaelides, JCP, 2005:20-51
	for (k = 0; k < np_local; k++)
	{
		if (num_p[k] >= 0)
		{
			j = num_p[k];
			//if (KeyProcess[k] == me)
			//if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn) && (xc[j][2] >= 0) && (xc[j][2] < NzT))
			{
				for (kk = k + 1; kk < np_local; kk++)
				{
					if (num_p[kk] >= 0)
					{
						i = num_p[kk];
						if (i != j)
						{
							lxc = (xc[j][0] - xc[i][0]);
							lyc = (xc[j][1] - xc[i][1]);
							lzc1 = (xc[j][2] - xc[i][2]);
							/*
							if (xc[i][2] < 0)
							lzc += NzT;
							if (xc[i][2] > NzT)
							lzc -= NzT;

							if ((lzc > 0) && (fabs(lzc) > (NzT - 2 * R - safe)))
							lzc = lzc - NzT;
							if ((lzc <0) && (fabs(lzc)>(NzT - 2 * R - safe)))
							lzc = lzc + NzT;
							*/
							lzc = lzc1;
							if (periodic_z == 1)
							{
								if ((lzc1 > 0) && (fabs(lzc1) > (NzT - 2 * R - safe)))
									lzc = lzc1 - NzT;
								else if ((lzc1 < 0) && (fabs(lzc1) > (NzT - 2 * R - safe)))
									lzc = lzc1 + NzT;
							}

							for (ii = 0; ii < 3; ii++)
								force_lub[ii] = 0.0;

							lc = sqrt(lxc*lxc + lyc*lyc + lzc*lzc);
							/*
							if (lc <= (2 * R))
							{
							npp1 = (lc - R*2.0 - safe) / safe;
							npp2 = (R * 2.0 - lc) / safe;
							temp = (fabs(Fgra / epp1)*npp1*npp1 + fabs(Fgra / epp2)*npp2);
							force_lub[0] = temp*lxc / lc;
							force_lub[1] = temp*lyc / lc;
							force_lub[2] = temp*lzc / lc;

							for (ii = 0; ii < 3; ii++)
							{
							Fpp_lub[k][ii] += force_lub[ii];
							Fpp_lub[kk][ii] -= force_lub[ii];
							}
							}
							else if ((lc >(2 * R)) && (lc <= (2 * R + safe)))
							{
							npp1 = (lc - R*2.0 - safe) / safe;
							temp = (fabs(Fgra / epp1)*npp1*npp1);
							force_lub[0] = temp*lxc / lc;
							force_lub[1] = temp*lyc / lc;
							force_lub[2] = temp*lzc / lc;

							for (ii = 0; ii < 3; ii++)
							{
							Fpp_lub[k][ii] += force_lub[ii];
							Fpp_lub[kk][ii] -= force_lub[ii];
							}
							}*/

							//if ((lc < (2 * R + safe)) && (lc >(2 * R)))
							if (lc < (2 * R + safe)) 
							{
								temp = -6 * PI*rhof*mv*D*D / 16 * (1 / (lc - 2 * R) - 1 / safe) / dx;
								if (lc < (2 * R + safe1))
									temp = -6 * PI*rhof*mv*D*D / 16 * (1 / safe1 - 1 / safe) / dx;
								temp *= ((uc[j][0] - uc[i][0]) * lxc / lc + (uc[j][1] - uc[i][1]) * lyc / lc + (uc[j][2] - uc[i][2]) * lzc / lc);

								force_lub[0] = temp*lxc / lc;
								force_lub[1] = temp*lyc / lc;
								force_lub[2] = temp*lzc / lc;

								for (ii = 0; ii < 3; ii++)
								{
									Fpp_lub[k][ii] += force_lub[ii];
									Fpp_lub[kk][ii] -= force_lub[ii];
								}

								if (lc < (2 * R + safe1))
								{
									temp = k1_elas*fabs((2 * R + safe1) - lc);
									force_lub[0] = temp*lxc / lc;
									force_lub[1] = temp*lyc / lc;
									force_lub[2] = temp*lzc / lc;

									for (ii = 0; ii < 3; ii++)
									{
										Fpp_lub[k][ii] += force_lub[ii];
										Fpp_lub[kk][ii] -= force_lub[ii];
									}
								}
								else if (lc <= (2 * R))
								{
									temp = k1_elas*safe1 + k2_elas*fabs(R * 2.0 - lc);
									force_lub[0] = temp*lxc / lc;
									force_lub[1] = temp*lyc / lc;
									force_lub[2] = temp*lzc / lc;

									for (ii = 0; ii < 3; ii++)
									{
										Fpp_lub[k][ii] += force_lub[ii];
										Fpp_lub[kk][ii] -= force_lub[ii];
									}
								}
							}
							
						}
					}
				}
				//���������֮����ײ
				if (periodic == 0)   //������������������ײ
				{
					//left boundary
					if (xc[j][0] <= (R + safe))
					{
						xc_fict = -R;
						lxc = (xc[j][0] - xc_fict);
						lc = abs(lxc);
						npw1 = (lc - R*2.0 - safe) / safe;
						npw2 = (R * 2.0 - lc) / safe;
						if (lc <= (2 * R))
							Fpw_lub[k][0] += (fabs(Fgra / epw1)*npw1*npw1 + fabs(Fgra / epw2)*npw2)*lxc / lc;
						else if ((lc > (2 * R)) && (lc <= (2 * R + safe)))
							Fpw_lub[k][0] += (fabs(Fgra / epw1)*npw1*npw1)*lxc / lc;
						else
							Fpw_lub[k][0] += 0.0;
					}

					//right boundary
					if (xc[j][0] >= (NxT - R - safe))
					{
						xc_fict = NxT + R;
						lxc = (xc[j][0] - xc_fict);
						lc = abs(lxc);
						npw1 = (lc - R*2.0 - safe) / safe;
						npw2 = (R * 2.0 - lc) / safe;
						if (lc <= (2 * R))
							Fpw_lub[k][0] += (fabs(Fgra / epw1)*npw1*npw1 + fabs(Fgra / epw2)*npw2)*lxc / lc;
						else if ((lc > (2 * R)) && (lc <= (2 * R + safe)))
							Fpw_lub[k][0] += (fabs(Fgra / epw1)*npw1*npw1)*lxc / lc;
						else
							Fpw_lub[k][0] += 0.0;
					}

					//bottom boundary		
					if (xc[j][1] <= (R + safe))
					{
						yc_fict = -R;
						lyc = (xc[j][1] - yc_fict);
						lc = abs(lyc);
						npw1 = (lc - R*2.0 - safe) / safe;
						npw2 = (R * 2.0 - lc) / safe;
						if (lc <= (2 * R))
							Fpw_lub[k][1] += (fabs(Fgra / epw1)*npw1*npw1 + fabs(Fgra / epw2)*npw2)*lyc / lc;
						else if ((lc > (2 * R)) && (lc <= (2 * R + safe)))
							Fpw_lub[k][1] += (fabs(Fgra / epw1)*npw1*npw1)*lyc / lc;
						else
							Fpw_lub[k][1] += 0.0;
					}

					//top boundary
					if (xc[j][1] >= (NyT - R - safe))
					{
						yc_fict = NyT + R;
						lyc = (xc[j][1] - yc_fict);
						lc = abs(lyc);
						npw1 = (lc - R*2.0 - safe) / safe;
						npw2 = (R * 2.0 - lc) / safe;
						if (lc <= (2 * R))
							Fpw_lub[k][1] += (fabs(Fgra / epw1)*npw1*npw1 + fabs(Fgra / epw2)*npw2)*lyc / lc;
						else if ((lc > (2 * R)) && (lc <= (2 * R + safe)))
							Fpw_lub[k][1] += (fabs(Fgra / epw1)*npw1*npw1)*lyc / lc;
						else
							Fpw_lub[k][1] += 0.0;
					}

					//front boundary
					if (xc[j][2] <= (R + safe))
					{
						zc_fict = -R;
						lzc = (xc[j][2] - zc_fict);
						lc = abs(lzc);
						npw1 = (lc - R*2.0 - safe) / safe;
						npw2 = (R * 2.0 - lc) / safe;
						if (lc <= (2 * R))
							Fpw_lub[k][2] += (fabs(Fgra / epw1)*npw1*npw1 + fabs(Fgra / epw2)*npw2)*lzc / lc;
						else if ((lc > (2 * R)) && (lc <= (2 * R + safe)))
							Fpw_lub[k][2] += (fabs(Fgra / epw1)*npw1*npw1)*lzc / lc;
						else
							Fpw_lub[k][2] += 0.0;
					}

					//back boundary
					if (xc[j][2] >= (NzT - R - safe))
					{
						zc_fict = NzT + R;
						lzc = (xc[j][2] - zc_fict);
						lc = abs(lzc);
						npw1 = (lc - R*2.0 - safe) / safe;
						npw2 = (R * 2.0 - lc) / safe;
						if (lc <= (2 * R))
							Fpw_lub[k][2] += (fabs(Fgra / epw1)*npw1*npw1 + fabs(Fgra / epw2)*npw2)*lzc / lc;
						else if ((lc > (2 * R)) && (lc <= (2 * R + safe)))
							Fpw_lub[k][2] += (fabs(Fgra / epw1)*npw1*npw1)*lzc / lc;
						else
							Fpw_lub[k][2] += 0.0;
					}
				}
			}
		}
	}

	for (k = 0; k < np_local; k++)
	{
		if (num_p[k] >= 0)
		{
			//j = num_p[k];
			for (i = 0; i < 3; i++)
				F_lub[k][i] = Fpp_lub[k][i] + Fpw_lub[k][i];
		}
	}
}

#if iflub_DEM_output
void ensure_shared_file_open(int me, int step) {
	int interval = step / 1000;
	int need_open = 0;

	// 检查是否需要打开/切换文件（无锁快速路径）
	if (shared_contact_file == NULL || interval != shared_contact_interval) {
		need_open = 1;
	}

	if (need_open) {
#pragma omp critical (contact_file_open)
		{
			// 双重检查
			if (shared_contact_file == NULL || interval != shared_contact_interval) {
				if (shared_contact_file != NULL) {
					fclose(shared_contact_file);
					shared_contact_file = NULL;
				}
				char fname[256];
				sprintf(fname, "lub_DEM_rank_%d_%d000-%d000.dat",
					me, interval, interval + 1);
				shared_contact_file = fopen(fname, "a");
				if (shared_contact_file) {
					setvbuf(shared_contact_file, NULL, _IOFBF, 1024 * 1024);
					shared_contact_interval = interval;
				}
			}
		}
	}
}
#endif


void lub_and_DEM_force_particle(int me)
{
	int x, y, x1, y1, z1, z2, i, j, k, ii, jj, kk, k1, n1, n2, n3;
	int me_y, me_x;
	double Fgra;
	double lxc, lyc, lzc, lzc1, lc;
	//double n, n1, n2;
	double Fpp_lub[np_local][3], Fpp_n_DEM[np_local][3], Fpp_t_DEM[np_local][3], Tpp_t_DEM[np_local][3];
	double tangential_overlap_temp[np_local][max_contact_num][3], contact_uji_t[max_contact_num][3], overlap_temp[3];
	int	   tangential_contact_index_temp[np_local][max_contact_num], contact_uji_t_index[max_contact_num];
	//double contact_uji_t[max_contact_num][3];
	//int    contact_uji_t_index[max_contact_num];
	double force_lub[3], force_n_DEM[3], force_t_DEM[3], torque_t_DEM[3];
	double temp_x, temp_y, temp_z;
	int x_p0, y_p0, z_p0;
	int xx_lub, xxn_lub, yy_lub, yyn_lub, zz_lub, zzn_lub;
	double k_t2_temp, eta_n2_temp, eta_t2_temp;
	double temp, temp2,temp3, epsilon_temp;
	double ujitemp_n[3], ujitemp_t[3];
	double temp4[4];
	int contact_num, last_num,lastflag,flag, jitemp, ijtemp;
	int contact_flag[max_contact_num];
	//static FILE* contact_file = NULL;
	//char filename[100];
	//static char current_filename[100] = "";
	//int write_flag;
	//static int file_has_written = 0;

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;

	// boyance and gravity
	Fgra = (1.0 - rhof / rhop) * mp * G;
#pragma omp parallel 
	{
		double force_lub[3], force_n_DEM[3], force_t_DEM[3], torque_t_DEM[3];
		double k_t2_temp, eta_n2_temp, eta_t2_temp;
		double temp, temp2, temp3, epsilon_temp;
		double ujitemp_n[3], ujitemp_t[3];
		double temp4[4];
		int contact_num, last_num, lastflag, flag, jitemp, ijtemp;
		int contact_flag[max_contact_num], contact_uji_t_index[max_contact_num];
		double contact_uji_t[max_contact_num][3];

		FILE* thread_contact_file = NULL;
		char thread_filename[100];
		int thread_file_has_written = 0;

		int thread_id = omp_get_thread_num();

		if (iflub_DEM_test) {
			// 每个线程创建自己的文件
			sprintf(thread_filename, "step_%d000-%d000_debug_poly_rank_%d_thread_%d.log",
				int(step / 1000), int(step / 1000) + 1, me, thread_id);

			thread_contact_file = fopen(thread_filename, "a");
			thread_file_has_written = 0;
		}
#if (iflub_DEM_output == 1)
		ensure_shared_file_open(me, step);
#endif

	#pragma omp for schedule(static) private(k, i, kk)
		for (k = 0; k < np_local; k++)
		{
			for (i = 0; i < 3; i++)
			{
				Fpp_lub[k][i] = 0.0;
				Fpp_n_DEM[k][i] = 0.0;
				Fpp_t_DEM[k][i] = 0.0;
				Tpp_t_DEM[k][i] = 0.0;
				F_lub[k][i] = 0.0;
				F_n_DEM[k][i] = 0.0;
				F_t_DEM[k][i] = 0.0;
				T_DEM[k][i] = 0.0;
			}

			for (kk = 0; kk < max_contact_num; kk++)
			{
				tangential_contact_index_temp[k][kk] = -1; //contact particle index
				tangential_overlap_temp[k][kk][0] = 0.0;	//tangential overlap of dimensions
				tangential_overlap_temp[k][kk][1] = 0.0;
				tangential_overlap_temp[k][kk][2] = 0.0;
			}

		}
	#pragma omp barrier

		//calculate tangential overlap using tangential_overlap[np][max_contact_num][4]
	#pragma omp for schedule(dynamic,10) private(k,j,kk,i,n1,n2,ii,\
												lxc,lyc,lzc,lzc1,lc)
		for (k = 0; k < np_local; k++)
		{
			if (num_p[k] >= 0)
			{
				j = num_p[k];
				{
					last_num = 0;
					for (n2 = 0; n2 < max_contact_num; n2++)
					{
						if (tangential_contact_index[j][n2] >= 0)
						{
							tangential_contact_index_temp[k][last_num] = tangential_contact_index[j][n2];
							tangential_overlap_temp[k][last_num][0] = tangential_overlap[j][n2][0];
							tangential_overlap_temp[k][last_num][1] = tangential_overlap[j][n2][1];
							tangential_overlap_temp[k][last_num][2] = tangential_overlap[j][n2][2];
							last_num++;
						}
					}
					for (ii = 0; ii < max_contact_num; ii++)
					{
						contact_flag[ii] = 0;
						//contact_uji_t_index[ii] = -1;//don't need to initialize,it works by contact_flag[ii]
						contact_uji_t[ii][0] = 0.0;
						contact_uji_t[ii][1] = 0.0;
						contact_uji_t[ii][2] = 0.0;
					}
					jitemp = 0;
					contact_num = 0;
					for (kk = 0; kk < np_local; kk++)
					{
						if (num_p[kk] >= 0)
						{
							i = num_p[kk];
							if (i != j)
							{
								lxc = (xc[i][0] - xc[j][0]);
								lyc = (xc[i][1] - xc[j][1]);
								lzc1 = (xc[i][2] - xc[j][2]);
								lzc = lzc1;
								if (periodic_z == 1)
								{
									if ((lzc1 > 0) && (fabs(lzc1) > (NzT / 2)))
										lzc = lzc1 - NzT;
									else if ((lzc1 < 0) && (fabs(lzc1) > (NzT / 2)))
										lzc = lzc1 + NzT;
								}
								lc = sqrt(lxc * lxc + lyc * lyc + lzc * lzc);

								if (lc < (2 * R))
								{
									/*
									if (n1 >= max_contact_num) {
										fprintf(stderr, "Rank %d: contact_num overflow!\n", me);
										break;
									}
									*/
									contact_flag[jitemp] = 1;
									temp = ((uc[j][0] - uc[i][0]) * lxc / lc + (uc[j][1] - uc[i][1]) * lyc / lc + (uc[j][2] - uc[i][2]) * lzc / lc);// uji dot normal
									ujitemp_n[0] = temp * lxc / lc;
									ujitemp_n[1] = temp * lyc / lc;
									ujitemp_n[2] = temp * lzc / lc;//uji_normal
									contact_uji_t_index[jitemp] = i;
									contact_uji_t[jitemp][0] = uc[j][0] - uc[i][0] - ujitemp_n[0] + D / 2 * ((wc[j][1] + wc[i][1]) * lzc / lc - (wc[j][2] + wc[i][2]) * lyc / lc);
									contact_uji_t[jitemp][1] = uc[j][1] - uc[i][1] - ujitemp_n[1] + D / 2 * ((wc[j][2] + wc[i][2]) * lxc / lc - (wc[j][0] + wc[i][0]) * lzc / lc);
									contact_uji_t[jitemp][2] = uc[j][2] - uc[i][2] - ujitemp_n[2] + D / 2 * ((wc[j][0] + wc[i][0]) * lyc / lc - (wc[j][1] + wc[i][1]) * lxc / lc);
									jitemp++;
									//printf("paticle_num\t%d\t%d\t%f\n", j + 1, i + 1, lc);
								}
							}
						}
					}
					contact_num = jitemp;
					if (last_num > 0)
					{
						//fprintf(contact_file, "Rank:%d\tstep=%d\tstep_DEM=%d\tparticle_num=%d\tlast_num=%d\n", me, step, step_DEM, j+1,last_num);

						for (n1 = 0; n1 < last_num; n1++)
						{
							if (tangential_contact_index_temp[k][n1] >= 0)
							{
								lastflag = 1;
								for (n2 = 0; n2 < contact_num; n2++)
								{
									if (tangential_contact_index_temp[k][n1] == contact_uji_t_index[n2])//update tangential displacement 
									{
										lastflag = 0;
										contact_flag[n2] = 0;
										//temp = (tangential_overlap_temp[k][n1][0] * lxc / lc + tangential_overlap_temp[k][n1][1] * lyc / lc + tangential_overlap_temp[k][n1][2] * lzc / lc);
										//overlap_temp[0] = tangential_overlap_temp[k][n1][0] - temp * lxc / lc;
										//overlap_temp[1] = tangential_overlap_temp[k][n1][1] - temp * lyc / lc;
										//overlap_temp[2] = tangential_overlap_temp[k][n1][2] - temp * lzc / lc;
										//temp2 = sqrt((tangential_overlap_temp[k][n1][0] * tangential_overlap_temp[k][n1][0] + tangential_overlap_temp[k][n1][1] * tangential_overlap_temp[k][n1][1] + tangential_overlap_temp[k][n1][2] * tangential_overlap_temp[k][n1][2])
										//	/ (overlap_temp[0] * overlap_temp[0] + overlap_temp[1] * overlap_temp[1] + overlap_temp[2] * overlap_temp[2]));
										//fprintf(contact_file, "Rank:%d\tstep=%d\tstep_DEM=%d\tparticle_num=%d\tlastoverlap=%f\t%f\t%f\tnor_overlap=%f\t%f\t%f\ttemp2=%f\t", me, step, step_DEM, j + 1, tangential_overlap_temp[k][n1][0], tangential_overlap_temp[k][n1][1], tangential_overlap_temp[k][n1][2],overlap_temp[0], overlap_temp[1], overlap_temp[2],temp2 );

										//tangential_overlap_temp[k][n1][0] = overlap_temp[0] * temp2 + contact_uji_t[n2][0] * dt_DEM;
										//tangential_overlap_temp[k][n1][1] = overlap_temp[1] * temp2 + contact_uji_t[n2][1] * dt_DEM;
										//tangential_overlap_temp[k][n1][2] = overlap_temp[2] * temp2 + contact_uji_t[n2][2] * dt_DEM;
										tangential_overlap_temp[k][n1][0] += contact_uji_t[n2][0] * dt_DEM;
										tangential_overlap_temp[k][n1][1] += contact_uji_t[n2][1] * dt_DEM;
										tangential_overlap_temp[k][n1][2] += contact_uji_t[n2][2] * dt_DEM;
										//fprintf(contact_file, "now_overlap=%f\t%f\t%f\n", tangential_overlap_temp[k][n1][0], tangential_overlap_temp[k][n1][1], tangential_overlap_temp[k][n1][2]);
										break;
									}
								}
								if (lastflag == 1)//pair contact is not exit, delete the contact
								{
									tangential_contact_index_temp[k][n1] = -1;
									tangential_overlap_temp[k][n1][0] = 0.0;
									tangential_overlap_temp[k][n1][1] = 0.0;
									tangential_overlap_temp[k][n1][2] = 0.0;
								}
							}
						}
					}
					if (contact_num > 0)
					{
						for (n1 = 0; n1 < contact_num; n1++)
						{
							if (contact_flag[n1] == 1)//new contact pair
							{
								for (n2 = 0; n2 < max_contact_num; n2++)
								{
									if (tangential_contact_index_temp[k][n2] < 0)
									{
										contact_flag[n1] = 0;
										tangential_contact_index_temp[k][n2] = contact_uji_t_index[n1];
										tangential_overlap_temp[k][n2][0] = contact_uji_t[n1][0] * dt_DEM;
										tangential_overlap_temp[k][n2][1] = contact_uji_t[n1][1] * dt_DEM;
										tangential_overlap_temp[k][n2][2] = contact_uji_t[n1][2] * dt_DEM;
										break;//Find the first available assignment and exit the loop
									}
									/*
									if (contact_flag[n1] > 0)
									{
										fprintf(stderr, "Rank %d: contact_num overflow2!\n", me);
										break;
									}
									*/
								}
							}
						}
					}
				}
			}
		}
	#pragma omp barrier

	#pragma omp for schedule(static)private(k,j,n1)
		for (k = 0; k < np_local; k++)
		{
			if (num_p[k] >= 0)
			{
				j = num_p[k];
				for (n1 = 0; n1 < max_contact_num; n1++)
				{
					tangential_contact_index[j][n1] = tangential_contact_index_temp[k][n1];
					tangential_overlap[j][n1][0] = tangential_overlap_temp[k][n1][0];
					tangential_overlap[j][n1][1] = tangential_overlap_temp[k][n1][1];
					tangential_overlap[j][n1][2] = tangential_overlap_temp[k][n1][2];
				}
			}
		}
	#pragma omp barrier

		//lubrication force ,see Xia Y, Physics of Fluids, 2020, 32, 10
	#pragma omp for schedule(dynamic,10) private(k,j,kk,i,n1,n2,ii,\
												lxc,lyc,lzc,lzc1,lc)
		for (k = 0; k < np_local; k++)
		{
			if (num_p[k] >= 0)
			{
				j = num_p[k];
				{
					for (kk = k + 1; kk < np_local; kk++)
					{
						if (num_p[kk] >= 0)
						{
							i = num_p[kk];
							if (i != j)
							{
								lxc = (xc[i][0] - xc[j][0]);
								lyc = (xc[i][1] - xc[j][1]);
								lzc1 = (xc[i][2] - xc[j][2]);
								lzc = lzc1;
								if (periodic_z == 1)
								{
									if ((lzc1 > 0) && (fabs(lzc1) > (NzT / 2)))
										lzc = lzc1 - NzT;
									else if ((lzc1 < 0) && (fabs(lzc1) > (NzT / 2)))
										lzc = lzc1 + NzT;
								}
								for (ii = 0; ii < 3; ii++)
								{
									force_lub[ii] = 0.0;
									force_n_DEM[ii] = 0.0;
									force_t_DEM[ii] = 0.0;
									torque_t_DEM[ii] = 0.0;
								}
								lc = sqrt(lxc * lxc + lyc * lyc + lzc * lzc);

								if (iflub > 0)
								{
									epsilon_temp = (lc - 2 * R) / R;
									//printf("lub_and_DEM");
									if (epsilon_temp < epsilon_act)
									{
										//printf("epsilon_temp=%f\t", epsilon_temp);
										//printf("11 = %f\t", log(epsilon_temp));
										if (epsilon_temp >= epsilon_max)
										{
											temp = (1.0 / 4.0 / epsilon_temp - 9.0 / 40.0 * log(epsilon_temp) - 3.0 / 112.0 * epsilon_temp * log(epsilon_temp) + 0.673);
											//printf("lub temp1=%f\t", temp);
										}
										else if (epsilon_temp < epsilon_max)
										{
											temp = (1.0 / 4.0 / epsilon_max - 9.0 / 40.0 * log(epsilon_max) - 3.0 / 112.0 * epsilon_max * log(epsilon_max) + 0.673);
											//printf("lub temp2=%f\t", temp);
											if (epsilon_temp < -epsilon_dis)
											{
												temp = (1.0 / 4.0 / epsilon_act - 9.0 / 40.0 * log(epsilon_act) - 3.0 / 112.0 * epsilon_act * log(epsilon_act) + 0.673);
												//printf("lub temp3=%f\t", temp);
											}
										}
										//temp -= (1.0 / 4.0 / epsilon_act - 9.0 / 40.0 * log(epsilon_act) - 3.0 / 112.0 * epsilon_act * log(epsilon_act) + 0.673);
										temp2 = temp - (1.0 / 4.0 / epsilon_act - 9.0 / 40.0 * log(epsilon_act) - 3.0 / 112.0 * epsilon_act * log(epsilon_act) + 0.673);
										//temp *= ((uc[j][0] - uc[i][0]) * lxc / lc + (uc[j][1] - uc[i][1]) * lyc / lc + (uc[j][2] - uc[i][2]) * lzc / lc) * -6 * PI * mv * D / 2;
										temp3 = temp2 * ((uc[j][0] - uc[i][0]) * lxc / lc + (uc[j][1] - uc[i][1]) * lyc / lc + (uc[j][2] - uc[i][2]) * lzc / lc) * -6 * PI * rhof * mv * D / 2;
										force_lub[0] = temp3 * lxc / lc;
										force_lub[1] = temp3 * lyc / lc;
										force_lub[2] = temp3 * lzc / lc;

										for (ii = 0; ii < 3; ii++)
										{
											#pragma omp atomic
											Fpp_lub[k][ii] += force_lub[ii];
											#pragma omp atomic
											Fpp_lub[kk][ii] -= force_lub[ii];
										}
									}
								}

								if (lc < 2 * R)
								{
									temp = ((uc[j][0] - uc[i][0]) * lxc / lc + (uc[j][1] - uc[i][1]) * lyc / lc + (uc[j][2] - uc[i][2]) * lzc / lc);// uji dot normal
									ujitemp_n[0] = temp * lxc / lc;
									ujitemp_n[1] = temp * lyc / lc;
									ujitemp_n[2] = temp * lzc / lc;
									ujitemp_t[0] = uc[j][0] - uc[i][0] - ujitemp_n[0] + D / 2 * ((wc[j][1] + wc[i][1]) * lzc / lc - ((wc[j][2] + wc[i][2]) * lyc / lc));
									ujitemp_t[1] = uc[j][1] - uc[i][1] - ujitemp_n[1] + D / 2 * ((wc[j][2] + wc[i][2]) * lxc / lc - ((wc[j][0] + wc[i][0]) * lzc / lc));
									ujitemp_t[2] = uc[j][2] - uc[i][2] - ujitemp_n[2] + D / 2 * ((wc[j][0] + wc[i][0]) * lyc / lc - ((wc[j][1] + wc[i][1]) * lxc / lc));
									if (if_linear_DEM == 1)
									{
										force_n_DEM[0] = -K_n * (R * 2.0 - lc) * dx * lxc / lc - ujitemp_n[0] * eta_n;
										force_n_DEM[1] = -K_n * (R * 2.0 - lc) * dx * lyc / lc - ujitemp_n[1] * eta_n;
										force_n_DEM[2] = -K_n * (R * 2.0 - lc) * dx * lzc / lc - ujitemp_n[2] * eta_n;
									}
									else
									{
										eta_n2_temp = eta_n2 * pow(((R * 2.0 - lc) * dx), 0.25);
										force_n_DEM[0] = -K_n2 * pow(((R * 2.0 - lc) * dx), 1.5) * lxc / lc - ujitemp_n[0] * eta_n2_temp;
										force_n_DEM[1] = -K_n2 * pow(((R * 2.0 - lc) * dx), 1.5) * lyc / lc - ujitemp_n[1] * eta_n2_temp;
										force_n_DEM[2] = -K_n2 * pow(((R * 2.0 - lc) * dx), 1.5) * lzc / lc - ujitemp_n[2] * eta_n2_temp;
									}
									flag = 0;
									for (n1 = 0; n1 < max_contact_num; n1++)
									{
										if (tangential_contact_index[j][n1] == i)
										{
											flag += 1;
											temp = sqrt(tangential_overlap[j][n1][0] * tangential_overlap[j][n1][0] + tangential_overlap[j][n1][1] * tangential_overlap[j][n1][1] +
												tangential_overlap[j][n1][2] * tangential_overlap[j][n1][2]);
											//temp2 = sqrt(ujitemp_t[0] * ujitemp_t[0] + ujitemp_t[1] * ujitemp_t[1] + ujitemp_t[2] * ujitemp_t[2]);
											if (temp > 0.0)
											{
												temp3 = (tangential_overlap[j][n1][0] * ujitemp_t[0] + tangential_overlap[j][n1][1] * ujitemp_t[1] +
													tangential_overlap[j][n1][2] * ujitemp_t[2]) / temp;

												if (if_linear_DEM)
												{
													//force_t_DEM[0] = -K_t * tangential_overlap[j][n1][0] - eta_t * temp3 * tangential_overlap[j][n1][0] / temp;
													//force_t_DEM[1] = -K_t * tangential_overlap[j][n1][1] - eta_t * temp3 * tangential_overlap[j][n1][1] / temp;
													//force_t_DEM[2] = -K_t * tangential_overlap[j][n1][2] - eta_t * temp3 * tangential_overlap[j][n1][2] / temp;
													force_t_DEM[0] = -K_t * tangential_overlap[j][n1][0] - eta_t * ujitemp_t[0];
													force_t_DEM[1] = -K_t * tangential_overlap[j][n1][1] - eta_t * ujitemp_t[1];
													force_t_DEM[2] = -K_t * tangential_overlap[j][n1][2] - eta_t * ujitemp_t[2];
												}
												else
												{
													k_t2_temp = K_t2 * sqrt((R * 2.0 - lc) * dx);
													eta_t2_temp = eta_t2 * pow(temp, 0.25) * pow(((R * 2.0 - lc) * dx), 0.25);
													//force_t_DEM[0] = -k_t2_temp * tangential_overlap[j][n1][0] - eta_t2_temp * temp3 * tangential_overlap[j][n1][0] / temp;
													//force_t_DEM[1] = -k_t2_temp * tangential_overlap[j][n1][1] - eta_t2_temp * temp3 * tangential_overlap[j][n1][1] / temp;
													//force_t_DEM[2] = -k_t2_temp * tangential_overlap[j][n1][2] - eta_t2_temp * temp3 * tangential_overlap[j][n1][2] / temp;
													force_t_DEM[0] = -k_t2_temp * tangential_overlap[j][n1][0] - eta_t2_temp * ujitemp_t[0];
													force_t_DEM[1] = -k_t2_temp * tangential_overlap[j][n1][1] - eta_t2_temp * ujitemp_t[1];
													force_t_DEM[2] = -k_t2_temp * tangential_overlap[j][n1][2] - eta_t2_temp * ujitemp_t[2];
												}
												for (n2 = 0; n2 < max_contact_num; n2++)
												{
													if (tangential_contact_index[i][n2] == j)
													{
														ijtemp = n2;
														flag += 2;
														break;
													}
												}
												temp2 = 0;
												if ((force_n_DEM[0] * force_n_DEM[0] + force_n_DEM[1] * force_n_DEM[1] + force_n_DEM[2] * force_n_DEM[2]) * miu_c_DEM * miu_c_DEM <
													(force_t_DEM[0] * force_t_DEM[0] + force_t_DEM[1] * force_t_DEM[1] + force_t_DEM[2] * force_t_DEM[2]))
												{
													temp2 = 1;
													temp4[0] = force_t_DEM[0];
													temp4[1] = force_t_DEM[1];
													temp4[2] = force_t_DEM[2];
													temp4[3] = sqrt(force_t_DEM[0] * force_t_DEM[0] + force_t_DEM[1] * force_t_DEM[1] + force_t_DEM[2] * force_t_DEM[2]);
													force_t_DEM[0] = sqrt(force_n_DEM[0] * force_n_DEM[0] + force_n_DEM[1] * force_n_DEM[1] + force_n_DEM[2] * force_n_DEM[2]) * miu_c_DEM * temp4[0] / temp4[3];
													force_t_DEM[1] = sqrt(force_n_DEM[0] * force_n_DEM[0] + force_n_DEM[1] * force_n_DEM[1] + force_n_DEM[2] * force_n_DEM[2]) * miu_c_DEM * temp4[1] / temp4[3];
													force_t_DEM[2] = sqrt(force_n_DEM[0] * force_n_DEM[0] + force_n_DEM[1] * force_n_DEM[1] + force_n_DEM[2] * force_n_DEM[2]) * miu_c_DEM * temp4[2] / temp4[3];
													if (if_linear_DEM)
													{
														//tangential_overlap[j][n1][0] = -(force_t_DEM[0] + eta_t * temp3 * tangential_overlap[j][n1][0] / temp) / K_t;
														//tangential_overlap[j][n1][1] = -(force_t_DEM[1] + eta_t * temp3 * tangential_overlap[j][n1][1] / temp) / K_t;
														//tangential_overlap[j][n1][2] = -(force_t_DEM[2] + eta_t * temp3 * tangential_overlap[j][n1][2] / temp) / K_t;
														//#pragma omp atomic
														tangential_overlap[j][n1][0] = -(force_t_DEM[0] + eta_t * ujitemp_t[0]) / K_t;
														//#pragma omp atomic
														tangential_overlap[j][n1][1] = -(force_t_DEM[1] + eta_t * ujitemp_t[1]) / K_t;
														//#pragma omp atomic
														tangential_overlap[j][n1][2] = -(force_t_DEM[2] + eta_t * ujitemp_t[2]) / K_t;
														//#pragma omp atomic
														tangential_overlap[i][ijtemp][0] = (force_t_DEM[0] + eta_t * ujitemp_t[0]) / K_t;
														//#pragma omp atomic
														tangential_overlap[i][ijtemp][1] = (force_t_DEM[1] + eta_t * ujitemp_t[1]) / K_t;
														//#pragma omp atomic
														tangential_overlap[i][ijtemp][2] = (force_t_DEM[2] + eta_t * ujitemp_t[2]) / K_t;
													}
													else
													{
														//tangential_overlap[j][n1][0] = -(force_t_DEM[0] + eta_t2_temp * temp3 * tangential_overlap[j][n1][0] / temp) / k_t2_temp;
														//tangential_overlap[j][n1][1] = -(force_t_DEM[1] + eta_t2_temp * temp3 * tangential_overlap[j][n1][1] / temp) / k_t2_temp;
														//tangential_overlap[j][n1][2] = -(force_t_DEM[2] + eta_t2_temp * temp3 * tangential_overlap[j][n1][2] / temp) / k_t2_temp;
														//#pragma omp atomic		
														tangential_overlap[j][n1][0] = -(force_t_DEM[0] + eta_t2_temp * ujitemp_t[0]) / k_t2_temp;
														//#pragma omp atomic
														tangential_overlap[j][n1][1] = -(force_t_DEM[1] + eta_t2_temp * ujitemp_t[1]) / k_t2_temp;
														//#pragma omp atomic
														tangential_overlap[j][n1][2] = -(force_t_DEM[2] + eta_t2_temp * ujitemp_t[2]) / k_t2_temp;
														//#pragma omp atomic
														tangential_overlap[i][ijtemp][0] = (force_t_DEM[0] + eta_t2_temp * ujitemp_t[0]) / k_t2_temp;
														//#pragma omp atomic
														tangential_overlap[i][ijtemp][1] = (force_t_DEM[1] + eta_t2_temp * ujitemp_t[1]) / k_t2_temp;
														//#pragma omp atomic
														tangential_overlap[i][ijtemp][2] = (force_t_DEM[2] + eta_t2_temp * ujitemp_t[2]) / k_t2_temp;
													}
												}
											}
											else
											{
												if (iflub_DEM_test)
												{
													fprintf(thread_contact_file, "time=%d only normal contact no tangential overlap\n", step);
													thread_file_has_written = 1;
												}
												else
													printf("time=%d only normal contact no tangential overlap\n", step);
											}
											break;
										}
									}
									if (flag == 0)
									{
										if (iflub_DEM_test)
										{
											if (thread_contact_file)
											{
												fprintf(thread_contact_file, "Rank:%d time=%d particle %d and particle %d j_th particle didn't find i_th particle at distance = %f\n", me, step, j + 1, i + 1, lc);
												thread_file_has_written = 1;
											}
										}
										else
											printf("Rank:%d time=%d particle %d and particle %d j_th particle didn't find i_th particle at distance = %f\n", me, step, j + 1, i + 1, lc);
									}
									else if (flag == 1)
									{
										if (iflub_DEM_test)
										{
											if (thread_contact_file)
												fprintf(thread_contact_file, "Rank:%d time=%d particle %d and particle %d i_th particle didn't find j_th particle at distance = %f\n", me, step, j + 1, i + 1, lc);
											thread_file_has_written = 1;
										}
										else
											printf("Rank:%d time=%d particle %d and particle %d i_th particle didn't find j_th particle at distance = %f\n", me, step, j + 1, i + 1, lc);
									}


									torque_t_DEM[0] = (force_t_DEM[2] * lyc - force_t_DEM[1] * lzc) / lc * D / 2;
									torque_t_DEM[1] = (force_t_DEM[0] * lzc - force_t_DEM[2] * lxc) / lc * D / 2;
									torque_t_DEM[2] = (force_t_DEM[1] * lxc - force_t_DEM[0] * lyc) / lc * D / 2;
									for (ii = 0; ii < 3; ii++)
									{
										#pragma omp atomic
										Fpp_n_DEM[k][ii] += force_n_DEM[ii];
										#pragma omp atomic
										Fpp_n_DEM[kk][ii] -= force_n_DEM[ii];
										#pragma omp atomic
										Fpp_t_DEM[k][ii] += force_t_DEM[ii];
										#pragma omp atomic
										Fpp_t_DEM[kk][ii] -= force_t_DEM[ii];
										#pragma omp atomic
										Tpp_t_DEM[k][ii] += torque_t_DEM[ii];
										#pragma omp atomic
										Tpp_t_DEM[kk][ii] += torque_t_DEM[ii];//tangential torque of two particle by DEM should be the same direction
									}
									
									if (iflub_DEM_test)
									{
										if (thread_contact_file)
										{
											thread_file_has_written = 1;
											fprintf(thread_contact_file, "Dem\tRank:%d\tstep=%d\tstep_DEM=%d\tdelta=%f\n", me, step, step_DEM, lc - 2 * R);
											fprintf(thread_contact_file, "%d\tx=%f\t%f\t%f\tu=%f\t%f\t%f\tw=%f\t%f\t%f\n", j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], wc[j][0], wc[j][1], wc[j][2]);
											fprintf(thread_contact_file, "ucc=%f\t%f\t%f\twcc=%f\t%f\t%f\n", ucc[j][0], ucc[j][1], ucc[j][2], wcc[j][0], wcc[j][1], wcc[j][2]);
											fprintf(thread_contact_file, "F_IBM =%f\t%f\t%f\tT_IBM =%f\t%f\t%f\n", F_IBM[j][0], F_IBM[j][1], F_IBM[j][2], T_IBM[j][0], T_IBM[j][1], T_IBM[j][2]);
											fprintf(thread_contact_file, "%d\tx=%f\t%f\t%f\tu=%f\t%f\t%f\tw=%f\t%f\t%f\n", i + 1, xc[i][0], xc[i][1], xc[i][2], uc[i][0], uc[i][1], uc[i][2], wc[i][0], wc[i][1], wc[i][2]);
											fprintf(thread_contact_file, "F_IBM =%f\t%f\t%f\tT_IBM =%f\t%f\t%f\n", F_IBM[i][0], F_IBM[i][1], F_IBM[i][2], T_IBM[i][0], T_IBM[i][1], T_IBM[i][2]);
											fprintf(thread_contact_file, "u_n=%f\t%f\t%f\tu_t=%f\t%f\t%f\ttangential=%d\t%f\t%f\t%f\t%f\t%f\n", ujitemp_n[0], ujitemp_n[1], ujitemp_n[2], ujitemp_t[0], ujitemp_t[1], ujitemp_t[2], tangential_contact_index[j][n1], tangential_overlap[j][n1][0], tangential_overlap[j][n1][1], tangential_overlap[j][n1][2], temp, temp3);
											fprintf(thread_contact_file, "f_n=%f\t%f\t%f\tf_t=%f\t%f\t%f\tT=%f\t%f\t%f\tifslip=%f\n", force_n_DEM[0], force_n_DEM[1], force_n_DEM[2], force_t_DEM[0], force_t_DEM[1], force_t_DEM[2], torque_t_DEM[0], torque_t_DEM[1], torque_t_DEM[2], temp2);
											if (if_linear_DEM == 0)
												fprintf(thread_contact_file, "K_n2=%f\tK_t2=%f\teta_n2=%f\teta_t2=%f\n", K_n2, k_t2_temp, eta_n2_temp, eta_t2_temp);
										}
									}
#if (iflub_DEM_output == 1)
									if (shared_contact_file)
									{
										thread_file_has_written = 1;
										if (KeyProcess[k] == me)
										{
#pragma omp critical (contact_log)
											{
												fprintf(shared_contact_file, "%d\t%d\t%d\t%f\t", me, step, step_DEM, lc - 2 * R);
												fprintf(shared_contact_file, "%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t", j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], wc[j][0], wc[j][1], wc[j][2]);
												fprintf(shared_contact_file, "%f\t%f\t%f\t%f\t%f\t%f\t", F_IBM[j][0], F_IBM[j][1], F_IBM[j][2], T_IBM[j][0], T_IBM[j][1], T_IBM[j][2]);
												fprintf(shared_contact_file, "%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t", i + 1, xc[i][0], xc[i][1], xc[i][2], uc[i][0], uc[i][1], uc[i][2], wc[i][0], wc[i][1], wc[i][2]);
												fprintf(shared_contact_file, "%f\t%f\t%f\t%f\t%f\t%f\t", F_IBM[i][0], F_IBM[i][1], F_IBM[i][2], T_IBM[i][0], T_IBM[i][1], T_IBM[i][2]);
												fprintf(shared_contact_file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\t%f\t%f\t%f\t%f\t%f\t", ujitemp_n[0], ujitemp_n[1], ujitemp_n[2], ujitemp_t[0], ujitemp_t[1], ujitemp_t[2], tangential_contact_index[j][n1], tangential_overlap[j][n1][0], tangential_overlap[j][n1][1], tangential_overlap[j][n1][2], temp, temp3);
												fprintf(shared_contact_file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t", force_n_DEM[0], force_n_DEM[1], force_n_DEM[2], force_t_DEM[0], force_t_DEM[1], force_t_DEM[2], torque_t_DEM[0], torque_t_DEM[1], torque_t_DEM[2], temp2);
												fprintf(shared_contact_file, "%f\t%f\t%f\n", force_lub[0], force_lub[1], force_lub[2]);
												//fflush(shared_contact_file);  // 可选：实时刷新，保证崩溃时数据不丢
											}
										}
									}
#endif
								}
							}
						}
					}//end particle pair
					if (periodic_x == 0)
					{
						if (iflub == 1)
						{
							if ((xc[j][0] < (R + safe))|| (xc[j][0] > (NxT +1 - R - safe)))
							{
								if (xc[j][0] < (R + safe))
								{
									epsilon_temp = (xc[j][0] - R) / R;
								}
								else if (xc[j][0] > (NxT + 1 - R - safe))
								{
									epsilon_temp = (NxT + 1 - xc[j][0] - R) / R;
								}
								if (epsilon_temp < epsilon_act)
								{
									if (epsilon_temp >= epsilon_max)
									{
										temp = (1. / epsilon_temp - 1. / 5. * log(epsilon_temp) - 1. / 21. * epsilon_temp * log(epsilon_temp) + 0.673);
									}
									else if (epsilon_temp < epsilon_max)
									{
										temp = (1. / epsilon_max - 1. / 5. * log(epsilon_max) - 1. / 21. * epsilon_max * log(epsilon_max) + 0.673);
										if (epsilon_temp < -epsilon_dis)
										{
											temp = (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
										}
									}
									temp2 = temp - (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
									temp3 = temp2 * uc[j][0] * -6 * PI * rhof * mv * D / 2;
									#pragma omp atomic
									Fpp_lub[k][0] += temp3;
									if (iflub_DEM_test && thread_contact_file)
									{
										thread_file_has_written = 1;
										// 输出标识：Wall_Lub
										fprintf(thread_contact_file, "Wall_Lub\tRank:%d\tstep=%d\tdir=X\twall=%s\n",
											me, step, (xc[j][0] < (R + safe)) ? "Left" : "Right");
										fprintf(thread_contact_file, "Par_%d\txc=%f\t%f\t%f\tuc=%f\t%f\t%f\twc=%f\t%f\t%f\n",
											j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], wc[j][0], wc[j][1], wc[j][2]);
										fprintf(thread_contact_file, "epsilon_temp=%f\teps_act=%f\teps_max=%f\teps_dis=%f\n",
											epsilon_temp, epsilon_act, epsilon_max, epsilon_dis);
										fprintf(thread_contact_file, "temp=%f\ttemp2=%f\ttemp3=%f\tF_lub=%f\t%f\t%f\n",
											temp, temp2, temp3, Fpp_lub[k][0], Fpp_lub[k][1], Fpp_lub[k][2]);
									}
								}
							}
						}//end lub
						if ((xc[j][0] < R) || (xc[j][0] > (NxT + 1 - R)))
						{
							if (xc[j][0] < R)
							{
								temp = (xc[j][0] - R) * dx;
							}
							else if (xc[j][0] > (NxT + 1 - R))
							{
								temp = (xc[j][0] + R - NxT - 1) * dx;
							}
							if (if_linear_DEM == 1)
							{
								force_n_DEM[0] = -K_n * temp - uc[j][0] * eta_n;
							}
							else
							{
								double sign = (temp > 0) ? 1.0 : -1.0;
								eta_n2_temp = eta_n2 * pow(fabs(temp), 0.25);
								force_n_DEM[0] = -K_n2 * pow(fabs(temp), 1.5) * sign - uc[j][0] * eta_n2_temp;
							}
							#pragma omp atomic
							Fpp_n_DEM[k][0] += force_n_DEM[0];
							if (iflub_DEM_test && thread_contact_file)
							{
								thread_file_has_written = 1;
								// 输出标识：Wall_DEM
								fprintf(thread_contact_file, "Wall_DEM\tRank:%d\tstep=%d\tdir=X\twall=%s\n",
									me, step, (xc[j][0] < R) ? "Left" : "Right");
								fprintf(thread_contact_file, "Par_%d\txc=%f\t%f\t%f\tuc=%f\t%f\t%f\n",
									j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2]);
								fprintf(thread_contact_file, "penetration=%f\tF_n_DEM=%f\t%f\t%f\n",
									temp, force_n_DEM[0], force_n_DEM[1], force_n_DEM[2]);
							}
						}
					}
					if (periodic_y == 0)
					{
						if (iflub == 1)
						{
							if ((xc[j][1] < (R + safe)) || (xc[j][1] > (NyT + 1 - R - safe)))
							{
								if (xc[j][1] < (R + safe))
								{
									epsilon_temp = (xc[j][1] - R) / R;
								}
								else if (xc[j][1] > (NyT + 1 - R - safe))
								{
									epsilon_temp = (NyT + 1 - xc[j][1] - R) / R;
								}
								if (epsilon_temp < epsilon_act)
								{
									if (epsilon_temp >= epsilon_max)
									{
										temp = (1. / epsilon_temp - 1. / 5. * log(epsilon_temp) - 1. / 21. * epsilon_temp * log(epsilon_temp) + 0.673);
									}
									else if (epsilon_temp < epsilon_max)
									{
										temp = (1. / epsilon_max - 1. / 5. * log(epsilon_max) - 1. / 21. * epsilon_max * log(epsilon_max) + 0.673);
										if (epsilon_temp < -epsilon_dis)
										{
											temp = (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
										}
									}
									temp2 = temp - (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
									temp3 = temp2 * uc[j][1] * -6 * PI * rhof * mv * D / 2;
									#pragma omp atomic
									Fpp_lub[k][1] += temp3;
									if (iflub_DEM_test && thread_contact_file)
									{
										thread_file_has_written = 1;
										fprintf(thread_contact_file, "Wall_Lub\tRank:%d\tstep=%d\tdir=Y\twall=%s\n",
											me, step, (xc[j][1] < (R + safe)) ? "Bottom" : "Top");
										fprintf(thread_contact_file, "Par_%d\txc=%f\t%f\t%f\tuc=%f\t%f\t%f\twc=%f\t%f\t%f\n",
											j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], wc[j][0], wc[j][1], wc[j][2]);
										fprintf(thread_contact_file, "epsilon_temp=%f\teps_act=%f\teps_max=%f\teps_dis=%f\n",
											epsilon_temp, epsilon_act, epsilon_max, epsilon_dis);
										fprintf(thread_contact_file, "temp=%f\ttemp2=%f\ttemp3=%f\tF_lub=%f\t%f\t%f\n",
											temp, temp2, temp3, Fpp_lub[k][0], Fpp_lub[k][1], Fpp_lub[k][2]);
									}
								}
							}
						}//end lub
						if ((xc[j][1] < R) || (xc[j][1] > (NyT + 1 - R)))
						{
							if (xc[j][1] < R)
							{
								temp = (xc[j][1] - R) * dx;
							}
							else if (xc[j][1] > (NyT + 1 - R))
							{
								temp = (xc[j][1] + R - NyT - 1) * dx;
							}
							if (if_linear_DEM == 1)
							{
								force_n_DEM[1] = -K_n * temp - uc[j][1] * eta_n;
							}
							else
							{
								double sign = (temp > 0) ? 1.0 : -1.0;
								eta_n2_temp = eta_n2 * pow(fabs(temp), 0.25);
								force_n_DEM[1] = -K_n2 * pow(fabs(temp), 1.5) * sign - uc[j][1] * eta_n2_temp;
							}
							#pragma omp atomic
							Fpp_n_DEM[k][1] += force_n_DEM[1];
							if (iflub_DEM_test && thread_contact_file)
							{
								thread_file_has_written = 1;
								fprintf(thread_contact_file, "Wall_DEM\tRank:%d\tstep=%d\tdir=Y\twall=%s\n",
									me, step, (xc[j][1] < R) ? "Bottom" : "Top");
								fprintf(thread_contact_file, "Par_%d\txc=%f\t%f\t%f\tuc=%f\t%f\t%f\n",
									j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2]);
								fprintf(thread_contact_file, "penetration=%f\tF_n_DEM=%f\t%f\t%f\n",
									temp, force_n_DEM[0], force_n_DEM[1], force_n_DEM[2]);
							}
						}
					}
					if (periodic_z == 0)
					{
						if (iflub == 1)
						{
							if ((xc[j][2] < (R + safe)) || (xc[j][2] > (NzT + 1 - R - safe)))
							{
								if (xc[j][2] < (R + safe))
								{
									epsilon_temp = (xc[j][2] - R) / R;
								}
								else if (xc[j][2] > (NzT + 1 - R - safe))
								{
									epsilon_temp = (NzT + 1 - xc[j][2] - R) / R;
								}
								if (epsilon_temp < epsilon_act)
								{
									if (epsilon_temp >= epsilon_max)
									{
										temp = (1. / epsilon_temp - 1. / 5. * log(epsilon_temp) - 1. / 21. * epsilon_temp * log(epsilon_temp) + 0.673);
									}
									else if (epsilon_temp < epsilon_max)
									{
										temp = (1. / epsilon_max - 1. / 5. * log(epsilon_max) - 1. / 21. * epsilon_max * log(epsilon_max) + 0.673);
										if (epsilon_temp < -epsilon_dis)
										{
											temp = (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
										}
									}
									temp2 = temp - (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
									temp3 = temp2 * uc[j][2] * -6 * PI * rhof * mv * D / 2;
									#pragma omp atomic
									Fpp_lub[k][2] += temp3;
									if (iflub_DEM_test && thread_contact_file)
									{
										thread_file_has_written = 1;
										fprintf(thread_contact_file, "Wall_Lub\tRank:%d\tstep=%d\tdir=Z\twall=%s\n",
											me, step, (xc[j][2] < (R + safe)) ? "Back" : "Front");
										fprintf(thread_contact_file, "Par_%d\txc=%f\t%f\t%f\tuc=%f\t%f\t%f\twc=%f\t%f\t%f\n",
											j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], wc[j][0], wc[j][1], wc[j][2]);
										fprintf(thread_contact_file, "epsilon_temp=%f\teps_act=%f\teps_max=%f\teps_dis=%f\n",
											epsilon_temp, epsilon_act, epsilon_max, epsilon_dis);
										fprintf(thread_contact_file, "temp=%f\ttemp2=%f\ttemp3=%f\tF_lub=%f\t%f\t%f\n",
											temp, temp2, temp3, Fpp_lub[k][0], Fpp_lub[k][1], Fpp_lub[k][2]);
									}
								}
							}
						}//end lub
						if ((xc[j][2] < R) || (xc[j][2] > (NzT + 1 - R)))
						{
							if (xc[j][2] < R)
							{
								temp = (xc[j][2] - R) * dx;
							}
							else if (xc[j][2] > (NzT + 1 - R))
							{
								temp = (xc[j][2] + R - NzT - 1) * dx;
							}
							if (if_linear_DEM == 1)
							{
								force_n_DEM[2] = -K_n * temp - uc[j][2] * eta_n;
							}
							else
							{
								double sign = (temp > 0) ? 1.0 : -1.0;
								eta_n2_temp = eta_n2 * pow(fabs(temp), 0.25);
								force_n_DEM[2] = -K_n2 * pow(fabs(temp), 1.5) * sign - uc[j][2] * eta_n2_temp;
							}
							#pragma omp atomic
							Fpp_n_DEM[k][2] += force_n_DEM[2];
							if (iflub_DEM_test && thread_contact_file)
							{
								thread_file_has_written = 1;
								fprintf(thread_contact_file, "Wall_DEM\tRank:%d\tstep=%d\tdir=Z\twall=%s\n",
									me, step, (xc[j][2] < R) ? "Back" : "Front");
								fprintf(thread_contact_file, "Par_%d\txc=%f\t%f\t%f\tuc=%f\t%f\t%f\n",
									j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2]);
								fprintf(thread_contact_file, "penetration=%f\tF_n_DEM=%f\t%f\t%f\n",
									temp, force_n_DEM[0], force_n_DEM[1], force_n_DEM[2]);
							}
						}
					}
				}
			}
		}
	#pragma omp barrier

	#pragma omp for schedule(static)private(k,i)
		for (k = 0; k < np_local; k++)
		{
			if (num_p[k] >= 0)
			{
				for (i = 0; i < 3; i++)
				{
					F_lub[k][i] = Fpp_lub[k][i];
					F_n_DEM[k][i] = Fpp_n_DEM[k][i];
					F_t_DEM[k][i] = Fpp_t_DEM[k][i];
					T_DEM[k][i] = Tpp_t_DEM[k][i];
				}
			}
		}
	#pragma omp barrier
		if (iflub_DEM_test && thread_contact_file) 
		{
			fclose(thread_contact_file);

			// 删除空文件
			//if (!thread_file_has_written) {
			//	if (remove(thread_filename) == 0) {
			//		// 可选：输出删除信息
			//		// printf("Rank %d Thread %d: Deleted empty file %s\n", me, thread_id, thread_filename);
			//	}
			//	else {
			//		fprintf(stderr, "Rank %d Thread %d: Failed to delete %s\n", me, thread_id, thread_filename);
			//	}
			//}
		}
	}//end openmp parallel

	end_dem_force_in_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_Pcolli);
	#endif
		
	MPI_Barrier(MPI_COMM_WORLD);
	/*
	if (iflub_DEM_test)
	{
		if (step % 1000 == 999)
		{
			if (contact_file)
			{
				fflush(contact_file);

				fclose(contact_file);
				contact_file = NULL;

				if (file_has_written == 0)
				{

					// 删除空文件 
					if (remove(current_filename) == 0)
					{
						printf("Rank %d: Deleted empty file %s\n", me, current_filename);
					}
					else {
						printf("Rank %d: Delete failed\n", me);
					}
				}
			}
		}
	}
	*/
}

void lub_and_DEM_force_particle_poly(int me)
{
	int x, y, x1, y1, z1, z2, i, j, k, ii, jj, kk, k1, n1, n2, n3;
	int me_y, me_x;
	double lxc, lyc, lzc, lzc1, lc;
	int rcj, rci;
	double rho_pj, rho_pi, rji_ratio, rij_ratio;
	double Fpp_lub[np_local_poly][3], Fpp_n_DEM[np_local_poly][3], Fpp_t_DEM[np_local_poly][3], Tpp_t_DEM[np_local_poly][3];
	double tangential_overlap_temp[np_local_poly][max_contact_num][3], contact_uji_t[max_contact_num][3], overlap_temp[3];
	int	   tangential_contact_index_temp[np_local_poly][max_contact_num], contact_uji_t_index[max_contact_num];
	double force_lubj[3], force_lubi[3], force_n_DEM[3], force_t_DEM[3], torque_t_DEMj[3], torque_t_DEMi[3];
	double k_t2_temp, eta_n2_temp, eta_t2_temp;
	double k_n_poly_temp, k_t_poly_temp, eta_n_poly_temp, eta_t_poly_temp;
	double temp, temp2, temp3, epsilon_tempj, epsilon_tempi;
	double ujitemp_n[3], ujitemp_t[3];
	double temp4[4];
	int contact_num, last_num, lastflag, flag, jitemp, ijtemp;
	int contact_flag[max_contact_num];
	//static FILE* contact_file = NULL;
	//char filename[100];
	//static char current_filename[100] = "";
	//int write_flag;
	//static int file_has_written = 0;

	/*
	if (iflub_DEM_test)
	{
		sprintf(filename, "step_%d000-%d000_debug_poly_rank_%d.log", int(step / 1000), int(step / 1000) + 1, me);
		if ((step % 1000) == 0)
		{
			strcpy(current_filename, filename);
			write_flag = 0;
			file_has_written = 0;
		}

		if (contact_file == NULL) {
			contact_file = fopen(filename, "a");  // "a"模式追加写入
			if (contact_file == NULL) {
				fprintf(stderr, "Rank %d: Failed to open %s\n", me, filename);
			}
		}
	}
	*/
	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;

    #pragma omp parallel 
	{
		double force_lubj[3], force_lubi[3], force_n_DEM[3], force_t_DEM[3], torque_t_DEMj[3], torque_t_DEMi[3];
		double k_t2_temp, eta_n2_temp, eta_t2_temp;
		double k_n_poly_temp, k_t_poly_temp, eta_n_poly_temp, eta_t_poly_temp;
		double temp, temp2, temp3, epsilon_tempj, epsilon_tempi;
		double ujitemp_n[3], ujitemp_t[3];
		double temp4[4];
		int contact_num, last_num, lastflag, flag, jitemp, ijtemp;
		int contact_flag[max_contact_num], contact_uji_t_index[max_contact_num];
		double contact_uji_t[max_contact_num][3];
		double rho_pj, rho_pi, rji_ratio, rij_ratio;

		FILE* thread_contact_file = NULL;
		char thread_filename[100];
		int thread_file_has_written = 0;

		int thread_id = omp_get_thread_num();

		if (iflub_DEM_test) {
			// 每个线程创建自己的文件
			sprintf(thread_filename, "step_%d000-%d000_debug_poly_rank_%d_thread_%d.log",
				int(step / 1000), int(step / 1000) + 1, me, thread_id);

			thread_contact_file = fopen(thread_filename, "a");
			thread_file_has_written = 0;
		}

	#pragma omp for schedule(static) private(k,i,kk)
		for (k = 0; k < np_local_poly; k++)
		{
			for (i = 0; i < 3; i++)
			{
				Fpp_lub[k][i] = 0.0;
				Fpp_n_DEM[k][i] = 0.0;
				Fpp_t_DEM[k][i] = 0.0;
				Tpp_t_DEM[k][i] = 0.0;
				F_lub_poly[k][i] = 0.0;
				F_n_DEM_poly[k][i] = 0.0;
				F_t_DEM_poly[k][i] = 0.0;
				T_DEM_poly[k][i] = 0.0;
			}

			for (kk = 0; kk < max_contact_num; kk++)
			{
				tangential_contact_index_temp[k][kk] = -1; //contact particle index
				tangential_overlap_temp[k][kk][0] = 0.0;	//tangential overlap of dimensions
				tangential_overlap_temp[k][kk][1] = 0.0;
				tangential_overlap_temp[k][kk][2] = 0.0;
			}

		}
	#pragma omp barrier

	//calculate tangential overlap using tangential_overlap[np][max_contact_num][4]
	#pragma omp for schedule(dynamic, 10) private(k, j, kk, i, n1, n2, ii, \
		rcj, rci, lxc, lyc, lzc1, lzc, lc)
		for (k = 0; k < np_local_poly; k++)
		{
			if (num_p_poly[k] >= 0)
			{
				j = num_p_poly[k];
				rcj = R_poly[rc_poly[j]];
				{
					last_num = 0;
					for (n2 = 0; n2 < max_contact_num; n2++)
					{
						if (tangential_contact_index[j][n2] >= 0)
						{
							tangential_contact_index_temp[k][last_num] = tangential_contact_index[j][n2];
							tangential_overlap_temp[k][last_num][0] = tangential_overlap[j][n2][0];
							tangential_overlap_temp[k][last_num][1] = tangential_overlap[j][n2][1];
							tangential_overlap_temp[k][last_num][2] = tangential_overlap[j][n2][2];
							last_num++;
						}
					}
					for (ii = 0; ii < max_contact_num; ii++)
					{
						contact_flag[ii] = 0;
						//contact_uji_t_index[ii] = -1;//don't need to initialize,it works by contact_flag[ii]
						contact_uji_t[ii][0] = 0.0;
						contact_uji_t[ii][1] = 0.0;
						contact_uji_t[ii][2] = 0.0;
					}
					jitemp = 0;
					contact_num = 0;
					for (kk = 0; kk < np_local_poly; kk++)
					{
						if (num_p_poly[kk] >= 0)
						{
							i = num_p_poly[kk];
							rci = R_poly[rc_poly[i]];
							if (i != j)
							{
								lxc = (xc[i][0] - xc[j][0]);
								lyc = (xc[i][1] - xc[j][1]);
								lzc1 = (xc[i][2] - xc[j][2]);
								lzc = lzc1;
								if (periodic_z == 1)
								{
									if ((lzc1 > 0) && (fabs(lzc1) > (NzT / 2)))
										lzc = lzc1 - NzT;
									else if ((lzc1 < 0) && (fabs(lzc1) > (NzT / 2)))
										lzc = lzc1 + NzT;
								}
								lc = sqrt(lxc * lxc + lyc * lyc + lzc * lzc);

								if (lc < (rci + rcj))
								{
									/*
									if (n1 >= max_contact_num) {
										fprintf(stderr, "Rank %d: contact_num overflow!\n", me);
										break;
									}
									*/
									contact_flag[jitemp] = 1;
									temp = ((uc[j][0] - uc[i][0]) * lxc / lc + (uc[j][1] - uc[i][1]) * lyc / lc + (uc[j][2] - uc[i][2]) * lzc / lc);// uji dot normal
									ujitemp_n[0] = temp * lxc / lc;
									ujitemp_n[1] = temp * lyc / lc;
									ujitemp_n[2] = temp * lzc / lc;//uji_normal
									contact_uji_t_index[jitemp] = i;
									contact_uji_t[jitemp][0] = uc[j][0] - uc[i][0] - ujitemp_n[0] + ((wc[j][1] * rcj + wc[i][1] * rci) * lzc / lc - (wc[j][2] * rcj + wc[i][2] * rci) * lyc / lc) * dx_poly;
									contact_uji_t[jitemp][1] = uc[j][1] - uc[i][1] - ujitemp_n[1] + ((wc[j][2] * rcj + wc[i][2] * rci) * lxc / lc - (wc[j][0] * rcj + wc[i][0] * rci) * lzc / lc) * dx_poly;
									contact_uji_t[jitemp][2] = uc[j][2] - uc[i][2] - ujitemp_n[2] + ((wc[j][0] * rcj + wc[i][0] * rci) * lyc / lc - (wc[j][1] * rcj + wc[i][1] * rci) * lxc / lc) * dx_poly;
									jitemp++;
									//printf("paticle_num\t%d\t%d\t%f\n", j + 1, i + 1, lc);
								}
							}
						}
					}
					contact_num = jitemp;
					if (last_num > 0)
					{
						//fprintf(contact_file, "Rank:%d\tstep=%d\tstep_DEM=%d\tparticle_num=%d\tlast_num=%d\n", me, step, step_DEM, j+1,last_num);

						for (n1 = 0; n1 < last_num; n1++)
						{
							if (tangential_contact_index_temp[k][n1] >= 0)
							{
								lastflag = 1;
								for (n2 = 0; n2 < contact_num; n2++)
								{
									if (tangential_contact_index_temp[k][n1] == contact_uji_t_index[n2])//update tangential displacement 
									{
										lastflag = 0;
										contact_flag[n2] = 0;
										tangential_overlap_temp[k][n1][0] += contact_uji_t[n2][0] * dt_DEM;
										tangential_overlap_temp[k][n1][1] += contact_uji_t[n2][1] * dt_DEM;
										tangential_overlap_temp[k][n1][2] += contact_uji_t[n2][2] * dt_DEM;
										//fprintf(contact_file, "now_overlap=%f\t%f\t%f\n", tangential_overlap_temp[k][n1][0], tangential_overlap_temp[k][n1][1], tangential_overlap_temp[k][n1][2]);
										break;
									}
								}
								if (lastflag == 1)//pair contact is not exit, delete the contact
								{
									tangential_contact_index_temp[k][n1] = -1;
									tangential_overlap_temp[k][n1][0] = 0.0;
									tangential_overlap_temp[k][n1][1] = 0.0;
									tangential_overlap_temp[k][n1][2] = 0.0;
								}
							}
						}
					}
					if (contact_num > 0)
					{
						/*
						if (contact_num)
						{
							fprintf(contact_file, "time=%d particle_num:%d\tlast_num = %d\n", step, j + 1, contact_num);
							write_flag = 1;
							file_has_written = 1;
						}
						*/
						for (n1 = 0; n1 < contact_num; n1++)
						{
							if (contact_flag[n1] == 1)//new contact pair
							{
								for (n2 = 0; n2 < max_contact_num; n2++)
								{
									if (tangential_contact_index_temp[k][n2] < 0)
									{
										contact_flag[n1] = 0;
										tangential_contact_index_temp[k][n2] = contact_uji_t_index[n1];
										tangential_overlap_temp[k][n2][0] = contact_uji_t[n1][0] * dt_DEM;
										tangential_overlap_temp[k][n2][1] = contact_uji_t[n1][1] * dt_DEM;
										tangential_overlap_temp[k][n2][2] = contact_uji_t[n1][2] * dt_DEM;
										//printf("new contact partile num %d\t%d\t%f\t%f\t%f\n", j + 1, int(contact_uji_t[n1][0]), tangential_overlap_temp[k][n2][1], tangential_overlap_temp[k][n2][2], tangential_overlap_temp[k][n2][3]);
										//printf("new contact partile num %d\t%f\t%f\t%f\n", j + 1, tangential_overlap_temp[k][n2][1], tangential_overlap_temp[k][n2][2], tangential_overlap_temp[k][n2][3]);
										break;//Find the first available assignment and exit the loop
									}
									/*
									if (contact_flag[n1] > 0)
									{
										fprintf(stderr, "Rank %d: contact_num overflow2!\n", me);
										break;
									}
									*/
								}
							}
						}
					}
				}
			}
		}
	#pragma omp barrier

	 #pragma omp for schedule(static) private(k, j, n1)	
		for (k = 0; k < np_local_poly; k++)
		{
			if (num_p_poly[k] >= 0)
			{
				j = num_p_poly[k];
				for (n1 = 0; n1 < max_contact_num; n1++)
				{
					/*
					if (iflub_DEM_test)
					{
						if (tangential_contact_index[j][n1] >= 0)
						{
							fprintf(contact_file, "Rank_id = %d\tstep=%d\tparticle_num=%d\tlast time contact par_num=%d,tangential_delta=%f\t%f\t%f\n", me, step, j + 1, tangential_contact_index[j][n1] + 1, tangential_overlap[j][n1][0], tangential_overlap[j][n1][1], tangential_overlap[j][n1][2]);
							file_has_written = 1;
						}
						if (tangential_contact_index_temp[k][n1] >= 0)
						{
							fprintf(contact_file, "Rank_id = %d\tstep=%d\tparticle_num=%d\tnow time contact par_num=%d,tangential_delta=%f\t%f\t%f\n", me, step, j + 1, tangential_contact_index_temp[k][n1] + 1, tangential_overlap_temp[k][n1][0], tangential_overlap_temp[k][n1][1], tangential_overlap_temp[k][n1][2]);
							file_has_written = 1;
						}
					}
					*/
					tangential_contact_index[j][n1] = tangential_contact_index_temp[k][n1];
					tangential_overlap[j][n1][0] = tangential_overlap_temp[k][n1][0];
					tangential_overlap[j][n1][1] = tangential_overlap_temp[k][n1][1];
					tangential_overlap[j][n1][2] = tangential_overlap_temp[k][n1][2];
				}
			}
		}
	#pragma omp barrier

	//lubrication force ,see Phys. Fluids 33, 083112 (2021); doi: 10.1063/5.0060222  （33）
	#pragma omp for schedule(dynamic,10) private(k, j, kk, i, n1, n2, ii, \
		rcj, rci, lxc, lyc, lzc1, lzc, lc)
		for (k = 0; k < np_local_poly; k++)
		{
			if (num_p_poly[k] >= 0)
			{
				j = num_p_poly[k];
				rcj = R_poly[rc_poly[j]];
				rho_pj = rhoc_poly[j];
				{
					for (kk = k + 1; kk < np_local_poly; kk++)
					{
						if (num_p_poly[kk] >= 0)
						{
							i = num_p_poly[kk];
							rci = R_poly[rc_poly[i]];
							rho_pi = rhoc_poly[i];
							if (i != j)
							{
								lxc = (xc[i][0] - xc[j][0]);
								lyc = (xc[i][1] - xc[j][1]);
								lzc1 = (xc[i][2] - xc[j][2]);
								lzc = lzc1;
								if (periodic_z == 1)
								{
									if ((lzc1 > 0) && (fabs(lzc1) > (NzT / 2)))
										lzc = lzc1 - NzT;
									else if ((lzc1 < 0) && (fabs(lzc1) > (NzT / 2)))
										lzc = lzc1 + NzT;
								}
								for (ii = 0; ii < 3; ii++)
								{
									force_lubj[ii] = 0.0;
									force_lubi[ii] = 0.0;
									force_n_DEM[ii] = 0.0;
									force_t_DEM[ii] = 0.0;
									torque_t_DEMj[ii] = 0.0;
									torque_t_DEMi[ii] = 0.0;
								}
								lc = sqrt(lxc * lxc + lyc * lyc + lzc * lzc);

								if (iflub > 0)
								{
									if ((lc - rci - rcj) < 1.0)
									{
										rji_ratio = rcj / rci;
										rij_ratio = rci / rcj;
										epsilon_tempj = (lc - rci - rcj) / rcj;
										epsilon_tempi = (lc - rci - rcj) / rci;
										if (epsilon_tempj >= epsilon_max)
										{
											temp = (1.0 / (1 + rji_ratio * rji_ratio) / epsilon_tempj - (1 + 7 * rji_ratio + rji_ratio * rji_ratio) / (5 * pow((1 + rji_ratio), 3)) * log(epsilon_tempj) - \
												(1 + 18 * rji_ratio - 29 * rji_ratio * rji_ratio + 18 * pow(rji_ratio, 3) + pow(rji_ratio, 4)) / (21 * pow((1 + rji_ratio), 4)) * epsilon_tempj * log(epsilon_tempj) + 0.673);
											//printf("lub temp1=%f\t", temp);
										}
										else if (epsilon_tempj < epsilon_max)
										{
											temp = (1.0 / (1 + rji_ratio * rji_ratio) / epsilon_max - (1 + 7 * rji_ratio + rji_ratio * rji_ratio) / (5 * pow((1 + rji_ratio), 3)) * log(epsilon_max) - \
												(1 + 18 * rji_ratio - 29 * rji_ratio * rji_ratio + 18 * pow(rji_ratio, 3) + pow(rji_ratio, 4)) / (21 * pow((1 + rji_ratio), 4)) * epsilon_max * log(epsilon_max) + 0.673);										//printf("lub temp2=%f\t", temp);
											if (epsilon_tempj < -epsilon_dis)
											{
												temp = (1.0 / (1 + rji_ratio * rji_ratio) / (1.0 / rcj) - (1 + 7 * rji_ratio + rji_ratio * rji_ratio) / (5 * pow((1 + rji_ratio), 3)) * log((1.0 / rcj)) - \
													(1 + 18 * rji_ratio - 29 * rji_ratio * rji_ratio + 18 * pow(rji_ratio, 3) + pow(rji_ratio, 4)) / (21 * pow((1 + rji_ratio), 4)) * (1.0 / rcj) * log((1.0 / rcj)) + 0.673);										//printf("lub temp2=%f\t", temp);
												//printf("lub temp3=%f\t", temp);
											}
										}
										temp2 = temp - (1.0 / (1 + rji_ratio * rji_ratio) / (1.0 / rcj) - (1 + 7 * rji_ratio + rji_ratio * rji_ratio) / (5 * pow((1 + rji_ratio), 3)) * log((1.0 / rcj)) - \
											(1 + 18 * rji_ratio - 29 * rji_ratio * rji_ratio + 18 * pow(rji_ratio, 3) + pow(rji_ratio, 4)) / (21 * pow((1 + rji_ratio), 4)) * (1.0 / rcj) * log((1.0 / rcj)) + 0.673);
										temp3 = temp2 * ((uc[j][0] - uc[i][0]) * lxc / lc + (uc[j][1] - uc[i][1]) * lyc / lc + (uc[j][2] - uc[i][2]) * lzc / lc) * -6 * PI * rhof * mv * rcj * dx_poly;
										force_lubj[0] = temp3 * lxc / lc;
										force_lubj[1] = temp3 * lyc / lc;
										force_lubj[2] = temp3 * lzc / lc;

										/*
										if (iflub_DEM_test)
										{
											file_has_written = 1;
											fprintf(contact_file, "lub\tRank:%d\tstep=%d\tstep_DEM=%d\tdelta=%f\n", me, step, step_DEM, (lc - rcj - rci));
											fprintf(contact_file, "%d\tx=%f\t%f\t%f\tu=%f\t%f\t%f\n", j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2]);
											fprintf(contact_file, "%d\tx=%f\t%f\t%f\tu=%f\t%f\t%f\n", i + 1, xc[i][0], xc[i][1], xc[i][2], uc[i][0], uc[i][1], uc[i][2]);
											fprintf(contact_file, "epsilon_tempj=%f\ttemp=%f\ttemp2=%f\ttemp3=%f\n", epsilon_tempj, temp, temp2, temp3);
											fprintf(contact_file, "force_lub=%f\t%f\t%f\n", force_lubj[0], force_lubj[1], force_lubj[2]);
										}
										*/
										//if (iflub_DEM_test && thread_contact_file) {
										//	fprintf(thread_contact_file, "lub\tRank:%d\tstep=%d\tstep_DEM=%d\tdelta=%f\n", me, step, step_DEM, (lc - rcj - rci));
										//	fprintf(thread_contact_file, "%d\tx=%f\t%f\t%f\tu=%f\t%f\t%f\n", j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2]);
										//	fprintf(thread_contact_file, "%d\tx=%f\t%f\t%f\tu=%f\t%f\t%f\n", i + 1, xc[i][0], xc[i][1], xc[i][2], uc[i][0], uc[i][1], uc[i][2]);
										//	fprintf(thread_contact_file, "epsilon_tempj=%f\ttemp=%f\ttemp2=%f\ttemp3=%f\n", epsilon_tempj, temp, temp2, temp3);
										//	fprintf(thread_contact_file, "force_lub=%f\t%f\t%f\n", force_lubj[0], force_lubj[1], force_lubj[2]);
										//	thread_file_has_written = 1;
										//}

										if (epsilon_tempi >= epsilon_max)
										{
											temp = (1.0 / (1 + rij_ratio * rij_ratio) / epsilon_tempi - (1 + 7 * rij_ratio + rij_ratio * rij_ratio) / (5 * pow((1 + rij_ratio), 3)) * log(epsilon_tempi) - \
												(1 + 18 * rij_ratio - 29 * rij_ratio * rij_ratio + 18 * pow(rij_ratio, 3) + pow(rij_ratio, 4)) / (21 * pow((1 + rij_ratio), 4)) * epsilon_tempi * log(epsilon_tempi) + 0.673);
											//printf("lub temp1=%f\t", temp);
										}
										else if (epsilon_tempi < epsilon_max)
										{
											temp = (1.0 / (1 + rij_ratio * rij_ratio) / epsilon_max - (1 + 7 * rij_ratio + rij_ratio * rij_ratio) / (5 * pow((1 + rij_ratio), 3)) * log(epsilon_max) - \
												(1 + 18 * rij_ratio - 29 * rij_ratio * rij_ratio + 18 * pow(rij_ratio, 3) + pow(rij_ratio, 4)) / (21 * pow((1 + rij_ratio), 4)) * epsilon_max * log(epsilon_max) + 0.673);										//printf("lub temp2=%f\t", temp);
											if (epsilon_tempi < -epsilon_dis)
											{
												temp = (1.0 / (1 + rij_ratio * rij_ratio) / (1.0 / rci) - (1 + 7 * rij_ratio + rij_ratio * rij_ratio) / (5 * pow((1 + rij_ratio), 3)) * log((1.0 / rci)) - \
													(1 + 18 * rij_ratio - 29 * rij_ratio * rij_ratio + 18 * pow(rij_ratio, 3) + pow(rij_ratio, 4)) / (21 * pow((1 + rij_ratio), 4)) * (1.0 / rci) * log((1.0 / rci)) + 0.673);										//printf("lub temp2=%f\t", temp);
												//printf("lub temp3=%f\t", temp);
											}
										}
										temp2 = temp - (1.0 / (1 + rij_ratio * rij_ratio) / (1.0 / rci) - (1 + 7 * rij_ratio + rij_ratio * rij_ratio) / (5 * pow((1 + rij_ratio), 3)) * log((1.0 / rci)) - \
											(1 + 18 * rij_ratio - 29 * rij_ratio * rij_ratio + 18 * pow(rij_ratio, 3) + pow(rij_ratio, 4)) / (21 * pow((1 + rij_ratio), 4)) * (1.0 / rci) * log((1.0 / rci)) + 0.673);
										temp3 = temp2 * ((uc[j][0] - uc[i][0]) * lxc / lc + (uc[j][1] - uc[i][1]) * lyc / lc + (uc[j][2] - uc[i][2]) * lzc / lc) * -6 * PI * rhof * mv * rci * dx_poly;
										force_lubi[0] = temp3 * lxc / lc;
										force_lubi[1] = temp3 * lyc / lc;
										force_lubi[2] = temp3 * lzc / lc;

										/*
										if (iflub_DEM_test)
										{
											file_has_written = 1;
											fprintf(contact_file, "epsilon_tempi=%f\ttemp=%f\ttemp2=%f\ttemp3=%f\n", epsilon_tempi, temp, temp2, temp3);
											fprintf(contact_file, "force_lub=%f\t%f\t%f\n", force_lubi[0], force_lubi[1], force_lubi[2]);
										}
										*/

										//if (iflub_DEM_test && thread_contact_file) {
										//	fprintf(thread_contact_file, "epsilon_tempi=%f\ttemp=%f\ttemp2=%f\ttemp3=%f\n", epsilon_tempi, temp, temp2, temp3);
										//	fprintf(thread_contact_file, "force_lub=%f\t%f\t%f\n", force_lubi[0], force_lubi[1], force_lubi[2]);
										//	thread_file_has_written = 1;
										//}

										for (ii = 0; ii < 3; ii++)
										{
											#pragma omp atomic
											Fpp_lub[k][ii] += force_lubj[ii];
											#pragma omp atomic
											Fpp_lub[kk][ii] -= force_lubi[ii];
										}
									}
								}

								if (lc < (rcj + rci))
								{
									temp = ((uc[j][0] - uc[i][0]) * lxc / lc + (uc[j][1] - uc[i][1]) * lyc / lc + (uc[j][2] - uc[i][2]) * lzc / lc);// uji dot normal
									ujitemp_n[0] = temp * lxc / lc;
									ujitemp_n[1] = temp * lyc / lc;
									ujitemp_n[2] = temp * lzc / lc;
									ujitemp_t[0] = uc[j][0] - uc[i][0] - ujitemp_n[0] + ((wc[j][1] * rcj + wc[i][1] * rci) * lzc / lc - (wc[j][2] * rcj + wc[i][2] * rci) * lyc / lc) * dx_poly;
									ujitemp_t[1] = uc[j][1] - uc[i][1] - ujitemp_n[1] + ((wc[j][2] * rcj + wc[i][2] * rci) * lxc / lc - (wc[j][0] * rcj + wc[i][0] * rci) * lzc / lc) * dx_poly;
									ujitemp_t[2] = uc[j][2] - uc[i][2] - ujitemp_n[2] + ((wc[j][0] * rcj + wc[i][0] * rci) * lyc / lc - (wc[j][1] * rcj + wc[i][1] * rci) * lxc / lc) * dx_poly;
									if (if_linear_DEM == 1)
									{
										force_n_DEM[0] = -K_n * (rcj + rci - lc) * dx_poly * lxc / lc - ujitemp_n[0] * eta_n;
										force_n_DEM[1] = -K_n * (rcj + rci - lc) * dx_poly * lyc / lc - ujitemp_n[1] * eta_n;
										force_n_DEM[2] = -K_n * (rcj + rci - lc) * dx_poly * lzc / lc - ujitemp_n[2] * eta_n;
									}
									else
									{
										k_n_poly_temp = (4.0 / 3.0 / (2 * (1 - pow(sigma_Poisson, 2)) / E_Young_poly) / sqrt((rci + rcj) / (rci * rcj * dx_poly)));
										eta_n_poly_temp = (alpha_DEM * sqrt((4.0 / 3.0 * PI * pow(rcj * rci, 3) * rho_pj * rho_pi) / (pow(rcj, 3) * rho_pj + pow(rci, 3) * rho_pi) * k_n_poly_temp) * pow(((rcj + rci - lc) * dx_poly), 0.25));
										force_n_DEM[0] = -k_n_poly_temp * pow(((rcj + rci - lc) * dx_poly), 1.5) * lxc / lc - ujitemp_n[0] * eta_n_poly_temp;
										force_n_DEM[1] = -k_n_poly_temp * pow(((rcj + rci - lc) * dx_poly), 1.5) * lyc / lc - ujitemp_n[1] * eta_n_poly_temp;
										force_n_DEM[2] = -k_n_poly_temp * pow(((rcj + rci - lc) * dx_poly), 1.5) * lzc / lc - ujitemp_n[2] * eta_n_poly_temp;
									}
									flag = 0;
									for (n1 = 0; n1 < max_contact_num; n1++)
									{
										if (tangential_contact_index[j][n1] == i)
										{
											flag += 1;
											temp = sqrt(tangential_overlap[j][n1][0] * tangential_overlap[j][n1][0] + tangential_overlap[j][n1][1] * tangential_overlap[j][n1][1] +
												tangential_overlap[j][n1][2] * tangential_overlap[j][n1][2]);
											//temp2 = sqrt(ujitemp_t[0] * ujitemp_t[0] + ujitemp_t[1] * ujitemp_t[1] + ujitemp_t[2] * ujitemp_t[2]);
											if (temp > 0.0)
											{
												temp3 = (tangential_overlap[j][n1][0] * ujitemp_t[0] + tangential_overlap[j][n1][1] * ujitemp_t[1] +
													tangential_overlap[j][n1][2] * ujitemp_t[2]) / temp;

												if (if_linear_DEM)
												{
													force_t_DEM[0] = -K_t * tangential_overlap[j][n1][0] - eta_t * ujitemp_t[0];
													force_t_DEM[1] = -K_t * tangential_overlap[j][n1][1] - eta_t * ujitemp_t[1];
													force_t_DEM[2] = -K_t * tangential_overlap[j][n1][2] - eta_t * ujitemp_t[2];
												}
												else
												{
													k_t_poly_temp = (8.0 / (2.0 * (2.0 - sigma_Poisson) / G_shear_poly) / sqrt((rcj + rci) / (rcj * rci * dx_poly)) * sqrt((rcj + rci - lc) * dx_poly));
													eta_t_poly_temp = (alpha_DEM * sqrt((4.0 / 3.0 * PI * pow(rcj * rci, 3) * rho_pj * rho_pi) / (pow(rcj, 3) * rho_pj + pow(rci, 3) * rho_pi) * k_t_poly_temp) * pow(temp, 0.25));
													force_t_DEM[0] = -k_t_poly_temp * tangential_overlap[j][n1][0] - eta_t_poly_temp * ujitemp_t[0];
													force_t_DEM[1] = -k_t_poly_temp * tangential_overlap[j][n1][1] - eta_t_poly_temp * ujitemp_t[1];
													force_t_DEM[2] = -k_t_poly_temp * tangential_overlap[j][n1][2] - eta_t_poly_temp * ujitemp_t[2];
												}
												for (n2 = 0; n2 < max_contact_num; n2++)
												{
													if (tangential_contact_index[i][n2] == j)
													{
														ijtemp = n2;
														flag += 2;
														break;
													}
												}
												temp2 = 0;
												if ((force_n_DEM[0] * force_n_DEM[0] + force_n_DEM[1] * force_n_DEM[1] + force_n_DEM[2] * force_n_DEM[2]) * miu_c_DEM * miu_c_DEM <
													(force_t_DEM[0] * force_t_DEM[0] + force_t_DEM[1] * force_t_DEM[1] + force_t_DEM[2] * force_t_DEM[2]))
												{
													temp2 = 1;
													temp4[0] = force_t_DEM[0];
													temp4[1] = force_t_DEM[1];
													temp4[2] = force_t_DEM[2];
													temp4[3] = sqrt(force_t_DEM[0] * force_t_DEM[0] + force_t_DEM[1] * force_t_DEM[1] + force_t_DEM[2] * force_t_DEM[2]);
													force_t_DEM[0] = sqrt(force_n_DEM[0] * force_n_DEM[0] + force_n_DEM[1] * force_n_DEM[1] + force_n_DEM[2] * force_n_DEM[2]) * miu_c_DEM * temp4[0] / temp4[3];
													force_t_DEM[1] = sqrt(force_n_DEM[0] * force_n_DEM[0] + force_n_DEM[1] * force_n_DEM[1] + force_n_DEM[2] * force_n_DEM[2]) * miu_c_DEM * temp4[1] / temp4[3];
													force_t_DEM[2] = sqrt(force_n_DEM[0] * force_n_DEM[0] + force_n_DEM[1] * force_n_DEM[1] + force_n_DEM[2] * force_n_DEM[2]) * miu_c_DEM * temp4[2] / temp4[3];
													if (if_linear_DEM)
													{
														//#pragma omp atomic
														tangential_overlap[j][n1][0] = -(force_t_DEM[0] + eta_t * ujitemp_t[0]) / K_t;
														//#pragma omp atomic
														tangential_overlap[j][n1][1] = -(force_t_DEM[1] + eta_t * ujitemp_t[1]) / K_t;
                                                        //#pragma omp atomic
														tangential_overlap[j][n1][2] = -(force_t_DEM[2] + eta_t * ujitemp_t[2]) / K_t;
                                                        //#pragma omp atomic
														tangential_overlap[i][ijtemp][0] = (force_t_DEM[0] + eta_t * ujitemp_t[0]) / K_t;
                                                        //#pragma omp atomic
														tangential_overlap[i][ijtemp][1] = (force_t_DEM[1] + eta_t * ujitemp_t[1]) / K_t;
                                                        //#pragma omp atomic
														tangential_overlap[i][ijtemp][2] = (force_t_DEM[2] + eta_t * ujitemp_t[2]) / K_t;
													}
													else
													{
														//#pragma omp atomic
														tangential_overlap[j][n1][0] = -(force_t_DEM[0] + eta_t_poly_temp * ujitemp_t[0]) / k_t_poly_temp;
														//#pragma omp atomic
														tangential_overlap[j][n1][1] = -(force_t_DEM[1] + eta_t_poly_temp * ujitemp_t[1]) / k_t_poly_temp;
														//#pragma omp atomic
														tangential_overlap[j][n1][2] = -(force_t_DEM[2] + eta_t_poly_temp * ujitemp_t[2]) / k_t_poly_temp;
														//#pragma omp atomic
														tangential_overlap[i][ijtemp][0] = (force_t_DEM[0] + eta_t_poly_temp * ujitemp_t[0]) / k_t_poly_temp;
                                                        //#pragma omp atomic
														tangential_overlap[i][ijtemp][1] = (force_t_DEM[1] + eta_t_poly_temp * ujitemp_t[1]) / k_t_poly_temp;
														//#pragma omp atomic
														tangential_overlap[i][ijtemp][2] = (force_t_DEM[2] + eta_t_poly_temp * ujitemp_t[2]) / k_t_poly_temp;
													}
												}
											}
											else
											{
												//if (iflub_DEM_test)
												//{
												//	fprintf(contact_file, "time=%d only normal contact no tangential overlap\n", step);
												//	file_has_written = 1;
												//}
												if (iflub_DEM_test)
												{
													fprintf(thread_contact_file, "time=%d only normal contact no tangential overlap\n", step);
													thread_file_has_written = 1;
												}
												else
													printf("time=%d only normal contact no tangential overlap\n", step);
											}
											break;
										}
									}
									if (flag == 0)
									{
										//if (iflub_DEM_test)
										//{
										//	if (contact_file)
										//	{
										//		fprintf(contact_file, "Rank:%d time=%d particle %d and particle %d j_th particle didn't find i_th particle at distance = %f\n", me, step, j + 1, i + 1, lc);
										//		write_flag = 1;
										//		file_has_written = 1;
										//	}
										//}
										if (iflub_DEM_test)
										{
											if (thread_contact_file)
											{
												fprintf(thread_contact_file, "Rank:%d time=%d particle %d and particle %d j_th particle didn't find i_th particle at distance = %f\n", me, step, j + 1, i + 1, lc);
												thread_file_has_written = 1;
											}
										}
										else
											printf("Rank:%d time=%d particle %d and particle %d j_th particle didn't find i_th particle at distance = %f\n", me, step, j + 1, i + 1, lc);
									}
									else if (flag == 1)
									{
										//if (iflub_DEM_test)
										//{
										//	if (contact_file)
										//		fprintf(contact_file, "Rank:%d time=%d particle %d and particle %d i_th particle didn't find j_th particle at distance = %f\n", me, step, j + 1, i + 1, lc);
										//	write_flag = 1;
										//	file_has_written = 1;
										//}
										if (iflub_DEM_test)
										{
											if (thread_contact_file)
												fprintf(thread_contact_file, "Rank:%d time=%d particle %d and particle %d i_th particle didn't find j_th particle at distance = %f\n", me, step, j + 1, i + 1, lc);
											thread_file_has_written = 1;
										}
										else
											printf("Rank:%d time=%d particle %d and particle %d i_th particle didn't find j_th particle at distance = %f\n", me, step, j + 1, i + 1, lc);
									}

									torque_t_DEMj[0] = (force_t_DEM[2] * lyc - force_t_DEM[1] * lzc) / lc * rcj * dx_poly;
									torque_t_DEMj[1] = (force_t_DEM[0] * lzc - force_t_DEM[2] * lxc) / lc * rcj * dx_poly;
									torque_t_DEMj[2] = (force_t_DEM[1] * lxc - force_t_DEM[0] * lyc) / lc * rcj * dx_poly;
									torque_t_DEMi[0] = (force_t_DEM[2] * lyc - force_t_DEM[1] * lzc) / lc * rci * dx_poly;
									torque_t_DEMi[1] = (force_t_DEM[0] * lzc - force_t_DEM[2] * lxc) / lc * rci * dx_poly;
									torque_t_DEMi[2] = (force_t_DEM[1] * lxc - force_t_DEM[0] * lyc) / lc * rci * dx_poly;
									for (ii = 0; ii < 3; ii++)
									{
										#pragma omp atomic
										Fpp_n_DEM[k][ii] += force_n_DEM[ii];
										#pragma omp atomic
										Fpp_n_DEM[kk][ii] -= force_n_DEM[ii];
										#pragma omp atomic
										Fpp_t_DEM[k][ii] += force_t_DEM[ii];
										#pragma omp atomic
										Fpp_t_DEM[kk][ii] -= force_t_DEM[ii];
										#pragma omp atomic
										Tpp_t_DEM[k][ii] += torque_t_DEMj[ii];
										#pragma omp atomic
										Tpp_t_DEM[kk][ii] += torque_t_DEMi[ii];//tangential torque of two particle by DEM should be the same direction
									}

double overlap_grid = rcj + rci - lc;
                                    if (overlap_grid >= 0.1)
                                    {
                                        printf("LARGE_OVERLAP  Rank:%d  step:%d  step_DEM:%d  overlap:%.6f  dist:%.6f  rcj:%d  rci:%d\n",
                                               me, step, step_DEM, overlap_grid, lc, rcj, rci);
                                        printf("  J:%d  x:(%.6f,%.6f,%.6f)  u:(%.6f,%.6f,%.6f)  w:(%.6f,%.6f,%.6f) f:(%.6f,%.6f,%.6f)\n",
                                               j+1, xc[j][0], xc[j][1], xc[j][2],
                                               uc[j][0], uc[j][1], uc[j][2],
                                               wc[j][0], wc[j][1], wc[j][2],
					       F_IBM[j][0],F_IBM[j][1],F_IBM[j][2]);
                                        printf("  I:%d  x:(%.6f,%.6f,%.6f)  u:(%.6f,%.6f,%.6f)  w:(%.6f,%.6f,%.6f) f:(%.6f,%.6f,%.6f)\n",
                                               i+1, xc[i][0], xc[i][1], xc[i][2],
                                               uc[i][0], uc[i][1], uc[i][2],
                                               wc[i][0], wc[i][1], wc[i][2],
					       F_IBM[i][0],F_IBM[i][1],F_IBM[i][2]);
                                        printf("  Fn:(%.6f,%.6f,%.6f)  Ft:(%.6f,%.6f,%.6f)\n",
                                               force_n_DEM[0], force_n_DEM[1], force_n_DEM[2],
                                               force_t_DEM[0], force_t_DEM[1], force_t_DEM[2]);
                                        printf("  Tj:(%.6f,%.6f,%.6f)  Ti:(%.6f,%.6f,%.6f)\n\n",
                                               torque_t_DEMj[0], torque_t_DEMj[1], torque_t_DEMj[2],
                                               torque_t_DEMi[0], torque_t_DEMi[1], torque_t_DEMi[2]);
                                    }

									/*
									if (iflub_DEM_test)
									{
										if (contact_file)
										{
											write_flag = 1;
											file_has_written = 1;
											fprintf(contact_file, "Dem\tRank:%d\tstep=%d\tstep_DEM=%d\tdelta=%f\n", me, step, step_DEM, lc - rcj - rci);
											fprintf(contact_file, "%d\tx=%f\t%f\t%f\tu=%f\t%f\t%f\tw=%f\t%f\t%f\n", j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], wc[j][0], wc[j][1], wc[j][2]);
											fprintf(contact_file, "ucc=%f\t%f\t%f\twcc=%f\t%f\t%f\n", ucc[j][0], ucc[j][1], ucc[j][2], wcc[j][0], wcc[j][1], wcc[j][2]);
											fprintf(contact_file, "F_IBM =%f\t%f\t%f\tT_IBM =%f\t%f\t%f\n", F_IBM[j][0], F_IBM[j][1], F_IBM[j][2], T_IBM[j][0], T_IBM[j][1], T_IBM[j][2]);
											fprintf(contact_file, "%d\tx=%f\t%f\t%f\tu=%f\t%f\t%f\tw=%f\t%f\t%f\n", i + 1, xc[i][0], xc[i][1], xc[i][2], uc[i][0], uc[i][1], uc[i][2], wc[i][0], wc[i][1], wc[i][2]);
											fprintf(contact_file, "F_IBM =%f\t%f\t%f\tT_IBM =%f\t%f\t%f\n", F_IBM[i][0], F_IBM[i][1], F_IBM[i][2], T_IBM[i][0], T_IBM[i][1], T_IBM[i][2]);
											fprintf(contact_file, "u_n=%f\t%f\t%f\tu_t=%f\t%f\t%f\ttangential=%d\t%f\t%f\t%f\t%f\t%f\n", ujitemp_n[0], ujitemp_n[1], ujitemp_n[2], ujitemp_t[0], ujitemp_t[1], ujitemp_t[2], tangential_contact_index[j][n1], tangential_overlap[j][n1][0], tangential_overlap[j][n1][1], tangential_overlap[j][n1][2], temp, temp3);
											fprintf(contact_file, "f_n=%f\t%f\t%f\tf_t=%f\t%f\t%f\tTj=%f\t%f\t%f\tTi=%f\t%f\t%f\tifslip=%f\n", force_n_DEM[0], force_n_DEM[1], force_n_DEM[2], force_t_DEM[0], force_t_DEM[1], force_t_DEM[2], torque_t_DEMj[0], torque_t_DEMj[1], torque_t_DEMj[2], torque_t_DEMi[0], torque_t_DEMi[1], torque_t_DEMi[2], temp2);
											if (if_linear_DEM == 0)
												fprintf(contact_file, "K_n=%f\tK_t=%f\teta_n=%f\teta_t=%f\n", k_n_poly_temp, k_t_poly_temp, eta_n_poly_temp, eta_t_poly_temp);
										}
									}
									*/
									if (iflub_DEM_test)
									{
										if (thread_contact_file)
										{
											thread_file_has_written = 1;
											fprintf(thread_contact_file, "Dem\tRank:%d\tstep=%d\tstep_DEM=%d\tdelta=%f\n", me, step, step_DEM, lc - rcj - rci);
											fprintf(thread_contact_file, "%d\tx=%f\t%f\t%f\tu=%f\t%f\t%f\tw=%f\t%f\t%f\n", j + 1, xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], wc[j][0], wc[j][1], wc[j][2]);
											fprintf(thread_contact_file, "ucc=%f\t%f\t%f\twcc=%f\t%f\t%f\n", ucc[j][0], ucc[j][1], ucc[j][2], wcc[j][0], wcc[j][1], wcc[j][2]);
											fprintf(thread_contact_file, "F_IBM =%f\t%f\t%f\tT_IBM =%f\t%f\t%f\n", F_IBM[j][0], F_IBM[j][1], F_IBM[j][2], T_IBM[j][0], T_IBM[j][1], T_IBM[j][2]);
											fprintf(thread_contact_file, "%d\tx=%f\t%f\t%f\tu=%f\t%f\t%f\tw=%f\t%f\t%f\n", i + 1, xc[i][0], xc[i][1], xc[i][2], uc[i][0], uc[i][1], uc[i][2], wc[i][0], wc[i][1], wc[i][2]);
											fprintf(thread_contact_file, "F_IBM =%f\t%f\t%f\tT_IBM =%f\t%f\t%f\n", F_IBM[i][0], F_IBM[i][1], F_IBM[i][2], T_IBM[i][0], T_IBM[i][1], T_IBM[i][2]);
											fprintf(thread_contact_file, "u_n=%f\t%f\t%f\tu_t=%f\t%f\t%f\ttangential=%d\t%f\t%f\t%f\t%f\t%f\n", ujitemp_n[0], ujitemp_n[1], ujitemp_n[2], ujitemp_t[0], ujitemp_t[1], ujitemp_t[2], tangential_contact_index[j][n1], tangential_overlap[j][n1][0], tangential_overlap[j][n1][1], tangential_overlap[j][n1][2], temp, temp3);
											fprintf(thread_contact_file, "f_n=%f\t%f\t%f\tf_t=%f\t%f\t%f\tTj=%f\t%f\t%f\tTi=%f\t%f\t%f\tifslip=%f\n", force_n_DEM[0], force_n_DEM[1], force_n_DEM[2], force_t_DEM[0], force_t_DEM[1], force_t_DEM[2], torque_t_DEMj[0], torque_t_DEMj[1], torque_t_DEMj[2], torque_t_DEMi[0], torque_t_DEMi[1], torque_t_DEMi[2], temp2);
											if (if_linear_DEM == 0)
												fprintf(thread_contact_file, "K_n=%f\tK_t=%f\teta_n=%f\teta_t=%f\n", k_n_poly_temp, k_t_poly_temp, eta_n_poly_temp, eta_t_poly_temp);
										}
									}
								}
							}
						}
					}//end particle pair
					if (periodic_x == 0)
					{
						if (iflub == 1)
						{
							if ((xc[j][0] < (rcj + safe)) || (xc[j][0] > (NxT + 1 - rcj - safe)))
							{
								if (xc[j][0] < (rcj + safe))
								{
									epsilon_tempj = (xc[j][0] - rcj) / rcj;
								}
								else if (xc[j][0] > (NxT + 1 - rcj - safe))
								{
									epsilon_tempj = (NxT + 1 - xc[j][0] - rcj) / rcj;
								}
								if (epsilon_tempj < epsilon_act)
								{
									if (epsilon_tempj >= epsilon_max)
									{
										temp = (1. / epsilon_tempj - 1. / 5. * log(epsilon_tempj) - 1. / 21. * epsilon_tempj * log(epsilon_tempj) + 0.673);
									}
									else if (epsilon_tempj < epsilon_max)
									{
										temp = (1. / epsilon_max - 1. / 5. * log(epsilon_max) - 1. / 21. * epsilon_max * log(epsilon_max) + 0.673);
										if (epsilon_tempj < -epsilon_dis)
										{
											temp = (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
										}
									}
									temp2 = temp - (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
									temp3 = temp2 * uc[j][0] * -6 * PI * rhof * mv * D / 2;
#pragma omp atomic
									Fpp_lub[k][0] += temp3;
								}
							}
						}//end lub
						if ((xc[j][0] < rcj) || (xc[j][0] > (NxT + 1 - rcj)))
						{
							if (xc[j][0] < rcj)
							{
								temp = (xc[j][0] - rcj) * dx;
							}
							else if (xc[j][0] > (NxT + 1 - rcj))
							{
								temp = (xc[j][0] + rcj - NxT - 1) * dx;
							}
							if (if_linear_DEM == 1)
							{
								force_n_DEM[0] = -K_n * temp - uc[j][0] * eta_n;
							}
							else
							{
								double sign = (temp > 0) ? 1.0 : -1.0;
								eta_n2_temp = eta_n2 * pow(fabs(temp), 0.25);
								force_n_DEM[0] = -K_n2 * pow(fabs(temp), 1.5) * sign - uc[j][0] * eta_n2_temp;
							}
#pragma omp atomic
							Fpp_n_DEM[k][0] += force_n_DEM[0];
						}
					}
					if (periodic_y == 0)
					{
						if (iflub == 1)
						{
							if ((xc[j][1] < (rcj + safe)) || (xc[j][1] > (NyT + 1 - rcj - safe)))
							{
								if (xc[j][1] < (rcj + safe))
								{
									epsilon_tempj = (xc[j][1] - rcj) / rcj;
								}
								else if (xc[j][1] > (NyT + 1 - rcj - safe))
								{
									epsilon_tempj = (NyT + 1 - xc[j][1] - rcj) / rcj;
								}
								if (epsilon_tempj < epsilon_act)
								{
									if (epsilon_tempj >= epsilon_max)
									{
										temp = (1. / epsilon_tempj - 1. / 5. * log(epsilon_tempj) - 1. / 21. * epsilon_tempj * log(epsilon_tempj) + 0.673);
									}
									else if (epsilon_tempj < epsilon_max)
									{
										temp = (1. / epsilon_max - 1. / 5. * log(epsilon_max) - 1. / 21. * epsilon_max * log(epsilon_max) + 0.673);
										if (epsilon_tempj < -epsilon_dis)
										{
											temp = (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
										}
									}
									temp2 = temp - (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
									temp3 = temp2 * uc[j][1] * -6 * PI * rhof * mv * D / 2;
#pragma omp atomic
									Fpp_lub[k][1] += temp3;
								}
							}
						}//end lub
						if ((xc[j][1] < rcj) || (xc[j][1] > (NyT + 1 - rcj)))
						{
							if (xc[j][1] < rcj)
							{
								temp = (xc[j][1] - rcj) * dx;
							}
							else if (xc[j][1] > (NyT + 1 - rcj))
							{
								temp = (xc[j][1] + rcj - NyT - 1) * dx;
							}
							if (if_linear_DEM == 1)
							{
								force_n_DEM[1] = -K_n * temp - uc[j][1] * eta_n;
							}
							else
							{
								double sign = (temp > 0) ? 1.0 : -1.0;
								eta_n2_temp = eta_n2 * pow(fabs(temp), 0.25);
								force_n_DEM[1] = -K_n2 * pow(fabs(temp), 1.5) * sign - uc[j][1] * eta_n2_temp;
							}
#pragma omp atomic
							Fpp_n_DEM[k][1] += force_n_DEM[1];
						}
					}
					if (periodic_z == 0)
					{
						if (iflub == 1)
						{
							if ((xc[j][2] < (rcj + safe)) || (xc[j][2] > (NzT + 1 - rcj - safe)))
							{
								if (xc[j][2] < (rcj + safe))
								{
									epsilon_tempj = (xc[j][2] - rcj) / rcj;
								}
								else if (xc[j][2] > (NzT + 1 - rcj - safe))
								{
									epsilon_tempj = (NzT + 1 - xc[j][2] - rcj) / rcj;
								}
								if (epsilon_tempj < epsilon_act)
								{
									if (epsilon_tempj >= epsilon_max)
									{
										temp = (1. / epsilon_tempj - 1. / 5. * log(epsilon_tempj) - 1. / 21. * epsilon_tempj * log(epsilon_tempj) + 0.673);
									}
									else if (epsilon_tempj < epsilon_max)
									{
										temp = (1. / epsilon_max - 1. / 5. * log(epsilon_max) - 1. / 21. * epsilon_max * log(epsilon_max) + 0.673);
										if (epsilon_tempj < -epsilon_dis)
										{
											temp = (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
										}
									}
									temp2 = temp - (1. / epsilon_act - 1. / 5. * log(epsilon_act) - 1. / 21. * epsilon_act * log(epsilon_act) + 0.673);
									temp3 = temp2 * uc[j][2] * -6 * PI * rhof * mv * D / 2;
#pragma omp atomic
									Fpp_lub[k][2] += temp3;
								}
							}
						}//end lub
						if ((xc[j][2] < rcj) || (xc[j][2] > (NzT + 1 - rcj)))
						{
							if (xc[j][2] < rcj)
							{
								temp = (xc[j][2] - rcj) * dx;
							}
							else if (xc[j][2] > (NzT + 1 - rcj))
							{
								temp = (xc[j][2] + rcj - NzT - 1) * dx;
							}
							if (if_linear_DEM == 1)
							{
								force_n_DEM[2] = -K_n * temp - uc[j][2] * eta_n;
							}
							else
							{
								double sign = (temp > 0) ? 1.0 : -1.0;
								eta_n2_temp = eta_n2 * pow(fabs(temp), 0.25);
								force_n_DEM[2] = -K_n2 * pow(fabs(temp), 1.5) * sign - uc[j][2] * eta_n2_temp;
							}
#pragma omp atomic
							Fpp_n_DEM[k][2] += force_n_DEM[2];
						}
					}
				}
			}
		}
    #pragma omp barrier
	#pragma omp for schedule(static) private(k, i)
		for (k = 0; k < np_local_poly; k++)
		{
			if (num_p_poly[k] >= 0)
			{
				for (i = 0; i < 3; i++)
				{
					F_lub_poly[k][i] = Fpp_lub[k][i];
					F_n_DEM_poly[k][i] = Fpp_n_DEM[k][i];
					F_t_DEM_poly[k][i] = Fpp_t_DEM[k][i];
					T_DEM_poly[k][i] = Tpp_t_DEM[k][i];
				}
			}
		}
    #pragma omp barrier
		if (iflub_DEM_test && thread_contact_file) {
			fclose(thread_contact_file);

			// 删除空文件
			//if (!thread_file_has_written) {
			//	if (remove(thread_filename) == 0) {
			//		// 可选：输出删除信息
			//		// printf("Rank %d Thread %d: Deleted empty file %s\n", me, thread_id, thread_filename);
			//	}
			//	else {
			//		fprintf(stderr, "Rank %d Thread %d: Failed to delete %s\n", me, thread_id, thread_filename);
			//	}
			//}
		}
	}//openmp jieshu

	end_dem_force_in_time = MPI_Wtime();
	#if if_gptl
		gptl_stop(GT_Pcolli);
	#endif

	MPI_Barrier(MPI_COMM_WORLD);
	/*
	if (iflub_DEM_test)
	{
		if (step % 1000 == 999)
		{
			if (contact_file)
			{
				fflush(contact_file);

				fclose(contact_file);
				contact_file = NULL;

				if (file_has_written == 0)
				{

					// 删除空文件 
					if (remove(current_filename) == 0)
					{
						printf("Rank %d: Deleted empty file %s\n", me, current_filename);
					}
					else {
						printf("Rank %d: Delete failed\n", me);
					}
				}
			}
		}
	}
	*/
}

void particle_evolvement(int me)
{
	int i, j, k;
	double Fgra;
	double uc1[3], wc1[3];
	double F_inf[3], T_inf[3];

	// boyance and gravity
	Fgra = (1.0 - rhof / rhop)*mp*G;

	avepU[0] = 0.0; avepU[1] = 0.0; avepU[2] = 0.0;
	avepW[0] = 0.0; avepW[1] = 0.0; avepW[2] = 0.0;
	ux_max = 0.0; uy_max = 0.0; uz_max = 0.0;

	IBforce[0] = 0.0; IBforce[1] = 0.0; IBforce[2] = 0.0;

	double local_avepU[3] = { 0.0, 0.0, 0.0 };
	double local_avepW[3] = { 0.0, 0.0, 0.0 };
	double local_IBforce[3] = { 0.0, 0.0, 0.0 };
	double local_ux_max = 0.0, local_uy_max = 0.0, local_uz_max = 0.0;
	double local_particle_ma_max = 0.0;
	int local_particle_ma_id = -1;

	double local_particle_ma_x[3] = { 0.0, 0.0, 0.0 };
	double local_particle_ma_u[3] = { 0.0, 0.0, 0.0 };

	// update particles' xc,uc,ac,wc,wac
	#pragma omp parallel reduction(+:local_avepU[0:3],local_avepW[0:3],local_IBforce[0:3])\
						 reduction(max:local_ux_max,local_uy_max,local_uz_max)
	{
		double thread_particle_ma_max = 0.0;
		int thread_particle_ma_id = -1;

		double thread_particle_ma_x[3] = { 0.0, 0.0, 0.0 };
		double thread_particle_ma_u[3] = { 0.0, 0.0, 0.0 };
	#pragma omp for schedule(dynamic, 10) private(k,i,j, uc1, wc1)
		for (k = 0; k < np_local; k++)
		{
			if (num_p[k] >= 0)
			{
				j = num_p[k];
				if (KeyProcess[k] == me)
				{
					for (i = 0; i < 3; i++)
					{
						uc1[i] = (1 + rhof / rhop) * uc[j][i] - rhof / rhop * ucc[j][i] + (F_IBM[j][i] + Fgra * G_dire[i] + F_lub[k][i] + F_n_DEM[k][i] + F_t_DEM[k][i]) * dt_DEM / mp;
						//uc1[i] = (1 + rhof / rhop)*uc[j][i] - rhof / rhop*ucc[j][i] + (F_IBM[j][i] + Fgra*G_dire[i] + F_lub[k][i])*dt / mp;
						xc[j][i] += dt_DEM * 0.5 * (uc[j][i] + uc1[i]) / dx;  //����dx��Ϊ�˱�Ϊ�������꣬�����ٻ���
						ucc[j][i] = uc[j][i];
						uc[j][i] = uc1[i];

						//rotational motion
						wc1[i] = (1 + rhof / rhop) * wc[j][i] - rhof / rhop * wcc[j][i] + (T_IBM[j][i] + T_DEM[k][i]) * dt_DEM / Mp_iner;
						//wc1[i] = (1 + rhof / rhop) * wc[j][i] - rhof / rhop * wcc[j][i] + (T_DEM[k][i]) * dt_DEM / Mp_iner;
						sitac[j][i] += dt_DEM * 0.5 * (wc[j][i] + wc1[i]);
						wcc[j][i] = wc[j][i];
						wc[j][i] = wc1[i];

						/*
						avepU[i] += uc[j][i];
						avepW[i] += wc[j][i];

						IBforce[i] += F_IBM[j][i];

						if (fabs(ux_max) < fabs(uc[j][0]))
							ux_max = uc[j][0];
						if (fabs(uy_max) < fabs(uc[j][1]))
							uy_max = uc[j][1];
						if (fabs(uz_max) < fabs(uc[j][2]))
							uz_max = uc[j][2];
						*/

						local_avepU[i] += uc[j][i];
						local_avepW[i] += wc[j][i];
						local_IBforce[i] += F_IBM[j][i];

						// 更新局部最大值
						if (fabs(local_ux_max) < fabs(uc[j][0]))
							local_ux_max = fabs(uc[j][0]);
						if (fabs(local_uy_max) < fabs(uc[j][1]))
							local_uy_max = fabs(uc[j][1]);
						if (fabs(local_uz_max) < fabs(uc[j][2]))
							local_uz_max = fabs(uc[j][2]);

					}
					//x���������Լ�¼
					if (xc[j][0] >= NxT)
					{
						cross_p[j][0] += 1;
					}
					if (xc[j][0] < 0)
					{
						cross_p[j][0] -= 1;
					}
					//y���������Լ�¼
					if (xc[j][1] >= NyT)
					{
						cross_p[j][1] += 1;
					}
					if (xc[j][1] < 0)
					{
						cross_p[j][1] -= 1;
					}
					//z���������Ա߽���������¼
					if (xc[j][2] >= NzT)
					{
						xc[j][2] -= NzT;
						cross_p[j][2] += 1;
					}
					if (xc[j][2] < 0)
					{
						xc[j][2] += NzT;
						cross_p[j][2] -= 1;
					}
					double particle_velocity_mag =
						sqrt(uc[j][0] * uc[j][0]
							+ uc[j][1] * uc[j][1]
							+ uc[j][2] * uc[j][2]);

					double particle_ma = particle_velocity_mag / cs;

					if (particle_ma > thread_particle_ma_max)
					{
						thread_particle_ma_max = particle_ma;
						thread_particle_ma_id = j;

						thread_particle_ma_x[0] = xc[j][0];
						thread_particle_ma_x[1] = xc[j][1];
						thread_particle_ma_x[2] = xc[j][2];

						thread_particle_ma_u[0] = uc[j][0];
						thread_particle_ma_u[1] = uc[j][1];
						thread_particle_ma_u[2] = uc[j][2];
					}
				}
			}
		}
#pragma omp critical(particle_ma_max_update)
		{
			if (thread_particle_ma_max > local_particle_ma_max)
			{
				local_particle_ma_max = thread_particle_ma_max;
				local_particle_ma_id = thread_particle_ma_id;

				for (int m = 0; m < 3; m++)
				{
					local_particle_ma_x[m] = thread_particle_ma_x[m];
					local_particle_ma_u[m] = thread_particle_ma_u[m];
				}
			}
		}
	}//end openmp

	particle_ma_max = local_particle_ma_max;

	if (local_particle_ma_max > MA_WARN)
	{
		fprintf(stderr,
			"[MA_WARNING][PARTICLE] "
			"step=%d step_DEM=%d rank=%d particle=%d "
			"coord=(%.12e, %.12e, %.12e) "
			"u=(%.12e, %.12e, %.12e) "
			"|u|=%.12e Ma=%.12e threshold=%.6f\n",
			step,
			step_DEM,
			me,
			local_particle_ma_id + 1,
			local_particle_ma_x[0],
			local_particle_ma_x[1],
			local_particle_ma_x[2],
			local_particle_ma_u[0],
			local_particle_ma_u[1],
			local_particle_ma_u[2],
			sqrt(local_particle_ma_u[0] * local_particle_ma_u[0]
				+ local_particle_ma_u[1] * local_particle_ma_u[1]
				+ local_particle_ma_u[2] * local_particle_ma_u[2]),
			local_particle_ma_max,
			MA_WARN);

		fflush(stderr);
	}

	for (i = 0; i < 3; i++) {
		avepU[i] = local_avepU[i];
		avepW[i] = local_avepW[i];
		IBforce[i] = local_IBforce[i];
	}
	ux_max = local_ux_max;
	uy_max = local_uy_max;
	uz_max = local_uz_max;

	MPI_Barrier(MPI_COMM_WORLD);
}

void particle_evolvement_poly(int me)
{
	int i, j, k;
	double Fgra, mpj, vpj, mpj_iner;
	double uc1[3], wc1[3];
	double F_inf[3], T_inf[3];

	avepU[0] = 0.0; avepU[1] = 0.0; avepU[2] = 0.0;
	avepW[0] = 0.0; avepW[1] = 0.0; avepW[2] = 0.0;
	ux_max = 0.0; uy_max = 0.0; uz_max = 0.0;

	IBforce[0] = 0.0; IBforce[1] = 0.0; IBforce[2] = 0.0;

	double local_avepU[3] = { 0.0, 0.0, 0.0 };
	double local_avepW[3] = { 0.0, 0.0, 0.0 };
	double local_IBforce[3] = { 0.0, 0.0, 0.0 };
	double local_ux_max = 0.0, local_uy_max = 0.0, local_uz_max = 0.0;
	double local_particle_ma_max = 0.0;
	int local_particle_ma_id = -1;

	double local_particle_ma_x[3] = { 0.0, 0.0, 0.0 };
	double local_particle_ma_u[3] = { 0.0, 0.0, 0.0 };

	// update particles' xc,uc,ac,wc,wac
	#pragma omp parallel reduction(+:local_avepU[0:3], local_avepW[0:3], local_IBforce[0:3]) \
                         reduction(max:local_ux_max, local_uy_max, local_uz_max)
	{
		double thread_particle_ma_max = 0.0;
		int thread_particle_ma_id = -1;

		double thread_particle_ma_x[3] = { 0.0, 0.0, 0.0 };
		double thread_particle_ma_u[3] = { 0.0, 0.0, 0.0 };
	#pragma omp for schedule(dynamic, 10) private(k, j, i, mpj, vpj, mpj_iner, Fgra, uc1, wc1)
		for (k = 0; k < np_local_poly; k++)
		{
			if (num_p_poly[k] >= 0)
			{
				j = num_p_poly[k];
				vpj = PI * pow((R_poly[rc_poly[j]] * dx_poly), 3) * 4. / 3.;
				mpj = vpj * rhoc_poly[j];
				mpj_iner = mpj * pow((R_poly[rc_poly[j]] * dx_poly), 2) * 2. / 5.;
				Fgra = (1.0 - rhof / rhoc_poly[j]) * mpj * G;
				if (KeyProcess_poly[k] == me)
				{
					for (i = 0; i < 3; i++)
					{
						uc1[i] = (1 + rhof / rhoc_poly[j]) * uc[j][i] - rhof / rhoc_poly[j] * ucc[j][i] + (F_IBM[j][i] + Fgra * G_dire[i] + F_lub_poly[k][i] + F_n_DEM_poly[k][i] + F_t_DEM_poly[k][i]) * dt_DEM / mpj;
						xc[j][i] += dt_DEM * 0.5 * (uc[j][i] + uc1[i]) / dx_poly;  //����dx��Ϊ�˱�Ϊ�������꣬�����ٻ���
						ucc[j][i] = uc[j][i];
						uc[j][i] = uc1[i];

						//rotational motion
						//wc1[i] = (1 + rhof / rhoc_poly[j]) * wc[j][i] - rhof / rhoc_poly[j] * wcc[j][i] + (T_IBM[j][i] + T_DEM_poly[k][i]) * dt_DEM / mpj_iner;
						wc1[i] = 0;
sitac[j][i] += dt_DEM * 0.5 * (wc[j][i] + wc1[i]);
						wcc[j][i] = wc[j][i];
						wc[j][i] = wc1[i];

						/*
						avepU[i] += (uc[j][i] * vpj);
						avepW[i] += (wc[j][i] * vpj);

						IBforce[i] += F_IBM[j][i];

						if (fabs(ux_max) < fabs(uc[j][0]))
							ux_max = uc[j][0];
						if (fabs(uy_max) < fabs(uc[j][1]))
							uy_max = uc[j][1];
						if (fabs(uz_max) < fabs(uc[j][2]))
							uz_max = uc[j][2];
						*/

						local_avepU[i] += (uc[j][i] * vpj);
						local_avepW[i] += (wc[j][i] * vpj);
						local_IBforce[i] += F_IBM[j][i];

						// 更新局部最大值
						if (fabs(local_ux_max) < fabs(uc[j][0]))
							local_ux_max = fabs(uc[j][0]);
						if (fabs(local_uy_max) < fabs(uc[j][1]))
							local_uy_max = fabs(uc[j][1]);
						if (fabs(local_uz_max) < fabs(uc[j][2]))
							local_uz_max = fabs(uc[j][2]);

					}
					//x���������Լ�¼
					if (xc[j][0] >= NxT)
					{
						cross_p[j][0] += 1;
					}
					if (xc[j][0] < 0)
					{
						cross_p[j][0] -= 1;
					}
					//y���������Լ�¼
					if (xc[j][1] >= NyT)
					{
						cross_p[j][1] += 1;
					}
					if (xc[j][1] < 0)
					{
						cross_p[j][1] -= 1;
					}
					//z���������Ա߽���������¼
					if (xc[j][2] >= NzT)
					{
						xc[j][2] -= NzT;
						cross_p[j][2] += 1;
					}
					if (xc[j][2] < 0)
					{
						xc[j][2] += NzT;
						cross_p[j][2] -= 1;
					}
					double particle_velocity_mag =
						sqrt(uc[j][0] * uc[j][0]
							+ uc[j][1] * uc[j][1]
							+ uc[j][2] * uc[j][2]);

					double particle_ma = particle_velocity_mag / cs;

					if (particle_ma > thread_particle_ma_max)
					{
						thread_particle_ma_max = particle_ma;
						thread_particle_ma_id = j;

						thread_particle_ma_x[0] = xc[j][0];
						thread_particle_ma_x[1] = xc[j][1];
						thread_particle_ma_x[2] = xc[j][2];

						thread_particle_ma_u[0] = uc[j][0];
						thread_particle_ma_u[1] = uc[j][1];
						thread_particle_ma_u[2] = uc[j][2];
					}

					//printf("evolvment Rank:%d %d_th par index=%d F_IBM=%f\t%f\t%f\tT_IBM=%f\t%f\t%f\n", me, j + 1, k, F_IBM[j][0], F_IBM[j][1], F_IBM[j][2], T_IBM[j][0], T_IBM[j][1], T_IBM[j][2]);
					//printf("wc=%f\t%f\t%f\twcc=%f\t%f\t%f\tT_DEM_poly=%f\t%f\t%f\n", wc[j][0], wc[j][1], wc[j][2], wcc[j][0], wcc[j][1], wcc[j][2], T_DEM_poly[k][0], T_DEM_poly[k][1], T_DEM_poly[k][2]);
				}
			}
		}
#pragma omp critical(particle_ma_max_update)
		{
			if (thread_particle_ma_max > local_particle_ma_max)
			{
				local_particle_ma_max = thread_particle_ma_max;
				local_particle_ma_id = thread_particle_ma_id;

				for (int m = 0; m < 3; m++)
				{
					local_particle_ma_x[m] = thread_particle_ma_x[m];
					local_particle_ma_u[m] = thread_particle_ma_u[m];
				}
			}
}
	}//end omp parallel

	particle_ma_max = local_particle_ma_max;

	if (local_particle_ma_max > MA_WARN)
	{
		fprintf(stderr,
			"[MA_WARNING][PARTICLE] "
			"step=%d step_DEM=%d rank=%d particle=%d "
			"coord=(%.12e, %.12e, %.12e) "
			"u=(%.12e, %.12e, %.12e) "
			"|u|=%.12e Ma=%.12e threshold=%.6f\n",
			step,
			step_DEM,
			me,
			local_particle_ma_id + 1,
			local_particle_ma_x[0],
			local_particle_ma_x[1],
			local_particle_ma_x[2],
			local_particle_ma_u[0],
			local_particle_ma_u[1],
			local_particle_ma_u[2],
			sqrt(local_particle_ma_u[0] * local_particle_ma_u[0]
				+ local_particle_ma_u[1] * local_particle_ma_u[1]
				+ local_particle_ma_u[2] * local_particle_ma_u[2]),
			local_particle_ma_max,
			MA_WARN);

		fflush(stderr);
	}

	for (i = 0; i < 3; i++) {
		avepU[i] = local_avepU[i];
		avepW[i] = local_avepW[i];
		IBforce[i] = local_IBforce[i];
	}
	ux_max = local_ux_max;
	uy_max = local_uy_max;
	uz_max = local_uz_max;

	MPI_Barrier(MPI_COMM_WORLD);
}

void Particle_index_update(int me)
{
	MPI_Status status;

	int x, y, i, j, k;
	int kk1, kk2, kk3, kk4, kk5, kk6, kk7, kk8;
	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int x_p0, y_p0;
	//4����
	double pxuw1_comm[np_comm_xface][22 + max_contact_num * 4];
	double pxuw3_comm[np_comm_yface][22 + max_contact_num * 4];
	double pxuw5_comm[np_comm_xface][22 + max_contact_num * 4];
	double pxuw7_comm[np_comm_yface][22 + max_contact_num * 4];
	//4����
	double pxuw2_comm[np_comm_corner][22 + max_contact_num * 4];
	double pxuw4_comm[np_comm_corner][22 + max_contact_num * 4];
	double pxuw6_comm[np_comm_corner][22 + max_contact_num * 4];
	double pxuw8_comm[np_comm_corner][22 + max_contact_num * 4];

	//ͨѶ��ʶ��
	int msgtag[8];

	me_y = int(me / NPROCx);
	me_x = me - me_y*NPROCx;

	//��һ����ַ�洢�����ֲ���ţ���ʼ��Ϊ-1
	for (k = 0; k < np_comm_xface; k++)
	{
		pxuw1_comm[k][0] = -1.0;
		pxuw5_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_yface; k++)
	{
		pxuw3_comm[k][0] = -1.0;
		pxuw7_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_corner; k++)
	{
		pxuw2_comm[k][0] = -1.0;
		pxuw4_comm[k][0] = -1.0;
		pxuw6_comm[k][0] = -1.0;
		pxuw8_comm[k][0] = -1.0;
	}

	kk1 = 0; kk2 = 0; kk3 = 0; kk4 = 0; kk5 = 0; kk6 = 0; kk7 = 0; kk8 = 0;
	//ͳ�Ƹ���������Ҫͨ�ŵĿ��������������뻺����
	for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
	{
		if (num_p[k] >= 0)
		{
			j = num_p[k];
			if (KeyProcess[k] == me)
			{
				//����ײ��������Ŀ���׼�����͵���Χ8���������
				//Prepare for sending to left,0
				if (xc[j][0] < xx1)
				{
					pxuw1_comm[kk1][0] = 1.0*j;
					for (i = 0; i < 3; i++)
					{
						pxuw1_comm[kk1][i + 1] = xc[j][i];
						pxuw1_comm[kk1][i + 4] = uc[j][i];
						pxuw1_comm[kk1][i + 7] = ucc[j][i];
						pxuw1_comm[kk1][i + 10] = wc[j][i];
						pxuw1_comm[kk1][i + 13] = wcc[j][i];
						pxuw1_comm[kk1][i + 16] = sitac[j][i];
						pxuw1_comm[kk1][i + 19] = 1.*cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw1_comm[kk1][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw1_comm[kk1][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw1_comm[kk1][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw1_comm[kk1][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk1++;
				}
				//Prepare for sending to upleft,1
				if ((xc[j][0] < xx1) && (xc[j][1] >= yyn1))
				{
					pxuw2_comm[kk2][0] = 1.0*j;
					for (i = 0; i < 3; i++)
					{
						pxuw2_comm[kk2][i + 1] = xc[j][i];
						pxuw2_comm[kk2][i + 4] = uc[j][i];
						pxuw2_comm[kk2][i + 7] = ucc[j][i];
						pxuw2_comm[kk2][i + 10] = wc[j][i];
						pxuw2_comm[kk2][i + 13] = wcc[j][i];
						pxuw2_comm[kk2][i + 16] = sitac[j][i];
						pxuw2_comm[kk2][i + 19] = 1.*cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw2_comm[kk2][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw2_comm[kk2][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw2_comm[kk2][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw2_comm[kk2][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk2++;
				}
				//up,2
				if (xc[j][1] >= yyn1)
				{
					pxuw3_comm[kk3][0] = 1.0 * j;
					for (i = 0; i < 3; i++)
					{
						pxuw3_comm[kk3][i + 1] = xc[j][i];
						pxuw3_comm[kk3][i + 4] = uc[j][i];
						pxuw3_comm[kk3][i + 7] = ucc[j][i];
						pxuw3_comm[kk3][i + 10] = wc[j][i];
						pxuw3_comm[kk3][i + 13] = wcc[j][i];
						pxuw3_comm[kk3][i + 16] = sitac[j][i];
						pxuw3_comm[kk3][i + 19] = 1.*cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw3_comm[kk3][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw3_comm[kk3][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw3_comm[kk3][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw3_comm[kk3][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk3++;
				}
				//upright,3
				if ((xc[j][0] >= xxn1) && (xc[j][1] >= yyn1))
				{
					pxuw4_comm[kk4][0] = 1.0*j;
					for (i = 0; i < 3; i++)
					{
						pxuw4_comm[kk4][i + 1] = xc[j][i];
						pxuw4_comm[kk4][i + 4] = uc[j][i];
						pxuw4_comm[kk4][i + 7] = ucc[j][i];
						pxuw4_comm[kk4][i + 10] = wc[j][i];
						pxuw4_comm[kk4][i + 13] = wcc[j][i];
						pxuw4_comm[kk4][i + 16] = sitac[j][i];
						pxuw4_comm[kk4][i + 19] = 1.*cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw4_comm[kk4][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw4_comm[kk4][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw4_comm[kk4][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw4_comm[kk4][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk4++;
				}
				//right,4
				if (xc[j][0] >= xxn1)
				{
					pxuw5_comm[kk5][0] = 1.0*j;
					for (i = 0; i < 3; i++)
					{
						pxuw5_comm[kk5][i + 1] = xc[j][i];
						pxuw5_comm[kk5][i + 4] = uc[j][i];
						pxuw5_comm[kk5][i + 7] = ucc[j][i];
						pxuw5_comm[kk5][i + 10] = wc[j][i];
						pxuw5_comm[kk5][i + 13] = wcc[j][i];
						pxuw5_comm[kk5][i + 16] = sitac[j][i];
						pxuw5_comm[kk5][i + 19] = 1.*cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw5_comm[kk5][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw5_comm[kk5][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw5_comm[kk5][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw5_comm[kk5][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk5++;
				}
				//lowright,5
				if ((xc[j][0] >= xxn1) && (xc[j][1] < yy1))
				{
					pxuw6_comm[kk6][0] = 1.0*j;
					for (i = 0; i < 3; i++)
					{
						pxuw6_comm[kk6][i + 1] = xc[j][i];
						pxuw6_comm[kk6][i + 4] = uc[j][i];
						pxuw6_comm[kk6][i + 7] = ucc[j][i];
						pxuw6_comm[kk6][i + 10] = wc[j][i];
						pxuw6_comm[kk6][i + 13] = wcc[j][i];
						pxuw6_comm[kk6][i + 16] = sitac[j][i];
						pxuw6_comm[kk6][i + 19] = 1.*cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw6_comm[kk6][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw6_comm[kk6][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw6_comm[kk6][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw6_comm[kk6][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk6++;
				}
				//low,6
				if (xc[j][1] < yy1)
				{
					pxuw7_comm[kk7][0] = 1.0*j;
					for (i = 0; i < 3; i++)
					{
						pxuw7_comm[kk7][i + 1] = xc[j][i];
						pxuw7_comm[kk7][i + 4] = uc[j][i];
						pxuw7_comm[kk7][i + 7] = ucc[j][i];
						pxuw7_comm[kk7][i + 10] = wc[j][i];
						pxuw7_comm[kk7][i + 13] = wcc[j][i];
						pxuw7_comm[kk7][i + 16] = sitac[j][i];
						pxuw7_comm[kk7][i + 19] = cross_p[j][i];
						pxuw7_comm[kk7][i + 19] = 1.*cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw7_comm[kk7][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw7_comm[kk7][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw7_comm[kk7][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw7_comm[kk7][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk7++;
				}
				//lowleft,7
				if ((xc[j][0] < xx1) && (xc[j][1] < yy1))
				{
					pxuw8_comm[kk8][0] = 1.0*j;
					for (i = 0; i < 3; i++)
					{
						pxuw8_comm[kk8][i + 1] = xc[j][i];
						pxuw8_comm[kk8][i + 4] = uc[j][i];
						pxuw8_comm[kk8][i + 7] = ucc[j][i];
						pxuw8_comm[kk8][i + 10] = wc[j][i];
						pxuw8_comm[kk8][i + 13] = wcc[j][i];
						pxuw8_comm[kk8][i + 16] = sitac[j][i];
						pxuw8_comm[kk8][i + 19] = 1.*cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw8_comm[kk8][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw8_comm[kk8][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw8_comm[kk8][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw8_comm[kk8][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk8++;
				}
				//����˶����߽����������Ƴ����ֲ���Ź�-1
				if ((xc[j][0] < xx) || (xc[j][1] < yy) || (xc[j][0] >= xxn) || (xc[j][1] >= yyn))
				{
					//num_p[k] = -1;
					KeyProcess[k] = -1;
				}
			}
			//���Ǳ��صĿ����ֲ���Ź�-1
			else
			{
				num_p[k] = -1;
				KeyProcess[k] = -1;
			}
		}
	}
	for (i = 0; i < 8; i++)
	{
		msgtag[i] = me * 10 + i;
	}
	MPI_Send(&pxuw1_comm[0][0], kk1 * (22 + max_contact_num * 4), MPI_DOUBLE, Left_me, msgtag[0], MPI_COMM_WORLD);
	MPI_Send(&pxuw2_comm[0][0], kk2 * (22 + max_contact_num * 4), MPI_DOUBLE, Upleft_me, msgtag[1], MPI_COMM_WORLD);
	MPI_Send(&pxuw3_comm[0][0], kk3 * (22 + max_contact_num * 4), MPI_DOUBLE, Up_me, msgtag[2], MPI_COMM_WORLD);
	MPI_Send(&pxuw4_comm[0][0], kk4 * (22 + max_contact_num * 4), MPI_DOUBLE, Upright_me, msgtag[3], MPI_COMM_WORLD);
	MPI_Send(&pxuw5_comm[0][0], kk5 * (22 + max_contact_num * 4), MPI_DOUBLE, Right_me, msgtag[4], MPI_COMM_WORLD);
	MPI_Send(&pxuw6_comm[0][0], kk6 * (22 + max_contact_num * 4), MPI_DOUBLE, Lowright_me, msgtag[5], MPI_COMM_WORLD);
	MPI_Send(&pxuw7_comm[0][0], kk7 * (22 + max_contact_num * 4), MPI_DOUBLE, Low_me, msgtag[6], MPI_COMM_WORLD);
	MPI_Send(&pxuw8_comm[0][0], kk8 * (22 + max_contact_num * 4), MPI_DOUBLE, Lowleft_me, msgtag[7], MPI_COMM_WORLD);

	//���صĻ��������
	//��һ����ַ�洢�����ֲ���ţ���ʼ��Ϊ-1
	for (k = 0; k < np_comm_xface; k++)
	{
		pxuw1_comm[k][0] = -1.0;
		pxuw5_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_yface; k++)
	{
		pxuw3_comm[k][0] = -1.0;
		pxuw7_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_corner; k++)
	{
		pxuw2_comm[k][0] = -1.0;
		pxuw4_comm[k][0] = -1.0;
		pxuw6_comm[k][0] = -1.0;
		pxuw8_comm[k][0] = -1.0;
	}

	int buffer_size;

	//Receive frome left, me is right
	if (Left_me != MPI_PROC_NULL) {
		MPI_Probe(Left_me, (Left_me * 10 + 4), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw5_comm[0][0], buffer_size, MPI_DOUBLE, Left_me, (Left_me * 10 + 4), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome left, me is right
			if (pxuw5_comm[k][0] >= 0)
			{
				j = (int)pxuw5_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw5_comm[k][i + 1];
					uc[j][i] = pxuw5_comm[k][i + 4];
					ucc[j][i] = pxuw5_comm[k][i + 7];
					wc[j][i] = pxuw5_comm[k][i + 10];
					wcc[j][i] = pxuw5_comm[k][i + 13];
					sitac[j][i] = pxuw5_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw5_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw5_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw5_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw5_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw5_comm[k][22 + i * 4 + 3]);
				}

				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local; i++)
				{
					if (num_p[i] == (-1))
					{
						num_p[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess[i] = me;
						break;
					}
				}
			}
		}
	}

	//Receive frome Upleft, me is Lowright
	if (Upleft_me != MPI_PROC_NULL) {
		MPI_Probe(Upleft_me, (Upleft_me * 10 + 5), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw6_comm[0][0], buffer_size, MPI_DOUBLE, Upleft_me, (Upleft_me * 10 + 5), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Upleft, me is Lowright
			if (pxuw6_comm[k][0] >= 0)
			{
				j = (int)pxuw6_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw6_comm[k][i + 1];
					uc[j][i] = pxuw6_comm[k][i + 4];
					ucc[j][i] = pxuw6_comm[k][i + 7];
					wc[j][i] = pxuw6_comm[k][i + 10];
					wcc[j][i] = pxuw6_comm[k][i + 13];
					sitac[j][i] = pxuw6_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw6_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw6_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw6_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw6_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw6_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local; i++)
				{
					if (num_p[i] == (-1))
					{
						num_p[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess[i] = me;
						break;
					}
				}
			}
		}
	}

	//Receive frome Up, me is Low
	if (Up_me != MPI_PROC_NULL) {
		MPI_Probe(Up_me, (Up_me * 10 + 6), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw7_comm[0][0], buffer_size, MPI_DOUBLE, Up_me, (Up_me * 10 + 6), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Up, me is Low
			if (pxuw7_comm[k][0] >= 0)
			{
				j = (int)pxuw7_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw7_comm[k][i + 1];
					uc[j][i] = pxuw7_comm[k][i + 4];
					ucc[j][i] = pxuw7_comm[k][i + 7];
					wc[j][i] = pxuw7_comm[k][i + 10];
					wcc[j][i] = pxuw7_comm[k][i + 13];
					sitac[j][i] = pxuw7_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw7_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw7_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw7_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw7_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw7_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local; i++)
				{
					if (num_p[i] == (-1))
					{
						num_p[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess[i] = me;
						break;
					}
				}
			}
		}
	}

	//Receive frome Upright, me is Lowleft
	if (Upright_me != MPI_PROC_NULL) {
		MPI_Probe(Upright_me, (Upright_me * 10 + 7), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw8_comm[0][0], buffer_size, MPI_DOUBLE, Upright_me, (Upright_me * 10 + 7), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Upright, me is Lowleft
			if (pxuw8_comm[k][0] >= 0)
			{
				j = (int)pxuw8_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw8_comm[k][i + 1];
					uc[j][i] = pxuw8_comm[k][i + 4];
					ucc[j][i] = pxuw8_comm[k][i + 7];
					wc[j][i] = pxuw8_comm[k][i + 10];
					wcc[j][i] = pxuw8_comm[k][i + 13];
					sitac[j][i] = pxuw8_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw8_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw8_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw8_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw8_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw8_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local; i++)
				{
					if (num_p[i] == (-1))
					{
						num_p[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess[i] = me;
						break;
					}
				}
			}
		}
	}

	//Receive frome Right, me is Left
	if (Right_me != MPI_PROC_NULL) {
		MPI_Probe(Right_me, (Right_me * 10 + 0), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw1_comm[0][0], buffer_size, MPI_DOUBLE, Right_me, (Right_me * 10 + 0), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Right, me is Left
			if (pxuw1_comm[k][0] >= 0)
			{
				j = (int)pxuw1_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw1_comm[k][i + 1];
					uc[j][i] = pxuw1_comm[k][i + 4];
					ucc[j][i] = pxuw1_comm[k][i + 7];
					wc[j][i] = pxuw1_comm[k][i + 10];
					wcc[j][i] = pxuw1_comm[k][i + 13];
					sitac[j][i] = pxuw1_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw1_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw1_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw1_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw1_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw1_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local; i++)
				{
					if (num_p[i] == (-1))
					{
						num_p[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess[i] = me;
						break;
					}
				}
			}
		}
	}
	//Receive frome Lowright, me is Upleft
	if (Lowright_me != MPI_PROC_NULL) {
		MPI_Probe(Lowright_me, (Lowright_me * 10 + 1), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw2_comm[0][0], buffer_size, MPI_DOUBLE, Lowright_me, (Lowright_me * 10 + 1), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Lowright, me is Upleft
			if (pxuw2_comm[k][0] >= 0)
			{
				j = (int)pxuw2_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw2_comm[k][i + 1];
					uc[j][i] = pxuw2_comm[k][i + 4];
					ucc[j][i] = pxuw2_comm[k][i + 7];
					wc[j][i] = pxuw2_comm[k][i + 10];
					wcc[j][i] = pxuw2_comm[k][i + 13];
					sitac[j][i] = pxuw2_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw2_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw2_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw2_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw2_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw2_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local; i++)
				{
					if (num_p[i] == (-1))
					{
						num_p[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess[i] = me;
						break;
					}
				}
			}
		}
	}

	//Receive frome Low, me is Up
	if (Low_me != MPI_PROC_NULL) {
		MPI_Probe(Low_me, (Low_me * 10 + 2), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw3_comm[0][0], buffer_size, MPI_DOUBLE, Low_me, (Low_me * 10 + 2), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Low, me is Up
			if (pxuw3_comm[k][0] >= 0)
			{
				j = (int)pxuw3_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw3_comm[k][i + 1];
					uc[j][i] = pxuw3_comm[k][i + 4];
					ucc[j][i] = pxuw3_comm[k][i + 7];
					wc[j][i] = pxuw3_comm[k][i + 10];
					wcc[j][i] = pxuw3_comm[k][i + 13];
					sitac[j][i] = pxuw3_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw3_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw3_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw3_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw3_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw3_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local; i++)
				{
					if (num_p[i] == (-1))
					{
						num_p[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess[i] = me;
						break;
					}
				}
			}
		}
	}

	//Receive frome Lowleft, me is Upright
	if (Lowleft_me != MPI_PROC_NULL) {
		MPI_Probe(Lowleft_me, (Lowleft_me * 10 + 3), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw4_comm[0][0], buffer_size, MPI_DOUBLE, Lowleft_me, (Lowleft_me * 10 + 3), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Lowleft, me is Upright
			if (pxuw4_comm[k][0] >= 0)
			{
				j = (int)pxuw4_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw4_comm[k][i + 1];
					uc[j][i] = pxuw4_comm[k][i + 4];
					ucc[j][i] = pxuw4_comm[k][i + 7];
					wc[j][i] = pxuw4_comm[k][i + 10];
					wcc[j][i] = pxuw4_comm[k][i + 13];
					sitac[j][i] = pxuw4_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw4_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw4_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw4_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw4_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw4_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local; i++)
				{
					if (num_p[i] == (-1))
					{
						num_p[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess[i] = me;
						break;
					}
				}
			}
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);
}

void Particle_index_update_poly(int me)
{
	MPI_Status status;

	int x, y, i, j, k;
	int kk1, kk2, kk3, kk4, kk5, kk6, kk7, kk8;
	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������
	int x_p0, y_p0;
	//4����
	double pxuw1_comm[np_comm_xface_poly][22 + max_contact_num * 4];
	double pxuw3_comm[np_comm_yface_poly][22 + max_contact_num * 4];
	double pxuw5_comm[np_comm_xface_poly][22 + max_contact_num * 4];
	double pxuw7_comm[np_comm_yface_poly][22 + max_contact_num * 4];
	//4����
	double pxuw2_comm[np_comm_corner_poly][22 + max_contact_num * 4];
	double pxuw4_comm[np_comm_corner_poly][22 + max_contact_num * 4];
	double pxuw6_comm[np_comm_corner_poly][22 + max_contact_num * 4];
	double pxuw8_comm[np_comm_corner_poly][22 + max_contact_num * 4];

	//ͨѶ��ʶ��
	int msgtag[8];

	me_y = int(me / NPROCx);
	me_x = me - me_y * NPROCx;

	//��һ����ַ�洢�����ֲ���ţ���ʼ��Ϊ-1
	for (k = 0; k < np_comm_xface_poly; k++)
	{
		pxuw1_comm[k][0] = -1.0;
		pxuw5_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_yface_poly; k++)
	{
		pxuw3_comm[k][0] = -1.0;
		pxuw7_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_corner_poly; k++)
	{
		pxuw2_comm[k][0] = -1.0;
		pxuw4_comm[k][0] = -1.0;
		pxuw6_comm[k][0] = -1.0;
		pxuw8_comm[k][0] = -1.0;
	}

	kk1 = 0; kk2 = 0; kk3 = 0; kk4 = 0; kk5 = 0; kk6 = 0; kk7 = 0; kk8 = 0;
	//ͳ�Ƹ���������Ҫͨ�ŵĿ��������������뻺����
	for (k = 0; k < np_local_poly; k++)  //���ݾֲ���ż���
	{
		if (num_p_poly[k] >= 0)
		{
			j = num_p_poly[k];
			if (KeyProcess_poly[k] == me)
			{
				//����ײ��������Ŀ���׼�����͵���Χ8���������
				//Prepare for sending to left,0
				if (xc[j][0] < xx1)
				{
					pxuw1_comm[kk1][0] = 1.0 * j;
					for (i = 0; i < 3; i++)
					{
						pxuw1_comm[kk1][i + 1] = xc[j][i];
						pxuw1_comm[kk1][i + 4] = uc[j][i];
						pxuw1_comm[kk1][i + 7] = ucc[j][i];
						pxuw1_comm[kk1][i + 10] = wc[j][i];
						pxuw1_comm[kk1][i + 13] = wcc[j][i];
						pxuw1_comm[kk1][i + 16] = sitac[j][i];
						pxuw1_comm[kk1][i + 19] = 1. * cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw1_comm[kk1][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw1_comm[kk1][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw1_comm[kk1][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw1_comm[kk1][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk1++;
				}
				//Prepare for sending to upleft,1
				if ((xc[j][0] < xx1) && (xc[j][1] >= yyn1))
				{
					pxuw2_comm[kk2][0] = 1.0 * j;
					for (i = 0; i < 3; i++)
					{
						pxuw2_comm[kk2][i + 1] = xc[j][i];
						pxuw2_comm[kk2][i + 4] = uc[j][i];
						pxuw2_comm[kk2][i + 7] = ucc[j][i];
						pxuw2_comm[kk2][i + 10] = wc[j][i];
						pxuw2_comm[kk2][i + 13] = wcc[j][i];
						pxuw2_comm[kk2][i + 16] = sitac[j][i];
						pxuw2_comm[kk2][i + 19] = 1. * cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw2_comm[kk2][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw2_comm[kk2][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw2_comm[kk2][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw2_comm[kk2][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk2++;
				}
				//up,2
				if (xc[j][1] >= yyn1)
				{
					pxuw3_comm[kk3][0] = 1.0 * j;
					for (i = 0; i < 3; i++)
					{
						pxuw3_comm[kk3][i + 1] = xc[j][i];
						pxuw3_comm[kk3][i + 4] = uc[j][i];
						pxuw3_comm[kk3][i + 7] = ucc[j][i];
						pxuw3_comm[kk3][i + 10] = wc[j][i];
						pxuw3_comm[kk3][i + 13] = wcc[j][i];
						pxuw3_comm[kk3][i + 16] = sitac[j][i];
						pxuw3_comm[kk3][i + 19] = 1. * cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw3_comm[kk3][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw3_comm[kk3][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw3_comm[kk3][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw3_comm[kk3][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk3++;
				}
				//upright,3
				if ((xc[j][0] >= xxn1) && (xc[j][1] >= yyn1))
				{
					pxuw4_comm[kk4][0] = 1.0 * j;
					for (i = 0; i < 3; i++)
					{
						pxuw4_comm[kk4][i + 1] = xc[j][i];
						pxuw4_comm[kk4][i + 4] = uc[j][i];
						pxuw4_comm[kk4][i + 7] = ucc[j][i];
						pxuw4_comm[kk4][i + 10] = wc[j][i];
						pxuw4_comm[kk4][i + 13] = wcc[j][i];
						pxuw4_comm[kk4][i + 16] = sitac[j][i];
						pxuw4_comm[kk4][i + 19] = 1. * cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw4_comm[kk4][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw4_comm[kk4][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw4_comm[kk4][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw4_comm[kk4][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk4++;
				}
				//right,4
				if (xc[j][0] >= xxn1)
				{
					pxuw5_comm[kk5][0] = 1.0 * j;
					for (i = 0; i < 3; i++)
					{
						pxuw5_comm[kk5][i + 1] = xc[j][i];
						pxuw5_comm[kk5][i + 4] = uc[j][i];
						pxuw5_comm[kk5][i + 7] = ucc[j][i];
						pxuw5_comm[kk5][i + 10] = wc[j][i];
						pxuw5_comm[kk5][i + 13] = wcc[j][i];
						pxuw5_comm[kk5][i + 16] = sitac[j][i];
						pxuw5_comm[kk5][i + 19] = 1. * cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw5_comm[kk5][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw5_comm[kk5][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw5_comm[kk5][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw5_comm[kk5][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk5++;
				}
				//lowright,5
				if ((xc[j][0] >= xxn1) && (xc[j][1] < yy1))
				{
					pxuw6_comm[kk6][0] = 1.0 * j;
					for (i = 0; i < 3; i++)
					{
						pxuw6_comm[kk6][i + 1] = xc[j][i];
						pxuw6_comm[kk6][i + 4] = uc[j][i];
						pxuw6_comm[kk6][i + 7] = ucc[j][i];
						pxuw6_comm[kk6][i + 10] = wc[j][i];
						pxuw6_comm[kk6][i + 13] = wcc[j][i];
						pxuw6_comm[kk6][i + 16] = sitac[j][i];
						pxuw6_comm[kk6][i + 19] = 1. * cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw6_comm[kk6][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw6_comm[kk6][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw6_comm[kk6][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw6_comm[kk6][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk6++;
				}
				//low,6
				if (xc[j][1] < yy1)
				{
					pxuw7_comm[kk7][0] = 1.0 * j;
					for (i = 0; i < 3; i++)
					{
						pxuw7_comm[kk7][i + 1] = xc[j][i];
						pxuw7_comm[kk7][i + 4] = uc[j][i];
						pxuw7_comm[kk7][i + 7] = ucc[j][i];
						pxuw7_comm[kk7][i + 10] = wc[j][i];
						pxuw7_comm[kk7][i + 13] = wcc[j][i];
						pxuw7_comm[kk7][i + 16] = sitac[j][i];
						pxuw7_comm[kk7][i + 19] = cross_p[j][i];
						pxuw7_comm[kk7][i + 19] = 1. * cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw7_comm[kk7][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw7_comm[kk7][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw7_comm[kk7][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw7_comm[kk7][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk7++;
				}
				//lowleft,7
				if ((xc[j][0] < xx1) && (xc[j][1] < yy1))
				{
					pxuw8_comm[kk8][0] = 1.0 * j;
					for (i = 0; i < 3; i++)
					{
						pxuw8_comm[kk8][i + 1] = xc[j][i];
						pxuw8_comm[kk8][i + 4] = uc[j][i];
						pxuw8_comm[kk8][i + 7] = ucc[j][i];
						pxuw8_comm[kk8][i + 10] = wc[j][i];
						pxuw8_comm[kk8][i + 13] = wcc[j][i];
						pxuw8_comm[kk8][i + 16] = sitac[j][i];
						pxuw8_comm[kk8][i + 19] = 1. * cross_p[j][i];
					}
					for (i = 0; i < max_contact_num; i++)
					{
						pxuw8_comm[kk8][22 + i * 4 + 0] = tangential_overlap[j][i][0];
						pxuw8_comm[kk8][22 + i * 4 + 1] = tangential_overlap[j][i][1];
						pxuw8_comm[kk8][22 + i * 4 + 2] = tangential_overlap[j][i][2];
						pxuw8_comm[kk8][22 + i * 4 + 3] = tangential_contact_index[j][i] * 1.0;
					}
					kk8++;
				}
				//����˶����߽����������Ƴ����ֲ���Ź�-1
				if ((xc[j][0] < xx) || (xc[j][1] < yy) || (xc[j][0] >= xxn) || (xc[j][1] >= yyn))
				{
					//num_p[k] = -1;
					KeyProcess_poly[k] = -1;
				}
			}
			//���Ǳ��صĿ����ֲ���Ź�-1
			else
			{
				num_p_poly[k] = -1;
				KeyProcess_poly[k] = -1;
			}
		}
	}
	for (i = 0; i < 8; i++)
	{
		msgtag[i] = me * 10 + i;
	}
	MPI_Send(&pxuw1_comm[0][0], kk1 * (22 + max_contact_num * 4), MPI_DOUBLE, Left_me, msgtag[0], MPI_COMM_WORLD);
	MPI_Send(&pxuw2_comm[0][0], kk2 * (22 + max_contact_num * 4), MPI_DOUBLE, Upleft_me, msgtag[1], MPI_COMM_WORLD);
	MPI_Send(&pxuw3_comm[0][0], kk3 * (22 + max_contact_num * 4), MPI_DOUBLE, Up_me, msgtag[2], MPI_COMM_WORLD);
	MPI_Send(&pxuw4_comm[0][0], kk4 * (22 + max_contact_num * 4), MPI_DOUBLE, Upright_me, msgtag[3], MPI_COMM_WORLD);
	MPI_Send(&pxuw5_comm[0][0], kk5 * (22 + max_contact_num * 4), MPI_DOUBLE, Right_me, msgtag[4], MPI_COMM_WORLD);
	MPI_Send(&pxuw6_comm[0][0], kk6 * (22 + max_contact_num * 4), MPI_DOUBLE, Lowright_me, msgtag[5], MPI_COMM_WORLD);
	MPI_Send(&pxuw7_comm[0][0], kk7 * (22 + max_contact_num * 4), MPI_DOUBLE, Low_me, msgtag[6], MPI_COMM_WORLD);
	MPI_Send(&pxuw8_comm[0][0], kk8 * (22 + max_contact_num * 4), MPI_DOUBLE, Lowleft_me, msgtag[7], MPI_COMM_WORLD);

	//���صĻ��������
	//��һ����ַ�洢�����ֲ���ţ���ʼ��Ϊ-1
	for (k = 0; k < np_comm_xface_poly; k++)
	{
		pxuw1_comm[k][0] = -1.0;
		pxuw5_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_yface_poly; k++)
	{
		pxuw3_comm[k][0] = -1.0;
		pxuw7_comm[k][0] = -1.0;
	}
	for (k = 0; k < np_comm_corner_poly; k++)
	{
		pxuw2_comm[k][0] = -1.0;
		pxuw4_comm[k][0] = -1.0;
		pxuw6_comm[k][0] = -1.0;
		pxuw8_comm[k][0] = -1.0;
	}

	int buffer_size;

	//Receive frome left, me is right
	if (Left_me != MPI_PROC_NULL) {
		MPI_Probe(Left_me, (Left_me * 10 + 4), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw5_comm[0][0], buffer_size, MPI_DOUBLE, Left_me, (Left_me * 10 + 4), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome left, me is right
			if (pxuw5_comm[k][0] >= 0)
			{
				j = (int)pxuw5_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw5_comm[k][i + 1];
					uc[j][i] = pxuw5_comm[k][i + 4];
					ucc[j][i] = pxuw5_comm[k][i + 7];
					wc[j][i] = pxuw5_comm[k][i + 10];
					wcc[j][i] = pxuw5_comm[k][i + 13];
					sitac[j][i] = pxuw5_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw5_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw5_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw5_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw5_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw5_comm[k][22 + i * 4 + 3]);
				}

				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R_poly_max - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R_poly_max + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R_poly_max - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R_poly_max + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local_poly; i++)
				{
					if (num_p_poly[i] == (-1))
					{
						num_p_poly[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess_poly[i] = me;
						break;
					}
				}
			}
		}
	}

	//Receive frome Upleft, me is Lowright
	if (Upleft_me != MPI_PROC_NULL) {
		MPI_Probe(Upleft_me, (Upleft_me * 10 + 5), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw6_comm[0][0], buffer_size, MPI_DOUBLE, Upleft_me, (Upleft_me * 10 + 5), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Upleft, me is Lowright
			if (pxuw6_comm[k][0] >= 0)
			{
				j = (int)pxuw6_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw6_comm[k][i + 1];
					uc[j][i] = pxuw6_comm[k][i + 4];
					ucc[j][i] = pxuw6_comm[k][i + 7];
					wc[j][i] = pxuw6_comm[k][i + 10];
					wcc[j][i] = pxuw6_comm[k][i + 13];
					sitac[j][i] = pxuw6_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw6_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw6_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw6_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw6_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw6_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R_poly_max - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R_poly_max + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R_poly_max - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R_poly_max + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local_poly; i++)
				{
					if (num_p_poly[i] == (-1))
					{
						num_p_poly[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess_poly[i] = me;
						break;
					}
				}
			}
		}
	}

	//Receive frome Up, me is Low
	if (Up_me != MPI_PROC_NULL) {
		MPI_Probe(Up_me, (Up_me * 10 + 6), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw7_comm[0][0], buffer_size, MPI_DOUBLE, Up_me, (Up_me * 10 + 6), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Up, me is Low
			if (pxuw7_comm[k][0] >= 0)
			{
				j = (int)pxuw7_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw7_comm[k][i + 1];
					uc[j][i] = pxuw7_comm[k][i + 4];
					ucc[j][i] = pxuw7_comm[k][i + 7];
					wc[j][i] = pxuw7_comm[k][i + 10];
					wcc[j][i] = pxuw7_comm[k][i + 13];
					sitac[j][i] = pxuw7_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw7_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw7_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw7_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw7_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw7_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R_poly_max - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R_poly_max + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R_poly_max - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R_poly_max + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local_poly; i++)
				{
					if (num_p_poly[i] == (-1))
					{
						num_p_poly[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess_poly[i] = me;
						break;
					}
				}
			}
		}
	}

	//Receive frome Upright, me is Lowleft
	if (Upright_me != MPI_PROC_NULL) {
		MPI_Probe(Upright_me, (Upright_me * 10 + 7), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw8_comm[0][0], buffer_size, MPI_DOUBLE, Upright_me, (Upright_me * 10 + 7), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Upright, me is Lowleft
			if (pxuw8_comm[k][0] >= 0)
			{
				j = (int)pxuw8_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw8_comm[k][i + 1];
					uc[j][i] = pxuw8_comm[k][i + 4];
					ucc[j][i] = pxuw8_comm[k][i + 7];
					wc[j][i] = pxuw8_comm[k][i + 10];
					wcc[j][i] = pxuw8_comm[k][i + 13];
					sitac[j][i] = pxuw8_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw8_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw8_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw8_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw8_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw8_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R_poly_max - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R_poly_max + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R_poly_max - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R_poly_max + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local_poly; i++)
				{
					if (num_p_poly[i] == (-1))
					{
						num_p_poly[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess_poly[i] = me;
						break;
					}
				}
			}
		}
	}
	//Receive frome Right, me is Left
	if (Right_me != MPI_PROC_NULL) {
		MPI_Probe(Right_me, (Right_me * 10 + 0), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw1_comm[0][0], buffer_size, MPI_DOUBLE, Right_me, (Right_me * 10 + 0), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Right, me is Left
			if (pxuw1_comm[k][0] >= 0)
			{
				j = (int)pxuw1_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw1_comm[k][i + 1];
					uc[j][i] = pxuw1_comm[k][i + 4];
					ucc[j][i] = pxuw1_comm[k][i + 7];
					wc[j][i] = pxuw1_comm[k][i + 10];
					wcc[j][i] = pxuw1_comm[k][i + 13];
					sitac[j][i] = pxuw1_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw1_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw1_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw1_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw1_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw1_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R_poly_max - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R_poly_max + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R_poly_max - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R_poly_max + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local_poly; i++)
				{
					if (num_p_poly[i] == (-1))
					{
						num_p_poly[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess_poly[i] = me;
						break;
					}
				}
			}
		}
	}
	//Receive frome Lowright, me is Upleft
	if (Lowright_me != MPI_PROC_NULL) {
		MPI_Probe(Lowright_me, (Lowright_me * 10 + 1), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw2_comm[0][0], buffer_size, MPI_DOUBLE, Lowright_me, (Lowright_me * 10 + 1), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Lowright, me is Upleft
			if (pxuw2_comm[k][0] >= 0)
			{
				j = (int)pxuw2_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw2_comm[k][i + 1];
					uc[j][i] = pxuw2_comm[k][i + 4];
					ucc[j][i] = pxuw2_comm[k][i + 7];
					wc[j][i] = pxuw2_comm[k][i + 10];
					wcc[j][i] = pxuw2_comm[k][i + 13];
					sitac[j][i] = pxuw2_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw2_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw2_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw2_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw2_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw2_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R_poly_max - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R_poly_max + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R_poly_max - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R_poly_max + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local_poly; i++)
				{
					if (num_p_poly[i] == (-1))
					{
						num_p_poly[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess_poly[i] = me;
						break;
					}
				}
			}
		}
	}

	//Receive frome Low, me is Up
	if (Low_me != MPI_PROC_NULL) {
		MPI_Probe(Low_me, (Low_me * 10 + 2), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw3_comm[0][0], buffer_size, MPI_DOUBLE, Low_me, (Low_me * 10 + 2), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Low, me is Up
			if (pxuw3_comm[k][0] >= 0)
			{
				j = (int)pxuw3_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw3_comm[k][i + 1];
					uc[j][i] = pxuw3_comm[k][i + 4];
					ucc[j][i] = pxuw3_comm[k][i + 7];
					wc[j][i] = pxuw3_comm[k][i + 10];
					wcc[j][i] = pxuw3_comm[k][i + 13];
					sitac[j][i] = pxuw3_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw3_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw3_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw3_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw3_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw3_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R_poly_max - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R_poly_max + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R_poly_max - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R_poly_max + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local_poly; i++)
				{
					if (num_p_poly[i] == (-1))
					{
						num_p_poly[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess_poly[i] = me;
						break;
					}
				}
			}
		}
	}

	//Receive frome Lowleft, me is Upright
	if (Lowleft_me != MPI_PROC_NULL) {
		MPI_Probe(Lowleft_me, (Lowleft_me * 10 + 3), MPI_COMM_WORLD, &status);
		MPI_Get_count(&status, MPI_DOUBLE, &buffer_size);
		MPI_Recv(&pxuw4_comm[0][0], buffer_size, MPI_DOUBLE, Lowleft_me, (Lowleft_me * 10 + 3), MPI_COMM_WORLD, &status);
		for (k = 0; k <= int(buffer_size / (22 + max_contact_num * 4)); k++)
		{
			//Receive frome Lowleft, me is Upright
			if (pxuw4_comm[k][0] >= 0)
			{
				j = (int)pxuw4_comm[k][0];
				for (i = 0; i < 3; i++)
				{
					xc[j][i] = pxuw4_comm[k][i + 1];
					uc[j][i] = pxuw4_comm[k][i + 4];
					ucc[j][i] = pxuw4_comm[k][i + 7];
					wc[j][i] = pxuw4_comm[k][i + 10];
					wcc[j][i] = pxuw4_comm[k][i + 13];
					sitac[j][i] = pxuw4_comm[k][i + 16];
					cross_p[j][i] = (int)pxuw4_comm[k][i + 19];
				}
				for (i = 0; i < max_contact_num; i++)
				{
					tangential_overlap[j][i][0] = pxuw4_comm[k][22 + i * 4 + 0];
					tangential_overlap[j][i][1] = pxuw4_comm[k][22 + i * 4 + 1];
					tangential_overlap[j][i][2] = pxuw4_comm[k][22 + i * 4 + 2];
					tangential_contact_index[j][i] = int(pxuw4_comm[k][22 + i * 4 + 3]);
				}
				//������������ʵ��		
				if (periodic_x == 1)
				{
					//�����һ�кˣ�
					if (me % NPROCx == 0)
					{
						if (xc[j][0] >= (NxT - 2 * R_poly_max - safe))
							xc[j][0] -= NxT;
					}
					//���ұ�һ�кˣ����������ߴ�������Ŀ���������������������
					if ((me + 1) % NPROCx == 0)
					{
						if (xc[j][0] < (2 * R_poly_max + safe))
							xc[j][0] += NxT;
					}
				}
				if (periodic_y == 1)
				{
					if (me < NPROCx)                  //������һ�ź�
					{
						if (xc[j][1] >= (NyT - 2 * R_poly_max - safe))
							xc[j][1] -= NyT;
					}
					if (me >= (NPROCx * (NPROCy - 1)))   //������һ�ź�
					{
						if (xc[j][1] < (2 * R_poly_max + safe))
							xc[j][1] += NyT;
					}
				}

				for (i = 0; i < np_local_poly; i++)
				{
					if (num_p_poly[i] == (-1))
					{
						num_p_poly[i] = j;
						if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
							KeyProcess_poly[i] = me;
						break;
					}
				}
			}
		}
	}

	MPI_Barrier(MPI_COMM_WORLD);
}


// ouput field data of fluid and particles in tecplot format -- parallel version and sequential version
void data_deal_3D(int me)
{
	MPI_Status status;

	FILE *file;
	int i, j, k, kk1, x, y, z, x2, y2,z2;
	int i2, j2, ix, jy, NxA, NyA, NxB, NyB;
	char fname[200];
	double u_out[Nx4][Ny4][Nz4][3], p_out[Nx4][Ny4][Nz4], Par_out[np_local][7], Par_out_poly[np_local_poly][7];
	double Par_out2[np_local][7], Par_out_poly2[np_local_poly][7], Par_out3[np_local][7], Par_out_poly3[np_local_poly][7], Par_out4[np_local][7], Par_out_poly4[np_local_poly][7];

	int nproc;
	int msgtag;

	if (if_polydisperse == 1)
	{
		for (k = 0; k < np_local_poly; k++)
		{
			Par_out_poly[k][0] = -1.0;
            Par_out_poly2[k][0] = -1.0;
            Par_out_poly3[k][0] = -1.0;
            Par_out_poly4[k][0] = -1.0;

		}
	}
	else
	{
		for (k = 0; k < np_local; k++)
		{
			Par_out[k][0] = -1.0;
            Par_out2[k][0] = -1.0;
            Par_out3[k][0] = -1.0;
            Par_out4[k][0] = -1.0;
		}
	}
	

	if (me != 0)
	{
		msgtag = me;
		for (x = 0; x <= Nx3; x++)
		for (y = 0; y <= Ny3; y++)
		for (z = 0; z <= Nz3; z++)
		{
			u_out[x][y][z][0] = u[x][y][z][0];
			u_out[x][y][z][1] = u[x][y][z][1];
			u_out[x][y][z][2] = u[x][y][z][2];
			p_out[x][y][z] = p[x][y][z]/p0;
		}
		MPI_Send(&u_out[0][0][0][0], Nx4*Ny4*Nz4 * 3, MPI_DOUBLE, 0, msgtag, MPI_COMM_WORLD);
		MPI_Send(&p_out[0][0][0], Nx4*Ny4*Nz4, MPI_DOUBLE, 0, msgtag, MPI_COMM_WORLD);
		
		kk1 = 0;
		if (if_polydisperse == 1)
		{
			for (k = 0; k < np_local_poly; k++)  //���ݾֲ���ż���
			{
				if (num_p_poly[k] >= 0)
				{
					j = num_p_poly[k];
					if (KeyProcess_poly[k] == me)
						//if ((xc[j][0] > xx) && (xc[j][0] < xxn) && (xc[j][1] > yy) && (xc[j][1] < yyn))
					{
						Par_out_poly[kk1][0] = 1.0 * j;
						Par_out_poly2[kk1][0] = 1.0 * j;
                        Par_out_poly3[kk1][0] = 1.0 * j;
                        Par_out_poly4[kk1][0] = 1.0 * j;
						for (i = 0; i < 3; i++)
						{
							Par_out_poly[kk1][i + 1] = xc[j][i];
                            Par_out_poly2[kk1][i + 1] = xc[j][i];
                            Par_out_poly3[kk1][i + 1] = xc[j][i];
                            Par_out_poly4[kk1][i + 1] = xc[j][i];
							Par_out_poly[kk1][i + 4] = uc[j][i];
                            Par_out_poly2[kk1][i + 4] = wc[j][i];
                            Par_out_poly3[kk1][i + 4] = F_IBM[j][i];
                            Par_out_poly4[kk1][i + 4] = T_IBM[j][i];
						}
						kk1++;
					}
				}
			}
			MPI_Send(&Par_out_poly[0][0], np_local_poly * 7, MPI_DOUBLE, 0, (NPROC + msgtag), MPI_COMM_WORLD);
            MPI_Send(&Par_out_poly2[0][0], np_local_poly * 7, MPI_DOUBLE, 0, (2 * NPROC + msgtag), MPI_COMM_WORLD);
            MPI_Send(&Par_out_poly3[0][0], np_local_poly * 7, MPI_DOUBLE, 0, (3 * NPROC + msgtag), MPI_COMM_WORLD);
            MPI_Send(&Par_out_poly4[0][0], np_local_poly * 7, MPI_DOUBLE, 0, (4 * NPROC + msgtag), MPI_COMM_WORLD);
		}
		else
		{
			for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					if (KeyProcess[k] == me)
						//if ((xc[j][0] > xx) && (xc[j][0] < xxn) && (xc[j][1] > yy) && (xc[j][1] < yyn))
					{
						Par_out[kk1][0] = 1.0 * j;
                        Par_out2[kk1][0] = 1.0 * j;
                        Par_out3[kk1][0] = 1.0 * j;
                        Par_out4[kk1][0] = 1.0 * j;
						for (i = 0; i < 3; i++)
						{
							Par_out[kk1][i + 1] = xc[j][i];
                            Par_out2[kk1][i + 1] = xc[j][i];
                            Par_out3[kk1][i + 1] = xc[j][i];
                            Par_out4[kk1][i + 1] = xc[j][i];
							Par_out[kk1][i + 4] = uc[j][i];
                            Par_out2[kk1][i + 4] = wc[j][i];
                            Par_out3[kk1][i + 4] = F_IBM[j][i];
                            Par_out4[kk1][i + 4] = T_IBM[j][i];
						}
						kk1++;
					}
				}
			}
			MPI_Send(&Par_out[0][0], np_local * 7, MPI_DOUBLE, 0, (NPROC + msgtag), MPI_COMM_WORLD);
            MPI_Send(&Par_out2[0][0], np_local * 7, MPI_DOUBLE, 0, (2 * NPROC + msgtag), MPI_COMM_WORLD);
			MPI_Send(&Par_out3[0][0], np_local * 7, MPI_DOUBLE, 0, (3 * NPROC + msgtag), MPI_COMM_WORLD);
            MPI_Send(&Par_out4[0][0], np_local * 7, MPI_DOUBLE, 0, (4 * NPROC + msgtag), MPI_COMM_WORLD);
		}
	}

	if (me == 0)
	{
		sprintf(fname, "%sflui_particle_data_3D%06d.dat", Data_folder, step);//For output with nginx
		file = fopen(fname, "w");

		fprintf(file, "TITLE=Sedimentation\n");
		fprintf(file, "VARIABLES=x,y,z,u,v,w,p\n");
		fprintf(file, "Zone I= %d,J= %d,K= %d,F= POINT\n", Nz1, Ny1, Nx1);

		for (x = 1; x <= Nx1; x++)
		for (y = 1; y <= Ny1; y++)
		for (z = 1; z <= Nz1; z++)
			fprintf(file, "%d\t%d\t%d\t%f\t%f\t%f\t%f\n", (x-1), (y-1), (z-1), u[x][y][z][0], u[x][y][z][1], u[x][y][z][2], p[x][y][z]/p0);

		for (nproc = 1; nproc < NPROC; nproc++)
		{
			msgtag = nproc;

			MPI_Recv(&u_out[0][0][0][0], Nx4*Ny4*Nz4 * 3, MPI_DOUBLE, nproc, msgtag, MPI_COMM_WORLD, &status);
			MPI_Recv(&p_out[0][0][0], Nx4*Ny4*Nz4, MPI_DOUBLE, nproc, msgtag, MPI_COMM_WORLD, &status);
			
			if (nproc % NPROCx == 0)  //����ߺ�
			{
				ix = 1; NxB = Nx1;
			}
			else
			{
				ix = 1;  NxB = Nx1;
			}

			if (nproc < NPROCx)
			{
				jy = 1; NyB = Ny1;
			}
			else
			{
				jy = 1; NyB = Ny1;
			}

			fprintf(file, "Zone I= %d,J= %d,K= %d,F= POINT\n", Nz1, (NyB-jy+1), (NxB-ix+1));
			for (x = ix; x <= NxB; x++)
			for (y = jy; y <= NyB; y++)
			for (z = 1; z <= Nz1; z++)
			{
				x2 = x + (nproc - (int)(nproc / NPROCx)*NPROCx)*Nx;
				y2 = y + (int)(nproc / NPROCx)*Ny;				
				fprintf(file, "%d\t%d\t%d\t%f\t%f\t%f\t%f\n", (x2-1), (y2-1), (z-1), u_out[x][y][z][0], u_out[x][y][z][1], u_out[x][y][z][2], p_out[x][y][z]);
			}
		}
		
		// u, position of particles
		fprintf(file, "Zone I= %d,F= POINT\n", np);
		if (if_polydisperse == 1)
		{
			for (k = 0; k < np_local_poly; k++)  //
			{
				if (num_p_poly[k] >= 0)
				{
					j = num_p_poly[k];
					if (KeyProcess_poly[k] == me)
						//if ((xc[j][0] > xx) && (xc[j][0] < xxn) && (xc[j][1] > yy) && (xc[j][1] < yyn))
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n", xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], R_poly[rc_poly[j]]);
				}
			}
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out_poly[0][0], np_local_poly * 7, MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local_poly; k++)
				{
					//Receive frome others, me is 0
					if (Par_out_poly[k][0] >= 0)
					{
						j = (int)(Par_out_poly[k][0]);
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n", Par_out_poly[k][1], Par_out_poly[k][2], Par_out_poly[k][3], Par_out_poly[k][4], Par_out_poly[k][5], Par_out_poly[k][6], R_poly[rc_poly[j]]);
					}
				}
			}
		}
		else
		{
			for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					if (KeyProcess[k] == me)
						//if ((xc[j][0] > xx) && (xc[j][0] < xxn) && (xc[j][1] > yy) && (xc[j][1] < yyn))
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n", xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], 1);
				}
			}

			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out[0][0], np_local * 7, MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local; k++)
				{
					//Receive frome others, me is 0
					if (Par_out[k][0] >= 0)
					{
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n", Par_out[k][1], Par_out[k][2], Par_out[k][3], Par_out[k][4], Par_out[k][5], Par_out[k][6], 1);
					}
				}
			}
		}

		// w, position of particles
		fprintf(file, "Zone I= %d,F= POINT\n", np);
		if (if_polydisperse == 1)
		{
			for (k = 0; k < np_local_poly; k++)  //
			{
				if (num_p_poly[k] >= 0)
				{
					j = num_p_poly[k];
					if (KeyProcess_poly[k] == me)
						//if ((xc[j][0] > xx) && (xc[j][0] < xxn) && (xc[j][1] > yy) && (xc[j][1] < yyn))
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n", xc[j][0], xc[j][1], xc[j][2], wc[j][0], wc[j][1], wc[j][2], R_poly[rc_poly[j]]);
				}
			}
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out_poly2[0][0], np_local_poly * 7, MPI_DOUBLE, nproc, (NPROC * 2 + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local_poly; k++)
				{
					//Receive frome others, me is 0
					if (Par_out_poly2[k][0] >= 0)
					{
						j = (int)(Par_out_poly2[k][0]);
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n", Par_out_poly2[k][1], Par_out_poly2[k][2], Par_out_poly2[k][3], Par_out_poly2[k][4], Par_out_poly2[k][5], Par_out_poly2[k][6], R_poly[rc_poly[j]]);
					}
				}
			}
		}
		else
		{
			for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					if (KeyProcess[k] == me)
						//if ((xc[j][0] > xx) && (xc[j][0] < xxn) && (xc[j][1] > yy) && (xc[j][1] < yyn))
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n", xc[j][0], xc[j][1], xc[j][2], wc[j][0], wc[j][1], wc[j][2], 1);
				}
			}
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out2[0][0], np_local * 7, MPI_DOUBLE, nproc, (NPROC * 2 + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local; k++)
				{
					//Receive frome others, me is 0
					if (Par_out2[k][0] >= 0)
					{
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n", Par_out2[k][1], Par_out2[k][2], Par_out2[k][3], Par_out2[k][4], Par_out2[k][5], Par_out2[k][6], 1);
					}
				}
			}
		}
		// F_IBM, position of particles
		fprintf(file, "Zone I= %d,F= POINT\n", np);
		if (if_polydisperse == 1)
		{
			for (k = 0; k < np_local_poly; k++)
			{
				if (num_p_poly[k] >= 0)
				{
					j = num_p_poly[k];
					if (KeyProcess_poly[k] == me)
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n",xc[j][0], xc[j][1], xc[j][2],F_IBM[j][0], F_IBM[j][1], F_IBM[j][2],R_poly[rc_poly[j]]);
				}
			}
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out_poly3[0][0], np_local_poly * 7, MPI_DOUBLE,nproc, (3 * NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local_poly; k++)
				{
					if (Par_out_poly3[k][0] >= 0)
					{
						j = (int)(Par_out_poly3[k][0]);
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n",Par_out_poly3[k][1], Par_out_poly3[k][2], Par_out_poly3[k][3],Par_out_poly3[k][4], Par_out_poly3[k][5], Par_out_poly3[k][6],R_poly[rc_poly[j]]);
					}
				}
			}
		}
		else
		{
			for (k = 0; k < np_local; k++)
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					if (KeyProcess[k] == me)
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n",xc[j][0], xc[j][1], xc[j][2],F_IBM[j][0], F_IBM[j][1], F_IBM[j][2], 1);
				}
			}
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out3[0][0], np_local * 7, MPI_DOUBLE, nproc, (3 * NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local; k++)
				{
					if (Par_out3[k][0] >= 0)
					{
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n",Par_out3[k][1], Par_out3[k][2], Par_out3[k][3],Par_out3[k][4], Par_out3[k][5], Par_out3[k][6], 1);
					}
				}
			}
		}

		// T_IBM, position of particles
		fprintf(file, "Zone I= %d,F= POINT\n", np);
		if (if_polydisperse == 1)
		{
			for (k = 0; k < np_local_poly; k++)
			{
				if (num_p_poly[k] >= 0)
				{
					j = num_p_poly[k];
					if (KeyProcess_poly[k] == me)
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n",xc[j][0], xc[j][1], xc[j][2],T_IBM[j][0], T_IBM[j][1], T_IBM[j][2],R_poly[rc_poly[j]]);
				}
			}
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out_poly4[0][0], np_local_poly * 7, MPI_DOUBLE,nproc, (4 * NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local_poly; k++)
				{
					if (Par_out_poly4[k][0] >= 0)
					{
						j = (int)(Par_out_poly4[k][0]);
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n",Par_out_poly4[k][1], Par_out_poly4[k][2], Par_out_poly4[k][3],Par_out_poly4[k][4], Par_out_poly4[k][5], Par_out_poly4[k][6],R_poly[rc_poly[j]]);
					}
				}
			}
		}
		else
		{
			for (k = 0; k < np_local; k++)
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					if (KeyProcess[k] == me)
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n",xc[j][0], xc[j][1], xc[j][2],T_IBM[j][0], T_IBM[j][1], T_IBM[j][2], 1);
				}
			}
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out4[0][0], np_local * 7, MPI_DOUBLE,nproc, (4 * NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local; k++)
				{
					if (Par_out4[k][0] >= 0)
					{
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%d\n",Par_out4[k][1], Par_out4[k][2], Par_out4[k][3],Par_out4[k][4], Par_out4[k][5], Par_out4[k][6], 1);
					}
				}
			}
		}

		fclose(file);
	}
}

void data_deal_3D_with_forcef(int me)
{
	MPI_Status status;

	FILE* file;
	int i, j, k, kk1, x, y, z, x2, y2, z2;
	int i2, j2, ix, jy, NxA, NyA, NxB, NyB;
	char fname[200];
	double u_out[Nx4][Ny4][Nz4][3], forcef_out[Nx4][Ny4][Nz4][3], p_out[Nx4][Ny4][Nz4];
	double Par_out_vel[np_local][10], Par_out_force[np_local][10]; // [0]:id, [1-3]:pos, [4-6]:vel/force, [7-9]:omega/torque
	double Par_out_vel_poly[np_local_poly][10], Par_out_force_poly[np_local_poly][10];

	int nproc;
	int msgtag;

	if (if_polydisperse == 1)
	{
		for (k = 0; k < np_local_poly; k++)
		{
			Par_out_vel_poly[k][0] = -1.0;
			Par_out_force_poly[k][0] = -1.0;
		}
	}
	else
	{
		for (k = 0; k < np_local; k++)
		{
			Par_out_vel[k][0] = -1.0;
			Par_out_force[k][0] = -1.0;
		}
	}

	if (me != 0)
	{
		msgtag = me;

		for (x = 0; x <= Nx3; x++)
			for (y = 0; y <= Ny3; y++)
				for (z = 0; z <= Nz3; z++)
				{
					u_out[x][y][z][0] = u[x][y][z][0];
					u_out[x][y][z][1] = u[x][y][z][1];
					u_out[x][y][z][2] = u[x][y][z][2];
					forcef_out[x][y][z][0] = forcef[x][y][z][0]; 
					forcef_out[x][y][z][1] = forcef[x][y][z][1];
					forcef_out[x][y][z][2] = forcef[x][y][z][2];
					p_out[x][y][z] = p[x][y][z] / p0;
				}

		MPI_Send(&u_out[0][0][0][0], Nx4 * Ny4 * Nz4 * 3, MPI_DOUBLE, 0, msgtag, MPI_COMM_WORLD);
		MPI_Send(&forcef_out[0][0][0][0], Nx4 * Ny4 * Nz4 * 3, MPI_DOUBLE, 0, msgtag + 1, MPI_COMM_WORLD);
		MPI_Send(&p_out[0][0][0], Nx4 * Ny4 * Nz4, MPI_DOUBLE, 0, msgtag + 2, MPI_COMM_WORLD);

		kk1 = 0;
		if (if_polydisperse == 1)
		{
			for (k = 0; k < np_local_poly; k++)
			{
				if (num_p_poly[k] >= 0)
				{
					j = num_p_poly[k];
					if (KeyProcess_poly[k] == me)
					{
						Par_out_vel_poly[kk1][0] = 1.0 * j;
						Par_out_force_poly[kk1][0] = 1.0 * j;

						for (i = 0; i < 3; i++)
						{
							Par_out_vel_poly[kk1][i + 1] = xc[j][i];      // pos
							Par_out_vel_poly[kk1][i + 4] = uc[j][i];      // vel
							Par_out_vel_poly[kk1][i + 7] = wc[j][i];      // omega

							Par_out_force_poly[kk1][i + 1] = xc[j][i];    // pos
							Par_out_force_poly[kk1][i + 4] = F_IBM[j][i]; // force
							Par_out_force_poly[kk1][i + 7] = T_IBM[j][i]; // torque
						}
						kk1++;
					}
				}
			}
			MPI_Send(&Par_out_vel_poly[0][0], np_local_poly * 10, MPI_DOUBLE, 0, (NPROC + msgtag), MPI_COMM_WORLD);
			MPI_Send(&Par_out_force_poly[0][0], np_local_poly * 10, MPI_DOUBLE, 0, (2 * NPROC + msgtag), MPI_COMM_WORLD);
		}
		else
		{
			for (k = 0; k < np_local; k++)
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					if (KeyProcess[k] == me)
					{
						Par_out_vel[kk1][0] = 1.0 * j;
						Par_out_force[kk1][0] = 1.0 * j;

						for (i = 0; i < 3; i++)
						{
							Par_out_vel[kk1][i + 1] = xc[j][i];
							Par_out_vel[kk1][i + 4] = uc[j][i];
							Par_out_vel[kk1][i + 7] = wc[j][i];

							Par_out_force[kk1][i + 1] = xc[j][i];
							Par_out_force[kk1][i + 4] = F_IBM[j][i];
							Par_out_force[kk1][i + 7] = T_IBM[j][i];
						}
						kk1++;
					}
				}
			}
			MPI_Send(&Par_out_vel[0][0], np_local * 10, MPI_DOUBLE, 0, (NPROC + msgtag), MPI_COMM_WORLD);
			MPI_Send(&Par_out_force[0][0], np_local * 10, MPI_DOUBLE, 0, (2 * NPROC + msgtag), MPI_COMM_WORLD);
		}
	}

	if (me == 0)
	{
		sprintf(fname, "%sflui_particle_data_with_forcef_3D%06d.dat", Data_folder, step);
		file = fopen(fname, "w");

		fprintf(file, "TITLE=Sedimentation\n");
		fprintf(file, "VARIABLES=x,y,z,u,v,w,fx,fy,fz,p\n");
		fprintf(file, "Zone I= %d,J= %d,K= %d,F= POINT\n", Nz4, Ny2, Nx2);

		for (x = 0; x <= Nx1; x++)
			for (y = 0; y <= Ny1; y++)
				for (z = 0; z <= Nz3; z++)
					fprintf(file, "%d\t%d\t%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n",
						(x - 1), (y - 1), (z - 1),
						u[x][y][z][0], u[x][y][z][1], u[x][y][z][2],
						forcef[x][y][z][0], forcef[x][y][z][1], forcef[x][y][z][2],
						p[x][y][z] / p0);

		for (nproc = 1; nproc < NPROC; nproc++)
		{
			msgtag = nproc;

			MPI_Recv(&u_out[0][0][0][0], Nx4 * Ny4 * Nz4 * 3, MPI_DOUBLE, nproc, msgtag, MPI_COMM_WORLD, &status);
			MPI_Recv(&forcef_out[0][0][0][0], Nx4 * Ny4 * Nz4 * 3, MPI_DOUBLE, nproc, msgtag + 1, MPI_COMM_WORLD, &status);
			MPI_Recv(&p_out[0][0][0], Nx4 * Ny4 * Nz4, MPI_DOUBLE, nproc, msgtag + 2, MPI_COMM_WORLD, &status);

			if (nproc % NPROCx == 0) { ix = 0; NxB = Nx2; }
			else { ix = 1; NxB = Nx2; }

			if ((nproc + 1) % NPROCx == 0) { ix = 1; NxB = Nx3; }
			else { ix = 1; NxB = Nx2; }

			if (nproc < NPROCx) { jy = 0; NyB = Ny2; }
			else { jy = 1; NyB = Ny2; }

			if (nproc >= (NPROCx * (NPROCy - 1))) { jy = 1; NyB = Ny3; }
			else { jy = 1; NyB = Ny2; }

			fprintf(file, "Zone I= %d,J= %d,K= %d,F= POINT\n", Nz4, (NyB - jy + 1), (NxB - ix + 1));
			for (x = ix; x <= NxB; x++)
				for (y = jy; y <= NyB; y++)
					for (z = 0; z <= Nz3; z++)
					{
						x2 = x + (nproc - (int)(nproc / NPROCx) * NPROCx) * Nx;
						y2 = y + (int)(nproc / NPROCx) * Ny;
						fprintf(file, "%d\t%d\t%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n",
							(x2 - 1), (y2 - 1), (z - 1),
							u_out[x][y][z][0], u_out[x][y][z][1], u_out[x][y][z][2],
							forcef_out[x][y][z][0], forcef_out[x][y][z][1], forcef_out[x][y][z][2],
							p_out[x][y][z]);
					}
		}

		fprintf(file, "Zone I= %d,F= POINT\n", np);
		if (if_polydisperse == 1)
		{
			for (k = 0; k < np_local_poly; k++)
			{
				if (num_p_poly[k] >= 0)
				{
					j = num_p_poly[k];
					if (KeyProcess_poly[k] == me)
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\n",
							xc[j][0], xc[j][1], xc[j][2],
							uc[j][0], uc[j][1], uc[j][2],
							wc[j][0], wc[j][1], wc[j][2],
							R_poly[rc_poly[j]]);
				}
			}
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out_vel_poly[0][0], np_local_poly * 10, MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local_poly; k++)
				{
					if (Par_out_vel_poly[k][0] >= 0)
					{
						j = (int)(Par_out_vel_poly[k][0]);
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\n",
							Par_out_vel_poly[k][1], Par_out_vel_poly[k][2], Par_out_vel_poly[k][3],
							Par_out_vel_poly[k][4], Par_out_vel_poly[k][5], Par_out_vel_poly[k][6],
							Par_out_vel_poly[k][7], Par_out_vel_poly[k][8], Par_out_vel_poly[k][9],
							R_poly[rc_poly[j]]);
					}
				}
			}
		}
		else
		{
			for (k = 0; k < np_local; k++)
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					if (KeyProcess[k] == me)
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\n",
							xc[j][0], xc[j][1], xc[j][2],
							uc[j][0], uc[j][1], uc[j][2],
							wc[j][0], wc[j][1], wc[j][2], R);
				}
			}
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out_vel[0][0], np_local * 10, MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local; k++)
				{
					if (Par_out_vel[k][0] >= 0)
					{
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\n",
							Par_out_vel[k][1], Par_out_vel[k][2], Par_out_vel[k][3],
							Par_out_vel[k][4], Par_out_vel[k][5], Par_out_vel[k][6],
							Par_out_vel[k][7], Par_out_vel[k][8], Par_out_vel[k][9], R);
					}
				}
			}
		}

		fprintf(file, "Zone I= %d,F= POINT\n", np);
		if (if_polydisperse == 1)
		{
			for (k = 0; k < np_local_poly; k++)
			{
				if (num_p_poly[k] >= 0)
				{
					j = num_p_poly[k];
					if (KeyProcess_poly[k] == me)
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\n",
							xc[j][0], xc[j][1], xc[j][2],
							F_IBM[j][0], F_IBM[j][1], F_IBM[j][2],
							T_IBM[j][0], T_IBM[j][1], T_IBM[j][2],
							R_poly[rc_poly[j]]);
				}
			}
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out_force_poly[0][0], np_local_poly * 10, MPI_DOUBLE, nproc, (2 * NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local_poly; k++)
				{
					if (Par_out_force_poly[k][0] >= 0)
					{
						j = (int)(Par_out_force_poly[k][0]);
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\n",
							Par_out_force_poly[k][1], Par_out_force_poly[k][2], Par_out_force_poly[k][3],
							Par_out_force_poly[k][4], Par_out_force_poly[k][5], Par_out_force_poly[k][6],
							Par_out_force_poly[k][7], Par_out_force_poly[k][8], Par_out_force_poly[k][9],
							R_poly[rc_poly[j]]);
					}
				}
			}
		}
		else
		{
			for (k = 0; k < np_local; k++)
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					if (KeyProcess[k] == me)
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\n",
							xc[j][0], xc[j][1], xc[j][2],
							F_IBM[j][0], F_IBM[j][1], F_IBM[j][2],
							T_IBM[j][0], T_IBM[j][1], T_IBM[j][2], R);
				}
			}
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out_force[0][0], np_local * 10, MPI_DOUBLE, nproc, (2 * NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local; k++)
				{
					if (Par_out_force[k][0] >= 0)
					{
						fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\n",
							Par_out_force[k][1], Par_out_force[k][2], Par_out_force[k][3],
							Par_out_force[k][4], Par_out_force[k][5], Par_out_force[k][6],
							Par_out_force[k][7], Par_out_force[k][8], Par_out_force[k][9], R);
					}
				}
			}
		}

		fclose(file);
	}
}

void data_par_prepare_output(int me)
{	
	MPI_Status status;
	FILE *file;

	int i, j, k, kk1;
	double Par_out[np_local][22], Par_out_poly[np_local_poly][22];
	int nproc;

	if (if_polydisperse == 1)
	{
		for (k = 0; k < np_local_poly; k++)
		{
			Par_out_poly[k][0] = -1.0;
		}

		if (me != 0)
		{
			kk1 = 0;
			for (k = 0; k < np_local_poly; k++)  //���ݾֲ���ż���
			{
				if (num_p_poly[k] >= 0)
				{
					j = num_p_poly[k];
					if (KeyProcess_poly[k] == me)
					{
						Par_out_poly[kk1][0] = 1.0 * j;
						for (i = 0; i < 3; i++)
						{
							Par_out_poly[kk1][i + 1] = xc[j][i];
							Par_out_poly[kk1][i + 4] = uc[j][i];
							Par_out_poly[kk1][i + 7] = wc[j][i];
							Par_out_poly[kk1][i + 10] = sitac[j][i];
							Par_out_poly[kk1][i + 13] = 1. * cross_p[j][i];
							Par_out_poly[kk1][i + 16] = F_IBM[j][i];
							Par_out_poly[kk1][i + 19] = T_IBM[j][i];
						}
						kk1++;
					}
				}
			}
			MPI_Send(&Par_out_poly[0][0], np_local_poly * 22, MPI_DOUBLE, 0, (NPROC + me), MPI_COMM_WORLD);
		}

		if (me == 0)
		{
			//�Ƚ����������ڿ�������
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out_poly[0][0], np_local_poly * 22, MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local_poly; k++)
				{
					//Receive frome others, me is 0
					if (Par_out_poly[k][0] >= 0)
					{
						j = (int)Par_out_poly[k][0];
						for (i = 0; i < 3; i++)
						{
							xc[j][i] = Par_out_poly[k][i + 1];
							uc[j][i] = Par_out_poly[k][i + 4];
							wc[j][i] = Par_out_poly[k][i + 7];
							sitac[j][i] = Par_out_poly[k][i + 10];
							cross_p[j][i] = (int)Par_out_poly[k][i + 13];
							F_IBM[j][i] = Par_out_poly[k][i + 16];
							T_IBM[j][i] = Par_out_poly[k][i + 19];
						}
					}
				}
			}
		}
	}
	else
	{
		for (k = 0; k < np_local; k++)
		{
			Par_out[k][0] = -1.0;
		}

		if (me != 0)
		{
			kk1 = 0;
			for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					if (KeyProcess[k] == me)
					{
						Par_out[kk1][0] = 1.0 * j;
						for (i = 0; i < 3; i++)
						{
							Par_out[kk1][i + 1] = xc[j][i];
							Par_out[kk1][i + 4] = uc[j][i];
							Par_out[kk1][i + 7] = wc[j][i];
							Par_out[kk1][i + 10] = sitac[j][i];
							Par_out[kk1][i + 13] = 1. * cross_p[j][i];
							Par_out[kk1][i + 16] = F_IBM[j][i];
							Par_out[kk1][i + 19] = T_IBM[j][i];
						}
						kk1++;
					}
				}
			}
			MPI_Send(&Par_out[0][0], np_local * 22, MPI_DOUBLE, 0, (NPROC + me), MPI_COMM_WORLD);
		}

		if (me == 0)
		{
			//�Ƚ����������ڿ�������
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out[0][0], np_local * 22, MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local; k++)
				{
					//Receive frome others, me is 0
					if (Par_out[k][0] >= 0)
					{
						j = (int)Par_out[k][0];
						for (i = 0; i < 3; i++)
						{
							xc[j][i] = Par_out[k][i + 1];
							uc[j][i] = Par_out[k][i + 4];
							wc[j][i] = Par_out[k][i + 7];
							sitac[j][i] = Par_out[k][i + 10];
							cross_p[j][i] = (int)Par_out[k][i + 13];
							F_IBM[j][i] = Par_out[k][i + 16];
							T_IBM[j][i] = Par_out[k][i + 19];
						}
					}
				}
			}
		}
	}
}

void data_par_contact_prepare_output(int me)
{
	MPI_Status status;
	FILE* file;

	int i, j, k, kk1;
	double Par_out[np_local][max_contact_num * 4 + 1], Par_out_poly[np_local_poly][max_contact_num * 4 + 1];
	int nproc;

	if (if_polydisperse == 1)
	{
		for (k = 0; k < np_local_poly; k++)
		{
			Par_out_poly[k][0] = -1.0;
		}

		if (me != 0)
		{
			kk1 = 0;
			for (k = 0; k < np_local_poly; k++)  //���ݾֲ���ż���
			{
				if (num_p_poly[k] >= 0)
				{
					j = num_p_poly[k];
					if (KeyProcess_poly[k] == me)
					{
						Par_out_poly[kk1][0] = 1.0 * j;
						for (i = 0; i < max_contact_num; i++)
						{
							Par_out_poly[kk1][i * 4 + 1] = tangential_contact_index[k][i]*1.0;
							Par_out_poly[kk1][i * 4 + 2] = tangential_overlap[k][i][0];
                            Par_out_poly[kk1][i * 4 + 3] = tangential_overlap[k][i][1];
                            Par_out_poly[kk1][i * 4 + 4] = tangential_overlap[k][i][2];
						}
						kk1++;
					}
				}
			}
			MPI_Send(&Par_out_poly[0][0], np_local_poly * (max_contact_num * 4 + 1), MPI_DOUBLE, 0, (NPROC + me), MPI_COMM_WORLD);
		}

		if (me == 0)
		{
			//�Ƚ����������ڿ�������
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out_poly[0][0], np_local_poly * (max_contact_num * 4 + 1), MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local_poly; k++)
				{
					//Receive frome others, me is 0
					if (Par_out_poly[k][0] >= 0)
					{
						j = (int)Par_out_poly[k][0];
						for (i = 0; i < max_contact_num; i++)
						{
                            tangential_contact_index[j][i] = (int)Par_out_poly[k][i * 4 + 1];
                            tangential_overlap[j][i][0] = Par_out_poly[k][i * 4 + 2];
                            tangential_overlap[j][i][1] = Par_out_poly[k][i * 4 + 3];
                            tangential_overlap[j][i][2] = Par_out_poly[k][i * 4 + 4];
						}
					}
				}
			}
		}
	}
	else
	{
		for (k = 0; k < np_local; k++)
		{
			Par_out[k][0] = -1.0;
		}

		if (me != 0)
		{
			kk1 = 0;
			for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					if (KeyProcess[k] == me)
					{
						Par_out[kk1][0] = 1.0 * j;
						for (i = 0; i < max_contact_num; i++)
						{
							Par_out[kk1][i * 4 + 1] = tangential_contact_index[k][i] * 1.0;
							Par_out[kk1][i * 4 + 2] = tangential_overlap[k][i][0];
							Par_out[kk1][i * 4 + 3] = tangential_overlap[k][i][1];
							Par_out[kk1][i * 4 + 4] = tangential_overlap[k][i][2];
						}
						kk1++;
					}
				}
			}
			MPI_Send(&Par_out[0][0], np_local * (max_contact_num * 4 + 1), MPI_DOUBLE, 0, (NPROC + me), MPI_COMM_WORLD);
		}

		if (me == 0)
		{
			//�Ƚ����������ڿ�������
			for (nproc = 1; nproc < NPROC; nproc++)
			{
				MPI_Recv(&Par_out[0][0], np_local * (max_contact_num * 4 + 1), MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
				for (k = 0; k < np_local; k++)
				{
					//Receive frome others, me is 0
					if (Par_out[k][0] >= 0)
					{
						j = (int)Par_out[k][0];
						for (i = 0; i < max_contact_num; i++)
						{
                            tangential_contact_index[k][i] = (int)Par_out[k][i * 4 + 1];
                            tangential_overlap[j][i][0] = Par_out[k][i * 4 + 2];
                            tangential_overlap[j][i][1] = Par_out[k][i * 4 + 3];
                            tangential_overlap[j][i][2] = Par_out[k][i * 4 + 4];
						}
					}
				}
			}
		}
	}
}

void data_deal_particle(int me)
{
	MPI_Status status;

	FILE *file;
	int i, j, k, kk1, x, y, z, dw_boun, temp;
	char fname[300];
	double n;
	int n1, n2, n3;
	int nproc;

	n = dx;
	n1 = (int)(Lx / D) + 1;
	n2 = (int)(Ly / D) + 1;
	n3 = (int)(Lz / D) + 1;
	double sita;
	dw_boun = (int)(2 * PI*R_boun / 5);
	double xb_boun[3];
	
	if (me == 0)
	{	
		sprintf(fname, "%sparticle_data%07d.dat", Data_folder, step);
		file = fopen(fname, "w");

		fprintf(file, "TITLE=particle_data\n");
		if (if_polydisperse == 1)
		{
			fprintf(file, "VARIABLES=x,y,z,ux,uy,uz,wx,wy,wz,FIBMx,FIBMy,FIBMz,TIBMx,TIBMy,TIBMz,n,R,rhop,crox,croy,croz,index\n");
			//fprintf(file, "Zone I= %d,J=22,F= POINT\n", np); // for paraview
			fprintf(file, "Zone I= %d,F= POINT\n", np);	//for tecplot
		}
		else
		{
			fprintf(file, "VARIABLES=x,y,z,ux,uy,uz,wx,wy,wz,FIBMx,FIBMy,FIBMz,TIBMx,TIBMy,TIBMz,n,D,crox,croy,croz\n");
			fprintf(file, "Zone I= %d,J=20,F= POINT\n", np);
		}
		//����������ڿ�������
		for (j = 0; j < np; j++)  //���ݾֲ���ż���
		{
			if (if_polydisperse == 1)
			{
				temp = rc_poly[j];

				fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\t%d\t%f\t%d\t%d\t%d\t%d\n", xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], wc[j][0], wc[j][1], wc[j][2], \
					F_IBM[j][0], F_IBM[j][1], F_IBM[j][2], T_IBM[j][0], T_IBM[j][1], T_IBM[j][2], (j + 1), (R_poly[temp]), rhoc_poly[j], cross_p[j][0], cross_p[j][1], cross_p[j][2], rc_poly[j] );
			}
			else
			{
				fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\t%d\t%d\t%d\t%d\n", xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], wc[j][0], wc[j][1], wc[j][2], \
					F_IBM[j][0], F_IBM[j][1], F_IBM[j][2], T_IBM[j][0], T_IBM[j][1], T_IBM[j][2], (j + 1), 1, cross_p[j][0], cross_p[j][1], cross_p[j][2]);
			}
		}
		
		//frame
		fprintf(file, "Zone I= %d,J= %d,K= %d,F= POINT\n", 2, 2, 2);
		for (x = 0; x <= NxT; x = x + NxT)
		for (y = 0; y <= NyT; y = y + NyT)
		for (z = 0; z <= NzT; z = z + NzT)
			fprintf(file, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", x, y, z, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0);

		//frame		
		/*
		fprintf(file, "Zone I= %d,J= %d,F= POINT \n", dw_boun+1, 2);
		for (i = 0; i <= NxT; i += NxT)
		for (j = 0; j <= dw_boun; j++)
		{
		sita = j * 2 * PI / dw_boun;
		xb_boun[0] = i;
		xb_boun[1] = 0.5*Ny + R_boun*cos(sita);
		xb_boun[2] = 0.5*Nz + R_boun*sin(sita);
		fprintf(file, "%.2f\t%.2f\t%.2f\t%d\t%d\t%d\t%d\t%d\n", xb_boun[0] * n, xb_boun[1] * n, xb_boun[2] * n, 0, 0, 0, (j + 1), 1);
		}
		*/
		fclose(file);
	}
}

void data_deal_particle_contact(int me)
{
	MPI_Status status;

	FILE* file;
	int i, j;
	char fname[300];
	int nproc;

	double xb_boun[3];

	if (me == 0)
	{
		sprintf(fname, "%sparticle_data_contact%07d.dat", Data_folder, step);
		file = fopen(fname, "w");

		fprintf(file, "TITLE=particle_data_contact\n");
		{
			fprintf(file, "VARIABLES=");
			for (i = 0; i < max_contact_num; i++)
			{
				if (i < max_contact_num - 1)
				{
					fprintf(file, "index%d,overlap%d_0,overlap%d_1,overlap%d_2,",i,i,i,i);
				}
				else
				{
                    fprintf(file, "index%d,overlap%d_0,overlap%d_1,overlap%d_2\n",i,i,i,i);
				}
			}
			fprintf(file, "Zone I= %d ,J= %d ,F= POINT\n", np, max_contact_num);
		}
		//����������ڿ�������
		for (j = 0; j < np; j++)  //���ݾֲ���ż���
		{
			fprintf(file, "%d\t", j);
			for (i = 0; i < max_contact_num; i++)
			{
				if (i < max_contact_num - 1)
				{
					fprintf(file, "%d\t%f\t%f\t%f\t", tangential_contact_index[j][i], tangential_overlap[j][i][0], tangential_overlap[j][i][1], tangential_overlap[j][i][2]);
				}
				else
				{
					fprintf(file, "%d\t%f\t%f\t%f\n", tangential_contact_index[j][i], tangential_overlap[j][i][0], tangential_overlap[j][i][1], tangential_overlap[j][i][2]);
				}
			}
		}

		fclose(file);
	}
}

void data_particle_pro(int me)
{
	MPI_Status status;

	FILE *file;
	int i, j, k, x, y, z;
	char fname[30];
	
	if (me == 0)
	{
		//����������ݹ�����
		if ((file = fopen("particle_data_pro.dat", "a")) == NULL)
		{
			printf("can not open the file!");
			exit(0);
		}
		//����������ڿ�������
		for (j = 0; j < np; j++) 
		{
			fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\t%d\t%d\n", xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], \
				wc[j][0], wc[j][1], wc[j][2], cross_p[j][0], cross_p[j][1], cross_p[j][2]);
		}
		fclose(file);
	}
}

/*
void data_deal_particle(int me)
{
	MPI_Status status;

	FILE *file;
	int i, j, k, kk1, x, y, z, dw_boun;
	char fname[20];
	double n;
	int n1, n2, n3;
	int nproc;

	n = dx;
	n1 = (int)(Lx / D) + 1;
	n2 = (int)(Ly / D) + 1;
	n3 = (int)(Lz / D) + 1;
	double sita;
	dw_boun = (int)(2 * PI*R_boun / 5);
	double xb_boun[3];

	double Par_out[np_local][19];
	for (k = 0; k < np_local; k++)
	{
		Par_out[k][0] = -1.0;
	}

	if (me != 0)
	{
		kk1 = 0;
		for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
		{
			if (num_p[k] >= 0)
			{
				j = num_p[k];
				//if (KeyProcess[k] == me)
				if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
				{
					Par_out[kk1][0] = 1.0*j;
					for (i = 0; i < 3; i++)
					{
						Par_out[kk1][i + 1] = xc[j][i];
						Par_out[kk1][i + 4] = uc[j][i];
						Par_out[kk1][i + 7] = ac[j][i];
						Par_out[kk1][i + 10] = wc[j][i];
						Par_out[kk1][i + 13] = sitac[j][i];
						Par_out[kk1][i + 16] = 1.*cross_p[j][i];
					}
					kk1++;
				}
			}
		}
		MPI_Send(&Par_out[0][0], np_local * 19, MPI_DOUBLE, 0, (NPROC + me), MPI_COMM_WORLD);
	}

	if (me == 0)
	{
		sprintf(fname, "particle_data%06d.dat", step);
		file = fopen(fname, "w");

		fprintf(file, "TITLE=particle_data\n");
		fprintf(file, "VARIABLES=x,y,z,ux,uy,uz,wx,wy,wz,n,D,flag,crox,croy,croz\n");
		fprintf(file, "Zone I= %d,F= POINT\n", np);
		//����������ڿ�������
		for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
		{
			if (num_p[k] >= 0)
			{
				j = num_p[k];
				//if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn) && (xc[j][2] >= 0) && (xc[j][2] < NzT))
				if (KeyProcess[k] == me)
					fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\t%d\t%d\t%d\t%d\t%d\n", xc[j][0], xc[j][1], xc[j][2], uc[j][0], uc[j][1], uc[j][2], wc[j][0], wc[j][1], wc[j][2], (j + 1), \
					1, flag_p[j], cross_p[j][0], cross_p[j][1], cross_p[j][2]);
				//fprintf(file, "%d\t%f\t%f\t%f\t%f\t%f\t%f\t%d\t%d\t%d\n", me, xc[j][0] * n, xc[j][1] * n, xc[j][2] * n, uc[j][0], uc[j][1], uc[j][2], (j + 1), 1, flag_p[j]);

			}
		}
		//������������ڿ�������
		for (nproc = 1; nproc < NPROC; nproc++)
		{
			MPI_Recv(&Par_out[0][0], np_local * 19, MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
			for (k = 0; k < np_local; k++)
			{
				//Receive frome others, me is 0
				if (Par_out[k][0] >= 0)
				{
					j = (int)Par_out[k][0];
					fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%d\t%d\t%d\t%d\t%d\t%d\n", Par_out[k][1], Par_out[k][2], Par_out[k][3], Par_out[k][4], Par_out[k][5], Par_out[k][6], \
						Par_out[k][10], Par_out[k][11], Par_out[k][12], (j + 1), 1, flag_p[j], (int)Par_out[k][16], (int)Par_out[k][17], (int)Par_out[k][18]);
					//fprintf(file, "%d\t%f\t%f\t%f\t%f\t%f\t%f\t%d\t%d\t%d\n", nproc, Par_out[k][1] * n, Par_out[k][2] * n, Par_out[k][3] * n, Par_out[k][4], Par_out[k][5], Par_out[k][6], (j + 1), 1, flag_p[j]);
				}
			}
		}

		//frame
		fprintf(file, "Zone I= %d,J= %d,K= %d,F= POINT\n", 2, 2, 2);
		for (x = 0; x <= NxT; x = x + NxT)
		for (y = 0; y <= NyT; y = y + NyT)
		for (z = 0; z <= NzT; z = z + NzT)
			fprintf(file, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", x, y, z, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0);

		
		fclose(file);
	}
}
*/
// output particles' position and velocities in tecplot format -- serial version
void FIB_particle(int me)
{
	MPI_Status status;

	FILE *file;
	int i,j, k,kk1,x, y, z;
	char fname[20];
	int nproc;
	double temp;

	double Par_out[np_local][4];
	for (k = 0; k < np_local; k++)
	{
		Par_out[k][0] = -1.0;
	}

	if (me != 0)
	{
		kk1 = 0;
		for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
		{
			if (num_p[k] >= 0)
			{
				j = num_p[k];
				//if (KeyProcess[k] == me)
				if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
				{
					Par_out[kk1][0] = 1.0*j;
					for (i = 0; i < 3; i++)
					{
						Par_out[kk1][i + 1] = F_IBM[j][i];
					}
					kk1++;
				}
			}
		}
		MPI_Send(&Par_out[0][0], np_local * 4, MPI_DOUBLE, 0, (NPROC + me), MPI_COMM_WORLD);
	}

	if (me == 0)
	{
		if ((file = fopen("par_drag.dat", "a")) == NULL)
		{
			printf("can not open the file!");
			exit(0);
		}
		
		//����������ڿ�������
		for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
		{
			if (num_p[k] >= 0)
			{
				j = num_p[k];
				if (KeyProcess[k] == me)
					fprintf(file, "%f\t%f\t%f\t%d\n", F_IBM[j][0], F_IBM[j][1], F_IBM[j][2], (j + 1));
			}
		}
		//������������ڿ�������
		for (nproc = 1; nproc < NPROC; nproc++)
		{
			MPI_Recv(&Par_out[0][0], np_local * 4, MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
			for (k = 0; k < np_local; k++)
			{
				//Receive frome others, me is 0
				if (Par_out[k][0] >= 0)
				{
					j = (int)Par_out[k][0];
					for (i = 0; i < 3; i++)
					{
						F_IBM[j][i] = Par_out[k][i + 1];
					}
					fprintf(file, "%f\t%f\t%f\t%d\n", Par_out[k][1], Par_out[k][2], Par_out[k][3], (j + 1));
				}
			}
		}		
		//temp = 6 * PI*mv*D / 2;
		//fprintf(file, "%f\t%f\t%f\n", step*t_stokes, F_IBM[0][0]/temp, F_IBM[1][0]/temp);

		fclose(file);
	}
}

void write_check_fluid(int me)
// update field data of fluid -- parallel version
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
	fwrite(&t, sizeof(double), 1, file);
	fwrite(u, sizeof(u), 1, file);
	fwrite(p, sizeof(p), 1, file);

	fclose(file);
}

void write_check_particle(int me)
// update particles' position and velocities -- parallel version
{
	FILE *file;
	int j;
	char fname[20];

	sprintf(fname, "check_particle%03d", me);
	if ((file = fopen(fname, "wb")) == NULL)
	{
		printf("can not open the file!");
		exit(0);
	}
	fwrite(xc, sizeof(xc), 1, file);
	fwrite(uc, sizeof(uc), 1, file);
	//fwrite(Crossx, sizeof(Crossx), 1, file);
	//fwrite(Crossy, sizeof(Crossy), 1, file);
	fclose(file);
}

void read_check_fluid(int me)
// update field data of fluid -- parallel version
{
	FILE *file;
	char fname[20];

	int nproc;
	int msgtag;
	int x, y, z, x2, NxB;
	MPI_Status status;	
		
	// fluid's u,rho
	sprintf(fname, "check_fluid%03d", me);
	if ((file = fopen(fname, "rb")) == NULL)
	{
		if (me == 0) printf("Simulation from new initial fluid field!\n");
		step0 = 1;
		t0 = 0.;		
		for (nproc = 0; nproc<NPROC; nproc++)
		{
			if (me == nproc)
			{
				for (x = 0; x <= Nx3; x++)
				for (y = 0; y <= Ny3; y++)
				for (z = 0; z <= Nz3; z++)
				{
					//x2 = x + me*Nx;
					u[x][y][z][0] = 0.0;
					u[x][y][z][1] = 0.0;
					u[x][y][z][2] = 0.0;
					p[x][y][z] = rhof * (cs * cs);
					//p[x][y][z] = rhof * (cs*cs) + (0.5*dp - 1.0*x2 / NxT*dp);
				}
			}
		}
	}
	else
	{
		fread(&step0, sizeof(int), 1, file);
		fread(&t0, sizeof(double), 1, file);
		fread(u, sizeof(u), 1, file);
		fread(p, sizeof(p), 1, file);
		fclose(file);
	}
}

void read_check_fluid2(int me)
// update field data of fluid -- parallel version
{
	FILE* file;
	char fname[20];

	int nproc;
	int msgtag;
	int x, y, z, x2, NxB;
	int n;
	int xmin, xmax, ymin, ymax, zmin, zmax;
	int dataxmin, dataymin, datazmin, dataxmax, dataymax, datazmax;
	int I, J, K, total_points;
	int x_coord, y_coord, z_coord;
	int x_index, y_index, z_index;
	double u_val, v_val, w_val, p_val;
	bool is_me_data_region;

	char str1[300];
	char line[300];

	int zone_count = 0;
int point_count = 0;	

	MPI_Status status;

	// fluid's u,rho
	sprintf(fname, "seed_fluid.dat");
	if ((file = fopen(fname, "rb")) == NULL)
	{
		if (me == 0) printf("Simulation from new initial fluid field!\n");
		step0 = 1;
		t0 = 0.;
		for (nproc = 0; nproc < NPROC; nproc++)
		{
			if (me == nproc)
			{
				for (x = 0; x <= Nx3; x++)
					for (y = 0; y <= Ny3; y++)
						for (z = 0; z <= Nz3; z++)
						{
							//x2 = x + me*Nx;
							u[x][y][z][0] = 0.0;
							u[x][y][z][1] = 0.0;
							u[x][y][z][2] = 0.0;
							p[x][y][z] = rhof * (cs * cs);
							//p[x][y][z] = rhof * (cs*cs) + (0.5*dp - 1.0*x2 / NxT*dp);
						}
			}
		}
	}
	else
	{
		//fread(&step0, sizeof(int), 1, file);
		//fread(&t0, sizeof(double), 1, file);
		//fread(u, sizeof(u), 1, file);
		//fread(p, sizeof(p), 1, file);
		//fclose(file);
		xmin = (me - (int)(me / NPROCx) * NPROCx) * Nx + 1;
		xmax = xmin + Nx - 1;
		ymin = (int)(me / NPROCx) * Ny + 1;
		ymax = ymin + Ny - 1;
		zmin = 1;
		zmax = Nz;
		printf("Process %d region is x from %d to %d, y from %d to %d, z from %d to %d\n", me, xmin, xmax, ymin, ymax, zmin, zmax);

		fgets(line, 300, file);  // TITLE行
		fgets(line, 300, file);  // VARIABLES行
		while (fgets(line, 256, file) != NULL)
		{
			// 解析Zone行
			if (strstr(line, "Zone") == NULL)
				continue;

			// 检查是否是流场数据（包含J和K）
			if (strstr(line, "J=") == NULL || strstr(line, "K=") == NULL)
			{
				// 没有J和K，是颗粒数据，结束读取
				break;
			}
			sscanf(line, "%s %s %d%s %d%s %d%s %s", str1, str1, &I, str1, &J, str1, &K, str1, str1);
			total_points = I * J * K;

			zone_count++;
			printf("Process %d: Reading fluid zone %d of %d (size: %d x %d x %d)\n",
				me, zone_count, NPROC, K - 1, J - 1, I - 1);
			if (( (Nx % (K - 1)) != 0) || ((Ny % (J - 1)) != 0) || (Nz != I - 1))
			{
                printf("Error: The size of fluid data is not consistent with the size of the simulation domain.\n");
				printf("I = %d, J = %d, K = %d but Nx = %d Ny = %d Nz = %d\n", I-1, J-1, K-1, Nx, Ny, Nz);
			}
			if (fgets(line, 256, file) == NULL)
				break;
			sscanf(line, "%d %d %d %lf %lf %lf %lf", &x_coord, &y_coord, &z_coord, &u_val, &v_val, &w_val, &p_val);
			dataxmin = x_coord + 1;
			dataymin = y_coord + 1;
            datazmin = z_coord + 1;
			dataxmax = x_coord + K - 2;
            dataymax = y_coord + J - 2;
            datazmax = z_coord + I - 2;
			
			is_me_data_region = false;
			if (!(dataxmax < xmin || dataxmin > xmax ||
				dataymax < ymin || dataymin > ymax ))
			{
				is_me_data_region = true;
			}
			if (is_me_data_region)
			{
				if (x_coord >= xmin && x_coord <= xmax)
				{
					if (y_coord >= ymin && y_coord <= ymax)
					{

						x_index = x_coord - xmin + 2;
						y_index = y_coord - ymin + 2;
						z_index = z_coord - zmin + 2;
						u[x_index][y_index][z_index][0] = u_val;
						u[x_index][y_index][z_index][1] = v_val;
						u[x_index][y_index][z_index][2] = w_val;
						p[x_index][y_index][z_index] = p_val * p0;
						point_count += 1;
					}
				}

				for (n = 1; n < total_points; n++)
				{
					fscanf(file, "%d\t%d\t%d\t%lf\t%lf\t%lf\t%lf\n", &x_coord, &y_coord, &z_coord, &u_val, &v_val, &w_val, &p_val);
					if (x_coord >= xmin && x_coord <= xmax)
					{
						if (y_coord >= ymin && y_coord <= ymax)
						{

							x_index = x_coord - xmin + 2;
							y_index = y_coord - ymin + 2;
							z_index = z_coord - zmin + 2;
							u[x_index][y_index][z_index][0] = u_val;
							u[x_index][y_index][z_index][1] = v_val;
							u[x_index][y_index][z_index][2] = w_val;
							p[x_index][y_index][z_index] = p_val * p0;
							point_count += 1;
if ((point_count *10)%(Nx*Ny*Nz)==0)
				printf("Process %d: Reading fluid  %d of %d )\n",me, point_count, (Nx*Ny*Nz));

}
					}
				}
			}
			else
			{
				for (n = 1; n < total_points; n++)
                {
					fgets(line, 256, file);
                }
			}
		}
		fclose(file);
		Fluid_data_exchange(me);
	}
}

void read_check_particle_contact(int me)
{ 
	FILE* file;
	char fname[20];

	int nproc;
	int n, i;
	int I, J;
	char line[300];
	char str1[300];
	MPI_Status status;
	sprintf(fname, "seed_particle_contact.dat");
	if ((file = fopen(fname, "rb")) == NULL)
	{
		if (me == 0)
			printf("Simulation from new initial particle contact field!\n");
		for (n = 0; n < np; n++)
		{
			for (i = 0; i < max_contact_num; i++)
			{
				tangential_contact_index[n][i] = -1;
				tangential_overlap[n][i][0] = 0.0;
				tangential_overlap[n][i][1] = 0.0;
				tangential_overlap[n][i][2] = 0.0;
			}
		}
	}
	else
	{
		fgets(line, 256, file);
		fscanf(file, "%*[^\n]");  // 读取直到换行符，但不存储
		fgetc(file);              // 读取并丢弃换行符
		fgets(line, 256, file);
		sscanf(line, "%s %s %d %s %d %s %s", str1, str1, &I, str1, &J, str1, str1);
		if (I != np)
		{
            printf("Error: The number of particles in %s is %d not consistent with the number of particles %d in the simulation domain.\n",fname, I,np);
			fclose(file);
			return;
		}
		if (J != max_contact_num)
		{
            printf("Error: The number of contact points in %s is %d not consistent with the number of contact points %d in the simulation domain.\n",fname, J, max_contact_num);
			fclose(file);
			return;
		}
        for (n = 0; n < np; n++)
        {
			for (i = 0; i < max_contact_num - 1; i++)
			{
				fscanf(file, "%d\t%lf\t%lf\t%lf\t",&tangential_contact_index[n][i], &tangential_overlap[n][i][0], &tangential_overlap[n][i][1], &tangential_overlap[n][i][2]);
			}
			fscanf(file, "%d\t%lf\t%lf\t%lf\n", &tangential_contact_index[n][i], &tangential_overlap[n][i][0], &tangential_overlap[n][i][1], &tangential_overlap[n][i][2]);
        }
		fclose(file);
		for (n = 0; n < np; n++)
		{
			MPI_Bcast(&tangential_contact_index[n][0], max_contact_num, MPI_INT, 0, MPI_COMM_WORLD);
			MPI_Bcast(&tangential_overlap[n][0][0], 3 * max_contact_num, MPI_DOUBLE, 0, MPI_COMM_WORLD);
		}
    }
	
}

// update particles' position and velocities -- parallel version
void read_check_particle(int me)
{
	FILE *fp;
	int i, j, k, n;
	double  x0, x1, x2;
	double lc, lc1, lcx, lcy, lcz;
	int  flag;
	char fname[30];
	double sita;
	double aa, bb, cc;
	char str1[40];

	//sprintf(fname, "check_particle%03d", me);
	sprintf(fname, "seed.dat");
	if ((fp = fopen(fname, "rb")) == NULL)
	{
		if (me == 0) printf("Simulation from new initial particle field!\n");
		if (me == 0) printf("volume fraction is %f!\n", fai_p);
		// initialize particle position		

		if (me == 0)
		{
			srand(time(0));
			
			if (fai_p <= 0.2)
			{

				for (n = 0; n < np; n++)
				{
					/*
					xc[0][0] = 120-R-7;
					xc[0][1] = 60;
					xc[0][2] = 60;

					xc[1][0] = 120+R+7;
					xc[1][1] = 60;
					xc[1][2] = 60;

					xc[2][0] = 68;
					xc[2][1] = 58;
					xc[2][2] = 10;
					*/
					/*
					xc[n][0] = rand() % (10 * (NxT - 2 * R + 1))/10. + R - 1;
					xc[n][1] = rand() % (10 * (NyT - 2 * R + 1))/10. + R - 1;
					xc[n][2] = rand() % (10 * (NzT - 2 * R + 1))/10. + R - 1;

					for (i = 0; i<n; i++)
					{
						lc = sqrt((xc[n][0] - xc[i][0])*(xc[n][0] - xc[i][0])+(xc[n][1] - xc[i][1])*(xc[n][1] - xc[i][1]) + (xc[n][2] - xc[i][2])*(xc[n][2] - xc[i][2]));
						if (lc <(2 * R + 1))
						{
							n--;
						}
					}
					*/
					xc[n][0] = rand() % (100 * (NxT)) / 100.;
					xc[n][1] = rand() % (100 * (NyT)) / 100.;
					xc[n][2] = rand() % (100 * (NzT)) / 100.;
					for (i = 0; i < n; i++)
					{
						lcx = abs(xc[n][0] - xc[i][0]);
						lcy = abs(xc[n][1] - xc[i][1]);
						lcz = abs(xc[n][2] - xc[i][2]);
						lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
						lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
						lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;
						lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);
						if (lc < (2 * R + 1))
						{
							n--;
						}

					}

					if (xc[n][0] < 0.5 * NxT && xc[n][1] < 0.5 * NyT) flag_p[n] = 0;
					if (xc[n][0] >= 0.5 * NxT && xc[n][1] < 0.5 * NyT) flag_p[n] = 1;
					if (xc[n][0] < 0.5 * NxT && xc[n][1] >= 0.5 * NyT) flag_p[n] = 2;
					if (xc[n][0] >= 0.5 * NxT && xc[n][1] >= 0.5 * NyT) flag_p[n] = 3;
				}
			}
			else if (fai_p > 0.2 && fai_p <= 0.64)
			{
				random_init_particle_position_generated_by_Monte_Carlo_method();
			}
			else
			{
                printf("Error: The volume fraction %f is not supported in this simulation!\n", fai_p);
                exit(0);
			}
			/*
			for (k = 0; k < z_np; k++)
			for (j = 0; j < y_np; j++)
			for (i = 0; i < x_np; i++)
			{
				n = k*x_np*y_np + j*x_np + i;
				
				
				//if (j % 2 == 0)				
				//	xc[n][0] = (NxT / 2 - (x_np - 1.0) / 2 * gap / dx) + i * gap / dx + rand() % 100 / 100.;
				//else
				//	xc[n][0] = (NxT / 2 - (x_np - 1.0) / 2 * gap / dx) + i * gap / dx + rand() % 100 / 100.;
					
				//xc[n][1] = 12 * 2 * R + j*gap / dx;
				//xc[n][2] = NzT / 2;
				xc[n][0] = (NxT / 2 - (x_np - 1.0) / 2 * gapx / dx) + i * gapx / dx + rand() % 100 / 100.;
				xc[n][1] = (NyT / 2 - (y_np - 1.0) / 2 * gapy / dx) + j * gapy / dx + rand() % 100 / 100.;
				xc[n][2] = (NzT / 2 - (z_np - 1.0) / 2 * gapz / dx) + k * gapz / dx + rand() % 100 / 100.;

				if (i < 0.5*x_np&&j<0.5*y_np) flag_p[n] = 0;
				if (i >= 0.5*x_np&&j<0.5*y_np) flag_p[n] = 1;
				if (i < 0.5*x_np&&j>=0.5*y_np) flag_p[n] = 2;
				if (i >= 0.5*x_np&&j>=0.5*y_np) flag_p[n] = 3;
			}
		*/
			//xc[0][0] = NxT / 2+2*R;
			//xc[0][1] = 8.0 * 2 * R;
			//xc[0][2] = NzT/2;
			/*
			for (n = 0; n < 6; n++)
			{
				sita = n * 2 * PI / 6;
				xc[n][0] = 1.5*R;
				xc[n][1] = NyT / 2 + (gap - 2 * dx) * cos(sita) / dx;
				xc[n][2] = NzT / 2 + (gap - 2 * dx) * sin(sita) / dx;
			}
			for (n = 6; n < 16; n++)
			{
				sita = (n - 6) * 2 * PI / 10;
				xc[n][0] = 1.5*R;
				xc[n][1] = NyT / 2 + (2 * gap - 2 * dx)*cos(sita) / dx;
				xc[n][2] = NzT / 2 + (2 * gap - 2 * dx)*sin(sita) / dx;
			}
			
			
			for (i = 0; i<x_np; i++)
			for (j = 0; j<y_np; j++)
			for (k = 0; k<z_np; k++)
			{
			n = i*y_np*z_np + j*z_np + k;
			xc[n][0] = (1.5*R + i*gap / dx) + rand() % 100 / 100.;
			xc[n][1] = (Ly / 2.0 + (j - (y_np - 1.0) / 2.0)*gap) / dx + rand() % 100 / 100.;
			xc[n][2] = (Lz / 2.0 + (k - (z_np - 1.0) / 2.0)*gap) / dx + rand() % 100 / 100.;
			}
			
			xc[0][0] = NxT / 2;
			xc[0][1] = NyT / 2+2*R;
			xc[0][2] = NzT/2;
			*/
		}

		for (j = 0; j<np; j++)
		{
			//int xp_init, yp_init, zp_init;
			//xp_init = (int)(xc[j][0]);
			//yp_init = (int)(xc[j][1]);
			//zp_init = (int)(xc[j][2]);

			//uc[j][0] = u[xp_init][yp_init][zp_init][0];
			//uc[j][0] = 0.0;
			//uc[j][1] = -pow((1 - fai), 5);
			//uc[j][2] = 0.0;
			uc[j][0] = u_init_x;
			uc[j][1] = u_init_y;
			uc[j][2] = u_init_z;
			MPI_Bcast(&xc[j][0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
            MPI_Bcast(&uc[j][0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
		}
		memset(wc, 0., sizeof(wc));
		MPI_Barrier(MPI_COMM_WORLD);

		// particle dependent variables on lagrangian points: uf, up, force, uc, ac
		//memset(uc, 0., sizeof(uc));
		//memset(ac, 0., sizeof(ac));
		// particle cross interface or not
		//memset(Cross, 0., sizeof(Cross));
		// particle statistics
		//		memset(ppdf,0,sizeof(ppdf));
		//		memset(rdf,0,sizeof(rdf));
		//		memset(us,0,sizeof(us));	

	}
	else
	{
		if (me == 0)
		{
			printf("file is opened!\n");
			fscanf(fp, "%s", str1);
			fscanf(fp, "%s", str1);
			fscanf(fp, "%s %s %d%s %s", str1, str1, &aa, str1, str1);

			for (j = 0; j < np; j++)
			{
				fscanf(fp, "%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%d\t%d\t%d\t%d\t%d\n", &xc[j][0], &xc[j][1], &xc[j][2], &uc[j][0], \
					&uc[j][1], &uc[j][2], &wc[j][0], &wc[j][1], &wc[j][2], &aa, &bb, &cc, &aa, &bb, &cc, &aa, &bb, &cc, &aa, &bb);
			}
			fclose(fp);
			/*
			fread(xc, sizeof(xc), 1, file);
			fread(uc, sizeof(uc), 1, file);
			//fread(Crossx, sizeof(Crossx), 1, file);
			//fread(Crossy, sizeof(Crossy), 1, file);
			fclose(file);
			*/
		}		
		for (j = 0; j < np; j++)
		{			
			//uc[j][0] = 0.0;
			//uc[j][1] = -pow((1-fai),5);
			//uc[j][2] = 0.0;
			MPI_Bcast(&xc[j][0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
			MPI_Bcast(&uc[j][0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
			MPI_Bcast(&wc[j][0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);

		}
	}
}

void random_init_particle_position_generated_by_Monte_Carlo_method()
{
	double R_init, R_target, delta_R, R_temp;
	double xc_temp[3];
	double displacement[3];
	double step_max, current_step;
	double u, theta, r, lc, lcx, lcy, lcz;
	double original_x, original_y, original_z;
	double phi_current;
	int swell_step_max;    // swell step
	int swell_step;
	int iter_count;
	int iter_max = 100;         
	int i, j, k, n;
	double min_gap = 1.0;
	int total_moved, moved, valid_position;
	int overlap_count;

	int eq_iter, eq_max_iter;
	int eq_accept_count, eq_total_attempts;
	double eq_step_size = min_gap / 4.0;  
	double eq_displacement[3];
	double eq_xc_temp[3];
	double eq_u, eq_theta;
	double acceptance_rate;

	R_target = R;
	R_init = R * pow(0.2 / fai_p, 1. / 3.);
	delta_R = ((pow(0.64 / fai_p, 1. / 3.) - 1.) * 2 * R - min_gap) / 300.;//where 300 is a Redundancy coefficient to make sure delta_p is small enough
	swell_step_max = (ceil(R_target - R_init) / delta_R);
	delta_R = (R_target - R_init) / double(swell_step_max);
	step_max = min_gap / 2.0;  // 添加step_max的初始化

	if (fai_p * (pow((R + 0.5 * min_gap) / R, 3)) > 0.64)
	{
		printf("Error: The volume fraction %f and min_gap %f is not supported in this simulation!\n", fai_p, min_gap);
		exit(0);
	}
	
	printf("now use Monte_Carlo_method to generate random_init_particle_position\n");
	// initialize particle position in a low fraction by small radius first
	printf("initialize particle position in a low fraction by small radius first\n");
    printf("R_init = %f\t R_target = %f\t delta_R =%f\n", R_init, R_target, delta_R);
	for (n = 0; n < np; n++)
	{
		xc[n][0] = rand() % (100 * (NxT)) / 100.;
		xc[n][1] = rand() % (100 * (NyT)) / 100.;
		xc[n][2] = rand() % (100 * (NzT)) / 100.;
		for (i = 0; i < n; i++)
		{
			lcx = fabs(xc[n][0] - xc[i][0]);
			lcy = fabs(xc[n][1] - xc[i][1]);
			lcz = fabs(xc[n][2] - xc[i][2]);
			lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
			lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
			lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;
			lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);
			if (lc < (2 * R_init + min_gap))
			{
				n--;
				break;
			}
		}
	}
	R_temp = R_init;
	for (swell_step = 0; swell_step < swell_step_max; swell_step++)
	{
		printf("Swelling step %d/%d: R = %.4f -> %.4f\n",
			swell_step + 1, swell_step_max, R_temp, R_temp + delta_R);
		R_temp += delta_R;
		total_moved = 0;  // 统计成功移动的颗粒数
		for (i = 0; i < np; i++)
		{
			moved = 0;// 标记当前颗粒是否成功移动
			original_x = xc[i][0];
			original_y = xc[i][1];
			original_z = xc[i][2];

			current_step = step_max;

			for (iter_count = 0; iter_count < iter_max; iter_count++)
			{
				u = ((double)rand() / 180.)/180. * PI;
				theta = ((double)rand() / 360.)/360. * 2.0 * PI;
				displacement[0] = current_step * sin(u) * sin(theta);
				displacement[1] = current_step * sin(u) * cos(theta);
				displacement[2] = current_step * cos(u);

                xc_temp[0] = original_x + displacement[0];
                xc_temp[1] = original_y + displacement[1];
                xc_temp[2] = original_z + displacement[2];

				xc_temp[0] = fmod(xc_temp[0] + NxT, NxT);
				xc_temp[1] = fmod(xc_temp[1] + NyT, NyT);
				xc_temp[2] = fmod(xc_temp[2] + NzT, NzT);

				valid_position = 1;

				for (j = 0; j < np; j++)
				{
					if (j == i)
					{
						continue;
					}
					lcx = fabs(xc_temp[0] - xc[j][0]);
					lcy = fabs(xc_temp[1] - xc[j][1]);
					lcz = fabs(xc_temp[2] - xc[j][2]);

					lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
					lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
					lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;

					lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);

					// 检查间隙是否大于min_gap（使用当前半径R_temp）
					if (lc < (2 * R_temp + min_gap))
					{
						valid_position = 0;
						break;  // 提前结束，不需要检查其他颗粒
					}

				}
				if (valid_position)
				{
					// 接受移动
					xc[i][0] = xc_temp[0];
					xc[i][1] = xc_temp[1];
					xc[i][2] = xc_temp[2];
					moved = 1;
					total_moved++;
					break;  // 跳出尝试循环
				}
				else
				{
					// 减小步长
					current_step = step_max * (1.0 - (double)(iter_count + 1) / iter_max);

					// 重置为原始位置，准备下一次尝试
					xc[i][0] = original_x;
					xc[i][1] = original_y;
					xc[i][2] = original_z;
				}
			}
			if (!moved)
			{
				// 如果所有尝试都失败，保持原始位置
				xc[i][0] = original_x;
				xc[i][1] = original_y;
				xc[i][2] = original_z;
			}
		}
		// 检查当前配置的重叠情况
		overlap_count = 0;
		for (i = 0; i < np; i++)
		{
			for (j = i + 1; j < np; j++)
			{
				lcx = fabs(xc[i][0] - xc[j][0]);
				lcy = fabs(xc[i][1] - xc[j][1]);
				lcz = fabs(xc[i][2] - xc[j][2]);

				lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
				lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
				lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;

				lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);

				if (lc < (2 * R_temp + min_gap))
				{
					overlap_count++;
				}
			}
		}
		phi_current = np * (4.0 / 3.0) * M_PI * pow(R_temp, 3) / (NxT * NyT * NzT);
		printf("  Moved %d particles, Current phi = %.4f, Overlaps = %d\n",
			total_moved, phi_current, overlap_count);

		if (overlap_count > 0 && swell_step == swell_step_max - 1)
		{
			printf("  Warning: %d overlaps remain at final swelling step\n", overlap_count);
		}
	}
	
	// 最终检查
	overlap_count = 0;
	for (i = 0; i < np; i++)
	{
		for (j = i + 1; j < np; j++)
		{
			lcx = fabs(xc[i][0] - xc[j][0]);
			lcy = fabs(xc[i][1] - xc[j][1]);
			lcz = fabs(xc[i][2] - xc[j][2]);

			lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
			lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
			lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;

			lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);

			if (lc < (2 * R_target + min_gap))
			{
				overlap_count++;
			}
		}
	}

	printf("\nGeneration completed:\n");
	printf("  Final radius: %.4f (target: %.4f)\n", R_temp, R_target);
	printf("  Final overlaps: %d\n", overlap_count);

	if (overlap_count == 0)
	{
		printf("  Success: All particles satisfy the minimum gap requirement.\n");
	}
	else
	{
		printf("  Warning: Configuration has %d overlaps!\n", overlap_count);
	}

	eq_max_iter = swell_step_max / 2.;
	printf("\nStarting simplified equilibration process...\n");
	printf("Fixed step size: %.4f, Max iterations: %d\n", eq_step_size, eq_max_iter);

	for (eq_iter = 0; eq_iter < eq_max_iter; eq_iter++)
	{
		eq_accept_count = 0;
		eq_total_attempts = 0;

		// 遍历所有颗粒
		for (i = 0; i < np; i++)
		{
			// 生成随机方向
			eq_u = ((double)rand() / RAND_MAX) * M_PI;
			eq_theta = ((double)rand() / RAND_MAX) * 2.0 * M_PI;

			// 固定距离的位移
			eq_displacement[0] = eq_step_size * sin(eq_u) * cos(eq_theta);
			eq_displacement[1] = eq_step_size * sin(eq_u) * sin(eq_theta);
			eq_displacement[2] = eq_step_size * cos(eq_u);

			// 计算试探位置
			eq_xc_temp[0] = xc[i][0] + eq_displacement[0];
			eq_xc_temp[1] = xc[i][1] + eq_displacement[1];
			eq_xc_temp[2] = xc[i][2] + eq_displacement[2];

			// 周期性边界处理
			eq_xc_temp[0] = fmod(eq_xc_temp[0] + NxT, NxT);
			eq_xc_temp[1] = fmod(eq_xc_temp[1] + NyT, NyT);
			eq_xc_temp[2] = fmod(eq_xc_temp[2] + NzT, NzT);

			// 检查是否满足间隙条件
			valid_position = 1;
			for (j = 0; j < np; j++)
			{
				if (j == i) continue;

				lcx = fabs(eq_xc_temp[0] - xc[j][0]);
				lcy = fabs(eq_xc_temp[1] - xc[j][1]);
				lcz = fabs(eq_xc_temp[2] - xc[j][2]);

				lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
				lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
				lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;

				lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);

				if (lc < (2 * R_target + min_gap))
				{
					valid_position = 0;
					break;
				}
			}

			eq_total_attempts++;
			if (valid_position)
			{
				// 接受移动
				xc[i][0] = eq_xc_temp[0];
				xc[i][1] = eq_xc_temp[1];
				xc[i][2] = eq_xc_temp[2];
				eq_accept_count++;
			}
			// 如果移动被拒绝，颗粒保持原位
		}

		// 每100步输出一次进度
		if ((eq_iter + 1) % 100 == 0)
		{
			acceptance_rate = (double)eq_accept_count / eq_total_attempts;
			printf("  Eq step %d/%d: acceptance rate = %.3f\n",
				eq_iter + 1, eq_max_iter, acceptance_rate);
		}
	}
	acceptance_rate = (double)eq_accept_count / eq_total_attempts;
	printf("Equilibration completed: final acceptance rate = %.3f\n", acceptance_rate);

	printf("\nFinal configuration check:\n");

	overlap_count = 0;
	for (i = 0; i < np; i++)
	{
		for (j = i + 1; j < np; j++)
		{
			lcx = fabs(xc[i][0] - xc[j][0]);
			lcy = fabs(xc[i][1] - xc[j][1]);
			lcz = fabs(xc[i][2] - xc[j][2]);

			lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
			lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
			lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;

			lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);

			if (lc < (2 * R_target + min_gap))
			{
				overlap_count++;
			}
		}
	}

	printf("Generation completed:\n");
}

void read_check_particle_poly(int me)
{
	FILE* fp;
	int i, j, k, n;
	double  x0, x1, x2;
	double lc, lc1, lcx, lcy, lcz;
	int  flag;
	char fname[30];
	double sita;
	double aa, bb, cc;
	char str1[40];
	int num_temp[R_poly_num];
	double temp;
	double R_sauter_3 = 0.;
	double R_sauter_2 = 0.;

	sprintf(fname, "seed.dat");
	if ((fp = fopen(fname, "rb")) == NULL)
	{
		if (me == 0) printf("Simulation from new initial particle field!\n");
		
		// initialize particle position		

		if (me == 0)
		{
			srand(time(0));

			temp = 0;
			for (i=0;i<R_poly_num;i++)
			{
				temp += R_poly_ratio[i];
			}
			if (temp != 1)
			{
				fprintf(stderr,"Error: sum of R_poly_ratio is not equal to 1!\n");
				for (i = 0; i < R_poly_num; i++)
				{
					R_poly_ratio[i] = R_poly_ratio[i] / temp;
				}
			}
			temp = 0;
			par_volume_poly = 0;
			Ff_poly = 0;
			for (i = 0; i < R_poly_num; i++)
			{
				if (i == (R_poly_num - 1))
					num_temp[i] = np;
				else
				{
					temp += R_poly_ratio[i];
					num_temp[i] = (int)(np * temp);
				}
				if (i == 0)
				{
					par_volume_poly += 4.0 * PI * pow(R_poly[i], 3) / 3.0 * num_temp[i];
					Ff_poly += 4.0 * PI * pow((R_poly[i] * dx_poly), 3) / 3.0 * (rhop_poly[i] / rhof - 1.0) * num_temp[i] * G;
					printf("%dth particle radius is %d, particle num is %d\n", i, R_poly[i], num_temp[i]);
					R_sauter_2 += pow(R_poly[i], 2) * num_temp[i];
                    R_sauter_3 += pow(R_poly[i], 3) * num_temp[i];
				}
				else
				{
					par_volume_poly += 4.0 * PI * pow(R_poly[i], 3) / 3.0 * (num_temp[i] - num_temp[i - 1]);
					Ff_poly += 4.0 * PI * pow((R_poly[i] * dx_poly), 3) / 3.0 * (rhop_poly[i] / rhof - 1.0) * (num_temp[i] - num_temp[i - 1]) * G;
					printf("%dth particle radius is %d, particle num is %d\n", i, R_poly[i], (num_temp[i] - num_temp[i - 1]));
                    R_sauter_2 += pow(R_poly[i], 2) * (num_temp[i] - num_temp[i - 1]);
                    R_sauter_3 += pow(R_poly[i], 3) * (num_temp[i] - num_temp[i - 1]);
				}
			}
			phi_p_poly = par_volume_poly / (NxT * NyT * NzT);
			Ff_poly /= (NxT * NyT * NzT * dx_poly * dx_poly * dx_poly);
			R_sauter_mean = R_sauter_3 / R_sauter_2;
			printf("volume fraction is %f!\tvolume by gird is %f!\n", phi_p_poly, par_volume_poly);
            printf("sauter mean R is %f\n", R_sauter_mean);
			for (i = 0; i < 3; i++)
			{
				Ff_body_poly[i] = Ff_poly * G_dire[i];
			}
			for (i = 0; i < R_poly_num; i++)
			{
				if (i == 0)
				{
					for (j = 0; j < num_temp[0]; j++)
					{ 
						rc_poly[j] = i;
						rhoc_poly[j] = rhop_poly[i];
					}
				}
				else
				{
					for (j = num_temp[i - 1]; j < num_temp[i]; j++)
					{
						rc_poly[j] = i;
						rhoc_poly[j] = rhop_poly[i];
					}
				}
			}
			if (phi_p_poly < 0.2)
			{
				for (n = 0; n < np; n++)
				{
					/*
					xc[n][0] = rand() % (10 * (NxT - 2 * R_poly_max + 1)) / 10. + R_poly_max - 1;
					xc[n][1] = rand() % (10 * (NyT - 2 * R_poly_max + 1)) / 10. + R_poly_max - 1;
					xc[n][2] = rand() % (10 * (NzT - 2 * R_poly_max + 1)) / 10. + R_poly_max - 1;
					//xc[n][2] = 25;
					for (i = 0; i < n; i++)
					{
						lc = sqrt((xc[n][0] - xc[i][0]) * (xc[n][0] - xc[i][0]) + (xc[n][1] - xc[i][1]) * (xc[n][1] - xc[i][1]) + (xc[n][2] - xc[i][2]) * (xc[n][2] - xc[i][2]));
						if (lc < (R_poly[rc_poly[n]] + R_poly[rc_poly[i]] + 1))
						{
							n--;
						}
					}
					*/
					xc[n][0] = rand() % (100 * (NxT)) / 100.;
					xc[n][1] = rand() % (100 * (NyT)) / 100.;
					xc[n][2] = rand() % (100 * (NzT)) / 100.;
					for (i = 0; i < n; i++)
					{
						lcx = abs(xc[n][0] - xc[i][0]);
						lcy = abs(xc[n][1] - xc[i][1]);
						lcz = abs(xc[n][2] - xc[i][2]);
						lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
						lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
						lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;
						lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);
						if (lc < (R_poly[rc_poly[n]] + R_poly[rc_poly[i]] + 1))
						{
							n--;
						}

					}

					if (xc[n][0] < 0.5 * NxT && xc[n][1] < 0.5 * NyT) flag_p[n] = 0;
					if (xc[n][0] >= 0.5 * NxT && xc[n][1] < 0.5 * NyT) flag_p[n] = 1;
					if (xc[n][0] < 0.5 * NxT && xc[n][1] >= 0.5 * NyT) flag_p[n] = 2;
					if (xc[n][0] >= 0.5 * NxT && xc[n][1] >= 0.5 * NyT) flag_p[n] = 3;
				}
			}
			else if (phi_p_poly >0.2 && phi_p_poly < 0.64)
			{ 
				random_init_poly_particle_position_generated_by_Monte_Carlo_method();
			}
			else
			{
				printf("Error: The volume fraction %f is not supported in this simulation!\n", phi_p_poly);
				exit(0);
			}

		}

		for (j = 0; j < np; j++)
		{
			uc[j][0] = u_init_x_poly;
			uc[j][1] = u_init_y_poly;
			uc[j][2] = u_init_z_poly;
			MPI_Bcast(&xc[j][0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
			MPI_Bcast(&uc[j][0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
			MPI_Bcast(&rc_poly[j], 1, MPI_INT, 0, MPI_COMM_WORLD);
            MPI_Bcast(&rhoc_poly[j], 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
		}
		memset(wc, 0., sizeof(wc));
		MPI_Barrier(MPI_COMM_WORLD);
	}
	else
	{
		if (me == 0)
		{
			printf("file is opened!\n");
			fscanf(fp, "%s", str1);
			fscanf(fp, "%s", str1);
			fscanf(fp, "%s %s %d%s %s", str1, str1, &aa, str1, str1);

			for (j = 0; j < np; j++)
			{
				fscanf(fp, "%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%lf\t%d\t%d\t%lf\t%d\t%d\t%d\t%d\n", &xc[j][0], &xc[j][1], &xc[j][2], &uc[j][0], \
					&uc[j][1], &uc[j][2], &wc[j][0], &wc[j][1], &wc[j][2], &aa, &bb, &cc, &aa, &bb, &cc, &aa, &bb, &rhoc_poly[j], &aa, &bb, &cc, &rc_poly[j]);
			}
			fclose(fp);
		}
		par_volume_poly = 0;
		Ff_poly = 0;
		R_sauter_2 = 0.0;  // reset to 0.,else rank 0 will be error
		R_sauter_3 = 0.0;
		for (j = 0; j < np; j++)
		{
			//par_volume_poly += 4.0 * PI * pow(R_poly[rc_poly[j]], 3) / 3.0;
            //Ff_poly += 4.0 * PI * pow((R_poly[rc_poly[j]] * dx_poly), 3) / 3.0 * (rhoc_poly[j] / rhof - 1.0) * G;

			MPI_Bcast(&xc[j][0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
			MPI_Bcast(&uc[j][0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
			MPI_Bcast(&wc[j][0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
			MPI_Bcast(&rc_poly[j], 1, MPI_INT, 0, MPI_COMM_WORLD);
			MPI_Bcast(&rhoc_poly[j], 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

			par_volume_poly += 4.0 * PI * pow(R_poly[rc_poly[j]], 3) / 3.0;
			Ff_poly += 4.0 * PI * pow((R_poly[rc_poly[j]] * dx_poly), 3) / 3.0 * (rhoc_poly[j] / rhof - 1.0) * G;
			R_sauter_2 += pow(R_poly[rc_poly[j]], 2);
			R_sauter_3 += pow(R_poly[rc_poly[j]], 3);
		}
		R_sauter_mean = R_sauter_3 / R_sauter_2;
		phi_p_poly = par_volume_poly / (NxT * NyT * NzT);
		Ff_poly /= (NxT * NyT * NzT * dx_poly * dx_poly * dx_poly);
		printf("volume fraction is %f!\tvolume by gird is %f!\n", phi_p_poly, par_volume_poly);
		printf("sauter mean R is %f\n", R_sauter_mean);
		for (i = 0; i < 3; i++)
		{
			Ff_body_poly[i] = Ff_poly * G_dire[i];
		}
	}
}

void random_init_poly_particle_position_generated_by_Monte_Carlo_method()
{
	// 多粒径相关局部变量
	double R_poly_init[R_poly_num];  // 各粒径的初始半径（低体积分数下）
	double R_poly_temp[R_poly_num];  // 各粒径的当前膨胀半径
	double delta_R_poly[R_poly_num]; // 各粒径每步膨胀的固定增量
	double target_ratio;             // 从初始半径到目标半径的总缩放比例（仅用于计算初始半径）
	double delta_temp;
	double r_sum;
	double current_ratio;

	double min_gap = 1.0;            
	double step_max, current_step;
	double displacement[3], xc_temp[3];
	double u, theta, lc, lcx, lcy, lcz;
	double original_x, original_y, original_z;
	double phi_current;
	int swell_step_max, swell_step;
	int iter_count, iter_max = 100;
	int total_moved, moved, valid_position;
	int overlap_count, i, j, n;

	// 平衡化过程变量
	int eq_max_iter, eq_iter;
	int eq_accept_count, eq_total_attempts;
	double eq_step_size = min_gap / 4.0;
	double eq_displacement[3], eq_xc_temp[3];
	double eq_u, eq_theta, acceptance_rate;

	phi_current = 0.0;
	for (i = 0; i < np; i++)
	{
		phi_current += 4.0 / 3.0 * M_PI * pow((R_poly[rc_poly[i]] + min_gap / 2), 3);
	}
	phi_current /= (NxT * NyT * NzT);
	if (phi_current > 0.64)
	{
		fprintf(stderr, "Error: The volume fraction %f and min_gap %f is not supported for poly particles! up to %f\n", phi_p_poly, min_gap, phi_current);
		exit(0);
	}

	// 2. 计算各粒径的初始半径（先在0.2低体积分数下初始化，避免初始重叠）
	target_ratio = pow(phi_p_poly / 0.2, 1.0 / 3.0);  // 总缩放比例（从0.2体积分数到目标体积分数）
	for (i = 0; i < R_poly_num; i++)
	{
		R_poly_init[i] = R_poly[i] / target_ratio;  // 初始半径 = 目标半径 / 缩放比例
		R_poly_temp[i] = R_poly_init[i];            // 初始化当前半径为初始半径
	}

	// 3. 设定膨胀步数和每步**固定线性增量**（核心修改：替换累乘为累加）
	delta_temp = ((pow(0.64 / phi_p_poly, 1.0 / 3.0) - 1) * R_poly_max * 2 - min_gap) / 600.;	//where 400 is a Redundancy coefficient to make sure delta_p is small enough
	swell_step_max = ceil(R_poly_max * (1. - 1. / target_ratio) / delta_temp);
    printf("target_ratio = %f, delta_temp = %f, swell_step_max = %d",target_ratio, delta_temp, swell_step_max);
	for (i = 0; i < R_poly_num; i++)
	{
		delta_R_poly[i] = (R_poly[i] - R_poly_init[i]) / swell_step_max;
	}
	step_max = min_gap / 2.0;  // 蒙特卡洛移动最大步长

	// ===================== 第二步：低体积分数下初始化颗粒位置 =====================
	printf("now use Monte_Carlo_method to generate random init poly particle position\n");
	printf("initialize poly particle position in low fraction (0.2) first\n");
	for (i = 0; i < R_poly_num; i++)
	{
		printf("  %dth poly: R_init = %.4f\t R_target = %d\t Step delta = %.6f\n",
			i, R_poly_init[i], R_poly[i], delta_R_poly[i]);
	}

	for (n = 0; n < np; n++)
	{
		// 生成随机初始位置（周期性计算域内）
		xc[n][0] = rand() % (100 * (NxT)) / 100.;
		xc[n][1] = rand() % (100 * (NyT)) / 100.;
		xc[n][2] = rand() % (100 * (NzT)) / 100.;

		// 检查与已生成颗粒的重叠（多粒径适配：两颗粒半径之和+最小间隙）
		for (i = 0; i < n; i++)
		{
			lcx = fabs(xc[n][0] - xc[i][0]);
			lcy = fabs(xc[n][1] - xc[i][1]);
			lcz = fabs(xc[n][2] - xc[i][2]);

			// 周期性边界距离处理（最小镜像距离）
			lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
			lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
			lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;

			lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);

			// 多粒径重叠判断：当前颗粒半径 + 已生成颗粒半径 + 最小间隙
			r_sum = R_poly_init[rc_poly[n]] + R_poly_init[rc_poly[i]] + min_gap;
			if (lc < r_sum)
			{
				n--;  // 重叠则重新生成当前颗粒
				break;
			}
		}
	}

	// ===================== 第三步：逐步膨胀颗粒+蒙特卡洛移动优化（核心修改：线性累加） =====================
	for (swell_step = 0; swell_step < swell_step_max; swell_step++)
	{
		// 输出当前膨胀进度（线性增长，更直观）
		current_ratio = (swell_step + 1) / (double)swell_step_max;
		printf("Swelling step %d/%d: current progress = %.6f (linear growth)\n",
			swell_step + 1, swell_step_max, current_ratio);

		// 1. 按**固定增量累加**更新所有粒径的当前半径（核心修改：替换累乘为累加）
		for (i = 0; i < R_poly_num; i++)
		{
			R_poly_temp[i] += delta_R_poly[i];

			// 最后一步强制赋值为目标半径，避免浮点累加的微小偏差（双重保障）
			if (swell_step == swell_step_max - 1)
			{
				R_poly_temp[i] = R_poly[i];
			}
		}

		// 2. 蒙特卡洛移动颗粒，解决膨胀带来的重叠（逻辑不变）
		total_moved = 0;
		for (i = 0; i < np; i++)
		{
			moved = 0;
			original_x = xc[i][0];
			original_y = xc[i][1];
			original_z = xc[i][2];
			current_step = step_max;

			// 最多尝试100次移动，逐步减小步长
			for (iter_count = 0; iter_count < iter_max; iter_count++)
			{
				// 生成3D随机方向位移（球坐标转直角坐标）
				u = ((double)rand() / RAND_MAX) * M_PI;
				theta = ((double)rand() / RAND_MAX) * 2.0 * M_PI;
				displacement[0] = current_step * sin(u) * cos(theta);
				displacement[1] = current_step * sin(u) * sin(theta);
				displacement[2] = current_step * cos(u);

				// 计算试探位置并处理周期性边界
				xc_temp[0] = original_x + displacement[0];
				xc_temp[1] = original_y + displacement[1];
				xc_temp[2] = original_z + displacement[2];
				xc_temp[0] = fmod(xc_temp[0] + NxT, NxT);
				xc_temp[1] = fmod(xc_temp[1] + NyT, NyT);
				xc_temp[2] = fmod(xc_temp[2] + NzT, NzT);

				// 检查试探位置是否有效（无重叠）
				valid_position = 1;
				for (j = 0; j < np; j++)
				{
					if (j == i) continue;

					lcx = fabs(xc_temp[0] - xc[j][0]);
					lcy = fabs(xc_temp[1] - xc[j][1]);
					lcz = fabs(xc_temp[2] - xc[j][2]);
					lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
					lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
					lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;
					lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);

					// 多粒径重叠判断（当前半径之和+最小间隙）
					r_sum = R_poly_temp[rc_poly[i]] + R_poly_temp[rc_poly[j]] + min_gap;
					if (lc < r_sum)
					{
						valid_position = 0;
						break;
					}
				}

				// 位置有效则接受移动，否则减小步长重试
				if (valid_position)
				{
					xc[i][0] = xc_temp[0];
					xc[i][1] = xc_temp[1];
					xc[i][2] = xc_temp[2];
					moved = 1;
					total_moved++;
					break;
				}
				else
				{
					current_step = step_max * (1.0 - (double)(iter_count + 1) / iter_max);
					xc[i][0] = original_x;
					xc[i][1] = original_y;
					xc[i][2] = original_z;
				}
			}

			if (!moved)
			{
				xc[i][0] = original_x;
				xc[i][1] = original_y;
				xc[i][2] = original_z;
			}
		}

		// 3. 统计当前配置的重叠数和体积分数
		overlap_count = 0;
		phi_current = 0.0;
		for (i = 0; i < np; i++)
		{
			// 计算当前总体积分数
			phi_current += 4.0 / 3.0 * M_PI * pow(R_poly_temp[rc_poly[i]], 3);
			for (j = i + 1; j < np; j++)
			{
				lcx = fabs(xc[i][0] - xc[j][0]);
				lcy = fabs(xc[i][1] - xc[j][1]);
				lcz = fabs(xc[i][2] - xc[j][2]);
				lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
				lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
				lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;
				lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);

				r_sum = R_poly_temp[rc_poly[i]] + R_poly_temp[rc_poly[j]] + min_gap;
				if (lc < r_sum)
				{
					overlap_count++;
				}
			}
		}
		phi_current /= (NxT * NyT * NzT);

		// 输出当前统计信息
		printf("  Moved %d particles, Current phi = %.4f, Overlaps = %d\n",
			total_moved, phi_current, overlap_count);

		// 最终膨胀步若有重叠，输出警告
		if (overlap_count > 0 && swell_step == swell_step_max - 1)
		{
			printf("  Warning: %d overlaps remain at final swelling step\n", overlap_count);
		}
	}

	// ===================== 第四步：最终配置检查 =====================
	overlap_count = 0;
	for (i = 0; i < np; i++)
	{
		for (j = i + 1; j < np; j++)
		{
			lcx = fabs(xc[i][0] - xc[j][0]);
			lcy = fabs(xc[i][1] - xc[j][1]);
			lcz = fabs(xc[i][2] - xc[j][2]);
			lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
			lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
			lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;
			lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);

			r_sum = R_poly[rc_poly[i]] + R_poly[rc_poly[j]] + min_gap;
			if (lc < r_sum)
			{
				overlap_count++;
			}
		}
	}

	printf("\nPoly particle generation completed:\n");
	printf("  Target volume fraction: %.4f\n", phi_p_poly);
	printf("  Final overlaps: %d\n", overlap_count);
	if (overlap_count == 0)
	{
		printf("  Success: All poly particles satisfy the minimum gap requirement.\n");
	}
	else
	{
		printf("  Warning: Poly configuration has %d overlaps!\n", overlap_count);
	}

	// ===================== 第五步：平衡化过程（优化颗粒分布） =====================
	eq_max_iter = swell_step_max / 2;
	printf("\nStarting simplified equilibration process for poly particles...\n");
	printf("Fixed step size: %.4f, Max iterations: %d\n", eq_step_size, eq_max_iter);

	for (eq_iter = 0; eq_iter < eq_max_iter; eq_iter++)
	{
		eq_accept_count = 0;
		eq_total_attempts = 0;

		for (i = 0; i < np; i++)
		{
			// 生成固定步长随机位移
			eq_u = ((double)rand() / RAND_MAX) * M_PI;
			eq_theta = ((double)rand() / RAND_MAX) * 2.0 * M_PI;
			eq_displacement[0] = eq_step_size * sin(eq_u) * cos(eq_theta);
			eq_displacement[1] = eq_step_size * sin(eq_u) * sin(eq_theta);
			eq_displacement[2] = eq_step_size * cos(eq_u);

			// 试探位置与周期性边界处理
			eq_xc_temp[0] = xc[i][0] + eq_displacement[0];
			eq_xc_temp[1] = xc[i][1] + eq_displacement[1];
			eq_xc_temp[2] = xc[i][2] + eq_displacement[2];
			eq_xc_temp[0] = fmod(eq_xc_temp[0] + NxT, NxT);
			eq_xc_temp[1] = fmod(eq_xc_temp[1] + NyT, NyT);
			eq_xc_temp[2] = fmod(eq_xc_temp[2] + NzT, NzT);

			// 检查位置有效性
			valid_position = 1;
			for (j = 0; j < np; j++)
			{
				if (j == i) continue;

				lcx = fabs(eq_xc_temp[0] - xc[j][0]);
				lcy = fabs(eq_xc_temp[1] - xc[j][1]);
				lcz = fabs(eq_xc_temp[2] - xc[j][2]);
				lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
				lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
				lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;
				lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);

				r_sum = R_poly[rc_poly[i]] + R_poly[rc_poly[j]] + min_gap;
				if (lc < r_sum)
				{
					valid_position = 0;
					break;
				}
			}

			// 统计接受率
			eq_total_attempts++;
			if (valid_position)
			{
				xc[i][0] = eq_xc_temp[0];
				xc[i][1] = eq_xc_temp[1];
				xc[i][2] = eq_xc_temp[2];
				eq_accept_count++;
			}
		}

		// 每100步输出平衡化进度
		if ((eq_iter + 1) % 100 == 0)
		{
			acceptance_rate = (double)eq_accept_count / eq_total_attempts;
			printf("  Eq step %d/%d: acceptance rate = %.3f\n",
				eq_iter + 1, eq_max_iter, acceptance_rate);
		}
	}

	// 平衡化完成统计
	acceptance_rate = (double)eq_accept_count / eq_total_attempts;
	printf("Poly particle equilibration completed: final acceptance rate = %.3f\n", acceptance_rate);

	// ===================== 第六步：最终平衡化后检查 =====================
	overlap_count = 0;
	for (i = 0; i < np; i++)
	{
		for (j = i + 1; j < np; j++)
		{
			lcx = fabs(xc[i][0] - xc[j][0]);
			lcy = fabs(xc[i][1] - xc[j][1]);
			lcz = fabs(xc[i][2] - xc[j][2]);
			lcx = (lcx > (NxT / 2.)) ? (NxT - lcx) : lcx;
			lcy = (lcy > (NyT / 2.)) ? (NyT - lcy) : lcy;
			lcz = (lcz > (NzT / 2.)) ? (NzT - lcz) : lcz;
			lc = sqrt(lcx * lcx + lcy * lcy + lcz * lcz);

			r_sum = R_poly[rc_poly[i]] + R_poly[rc_poly[j]] + min_gap;
			if (lc < r_sum)
			{
				overlap_count++;
			}
		}
	}

	printf("\nFinal poly particle configuration check:\n");
	printf("  Final overlaps: %d\n", overlap_count);
	if (overlap_count == 0)
	{
		printf("  Success: All poly particles are well distributed without overlaps.\n");
	}
	else
	{
		printf("  Warning: Poly configuration still has %d overlaps after equilibration.\n", overlap_count);
	}
}


void static_fluid_velocity(int me)
{
	int x, y, z,j,k;
	int rcj;
	double lc, lxc, lyc, lzc;	
	int me_x, me_y;  //�ý��̶�Ӧ��x�����y������	
	double xc_key[3];

	double local_m_fluid = 0;
	double local_aveU[3] = { 0.0, 0.0, 0.0 };

	me_y = int(me / NPROCx);
	me_x = me - me_y*NPROCx;

	/*
	for (x = 2; x <= Nx1; x++)
	for (y = 2; y <= Ny1; y++)
	for (z = 2; z <= Nz1; z++)
	{
		if (if_polydisperse == 1)
		{
			for (k = 0; k < np_local_poly; k++)
			{
				if (num_p_poly[k] >= 0)
				{
					j = num_p_poly[k];
					rcj = R_poly[rc_poly[j]];
					xc_key[0] = xc[j][0] + 1 - me_x * Nx;
					xc_key[1] = xc[j][1] + 1 - me_y * Ny;
					xc_key[2] = xc[j][2] + 1;

					lxc = (x - xc_key[0]);
					lyc = (y - xc_key[1]);
					lzc = (z - xc_key[2]);
					lc = sqrt(lxc * lxc + lyc * lyc + lzc * lzc);
					if (lc <= rcj)
					{
						aveU[0] -= u[x][y][z][0];
						aveU[1] -= u[x][y][z][1];
						aveU[2] -= u[x][y][z][2];
						m_fluid--;
					}
				}
			}
		}
		else
		{
			for (k = 0; k < np_local; k++)
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					xc_key[0] = xc[j][0] + 1 - me_x * Nx;
					xc_key[1] = xc[j][1] + 1 - me_y * Ny;
					xc_key[2] = xc[j][2] + 1;

					lxc = (x - xc_key[0]);
					lyc = (y - xc_key[1]);
					lzc = (z - xc_key[2]);
					lc = sqrt(lxc * lxc + lyc * lyc + lzc * lzc);
					if (lc <= R)
					{
						aveU[0] -= u[x][y][z][0];
						aveU[1] -= u[x][y][z][1];
						aveU[2] -= u[x][y][z][2];
						m_fluid--;
					}
				}
			}
		}
	}
	*/
	if (if_polydisperse == 1)
	{
	#pragma omp parallel
		{
			double xc_key[3];

	#pragma omp for collapse(3) schedule(static) private(x,y,z,j,k,rcj,lc,lxc,lyc,lzc)\
											 reduction(+:local_m_fluid,local_aveU[0:3])
			for (x = 2; x <= Nx1; x++)
			{
				for (y = 2; y <= Ny1; y++)
				{
					for (z = 2; z <= Nz1; z++)
					{
						for (k = 0; k < np_local_poly; k++)
						{
							if (num_p_poly[k] >= 0)
							{
								j = num_p_poly[k];
								rcj = R_poly[rc_poly[j]];
								xc_key[0] = xc[j][0] + 1 - me_x * Nx;
								xc_key[1] = xc[j][1] + 1 - me_y * Ny;
								xc_key[2] = xc[j][2] + 1;

								lxc = (x - xc_key[0]);
								lyc = (y - xc_key[1]);
								lzc = (z - xc_key[2]);
								lc = sqrt(lxc * lxc + lyc * lyc + lzc * lzc);
								if (lc <= rcj)
								{
									//aveU[0] -= u[x][y][z][0];
									//aveU[1] -= u[x][y][z][1];
									//aveU[2] -= u[x][y][z][2];
									//m_fluid--;
                                    local_aveU[0] += u[x][y][z][0];
                                    local_aveU[1] += u[x][y][z][1];
                                    local_aveU[2] += u[x][y][z][2];
                                    local_m_fluid++;
								}
							}
						}
					}
				}
			}
		}//end openmp parallel
	}
	else
	{
	#pragma omp parallel
		{
			double xc_key[3];

	#pragma omp for collapse(3) schedule(static) private(x,y,z,j,k,lc,lxc,lyc,lzc)\
											 reduction(+:local_m_fluid,local_aveU[0:3])
			for (x = 2; x <= Nx1; x++)
			{
				for (y = 2; y <= Ny1; y++)
				{
					for (z = 2; z <= Nz1; z++)
					{
						for (k = 0; k < np_local; k++)
						{
							if (num_p[k] >= 0)
							{
								j = num_p[k];
								xc_key[0] = xc[j][0] + 1 - me_x * Nx;
								xc_key[1] = xc[j][1] + 1 - me_y * Ny;
								xc_key[2] = xc[j][2] + 1;

								lxc = (x - xc_key[0]);
								lyc = (y - xc_key[1]);
								lzc = (z - xc_key[2]);
								lc = sqrt(lxc * lxc + lyc * lyc + lzc * lzc);
								if (lc <= R)
								{
									//aveU[0] -= u[x][y][z][0];
									//aveU[1] -= u[x][y][z][1];
									//aveU[2] -= u[x][y][z][2];
									//m_fluid--;
									local_aveU[0] += u[x][y][z][0];
									local_aveU[1] += u[x][y][z][1];
									local_aveU[2] += u[x][y][z][2];
									local_m_fluid++;
								}
							}
						}
					}
				}
			}
		}//end openmp parallel
	}
	aveU[0] -= local_aveU[0];
	aveU[1] -= local_aveU[1];
	aveU[2] -= local_aveU[2];
	m_fluid -= local_m_fluid;
	MPI_Barrier(MPI_COMM_WORLD);
}

void static_fluid_velocity_2(int me)
{
	int me_x = me % NPROCx;
	int me_y = me / NPROCx;

	double local_aveUx = 0.;
	double local_aveUy = 0.;
    double local_aveUz = 0.;
	int local_m_fluid = 0;

	if (if_polydisperse == 1) 
	{
	#pragma omp parallel 
		{
			int j, k, rcj;
			double xc_key[3];
			int x_min, x_max, y_min, y_max, z_min, z_max;
			double lxc2, lyc2, lzc2,lc2;
			int x, y, z,z1;

	#pragma omp for schedule(dynamic, 5) private(x,y,z) reduction(+:local_aveUx, local_aveUy, local_aveUz, local_m_fluid)
			for (k = 0; k < np_local_poly; k++) 
			{
				if (num_p_poly[k] >= 0) 
				{
					j = num_p_poly[k];
					rcj = R_poly[rc_poly[j]];
					lc2 = rcj * rcj;

					// 将颗粒坐标转换到局部网格坐标
					xc_key[0] = xc[j][0] + 1 - me_x * Nx;
					xc_key[1] = xc[j][1] + 1 - me_y * Ny;
					xc_key[2] = xc[j][2] + 1;

					// 计算颗粒的边界盒
					x_min = (int)((2.0 > floor(xc_key[0] - rcj - 1)) ? 2.0 : floor(xc_key[0] - rcj - 1));
					x_max = (int)((Nx1 < ceil(xc_key[0] + rcj + 1)) ? Nx1 : ceil(xc_key[0] + rcj + 1));
					y_min = (int)((2.0 > floor(xc_key[1] - rcj - 1)) ? 2.0 : floor(xc_key[1] - rcj - 1));
					y_max = (int)((Ny1 < ceil(xc_key[1] + rcj + 1)) ? Ny1 : ceil(xc_key[1] + rcj + 1));
					//z_min = (int)((2.0 > floor(xc_key[2] - rcj - 1)) ? 2.0 : floor(xc_key[2] - rcj - 1));
					//z_max = (int)((Nz1 < ceil(xc_key[2] + rcj + 1)) ? Nz1 : ceil(xc_key[2] + rcj + 1));
					z_min = (int)( floor(xc_key[2] - rcj));
					z_max = (int)(ceil(xc_key[2] + rcj));

					// 只遍历边界盒内的网格点
					for (x = x_min; x <= x_max; x++) {
						lxc2 = (x - xc_key[0]);
						lxc2 = lxc2 * lxc2;
						for (y = y_min; y <= y_max; y++) {
							lyc2 = y - xc_key[1];
                            lyc2 = lyc2 * lyc2;
							for (z = z_min; z <= z_max; z++) {
								lzc2 = z - xc_key[2];
                                lzc2 = lzc2 * lzc2;

								if (lxc2 + lyc2 + lzc2 <= lc2) 
								{
									z1 = z;
									if (z1 <= 1)
									{
										z1 += Nz;
									}
									if (z1 >= Nz2)
									{
										z1 -= Nz;
									}
									local_aveUx += u[x][y][z1][0];
									local_aveUy += u[x][y][z1][1];
									local_aveUz += u[x][y][z1][2];
									local_m_fluid++;
								}
							}
						}
					}
				}
			}
		}//end openmp parallel
	}
	else 
	{
	#pragma omp parallel
		{
			int j,k;
			double xc_key[3];
			int x_min, x_max, y_min, y_max, z_min, z_max;
			double lxc2, lyc2, lzc2, lc2;
			int x, y, z,z1;

	#pragma omp for schedule(dynamic, 5) private(x,y,z) reduction(+:local_aveUx, local_aveUy, local_aveUz, local_m_fluid)
			for (k = 0; k < np_local; k++)
			{
				if (num_p[k] >= 0)
				{
					j = num_p[k];
					lc2 = R*R;

					// 将颗粒坐标转换到局部网格坐标
					xc_key[0] = xc[j][0] + 1 - me_x * Nx;
					xc_key[1] = xc[j][1] + 1 - me_y * Ny;
					xc_key[2] = xc[j][2] + 1;

					// 计算颗粒的边界盒
					x_min = (int)((2.0 > floor(xc_key[0] - R - 1)) ? 2.0 : floor(xc_key[0] - R - 1));
					x_max = (int)((Nx1 < ceil(xc_key[0] + R + 1)) ? Nx1 : ceil(xc_key[0] + R + 1));
					y_min = (int)((2.0 > floor(xc_key[1] - R - 1)) ? 2.0 : floor(xc_key[1] - R - 1));
					y_max = (int)((Ny1 < ceil(xc_key[1] + R + 1)) ? Ny1 : ceil(xc_key[1] + R + 1));
					//z_min = (int)((2.0 > floor(xc_key[2] - R - 1)) ? 2.0 : floor(xc_key[2] - R - 1));
					//z_max = (int)((Nz1 < ceil(xc_key[2] + R + 1)) ? Nz1 : ceil(xc_key[2] + R + 1));
					z_min = (int)(floor(xc_key[2] - R));
					z_max = (int)(ceil(xc_key[2] + R));

					// 只遍历边界盒内的网格点
					for (x = x_min; x <= x_max; x++) {
						lxc2 = (x - xc_key[0]);
						lxc2 = lxc2 * lxc2;
						for (y = y_min; y <= y_max; y++) {
							lyc2 = y - xc_key[1];
							lyc2 = lyc2 * lyc2;
							for (z = z_min; z <= z_max; z++) {
								lzc2 = z - xc_key[2];
								lzc2 = lzc2 * lzc2;

								if (lxc2 + lyc2 + lzc2 <= lc2)
								{
									z1 = z;
									if (z1 <= 1)
									{
										z1 += Nz;
									}
									if (z1 >= Nz2)
									{
										z1 -= Nz;
									}
									local_aveUx += u[x][y][z1][0];
									local_aveUy += u[x][y][z1][1];
									local_aveUz += u[x][y][z1][2];
									local_m_fluid++;
								}
							}
						}
					}
				}
			}
		}//end openmp parallel
	}

	aveU[0] -= local_aveUx;
	aveU[1] -= local_aveUy;
	aveU[2] -= local_aveUz;
	m_fluid -= local_m_fluid;
	MPI_Barrier(MPI_COMM_WORLD);
}

void relative_2p(int me)
{
	MPI_Status status;

	FILE *file;
	int i, j, k, kk, kk1;
	int nproc;

	if (me == 0)
	{
		if ((file = fopen("relative_u.dat", "a")) == NULL)
		{
			printf("can not open the file!");
			exit(0);
		}
		fprintf(file, "%f\t%f\t%f\n", step*t_stokes, (abs)(xc[0][0] - xc[1][0]+2*R), (uc[0][0] - uc[1][0]));
		fclose(file);
	}
}


//particle drag force coefficient
void save_fx_fy(int me)
{
    char fname_1[200];
	char fname_2[200];
	char fname_3[200];
	char fname_4[200];
	char fname_5[200];
	char fname_6[200];
	char fname_7[200];
	sprintf(fname_1, "%sfluid_u.dat", Data_folder);
	sprintf(fname_2, "%svar_u.dat", Data_folder);
	sprintf(fname_3, "%sparticle_u.dat", Data_folder);
	sprintf(fname_4, "%serror_u.dat", Data_folder);
	sprintf(fname_5, "%sparticle_u_stokes.dat", Data_folder);
	sprintf(fname_6, "%sparticle_umax.dat", Data_folder);
	sprintf(fname_7, "%svelocity_absmax.dat", Data_folder);

	MPI_Status status;

	FILE *file;
	int i, j, k, kk, kk1;
	double temp, temp1, var_u[3];
	double FluidUx_max = 0.0;
	double FluidUy_max = 0.0;
	double FluidUz_max = 0.0;

	double FluidMa_max = 0.0;
	double ParticleMa_max = 0.0;
	int nproc;
	double Par_out[np_local][19];
	int fluid_m;
	for (k = 0; k < np_local; k++)
	{
		Par_out[k][0] = -1.0;
	}

	MPI_Reduce(&m_fluid, &fluid_m, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

	MPI_Reduce(&aveU[0], &Uave[0], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&aveU[1], &Uave[1], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&aveU[2], &Uave[2], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	MPI_Reduce(&avepU[0], &Uavep[0], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&avepU[1], &Uavep[1], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&avepU[2], &Uavep[2], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	MPI_Reduce(&avepW[0], &Wavep[0], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&avepW[1], &Wavep[1], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&avepW[2], &Wavep[2], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	MPI_Reduce(&ux_max, &Ux_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	MPI_Reduce(&uy_max, &Uy_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	MPI_Reduce(&uz_max, &Uz_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	// fluid directional absolute maxima
	MPI_Reduce(&fluid_ux_max, &FluidUx_max,
		1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	MPI_Reduce(&fluid_uy_max, &FluidUy_max,
		1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	MPI_Reduce(&fluid_uz_max, &FluidUz_max,
		1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);


	// Mach-number maxima
	MPI_Reduce(&fluid_ma_max, &FluidMa_max,
		1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	MPI_Reduce(&particle_ma_max, &ParticleMa_max,
		1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	MPI_Reduce(&IBforce[0], &forceIB[0], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&IBforce[1], &forceIB[1], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&IBforce[2], &forceIB[2], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	//MPI_Allreduce(&u_max, &global_u_max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);

	if (me != 0)
	{
		kk1 = 0;
		for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
		{
			if (num_p[k] >= 0)
			{
				j = num_p[k];
				if (KeyProcess[k] == me)
				{
					Par_out[kk1][0] = 1.0*j;
					for (i = 0; i < 3; i++)
					{
						Par_out[kk1][i + 1] = xc[j][i];
						Par_out[kk1][i + 4] = uc[j][i];
						Par_out[kk1][i + 7] = ucc[j][i];
						Par_out[kk1][i + 10] = wc[j][i];
						Par_out[kk1][i + 13] = wcc[j][i];
						Par_out[kk1][i + 16] = sitac[j][i];
					}
					kk1++;
				}
			}
		}
		MPI_Send(&Par_out[0][0], np_local * 19, MPI_DOUBLE, 0, (NPROC + me), MPI_COMM_WORLD);
	}

	if (me == 0)
	{
		for (i = 0; i < 3; i++)
		{
			Uave[i] /= fluid_m;
			Uavep[i] /= np;
		}

		for (kk = 0; kk < 3; kk++)
			var_u[kk] = 0.0;

		//ͳ�Ʊ����ڵĿ����ٶȲ���
		for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
		{
			if (num_p[k] >= 0)
			{
				j = num_p[k];
				if (KeyProcess[k] == me)
				{
					for (kk = 0; kk < 3; kk++)
					{
						temp = (uc[j][kk] - Uavep[kk]);
						var_u[kk] += temp*temp;
					}
				}
			}
		}
		//ͳ���������ڿ����ٶȲ���
		for (nproc = 1; nproc < NPROC; nproc++)
		{
			MPI_Recv(&Par_out[0][0], np_local * 19, MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
			for (k = 0; k < np_local; k++)
			{
				//Receive frome others, me is 0
				if (Par_out[k][0] >= 0)
				{
					j = (int)Par_out[k][0];
					for (kk = 0; kk < 3; kk++)
					{
						temp = (Par_out[k][kk + 4] - Uavep[kk]);
						var_u[kk] += temp*temp;
					}
				}
			}
		}

		double temp_error[3];
		double Error_deri[3];

		for (i = 0; i < 3; i++)
		{
#if (if_stationary == 0)
				temp_error[i] = fai_p * Uavep[i] + (1 - fai_p) * Uave[i];   //����ƽ���ٶ�
#else
			{
				temp_error[i] = Uave[i] + Res_target * G_dire[i] * mv / D / (1 - fai_p);
				if (t <=1)
					Ff_body[i] = 0;
			}
#endif
			Error_int[i] += temp_error[i] * dt;
			Error_deri[i] = (temp_error[i] - Error_U[i]) / dt;

			Ff_body[i] += K_pid * (temp_error[i] + Error_int[i] * T_inte + Error_deri[i] * T_deri);

			Error_U[i] = temp_error[i];

			forceIB[i] /= np;
		}

		if (step % 1 == 0)
		{
			//�������ƽ���ٶ�
			if ((file = fopen(fname_1, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}
			fprintf(file, "%f\t%d\t%f\t%f\t%f\t%d\n", step*t_stokes, step, Uave[0], Uave[1], Uave[2], fluid_m);
			fclose(file);

			//����ٶȲ���
			if ((file = fopen(fname_2, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}
			fprintf(file, "%f\t%d\t%f\t%f\t%f\n", step*t_stokes, step, var_u[0] / np, var_u[1] / np, var_u[2] / np);
			fclose(file);

			if ((file = fopen(fname_3, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}

			//fprintf(file, "%d\t%f\t%f\t%f\t%f\t%f\t%f\n", step, Uavep[0] / np, Uavep[1] / np, Uavep[2] / np, Wavep[0] / np, Wavep[1] / np, Wavep[2] / np);
			temp = sqrt(Uavep[0] * Uavep[0] + Uavep[1] * Uavep[1] + Uavep[2] * Uavep[2]);
			temp1 = sqrt(forceIB[0] * forceIB[0] + forceIB[1] * forceIB[1] + forceIB[2] * forceIB[2]);

			fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n", step*t_stokes, Uavep[0], Uavep[1], Uavep[2], temp, forceIB[0], forceIB[1], forceIB[2], temp1);
			fclose(file);

			if ((file = fopen(fname_4, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}
			temp = sqrt(Ff_body[0] * Ff_body[0] + Ff_body[1] * Ff_body[1] + Ff_body[2] * Ff_body[2]);
			fprintf(file, "%f\t%f\t%f\t%f\t%f\n", step*t_stokes, Error_U[0], Error_U[1], Error_U[2], temp);
			fclose(file);

			if ((file = fopen(fname_5, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}

			if (step >= step_stokes)
			{
				Uave_stokes[0] += Uavep[0];
				Uave_stokes[1] += Uavep[1];
				Uave_stokes[2] += Uavep[2];

				temp = sqrt(Uave_stokes[0] * Uave_stokes[0] + Uave_stokes[1] * Uave_stokes[1] + Uave_stokes[2] * Uave_stokes[2]);
				temp1 = (step - step_stokes + 1) / 50;

				fprintf(file, "%f\t%f\t%f\t%f\t%f\n", step*t_stokes, Uave_stokes[0] / temp1, Uave_stokes[1] / temp1, Uave_stokes[2] / temp1, temp / temp1);
			}
			fclose(file);

			if ((file = fopen(fname_6, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}
			fprintf(file, "%f\t%f\t%f\t%f\n", step*t_stokes, Ux_max, Uy_max, Uz_max);
			fclose(file);

			if ((file = fopen(fname_7, "a")) == NULL)
			{
				fprintf(stderr, "Cannot open velocity_absmax.dat\n");
				exit(0);
			}

			fprintf(file,
				"%d\t"
				"%.12e\t%.12e\t%.12e\t%.12e\t"
				"%.12e\t%.12e\t%.12e\t%.12e\n",
				step,

				FluidUx_max,
				FluidUy_max,
				FluidUz_max,
				FluidMa_max,

				Ux_max,
				Uy_max,
				Uz_max,
				ParticleMa_max);

			fclose(file);
		}
	}
	MPI_Bcast(&Ff_body[0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
}

void save_fx_fy_poly(int me)
{
	char fname_1[200];
	char fname_2[200];
	char fname_3[200];
	char fname_4[200];
	char fname_5[200];
	char fname_6[200];
	char fname_7[200];
	sprintf(fname_1, "%sfluid_u.dat", Data_folder);
	sprintf(fname_2, "%svar_u.dat", Data_folder);
	sprintf(fname_3, "%sparticle_u.dat", Data_folder);
	sprintf(fname_4, "%serror_u.dat", Data_folder);
	sprintf(fname_5, "%sparticle_u_stokes.dat", Data_folder);
	sprintf(fname_6, "%sparticle_umax.dat", Data_folder);
	sprintf(fname_7, "%svelocity_absmax.dat", Data_folder);

	MPI_Status status;

	FILE* file;
	int i, j, k, kk, kk1;
	double temp, temp1, var_u[3];
	double FluidUx_max = 0.0;
	double FluidUy_max = 0.0;
	double FluidUz_max = 0.0;

	double FluidMa_max = 0.0;
	double ParticleMa_max = 0.0;
	int nproc;
	double Par_out[np_local_poly][19];
	int fluid_m;
	for (k = 0; k < np_local_poly; k++)
	{
		Par_out[k][0] = -1.0;
	}

	MPI_Reduce(&m_fluid, &fluid_m, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

	MPI_Reduce(&aveU[0], &Uave[0], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&aveU[1], &Uave[1], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&aveU[2], &Uave[2], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	MPI_Reduce(&avepU[0], &Uavep[0], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&avepU[1], &Uavep[1], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&avepU[2], &Uavep[2], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	MPI_Reduce(&avepW[0], &Wavep[0], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&avepW[1], &Wavep[1], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&avepW[2], &Wavep[2], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	MPI_Reduce(&ux_max, &Ux_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	MPI_Reduce(&uy_max, &Uy_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	MPI_Reduce(&uz_max, &Uz_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	// fluid directional absolute maxima
	MPI_Reduce(&fluid_ux_max, &FluidUx_max,
		1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	MPI_Reduce(&fluid_uy_max, &FluidUy_max,
		1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	MPI_Reduce(&fluid_uz_max, &FluidUz_max,
		1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);


	// Mach-number maxima
	MPI_Reduce(&fluid_ma_max, &FluidMa_max,
		1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	MPI_Reduce(&particle_ma_max, &ParticleMa_max,
		1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

	MPI_Reduce(&IBforce[0], &forceIB[0], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&IBforce[1], &forceIB[1], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&IBforce[2], &forceIB[2], 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	//MPI_Allreduce(&u_max, &global_u_max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);

	if (me != 0)
	{
		kk1 = 0;
		for (k = 0; k < np_local_poly; k++)  //���ݾֲ���ż���
		{
			if (num_p_poly[k] >= 0)
			{
				j = num_p_poly[k];
				if (KeyProcess[k] == me)
				{
					Par_out[kk1][0] = 1.0 * j;
					for (i = 0; i < 3; i++)
					{
						Par_out[kk1][i + 1] = xc[j][i];
						Par_out[kk1][i + 4] = uc[j][i];
						Par_out[kk1][i + 7] = ucc[j][i];
						Par_out[kk1][i + 10] = wc[j][i];
						Par_out[kk1][i + 13] = wcc[j][i];
						Par_out[kk1][i + 16] = sitac[j][i];
					}
					kk1++;
				}
			}
		}
		MPI_Send(&Par_out[0][0], np_local_poly * 19, MPI_DOUBLE, 0, (NPROC + me), MPI_COMM_WORLD);
	}

	if (me == 0)
	{
		for (i = 0; i < 3; i++)
		{
			Uave[i] /= fluid_m;
			Uavep[i] /= (par_volume_poly * dx_poly * dx_poly * dx_poly);//particle velocity average by there particle volume
		}

		for (kk = 0; kk < 3; kk++)
			var_u[kk] = 0.0;

		//ͳ�Ʊ����ڵĿ����ٶȲ���
		for (k = 0; k < np_local_poly; k++)  //���ݾֲ���ż���
		{
			if (num_p_poly[k] >= 0)
			{
				j = num_p_poly[k];
				if (KeyProcess_poly[k] == me)
				{
					for (kk = 0; kk < 3; kk++)
					{
						temp = (uc[j][kk] - Uavep[kk]);
						var_u[kk] += temp * temp * pow((R_poly[rc_poly[j]]), 3) * 4. / 3. * PI;
					}
				}
			}
		}
		//ͳ���������ڿ����ٶȲ���
		for (nproc = 1; nproc < NPROC; nproc++)
		{
			MPI_Recv(&Par_out[0][0], np_local_poly * 19, MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
			for (k = 0; k < np_local_poly; k++)
			{
				//Receive frome others, me is 0
				if (Par_out[k][0] >= 0)
				{
					j = (int)Par_out[k][0];
					for (kk = 0; kk < 3; kk++)
					{
						temp = (Par_out[k][kk + 4] - Uavep[kk]);
						var_u[kk] += temp * temp * pow((R_poly[rc_poly[j]]), 3) * 4. / 3. * PI;
					}
				}
			}
		}

		double temp_error[3];
		double Error_deri[3];

		for (i = 0; i < 3; i++)
		{
#if (if_stationary == 0)
				temp_error[i] = phi_p_poly * Uavep[i] + (1 - phi_p_poly) * Uave[i];   //����ƽ���ٶ�
#else
			{
				temp_error[i] = Uave[i] + Res_target * G_dire[i] * mv / (R_sauter_mean * 2 * dx_poly) / (1 - phi_p_poly);
				if (t <= 1)
					Ff_body_poly[i] = 0;
			}
#endif
			Error_int[i] += temp_error[i] * dt;
			Error_deri[i] = (temp_error[i] - Error_U[i]) / dt;

			Ff_body_poly[i] += K_pid * (temp_error[i] + Error_int[i] * T_inte + Error_deri[i] * T_deri);

			Error_U[i] = temp_error[i];

			forceIB[i] /= np;
		}

		if (step % 1 == 0)
		{
			//�������ƽ���ٶ�
			if ((file = fopen(fname_1, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}
			fprintf(file, "%f\t%d\t%f\t%f\t%f\t%d\n", step * t_stokes, step, Uave[0], Uave[1], Uave[2], fluid_m);
			fclose(file);

			//����ٶȲ���
			if ((file = fopen(fname_2, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}
			for (kk = 0; kk < 3; kk++)
			{
				var_u[kk] = var_u[kk] / par_volume_poly;
			}
			fprintf(file, "%f\t%d\t%f\t%f\t%f\n", step * t_stokes, step, var_u[0], var_u[1], var_u[2]);
			fclose(file);

			if ((file = fopen(fname_3, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}

			//fprintf(file, "%d\t%f\t%f\t%f\t%f\t%f\t%f\n", step, Uavep[0] / np, Uavep[1] / np, Uavep[2] / np, Wavep[0] / np, Wavep[1] / np, Wavep[2] / np);
			temp = sqrt(Uavep[0] * Uavep[0] + Uavep[1] * Uavep[1] + Uavep[2] * Uavep[2]);
			temp1 = sqrt(forceIB[0] * forceIB[0] + forceIB[1] * forceIB[1] + forceIB[2] * forceIB[2]);

			fprintf(file, "%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n", step * t_stokes, Uavep[0], Uavep[1], Uavep[2], temp, forceIB[0], forceIB[1], forceIB[2], temp1);
			fclose(file);

			if ((file = fopen(fname_4, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}
			temp = sqrt(Ff_body_poly[0] * Ff_body_poly[0] + Ff_body_poly[1] * Ff_body_poly[1] + Ff_body_poly[2] * Ff_body_poly[2]);
			fprintf(file, "%f\t%f\t%f\t%f\t%f\n", step * t_stokes, Error_U[0], Error_U[1], Error_U[2], temp);
			fclose(file);

			if ((file = fopen(fname_5, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}

			if (step >= step_stokes)
			{
				Uave_stokes[0] += Uavep[0];
				Uave_stokes[1] += Uavep[1];
				Uave_stokes[2] += Uavep[2];

				temp = sqrt(Uave_stokes[0] * Uave_stokes[0] + Uave_stokes[1] * Uave_stokes[1] + Uave_stokes[2] * Uave_stokes[2]);
				temp1 = (step - step_stokes + 1) / 50;

				fprintf(file, "%f\t%f\t%f\t%f\t%f\n", step * t_stokes, Uave_stokes[0] / temp1, Uave_stokes[1] / temp1, Uave_stokes[2] / temp1, temp / temp1);
			}
			fclose(file);

			if ((file = fopen(fname_6, "a")) == NULL)
			{
				printf("can not open the file!");
				exit(0);
			}
			fprintf(file, "%f\t%f\t%f\t%f\n", step * t_stokes, Ux_max, Uy_max, Uz_max);
			fclose(file);

			if ((file = fopen(fname_7, "a")) == NULL)
			{
				fprintf(stderr, "Cannot open velocity_absmax.dat\n");
				exit(0);
			}

			fprintf(file,
				"%d\t"
				"%.12e\t%.12e\t%.12e\t%.12e\t"
				"%.12e\t%.12e\t%.12e\t%.12e\n",
				step,

				FluidUx_max,
				FluidUy_max,
				FluidUz_max,
				FluidMa_max,

				Ux_max,
				Uy_max,
				Uz_max,
				ParticleMa_max);

			fclose(file);
		}
	}
	MPI_Bcast(&Ff_body_poly[0], 3, MPI_DOUBLE, 0, MPI_COMM_WORLD);
}

void save_particle_surface(int me)
{
	MPI_Status status;
	FILE *fp;
	char fname[200];
	int i, j, k, p, kk1;
	int x, y, z;
	double x1, y1, z1, x2, y2, z2, x3, y3, z3, x4, y4, z4;
	double n = dx;
	int nproc;

	int ns_out, num_out;
	ns_out = 15;
	int nt_out[ns_out];
	double xb_out[16][ns_out], yb_out[16][ns_out], zb_out[16][ns_out];
	double sita, alpha;

	num_out = 0;
	for (i = 0; i <= 15; i++)
	for (j = 0; j < ns_out; j++)
	{
		alpha = i * 2 * PI / 15;
		sita = j * PI / (ns_out - 1);

		if (j == 0)    //���ϼ������ϵ�
		{
			xb_out[i][j] = 0.0;
			yb_out[i][j] = -R;
			zb_out[i][j] = 0.0;
		}
		else if (j == (ns_out - 1))
		{
			xb_out[i][j] = 0.0;
			yb_out[i][j] = R;
			zb_out[i][j] = 0.0;
		}
		else
		{
			xb_out[i][j] = R*sin(sita)*cos(alpha);
			yb_out[i][j] = -R*cos(sita);
			zb_out[i][j] = R*sin(sita)*sin(alpha);
		}
	}

	double Par_out[np_local][7];
	for (k = 0; k < np_local; k++)
	{
		Par_out[k][0] = -1.0;
	}
	if (me != 0)
	{
		kk1 = 0;
		for (k = 0; k < np_local; k++)  //���ݾֲ���ż���
		{
			if (num_p[k] >= 0)
			{
				j = num_p[k];
				if (KeyProcess[k] == me)
					//if ((xc[j][0] >= xx) && (xc[j][0] < xxn) && (xc[j][1] >= yy) && (xc[j][1] < yyn))
				{
					Par_out[kk1][0] = 1.0*j;
					for (i = 0; i < 3; i++)
					{
						Par_out[kk1][i + 1] = xc[j][i];
						Par_out[kk1][i + 4] = sitac[j][i];
					}
					kk1++;
				}
			}
		}
		MPI_Send(&Par_out[0][0], np_local * 7, MPI_DOUBLE, 0, (NPROC + me), MPI_COMM_WORLD);
	}


	if (me == 0)
	{
		sprintf(fname, "%sparticle_surface_%06d.dat", Data_folder, step);
		fp = fopen(fname, "w");

		fprintf(fp, "TITLE=part_surface3D-p\n");
		fprintf(fp, "VARIABLES=x,y,z,u,v,w,p\n");
		//fprintf(fp, "VARIABLES=x,y,z,s\n");
		//fprintf(fp, "Zone I= %d,J= %d,F= POINT \n", ns_out, 16);

		for (k = 0; k < np_local; k++)
		{
			if (num_p[k] >= 0)
			{
				p = num_p[k];
				if (KeyProcess[k] == me)
				{
					fprintf(fp, "Zone I= %d,J= %d,F= POINT \n", ns_out, 16);
					for (i = 0; i <= 15; i++)
					for (j = 0; j < ns_out; j++)
					{
						//��z����ת						
						x1 = xb_out[i][j] * cos(sitac[p][2]) - yb_out[i][j] * sin(sitac[p][2]);
						y1 = xb_out[i][j] * sin(sitac[p][2]) + yb_out[i][j] * cos(sitac[p][2]);
						z1 = zb_out[i][j];
						//��x����ת
						x2 = x1;
						y2 = y1 * cos(sitac[p][0]) - z1 * sin(sitac[p][0]);
						z2 = y1 * sin(sitac[p][0]) + z1 * cos(sitac[p][0]);
						//��y����ת
						x3 = z2 * sin(sitac[p][1]) + x2 * cos(sitac[p][1]);
						y3 = y2;
						z3 = z2 * cos(sitac[p][1]) - x2 * sin(sitac[p][1]);

						x4 = xc[p][0] + x3;
						y4 = xc[p][1] + y3;
						z4 = xc[p][2] + z3;

						fprintf(fp, "%f\t%f\t%f\t%d\t%d\t%d\t%d\n", x4 * n, y4 * n, z4 * n, 0, 0, 0, j);
					}
				}
			}
		}

		//������������ڿ�������
		for (nproc = 1; nproc < NPROC; nproc++)
		{
			MPI_Recv(&Par_out[0][0], np_local * 7, MPI_DOUBLE, nproc, (NPROC + nproc), MPI_COMM_WORLD, &status);
			for (k = 0; k < np_local; k++)
			{
				//Receive frome others, me is 0
				if (Par_out[k][0] >= 0)
				{
					//p = (int)Par_out[k][0];
					fprintf(fp, "Zone I= %d,J= %d,F= POINT \n", ns_out, 16);
					for (i = 0; i <= 15; i++)
					for (j = 0; j < ns_out; j++)
					{
						//��z����ת						
						x1 = xb_out[i][j] * cos(Par_out[k][6]) - yb_out[i][j] * sin(Par_out[k][6]);
						y1 = xb_out[i][j] * sin(Par_out[k][6]) + yb_out[i][j] * cos(Par_out[k][6]);
						z1 = zb_out[i][j];
						//��x����ת
						x2 = x1;
						y2 = y1 * cos(Par_out[k][4]) - z1 * sin(Par_out[k][4]);
						z2 = y1 * sin(Par_out[k][4]) + z1 * cos(Par_out[k][4]);
						//��y����ת
						x3 = z2 * sin(Par_out[k][5]) + x2 * cos(Par_out[k][5]);
						y3 = y2;
						z3 = z2 * cos(Par_out[k][5]) - x2 * sin(Par_out[k][5]);

						x4 = Par_out[k][1] + x3;
						y4 = Par_out[k][2] + y3;
						z4 = Par_out[k][3] + z3;

						fprintf(fp, "%f\t%f\t%f\t%d\t%d\t%d\t%d\n", x4 * n, y4 * n, z4 * n, 0, 0, 0, j);
					}
				}
			}
		}

		//frame
		fprintf(fp, "Zone I= %d,J= %d,K= %d,F= POINT\n", 2, 2, 2);
		for (x = 0; x <= NxT; x = x + NxT)
		for (y = 0; y <= NyT; y = y + NyT)
		for (z = 0; z <= NzT; z = z + NzT)
			fprintf(fp, "%f\t%f\t%f\t%d\t%d\t%d\t%d\n", x*n, y*n, z*n, 0, 0, 0, 0);


		/*
		int dw_boun;
		double sita;
		dw_boun = (int)(2 * PI*R_boun / 5);
		double xb_boun[3];

		fprintf(fp, "Zone I= %d,J= %d,F= POINT \n", dw_boun+1, 2);
		for (i = 0; i <= NxT; i += NxT)
		for (j = 0; j <= dw_boun; j++)
		{
		sita = j * 2 * PI / dw_boun;
		xb_boun[0] = i;
		xb_boun[1] = 0.5*Ny + R_boun*cos(sita);
		xb_boun[2] = 0.5*Nz + R_boun*sin(sita);
		fprintf(fp, "%.2f\t%.2f\t%.2f\t%d\n", xb_boun[0] * n, xb_boun[1] * n, xb_boun[2] * n,0);
		}
		*/
		fclose(fp);
	}
}

/*
void save_surface_pressure(int me)
{
	FILE *fp;
	char fname[20];
	int i, j, k;
	double coef_pf;
	double n;
	n = 10000 * dx;
	
	for (k = 0; k < np; k++)
	{
			if (me == KeyProcess[k]) 
				{
					sprintf(fname, "particle_pressure_%06d_%02d.dat", step, (k+1));
		      fp = fopen(fname, "w");
		
		      fprintf(fp, "TITLE=part_pressure3D-p\n");
		      fprintf(fp, "VARIABLES=x,y,z,p\n");
		      fprintf(fp, "Zone I= %d,J= %d,F= POINT \n", np, num_points);
		      
		      for (i = 0; i < ns; i++)
			    {
				    for (j = 0; j < nt[i]; j++)
				    {
					    coef_pf = (pf[k][i][j] - p0) / (0.5*rhof*Ua*Ua);
					    fprintf(fp, "%.2f\t%.2f\t%.2f\t%f\n", (xb[i][j] + xc[k][0]) * n, (yb[i][j] + xc[k][1]) * n, (zb[i][j] + xc[k][2]) * n, coef_pf);
				     }
			    }
			    
			    fclose(fp);
			}			
	}		
}
*/

//--------------------------------------------------------------------------------------------------------
//sub-function
double fequation(double u[3], double p, int i)
{
	double m, n, uu, feq;
	m = (e[i][0] * c* u[0] + e[i][1] * c* u[1] + e[i][2] * c* u[2]) / (c*c);
	uu = (u[0] * u[0] + u[1] * u[1] + u[2] * u[2]) / (c*c);
	n = 3.0*m + 4.5*m*m - 1.5*uu;
	if (i == 0) feq = rhof + (w[i] - 1)*p / (cs*cs) + w[i] * n;
	else feq = w[i] * (p / (cs*cs) + n);
	return feq;
}

//2-point function(bilinear interpolation), satisfy the zeroth and first disrete moment conditions,is second-order approximation. Kang & Hassan, IJNMF, 2011.
double delta_2p(double r)
{
	double result;
	r = fabs(r);
	if (r <= 1.0)
	{
		result = 1 - r;
	}
	else result = 0.0;
	return result;
}

//3-point function
double delta_3p(double r)
{
	double result;
	r = fabs(r);
	if (r <= 0.5)
	{
		result = (1.0 + sqrt(1.0 - 3.0*r*r)) / 3.0;
	}
	else if (r <= 1.5)
	{
		result = (5.0 - 3.0*r - sqrt(1.0 - 3.0*(1.0 - r)*(1.0 - r))) / 6.0;
	}
	else result = 0.0;
	return result;
}

void delta_3p_less(double r, double result[3])
{
	double s = sqrt(1.0 - 3.0 * r * r);
    
    result[0] = (2.0 - 3.0 * r - s) / 6.0;
    result[1] = (1.0 + s) / 3.0;
    result[2] = (2.0 + 3.0 * r - s) / 6.0;
}
//4-point function, atisfy the zeroth and first disrete moment conditions, is second-order approximation. Kang & Hassan, IJNMF, 2011.
double delta_4p(double r)
{
	double result;
	r = fabs(r);
	if (r < 1.0)
	{
		result = (3.0 - 2.0*r + sqrt(1.0 + 4.0*r - 4 * r*r)) / 8.0;
	}
	else if (r < 2.0)
	{
		result = (5.0 - 2.0*r - sqrt(12.0*r - 7 - 4 * r*r)) / 8.0;
	}
	else result = 0.0;
	return result;
}
// cosine-type 4-point function, only satisfies the zeroth discrete moment condition, Wu & Aidun,IJNMF,2010.ֻ��һ�׾��ȡ�
double delta_4pcos(double r)
{
	double result;
	r = fabs(r);
	if (r <= 2)
	{
		result = (1.0 + cos(PI*r / 2)) / 4;
	}
	else result = 0.0;
	return result;
}

void Fluid_Boundary_circle(int me)                  //Բ�ν��������߽����������������ڣ���Χ����
{
	int x, y, z, i, j, Nx_boun, dw_boun;	
	double xf, yf, zf, lx, ly, lz, rf, xL, yL, zL;
	double sita,LP_boun_area;
	Nx_boun = 2 * Nx2 - 1;
	dw_boun = (int)(2 * PI*R_boun / 0.5);
	LP_boun_area = 0.5*(2 * PI*R_boun / dw_boun);

	double xb_boun[Nx_boun][dw_boun][3], uf_boun[Nx_boun][dw_boun][3], force_boun[Nx_boun][dw_boun][3];
	double delta_u, delta_f;
	double fluid_f[3];
	
	for (i = 0; i < Nx_boun; i++)
	for (j = 0; j < dw_boun; j++)
	{
		sita = j * 2 * PI / dw_boun;
		xb_boun[i][j][0] = (i + 1)*0.5;
		xb_boun[i][j][1] = 0.5*Ny+R_boun*cos(sita);
		xb_boun[i][j][2] = 0.5*Nz+R_boun*sin(sita);
		uf_boun[i][j][0] = 0.0;
		uf_boun[i][j][1] = 0.0;
		uf_boun[i][j][2] = 0.0;
	}
		
	for (i = 0; i < Nx_boun; i++)
	for (j = 0; j < dw_boun; j++)
	{	
		for (x = int(xb_boun[i][j][0] - 1); x <= int(xb_boun[i][j][0] + 1); x++)
		for (y = int(xb_boun[i][j][1] - 1); y <= int(xb_boun[i][j][1] + 1); y++)
		for (z = int(xb_boun[i][j][2] - 1); z <= int(xb_boun[i][j][2] + 1); z++)
		{
			lx = x - xb_boun[i][j][0];
			ly = y - xb_boun[i][j][1];
			lz = z - xb_boun[i][j][2];
			delta_u = (delta_2p(lx)*delta_2p(ly)*delta_2p(lz));
			uf_boun[i][j][0] += u[x][y][z][0] * delta_u;
			uf_boun[i][j][1] += u[x][y][z][1] * delta_u;
			uf_boun[i][j][2] += u[x][y][z][2] * delta_u;
		}
		force_boun[i][j][0] = 2 * rhof * uf_boun[i][j][0] / dt;
		force_boun[i][j][1] = 2 * rhof * uf_boun[i][j][1] / dt;
		force_boun[i][j][2] = 2 * rhof * uf_boun[i][j][2] / dt;
	}

	for (x = 1; x < Nx; x++)   
	for (y = 1; y < Ny; y++)    
	for (z = 1; z < Nz; z++)
	{
		fluid_f[0] = 0.0; fluid_f[1] = 0.0; fluid_f[2] = 0.0;
		yf = (y - 0.5*Ny); zf = (z - 0.5*Nz); rf = sqrt(yf*yf + zf*zf);
		if (rf <= (R_boun + 1.42) && rf >= (R_boun - 1.42))
		{
			for (i = 0; i < Nx_boun; i++)
			{
				if ((xb_boun[i][j][0] <= (x + 1)) && (xb_boun[i][j][0] >= (x - 1)))
				{
					for (j = 0; j < dw_boun; j++)
					{
						xL = x - xb_boun[i][j][0]; yL = y - xb_boun[i][j][1]; zL = z - xb_boun[i][j][2];
						if (fabs(yL) <= 1.0&&fabs(zL) <= 1.0)
						{
							delta_f = delta_2p(xL)*delta_2p(yL)*delta_2p(zL)*(LP_dr_2p*LP_boun_area);
							fluid_f[0] -= force_boun[i][j][0] * delta_f;
							fluid_f[1] -= force_boun[i][j][1] * delta_f;
							fluid_f[2] -= force_boun[i][j][2] * delta_f;
						}
					}
				}				
			}
			
		}
		forcef[x][y][z][0] += fluid_f[0];
		forcef[x][y][z][1] += fluid_f[1];
		forcef[x][y][z][2] += fluid_f[2];
	}
}

void save_fluid_boundary(int me)
{
	FILE *fp;
	char fname[20];
	double n;
	n = 10000 * dx;
	int x, y, z, i, j, Nx_boun, dw_boun;
	double sita;
	Nx_boun = 2 * Nx2 - 1;
	dw_boun = (int)(2 * PI*R_boun/5);

	double xb_boun[3];
	
	if (me == 0)
	{
		sprintf(fname, "fluid_boundary_%06d.dat", step);
		fp = fopen(fname, "w");

		fprintf(fp, "TITLE=fluid_boundary3D-p\n");
		fprintf(fp, "VARIABLES=x,y,z\n");
		fprintf(fp, "Zone I= %d,J= %d,F= POINT \n", dw_boun+1, 2);
		for (i = 0; i <= NxT; i += NxT)
		for (j = 0; j <= dw_boun; j++)
		{
			sita = j * 2 * PI / dw_boun;
			xb_boun[0] = i;
			xb_boun[1] = 0.5*Ny + R_boun*cos(sita);
			xb_boun[2] = 0.5*Nz + R_boun*sin(sita);
			fprintf(fp, "%.2f\t%.2f\t%.2f\n", xb_boun[0] * n, xb_boun[1] * n, xb_boun[2] * n);
		}		

		fclose(fp);
	}
}

void CPU_time(int me)
{
	FILE *file;

	int nproc,i;
	int msgtag;
	MPI_Status status;
	double u_p_time0, com1_time0, IBM_time0, Fcolli_time0, com2_time0, stream_time0, Pcolli_time0, Pmove_time0;
	double total;
	double time_CPU[10];
	
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

		MPI_Send(&time_CPU[0], 10, MPI_DOUBLE, 0, msgtag, MPI_COMM_WORLD);
	}
	
	if (me == 0)
	{
		if ((file = fopen("CPU_time.dat", "a")) == NULL)
		{
			printf("can not open the file!");
			exit(0);
		}
		fprintf(file, "step = %d\n",step);
		fprintf(file, "me\tu_p\tcom1\tIBM\tcom2_time\tFcolli\tcom3_time\tstream\tPcolli\tPmove\tcom4\ttotal\n");

		total = u_p_time + com1_time + IBM_time + Fcolli_time + com2_time + com3_time + com4_time + stream_time + Pcolli_time + Pmove_time;

		fprintf(file, "%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n", me, u_p_time, com1_time, IBM_time, com2_time, Fcolli_time, com3_time, stream_time, Pcolli_time, Pmove_time, com4_time, total);

		for (nproc = 1; nproc < NPROC; nproc++)
		{
			msgtag = nproc;
			MPI_Recv(&time_CPU[0], 10, MPI_DOUBLE, nproc, msgtag, MPI_COMM_WORLD, &status);

			total = 0;
			for (i = 0; i < 10; i++)
				total += time_CPU[i];
			fprintf(file, "%d\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n", nproc, time_CPU[0], time_CPU[1], time_CPU[2], time_CPU[3], time_CPU[4], time_CPU[5], time_CPU[6], time_CPU[7], time_CPU[8], time_CPU[9], total);
		}

		fclose(file);
	}
}

void CPU_time2(int me)
{
	FILE* file;

	int nproc, i;
	int msgtag;
	MPI_Status status;
	double u_p_time0, com1_time0, IBM_time0, Fcolli_time0, com2_time0, stream_time0, Pcolli_time0, Pmove_time0;
	double total;
	double time_CPU[14];

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
		//time_CPU[10] = collision_change_time;
		time_CPU[11] = f_pack_time;
        time_CPU[12] = f_change_time;
        time_CPU[13] = f_unpack_time;

		MPI_Send(&time_CPU[0], 14, MPI_DOUBLE, 0, msgtag, MPI_COMM_WORLD);
	}

	if (me == 0)
	{
		if ((file = fopen("CPU_time.dat", "a")) == NULL)
		{
			printf("can not open the file!");
			exit(0);
		}
		fprintf(file, "step = %d\n", step);
		fprintf(file, "me\tu_p\tcom1\tIBM\tcom2_time\tFcolli\tcollision_change\tcom3_time\tstream\tPcolli\tPmove\tcom4\ttotal\n");

		total = u_p_time + com1_time + IBM_time + Fcolli_time + com2_time + com3_time + com4_time + stream_time + Pcolli_time + Pmove_time;

		fprintf(file, "%d\t%f\t%f\t%f\t%f\t%f\t | %f\t%f\t%f\t%f | \t%f\t%f\t%f\t%f\t%f\n", me, u_p_time, com1_time, IBM_time, com2_time, Fcolli_time, com3_time, f_pack_time, f_change_time, f_unpack_time, stream_time, Pcolli_time, Pmove_time, com4_time, total);

		for (nproc = 1; nproc < NPROC; nproc++)
		{
			msgtag = nproc;
			MPI_Recv(&time_CPU[0], 14, MPI_DOUBLE, nproc, msgtag, MPI_COMM_WORLD, &status);

			total = 0;
			for (i = 0; i < 10; i++)
				total += time_CPU[i];
			fprintf(file, "%d\t%f\t%f\t%f\t%f\t%f\t | %f\t%f\t%f\t%f | \t%f\t%f\t%f\t%f\t%f\n", nproc, time_CPU[0], time_CPU[1], time_CPU[2], time_CPU[3], time_CPU[4], time_CPU[5], time_CPU[11], time_CPU[12], time_CPU[13], time_CPU[6], time_CPU[7], time_CPU[8], time_CPU[9], total);
		}

		fclose(file);
	}
}

#if if_gptl
static void gptl_check(int result, const char *operation, const char *name)
{
    if (result != 0) {
        int rank;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        fprintf(stderr, "Rank %d: %s failed for GPTL timer %s\n",
                rank, operation, name);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
}

static void gptl_start(GptlTimer timer)
{
    gptl_check(GPTLstart(gptl_timer_names[timer]), "GPTLstart",
               gptl_timer_names[timer]);
    gptl_timer_used[timer] = true;
}

static void gptl_stop(GptlTimer timer)
{
    gptl_check(GPTLstop(gptl_timer_names[timer]), "GPTLstop",
               gptl_timer_names[timer]);
}

static double gptl_elapsed(GptlTimer timer)
{
    // Stationary simulations do not execute the DEM timers: output zero.
    if (!gptl_timer_used[timer]) return 0.0;
    double seconds = 0.0;
    gptl_check(GPTLget_wallclock(gptl_timer_names[timer], 0, &seconds),
               "GPTLget_wallclock", gptl_timer_names[timer]);
    return seconds;
}

void CPU_time_gptl(int me)
{
    double time_CPU[13] = {};
    for (int i = 0; i < 13; ++i)
        time_CPU[i] = gptl_elapsed(static_cast<GptlTimer>(i));

    // Preserve the old counters for any existing code that reads them.
    u_p_time = time_CPU[0]; com1_time = time_CPU[1]; IBM_time = time_CPU[2];
    com2_time = time_CPU[3]; Fcolli_time = time_CPU[4]; com3_time = time_CPU[5];
    stream_time = time_CPU[6]; Pcolli_time = time_CPU[7];
    Pmove_time = time_CPU[8]; com4_time = time_CPU[9];
    f_pack_time = time_CPU[10]; f_change_time = time_CPU[11];
    f_unpack_time = time_CPU[12];
    collision_change_time = gptl_elapsed(GT_collision_change);

    if (me != 0) {
        MPI_Send(time_CPU, 13, MPI_DOUBLE, 0, me, MPI_COMM_WORLD);
        return;
    }

    FILE *file = fopen("CPU_time_gptl.dat", "a");
    if (file == NULL) {
        fprintf(stderr, "can not open CPU_time_gptl.dat!\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        return;
    }
    fprintf(file, "step = %d\n", step);
    fprintf(file, "me\tu_p\tcom1\tIBM\tcom2_time\tFcolli\t | com3_time\tf_pack\tf_change\tf_unpack | \tstream\tPcolli\tPmove\tcom4\ttotal\n");

    for (int nproc = 0; nproc < numproc; ++nproc) {
        if (nproc != 0) {
            MPI_Status status;
            MPI_Recv(time_CPU, 13, MPI_DOUBLE, nproc, nproc,
                     MPI_COMM_WORLD, &status);
        }
        double total = 0.0;
        // f_pack/f_change/f_unpack are included in com3: do not double count.
        for (int i = 0; i < 10; ++i) total += time_CPU[i];
        fprintf(file, "%d\t%f\t%f\t%f\t%f\t%f\t | %f\t%f\t%f\t%f | \t%f\t%f\t%f\t%f\t%f\n",
                nproc, time_CPU[0], time_CPU[1], time_CPU[2], time_CPU[3],
                time_CPU[4], time_CPU[5], time_CPU[10], time_CPU[11],
                time_CPU[12], time_CPU[6], time_CPU[7], time_CPU[8],
                time_CPU[9], total);
    }
    fclose(file);
}
#endif

void data_deal_with_forcef_3D_local(int me)
{
	/*
	========================================================================
	局部流场 + forcef + 相互作用颗粒输出

	功能：
	1. 只输出指定真实物理区域内的流场：
		  x y z u v w fx fy fz p

	2. 不输出MPI虚拟网格。
	   根据当前程序的局部坐标定义：
		  local index = 2  -> global coordinate = me_x*Nx + 1
	   因此每个rank只使用：
		  x = 2 ... Nx1
		  y = 2 ... Ny1
		  z = 2 ... Nz1
	   再通过周期映射把 NxT -> 0、NyT -> 0、NzT -> 0，
	   最终得到唯一的 0 ... NxT-1 物理节点。

	3. 选择局部区域附近的颗粒，并考虑周期边界。
	   颗粒使用owner数据，避免ghost重复。

	4. 颗粒输出两个Tecplot Zone：
	   Particle_velocity:
		  x y z ux uy uz wx wy wz R

	   Particle_force:
		  x y z FIBMx FIBMy FIBMz TIBMx TIBMy TIBMz R

	5. 另输出local_particle_detail文件，用于调试颗粒ID、原始坐标、
	   周期镜像坐标、速度、IBM力/力矩等。

	注意：
	应当在：
		IBM_Particle...
		Particle_IB_comm...
	之后，
		Fluid_collision()
	之前调用，
	此时forcef为当前IBM spread后的Eulerian force。
	========================================================================
	*/

	/* ================================================================
	   1. 用户只需要修改这里
	   ================================================================ */

	   // -------- 局部流场范围：两端都包含 --------
	const int xmin = 480;
	const int xmax = 599;

	const int ymin = 200;
	const int ymax = 310;

	const int zmin = 500;
	const int zmax = 599;

	/*
	   颗粒筛选距离：

	   surface_gap <= particle_margin_R * R 时保留。

	   若严格按照“颗粒表面距离局部区域 <= 1R”：
		   particle_margin_R = 1.0

	   你之前给的例子：
		   box x=[10,50]
		   particle center=65
		   R=5

	   颗粒表面最近位置=60，
	   与box距离=10=2R，
	   因而要保留这个例子，需要设为2.0。
	*/
	const double particle_margin_R = 2.0;


	/* ================================================================
	   2. 基本局部变量
	   ================================================================ */

	int i, j, k;
	int x, y, z;

	int me_x, me_y, me_z;
	int me_xy_temp;

	int gx_raw, gy_raw, gz_raw;
	int gx, gy, gz;

	int ix_local, iy_local, iz_local;

	FILE* file = NULL;
	FILE* file_detail = NULL;

	char fname[300];
	char fname_detail[300];

	MPI_Status status;

	// 进程在3D进程拓扑中的位置
	me_z = me / (NPROCx * NPROCy);

	me_xy_temp =
		me - me_z * NPROCx * NPROCy;

	me_y =
		me_xy_temp / NPROCx;

	me_x =
		me_xy_temp - me_y * NPROCx;


	/* ================================================================
	   3. 检查局部区域
	   当前版本要求局部box本身位于0~L-1内。
	   颗粒周期镜像会自动处理。
	   ================================================================ */

	if (
		xmin < 0 || xmax >= NxT ||
		ymin < 0 || ymax >= NyT ||
		zmin < 0 || zmax >= NzT ||
		xmin > xmax ||
		ymin > ymax ||
		zmin > zmax
		)
	{
		if (me == 0)
		{
			fprintf(stderr,
				"[LOCAL_OUTPUT_ERROR] "
				"slice range is invalid: "
				"x=[%d,%d], y=[%d,%d], z=[%d,%d]\n",
				xmin, xmax,
				ymin, ymax,
				zmin, zmax);
		}

		return;
	}


	const int local_nx = xmax - xmin + 1;
	const int local_ny = ymax - ymin + 1;
	const int local_nz = zmax - zmin + 1;

	const int local_total_points =
		local_nx * local_ny * local_nz;


	/* ================================================================
	   4. 当前rank统计属于局部box的真实流场点数
	   ================================================================ */

	int local_fluid_count = 0;

	/*
	   只遍历每个MPI rank真正拥有的节点。

	   根据你的局部坐标定义：
		   index 0 -> global = me_x*Nx - 1
		   index 1 -> global = me_x*Nx
		   index 2 -> global = me_x*Nx + 1

	   因此：
		   2 ... Nx1
	   是本rank真正推进的物理节点。

	   最后一块的global=NxT，通过%NxT映射成0，
	   从而自动处理周期端点。
	*/

	for (x = 2; x <= Nx1; x++)
	{
		gx_raw =
			x + me_x * Nx - 1;

		gx = gx_raw % NxT;

		for (y = 2; y <= Ny1; y++)
		{
			gy_raw =
				y + me_y * Ny - 1;

			gy = gy_raw % NyT;

			for (z = 2; z <= Nz1; z++)
			{
				gz_raw =
					z + me_z * Nz - 1;

				gz = gz_raw % NzT;

				if (
					gx >= xmin && gx <= xmax &&
					gy >= ymin && gy <= ymax &&
					gz >= zmin && gz <= zmax
					)
				{
					local_fluid_count++;
				}
			}
		}
	}


	/* ================================================================
	   5. 当前rank打包局部流场
	   每个点10个double：

	   0-2  x y z
	   3-5  u v w
	   6-8  fx fy fz
	   9    p/p0
	   ================================================================ */

	const int fluid_record_size = 10;

	double* fluid_send = NULL;

	if (local_fluid_count > 0)
	{
		fluid_send = (double*)malloc(
			local_fluid_count *
			fluid_record_size *
			sizeof(double)
		);
	}

	int fluid_counter = 0;

	for (x = 2; x <= Nx1; x++)
	{
		gx_raw =
			x + me_x * Nx - 1;

		gx = gx_raw % NxT;

		for (y = 2; y <= Ny1; y++)
		{
			gy_raw =
				y + me_y * Ny - 1;

			gy = gy_raw % NyT;

			for (z = 2; z <= Nz1; z++)
			{
				gz_raw =
					z + me_z * Nz - 1;

				gz = gz_raw % NzT;

				if (
					gx >= xmin && gx <= xmax &&
					gy >= ymin && gy <= ymax &&
					gz >= zmin && gz <= zmax
					)
				{
					int q =
						fluid_counter *
						fluid_record_size;

					fluid_send[q + 0] = (double)gx;
					fluid_send[q + 1] = (double)gy;
					fluid_send[q + 2] = (double)gz;

					fluid_send[q + 3] =
						u[x][y][z][0];

					fluid_send[q + 4] =
						u[x][y][z][1];

					fluid_send[q + 5] =
						u[x][y][z][2];

					fluid_send[q + 6] =
						forcef[x][y][z][0];

					fluid_send[q + 7] =
						forcef[x][y][z][1];

					fluid_send[q + 8] =
						forcef[x][y][z][2];

					fluid_send[q + 9] =
						p[x][y][z] / p0;

					fluid_counter++;
				}
			}
		}
	}


	/* ================================================================
	   6. 收集各rank局部流场点数
	   ================================================================ */

	int* fluid_counts = NULL;
	int* fluid_counts_double = NULL;
	int* fluid_displs_double = NULL;

	if (me == 0)
	{
		fluid_counts =
			(int*)malloc(
				NPROC * sizeof(int)
			);
	}

	MPI_Gather(
		&local_fluid_count,
		1,
		MPI_INT,
		fluid_counts,
		1,
		MPI_INT,
		0,
		MPI_COMM_WORLD
	);


	/* ================================================================
	   7. rank0准备接收整个局部流场
	   ================================================================ */

	double* fluid_recv = NULL;

	int total_fluid_count = 0;

	if (me == 0)
	{
		fluid_counts_double =
			(int*)malloc(
				NPROC * sizeof(int)
			);

		fluid_displs_double =
			(int*)malloc(
				NPROC * sizeof(int)
			);

		fluid_displs_double[0] = 0;

		for (i = 0; i < NPROC; i++)
		{
			total_fluid_count +=
				fluid_counts[i];

			fluid_counts_double[i] =
				fluid_counts[i] *
				fluid_record_size;

			if (i > 0)
			{
				fluid_displs_double[i] =
					fluid_displs_double[i - 1]
					+
					fluid_counts_double[i - 1];
			}
		}

		if (total_fluid_count > 0)
		{
			fluid_recv =
				(double*)malloc(
					total_fluid_count *
					fluid_record_size *
					sizeof(double)
				);
		}
	}


	MPI_Gatherv(
		fluid_send,
		local_fluid_count * fluid_record_size,
		MPI_DOUBLE,

		fluid_recv,
		fluid_counts_double,
		fluid_displs_double,
		MPI_DOUBLE,

		0,
		MPI_COMM_WORLD
	);


	if (fluid_send != NULL)
	{
		free(fluid_send);
		fluid_send = NULL;
	}


	/* ================================================================
	   8. 颗粒筛选

	   每个rank只检查owner particle：
		  KeyProcess_poly[k] == me

	   周期性：
	   对每个颗粒尝试[-L,0,+L]的27个周期镜像，
	   选与local box中心距离最近的那个。

	   判断：
		  center_to_box_distance
		  surface_gap = center_to_box_distance - R

	   surface_gap <= particle_margin_R * R
	   即保留。
	   ================================================================ */

	   /*
		  particle record:

		  0      particle ID

		  1-3    periodic image position
		  4-6    original position

		  7-9    uc
		  10-12  wc

		  13-15  F_IBM
		  16-18  T_IBM

		  19     R
		  20     rhop

		  21     surface_gap
		  22     rc/index
	   */

	const int particle_record_size = 23;

	int max_local_particle_num;

	if (if_polydisperse == 1)
	{
		max_local_particle_num =
			np_local_poly;
	}
	else
	{
		max_local_particle_num =
			np_local;
	}

	double* particle_send = NULL;

	if (max_local_particle_num > 0)
	{
		particle_send =
			(double*)malloc(
				max_local_particle_num *
				particle_record_size *
				sizeof(double)
			);
	}

	int local_particle_count = 0;


	/* ================================================================
	   8.1 多分散
	   ================================================================ */

	if (if_polydisperse == 1)
	{
		for (k = 0; k < np_local_poly; k++)
		{
			if (num_p_poly[k] < 0)
				continue;

			j = num_p_poly[k];

			// 只输出owner，避免ghost重复
			if (KeyProcess_poly[k] != me)
				continue;


			double Rj =
				R_poly[rc_poly[j]];


			/*
			   找离local box最近的周期镜像
			*/

			double best_dist2 = 1.0e100;

			double best_x =
				xc[j][0];

			double best_y =
				xc[j][1];

			double best_z =
				xc[j][2];


			int sx, sy, sz;

			for (sx = -1; sx <= 1; sx++)
			{
				if (
					periodic_x == 0 &&
					sx != 0
					)
					continue;


				double x_image =
					xc[j][0]
					+
					sx * NxT;


				for (sy = -1; sy <= 1; sy++)
				{
					if (
						periodic_y == 0 &&
						sy != 0
						)
						continue;


					double y_image =
						xc[j][1]
						+
						sy * NyT;


					for (sz = -1; sz <= 1; sz++)
					{
						if (
							periodic_z == 0 &&
							sz != 0
							)
							continue;


						double z_image =
							xc[j][2]
							+
							sz * NzT;


						/*
						   点到axis-aligned box的距离
						*/

						double dx_box = 0.0;
						double dy_box = 0.0;
						double dz_box = 0.0;


						if (x_image < xmin)
							dx_box =
							xmin - x_image;

						else if (x_image > xmax)
							dx_box =
							x_image - xmax;


						if (y_image < ymin)
							dy_box =
							ymin - y_image;

						else if (y_image > ymax)
							dy_box =
							y_image - ymax;


						if (z_image < zmin)
							dz_box =
							zmin - z_image;

						else if (z_image > zmax)
							dz_box =
							z_image - zmax;


						double dist2 =
							dx_box * dx_box
							+
							dy_box * dy_box
							+
							dz_box * dz_box;


						if (dist2 < best_dist2)
						{
							best_dist2 = dist2;

							best_x = x_image;
							best_y = y_image;
							best_z = z_image;
						}
					}
				}
			}


			double center_to_box_distance =
				sqrt(best_dist2);


			double surface_gap =
				center_to_box_distance
				-
				Rj;


			/*
			   与局部流场发生交互/足够接近
			*/

			if (
				surface_gap
				<=
				particle_margin_R * Rj
				)
			{
				int q =
					local_particle_count *
					particle_record_size;


				particle_send[q + 0] =
					(double)(j + 1);


				// 周期镜像坐标
				particle_send[q + 1] =
					best_x;

				particle_send[q + 2] =
					best_y;

				particle_send[q + 3] =
					best_z;


				// 原始坐标
				particle_send[q + 4] =
					xc[j][0];

				particle_send[q + 5] =
					xc[j][1];

				particle_send[q + 6] =
					xc[j][2];


				// 平动速度
				particle_send[q + 7] =
					uc[j][0];

				particle_send[q + 8] =
					uc[j][1];

				particle_send[q + 9] =
					uc[j][2];


				// 角速度
				particle_send[q + 10] =
					wc[j][0];

				particle_send[q + 11] =
					wc[j][1];

				particle_send[q + 12] =
					wc[j][2];


				// IBM force
				particle_send[q + 13] =
					F_IBM[j][0];

				particle_send[q + 14] =
					F_IBM[j][1];

				particle_send[q + 15] =
					F_IBM[j][2];


				// IBM torque
				particle_send[q + 16] =
					T_IBM[j][0];

				particle_send[q + 17] =
					T_IBM[j][1];

				particle_send[q + 18] =
					T_IBM[j][2];


				particle_send[q + 19] =
					Rj;

				particle_send[q + 20] =
					rhoc_poly[j];

				particle_send[q + 21] =
					surface_gap;

				particle_send[q + 22] =
					(double)rc_poly[j];


				local_particle_count++;
			}
		}
	}


	/* ================================================================
	   8.2 单分散
	   ================================================================ */

	else
	{
		for (k = 0; k < np_local; k++)
		{
			if (num_p[k] < 0)
				continue;

			j = num_p[k];

			if (KeyProcess[k] != me)
				continue;


			double Rj = R;


			double best_dist2 = 1.0e100;

			double best_x =
				xc[j][0];

			double best_y =
				xc[j][1];

			double best_z =
				xc[j][2];


			int sx, sy, sz;

			for (sx = -1; sx <= 1; sx++)
			{
				if (
					periodic_x == 0 &&
					sx != 0
					)
					continue;

				double x_image =
					xc[j][0]
					+
					sx * NxT;


				for (sy = -1; sy <= 1; sy++)
				{
					if (
						periodic_y == 0 &&
						sy != 0
						)
						continue;

					double y_image =
						xc[j][1]
						+
						sy * NyT;


					for (sz = -1; sz <= 1; sz++)
					{
						if (
							periodic_z == 0 &&
							sz != 0
							)
							continue;

						double z_image =
							xc[j][2]
							+
							sz * NzT;


						double dx_box = 0.0;
						double dy_box = 0.0;
						double dz_box = 0.0;


						if (x_image < xmin)
							dx_box =
							xmin - x_image;

						else if (x_image > xmax)
							dx_box =
							x_image - xmax;


						if (y_image < ymin)
							dy_box =
							ymin - y_image;

						else if (y_image > ymax)
							dy_box =
							y_image - ymax;


						if (z_image < zmin)
							dz_box =
							zmin - z_image;

						else if (z_image > zmax)
							dz_box =
							z_image - zmax;


						double dist2 =
							dx_box * dx_box
							+
							dy_box * dy_box
							+
							dz_box * dz_box;


						if (dist2 < best_dist2)
						{
							best_dist2 = dist2;

							best_x = x_image;
							best_y = y_image;
							best_z = z_image;
						}
					}
				}
			}


			double surface_gap =
				sqrt(best_dist2)
				-
				Rj;


			if (
				surface_gap
				<=
				particle_margin_R * Rj
				)
			{
				int q =
					local_particle_count *
					particle_record_size;


				particle_send[q + 0] =
					(double)(j + 1);


				particle_send[q + 1] =
					best_x;

				particle_send[q + 2] =
					best_y;

				particle_send[q + 3] =
					best_z;


				particle_send[q + 4] =
					xc[j][0];

				particle_send[q + 5] =
					xc[j][1];

				particle_send[q + 6] =
					xc[j][2];


				particle_send[q + 7] =
					uc[j][0];

				particle_send[q + 8] =
					uc[j][1];

				particle_send[q + 9] =
					uc[j][2];


				particle_send[q + 10] =
					wc[j][0];

				particle_send[q + 11] =
					wc[j][1];

				particle_send[q + 12] =
					wc[j][2];


				particle_send[q + 13] =
					F_IBM[j][0];

				particle_send[q + 14] =
					F_IBM[j][1];

				particle_send[q + 15] =
					F_IBM[j][2];


				particle_send[q + 16] =
					T_IBM[j][0];

				particle_send[q + 17] =
					T_IBM[j][1];

				particle_send[q + 18] =
					T_IBM[j][2];


				particle_send[q + 19] =
					Rj;

				particle_send[q + 20] =
					rhop;

				particle_send[q + 21] =
					surface_gap;

				particle_send[q + 22] =
					0.0;


				local_particle_count++;
			}
		}
	}


	/* ================================================================
	   9. 汇总particle数量
	   ================================================================ */

	int* particle_counts = NULL;
	int* particle_counts_double = NULL;
	int* particle_displs_double = NULL;

	if (me == 0)
	{
		particle_counts =
			(int*)malloc(
				NPROC * sizeof(int)
			);
	}


	MPI_Gather(
		&local_particle_count,
		1,
		MPI_INT,

		particle_counts,
		1,
		MPI_INT,

		0,
		MPI_COMM_WORLD
	);


	int total_particle_count = 0;

	double* particle_recv = NULL;


	if (me == 0)
	{
		particle_counts_double =
			(int*)malloc(
				NPROC * sizeof(int)
			);

		particle_displs_double =
			(int*)malloc(
				NPROC * sizeof(int)
			);


		particle_displs_double[0] = 0;


		for (i = 0; i < NPROC; i++)
		{
			total_particle_count +=
				particle_counts[i];


			particle_counts_double[i] =
				particle_counts[i]
				*
				particle_record_size;


			if (i > 0)
			{
				particle_displs_double[i] =
					particle_displs_double[i - 1]
					+
					particle_counts_double[i - 1];
			}
		}


		if (total_particle_count > 0)
		{
			particle_recv =
				(double*)malloc(
					total_particle_count
					*
					particle_record_size
					*
					sizeof(double)
				);
		}
	}


	MPI_Gatherv(
		particle_send,
		local_particle_count
		*
		particle_record_size,
		MPI_DOUBLE,

		particle_recv,
		particle_counts_double,
		particle_displs_double,
		MPI_DOUBLE,

		0,
		MPI_COMM_WORLD
	);


	if (particle_send != NULL)
	{
		free(particle_send);
		particle_send = NULL;
	}


	/* ================================================================
	   10. rank0重建规则局部流场
	   ================================================================ */

	if (me == 0)
	{
		/*
		   field:
		   每点10变量，与fluid record相同。

		   filled:
		   用于检查是否每个局部节点都被唯一收到。
		*/

		double* field =
			(double*)calloc(
				local_total_points
				*
				fluid_record_size,
				sizeof(double)
			);


		unsigned char* filled =
			(unsigned char*)calloc(
				local_total_points,
				sizeof(unsigned char)
			);


		int duplicate_count = 0;


		for (i = 0;
			i < total_fluid_count;
			i++)
		{
			int q =
				i * fluid_record_size;


			gx =
				(int)fluid_recv[q + 0];

			gy =
				(int)fluid_recv[q + 1];

			gz =
				(int)fluid_recv[q + 2];


			ix_local =
				gx - xmin;

			iy_local =
				gy - ymin;

			iz_local =
				gz - zmin;


			int local_id =
				(
					ix_local
					*
					local_ny
					+
					iy_local
					)
				*
				local_nz
				+
				iz_local;


			if (filled[local_id])
			{
				duplicate_count++;
			}


			filled[local_id] = 1;


			int dest =
				local_id
				*
				fluid_record_size;


			for (j = 0;
				j < fluid_record_size;
				j++)
			{
				field[dest + j] =
					fluid_recv[q + j];
			}
		}


		int missing_count = 0;

		for (i = 0;
			i < local_total_points;
			i++)
		{
			if (!filled[i])
			{
				missing_count++;
			}
		}


		if (
			missing_count > 0
			||
			duplicate_count > 0
			)
		{
			fprintf(
				stderr,
				"[LOCAL_OUTPUT_CHECK] "
				"step=%d "
				"missing=%d duplicate=%d "
				"expected=%d received=%d\n",
				step,
				missing_count,
				duplicate_count,
				local_total_points,
				total_fluid_count
			);

			fflush(stderr);
		}


		/* ============================================================
		   11. 写Tecplot主文件
		   ============================================================ */

		sprintf(
			fname,
			"%slocal_flui_particle_data_with_forcef_3D%06d.dat",
			Data_folder,
			step
		);


		file = fopen(fname, "w");


		if (file == NULL)
		{
			fprintf(
				stderr,
				"Cannot open %s\n",
				fname
			);

			MPI_Abort(
				MPI_COMM_WORLD,
				1
			);
		}


		fprintf(
			file,
			"TITLE=Sedimentation\n"
		);


		fprintf(
			file,
			"VARIABLES=x,y,z,u,v,w,fx,fy,fz,p\n"
		);


		/*
		   数据循环：
			 x outer
			 y middle
			 z inner

		   因此Tecplot：
			 I = nz
			 J = ny
			 K = nx

		   与原PFlows输出方式一致。
		*/

		fprintf(
			file,
			"Zone T=\"Local_Fluid\", "
			"I=%d,J=%d,K=%d,F=POINT\n",
			local_nz,
			local_ny,
			local_nx
		);


		for (gx = xmin;
			gx <= xmax;
			gx++)
		{
			for (gy = ymin;
				gy <= ymax;
				gy++)
			{
				for (gz = zmin;
					gz <= zmax;
					gz++)
				{
					ix_local =
						gx - xmin;

					iy_local =
						gy - ymin;

					iz_local =
						gz - zmin;


					int local_id =
						(
							ix_local
							*
							local_ny
							+
							iy_local
							)
						*
						local_nz
						+
						iz_local;


					int q =
						local_id
						*
						fluid_record_size;


					fprintf(
						file,
						"%d\t%d\t%d\t"
						"%.12e\t%.12e\t%.12e\t"
						"%.12e\t%.12e\t%.12e\t"
						"%.12e\n",

						gx,
						gy,
						gz,

						field[q + 3],
						field[q + 4],
						field[q + 5],

						field[q + 6],
						field[q + 7],
						field[q + 8],

						field[q + 9]
					);
				}
			}
		}


		/* ============================================================
		   12. Particle velocity / omega zone
		   x y z | uc | wc | R
		   ============================================================ */

		if (total_particle_count > 0)
		{
			fprintf(
				file,
				"Zone T=\"Particle_velocity\", "
				"I=%d,F=POINT\n",
				total_particle_count
			);


			for (i = 0;
				i < total_particle_count;
				i++)
			{
				int q =
					i * particle_record_size;


				fprintf(
					file,
					"%.12e\t%.12e\t%.12e\t"
					"%.12e\t%.12e\t%.12e\t"
					"%.12e\t%.12e\t%.12e\t"
					"%.12e\n",

					// periodic image position
					particle_recv[q + 1],
					particle_recv[q + 2],
					particle_recv[q + 3],

					// uc
					particle_recv[q + 7],
					particle_recv[q + 8],
					particle_recv[q + 9],

					// wc
					particle_recv[q + 10],
					particle_recv[q + 11],
					particle_recv[q + 12],

					// R
					particle_recv[q + 19]
				);
			}


			/* ========================================================
			   13. Particle IBM force / torque zone
			   x y z | FIBM | TIBM | R
			   ======================================================== */

			fprintf(
				file,
				"Zone T=\"Particle_force\", "
				"I=%d,F=POINT\n",
				total_particle_count
			);


			for (i = 0;
				i < total_particle_count;
				i++)
			{
				int q =
					i * particle_record_size;


				fprintf(
					file,
					"%.12e\t%.12e\t%.12e\t"
					"%.12e\t%.12e\t%.12e\t"
					"%.12e\t%.12e\t%.12e\t"
					"%.12e\n",

					// periodic image position
					particle_recv[q + 1],
					particle_recv[q + 2],
					particle_recv[q + 3],

					// FIBM
					particle_recv[q + 13],
					particle_recv[q + 14],
					particle_recv[q + 15],

					// TIBM
					particle_recv[q + 16],
					particle_recv[q + 17],
					particle_recv[q + 18],

					// R
					particle_recv[q + 19]
				);
			}
		}


		fclose(file);


		/* ============================================================
		   14. 另外输出颗粒详细调试文件
		   ============================================================ */

		sprintf(
			fname_detail,
			"%slocal_particle_detail%06d.dat",
			Data_folder,
			step
		);


		file_detail =
			fopen(
				fname_detail,
				"w"
			);


		if (file_detail != NULL)
		{
			fprintf(
				file_detail,
				"# id "
				"x_image y_image z_image "
				"x_original y_original z_original "
				"ux uy uz "
				"wx wy wz "
				"FIBMx FIBMy FIBMz "
				"TIBMx TIBMy TIBMz "
				"R rhop surface_gap index\n"
			);


			for (i = 0;
				i < total_particle_count;
				i++)
			{
				int q =
					i * particle_record_size;


				fprintf(
					file_detail,

					"%d\t"

					"%.12e\t%.12e\t%.12e\t"

					"%.12e\t%.12e\t%.12e\t"

					"%.12e\t%.12e\t%.12e\t"

					"%.12e\t%.12e\t%.12e\t"

					"%.12e\t%.12e\t%.12e\t"

					"%.12e\t%.12e\t%.12e\t"

					"%.12e\t%.12e\t%.12e\t%d\n",

					(int)particle_recv[q + 0],

					particle_recv[q + 1],
					particle_recv[q + 2],
					particle_recv[q + 3],

					particle_recv[q + 4],
					particle_recv[q + 5],
					particle_recv[q + 6],

					particle_recv[q + 7],
					particle_recv[q + 8],
					particle_recv[q + 9],

					particle_recv[q + 10],
					particle_recv[q + 11],
					particle_recv[q + 12],

					particle_recv[q + 13],
					particle_recv[q + 14],
					particle_recv[q + 15],

					particle_recv[q + 16],
					particle_recv[q + 17],
					particle_recv[q + 18],

					particle_recv[q + 19],
					particle_recv[q + 20],
					particle_recv[q + 21],

					(int)particle_recv[q + 22]
				);
			}


			fclose(file_detail);
		}


		printf(
			"Local forcef field output: "
			"step=%d, "
			"region=[%d:%d,%d:%d,%d:%d], "
			"fluid_points=%d, particles=%d\n",

			step,

			xmin,
			xmax,

			ymin,
			ymax,

			zmin,
			zmax,

			local_total_points,
			total_particle_count
		);


		/* ============================================================
		   15. rank0释放内存
		   ============================================================ */

		free(field);
		free(filled);
	}


	/* ================================================================
	   16. 所有MPI通讯数组释放
	   ================================================================ */

	if (me == 0)
	{
		if (fluid_recv != NULL)
			free(fluid_recv);

		if (fluid_counts != NULL)
			free(fluid_counts);

		if (fluid_counts_double != NULL)
			free(fluid_counts_double);

		if (fluid_displs_double != NULL)
			free(fluid_displs_double);


		if (particle_recv != NULL)
			free(particle_recv);

		if (particle_counts != NULL)
			free(particle_counts);

		if (particle_counts_double != NULL)
			free(particle_counts_double);

		if (particle_displs_double != NULL)
			free(particle_displs_double);
	}
}
