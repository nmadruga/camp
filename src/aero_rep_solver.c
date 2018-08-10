/* Copyright (C) 2015-2018 Matthew Dawson
 * Licensed under the GNU General Public License version 1 or (at your
 * option) any later version. See the file COPYING for details.
 *
 * Aerosol representation-specific functions for use by the solver
 *
 */
/** \file
 * \brief Aerosol representation functions
 */
#include "aero_rep_solver.h"
#include "phlex_solver.h"

#define NUM_ENV_VAR 2

// Aerosol representations (Must match parameters defined in pmc_aero_rep_factory
#define AERO_REP_SINGLE_PARTICLE   1
#define AERO_REP_MODAL_BINNED_MASS 2

/** \brief Get state array elements used by aerosol representation functions
 *
 * \param model_data A pointer to the model data
 * \param state_flags An array of flags the length of the state array
 *                    indicating species used
 * \return The aero_rep_data pointer advanced by the size of the aerosol
 *         representation data
 */
void * aero_rep_get_dependencies(ModelData *model_data, pmc_bool *state_flags)
{

  // Get the number of aerosol representations
  int *aero_rep_data = (int*) (model_data->aero_rep_data);
  int n_aero_rep = *(aero_rep_data++);

  // Loop through the aerosol representations to determine the Jacobian elements
  // used advancing the aero_rep_data pointer each time
  for (int i_aero_rep=0; i_aero_rep<n_aero_rep; i_aero_rep++) {

    // Get the aerosol representation type
    int aero_rep_type = *(aero_rep_data++);

    // Call the appropriate function
    switch (aero_rep_type) {
      case AERO_REP_MODAL_BINNED_MASS :
	aero_rep_data = (int*) aero_rep_modal_binned_mass_get_dependencies(
                  (void*) aero_rep_data, state_flags);
        break;
      case AERO_REP_SINGLE_PARTICLE :
	aero_rep_data = (int*) aero_rep_single_particle_get_dependencies(
                  (void*) aero_rep_data, state_flags);
        break;
    }
  }
  return aero_rep_data;
}

/** \brief Update the time derivative and Jacobian array ids
 *
 * \param model_data Pointer to the model data
 * \param deriv_size Number of elements per state on the derivative array
 * \param jac_size Number of elements per state on the Jacobian array
 * \param deriv_ids Ids for state variables on the time derivative array
 * \param jac_ids Ids for state variables on the Jacobian array
 */
void aero_rep_update_ids(ModelData *model_data, int deriv_size, int jac_size, 
            int *deriv_ids, int **jac_ids)
{

  int *aero_rep_data;
  int env_offset = 0;

  // Loop through the unique states
  for (int i_state = 0; i_state < model_data->n_states; i_state++) {

    // Point to the aerosol representation data for this state
    aero_rep_data = (int*) (model_data->aero_rep_data);
    aero_rep_data += (model_data->aero_rep_data_size / sizeof(int)) * i_state;

    // Get the number of aerosol representations
    int n_aero_rep = *(aero_rep_data++);

    // Loop through the aerosol representations advancing the aero_rep_data pointer each time
    for (int i_aero_rep=0; i_aero_rep<n_aero_rep; i_aero_rep++) {

      // Get the aerosol representation type
      int aero_rep_type = *(aero_rep_data++);

      // Call the appropriate function
      switch (aero_rep_type) {
        case AERO_REP_MODAL_BINNED_MASS :
          aero_rep_data = (int*) aero_rep_modal_binned_mass_update_ids(
                    model_data, deriv_ids, jac_ids, env_offset, (void*) aero_rep_data);
          break;
        case AERO_REP_SINGLE_PARTICLE :
          aero_rep_data = (int*) aero_rep_single_particle_update_ids(
                    model_data, deriv_ids, jac_ids, env_offset, (void*) aero_rep_data);
          break;
      }
    }
    
    // Update the derivative and Jacobian ids for the next state
    for (int i_elem = 0; i_elem < model_data->n_state_var; i_elem++)
      if (deriv_ids[i_elem]>=0) deriv_ids[i_elem] += deriv_size;
    for (int i_elem = 0; i_elem < model_data->n_state_var; i_elem++)
      for (int j_elem = 0; j_elem < model_data->n_state_var; j_elem++)
        if (jac_ids[i_elem][j_elem]>=0) jac_ids[i_elem][j_elem] += jac_size;

    // Update the environmental array offset for the next state
    env_offset += NUM_ENV_VAR;

  }

    // Reset the indices to the first state's values
    for (int i_elem = 0; i_elem < model_data->n_state_var; i_elem++)
      if (deriv_ids[i_elem]>=0) deriv_ids[i_elem] -= 
              (model_data->n_states) * deriv_size;
    for (int i_elem = 0; i_elem < model_data->n_state_var; i_elem++)
      for (int j_elem = 0; j_elem < model_data->n_state_var; j_elem++)
        if (jac_ids[i_elem][j_elem]>=0) jac_ids[i_elem][j_elem] -=
                (model_data->n_states) * jac_size;

}

