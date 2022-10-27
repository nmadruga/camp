/* Copyright (C) 2021 Barcelona Supercomputing Center and University of
 * Illinois at Urbana-Champaign
 * SPDX-License-Identifier: MIT
 */

#include "itsolver_gpu.h"
#include "cvode_cuda.h"

extern "C" {
#include "cvode_gpu.h"
#include "rxns_gpu.h"
}

#ifdef CAMP_USE_MPI
#include <mpi.h>
#endif

#define HANDLE_ERROR( err ) (HandleError( err, __FILE__, __LINE__ ))

#include <unistd.h>

static void HandleError(cudaError_t err,
                        const char *file,
                        int line) {
  if (err != cudaSuccess) {
    printf("%s in %s at line %d\n", cudaGetErrorString(err),
           file, line);
    exit(EXIT_FAILURE);
  }
}

int nextPowerOfTwoCVODE(int v){

  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;

  return v;
}

void createLinearSolver_cvode(SolverData *sd){
  ModelDataGPU *mGPU = sd->mGPU;
  mGPU->maxIt=1000;
  mGPU->tolmax=1.0e-30;
  int nrows = mGPU->nrows;
  double ** dr0 = &mGPU->dr0;
  double ** dr0h = &mGPU->dr0h;
  double ** dn0 = &mGPU->dn0;
  double ** dp0 = &mGPU->dp0;
  double ** dt = &mGPU->dt;
  double ** ds = &mGPU->ds;
  double ** dy = &mGPU->dy;
  double ** ddiag = &mGPU->ddiag;
  cudaMalloc(dr0,nrows*sizeof(double));
  cudaMalloc(dr0h,nrows*sizeof(double));
  cudaMalloc(dn0,nrows*sizeof(double));
  cudaMalloc(dp0,nrows*sizeof(double));
  cudaMalloc(dt,nrows*sizeof(double));
  cudaMalloc(ds,nrows*sizeof(double));
  cudaMalloc(dy,nrows*sizeof(double));
  HANDLE_ERROR(cudaMalloc(ddiag,nrows*sizeof(double)));
  ModelDataCPU *mCPU = &(sd->mCPU);
  mCPU->aux=(double*)malloc(sizeof(double)*mCPU->blocks);
}

__global__
void init_jac_partials_cvode(double* production_partials, double* loss_partials) {
  int tid = blockIdx.x * blockDim.x + threadIdx.x;
  production_partials[tid]=0.0;
  loss_partials[tid]=0.0;
}

int jacobian_initialize_cuda_cvode(SolverData *sd) {
  ModelDataGPU *mGPU = sd->mGPU;
  Jacobian *jac = &sd->jac;
#ifdef DEBUG_jacobian_initialize_gpu
  printf("jacobian_initialize_gpu start \n");
#endif
  mGPU = sd->mGPU;
  JacobianGPU *jacgpu = &(mGPU->jac);
  cudaMalloc((void **) &jacgpu->num_elem, 1 * sizeof(jacgpu->num_elem));
  cudaMemcpy(jacgpu->num_elem, &jac->num_elem, 1 * sizeof(jacgpu->num_elem), cudaMemcpyHostToDevice);
  int num_elem = jac->num_elem * mGPU->n_cells;
  cudaMalloc((void **) &(jacgpu->production_partials), num_elem * sizeof(jacgpu->production_partials));
  HANDLE_ERROR(cudaMalloc((void **) &(jacgpu->loss_partials), num_elem * sizeof(jacgpu->loss_partials)));
  int iDevice=0;
  cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, iDevice);
  int threads_block = prop.maxThreadsPerBlock;
  int blocks = (num_elem +threads_block - 1) / threads_block;
  init_jac_partials_cvode <<<blocks,threads_block>>>(jacgpu->production_partials,jacgpu->loss_partials);

#ifdef DEBUG_jacobian_initialize_gpu
  printf("jacobian_initialize_gpu end \n");
#endif
  return 1;
}

__global__
void init_J_tmp2_cuda_cvode(double* J_tmp2) {
  int tid = blockIdx.x * blockDim.x + threadIdx.x;
  J_tmp2[tid]=0.0;
}

void init_jac_cuda_cvode(SolverData *sd){
  ModelData *md = &(sd->model_data);
  ModelDataGPU *mGPU;
  ModelDataCPU *mCPU = &(sd->mCPU);
#ifdef DEBUG_init_jac_cuda
  printf("init_jac_cuda start \n");
#endif
  mGPU = sd->mGPU;
  mCPU->jac_size = md->n_per_cell_solver_jac_elem * mGPU->n_cells * sizeof(double);
  mCPU->nnz_J_solver = SM_NNZ_S(md->J_solver)/md->n_cells*mGPU->n_cells;
  cudaMalloc((void **) &mGPU->dA, mCPU->jac_size);
  cudaMalloc((void **) &mGPU->J_solver, mCPU->jac_size);
  cudaMalloc((void **) &mGPU->J_state, mCPU->deriv_size);
  cudaMalloc((void **) &mGPU->J_deriv, mCPU->deriv_size);
  cudaMalloc((void **) &mGPU->J_tmp, mCPU->deriv_size);
  cudaMalloc((void **) &mGPU->J_tmp2, mCPU->deriv_size);
  cudaMalloc((void **) &mGPU->jac_map, sizeof(JacMap) * md->n_mapped_values);
  HANDLE_ERROR(cudaMalloc((void **) &mGPU->n_mapped_values, 1 * sizeof(int)));
#ifdef DEBUG_init_jac_cuda
  printf("md->n_per_cell_dep_var %d sd->jac.num_spec %d md->n_per_cell_solver_jac_elem %d "
         "md->n_mapped_values %d jac->num_elem %d  mCPU->nnz_J_solver %d "
         "mCPU->jac_size/sizeof(double) %d SM_NNZ_S(sd->J) %d\n",
         md->n_per_cell_dep_var,sd->jac.num_spec,md->n_per_cell_solver_jac_elem, md->n_mapped_values,
         sd->jac.num_elem,mCPU->nnz_J_solver,mCPU->jac_size/sizeof(double),
         SM_NNZ_S(sd->J));
#endif
  double *J = SM_DATA_S(sd->J);
  HANDLE_ERROR(cudaMemcpy(mGPU->dA, J, mCPU->jac_size, cudaMemcpyHostToDevice));
  double *J_solver = SM_DATA_S(md->J_solver);
  cudaMemcpy(mGPU->J_solver, J_solver, mCPU->jac_size, cudaMemcpyHostToDevice);
  double *J_state = N_VGetArrayPointer(md->J_state);
  HANDLE_ERROR(cudaMemcpy(mGPU->J_state, J_state, mCPU->deriv_size, cudaMemcpyHostToDevice));
  double *J_deriv = N_VGetArrayPointer(md->J_deriv);
  double *J_tmp2 = N_VGetArrayPointer(md->J_tmp2);
  HANDLE_ERROR(cudaMemcpy(mGPU->J_deriv, J_deriv, mCPU->deriv_size, cudaMemcpyHostToDevice));
  int iDevice=0;
  cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, iDevice);
  int threads_block = prop.maxThreadsPerBlock;;
  int blocks = (mCPU->deriv_size/sizeof(double)+threads_block - 1) / threads_block;
  init_J_tmp2_cuda_cvode <<<blocks,threads_block>>>(mGPU->J_tmp2);
  HANDLE_ERROR(cudaMemcpy(mGPU->jac_map, md->jac_map, sizeof(JacMap) * md->n_mapped_values, cudaMemcpyHostToDevice));
  HANDLE_ERROR(cudaMemcpy(mGPU->n_mapped_values, &md->n_mapped_values, 1 * sizeof(int), cudaMemcpyHostToDevice));
  jacobian_initialize_cuda_cvode(sd);
