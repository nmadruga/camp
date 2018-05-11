/* Copyright (C) 2015-2017 Matthew Dawson
 * Licensed under the GNU General Public License version 2 or (at your
 * option) any later version. See the file COPYING for details.
 *
 * Header file for reaction functions
 *
 * TODO Automatically generate rxn_solver.c and rxn_solver.h code
 * maybe using cmake?
 *
*/
/** \file
 * \brief Header file for reaction solver functions
*/
#ifndef RXN_SOLVER_H_
#define RXN_SOLVER_H_
#include "phlex_solver.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <xmmintrin.h>

#define _RXN_PREFETCH_(addr) _mm_prefetch(((char*)(addr)), _MM_HINT_T0)

#ifdef PMC_USE_SUNDIALS

// activity
void * rxn_PDFiTE_activity_get_used_jac_elem(void *rxn_data, bool **jac_struct);
void * rxn_PDFiTE_activity_update_ids(int *deriv_ids, int **jac_ids, void *rxn_data);
void * rxn_PDFiTE_activity_update_env_state(realtype *env_data, void *rxn_data);
void * rxn_PDFiTE_activity_pre_calc(ModelData *model_data, void *rxn_data);
void * rxn_PDFiTE_activity_calc_deriv_contrib(ModelData *model_data, realtype *deriv, void *rxn_data);
void * rxn_PDFiTE_activity_calc_jac_contrib(ModelData *model_data, realtype *J, void *rxn_data);
void * rxn_PDFiTE_activity_skip(void *rxn_data);
void * rxn_PDFiTE_activity_print(void *rxn_data);
void * rxn_PDFiTE_activity_get_rate(void *rxn_data, realtype *state, realtype *env, realtype *rate);

// aqueous_equilibrium
void * rxn_aqueous_equilibrium_get_used_jac_elem(void *rxn_data, bool **jac_struct);
void * rxn_aqueous_equilibrium_update_ids(int *deriv_ids, int **jac_ids, void *rxn_data);
void * rxn_aqueous_equilibrium_update_env_state(realtype *env_data, void *rxn_data);
void * rxn_aqueous_equilibrium_pre_calc(ModelData *model_data, void *rxn_data);
void * rxn_aqueous_equilibrium_calc_deriv_contrib(ModelData *model_data, realtype *deriv, void *rxn_data, double time_step);
void * rxn_aqueous_equilibrium_calc_jac_contrib(ModelData *model_data, realtype *J, void *rxn_data, double time_step);
void * rxn_aqueous_equilibrium_skip(void *rxn_data);
void * rxn_aqueous_equilibrium_print(void *rxn_data);
void * rxn_aqueous_equilibrium_get_rate(void *rxn_data, realtype *state, realtype *env, realtype *rate);

// arrhenius
void * rxn_arrhenius_get_used_jac_elem(void *rxn_data, bool **jac_struct);
void * rxn_arrhenius_update_ids(int *deriv_ids, int **jac_ids, void *rxn_data);
void * rxn_arrhenius_update_env_state(realtype *env_data, void *rxn_data);
void * rxn_arrhenius_pre_calc(ModelData *model_data, void *rxn_data);
void * rxn_arrhenius_calc_deriv_contrib(realtype *state, realtype *deriv, void *rxn_data);
void * rxn_arrhenius_calc_jac_contrib(realtype *state, realtype *J, void *rxn_data);
void * rxn_arrhenius_skip(void *rxn_data);
void * rxn_arrhenius_print(void *rxn_data);
void * rxn_arrhenius_get_rate(void *rxn_data, realtype *state, realtype *env, realtype *rate);

// CMAQ_H2O2
void * rxn_CMAQ_H2O2_get_used_jac_elem(void *rxn_data, bool **jac_struct);
void * rxn_CMAQ_H2O2_update_ids(int *deriv_ids, int **jac_ids, void *rxn_data);
void * rxn_CMAQ_H2O2_update_env_state(realtype *env_data, void *rxn_data);
void * rxn_CMAQ_H2O2_pre_calc(ModelData *model_data, void *rxn_data);
void * rxn_CMAQ_H2O2_calc_deriv_contrib(realtype *state, realtype *deriv, void *rxn_data);
void * rxn_CMAQ_H2O2_calc_jac_contrib(realtype *state, realtype *J, void *rxn_data);
void * rxn_CMAQ_H2O2_skip(void *rxn_data);
void * rxn_CMAQ_H2O2_print(void *rxn_data);
void * rxn_CMAQ_H2O2_get_rate(void *rxn_data, realtype *state, realtype *env, realtype *rate);

// CMAQ_OH_HNO3
void * rxn_CMAQ_OH_HNO3_get_used_jac_elem(void *rxn_data, bool **jac_struct);
void * rxn_CMAQ_OH_HNO3_update_ids(int *deriv_ids, int **jac_ids, void *rxn_data);
void * rxn_CMAQ_OH_HNO3_update_env_state(realtype *env_data, void *rxn_data);
void * rxn_CMAQ_OH_HNO3_pre_calc(ModelData *model_data, void *rxn_data);
void * rxn_CMAQ_OH_HNO3_calc_deriv_contrib(realtype *state, realtype *deriv, void *rxn_data);
void * rxn_CMAQ_OH_HNO3_calc_jac_contrib(realtype *state, realtype *J, void *rxn_data);
void * rxn_CMAQ_OH_HNO3_skip(void *rxn_data);
void * rxn_CMAQ_OH_HNO3_print(void *rxn_data);
void * rxn_CMAQ_OH_HNO3_get_rate(void *rxn_data, realtype *state, realtype *env, realtype *rate);