/** \brief Update the aerosol representations for new environmental conditions
 *
 * \param model_data Pointer to the model data
 * \param env Pointer to the environmental state array
 */
void aero_rep_update_env_state(ModelData *model_data, PMC_C_FLOAT *env)
{

  int *aero_rep_data = (int*) (model_data->aero_rep_data);
  
  // Loop through the unique states to solve
  for (int i_state = 0; i_state < model_data->n_states; i_state++) {

  // Get the number of aerosol representations
  int n_aero_rep = *(aero_rep_data++);

  // Loop through the aerosol representations to update the environmental
  // conditions advancing the aero_rep_data pointer each time
  for (int i_aero_rep=0; i_aero_rep<n_aero_rep; i_aero_rep++) {

    // Get the aerosol representation type
    int aero_rep_type = *(aero_rep_data++);

    // Call the appropriate function
    switch (aero_rep_type) {
      case AERO_REP_MODAL_BINNED_MASS :
	aero_rep_data = (int*) aero_rep_modal_binned_mass_update_env_state(
                  env, (void*) aero_rep_data);
        break;
      case AERO_REP_SINGLE_PARTICLE :
	aero_rep_data = (int*) aero_rep_single_particle_update_env_state(
                  env, (void*) aero_rep_data);
        break;
    }
  }
  }
}

/** \brief Update the aerosol representations for a new state
 *
 * \param model_data Pointer to the model data
 */
void aero_rep_update_state(ModelData *model_data)
{

  int *aero_rep_data = (int*) (model_data->aero_rep_data);
  
  // Loop through the unique states to solve
  for (int i_state = 0; i_state < model_data->n_states; i_state++) {

  // Get the number of aerosol representations
  int n_aero_rep = *(aero_rep_data++);

  // Loop through the aerosol representations to update the state
  // advancing the aero_rep_data pointer each time
  for (int i_aero_rep=0; i_aero_rep<n_aero_rep; i_aero_rep++) {

    // Get the aerosol representation type
    int aero_rep_type = *(aero_rep_data++);

    // Call the appropriate function
    switch (aero_rep_type) {
      case AERO_REP_MODAL_BINNED_MASS :
	aero_rep_data = (int*) aero_rep_modal_binned_mass_update_state(model_data, 
                  (void*) aero_rep_data);
        break;
      case AERO_REP_SINGLE_PARTICLE :
	aero_rep_data = (int*) aero_rep_single_particle_update_state(model_data, 
                  (void*) aero_rep_data);
        break;
    }
  }
  }
}

/** \brief Get the effective particle radius, \f$r_{eff}\f$ (m)
 *
 * Calculates effective particle radius \f$r_{eff}\f$ (m), as well as the set of
 * \f$\frac{\partial r_{eff}}{\partial y}\f$ where \f$y\f$ are variables on the
 * solver state array.
 *
 * \param model_data Pointer to the model data
 * \param state_id Id of the unique state to get data for
 * \param aero_rep_idx Index of aerosol representation to use for calculation
 * \param aero_phase_idx Index of the aerosol phase within the aerosol
 *                       representation
 * \param radius Pointer to hold effective particle radius (m)
 * \return A pointer to a set of partial derivatives
 *         \f$\frac{\partial r_{eff}}{\partial y}\f$, or a NULL pointer if no
 *         partial derivatives exist
 */
