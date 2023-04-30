/* Copyright (C) 2021 Barcelona Supercomputing Center and University of
 * Illinois at Urbana-Champaign
 * SPDX-License-Identifier: MIT
 */

#include<iostream>
#include<cuda.h>
#include<cuda_runtime.h>
#include<cuda_runtime_api.h>

#include "libsolv.h"

//#include<cublas.h>
//#include<cublas_v2.h>

using namespace std;

//
//dAthreads
//
// Para reservar memoria Double e Int
extern "C++" void cudaMallocDouble(double* &vector,int size)
{
	cudaMalloc((void**)&vector,size*sizeof(double));
}

extern "C++" void cudaMallocInt(int* &vector,int size)
{
	cudaMalloc((void**)&vector,size*sizeof(int));
}

// Para copiar a CPU->GPU Double e Int
extern "C++" void cudaMemcpyDToGpu(double* h_vect,double* d_vect,int size )
{
  cudaMemcpy(d_vect,h_vect,size*sizeof(double),cudaMemcpyHostToDevice);
}

extern "C++" void cudaMemcpyIToGpu(int* h_vect,int* d_vect,int size )
{
		cudaMemcpy(d_vect,h_vect,size*sizeof(int),cudaMemcpyHostToDevice);
}

// Para copiar a GPU->CPU Double e Int
extern "C++" void cudaMemcpyIToCpu(int* h_vect, int* d_vect,int size )
{
		cudaMemcpy(h_vect,d_vect,size*sizeof(int),cudaMemcpyDeviceToHost);
}

extern "C++" void cudaMemcpyDToCpu(double* h_vect, double* d_vect,int size )
{
  cudaMemcpy(h_vect,d_vect,size*sizeof(double),cudaMemcpyDeviceToHost);
}

// Para liberar memoria
extern "C++" void cudaFreeMem(void* vector)
{
	cudaFree(vector);
}

extern "C++" void cudaGetLastErrorC(){
     cudaError_t error;
     error=cudaGetLastError();
     if(error!= cudaSuccess)
     {
       cout<<" ERROR INSIDE A CUDA FUNCTION: "<<error<<" "<<cudaGetErrorString(error)<<endl;
       exit(0);
     }
}

__global__ void cudamatScaleAddI(int nrows, double* dA, int* djA, int* diA, double alpha)
{
	int row= threadIdx.x + blockDim.x*blockIdx.x;
  if(row < nrows)
  {
    int jstart = diA[row];
    int jend   = diA[row+1];
    for(int j=jstart; j<jend; j++)
    {
      if(djA[j]==row)
      {
        dA[j] = 1.0 + alpha*dA[j];
      }
      else{
        dA[j] = alpha*dA[j];
      }
    }
  }
}

// A = I - gamma*J
// dA  : Matrix values (nnz size)
// djA : Matrix columns (nnz size)
// diA : Matrix rows (nrows+1 size)
// alpha : Scale factor
extern "C++" void gpu_matScaleAddI(int nrows, double* dA, int* djA, int* diA, double alpha, int blocks, int threads)
{

   blocks = (nrows+threads-1)/threads;

   dim3 dimGrid(blocks,1,1);
   dim3 dimBlock(threads,1,1);

  cudamatScaleAddI<<<dimGrid,dimBlock>>>(nrows, dA, djA, diA, alpha);
}

__global__
void check_input_gpud(double *x, int len, int var_id)
{
  int i = blockIdx.x * blockDim.x + threadIdx.x;

  printf("%d[%d]=%-le\n",var_id,i,x[i]);

}

// Diagonal precond
__global__ void cudadiagprecond(int nrows, double* dA, int* djA, int* diA, double* ddiag)
{
  int row= threadIdx.x + blockDim.x*blockIdx.x;

#ifdef DEBUG_cudadiagprecond


#endif

  if(row < nrows){
    int jstart=diA[row];
    int jend  =diA[row+1];
    for(int j=jstart;j<jend;j++){
      if(djA[j]==row){
        if(dA[j]!=0.0)
          ddiag[row]= 1.0/dA[j];
        else{
          //printf("cudadiagprecond else\n");
          ddiag[row]= 1.0;
        }
      }
    }
  }

}