#ifdef DEBUG_init_jac_cuda
  printf("init_jac_cuda end \n");
#endif
}

void set_int_double_cuda_cvode(
    int n_rxn, int rxn_env_data_idx_size,
    int *rxn_int_data, double *rxn_float_data,
    int *rxn_int_indices, int *rxn_float_indices,
    int *rxn_env_idx,
    SolverData *sd
) {
  ModelData *md = &(sd->model_data);
  ModelDataGPU *mGPU = sd->mGPU;
  cudaMalloc((void **) &mGPU->rxn_int, (md->n_rxn_int_param + md->n_rxn)*sizeof(int));
  cudaMalloc((void **) &mGPU->rxn_double, md->n_rxn_float_param*sizeof(double));
#ifdef REVERSE_INT_FLOAT_MATRIX
#else
  cudaMalloc((void **) &mGPU->rxn_int_indices, (md->n_rxn+1)*sizeof(int));
  cudaMalloc((void **) &mGPU->rxn_float_indices, (md->n_rxn+1)*sizeof(int));
#endif
  HANDLE_ERROR(cudaMemcpy(mGPU->rxn_int, rxn_int_data,(md->n_rxn_int_param + md->n_rxn)*sizeof(int), cudaMemcpyHostToDevice));
  HANDLE_ERROR(cudaMemcpy(mGPU->rxn_double, rxn_float_data, md->n_rxn_float_param*sizeof(double), cudaMemcpyHostToDevice));
  HANDLE_ERROR(cudaMemcpy(mGPU->rxn_env_data_idx, rxn_env_idx, rxn_env_data_idx_size, cudaMemcpyHostToDevice));
#ifdef REVERSE_INT_FLOAT_MATRIX
#else
  HANDLE_ERROR(cudaMemcpy(mGPU->rxn_int_indices, md->rxn_int_indices,(md->n_rxn+1)*sizeof(int), cudaMemcpyHostToDevice));
  HANDLE_ERROR(cudaMemcpy(mGPU->rxn_float_indices, md->rxn_float_indices,(md->n_rxn+1)*sizeof(int), cudaMemcpyHostToDevice));
#endif
}

void solver_init_int_double_cuda_cvode(SolverData *sd) {
  ModelData *md = &(sd->model_data);
  ModelDataCPU *mCPU = &(sd->mCPU);
#ifdef DEBUG_solver_init_int_double_gpu
  printf("solver_init_int_double_cuda_cvode start \n");
#endif
#ifdef REVERSE_INT_FLOAT_MATRIX
  set_reverse_int_double_rxn(
          md->n_rxn, mCPU->rxn_env_data_idx_size,
          md->rxn_int_data, md->rxn_float_data,
          md->rxn_int_indices, md->rxn_float_indices, md->rxn_env_idx,
          sd
  );
#else
  set_int_double_cuda_cvode(
      md->n_rxn, mCPU->rxn_env_data_idx_size,
      md->rxn_int_data, md->rxn_float_data,
      md->rxn_int_indices, md->rxn_float_indices, md->rxn_env_idx,
      sd
  );
#endif
#ifdef DEBUG_solver_init_int_double_gpu
  printf("solver_init_int_double_cuda_cvode end \n");
#endif
}

void solver_new_gpu_cu_cvode(SolverData *sd) {
  ModelData *md = &(sd->model_data);
  ModelDataGPU *mGPU;
  ModelDataCPU *mCPU = &(sd->mCPU);
  int n_dep_var = md->n_per_cell_dep_var;
  int n_state_var = md->n_per_cell_state_var;
  int n_rxn = md->n_rxn;
  int n_rxn_env_param = md->n_rxn_env_data;
  int n_cells_total = md->n_cells;
#ifdef DEBUG_solver_new_gpu_cu_cvode
  printf("solver_new_gpu_cu_cvode start \n");
#endif
#ifdef DEV_CPUGPU
  sd->nCellsGPUPerc=0.7;
  n_cells_total *= sd->nCellsGPUPerc;
#endif
  sd->mGPU = (ModelDataGPU *)malloc(sizeof(ModelDataGPU));
  int remainder = n_cells_total % sd->nDevices;
  int nDevicesMax;
  cudaGetDeviceCount(&nDevicesMax);
  if (sd->nDevices > nDevicesMax) {
    printf("ERROR: Not enough GPUs to launch, nDevices %d nDevicesMax %d\n", sd->nDevices, nDevicesMax);
    exit(0);
  }
  int coresPerNode = 40;
  int size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  if (size > 40 && size % coresPerNode != 0) {
    printf("ERROR: MORE THAN 40 MPI PROCESSES AND NOT MULTIPLE OF 40, WHEN CTE-POWER ONLY HAS 40 CORES PER NODE\n");
    exit(0);
  }
  if (size > sd->nDevices*(coresPerNode/nDevicesMax)){
    printf("ERROR: MORE MPI PROCESSES THAN DEVICES (FOLLOW PROPORTION, FOR CTE-POWER IS 10 PROCESSES FOR EACH GPU, SINCE IT HAS 4 GPUS AND 40 PROCESSES PER NODE)\n");
    exit(0);
  }
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  cudaSetDevice(0);
  for (int i = 0; i < coresPerNode; i++) {
    if (rank < coresPerNode / nDevicesMax * (i + 1) && rank >= coresPerNode / nDevicesMax * i && i<sd->nDevices) {
      cudaSetDevice(i);
      //printf("rank %d, device %d", rank, i);
    }
  }
  int iDevice = 0;
  mGPU = sd->mGPU;
  int n_cells = int(n_cells_total / sd->nDevices);
  if (remainder!=0 && iDevice==0 && n_cells_total != 1){
    //printf("WARNING:  PENDING TO CHECK THAT WORKS CASE: sd->nDevicesMODn_cells!=0\n");
    //printf("remainder %d n_cells_total %d nDevices %d n_cells %d\n",remainder,n_cells_total,sd->nDevices,n_cells);
    n_cells+=remainder;
  }
  mGPU->n_cells=n_cells;
  mCPU->state_size = n_state_var * n_cells * sizeof(double);
  mCPU->deriv_size = n_dep_var * n_cells * sizeof(double);
  mCPU->env_size = CAMP_NUM_ENV_PARAM_ * n_cells * sizeof(double); //Temp and pressure
  mCPU->rxn_env_data_size = n_rxn_env_param * n_cells * sizeof(double);
  mCPU->rxn_env_data_idx_size = (n_rxn+1) * sizeof(int);
  mCPU->map_state_deriv_size = n_dep_var * n_cells * sizeof(int);
  cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, iDevice);
  mCPU->max_n_gpu_thread = prop.maxThreadsPerBlock;
  mCPU->max_n_gpu_blocks = prop.maxGridSize[1];
  int n_blocks = (mCPU->deriv_size + mCPU->max_n_gpu_thread - 1) / mCPU->max_n_gpu_thread;
  if( n_blocks > mCPU->max_n_gpu_blocks){
    printf("\nWarning: More blocks assigned: %d than maximum block numbers: %d",
           n_blocks, mCPU->max_n_gpu_blocks);
  }
  HANDLE_ERROR(cudaMalloc((void **) &mGPU->deriv_data, mCPU->deriv_size));
  mGPU->n_rxn=md->n_rxn;
  mGPU->n_rxn_env_data=md->n_rxn_env_data;
  cudaMalloc((void **) &mGPU->state, mCPU->state_size);
  cudaMalloc((void **) &mGPU->env, mCPU->env_size);
  cudaMalloc((void **) &mGPU->rxn_env_data, mCPU->rxn_env_data_size);
  cudaMalloc((void **) &mGPU->rxn_env_data_idx, mCPU->rxn_env_data_idx_size);
  HANDLE_ERROR(cudaMalloc((void **) &mGPU->map_state_deriv, mCPU->map_state_deriv_size));
  int num_spec = md->n_per_cell_dep_var*mGPU->n_cells;
  cudaMalloc((void **) &(mGPU->production_rates),num_spec*sizeof(mGPU->production_rates));
  cudaMalloc((void **) &(mGPU->loss_rates),num_spec*sizeof(mGPU->loss_rates));
  mCPU->map_state_derivCPU = (int *)malloc(mCPU->map_state_deriv_size);
  int i_dep_var = 0;
  for (int i_cell = 0; i_cell < n_cells; i_cell++) {
    for (int i_spec = 0; i_spec < n_state_var; i_spec++) {
      if (md->var_type[i_spec] == CHEM_SPEC_VARIABLE) {
        mCPU->map_state_derivCPU[i_dep_var] = i_spec + i_cell * n_state_var;
        //printf("%d %d, %d %d %d\n", mCPU->map_state_deriv_size/sizeof(int),
        //       mCPU->map_state_derivCPU[i_dep_var],n_state_var, i_spec, i_cell, i_dep_var);
        i_dep_var++;
      }
    }
  }
  HANDLE_ERROR(cudaMemcpy(mGPU->map_state_deriv, mCPU->map_state_derivCPU,
                          mCPU->map_state_deriv_size, cudaMemcpyHostToDevice));
  if(n_dep_var<32 && sd->use_cpu==0) {
    printf("CAMP ERROR: TOO FEW SPECIES FOR GPU (Species < 32),"
           " use CPU case instead (More info: https://earth.bsc.es/gitlab/ac/camp/-/issues/49 \n");
    exit(0);
  }
