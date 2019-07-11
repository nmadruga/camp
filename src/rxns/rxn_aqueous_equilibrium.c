/* Copyright (C) 2015-2018 Matthew Dawson
 * Licensed under the GNU General Public License version 2 or (at your
 * option) any later version. See the file COPYING for details.
 *
 * Aqueous Equilibrium reaction solver functions
 *
*/
/** \file
 * \brief Aqueous Equilibrium reaction solver functions
*/
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../rxns.h"

// TODO Lookup environmental indices during initialization
#define TEMPERATURE_K_ env_data[0]
#define PRESSURE_PA_ env_data[1]

// Small number
#define SMALL_NUMBER_ 1.0e-30

// Factor used to calculate minimum water concentration for aqueous
// phase equilibrium reactions
#define MIN_WATER_ 1.0e-4

#define NUM_REACT_ (int_data[0])
#define NUM_PROD_ (int_data[1])
#define NUM_AERO_PHASE_ (int_data[2])
#define A_ (float_data[0])
#define C_ (float_data[1])
#define RATE_CONST_REVERSE_ (float_data[2])
#define RATE_CONST_FORWARD_ (float_data[3])
#define NUM_INT_PROP_ 3
#define NUM_FLOAT_PROP_ 4
#define REACT_(x) (int_data[NUM_INT_PROP_+x]-1)
#define PROD_(x) (int_data[NUM_INT_PROP_+NUM_REACT_*NUM_AERO_PHASE_+x]-1)
#define WATER_(x) (int_data[NUM_INT_PROP_+(NUM_REACT_+NUM_PROD_)*NUM_AERO_PHASE_+x]-1)
#define ACTIVITY_COEFF_(x) (int_data[NUM_INT_PROP_+(NUM_REACT_+NUM_PROD_+1)*NUM_AERO_PHASE_+x]-1)
#define DERIV_ID_(x) (int_data[NUM_INT_PROP_+(NUM_REACT_+NUM_PROD_+2)*NUM_AERO_PHASE_+x])
#define JAC_ID_(x) (int_data[NUM_INT_PROP_+(2*(NUM_REACT_+NUM_PROD_)+2)*NUM_AERO_PHASE_+x])
#define MASS_FRAC_TO_M_(x) (float_data[NUM_FLOAT_PROP_+x])
#define SMALL_WATER_CONC_(x) (float_data[NUM_FLOAT_PROP_+NUM_REACT_+NUM_PROD_+x])
#define SMALL_CONC_(x) (float_data[NUM_FLOAT_PROP_+NUM_REACT_+NUM_PROD_+NUM_AERO_PHASE_+x])
#define INT_DATA_SIZE_ (NUM_INT_PROP_+((NUM_REACT_+NUM_PROD_)*(NUM_REACT_+NUM_PROD_+3)+2)*NUM_AERO_PHASE_)
#define FLOAT_DATA_SIZE_ (NUM_FLOAT_PROP_+NUM_PROD_+NUM_REACT_+2*NUM_AERO_PHASE_)

/** \brief Flag Jacobian elements used by this reaction
 *
 * \param rxn_data A pointer to the reaction data
 * \param jac_struct 2D array of flags indicating potentially non-zero
 *                   Jacobian elements
 * \return The rxn_data pointer advanced by the size of the reaction data
 */