// HL_phase_transfer
void * rxn_HL_phase_transfer_get_used_jac_elem(void *rxn_data, bool **jac_struct);
void * rxn_HL_phase_transfer_update_ids(int *deriv_ids, int **jac_ids, void *rxn_data);
void * rxn_HL_phase_transfer_update_env_state(realtype *env_data, void *rxn_data);
void * rxn_HL_phase_transfer_pre_calc(ModelData *model_data, void *rxn_data);
void * rxn_HL_phase_transfer_calc_deriv_contrib(ModelData *model_data, realtype *deriv, void *rxn_data);
void * rxn_HL_phase_transfer_calc_jac_contrib(ModelData *model_data, realtype *J, void *rxn_data);
void * rxn_HL_phase_transfer_skip(void *rxn_data);
void * rxn_HL_phase_transfer_print(void *rxn_data);
void * rxn_HL_phase_transfer_get_rate(void *rxn_data, realtype *state, realtype *env, realtype *rate);

// photolysis
void * rxn_photolysis_get_used_jac_elem(void *rxn_data, bool **jac_struct);
void * rxn_photolysis_update_ids(int *deriv_ids, int **jac_ids, void *rxn_data);
void * rxn_photolysis_update_env_state(realtype *env_data, void *rxn_data);
void * rxn_photolysis_pre_calc(ModelData *model_data, void *rxn_data);
void * rxn_photolysis_calc_deriv_contrib(realtype *state, realtype *deriv, void *rxn_data);
void * rxn_photolysis_calc_jac_contrib(realtype *state, realtype *J, void *rxn_data);
void * rxn_photolysis_set_photo_rate(int photo_id, realtype base_rate, void *rxn_data);
void * rxn_photolysis_skip(void *rxn_data);
void * rxn_photolysis_print(void *rxn_data);
void * rxn_photolysis_get_rate(void *rxn_data, realtype *state, realtype *env, realtype *rate);

// SIMPOL_phase_transfer
void * rxn_SIMPOL_phase_transfer_get_used_jac_elem(void *rxn_data, bool **jac_struct);
void * rxn_SIMPOL_phase_transfer_update_ids(int *deriv_ids, int **jac_ids, void *rxn_data);
void * rxn_SIMPOL_phase_transfer_update_env_state(realtype *env_data, void *rxn_data);
void * rxn_SIMPOL_phase_transfer_pre_calc(ModelData *model_data, void *rxn_data);
void * rxn_SIMPOL_phase_transfer_calc_deriv_contrib(ModelData *model_data, realtype *deriv, void *rxn_data);
void * rxn_SIMPOL_phase_transfer_calc_jac_contrib(ModelData *model_data, realtype *J, void *rxn_data);
void * rxn_SIMPOL_phase_transfer_skip(void *rxn_data);
void * rxn_SIMPOL_phase_transfer_print(void *rxn_data);
void * rxn_SIMPOL_phase_transfer_get_rate(void *rxn_data, realtype *state, realtype *env, realtype *rate);

// troe
void * rxn_troe_get_used_jac_elem(void *rxn_data, bool **jac_struct);
void * rxn_troe_update_ids(int *deriv_ids, int **jac_ids, void *rxn_data);
void * rxn_troe_update_env_state(realtype *env_data, void *rxn_data);
void * rxn_troe_pre_calc(ModelData *model_data, void *rxn_data);
void * rxn_troe_calc_deriv_contrib(realtype *state, realtype *deriv, void *rxn_data);
void * rxn_troe_calc_jac_contrib(realtype *state, realtype *J, void *rxn_data);
void * rxn_troe_skip(void *rxn_data);
void * rxn_troe_print(void *rxn_data);
void * rxn_troe_get_rate(void *rxn_data, realtype *state, realtype *env, realtype *rate);

// ZSR_aerosol_water
void * rxn_ZSR_aerosol_water_get_used_jac_elem(void *rxn_data, bool **jac_struct);
void * rxn_ZSR_aerosol_water_update_ids(int *deriv_ids, int **jac_ids, void *rxn_data);
void * rxn_ZSR_aerosol_water_update_env_state(realtype *env_data, void *rxn_data);
void * rxn_ZSR_aerosol_water_pre_calc(ModelData *model_data, void *rxn_data);
void * rxn_ZSR_aerosol_water_calc_deriv_contrib(ModelData *model_data, realtype *deriv, void *rxn_data);
void * rxn_ZSR_aerosol_water_calc_jac_contrib(ModelData *model_data, realtype *J, void *rxn_data);
void * rxn_ZSR_aerosol_water_skip(void *rxn_data);
void * rxn_ZSR_aerosol_water_print(void *rxn_data);
void * rxn_ZSR_aerosol_water_get_rate(void *rxn_data, realtype *state, realtype *env, realtype *rate);

#endif
#endif