void * aero_rep_get_effective_radius(ModelData *model_data, int state_id, 
          int aero_rep_idx, int aero_phase_idx, PMC_C_FLOAT *radius)
{

  // Set up a pointer for the partial derivatives
  void *partial_deriv = NULL;

  // Get a pointer to the unique states data
  int *aero_rep_data = (int*) (model_data->aero_rep_data);
  aero_rep_data += (model_data->aero_rep_data_size / sizeof(int)) * state_id;

  // Get the number of aerosol representations
  int n_aero_rep = *(aero_rep_data++);

  // Loop through the aerosol representations to find the one requested
  for (int i_aero_rep=0; i_aero_rep<aero_rep_idx; i_aero_rep++) {

    // Get the aerosol representation type
    int aero_rep_type = *(aero_rep_data++);

    // Advance the pointer to the next aerosol representation
    switch (aero_rep_type) {
      case AERO_REP_MODAL_BINNED_MASS :
	aero_rep_data = (int*) aero_rep_modal_binned_mass_skip(
                  (void*) aero_rep_data);
        break;
      case AERO_REP_SINGLE_PARTICLE :
	aero_rep_data = (int*) aero_rep_single_particle_skip(
                  (void*) aero_rep_data);
        break;
    }
  }

  // Get the aerosol representation type
  int aero_rep_type = *(aero_rep_data++);

  // Get the particle radius and set of partial derivatives
  switch (aero_rep_type) {
    case AERO_REP_MODAL_BINNED_MASS :
      aero_rep_data = (int*) aero_rep_modal_binned_mass_get_effective_radius(
                aero_phase_idx, radius, partial_deriv, (void*) aero_rep_data);
      break;
    case AERO_REP_SINGLE_PARTICLE :
      aero_rep_data = (int*) aero_rep_single_particle_get_effective_radius(
                aero_phase_idx, radius, partial_deriv, (void*) aero_rep_data);
      break;
  }
  return partial_deriv;
}

/** \brief Get the particle number concentration \f$n\f$ (\f$\mbox{\si{\#\per\cubic\centi\metre}}\f$)
 *
 * Calculates particle number concentration, \f$n\f$
 * (\f$\mbox{\si{\#\per\cubic\centi\metre}}\f$), as well as the set of
 * \f$\frac{\partial n}{\partial y}\f$ where \f$y\f$ are variables on the
 * solver state array.
 *
 * \param model_data Pointer to the model data
 * \param state_id Id of the unique state to get data for
 * \param aero_rep_idx Index of aerosol representation to use for calculation
 * \param aero_phase_idx Index of the aerosol phase within the aerosol
 *                       representation
 * \param number_conc Pointer to hold calculated number concentration, \f$n\f$
 *                    (\f$\mbox{\si{\#\per\cubic\centi\metre}}\f$)
 * \return A pointer to a set of partial derivatives
 *         \f$\frac{\partial n}{\partial y}\f$, or a NULL pointer if no partial
 *         derivatives exist
 */