void * rxn_aqueous_equilibrium_get_used_jac_elem(void *rxn_data,
          bool **jac_struct)
{
  int *int_data = (int*) rxn_data;
  double *float_data = (double*) &(int_data[INT_DATA_SIZE_]);

  // Loop over all the instances of the specified phase
  for (int i_phase = 0; i_phase < NUM_AERO_PHASE_; i_phase++) {

    // Add dependence on reactants for reactants and products (forward reaction)
    for (int i_react_ind = i_phase*NUM_REACT_;
              i_react_ind < (i_phase+1)*NUM_REACT_; i_react_ind++) {
      for (int i_react_dep = i_phase*NUM_REACT_;
                i_react_dep < (i_phase+1)*NUM_REACT_; i_react_dep++)
        jac_struct[REACT_(i_react_dep)][REACT_(i_react_ind)] = true;
      for (int i_prod_dep = i_phase*NUM_PROD_;
                i_prod_dep < (i_phase+1)*NUM_PROD_; i_prod_dep++)
        jac_struct[PROD_(i_prod_dep)][REACT_(i_react_ind)] = true;
    }

    // Add dependence on products for reactants and products (reverse reaction)
    for (int i_prod_ind = i_phase*NUM_PROD_;
              i_prod_ind < (i_phase+1)*NUM_PROD_; i_prod_ind++) {
      for (int i_react_dep = i_phase*NUM_REACT_;
                i_react_dep < (i_phase+1)*NUM_REACT_; i_react_dep++)
        jac_struct[REACT_(i_react_dep)][PROD_(i_prod_ind)] = true;
      for (int i_prod_dep = i_phase*NUM_PROD_;
                i_prod_dep < (i_phase+1)*NUM_PROD_; i_prod_dep++)
        jac_struct[PROD_(i_prod_dep)][PROD_(i_prod_ind)] = true;
    }

    // Add dependence on aerosol-phase water for reactants and products
    for (int i_react_dep = i_phase*NUM_REACT_;
              i_react_dep < (i_phase+1)*NUM_REACT_; i_react_dep++)
      jac_struct[REACT_(i_react_dep)][WATER_(i_phase)] = true;
    for (int i_prod_dep = i_phase*NUM_PROD_;
              i_prod_dep < (i_phase+1)*NUM_PROD_; i_prod_dep++)
      jac_struct[PROD_(i_prod_dep)][WATER_(i_phase)] = true;

  }

  return (void*) &(float_data[FLOAT_DATA_SIZE_]);
}

/** \brief Update the time derivative and Jacbobian array indices
 *
 * \param model_data Pointer to the model data
 * \param deriv_ids Id of each state variable in the derivative array
 * \param jac_ids Id of each state variable combo in the Jacobian array
 * \param rxn_data Pointer to the reaction data
 * \return The rxn_data pointer advanced by the size of the reaction data
 */