extern "C++" void gpu_diagprecond(int nrows, double* dA, int* djA, int* diA, double* ddiag, int blocks, int threads)
{

  blocks = (nrows+threads-1)/threads;

  dim3 dimGrid(blocks,1,1);
  dim3 dimBlock(threads,1,1);

  cudadiagprecond<<<dimGrid,dimBlock>>>(nrows, dA, djA, diA, ddiag);
  //check_input_gpud<< < 1, 5>> >(ddiag,nrows,0);
}

// y = constant
__global__ void cudasetconst(double* dy,double constant,int nrows)
{
	int row= threadIdx.x + blockDim.x*blockIdx.x;
   	if(row < nrows){
		dy[row]=constant;
	}
}

extern "C++" void gpu_yequalsconst(double *dy, double constant, int nrows, int blocks, int threads)
{
   dim3 dimGrid(blocks,1,1);
   dim3 dimBlock(threads,1,1);

   cudasetconst<<<dimGrid,dimBlock>>>(dy,constant,nrows);

}


// x=A*b
__global__ void cudaSpmvCSR(double* dx, double* db, int nrows, double* dA, int* djA, int* diA)
{
	int row= threadIdx.x + blockDim.x*blockIdx.x;
  if(row < nrows)
  {
    int jstart = diA[row];
    int jend   = diA[row+1];
    double sum = 0.0;
    for(int j=jstart; j<jend; j++)
    {
      sum+= db[djA[j]]*dA[j];
    }
    dx[row]=sum;
	}

}

__global__ void cudaSpmvCSC(double* dx, double* db, int nrows, double* dA, int* djA, int* diA)
{
  double mult;
  int row= threadIdx.x + blockDim.x*blockIdx.x;
  if(row < nrows)
  {
    int jstart = diA[row];
    int jend   = diA[row+1];
    for(int j=jstart; j<jend; j++)
    {
      mult = db[row]*dA[j];
      atomicAdd(&(dx[djA[j]]),mult);
    }
	}
}

extern "C++" void gpu_spmv(double* dx ,double* db, int nrows, double* dA, int *djA,int *diA,int blocks,int  threads)
{
   dim3 dimGrid(blocks,1,1);
   dim3 dimBlock(threads,1,1);

#ifndef CSR_SPMV_CPU

   cudaSpmvCSR<<<dimGrid,dimBlock>>>(dx, db, nrows, dA, djA, diA);

#else
    cudasetconst<<<dimGrid,dimBlock>>>(dx, 0.0, nrows);
    cudaSpmvCSC<<<dimGrid,dimBlock>>>(dx, db, nrows, dA, djA, diA);
#endif

}

// y= a*x+ b*y
__global__ void cudaaxpby(double* dy,double* dx, double a, double b, int nrows)
{
	int row= threadIdx.x + blockDim.x*blockIdx.x;
   	if(row < nrows){
		dy[row]= a*dx[row] + b*dy[row];
	}
}

extern "C++" void gpu_axpby(double* dy ,double* dx, double a, double b, int nrows, int blocks, int threads)
{

   dim3 dimGrid(blocks,1,1);
   dim3 dimBlock(threads,1,1);

   cudaaxpby<<<dimGrid,dimBlock>>>(dy,dx,a,b,nrows);
}

// y = x
__global__ void cudayequalsx(double* dy,double* dx,int nrows)
{
	int row= threadIdx.x + blockDim.x*blockIdx.x;
   	if(row < nrows){
		dy[row]=dx[row];
	}
}

extern "C++" void gpu_yequalsx(double *dy, double* dx, int nrows, int blocks, int threads)
{
   dim3 dimGrid(blocks,1,1);
   dim3 dimBlock(threads,1,1);

   cudayequalsx<<<dimGrid,dimBlock>>>(dy,dx,nrows);

}