void * aero_rep_get_number_conc(ModelData *model_data, int state_id,
          int aero_rep_idx, int aero_phase_idx, PMC_C_FLOAT *number_conc)
{

  // Set up a pointer for the partial derivatives
  void *partial_deriv = NULL;

  // Get a pointer to the unique states data
  int *aero_rep_data = (int*) (model_data->aero_rep_data);
  aero_rep_data += (model_data->aero_rep_data_size / sizeof(int)) * state_id;
  
  // Get the number of aerosol representations
  int n_aero_rep = *(aero_rep_data++);

  // Loop through the aerosol representations to find the one requested
  for (int i_aero_rep=0; i_aero_rep<aero_rep_idx; i_aero_rep++) {

    // Get the aerosol representation type
    int aero_rep_type = *(aero_rep_data++);

    // Advance the pointer to the next aerosol representation
    switch (aero_rep_type) {
      case AERO_REP_MODAL_BINNED_MASS :
	aero_rep_data = (int*) aero_rep_modal_binned_mass_skip(
                  (void*) aero_rep_data);
        break;
      case AERO_REP_SINGLE_PARTICLE :
	aero_rep_data = (int*) aero_rep_single_particle_skip(
                  (void*) aero_rep_data);
        break;
    }
  }

  // Get the aerosol representation type
  int aero_rep_type = *(aero_rep_data++);

  // Get the particle number concentration
  switch (aero_rep_type) {
    case AERO_REP_MODAL_BINNED_MASS :
      aero_rep_data = (int*) aero_rep_modal_binned_mass_get_number_conc(
                aero_phase_idx, number_conc, partial_deriv,
                (void*) aero_rep_data);
      break;
    case AERO_REP_SINGLE_PARTICLE :
      aero_rep_data = (int*) aero_rep_single_particle_get_number_conc( 
		aero_phase_idx, number_conc, partial_deriv,
                (void*) aero_rep_data);
      break;
  }
  return partial_deriv;
}

/** \brief Check whether aerosol concentrations are per-particle or total for each phase
 *
 * \param model_data Pointer to the model data
 * \param state_id Id of the unique state to get data for
 * \param aero_rep_idx Index of aerosol representation to use for calculation
 * \param aero_phase_idx Index of the aerosol phase within the aerosol
 *                       representation
 * \return 0 for per-particle; 1 for total for each phase
 */
int aero_rep_get_aero_conc_type(ModelData *model_data, int state_id,
          int aero_rep_idx, int aero_phase_idx)
{

  // Initialize the aerosol concentration type
  int aero_conc_type = 0;

  // Get a pointer to the unique states data
  int *aero_rep_data = (int*) (model_data->aero_rep_data);
  aero_rep_data += (model_data->aero_rep_data_size / sizeof(int)) * state_id;
  
  // Get the number of aerosol representations
  int n_aero_rep = *(aero_rep_data++);

  // Loop through the aerosol representations to find the one requested
  for (int i_aero_rep=0; i_aero_rep<aero_rep_idx; i_aero_rep++) {

    // Get the aerosol representation type
    int aero_rep_type = *(aero_rep_data++);

    // Advance the pointer to the next aerosol representation
    switch (aero_rep_type) {
      case AERO_REP_MODAL_BINNED_MASS :
	aero_rep_data = (int*) aero_rep_modal_binned_mass_skip(
                  (void*) aero_rep_data);
        break;
      case AERO_REP_SINGLE_PARTICLE :
	aero_rep_data = (int*) aero_rep_single_particle_skip(
                  (void*) aero_rep_data);
        break;
    }
  }

  // Get the aerosol representation type
  int aero_rep_type = *(aero_rep_data++);

  // Get the type of aerosol concentration
  switch (aero_rep_type) {
    case AERO_REP_MODAL_BINNED_MASS :
      aero_rep_data = (int*) aero_rep_modal_binned_mass_get_aero_conc_type(
                aero_phase_idx, &aero_conc_type, (void*) aero_rep_data);
      break;
    case AERO_REP_SINGLE_PARTICLE :
      aero_rep_data = (int*) aero_rep_single_particle_get_aero_conc_type( 
		aero_phase_idx, &aero_conc_type, (void*) aero_rep_data);
      break;
  }
  return aero_conc_type;
}