void * rxn_aqueous_equilibrium_update_ids(ModelData *model_data, int *deriv_ids,
          int **jac_ids, void *rxn_data)
{
  int *int_data = (int*) rxn_data;
  double *float_data = (double*) &(int_data[INT_DATA_SIZE_]);

  // Update the time derivative ids
  for (int i_phase = 0, i_deriv = 0; i_phase < NUM_AERO_PHASE_; i_phase++) {
    for (int i_react = 0; i_react < NUM_REACT_; i_react++)
      DERIV_ID_(i_deriv++) = deriv_ids[REACT_(i_phase*NUM_REACT_+i_react)];
    for (int i_prod = 0; i_prod < NUM_PROD_; i_prod++)
      DERIV_ID_(i_deriv++) = deriv_ids[PROD_(i_phase*NUM_PROD_+i_prod)];
  }

  // Update the Jacobian ids
  for (int i_phase = 0, i_jac = 0; i_phase < NUM_AERO_PHASE_; i_phase++) {

    // Add dependence on reactants for reactants and products (forward reaction)
    for (int i_react_ind = i_phase*NUM_REACT_;
              i_react_ind < (i_phase+1)*NUM_REACT_; i_react_ind++) {
      for (int i_react_dep = i_phase*NUM_REACT_;
                i_react_dep < (i_phase+1)*NUM_REACT_; i_react_dep++)
        JAC_ID_(i_jac++) = jac_ids[REACT_(i_react_dep)][REACT_(i_react_ind)];
      for (int i_prod_dep = i_phase*NUM_PROD_;
                i_prod_dep < (i_phase+1)*NUM_PROD_; i_prod_dep++)
        JAC_ID_(i_jac++) = jac_ids[PROD_(i_prod_dep)][REACT_(i_react_ind)];
    }

    // Add dependence on products for reactants and products (reverse reaction)
    for (int i_prod_ind = i_phase*NUM_PROD_;
              i_prod_ind < (i_phase+1)*NUM_PROD_; i_prod_ind++) {
      for (int i_react_dep = i_phase*NUM_REACT_;
                i_react_dep < (i_phase+1)*NUM_REACT_; i_react_dep++)
        JAC_ID_(i_jac++) = jac_ids[REACT_(i_react_dep)][PROD_(i_prod_ind)];
      for (int i_prod_dep = i_phase*NUM_PROD_;
                i_prod_dep < (i_phase+1)*NUM_PROD_; i_prod_dep++)
        JAC_ID_(i_jac++) = jac_ids[PROD_(i_prod_dep)][PROD_(i_prod_ind)];
    }

    // Add dependence on aerosol-phase water for reactants and products
    for (int i_react_dep = i_phase*NUM_REACT_;
              i_react_dep < (i_phase+1)*NUM_REACT_; i_react_dep++)
      JAC_ID_(i_jac++) = jac_ids[REACT_(i_react_dep)][WATER_(i_phase)];
    for (int i_prod_dep = i_phase*NUM_PROD_;
              i_prod_dep < (i_phase+1)*NUM_PROD_; i_prod_dep++)
      JAC_ID_(i_jac++) = jac_ids[PROD_(i_prod_dep)][WATER_(i_phase)];

  }

  // Calculate a small concentration for aerosol-phase species based on the
  // integration tolerances to use during solving. TODO find a better place
  // to do this
  realtype *abs_tol = model_data->abs_tol;
  for (int i_phase = 0; i_phase < NUM_AERO_PHASE_; i_phase++ ) {
    SMALL_CONC_(i_phase) = 99999.0;
    for (int i_react = 0; i_react < NUM_REACT_; i_react++) {
      if (SMALL_CONC_(i_phase) >
          abs_tol[REACT_(i_phase*NUM_REACT_+i_react)] / 100.0)
          SMALL_CONC_(i_phase) =
            abs_tol[REACT_(i_phase*NUM_REACT_+i_react)] / 100.0;
    }
    for (int i_prod = 0; i_prod < NUM_PROD_; i_prod++) {
      if (SMALL_CONC_(i_phase) >
          abs_tol[PROD_(i_phase*NUM_PROD_+i_prod)] / 100.0)
          SMALL_CONC_(i_phase) =
            abs_tol[PROD_(i_phase*NUM_PROD_+i_prod)] / 100.0;
    }
  }

  // Calculate a small concentration for aerosol-phase water based on the
  // integration tolerances to use during solving. TODO find a better place
  // to do this
  for (int i_phase = 0; i_phase < NUM_AERO_PHASE_; i_phase++) {
    SMALL_WATER_CONC_(i_phase) = abs_tol[WATER_(i_phase)] / 10.0;
  }

  return (void*) &(float_data[FLOAT_DATA_SIZE_]);

}

/** \brief Update reaction data for new environmental conditions
 *
 * For Aqueous Equilibrium reaction this only involves recalculating the
 * forward rate constant.
 *
 * \param env_data Pointer to the environmental state array
 * \param rxn_data Pointer to the reaction data
 * \return The rxn_data pointer advanced by the size of the reaction data
 */
void * rxn_aqueous_equilibrium_update_env_state(double *env_data,
          void *rxn_data)
{
  int *int_data = (int*) rxn_data;
  double *float_data = (double*) &(int_data[INT_DATA_SIZE_]);

  // Calculate the equilibrium constant
  // (assumes reactant and product concentrations in M)
  double equil_const;
  if (C_==0.0) {
    equil_const = A_;
  } else {
    equil_const = A_ * exp(C_ * (1.0/TEMPERATURE_K_ - 1.0/298.0));
  }

  // Set the forward rate constant
  RATE_CONST_FORWARD_ = equil_const * RATE_CONST_REVERSE_;

  return (void*) &(float_data[FLOAT_DATA_SIZE_]);
}

/** \brief Do pre-derivative calculations
 *
 * Nothing to do for aqueous_equilibrium reactions
 *
 * \param model_data Pointer to the model data, including the state array
 * \param rxn_data Pointer to the reaction data
 * \return The rxn_data pointer advanced by the size of the reaction data
 */