__global__ void cudareducey(double *g_odata, unsigned int n)
{
  extern __shared__ double sdata[];
  unsigned int tid = threadIdx.x;

  double mySum =  (tid < n) ? g_odata[tid] : 0;

  sdata[tid] = mySum;
  __syncthreads();

  for (unsigned int s=blockDim.x/2; s>0; s>>=1)
  {
    if (tid < s)
      sdata[tid] = mySum = mySum + sdata[tid + s];

    __syncthreads();
  }

  if (tid == 0) g_odata[blockIdx.x] = sdata[0];
}

__global__ void cudadotxy(double *g_idata1, double *g_idata2, double *g_odata, unsigned int n)
{
  extern __shared__ double sdata[];
  unsigned int tid = threadIdx.x;
  unsigned int i = blockIdx.x*(blockDim.x*2) + threadIdx.x;//*2 because init blocks is half
  //unsigned int i = blockIdx.x*blockDim.x + threadIdx.x;//*2 because init blocks is half

  double mySum = (i < n) ? g_idata1[i]*g_idata2[i] : 0;

  if (i + blockDim.x < n)
    mySum += g_idata1[i+blockDim.x]*g_idata2[i+blockDim.x];

  sdata[tid] = mySum;
  __syncthreads();

  //for (unsigned int s=(blockDim.x+1)/2; s>0; s>>=1)
  for (unsigned int s=blockDim.x/2; s>0; s>>=1)
  {
    if (tid < s)
      sdata[tid] = mySum = mySum + sdata[tid + s];

    __syncthreads();
  }

  if (tid == 0) g_odata[blockIdx.x] = sdata[0];
}

//threads need to be pow of 2 //todo h_temp not needed
extern "C++" double gpu_dotxy(double* vec1, double* vec2, double* h_temp, double* d_temp, int nrows, int blocks,int threads)
{
  double sum;
  dim3 dimGrid(blocks,1,1);
  dim3 dimBlock(threads,1,1);

  //threads*sizeof(double)
  cudadotxy<<<dimGrid,dimBlock,threads*sizeof(double)>>>(vec1,vec2,d_temp,nrows);
  cudaMemcpy(&sum, d_temp, sizeof(double), cudaMemcpyDeviceToHost);
  //printf("rho1 %f", sum);

  int redsize= sqrt(blocks) +1;
  redsize=pow(2,redsize);

  dim3 dimGrid2(1,1,1);
  dim3 dimBlock2(redsize,1,1);

  cudareducey<<<dimGrid2,dimBlock2,redsize*sizeof(double)>>>(d_temp,blocks);
  cudaMemcpy(&sum, d_temp, sizeof(double), cudaMemcpyDeviceToHost);

  return sum;

/*
  cudaMemcpy(h_temp, d_temp, blocks * sizeof(double), cudaMemcpyDeviceToHost);
  double sum=0;
  for(int i=0;i<blocks;i++)
  {
    sum+=h_temp[i];
  }
  return sum;
*/
  /*dim3 dimGrid2(1,1,1);
  dim3 dimBlock2(blocks,1,1);

  //Cuda only sum kernel call
  //cudareducey<<<dimGrid2,dimBlock2,blocks*sizeof(double)>>>(d_temp,blocks); //Takes quasi WAY MORE than cpu calc

  cudaMemcpy(h_temp, d_temp, sizeof(double), cudaMemcpyDeviceToHost);
  return h_temp[0];*/
}

/*
extern "C++" double gpu_dotxy(double *dy, double* dx, int nrows)
{
   double dot=0.0;
   cublasHandle_t hl;
   cublasCreate(&hl);

   cublasDdot(hl,nrows,dy,1,dx,1,&dot);

   cublasDestroy(hl);
   return dot;
}
*/

// z= a*z + x + b*y
__global__ void cudazaxpbypc(double* dz, double* dx,double* dy, double a, double b, int nrows)
{
	int row= threadIdx.x + blockDim.x*blockIdx.x;
   	if(row < nrows){
		dz[row]=a*dz[row]  + dx[row] + b*dy[row];
	}
}