/** \brief Get the total mass of an aerosol phase in this representation \f$m\f$ (\f$\mbox{\si{\micro\gram\per\cubic\metre}}\f$)
 *
 * Calculates total aerosol phase mass, \f$m\f$
 * (\f$\mbox{\si{\micro\gram\per\cubic\metre}}\f$), as well as the set of
 * \f$\frac{\partial m}{\partial y}\f$ where \f$y\f$ are variables on the
 * solver state array.
 *
 * \param model_data Pointer to the model data
 * \param state_id Id of the unique state to get data for
 * \param aero_rep_idx Index of aerosol representation to use for calculation
 * \param aero_phase_idx Index of the aerosol phase within the aerosol 
 *                       representation
 * \param aero_phase_mass Pointer to hold calculated aerosol-phase mass,
 *                        \f$m\f$
 *                        (\f$\mbox{\si{\micro\gram\per\cubic\metre}}\f$)
 * \param aero_phase_avg_MW Pointer to hold calculated average MW in the
 *                          aerosol phase (\f$\mbox{\si{\kilogram\per\mole}}\f$)
 * \return A pointer to a set of partial derivatives 
 *         \f$\frac{\partial m}{\partial y}\f$, or a NULL pointer if no partial
 *         derivatives exist
 */
void * aero_rep_get_aero_phase_mass(ModelData *model_data, int state_id,
          int aero_rep_idx, int aero_phase_idx, PMC_C_FLOAT *aero_phase_mass,
          PMC_C_FLOAT *aero_phase_avg_MW)
{

  // Set up a pointer for the partial derivatives
  void *partial_deriv = NULL;

  // Get a pointer to the unique states data
  int *aero_rep_data = (int*) (model_data->aero_rep_data);
  aero_rep_data += (model_data->aero_rep_data_size / sizeof(int)) * state_id;
  
  // Get the number of aerosol representations
  int n_aero_rep = *(aero_rep_data++);

  // Loop through the aerosol representations to find the one requested
  for (int i_aero_rep=0; i_aero_rep<aero_rep_idx; i_aero_rep++) {

    // Get the aerosol representation type
    int aero_rep_type = *(aero_rep_data++);

    // Advance the pointer to the next aerosol representation
    switch (aero_rep_type) {
      case AERO_REP_MODAL_BINNED_MASS :
	aero_rep_data = (int*) aero_rep_modal_binned_mass_skip(
                  (void*) aero_rep_data);
        break;
      case AERO_REP_SINGLE_PARTICLE :
	aero_rep_data = (int*) aero_rep_single_particle_skip(
                  (void*) aero_rep_data);
        break;
    }
  }

  // Get the aerosol representation type
  int aero_rep_type = *(aero_rep_data++);

  // Get the particle number concentration
  switch (aero_rep_type) {
    case AERO_REP_MODAL_BINNED_MASS :
      aero_rep_data = (int*) aero_rep_modal_binned_mass_get_aero_phase_mass( 
		      aero_phase_idx, aero_phase_mass, aero_phase_avg_MW, 
                      partial_deriv, (void*) aero_rep_data);
      break;
    case AERO_REP_SINGLE_PARTICLE :
      aero_rep_data = (int*) aero_rep_single_particle_get_aero_phase_mass( 
		      aero_phase_idx, aero_phase_mass, aero_phase_avg_MW, 
                      partial_deriv, (void*) aero_rep_data);
      break;
  }
  return partial_deriv;
}

/** \brief Add condensed data to the condensed data block for aerosol representations
 *
 * \param aero_rep_type Aerosol representation type
 * \param n_int_param Number of integer parameters
 * \param n_float_param Number of floating-point parameters
 * \param int_param Pointer to integer parameter array
 * \param float_param Pointer to floating-point parameter array
 * \param solver_data Pointer to solver data
 */
void aero_rep_add_condensed_data(int aero_rep_type, int n_int_param,
          int n_float_param, int *int_param, PMC_C_FLOAT *float_param,
          void *solver_data)
{
  ModelData *model_data = (ModelData*)
          &(((SolverData*)solver_data)->model_data);
  int *aero_rep_data;
  PMC_C_FLOAT *flt_ptr;

  // Loop backwards through the unique states
  for (int i_state=model_data->n_states-1; i_state >= 0; i_state--) {

    // Point to the next aerosol representation's space for this state
    aero_rep_data = (int*) (model_data->nxt_aero_rep);
    aero_rep_data += (model_data->aero_rep_data_size / sizeof(int)) * i_state;

    // Add the aerosol representation type
    *(aero_rep_data++) = aero_rep_type;

    // Add integer parameters
    for (int i=0; i<n_int_param; i++) *(aero_rep_data++) = int_param[i];

    // Add floating-point parameters
    flt_ptr = (PMC_C_FLOAT*) aero_rep_data;
    for (int i=0; i<n_float_param; i++)
            *(flt_ptr++) = (PMC_C_FLOAT) float_param[i];

  }

  // Set the pointer for the next free space in aero_rep_data
  model_data->nxt_aero_rep = (void*) flt_ptr;
}