void * rxn_aqueous_equilibrium_pre_calc(ModelData *model_data, void *rxn_data)
{
  int *int_data = (int*) rxn_data;
  double *float_data = (double*) &(int_data[INT_DATA_SIZE_]);

  return (void*) &(float_data[FLOAT_DATA_SIZE_]);
}

/** \brief Calculate contributions to the time derivative \f$f(t,y)\f$ from
 * this reaction.
 *
 * \param model_data Pointer to the model data, including the state array
 * \param deriv Pointer to the time derivative to add contributions to
 * \param rxn_data Pointer to the reaction data
 * \param time_step Current time step of the itegrator (s)
 * \return The rxn_data pointer advanced by the size of the reaction data
 */
#ifdef PMC_USE_SUNDIALS
void * rxn_aqueous_equilibrium_calc_deriv_contrib(double *state, ModelData *model_data,
          realtype *deriv, void *rxn_data, double time_step)
{
  //realtype *state = model_data->state;
  realtype *env_data = model_data->env;
  int *int_data = (int*) rxn_data;
  realtype *float_data = (realtype*) &(int_data[INT_DATA_SIZE_]);

  // Calculate derivative contributions for each aerosol phase
  for (int i_phase=0, i_deriv = 0; i_phase<NUM_AERO_PHASE_; i_phase++) {

    // If no aerosol water is present, no reaction occurs
    if (state[WATER_(i_phase)] < MIN_WATER_ * SMALL_WATER_CONC_(i_phase)) {
      i_deriv += NUM_REACT_ + NUM_PROD_;
      continue;
    }

    // Slow down rates as water approaches the minimum value
    realtype water_adj = state[WATER_(i_phase)] -
                         MIN_WATER_ * SMALL_WATER_CONC_(i_phase);
    water_adj = ( water_adj > ZERO ) ? water_adj : ZERO;
    realtype water_scaling =
      2.0 / ( 1.0 + exp( -water_adj / SMALL_WATER_CONC_(i_phase) ) ) - 1.0;

    // Get the lowest concentration to use in slowing rates
    realtype min_react_conc = 1.0e100;
    realtype min_prod_conc  = 1.0e100;

    // Calculate the forward rate (M/s)
    realtype forward_rate = RATE_CONST_FORWARD_ * water_scaling;
    for (int i_react = 0; i_react < NUM_REACT_; i_react++) {
      forward_rate *= state[REACT_(i_phase*NUM_REACT_+i_react)] *
              MASS_FRAC_TO_M_(i_react) / state[WATER_(i_phase)];
      if (min_react_conc > state[REACT_(i_phase*NUM_REACT_+i_react)])
        min_react_conc = state[REACT_(i_phase*NUM_REACT_+i_react)];
    }

    // Calculate the reverse rate (M/s)
    realtype reverse_rate = RATE_CONST_REVERSE_ * water_scaling;
    for (int i_prod = 0; i_prod < NUM_PROD_; i_prod++) {
      reverse_rate *= state[PROD_(i_phase*NUM_PROD_+i_prod)] *
              MASS_FRAC_TO_M_(NUM_REACT_+i_prod) / state[WATER_(i_phase)];
      if (min_prod_conc > state[PROD_(i_phase*NUM_PROD_+i_prod)])
        min_prod_conc = state[PROD_(i_phase*NUM_PROD_+i_prod)];
    }
    if (ACTIVITY_COEFF_(i_phase)>=0) reverse_rate *=
            state[ACTIVITY_COEFF_(i_phase)];

    // Slow rates as concentrations become low
    realtype min_conc =
      (forward_rate > reverse_rate) ? min_react_conc : min_prod_conc;
    min_conc -= SMALL_NUMBER_;
    if (min_conc <= ZERO) continue;
    realtype spec_scaling =
      2.0 / ( 1.0 + exp( -min_conc / SMALL_CONC_(i_phase) ) ) - 1.0;
    forward_rate *= spec_scaling;
    reverse_rate *= spec_scaling;

    // Reactants change as (reverse - forward) (ug/m3/s)
    for (int i_react = 0; i_react < NUM_REACT_; i_react++) {
      if (DERIV_ID_(i_deriv)<0) {i_deriv++; continue;}
      deriv[DERIV_ID_(i_deriv++)] += (reverse_rate - forward_rate) /
	      MASS_FRAC_TO_M_(i_react) * state[WATER_(i_phase)];
    }

    // Products change as (forward - reverse) (ug/m3/s)
    for (int i_prod = 0; i_prod < NUM_PROD_; i_prod++) {
      if (DERIV_ID_(i_deriv)<0) {i_deriv++; continue;}
      deriv[DERIV_ID_(i_deriv++)] += (forward_rate - reverse_rate) /
	      MASS_FRAC_TO_M_(NUM_REACT_+i_prod) * state[WATER_(i_phase)];
    }

  }

  return (void*) &(float_data[FLOAT_DATA_SIZE_]);

}
#endif