extern "C++" void gpu_zaxpbypc(double* dz, double* dx ,double* dy, double a, double b, int nrows, int blocks, int threads)
{

   dim3 dimGrid(blocks,1,1);
   dim3 dimBlock(threads,1,1);

   cudazaxpbypc<<<dimGrid,dimBlock>>>(dz,dx,dy,a,b,nrows);
}

// z= x*y
__global__ void cudamultxy(double* dz, double* dx,double* dy, int nrows)
{
	int row= threadIdx.x + blockDim.x*blockIdx.x;
   	if(row < nrows){
		dz[row]=dx[row]*dy[row];
	}
}

extern "C++" void gpu_multxy(double* dz, double* dx ,double* dy, int nrows, int blocks, int threads)
{

   dim3 dimGrid(blocks,1,1);
   dim3 dimBlock(threads,1,1);

   cudamultxy<<<dimGrid,dimBlock>>>(dz,dx,dy,nrows);
}

// a*x + b*y = z
//__global__ void cudazaxpby(double* dz, double* dx,double* dy, double a, double b, int nrows)
__global__ void cudazaxpby(double a, double* dx, double b, double* dy, double* dz, int nrows)
{
	int row= threadIdx.x + blockDim.x*blockIdx.x;
   	if(row < nrows){
		dz[row]=a*dx[row] + b*dy[row];
	}
}

extern "C++" void gpu_zaxpby(double a, double* dx, double b, double* dy, double* dz, int nrows, int blocks, int threads)
{

   dim3 dimGrid(blocks,1,1);
   dim3 dimBlock(threads,1,1);

  cudazaxpby<<<dimGrid,dimBlock>>>(a,dx,b,dy,dz,nrows);
}

// y= a*x + y
__global__ void cudaaxpy(double* dy,double* dx, double a, int nrows)
{
	int row= threadIdx.x + blockDim.x*blockIdx.x;
   	if(row < nrows){
		dy[row]=a*dx[row] + dy[row];
	}
}

extern "C++" void gpu_axpy(double* dy, double* dx ,double a, int nrows, int blocks, int threads)
{

   dim3 dimGrid(blocks,1,1);
   dim3 dimBlock(threads,1,1);

   cudaaxpy<<<dimGrid,dimBlock>>>(dy,dx,a,nrows);
}

// sqrt(sum ( (x_i*y_i)^2)/n)
__global__ void cudaDVWRMS_Norm(double *g_idata1, double *g_idata2, double *g_odata, unsigned int n)
{
  extern __shared__ double sdata[];
  unsigned int tid = threadIdx.x;
  unsigned int i = blockIdx.x*(blockDim.x*2) + threadIdx.x;

  double mySum = (i < n) ? g_idata1[i]*g_idata1[i]*g_idata2[i]*g_idata2[i] : 0;

  if (i + blockDim.x < n)
    mySum += g_idata1[i+blockDim.x]*g_idata1[i+blockDim.x]*g_idata2[i+blockDim.x]*g_idata2[i+blockDim.x];

  sdata[tid] = mySum;
  __syncthreads();

  for (unsigned int s=blockDim.x/2; s>0; s>>=1)
  {
    if (tid < s)
      sdata[tid] = mySum = mySum + sdata[tid + s];

    __syncthreads();
  }

  if (tid == 0) g_odata[blockIdx.x] = sdata[0];
}

