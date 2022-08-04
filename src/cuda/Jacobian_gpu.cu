/* Copyright (C) 2021 Barcelona Supercomputing Center and University of
 * Illinois at Urbana-Champaign
 * SPDX-License-Identifier: MIT
 */

extern "C" {
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "Jacobian_gpu.h"
//#include "../debug_and_stats/camp_debug_2.h"

#define BUFFER_SIZE 10
#define SMALL_NUMBER 1e-90

int jacobian_initialize_gpu(SolverData *sd) {
  ModelDataGPU *mGPU = sd->mGPU;
  Jacobian *jac = &sd->jac;

  int offset_nnz = 0;
  for (int iDevice = 0; iDevice < sd->nDevices; iDevice++) {
    cudaSetDevice(iDevice);
    sd->mGPU = &(sd->mGPUs[iDevice]);
    mGPU = sd->mGPU;

    JacobianGPU *jacgpu = &(mGPU->jac);
    cudaMalloc((void **) &jacgpu->num_elem, 1 * sizeof(jacgpu->num_elem));
    cudaMemcpy(jacgpu->num_elem, &jac->num_elem, 1 * sizeof(jacgpu->num_elem), cudaMemcpyHostToDevice);

    int num_elem = jac->num_elem * mGPU->n_cells;
    int num_spec = jac->num_spec;
    cudaMalloc((void **) &(jacgpu->production_partials), num_elem * sizeof(jacgpu->production_partials));
    cudaMalloc((void **) &(jacgpu->loss_partials), num_elem * sizeof(jacgpu->loss_partials));

    //printf("jac->num_elem %d\n",jac->num_elem);
    //printf("jac->num_spec %d\n",num_spec);

    cudaMemcpy(jacgpu->production_partials, jac->production_partials+offset_nnz, num_elem * sizeof(jacgpu->production_partials),
               cudaMemcpyHostToDevice);
    cudaMemcpy(jacgpu->loss_partials, jac->loss_partials+offset_nnz, num_elem * sizeof(jacgpu->loss_partials),
               cudaMemcpyHostToDevice);

    //print_int(jac->col_ptrs,jac->num_spec+1,"jac->col_ptrs");

    /*

    for (int i = 0; i < jac->num_elem; ++i) {
      printf(" jac.production_partials[%d]=%d\n",i,jac->production_partials[i]);
    }

    for (unsigned int i_col = 0; i_col < jac->num_spec; ++i_col) {
      printf(" jac.col_ptrs[%d]=%d\n",i_col,jac->col_ptrs[i_col]);
      for (int i_elem = jac->col_ptrs[i_col];
           i_elem < jac->col_ptrs[i_col + 1]; ++i_elem) {
        printf(" jac.production_partials[%d]=%d\n",i_elem,jac->production_partials[i_elem]);
      }
    }
*/

    /*
    for (int i=0; i<jac->num_spec+1; i++){
      printf("jac->col_ptrs[%d]=%d\n",i,jac->col_ptrs[i]);
    }

    for (unsigned int i_col = 0; i_col < jac.num_spec; ++i_col){
      printf("\n");

    }
*/

    offset_nnz += num_elem;
  }

  return 1;
}

#ifdef __CUDA_ARCH__
__host__ __device__
#endif
void jacobian_reset_gpu(JacobianGPU jac) {

#ifdef __CUDA_ARCH__

  __syncthreads();
#ifndef DEV_REMOVE_threadIdx0

  //int i_col = threadIdx.x;
  //int num_spec = jac.num_spec[0];

  //fine

  int nnz = jac.num_elem[0];
  int n_iters = nnz / blockDim.x;
  for (int i = 0; i < n_iters; i++) {
    int j = threadIdx.x + i*blockDim.x;
    jac.production_partials[j] = 0.0;
    jac.loss_partials[j] = 0.0;
  }
  int residual=nnz-(blockDim.x*n_iters);
  if(threadIdx.x < residual){
    int j = threadIdx.x + n_iters*blockDim.x;
    jac.production_partials[j] = 0.0;
    jac.loss_partials[j] = 0.0;
    }

  //wrong because 73 threads but 79 num_spec
  /*
  if(threadIdx.x==0){
    for(int i_col=0; i_col<blockDim.x;i_col++){
      for (int i_elem = jac.col_ptrs[i_col]; i_elem < jac.col_ptrs[i_col+1]; i_elem++) {
        //if(blockIdx.x==0)printf("%d ",i_elem);
        jac.production_partials[i_elem] = 0.0;
        jac.loss_partials[i_elem] = 0.0;
      }
    }
    //if(blockIdx.x==0)printf("\n");
  }
   */

#else

  if(threadIdx.x==0){
    int nnz = jac.num_elem[0];
    //for (int n = (nnz/gridDim.x)*blockIdx.x; n < (nnz/gridDim.x)*(blockIdx.x+1); n++) {
    for (int n = 0; n < nnz; n++) {
      jac.production_partials[n] = 0.0;
      jac.loss_partials[n] = 0.0;
    }
 }

#endif

__syncthreads();

#endif

}

#ifdef __CUDA_ARCH__
__host__ __device__
#endif
void jacobian_output_gpu(JacobianGPU jac, double *dest_array) {

#ifdef __CUDA_ARCH__

  __syncthreads();

#ifndef DEV_REMOVE_threadIdx0

  int nnz = jac.num_elem[0];
  int n_iters = nnz / blockDim.x;
  for (int i = 0; i < n_iters; i++) {
    int j = threadIdx.x + i*blockDim.x;
    dest_array[j] = jac.production_partials[j] - jac.loss_partials[j];
  }
  int residual=nnz-(blockDim.x*n_iters);
  if(threadIdx.x < residual){
    int j = threadIdx.x + n_iters*blockDim.x;
    dest_array[j] = jac.production_partials[j] - jac.loss_partials[j];
  }

#else

  if(threadIdx.x==0){
    int nnz = jac.num_elem[0];
    //for (int n = (nnz/gridDim.x)*blockIdx.x; n < (nnz/gridDim.x)*(blockIdx.x+1); n++) {
    for (int n = 0; n < nnz; n++) {
        //check_isnanld(&drf_dy,1,"post jacobian_output drf_dy");
        //check_isnanld(&drr_dy,1,"post jacobian_output drr_dy");

        dest_array[n] = jac.production_partials[n] - jac.loss_partials[n];
    }
  }

#endif

__syncthreads();
#endif

}

#ifdef __CUDA_ARCH__
__host__ __device__
#endif
void jacobian_add_value_gpu(JacobianGPU jac, unsigned int elem_id,
                            unsigned int prod_or_loss,
                            double jac_contribution) {
#ifdef __CUDA_ARCH__
  if (prod_or_loss == JACOBIAN_PRODUCTION)
    atomicAdd_block(&(jac.production_partials[elem_id]),jac_contribution);
  if (prod_or_loss == JACOBIAN_LOSS)
    atomicAdd_block(&(jac.loss_partials[elem_id]),jac_contribution);
#else
  if (prod_or_loss == JACOBIAN_PRODUCTION)
    jac.production_partials[elem_id] += jac_contribution;
  if (prod_or_loss == JACOBIAN_LOSS)
    jac.loss_partials[elem_id] += jac_contribution;
#endif

  //check_isnanld(&jac_contribution,1,"post jacobian_add_value jac_contribution");
}

}