/** \brief Calculate contributions to the Jacobian from this reaction
 *
 * \param model_data Pointer to the model data
 * \param J Pointer to the sparse Jacobian matrix to add contributions to
 * \param rxn_data Pointer to the reaction data
 * \param time_step Current time step of the itegrator (s)
 * \return The rxn_data pointer advanced by the size of the reaction data
 */
#ifdef PMC_USE_SUNDIALS
void * rxn_aqueous_equilibrium_calc_jac_contrib(double *state, ModelData *model_data,
          realtype *J, void *rxn_data, double time_step)
{
  //realtype *state = model_data->state;
  realtype *env_data = model_data->env;
  int *int_data = (int*) rxn_data;
  realtype *float_data = (realtype*) &(int_data[INT_DATA_SIZE_]);

  // Calculate Jacobian contributions for each aerosol phase
  for (int i_phase=0, i_jac = 0; i_phase<NUM_AERO_PHASE_; i_phase++) {

    // If not aerosol water is present, no reaction occurs
    if (state[WATER_(i_phase)] < MIN_WATER_ * SMALL_WATER_CONC_(i_phase)) {
      i_jac += (NUM_REACT_ + NUM_PROD_) * (NUM_REACT_ + NUM_PROD_ + 1);
      continue;
    }

    // Slow down rates as water approaches the minimum value
    realtype water_adj = state[WATER_(i_phase)] -
                         MIN_WATER_ * SMALL_WATER_CONC_(i_phase);
    water_adj = ( water_adj > ZERO ) ? water_adj : ZERO;
    realtype water_scaling =
      2.0 / ( 1.0 + exp( -water_adj / SMALL_WATER_CONC_(i_phase) ) ) - 1.0;
    realtype water_scaling_deriv =
      2.0 / ( SMALL_WATER_CONC_(i_phase) *
              ( exp(  water_adj / SMALL_WATER_CONC_(i_phase) ) + 2.0 +
                exp( -water_adj / SMALL_WATER_CONC_(i_phase) ) ) );

    // Get the lowest concentration to use in slowing rates
    realtype min_react_conc = 1.0e100;
    realtype min_prod_conc  = 1.0e100;
    int low_react_id = 0;
    int low_prod_id  = 0;

    // Calculate the forward rate (M/s)
    realtype forward_rate = RATE_CONST_FORWARD_;
    for (int i_react = 0; i_react < NUM_REACT_; i_react++) {
      forward_rate *= state[REACT_(i_phase*NUM_REACT_+i_react)] *
              MASS_FRAC_TO_M_(i_react) / state[WATER_(i_phase)];
      if (min_react_conc > state[REACT_(i_phase*NUM_REACT_+i_react)]) {
        min_react_conc = state[REACT_(i_phase*NUM_REACT_+i_react)];
        low_react_id = i_react;
      }
    }

    // Calculate the reverse rate (M/s)
    realtype reverse_rate = RATE_CONST_REVERSE_;
    for (int i_prod = 0; i_prod < NUM_PROD_; i_prod++) {
      reverse_rate *= state[PROD_(i_phase*NUM_PROD_+i_prod)] *
              MASS_FRAC_TO_M_(NUM_REACT_+i_prod) / state[WATER_(i_phase)];
      if (min_prod_conc > state[PROD_(i_phase*NUM_PROD_+i_prod)]) {
        min_prod_conc = state[PROD_(i_phase*NUM_PROD_+i_prod)];
        low_prod_id = NUM_REACT_ + i_prod;
      }
    }
    if (ACTIVITY_COEFF_(i_phase)>=0) reverse_rate *=
            state[ACTIVITY_COEFF_(i_phase)];

    // Slow rates as concentrations become low
    realtype min_conc;
    int low_spec_id;
    if (forward_rate > reverse_rate) {
      min_conc = min_react_conc;
      low_spec_id = low_react_id;
    } else {
      min_conc = min_prod_conc;
      low_spec_id = low_prod_id;
    }
    min_conc -= SMALL_NUMBER_;
    if (min_conc <= ZERO) continue;
    realtype spec_scaling =
      2.0 / ( 1.0 + exp( -min_conc / SMALL_CONC_(i_phase) ) ) - 1.0;
    realtype spec_scaling_deriv =
      2.0 / ( SMALL_CONC_(i_phase) *
              ( exp(  min_conc / SMALL_CONC_(i_phase) ) + 2.0 +
                exp( -min_conc / SMALL_CONC_(i_phase) ) ) );

    // Add dependence on reactants for reactants and products (forward reaction)
    for (int i_react_ind = 0; i_react_ind < NUM_REACT_; i_react_ind++) {
      for (int i_react_dep = 0; i_react_dep < NUM_REACT_; i_react_dep++) {
	if (JAC_ID_(i_jac)<0 || forward_rate==0.0) {i_jac++; continue;}
        if (low_spec_id == i_react_ind) {
          J[JAC_ID_(i_jac)] += (-forward_rate) * water_scaling *
                               spec_scaling_deriv;
        }
        J[JAC_ID_(i_jac++)] += (-forward_rate) * water_scaling * spec_scaling /
                state[REACT_(i_phase*NUM_REACT_+i_react_ind)] /
		MASS_FRAC_TO_M_(i_react_dep) * state[WATER_(i_phase)];
      }
      for (int i_prod_dep = 0; i_prod_dep < NUM_PROD_; i_prod_dep++) {
	if (JAC_ID_(i_jac)<0 || forward_rate==0.0) {i_jac++; continue;}
        if (low_spec_id == i_react_ind) {
          J[JAC_ID_(i_jac)] += (forward_rate) * water_scaling *
                               spec_scaling_deriv;
        }
        J[JAC_ID_(i_jac++)] += (forward_rate) * water_scaling * spec_scaling /
                state[REACT_(i_phase*NUM_REACT_+i_react_ind)] /
		MASS_FRAC_TO_M_(NUM_REACT_ + i_prod_dep) *
                state[WATER_(i_phase)];
      }
    }

    // Add dependence on products for reactants and products (reverse reaction)
    for (int i_prod_ind = 0; i_prod_ind < NUM_PROD_; i_prod_ind++) {
      for (int i_react_dep = 0; i_react_dep < NUM_REACT_; i_react_dep++) {
	if (JAC_ID_(i_jac)<0 || reverse_rate==0.0) {i_jac++; continue;}
        if (low_spec_id == NUM_REACT_ + i_prod_ind) {
          J[JAC_ID_(i_jac)] += (reverse_rate) * water_scaling *
                               spec_scaling_deriv;
        }
        J[JAC_ID_(i_jac++)] += (reverse_rate) * water_scaling * spec_scaling /
                state[PROD_(i_phase*NUM_PROD_+i_prod_ind)] /
		MASS_FRAC_TO_M_(i_react_dep) * state[WATER_(i_phase)];
      }
      for (int i_prod_dep = 0; i_prod_dep < NUM_PROD_; i_prod_dep++) {
	if (JAC_ID_(i_jac)<0 || reverse_rate==0.0) {i_jac++; continue;}
        if (low_spec_id == NUM_REACT_ + i_prod_ind) {
          J[JAC_ID_(i_jac)] += (-reverse_rate) * water_scaling *
                               spec_scaling_deriv;
        }
        J[JAC_ID_(i_jac++)] += (-reverse_rate) * water_scaling * spec_scaling /
                state[PROD_(i_phase*NUM_PROD_+i_prod_ind)] /
		MASS_FRAC_TO_M_(NUM_REACT_ + i_prod_dep) *
                state[WATER_(i_phase)];
      }
    }

    // Add dependence on aerosol-phase water for reactants and products
    for (int i_react_dep = 0; i_react_dep < NUM_REACT_; i_react_dep++) {
      if (JAC_ID_(i_jac)<0) {i_jac++; continue;}
      J[JAC_ID_(i_jac++)] +=
        ( ( forward_rate * (NUM_REACT_-1) - reverse_rate * (NUM_PROD_-1) )
          * water_scaling * spec_scaling / state[WATER_(i_phase)] +
          ( forward_rate - reverse_rate ) * spec_scaling * water_scaling_deriv
        ) / MASS_FRAC_TO_M_(i_react_dep);
    }
    for (int i_prod_dep = 0; i_prod_dep < NUM_PROD_; i_prod_dep++) {
      if (JAC_ID_(i_jac)<0) {i_jac++; continue;}
      J[JAC_ID_(i_jac++)] -=
        ( ( forward_rate * (NUM_REACT_-1) - reverse_rate * (NUM_PROD_-1) )
          * water_scaling * spec_scaling / state[WATER_(i_phase)] +
          ( forward_rate - reverse_rate ) * spec_scaling * water_scaling_deriv
        ) / MASS_FRAC_TO_M_(NUM_REACT_ + i_prod_dep);
    }

  }

  return (void*) &(float_data[FLOAT_DATA_SIZE_]);

}
#endif