#ifdef DEBUG_solver_new_gpu_cu_cvode
  printf("solver_new_gpu_cu_cvode end \n");
#endif
}
#ifdef DEV_CSR_REACTIONS
void getCSRReactions(SolverData *sd) {
  ModelDataGPU *mGPU = sd->mGPU;
  ModelData *md = &(sd->model_data);
  int nnz=md->n_mapped_values;
  int *colA= (int *)malloc(nnz * sizeof(int));
  int *jA= (int *)malloc(nnz * sizeof(int));
  int i_cell=0;
  int n_state_var = md->n_per_cell_state_var;
  int n_dep_var = md->n_per_cell_dep_var;
  md->grid_cell_id = i_cell;
  md->grid_cell_state = &(md->total_state[i_cell * n_state_var]);
  md->grid_cell_env = &(md->total_env[i_cell * CAMP_NUM_ENV_PARAM_]);
  md->grid_cell_rxn_env_data =
    &(md->rxn_env_data[i_cell * md->n_rxn_env_data]);
  int n_rxn = md->n_rxn;
  for (int i_rxn = 0; i_rxn < n_rxn; i_rxn++) {
    int *rxn_int_data =
        &(md->rxn_int_data[md->rxn_int_indices[i_rxn]]);
    double *rxn_float_data =
        &(md->rxn_float_data[md->rxn_float_indices[i_rxn]]);
    double *rxn_env_data =
        &(md->grid_cell_rxn_env_data[md->rxn_env_idx[i_rxn]]);
    int rxn_type = *(rxn_int_data++);
    switch (rxn_type) {
      case RXN_ARRHENIUS:
        rxn_arrhenius_get_jac_indices(md, sd->jac, rxn_int_data, rxn_float_data,
                                       rxn_env_data,colA,jA);
        break;/*
      case RXN_CMAQ_H2O2:
        rxn_CMAQ_H2O2_update_env_state(md, sd->jac, rxn_int_data, rxn_float_data,
                                       rxn_env_data,colA,jA);
        break;
      case RXN_CMAQ_OH_HNO3:
        rxn_CMAQ_OH_HNO3_update_env_state(md, sd->jac, rxn_int_data,
                                          rxn_float_data, rxn_env_data,colA,jA);
        break;
      case RXN_EMISSION:
        rxn_emission_update_env_state(md, sd->jac, rxn_int_data, rxn_float_data,
                                      rxn_env_data,colA,jA);
        break;
      case RXN_PHOTOLYSIS:
        rxn_photolysis_update_env_state(md, sd->jac, rxn_int_data,
                                        rxn_float_data, rxn_env_data,colA,jA);
        break;
      case RXN_TROE:
        rxn_troe_update_env_state(md, sd->jac, rxn_int_data, rxn_float_data,
                                  rxn_env_data,colA,jA);
        break;*/
    }
  }
  cudaMemset(mGPU->colARXN, -1, md->n_mapped_values * sizeof(int));
  cudaMemset(mGPU->jARXN, -1, md->n_mapped_values * sizeof(int));
  cudaMemcpy(mGPU->colARXN, colA, md->n_mapped_values * sizeof(int), cudaMemcpyHostToDevice);
  cudaMemcpy(mGPU->jARXN, jA, md->n_mapped_values * sizeof(int), cudaMemcpyHostToDevice);

  free(colA);
  free(jA);
}
#endif

#ifdef USE_CSR_ODE_GPU
void swapCSC_CSR_ODE(SolverData *sd){
  ModelDataGPU *mGPU = sd->mGPU;
  int n_row=mGPU->nrows/mGPU->n_cells;
  ModelDataCPU *mCPU = &(sd->mCPU);
  int* Ap=mCPU->iA;
  int* Aj=mCPU->jA;
  double* Ax=mGPU->A;
  int nnz=mGPU->nnz/mGPU->n_cells;
  //printf("n_row %d nnz %d \n",n_row,nnz);
  int* Bp=(int*)malloc((n_row+1)*sizeof(int));
  int* Bi=(int*)malloc(nnz*sizeof(int));
  double* Bx=(double*)malloc(nnz*sizeof(double));
  memset(Bp, 0, (n_row+1)*sizeof(int));
  for (int n = 0; n < nnz; n++){
   Bp[Aj[n]]++;
  }
  for(int col = 0, cumsum = 0; col < n_row; col++){
    int temp  = Bp[col];
    Bp[col] = cumsum;
    cumsum += temp;
  }
  Bp[n_row] = nnz;
  int *mapJSPMV= (int *)malloc(nnz * sizeof(int));
  for(int row = 0; row < n_row; row++){
    for(int jj = Ap[row]; jj < Ap[row+1]; jj++){
      int col  = Aj[jj];
      int dest = Bp[col];
      Bi[dest] = row;
      Bx[dest] = Ax[jj];
      mapJSPMV[jj]=dest;
      Bp[col]++;
    }
  }
  for(int col = 0, last = 0; col <= n_row; col++){
    int temp  = Bp[col];
    Bp[col] = last;
    last    = temp;
  }
  ModelData *md = &(sd->model_data);
  nnz=md->n_mapped_values;
  int *aux_solver_id= (int *)malloc(nnz * sizeof(int));
  for (int i = 0; i < nnz; i++){
    aux_solver_id[i]=mapJSPMV[md->jac_map[i].solver_id];
  }
  free(mapJSPMV);
  int *jac_solver_id= (int *)malloc(nnz * sizeof(int));
  for (int i = 0; i < nnz; i++){
    jac_solver_id[i]=aux_solver_id[i];
    aux_solver_id[i]=md->jac_map[i].solver_id;
    md->jac_map[i].solver_id=jac_solver_id[i];
    //printf("md->jac_map[i].solver_id %d",md->jac_map[i].solver_id);
  }//printf("\n");
  mGPU = sd->mGPU;
  cudaMemcpy(mGPU->diA, Bp, (n_row + 1) * sizeof(int), cudaMemcpyHostToDevice);
  cudaMemcpy(mGPU->djA, Bi, nnz * sizeof(int), cudaMemcpyHostToDevice);
  cudaMemcpy(mGPU->dA, Bx, nnz * sizeof(double), cudaMemcpyHostToDevice);
  HANDLE_ERROR(cudaMemcpy(mGPU->jac_map, md->jac_map, sizeof(JacMap) * md->n_mapped_values, cudaMemcpyHostToDevice));
  free(Bp);
  free(Bi);
  free(Bx);
  free(jac_solver_id);
  free(aux_solver_id);
}
#endif