/** \brief Update aerosol representation data
 *
 * \param state_id Index of unique state to update
 * \param update_aero_rep_type Aerosol representation type to update
 * \param update_data Pointer to data needed for update
 * \param solver_data Pointer to solver data
 */
void aero_rep_update_data(int state_id, int update_aero_rep_type,
            void *update_data, void *solver_data)
{
  ModelData *model_data = (ModelData*)
          &(((SolverData*)solver_data)->model_data);

  // Get the number of aerosol representations
  int *aero_rep_data = (int*) (model_data->aero_rep_data);
  aero_rep_data += (model_data->aero_rep_data_size / sizeof(int)) * state_id;
  int n_aero_rep = *(aero_rep_data++);

  // Loop through the aerosol representations advancing the pointer each time
  for (int i_aero_rep=0; i_aero_rep<n_aero_rep; i_aero_rep++) {

    // Get the aerosol representation type
    int aero_rep_type = *(aero_rep_data++);

    // Skip aerosol representations of other types
    if (aero_rep_type!=update_aero_rep_type) {
      switch (aero_rep_type) {
        case AERO_REP_MODAL_BINNED_MASS :
	  aero_rep_data = (int*) aero_rep_modal_binned_mass_skip(
                    (void*)aero_rep_data);
          break;
        case AERO_REP_SINGLE_PARTICLE :
	  aero_rep_data = (int*) aero_rep_single_particle_skip(
                    (void*)aero_rep_data);
          break;
      }

    // ... otherwise, call the update function for reaction types that have them
    } else {
      switch (aero_rep_type) {
        case AERO_REP_MODAL_BINNED_MASS :
          aero_rep_data = (int*) aero_rep_modal_binned_mass_update_data( 
	    		  (void*)update_data, (void*)aero_rep_data);
          break;
        case AERO_REP_SINGLE_PARTICLE :
          aero_rep_data = (int*) aero_rep_single_particle_update_data( 
	    		  (void*)update_data, (void*)aero_rep_data);
          break;
      }
    }
  }
}

/** \brief Print the aerosol representation data
 *
 * \param solver_data Pointer to the solver data
 */
void aero_rep_print_data(void *solver_data)
{
  ModelData *model_data = (ModelData*)
          &(((SolverData*)solver_data)->model_data);

  int *aero_rep_data = (int*) (model_data->aero_rep_data);
  
  // Loop through the unique states to solve
  for (int i_state = 0; i_state < model_data->n_states; i_state++) {

  // Get the number of aerosol representations
  int n_aero_rep = *(aero_rep_data++);

  printf("\n\nAerosol representation data\n\nnumber of aerosol "
            "representations: %d\n\n", n_aero_rep);

  // Loop through the aerosol representations advancing the pointer each time
  for (int i_aero_rep=0; i_aero_rep<n_aero_rep; i_aero_rep++) {

    // Get the aerosol representation type
    int aero_rep_type = *(aero_rep_data++);

    // Call the appropriate printing function
    switch (aero_rep_type) {
      case AERO_REP_MODAL_BINNED_MASS :
	aero_rep_data = (int*) aero_rep_modal_binned_mass_print(
                  (void*)aero_rep_data);
	break;
      case AERO_REP_SINGLE_PARTICLE :
	aero_rep_data = (int*) aero_rep_single_particle_print(
                  (void*)aero_rep_data);
	break;
    }
  }
  }
}

/** \brief Free an update data object
 *
 * \param update_data Object to free
 */
void aero_rep_free_update_data(void *update_data)
{
  free(update_data);
}