extern "C++" double gpu_VWRMS_Norm(int n, double* vec1,double* vec2,double* h_temp,double* d_temp, int blocks,int threads)
{
  dim3 dimGrid(blocks,1,1);
  dim3 dimBlock(threads,1,1);

  cudaDVWRMS_Norm<<<dimGrid,dimBlock,threads*sizeof(double)>>>(vec1,vec2,d_temp,n);

  //cudaMemcpy(h_temp, d_temp, blocks * sizeof(double), cudaMemcpyDeviceToHost);

  int redsize= sqrt(blocks) +1;
  redsize=pow(2,redsize);

  dim3 dimGrid2(1,1,1);
  dim3 dimBlock2(redsize,1,1);

  cudareducey<<<dimGrid2,dimBlock2,redsize*sizeof(double)>>>(d_temp,blocks);

  double sum;
  cudaMemcpy(&sum, d_temp, sizeof(double), cudaMemcpyDeviceToHost);

  return sqrt(sum/n);

/*
  double sum=0;
  for(int i=0;i<blocks;i++)
  {
    sum+=h_temp[i];
  }
  return sqrt(sum/n);
  */
}

// y=alpha*y
__global__ void cudascaley(double* dy, double a, int nrows)
{
  int row= threadIdx.x + blockDim.x*blockIdx.x;
  if(row < nrows){
    dy[row]=a*dy[row];
  }
}

extern "C++" void gpu_scaley(double* dy, double a, int nrows, int blocks, int threads)
{
  dim3 dimGrid(blocks,1,1);
  dim3 dimBlock(threads,1,1);

  cudascaley<<<dimGrid,dimBlock>>>(dy,a,nrows);
}

// Device functions (equivalent to global functions but in device to allow calls from gpu)
__device__ void cudaDeviceBCGprecond(double* dA, int* djA, int* diA, double* ddiag, double alpha){
int row= threadIdx.x + blockDim.x*blockIdx.x;
int nnz=diA[blockDim.x];
  for(int j=diA[threadIdx.x];j<diA[threadIdx.x+1];j++){
    if(djA[j]==threadIdx.x){
      dA[j+nnz*blockIdx.x] = 1.0 + alpha*dA[j+nnz*blockIdx.x];
      if(dA[j+nnz*blockIdx.x]!=0.0){
        ddiag[row]= 1.0/dA[j+nnz*blockIdx.x];
       }else{
        ddiag[row]= 1.0;
      }
    }else{
      dA[j+nnz*blockIdx.x] = alpha*dA[j+nnz*blockIdx.x];
    }
  }
}

// y = constant
__device__ void cudaDevicesetconst(double* dy,double constant){
  int row= threadIdx.x + blockDim.x*blockIdx.x;
  dy[row]=constant;
}

// x=A*b
__device__ void cudaDeviceSpmvCSR(double* dx, double* db, double* dA, int* djA, int* diA){
  __syncthreads();
  int row= threadIdx.x + blockDim.x*blockIdx.x;
  double sum = 0.0;
  int nnz=diA[blockDim.x];
  for(int j=diA[threadIdx.x]; j<diA[threadIdx.x+1]; j++){
    sum+= db[djA[j]+blockDim.x*blockIdx.x]*dA[j+nnz*blockIdx.x];
  }
  dx[row]=sum;
  __syncthreads();
}

__device__ void cudaDeviceSpmvCSC_block(double* dx, double* db, double* dA, int* djA, int* diA){
  int row = threadIdx.x + blockDim.x*blockIdx.x;
  __syncthreads();
  dx[row]=0.0;
  __syncthreads(); //Multiple threads can save to the same row

  int nnz=diA[blockDim.x];
  for(int j=diA[threadIdx.x]; j<diA[threadIdx.x+1]; j++){
    double mult = db[row]*dA[j+nnz*blockIdx.x];
    atomicAdd_block(&(dx[djA[j]+blockDim.x*blockIdx.x]),mult);
  //		dx[djA[j]]+= db[row]*dA[j];
  }
  __syncthreads();
}

__device__ void cudaDeviceSpmv(double* dx, double* db, double* dA, int* djA, int* diA){
#ifndef USE_CSR_ODE_GPU
  cudaDeviceSpmvCSR(dx,db,dA,djA,diA);
#else
  cudaDeviceSpmvCSC_block(dx,db,dA,djA,diA);
#endif
}