void constructor_cvode_gpu(CVodeMem cv_mem, SolverData *sd){
  ModelDataCPU *mCPU = &(sd->mCPU);
  ModelData *md = &(sd->model_data);
  CVDlsMem cvdls_mem = (CVDlsMem) cv_mem->cv_lmem;
  SUNMatrix J = cvdls_mem->A;
  sd->flagCells = (int *) malloc((md->n_cells) * sizeof(int));
  ModelDataGPU *mGPU = sd->mGPU;
#ifdef DEBUG_constructor_cvode_gpu
  printf("DEBUG_constructor_cvode_gpu start \n");
#endif
  solver_new_gpu_cu_cvode(sd);
  init_jac_cuda_cvode(sd);
  solver_init_int_double_cuda_cvode(sd);
#ifdef DEV_CSR_REACTIONS
  getCSRReactions(sd);
#endif
  mGPU = sd->mGPU;
#ifdef DEBUG_constructor_cvode_gpu
  printf("DEBUG_constructor_cvode_gpu start2 \n");
#endif
#ifdef CAMP_DEBUG_GPU
  mCPU->counterNewtonIt=0;
  mCPU->counterLinSolSetup=0;
  mCPU->counterLinSolSolve=0;
  mCPU->counterDerivNewton=0;
  mCPU->counterBCG=0;
  mCPU->counterDerivSolve=0;
  mCPU->countersolveCVODEGPU=0;

  mCPU->timeNewtonIt=CAMP_TINY;
  mCPU->timeLinSolSetup=CAMP_TINY;
  mCPU->timeLinSolSolve=CAMP_TINY;
  mCPU->timecvStep=CAMP_TINY;
  mCPU->timeDerivNewton=CAMP_TINY;
  mCPU->timeBiConjGrad=CAMP_TINY;
  mCPU->timeBiConjGradMemcpy=CAMP_TINY;
  mCPU->timeDerivSolve=CAMP_TINY;

  cudaEventCreate(&mCPU->startDerivNewton);
  cudaEventCreate(&mCPU->startDerivSolve);
  cudaEventCreate(&mCPU->startLinSolSetup);
  cudaEventCreate(&mCPU->startLinSolSolve);
  cudaEventCreate(&mCPU->startNewtonIt);
  cudaEventCreate(&mCPU->startcvStep);
  cudaEventCreate(&mCPU->startBCG);
  cudaEventCreate(&mCPU->startBCGMemcpy);

  cudaEventCreate(&mCPU->stopDerivNewton);
  cudaEventCreate(&mCPU->stopDerivSolve);
  cudaEventCreate(&mCPU->stopLinSolSetup);
  cudaEventCreate(&mCPU->stopLinSolSolve);
  cudaEventCreate(&mCPU->stopNewtonIt);
  cudaEventCreate(&mCPU->stopcvStep);
  cudaEventCreate(&mCPU->stopBCG);
  cudaEventCreate(&mCPU->stopBCGMemcpy);
#endif

  int iDevice=0;//todo delete
  mGPU = sd->mGPU;

  mGPU->nnz = SM_NNZ_S(J)/md->n_cells*mGPU->n_cells;
  mGPU->nrows = SM_NP_S(J)/md->n_cells*mGPU->n_cells;
  cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, iDevice);
  mCPU->threads = prop.maxThreadsPerBlock; //1024
  mCPU->blocks = (mGPU->nrows + mCPU->threads - 1) / mCPU->threads;
  if(sd->use_gpu_cvode==0){
    createLinearSolver(sd);
  }else{
    createLinearSolver_cvode(sd);
  }
  mGPU->A = ((double *) SM_DATA_S(J));
  //Using int per default as sundindextype give wrong results in CPU, so translate from int64 to int
  if(sd->use_gpu_cvode==1){
    mCPU->jA = (int *) malloc(sizeof(int) *mGPU->nnz/mGPU->n_cells);
    mCPU->iA = (int *) malloc(sizeof(int) * (mGPU->nrows/mGPU->n_cells + 1));
    for (int i = 0; i < mGPU->nnz/mGPU->n_cells; i++)
      mCPU->jA[i] = SM_INDEXVALS_S(J)[i];
    for (int i = 0; i <= mGPU->nrows/mGPU->n_cells; i++)
      mCPU->iA[i] = SM_INDEXPTRS_S(J)[i];
    cudaMalloc((void **) &mGPU->djA, mGPU->nnz/mGPU->n_cells * sizeof(int));
    cudaMalloc((void **) &mGPU->diA, (mGPU->nrows/mGPU->n_cells + 1) * sizeof(int));
    cudaMemcpy(mGPU->djA, mCPU->jA, mGPU->nnz/mGPU->n_cells * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(mGPU->diA, mCPU->iA, (mGPU->nrows/mGPU->n_cells + 1) * sizeof(int), cudaMemcpyHostToDevice);
  }else{
    mCPU->jA = (int *) malloc(sizeof(int) *mGPU->nnz);
    mCPU->iA = (int *) malloc(sizeof(int) * (mGPU->nrows + 1));
    for (int i = 0; i < mGPU->nnz; i++)
      mCPU->jA[i] = SM_INDEXVALS_S(J)[i];
    for (int i = 0; i <= mGPU->nrows; i++)
      mCPU->iA[i] = SM_INDEXPTRS_S(J)[i];
    cudaMalloc((void **) &mGPU->djA, mGPU->nnz * sizeof(int));
    cudaMalloc((void **) &mGPU->diA, (mGPU->nrows + 1) * sizeof(int));
    cudaMemcpy(mGPU->djA, mCPU->jA, mGPU->nnz * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(mGPU->diA, mCPU->iA, (mGPU->nrows + 1) * sizeof(int), cudaMemcpyHostToDevice);
  }
  mGPU->dftemp = mGPU->deriv_data;
  double *ewt = N_VGetArrayPointer(cv_mem->cv_ewt);
  double *tempv = N_VGetArrayPointer(cv_mem->cv_tempv);
  double *cv_last_yn = N_VGetArrayPointer(cv_mem->cv_last_yn);
  double *cv_acor_init = N_VGetArrayPointer(cv_mem->cv_acor_init);
  cudaMalloc((void **) &mGPU->mdvo, sizeof(ModelDataVariable));
  cudaMalloc((void **) &mGPU->sCells, sizeof(ModelDataVariable)*mGPU->n_cells);
  cudaMalloc((void **) &mGPU->flag, 1 * sizeof(int));
  cudaMalloc((void **) &mGPU->flagCells, mGPU->n_cells * sizeof(int));
  cudaMalloc((void **) &mGPU->dsavedJ, mGPU->nnz * sizeof(double));
  cudaMalloc((void **) &mGPU->dewt, mGPU->nrows * sizeof(double));
  cudaMalloc((void **) &mGPU->cv_acor, mGPU->nrows * sizeof(double));
  cudaMalloc((void **) &mGPU->dtempv, mGPU->nrows * sizeof(double));
  cudaMalloc((void **) &mGPU->dtempv1, mGPU->nrows * sizeof(double));
  cudaMalloc((void **) &mGPU->dtempv2, mGPU->nrows * sizeof(double));
  cudaMalloc((void **) &mGPU->dzn, mGPU->nrows * (cv_mem->cv_qmax + 1) * sizeof(double));//L_MAX 6
  cudaMalloc((void **) &mGPU->dcv_y, mGPU->nrows * sizeof(double));
  cudaMalloc((void **) &mGPU->dx, mGPU->nrows * sizeof(double));
  cudaMalloc((void **) &mGPU->cv_last_yn, mGPU->nrows * sizeof(double));
  cudaMalloc((void **) &mGPU->cv_acor_init, mGPU->nrows * sizeof(double));
  cudaMalloc((void **) &mGPU->cv_acor, mGPU->nrows * sizeof(double));
  cudaMalloc((void **) &mGPU->yout, mGPU->nrows * sizeof(double));
  cudaMalloc((void **) &mGPU->cv_l, L_MAX * mGPU->n_cells * sizeof(double));
  cudaMalloc((void **) &mGPU->cv_tau, (L_MAX + 1) * mGPU->n_cells * sizeof(double));
  cudaMalloc((void **) &mGPU->cv_tq, (NUM_TESTS + 1) * mGPU->n_cells * sizeof(double));
  cudaMalloc((void **) &mGPU->cv_Vabstol, mGPU->nrows * sizeof(double));
  HANDLE_ERROR(cudaMemset(mGPU->flagCells, CV_SUCCESS, mGPU->n_cells * sizeof(int)));
  cudaMemcpy(mGPU->dsavedJ, mGPU->A, mGPU->nnz * sizeof(double), cudaMemcpyHostToDevice);
  cudaMemcpy(mGPU->dewt, ewt, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice);
  cudaMemcpy(mGPU->cv_acor, ewt, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice);
  cudaMemcpy(mGPU->dftemp, ewt, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice);
  cudaMemcpy(mGPU->dx, tempv, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice);
  cudaMemcpy(mGPU->cv_last_yn, cv_last_yn, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice);
  HANDLE_ERROR(cudaMemcpy(mGPU->cv_acor_init, cv_acor_init, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice));
  mGPU->replacement_value = TINY;
  mGPU->threshhold = -SMALL;
  mGPU->state_size_cell = md->n_per_cell_state_var;
  int flag = 999; //CAMP_SOLVER_SUCCESS
  cudaMemcpy(mGPU->flag, &flag, 1 * sizeof(int), cudaMemcpyHostToDevice);
  if(md->n_per_cell_dep_var > prop.maxThreadsPerBlock/2){
    printf("ERROR: More species than threads per block availabless\n");
    exit(0);
  }
#ifdef CAMP_DEBUG_GPU
#ifndef CAMP_PROFILE_DEVICE_FUNCTIONS
  cudaDeviceGetAttribute(&mGPU->clock_khz, cudaDevAttrClockRate, 0);
  mCPU->mdvCPU.countercvStep=0;
  mCPU->mdvCPU.counterBCGInternal=0;
  mCPU->mdvCPU.counterBCG=0;
  mCPU->mdvCPU.timeNewtonIteration=0.;
  mCPU->mdvCPU.timeJac=0.;
  mCPU->mdvCPU.timelinsolsetup=0.;
  mCPU->mdvCPU.timecalc_Jac=0.;
  mCPU->mdvCPU.timef=0.;
  mCPU->mdvCPU.timeguess_helper=0.;
  mCPU->mdvCPU.dtBCG=0.;
  mCPU->mdvCPU.dtcudaDeviceCVode=0.;
  mCPU->mdvCPU.dtPostBCG=0.;
#endif
#endif
  for (int i = 0; i < mGPU->n_cells; i++){
    cudaMemcpy(&mGPU->sCells[i], &mCPU->mdvCPU, sizeof(ModelDataVariable), cudaMemcpyHostToDevice);
  }
  HANDLE_ERROR(cudaMemcpy(mGPU->mdvo, &mCPU->mdvCPU, sizeof(ModelDataVariable), cudaMemcpyHostToDevice));
  mCPU->mdvCPU.nstlj = 0;
#ifdef USE_CSR_ODE_GPU
  if(sd->use_gpu_cvode==1) {
    swapCSC_CSR_ODE(sd);
  }
#endif
  if(cv_mem->cv_sldeton){
    printf("ERROR: cudaDevicecvBDFStab is pending to implement "
           "(disabled by default on CAMP)\n");
    exit(0); }
#ifdef DEBUG_constructor_cvode_gpu
  printf("DEBUG_constructor_cvode_gpu end \n");
#endif
}

__global__
void cudaGlobalCVode(ModelDataGPU md_object) {
  extern __shared__ int flag_shr[];
  int tid = blockIdx.x * blockDim.x + threadIdx.x;
  ModelDataGPU *md = &md_object;
  md->s=&md->sCells[blockIdx.x];
  int active_threads = md->nrows;
  int istate;
  __syncthreads();
  if(tid<active_threads){
    __syncthreads();
#ifdef CAMP_DEBUG_GPU
#ifndef CAMP_PROFILE_DEVICE_FUNCTIONS
    int clock_khz=md->clock_khz;
    clock_t start;
    start = clock();
    __syncthreads();
#endif
#endif
    istate=cudaDeviceCVode(md,md->s);//dmdv as a function parameter seems faster than removing it
    __syncthreads();
#ifdef CAMP_DEBUG_GPU
#ifndef CAMP_PROFILE_DEVICE_FUNCTIONS
  if(threadIdx.x==0) md->s->dtcudaDeviceCVode += ((double)(int)(clock() - start))/(clock_khz*1000);
  __syncthreads();
  //if(tid==0) printf("dtcudaDeviceCVode %lf\n",md->s->dtcudaDeviceCVode);
#endif
#endif
  }
  __syncthreads();
  if(threadIdx.x==0) md->flagCells[blockIdx.x]=istate;
#ifndef CAMP_PROFILE_DEVICE_FUNCTIONS
  ModelDataVariable *mdvo = md->mdvo;
  *mdvo = *md->s;
#endif
  //if(tid==0) printf("cudaGlobalCVode end\n");
}

int cudaCVode(void *cvode_mem, realtype tout, N_Vector yout,
               realtype *tret, int itask, SolverData *sd){
  CVodeMem cv_mem;
  int retval, hflag, istate, ier, irfndp;
  realtype troundoff, tout_hin, rh;
  ModelDataCPU *mCPU = &(sd->mCPU);
  ModelDataGPU *mGPU;
  ModelData *md = &(sd->model_data);
  //printf("cudaCVode start \n");
   // 1. Check and process inputs
  if (cvode_mem == NULL) {
    cvProcessError(NULL, CV_MEM_NULL, "CVODE", "CVode", MSGCV_NO_MEM);
    return(CV_MEM_NULL);
  }
  cv_mem = (CVodeMem) cvode_mem;
  if (cv_mem->cv_MallocDone == SUNFALSE) {
    cvProcessError(cv_mem, CV_NO_MALLOC, "CVODE", "CVode", MSGCV_NO_MALLOC);
    return(CV_NO_MALLOC);
  }
  if ((cv_mem->cv_y = yout) == NULL) {
    cvProcessError(cv_mem, CV_ILL_INPUT, "CVODE", "CVode", MSGCV_YOUT_NULL);
    return(CV_ILL_INPUT);
  }
  if (tret == NULL) {
    cvProcessError(cv_mem, CV_ILL_INPUT, "CVODE", "CVode", MSGCV_TRET_NULL);
    return(CV_ILL_INPUT);
  }
  if ( (itask != CV_NORMAL) && (itask != CV_ONE_STEP) ) {
    cvProcessError(cv_mem, CV_ILL_INPUT, "CVODE", "CVode", MSGCV_BAD_ITASK);
    return(CV_ILL_INPUT);
  }
  if (itask == CV_NORMAL) cv_mem->cv_toutc = tout;
  cv_mem->cv_taskc = itask;
  //2. Initializations performed only at the first step (nst=0):
  if (cv_mem->cv_nst == 0) {
    cv_mem->cv_tretlast = *tret = cv_mem->cv_tn;
    ier = cvInitialSetup_gpu(cv_mem);
    if (ier!= CV_SUCCESS) return(ier);
    retval = f(cv_mem->cv_tn, cv_mem->cv_zn[0], cv_mem->cv_zn[1], cv_mem->cv_user_data);
    N_VScale(ONE, cv_mem->cv_zn[0], yout);
    cv_mem->cv_nfe++;
    if (retval < 0) {
      cvProcessError(cv_mem, CV_RHSFUNC_FAIL, "CVODE", "CVode",
                     MSGCV_RHSFUNC_FAILED, cv_mem->cv_tn);
      return(CV_RHSFUNC_FAIL);
    }
    if (retval > 0) {
      cvProcessError(cv_mem, CV_FIRST_RHSFUNC_ERR, "CVODE", "CVode",
                     MSGCV_RHSFUNC_FIRST);
      return(CV_FIRST_RHSFUNC_ERR);
    }
    if (cv_mem->cv_tstopset) {
      if ( (cv_mem->cv_tstop - cv_mem->cv_tn)*(tout - cv_mem->cv_tn) <= ZERO ) {
        cvProcessError(cv_mem, CV_ILL_INPUT, "CVODE", "CVode",
                       MSGCV_BAD_TSTOP, cv_mem->cv_tstop, cv_mem->cv_tn);
        return(CV_ILL_INPUT);
      }
    }
    cv_mem->cv_h = cv_mem->cv_hin;
    if ( (cv_mem->cv_h != ZERO) && ((tout-cv_mem->cv_tn)*cv_mem->cv_h < ZERO) ) {
      cvProcessError(cv_mem, CV_ILL_INPUT, "CVODE", "CVode", MSGCV_BAD_H0);
      return(CV_ILL_INPUT);
    }
    if (cv_mem->cv_h == ZERO) {
      tout_hin = tout;
      if ( cv_mem->cv_tstopset && (tout-cv_mem->cv_tn)*(tout-cv_mem->cv_tstop) > ZERO )
        tout_hin = cv_mem->cv_tstop;
      hflag = cvHin_gpu(cv_mem, tout_hin); //set cv_y
      if (hflag != CV_SUCCESS) {
        istate = cvHandleFailure_gpu(cv_mem, hflag);
        return(istate);
      }
    }
    rh = SUNRabs(cv_mem->cv_h)*cv_mem->cv_hmax_inv;
    if (rh > ONE) cv_mem->cv_h /= rh;
    if (SUNRabs(cv_mem->cv_h) < cv_mem->cv_hmin)
      cv_mem->cv_h *= cv_mem->cv_hmin/SUNRabs(cv_mem->cv_h);
    if (cv_mem->cv_tstopset) {
      if ( (cv_mem->cv_tn + cv_mem->cv_h - cv_mem->cv_tstop)*cv_mem->cv_h > ZERO )
        cv_mem->cv_h = (cv_mem->cv_tstop - cv_mem->cv_tn)*(ONE-FOUR*cv_mem->cv_uround);
    }
    cv_mem->cv_hscale = cv_mem->cv_h;
    cv_mem->cv_h0u    = cv_mem->cv_h;
    cv_mem->cv_hprime = cv_mem->cv_h;
    N_VScale(cv_mem->cv_h, cv_mem->cv_zn[1], cv_mem->cv_zn[1]);
    if (cv_mem->cv_ghfun) {
      N_VLinearSum(ONE, cv_mem->cv_zn[0], ONE, cv_mem->cv_zn[1], cv_mem->cv_tempv1);
      cv_mem->cv_ghfun(cv_mem->cv_tn + cv_mem->cv_h, cv_mem->cv_h, cv_mem->cv_tempv1,
                       cv_mem->cv_zn[0], cv_mem->cv_zn[1], cv_mem->cv_user_data,
                       cv_mem->cv_tempv2, cv_mem->cv_acor_init);
    }
    if (cv_mem->cv_nrtfn > 0) {
      retval = cvRcheck1_gpu(cv_mem);
      if (retval == CV_RTFUNC_FAIL) {
        cvProcessError(cv_mem, CV_RTFUNC_FAIL, "CVODE", "cvRcheck1",
                       MSGCV_RTFUNC_FAILED, cv_mem->cv_tn);
        return(CV_RTFUNC_FAIL);
      }
    }
  } /* end of first call block */
   //3. At following steps, perform stop tests:
  if (cv_mem->cv_nst > 0) {
    troundoff = FUZZ_FACTOR*cv_mem->cv_uround*(SUNRabs(cv_mem->cv_tn) + SUNRabs(cv_mem->cv_h));
    if (cv_mem->cv_nrtfn > 0) {
      irfndp = cv_mem->cv_irfnd;
      retval = cvRcheck2_gpu(cv_mem);
      if (retval == CLOSERT) {
        cvProcessError(cv_mem, CV_ILL_INPUT, "CVODE", "cvRcheck2",
                       MSGCV_CLOSE_ROOTS, cv_mem->cv_tlo);
        return(CV_ILL_INPUT);
      } else if (retval == CV_RTFUNC_FAIL) {
        cvProcessError(cv_mem, CV_RTFUNC_FAIL, "CVODE", "cvRcheck2",
                       MSGCV_RTFUNC_FAILED, cv_mem->cv_tlo);
        return(CV_RTFUNC_FAIL);
      } else if (retval == RTFOUND) {
        cv_mem->cv_tretlast = *tret = cv_mem->cv_tlo;
        return(CV_ROOT_RETURN);
      }
      if ( SUNRabs(cv_mem->cv_tn - cv_mem->cv_tretlast) > troundoff ) {
        retval = cvRcheck3_gpu(cv_mem);
        if (retval == CV_SUCCESS) {     /* no root found */
          cv_mem->cv_irfnd = 0;
          if ((irfndp == 1) && (itask == CV_ONE_STEP)) {
            cv_mem->cv_tretlast = *tret = cv_mem->cv_tn;
            N_VScale(ONE, cv_mem->cv_zn[0], yout);
            return(CV_SUCCESS);
          }
        } else if (retval == RTFOUND) {  /* a new root was found */
          cv_mem->cv_irfnd = 1;
          cv_mem->cv_tretlast = *tret = cv_mem->cv_tlo;
          return(CV_ROOT_RETURN);
        } else if (retval == CV_RTFUNC_FAIL) {  /* g failed */
          cvProcessError(cv_mem, CV_RTFUNC_FAIL, "CVODE", "cvRcheck3",
                         MSGCV_RTFUNC_FAILED, cv_mem->cv_tlo);
          return(CV_RTFUNC_FAIL);
        }
      }
    } /* end of root stop check */
    if ( (itask == CV_NORMAL) && ((cv_mem->cv_tn-tout)*cv_mem->cv_h >= ZERO) ) {
      cv_mem->cv_tretlast = *tret = tout;
      ier =  CVodeGetDky(cv_mem, tout, 0, yout);
      if (ier != CV_SUCCESS) {
        cvProcessError(cv_mem, CV_ILL_INPUT, "CVODE", "CVode",
                       MSGCV_BAD_TOUT, tout);
        return(CV_ILL_INPUT);
      }
      return(CV_SUCCESS);
    }
    if ( itask == CV_ONE_STEP &&
         SUNRabs(cv_mem->cv_tn - cv_mem->cv_tretlast) > troundoff ) {
      cv_mem->cv_tretlast = *tret = cv_mem->cv_tn;
      N_VScale(ONE, cv_mem->cv_zn[0], yout);
      return(CV_SUCCESS);
    }
    if ( cv_mem->cv_tstopset ) {
      if ( SUNRabs(cv_mem->cv_tn - cv_mem->cv_tstop) <= troundoff) {
        ier =  CVodeGetDky(cv_mem, cv_mem->cv_tstop, 0, yout);
        if (ier != CV_SUCCESS) {
          cvProcessError(cv_mem, CV_ILL_INPUT, "CVODE", "CVode",
                         MSGCV_BAD_TSTOP, cv_mem->cv_tstop, cv_mem->cv_tn);
          return(CV_ILL_INPUT);
        }
        cv_mem->cv_tretlast = *tret = cv_mem->cv_tstop;
        cv_mem->cv_tstopset = SUNFALSE;
        return(CV_TSTOP_RETURN);
      }
      if ( (cv_mem->cv_tn + cv_mem->cv_hprime - cv_mem->cv_tstop)*cv_mem->cv_h > ZERO ) {
        cv_mem->cv_hprime = (cv_mem->cv_tstop - cv_mem->cv_tn)*(ONE-FOUR*cv_mem->cv_uround);
        cv_mem->cv_eta = cv_mem->cv_hprime/cv_mem->cv_h;
      }
    }
  } /* end stopping tests block */
   //4. Looping point for internal steps
  if (cv_mem->cv_y == NULL) {
    cvProcessError(cv_mem, CV_BAD_DKY, "CVODE", "CVodeGetDky", MSGCV_NULL_DKY);
    return(CV_BAD_DKY);
  }
#ifdef CAMP_DEBUG_GPU
  cudaEventRecord(mCPU->startcvStep);
#endif
  for (int i = 0; i < md->n_cells; i++)//md->nCellsGPU
    sd->flagCells[i] = 99;
#ifdef ODE_WARNING
  mCPU->mdvCPU.cv_nhnil = cv_mem->cv_nhnil;
#endif
  mCPU->mdvCPU.cv_tretlast = cv_mem->cv_tretlast;
  mCPU->mdvCPU.cv_etaqm1 = cv_mem->cv_etaqm1;
  mCPU->mdvCPU.cv_etaq = cv_mem->cv_etaq;
  mCPU->mdvCPU.cv_etaqp1 = cv_mem->cv_etaqp1;
  mCPU->mdvCPU.cv_saved_tq5 = cv_mem->cv_saved_tq5;
  mCPU->mdvCPU.cv_tolsf = cv_mem->cv_tolsf;
  mCPU->mdvCPU.cv_indx_acor = cv_mem->cv_indx_acor;
  mCPU->mdvCPU.cv_hu = cv_mem->cv_hu;
  mCPU->mdvCPU.cv_jcur = cv_mem->cv_jcur;
  mCPU->mdvCPU.cv_nstlp = (int) cv_mem->cv_nstlp;
  mCPU->mdvCPU.cv_L = cv_mem->cv_L;
  mCPU->mdvCPU.cv_acnrm = cv_mem->cv_acnrm;
  mCPU->mdvCPU.cv_qwait = cv_mem->cv_qwait;
  mCPU->mdvCPU.cv_crate = cv_mem->cv_crate;
  mCPU->mdvCPU.cv_gamrat = cv_mem->cv_gamrat;
  mCPU->mdvCPU.cv_gammap = cv_mem->cv_gammap;
  mCPU->mdvCPU.cv_nst = cv_mem->cv_nst;
  mCPU->mdvCPU.cv_gamma = cv_mem->cv_gamma;
  mCPU->mdvCPU.cv_rl1 = cv_mem->cv_rl1;
  mCPU->mdvCPU.cv_eta = cv_mem->cv_eta;
  mCPU->mdvCPU.cv_q = cv_mem->cv_q;
  mCPU->mdvCPU.cv_qprime = cv_mem->cv_qprime;
  mCPU->mdvCPU.cv_h = cv_mem->cv_h;
  mCPU->mdvCPU.cv_next_h = cv_mem->cv_next_h;
  mCPU->mdvCPU.cv_hscale = cv_mem->cv_hscale;
  mCPU->mdvCPU.cv_hprime = cv_mem->cv_hprime;
  mCPU->mdvCPU.cv_hmin = cv_mem->cv_hmin;
  mCPU->mdvCPU.cv_tn = cv_mem->cv_tn;
  mCPU->mdvCPU.cv_etamax = cv_mem->cv_etamax;
  mCPU->mdvCPU.cv_maxncf = cv_mem->cv_maxncf;
  mCPU->mdvCPU.tret = *tret;
  cudaStream_t stream = 0;
  mGPU = sd->mGPU;
  double *ewt = NV_DATA_S(cv_mem->cv_ewt);
  double *acor = NV_DATA_S(cv_mem->cv_acor);
  double *tempv = NV_DATA_S(cv_mem->cv_tempv);
  double *ftemp = NV_DATA_S(cv_mem->cv_ftemp);
  double *cv_last_yn = N_VGetArrayPointer(cv_mem->cv_last_yn);
  double *cv_acor_init = N_VGetArrayPointer(cv_mem->cv_acor_init);
  double *youtArray = N_VGetArrayPointer(yout);
  double *cv_Vabstol = N_VGetArrayPointer(cv_mem->cv_Vabstol);
  cudaMemcpyAsync(mGPU->dewt, ewt, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice, stream);
  cudaMemcpyAsync(mGPU->cv_acor, acor, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice, stream);
  cudaMemcpyAsync(mGPU->dtempv, tempv, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice, stream);
  cudaMemcpyAsync(mGPU->dftemp, ftemp, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice, stream);
  cudaMemcpyAsync(mGPU->cv_last_yn, cv_last_yn, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice, stream);
  cudaMemcpyAsync(mGPU->cv_acor_init, cv_acor_init, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice, stream);
  cudaMemcpyAsync(mGPU->yout, youtArray, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice, stream);
  cudaMemcpyAsync(mGPU->cv_Vabstol, cv_Vabstol, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice, stream);
  for (int i = 0; i <= cv_mem->cv_qmax; i++) {//cv_qmax+1 (6)?
    double *zn = NV_DATA_S(cv_mem->cv_zn[i]);
    cudaMemcpyAsync((i * mGPU->nrows + mGPU->dzn), zn, mGPU->nrows * sizeof(double), cudaMemcpyHostToDevice, stream);
  }
  cudaMemcpyAsync(mGPU->flagCells, sd->flagCells, mGPU->n_cells * sizeof(int), cudaMemcpyHostToDevice,
                  stream);
  cudaMemcpyAsync(mGPU->state, md->total_state, mCPU->state_size, cudaMemcpyHostToDevice, stream);
  mGPU->cv_tstop = cv_mem->cv_tstop;
  mGPU->cv_tstopset = cv_mem->cv_tstopset;
  mGPU->cv_nlscoef = cv_mem->cv_nlscoef;
  mGPU->init_time_step = sd->init_time_step;
  mGPU->cv_mxstep = cv_mem->cv_mxstep;
  mGPU->cv_uround = cv_mem->cv_uround;
  mGPU->cv_hmax_inv = cv_mem->cv_hmax_inv;
  mGPU->cv_reltol = cv_mem->cv_reltol;
  mGPU->cv_maxcor = cv_mem->cv_maxcor;
  mGPU->cv_qmax = cv_mem->cv_qmax;
  mGPU->cv_maxnef = cv_mem->cv_maxnef;
  mGPU->tout = tout;
  for (int i = 0; i < mGPU->n_cells; i++) {
    cudaMemcpyAsync(mGPU->cv_l + i * L_MAX, cv_mem->cv_l, L_MAX * sizeof(double), cudaMemcpyHostToDevice, stream);
    cudaMemcpyAsync(mGPU->cv_tau + i * (L_MAX + 1), cv_mem->cv_tau, (L_MAX + 1) * sizeof(double),
                    cudaMemcpyHostToDevice, stream);
    cudaMemcpyAsync(mGPU->cv_tq + i * (NUM_TESTS + 1), cv_mem->cv_tq, (NUM_TESTS + 1) * sizeof(double),
                    cudaMemcpyHostToDevice, stream);
    cudaMemcpyAsync(&mGPU->sCells[i], &mCPU->mdvCPU, sizeof(ModelDataVariable), cudaMemcpyHostToDevice, stream);
  }
  int len_cell = mGPU->nrows / mGPU->n_cells;
  int threads_block = len_cell;
  int blocks = mGPU->n_cells;
  int n_shr_memory = nextPowerOfTwoCVODE(len_cell);
  int n_shr_empty = mGPU->n_shr_empty = n_shr_memory - threads_block;
  cudaGlobalCVode <<<blocks, threads_block, n_shr_memory * sizeof(double), stream>>>(*mGPU);
  cudaMemcpyAsync(cv_acor_init, mGPU->cv_acor_init, mGPU->nrows * sizeof(double), cudaMemcpyDeviceToHost, stream);
  cudaMemcpyAsync(youtArray, mGPU->yout, mGPU->nrows * sizeof(double), cudaMemcpyDeviceToHost, stream);
  for (int i = 0; i <= cv_mem->cv_qmax; i++) {//cv_qmax+1 (6)?
    double *zn = NV_DATA_S(cv_mem->cv_zn[i]);
    cudaMemcpyAsync(zn, (i * mGPU->nrows + mGPU->dzn), mGPU->nrows * sizeof(double), cudaMemcpyDeviceToHost, stream);
  }
  cudaMemcpyAsync(sd->flagCells, mGPU->flagCells, mGPU->n_cells * sizeof(int), cudaMemcpyDeviceToHost, stream);
#ifdef DEV_CPUGPU
  printf("todo DEV_CPUGPU: Too much execution time \n");
  int nCellsGPUPerc=sd->nCellsGPUPerc;
  int nCellsGPU = md->n_cells*nCellsGPUPerc;
  int nCellsCPU= md->n_cells - nCellsGPU;
  double* stotal_state=md->total_state;
  md->total_state+=md->n_per_cell_state_var*nCellsGPU;
  N_Vector sy = N_VClone(sd->y); //todo nvclone(y) or just y=sy?
  //N_Vector sy = y;
  N_Vector yAux = N_VNew_Serial(md->n_per_cell_dep_var * nCellsCPU);
  double *yAuxArray = N_VGetArrayPointer(yout);
  double *youtArray = N_VGetArrayPointer(yout);
  for (int i = 0; i < md->n_per_cell_dep_var*nCellsCPU; i++){
    yAuxArray[i]=youtArray[i+md->n_per_cell_state_var*nCellsGPU];
  }
  sd->y=N_VClone(yAux);
  istate = CVode(cvode_mem, tout, yout, tret, itask);
  md->total_state=stotal_state;
  sd->y=N_VClone(sy);
  yAuxArray= N_VGetArrayPointer(sd->y);
  for (int i = 0; i < md->n_per_cell_dep_var*nCellsCPU; i++){
    yAuxArray[i+md->n_per_cell_state_var*nCellsGPU]=youtArray[i];
  }
  if (istate !=CV_SUCCESS ){
    printf("ERROR in solving the CPU part of CPU+GPU solving");
    return(istate);
  }
#endif
  cudaDeviceSynchronize();
  mGPU = sd->mGPU;
#ifdef CAMP_DEBUG_GPU
    cudaEventRecord(mCPU->stopcvStep);
    cudaEventSynchronize(mCPU->stopcvStep);
    float mscvStep = 0.0;
    cudaEventElapsedTime(&mscvStep, mCPU->startcvStep, mCPU->stopcvStep);
    mCPU->timecvStep+= mscvStep/1000;
    //printf("mCPU->timecvStep %lf\n",mCPU->timecvStep);
#ifndef CAMP_PROFILE_DEVICE_FUNCTIONS
    cudaMemcpy(&mCPU->mdvCPU, mGPU->mdvo, sizeof(ModelDataVariable), cudaMemcpyDeviceToHost);
    mCPU->timeBiConjGrad=mCPU->timecvStep*mCPU->mdvCPU.dtBCG/mCPU->mdvCPU.dtcudaDeviceCVode;
    mCPU->counterBCG+= mCPU->mdvCPU.counterBCG;
    //printf("mCPU->mdvCPU.dtcudaDeviceCVode %lf\n",mCPU->mdvCPU.dtcudaDeviceCVode);
#else
    mCPU->timeBiConjGrad=0.;
    mCPU->counterBCG+=0;
#endif
#endif
  istate = CV_SUCCESS;
  for (int i = 0; i < mGPU->n_cells; i++) {
    if (sd->flagCells[i] != istate) {
      istate = sd->flagCells[i];
      break;
    }
  }
  if(istate!=CV_SUCCESS ) {
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    printf("cudaCVode2 kflag %d rank %d\n",istate,rank);
    istate = cvHandleFailure_gpu(cv_mem, istate);
  }
  return(istate);
}

void solver_get_statistics_gpu(SolverData *sd){
  ModelDataGPU *mGPU = sd->mGPU;
  ModelDataCPU *mCPU = &(sd->mCPU);
  cudaMemcpy(&mCPU->mdvCPU,mGPU->mdvo,sizeof(ModelDataVariable),cudaMemcpyDeviceToHost);
}

void solver_reset_statistics_gpu(SolverData *sd){
  ModelDataGPU *mGPU = sd->mGPU;
  ModelDataCPU *mCPU = &(sd->mCPU);
  //printf("solver_reset_statistics_gpu\n");
  mGPU = sd->mGPU;
#ifdef CAMP_DEBUG_GPU
  for (int i = 0; i < mGPU->n_cells; i++){
    cudaMemcpy(&mGPU->sCells[i], &mCPU->mdvCPU, sizeof(ModelDataVariable), cudaMemcpyHostToDevice);
  }
#endif
}