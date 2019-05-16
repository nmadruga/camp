/* Copyright (C) 2015-2018 Matthew Dawson
 * Licensed under the GNU General Public License version 1 or (at your
 * option) any later version. See the file COPYING for details.
 *
 * Header for aerosol representations functions
 *
 */
/** \file
 * \brief Header file for aerosol representations functions
 */
#ifndef AERO_REPS_H_
#define AERO_REPS_H_
#include "../phlex_gpu_solver.h"

// binned/modal mass
int aero_rep_modal_binned_mass_get_used_jac_elem(
          ModelDatagpu *model_data, int aero_phase_idx,
          void *aero_rep_data, bool *jac_struct);
void * aero_rep_modal_binned_mass_get_dependencies(
          void *aero_rep_data, bool *state_flags);
void * aero_rep_modal_binned_mass_update_env_state(
          double *env_data, void *aero_rep_data);
void * aero_rep_modal_binned_mass_update_state(
          ModelDatagpu *model_data, void *aero_rep_data);
void * aero_rep_modal_binned_mass_get_effective_radius(
          int aero_phase_idx, double *radius, double *partial_deriv,
          void *aero_rep_data);
void * aero_rep_modal_binned_mass_get_number_conc(
          int aero_phase_idx, double *number_conc, double *partial_deriv,
          void *aero_rep_data);
void * aero_rep_modal_binned_mass_get_aero_conc_type(
          int aero_phase_idx, int *aero_conc_type, void *aero_rep_data);
void * aero_rep_modal_binned_mass_get_aero_phase_mass(
          int aero_phase_idx, double *aero_phase_mass,
          double *aero_phase_avg_MW, double *partial_deriv,
          void *aero_rep_data);
void * aero_rep_modal_binned_mass_update_data(
          void *update_data, void *aero_rep_data);
void * aero_rep_modal_binned_mass_print(
          void *aero_rep_data);
void * aero_rep_modal_binned_mass_skip(
          void *aero_rep_data);
void * aero_rep_modal_binned_mass_create_gmd_update_data();
void aero_rep_modal_binned_mass_set_gmd_update_data(void *update_data,
          int aero_rep_id, int section_id, double gmd);
void * aero_rep_modal_binned_mass_create_gsd_update_data();
void aero_rep_modal_binned_mass_set_gsd_update_data(void *update_data,
          int aero_rep_id, int section_id, double gsd);

// single particle
int aero_rep_single_particle_get_used_jac_elem(
          ModelDatagpu *model_data, int aero_phase_idx,
          void *aero_rep_data, bool *jac_struct);
void * aero_rep_single_particle_get_dependencies(
          void *aero_rep_data, bool *state_flags);
void * aero_rep_single_particle_update_env_state(
          double *env_data, void *aero_rep_data);
void * aero_rep_single_particle_update_state(
          ModelDatagpu *model_data, void *aero_rep_data);
void * aero_rep_single_particle_get_effective_radius(
          int aero_phase_idx, double *radius, double *partial_deriv,
          void *aero_rep_data);
void * aero_rep_single_particle_get_number_conc(
          int aero_phase_idx, double *number_conc, double *partial_deriv,
          void *aero_rep_data);
void * aero_rep_single_particle_get_aero_conc_type(
          int aero_phase_idx, int *aero_conc_type, void *aero_rep_data);
void * aero_rep_single_particle_get_aero_phase_mass(
          int aero_phase_idx, double *aero_phase_mass,
          double *aero_phase_avg_MW, double *partial_deriv,
          void *aero_rep_data);
void * aero_rep_single_particle_update_data(
          void *update_data, void *aero_rep_data);
void * aero_rep_single_particle_print(
          void *aero_rep_data);
void * aero_rep_single_particle_skip(
          void *aero_rep_data);
void * aero_rep_single_particle_create_radius_update_data();
void aero_rep_single_particle_set_radius_update_data(void *update_data,
          int aero_rep_id, double radius);
void * aero_rep_single_particle_create_number_update_data();
void aero_rep_single_particle_set_number_update_data(void *update_data,
          int aero_rep_id, double number_conc);

#endif