// y= a*x+ b*y
__device__ void cudaDeviceaxpby(double* dy,double* dx, double a, double b, int nrows)
{
  int row= threadIdx.x + blockDim.x*blockIdx.x;
  dy[row]= a*dx[row] + b*dy[row];
}

// y = x
__device__ void cudaDeviceyequalsx(double* dy,double* dx,int nrows)
{
  int row= threadIdx.x + blockDim.x*blockIdx.x;
    dy[row]=dx[row];
}

__device__ void cudaDevicemin(double *g_odata, double in, volatile double *sdata, int n_shr_empty)
{
  unsigned int tid = threadIdx.x;

  __syncthreads();

  sdata[tid] = in;

  __syncthreads();
  //first threads update empty positions
  if(tid<n_shr_empty)
    sdata[tid+blockDim.x]=sdata[tid];
  __syncthreads(); //Not needed (should)

  for (unsigned int s=(blockDim.x+n_shr_empty)/2; s>0; s>>=1)
  {
    if (tid < s){
      if(sdata[tid + s] < sdata[tid] ) sdata[tid]=sdata[tid + s];
    }
    __syncthreads();
  }

  __syncthreads();
  *g_odata = sdata[0];
  __syncthreads();

}

__device__ void cudaDevicemaxI(int *g_odata, int in, volatile double *sdata, int n_shr_empty)
{
  unsigned int tid = threadIdx.x;
  unsigned int i = blockIdx.x*blockDim.x + threadIdx.x;

  __syncthreads();

  sdata[tid] = in;

  __syncthreads();
  //first threads update empty positions
  if(tid<n_shr_empty)
    sdata[tid+blockDim.x]=sdata[tid];
  __syncthreads(); //Not needed (should)

  for (unsigned int s=(blockDim.x+n_shr_empty)/2; s>0; s>>=1)
  {
    if (tid < s){
      if(sdata[tid + s] > sdata[tid] ) sdata[tid]=sdata[tid + s];
    }
    __syncthreads();
  }

  __syncthreads();
  *g_odata = sdata[0];
  __syncthreads();

}

__device__ void cudaDeviceaddI(int *g_odata, int in, volatile double *sdata, int n_shr_empty)
{
  //extern __shared__ double sdata[];
  unsigned int tid = threadIdx.x;
  unsigned int i = blockIdx.x*blockDim.x + threadIdx.x;

  __syncthreads();

  sdata[tid] = in;

  __syncthreads();

  //first threads update empty positions
  if(tid<n_shr_empty)
    sdata[tid+blockDim.x]=sdata[tid];

  __syncthreads(); //Not needed (should)

  //if(blockIdx.x==0)printf("i %d in %le sdata[tid] %le\n",i,in,sdata[tid]);

  for (unsigned int s=(blockDim.x+n_shr_empty)/2; s>0; s>>=1)
  {
    if (tid < s){//&& sdata[tid + s]!=0.
      //if(sdata[tid + s] < sdata[tid] ) sdata[tid]=sdata[tid + s];
      sdata[tid] += sdata[tid + s];
    }
    __syncthreads();
  }

  __syncthreads();
  *g_odata = sdata[0];
  __syncthreads();

}

__device__ void warpReduce(volatile double *sdata, unsigned int tid) {
  unsigned int blockSize = blockDim.x;
  if (blockSize >= 64) sdata[tid] += sdata[tid + 32];
  if (blockSize >= 32) sdata[tid] += sdata[tid + 16];
  if (blockSize >= 16) sdata[tid] += sdata[tid + 8];
  if (blockSize >= 8) sdata[tid] += sdata[tid + 4];
  if (blockSize >= 4) sdata[tid] += sdata[tid + 2];
  if (blockSize >= 2) sdata[tid] += sdata[tid + 1];
}

