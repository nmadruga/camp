/* Copyright (C) 2019 Christian Guzman
 * Licensed under the GNU General Public License version 1 or (at your
 * option) any later version. See the file COPYING for details.
 *
 * First-Order loss reaction solver functions
 *
*/
/** \file
 * \brief First-Order loss reaction solver functions
*/
extern "C"{
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../rxns_gpu.h"

#define TEMPERATURE_K_ env_data[0]
#define PRESSURE_PA_ env_data[1]

#ifdef REVERSE_INT_FLOAT_MATRIX

#define RXN_ID_ (int_data[0*n_rxn])
#define REACT_ (int_data[1*n_rxn]-1)
#define DERIV_ID_ int_data[2*n_rxn]
#define JAC_ID_ int_data[3*n_rxn]
#define SCALING_ float_data[0*n_rxn]
#define RATE_CONSTANT_ rxn_env_data[0*n_rxn]
#define BASE_RATE_ rxn_env_data[1*n_rxn]
#define NUM_INT_PROP_ 4
#define NUM_FLOAT_PROP_ 1
#define INT_DATA_SIZE_ (NUM_INT_PROP_)
#define FLOAT_DATA_SIZE_ (NUM_FLOAT_PROP_)

#else

#define RXN_ID_ (int_data[0])
#define REACT_ (int_data[1]-1)
#define DERIV_ID_ int_data[2]
#define JAC_ID_ int_data[3]
#define SCALING_ float_data[0]
#define RATE_CONSTANT_ rxn_env_data[0]
#define BASE_RATE_ rxn_env_data[1]
#define NUM_INT_PROP_ 4
#define NUM_FLOAT_PROP_ 1
#define INT_DATA_SIZE_ (NUM_INT_PROP_)
#define FLOAT_DATA_SIZE_ (NUM_FLOAT_PROP_)

#endif

/** \brief Calculate contributions to the time derivative \f$f(t,y)\f$ from
 * this reaction.
 *
 * \param model_data Pointer to the model data, including the state array
 * \param deriv Pointer to the time derivative to add contributions to
 * \param rxn_data Pointer to the reaction data
 * \param time_step Current time step being computed (s)
 * \return The rxn_data pointer advanced by the size of the reaction data
 */
#ifdef CAMP_USE_SUNDIALS
#ifdef __CUDA_ARCH__
__host__ __device__
#endif
void rxn_gpu_first_order_loss_calc_deriv_contrib(ModelDataGPU *model_data, realtype *deriv, int *rxn_int_data,
          double *rxn_float_data, double *rxn_env_data, double time_step)
{
#ifdef __CUDA_ARCH__
  int n_rxn=model_data->n_rxn;
#else
  int n_rxn=1;
#endif
  int *int_data = rxn_int_data;
  double *float_data = rxn_float_data;
  double *state = model_data->grid_cell_state;
  double *env_data = model_data->grid_cell_env;

  // Calculate the reaction rate
  realtype rate = RATE_CONSTANT_ * state[REACT_];

  // Add contributions to the time derivative
  //if (DERIV_ID_ >= 0) deriv[DERIV_ID_] -= rate;
  if (DERIV_ID_ >= 0)
#ifdef __CUDA_ARCH__
    atomicAdd((double*)&(deriv[DERIV_ID_]),-rate);
#else
    deriv[DERIV_ID_] -= rate;
#endif

}
#endif

/** \brief Calculate contributions to the Jacobian from this reaction
 *
 * \param model_data Pointer to the model data
 * \param J Pointer to the sparse Jacobian matrix to add contributions to
 * \param rxn_data Pointer to the reaction data
 * \param time_step Current time step being calculated (s)
 * \return The rxn_data pointer advanced by the size of the reaction data
 */
#ifdef CAMP_USE_SUNDIALS
#ifdef __CUDA_ARCH__
__host__ __device__
#endif
void rxn_gpu_first_order_loss_calc_jac_contrib(ModelDataGPU *model_data, JacobianGPU jac, int *rxn_int_data,
          double *rxn_float_data, double *rxn_env_data, double time_step)
{
#ifdef __CUDA_ARCH__
  int n_rxn=model_data->n_rxn;
#else
  int n_rxn=1;;
#endif
  int *int_data = rxn_int_data;
  double *float_data = rxn_float_data;
  double *state = model_data->grid_cell_state;
  double *env_data = model_data->grid_cell_env;

  // Add contributions to the Jacobian
  if (JAC_ID_ >= 0)
    jacobian_add_value_gpu(jac, (unsigned int)JAC_ID_, JACOBIAN_LOSS,
                       RATE_CONSTANT_);

}
#endif

#undef TEMPERATURE_K_
#undef PRESSURE_PA_

#undef RXN_ID_
#undef REACT_
#undef DERIV_ID_
#undef JAC_ID_
#undef BASE_RATE_
#undef SCALING_
#undef RATE_CONSTANT_
#undef NUM_INT_PROP_
#undef NUM_FLOAT_PROP_
#undef INT_DATA_SIZE_
#undef FLOAT_DATA_SIZE_
}