/** \brief Advance the reaction data pointer to the next reaction
 *
 * \param rxn_data Pointer to the reaction data
 * \return The rxn_data pointer advanced by the size of the reaction data
 */
void * rxn_aqueous_equilibrium_skip(void *rxn_data)
{
  int *int_data = (int*) rxn_data;
  double *float_data = (double*) &(int_data[INT_DATA_SIZE_]);

  return (void*) &(float_data[FLOAT_DATA_SIZE_]);
}

/** \brief Print the Aqueous Equilibrium reaction parameters
 *
 * \param rxn_data Pointer to the reaction data
 * \return The rxn_data pointer advanced by the size of the reaction data
 */
void * rxn_aqueous_equilibrium_print(void *rxn_data)
{
  int *int_data = (int*) rxn_data;
  double *float_data = (double*) &(int_data[INT_DATA_SIZE_]);

  printf("\n\nAqueous Equilibrium reaction\n");
  for (int i=0; i<INT_DATA_SIZE_; i++)
    printf("  int param %d = %d\n", i, int_data[i]);
  for (int i=0; i<FLOAT_DATA_SIZE_; i++)
    printf("  float param %d = %le\n", i, float_data[i]);

  return (void*) &(float_data[FLOAT_DATA_SIZE_]);
}

#undef TEMPERATURE_K_
#undef PRESSURE_PA_

#undef SMALL_NUMBER_

#undef MIN_WATER_

#undef NUM_REACT_
#undef NUM_PROD_
#undef NUM_AERO_PHASE_
#undef A_
#undef C_
#undef RATE_CONST_REVERSE_
#undef RATE_CONST_FORWARD_
#undef NUM_INT_PROP_
#undef NUM_FLOAT_PROP_
#undef REACT_
#undef PROD_
#undef WATER_
#undef ACTIVITY_COEFF_
#undef DERIV_ID_
#undef JAC_ID_
#undef MASS_FRAC_TO_M_
#undef SMALL_WATER_CONC_
#undef SMALL_CONC_
#undef INT_DATA_SIZE_
#undef FLOAT_DATA_SIZE_