__device__ void cudaDevicedotxy(double *g_idata1, double *g_idata2,
                                 double *g_odata, int n_shr_empty)
{
  extern __shared__ double sdata[];
  unsigned int tid = threadIdx.x;
  unsigned int i = blockIdx.x*blockDim.x + threadIdx.x;

  __syncthreads();
  if(tid<n_shr_empty)
    sdata[tid+blockDim.x]=0.;
  __syncthreads();
  sdata[tid] = g_idata1[i]*g_idata2[i];
  __syncthreads();
  unsigned int blockSize = blockDim.x+n_shr_empty;
  // do reduction in shared mem
  if ((blockSize >= 1024) && (tid < 512)) {
    sdata[tid] += sdata[tid + 512];
  }
  __syncthreads();
  if ((blockSize >= 512) && (tid < 256)) {
    sdata[tid] += sdata[tid + 256];
  }
  __syncthreads();
  if ((blockSize >= 256) && (tid < 128)) {
    sdata[tid] += sdata[tid + 128];
  }
  __syncthreads();
  if ((blockSize >= 128) && (tid < 64)) {
    sdata[tid] += sdata[tid + 64];
  }
  __syncthreads();
  if (tid < 32) warpReduce(sdata, tid);
  __syncthreads();//not needed?
  *g_odata = sdata[0];
  __syncthreads();
}

// z= a*z + x + b*y
__device__ void cudaDevicezaxpbypc(double* dz, double* dx,double* dy, double a, double b, int nrows)
{
  int row= threadIdx.x + blockDim.x*blockIdx.x;
  dz[row]=a*dz[row]  + dx[row] + b*dy[row];
}

// z= x*y
__device__ void cudaDevicemultxy(double* dz, double* dx,double* dy, int nrows)
{
  int row= threadIdx.x + blockDim.x*blockIdx.x;
  dz[row]=dx[row]*dy[row];
}

// z= a*x + b*y
__device__ void cudaDevicezaxpby(double a, double* dx, double b, double* dy, double* dz, int nrows)
{
  int row= threadIdx.x + blockDim.x*blockIdx.x;
  dz[row]=a*dx[row] + b*dy[row];
}

// y= a*x + y
__device__ void cudaDeviceaxpy(double* dy,double* dx, double a, int nrows)
{
  int row= threadIdx.x + blockDim.x*blockIdx.x;
  dy[row]=a*dx[row] + dy[row];
}

// sqrt(sum ( (x_i*y_i)^2)/n)
__device__ void cudaDeviceVWRMS_Norm(double *g_idata1, double *g_idata2, double *g_odata, int n, int n_shr_empty)
{
  extern __shared__ double sdata[];
  unsigned int tid = threadIdx.x;
  //unsigned int i = blockIdx.x*(blockDim.x*2) + threadIdx.x;
  unsigned int i = blockIdx.x*blockDim.x + threadIdx.x;

  __syncthreads();

  //first threads update empty positions
  if(tid<n_shr_empty)
    sdata[tid+blockDim.x]=0.;

  __syncthreads(); //Not needed (should)

/*
  double mySum = (i < n) ? g_idata1[i]*g_idata1[i]*g_idata2[i]*g_idata2[i] : 0;
  if (i + blockDim.x < n)
    mySum += g_idata1[i+blockDim.x]*g_idata1[i+blockDim.x]*g_idata2[i+blockDim.x]*g_idata2[i+blockDim.x];
*/

  __syncthreads();
  sdata[tid] = g_idata1[i]*g_idata1[i]*g_idata2[i]*g_idata2[i];
  __syncthreads();

  for (unsigned int s=(blockDim.x+n_shr_empty)/2; s>0; s>>=1)
  {
    if (tid < s)
      sdata[tid] += sdata[tid + s];

    __syncthreads();
  }

  //if (tid == 0) g_odata[blockIdx.x] = sdata[0];
  g_odata[0] = sqrt(sdata[0]/n);
  //*g_odata = sqrt(sdata[0]/n);
  __syncthreads();
}

// y=alpha*y
__device__ void cudaDevicescaley(double* dy, double a, int nrows)
{
  int row= threadIdx.x + blockDim.x*blockIdx.x;
  dy[row]=a*dy[row];
}

