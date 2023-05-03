/* Copyright (C) 2019 Christian Guzman
 * Licensed under the GNU General Public License version 1 or (at your
 * option) any later version. See the file COPYING for details.
 *
 * CMAQ_OH_HNO3 reaction solver functions
 *
*/
/** \file
 * \brief CMAQ_OH_HNO3 reaction solver functions
*/
extern "C"{
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../rxns_gpu.h"

#define TEMPERATURE_K_ env_data[0]
#define PRESSURE_PA_ env_data[1]

#define NUM_REACT_ int_data[0]
#define NUM_PROD_ int_data[1]
#define k0_A_ float_data[0]
#define k0_B_ float_data[1]
#define k0_C_ float_data[2]
#define k2_A_ float_data[3]
#define k2_B_ float_data[4]
#define k2_C_ float_data[5]
#define k3_A_ float_data[6]
#define k3_B_ float_data[7]
#define k3_C_ float_data[8]
#define SCALING_ float_data[9]
#define CONV_ float_data[10]
#define RATE_CONSTANT_ rxn_env_data[0]
#define NUM_INT_PROP_ 2
#define NUM_FLOAT_PROP_ 11
#define REACT_(x) (int_data[(NUM_INT_PROP_ + x)]-1)
#define PROD_(x) (int_data[(NUM_INT_PROP_ + NUM_REACT_ + x)]-1)
#define DERIV_ID_(x) int_data[(NUM_INT_PROP_ + NUM_REACT_ + NUM_PROD_ + x)]
#define JAC_ID_(x) int_data[(NUM_INT_PROP_ + 2*(NUM_REACT_+NUM_PROD_) + x)]
#define YIELD_(x) float_data[(NUM_FLOAT_PROP_ + x)]
#define INT_DATA_SIZE_ (NUM_INT_PROP_+(NUM_REACT_+2)*(NUM_REACT_+NUM_PROD_))
#define FLOAT_DATA_SIZE_ (NUM_FLOAT_PROP_+NUM_PROD_)

#ifdef CAMP_USE_SUNDIALS
#ifdef __CUDA_ARCH__
__device__
#endif
void rxn_gpu_CMAQ_OH_HNO3_calc_deriv_contrib(ModelDataGPU *model_data, TimeDerivativeGPU time_deriv, int *rxn_int_data,
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
  double rate = RATE_CONSTANT_;
  for (int i_spec=0; i_spec<NUM_REACT_; i_spec++)
      rate *= state[REACT_(i_spec)];

  // Add contributions to the time derivative
  if (rate!=ZERO) {
    int i_dep_var = 0;
    for (int i_spec=0; i_spec<NUM_REACT_; i_spec++, i_dep_var++) {
      if (DERIV_ID_(i_dep_var) < 0) continue;
      time_derivative_add_value_gpu(time_deriv, DERIV_ID_(i_dep_var), -rate);
    }
    for (int i_spec=0; i_spec<NUM_PROD_; i_spec++, i_dep_var++) {
      if (DERIV_ID_(i_dep_var) < 0) continue;
      // Negative yields are allowed, but prevented from causing negative
      // concentrations that lead to solver failures
      if (-rate*YIELD_(i_spec)*time_step <= state[PROD_(i_spec)]) {
        time_derivative_add_value_gpu(time_deriv, DERIV_ID_(i_dep_var),rate*YIELD_(i_spec));
      }
    }
  }
}

#ifdef __CUDA_ARCH__
__device__
#endif
void rxn_gpu_CMAQ_OH_HNO3_calc_jac_contrib(ModelDataGPU *model_data, JacobianGPU jac, int *rxn_int_data,
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
  int i_elem = 0;
  for (int i_ind = 0; i_ind < NUM_REACT_; i_ind++) {
    // Calculate d_rate / d_i_ind
    double rate = RATE_CONSTANT_;
    for (int i_spec = 0; i_spec < NUM_REACT_; i_spec++)
      if (i_ind != i_spec) rate *= state[REACT_(i_spec)];

    for (int i_dep = 0; i_dep < NUM_REACT_; i_dep++, i_elem++) {
      if (JAC_ID_(i_elem) < 0) continue;
      jacobian_add_value_gpu(jac, (unsigned int)JAC_ID_(i_elem), JACOBIAN_LOSS,
                         rate);
    }
    for (int i_dep = 0; i_dep < NUM_PROD_; i_dep++, i_elem++) {
      if (JAC_ID_(i_elem) < 0) continue;
      // Negative yields are allowed, but prevented from causing negative
      // concentrations that lead to solver failures
      if (-rate * state[REACT_(i_ind)] * YIELD_(i_dep) * time_step <=
          state[PROD_(i_dep)]) {
        jacobian_add_value_gpu(jac, (unsigned int)JAC_ID_(i_elem),
                           JACOBIAN_PRODUCTION, YIELD_(i_dep) * rate);
      }
    }
  }

}
#endif

#undef TEMPERATURE_K_
#undef PRESSURE_PA_

#undef NUM_REACT_
#undef NUM_PROD_
#undef k0_A_
#undef k0_B_
#undef k0_C_
#undef k2_A_
#undef k2_B_
#undef k2_C_
#undef k3_A_
#undef k3_B_
#undef k3_C_
#undef SCALING_
#undef CONV_
#undef RATE_CONSTANT_
#undef NUM_INT_PROP_
#undef NUM_FLOAT_PROP_
#undef REACT_
#undef PROD_
#undef DERIV_ID_
#undef JAC_ID_
#undef YIELD_
#undef INT_DATA_SIZE_
#undef FLOAT_DATA_SIZE_
}