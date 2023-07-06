/* Copyright (C) 2021 Barcelona Supercomputing Center and University of
 * Illinois at Urbana-Champaign
 * SPDX-License-Identifier: MIT
 *
 * This is the c ODE solver for the chemistry module
 */
/** \file
 * \brief Interface to c solvers for chemistry
 */
#include "camp_solver.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "aero_rep_solver.h"
#include "rxn_solver.h"
#include "sub_model_solver.h"
#ifdef CAMP_USE_GPU
#include "cuda/cvode_gpu.h"
#include "cuda/cvode_ls_gpu.h"
#endif
#ifdef CAMP_USE_GSL
#include <gsl/gsl_deriv.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_roots.gpupartmch>
#endif
#include "camp_debug.h"
#include "debug_and_stats/camp_debug_2.h"
#include "debug_and_stats/new.h"

#ifdef CAMP_USE_MPI
#include <mpi.h>
#endif

// Default solver initial time step relative to total integration time
#define DEFAULT_TIME_STEP 1.0
// State advancement factor for Jacobian element evaluation
#define JAC_CHECK_ADV_MAX 1.0E-00
#define JAC_CHECK_ADV_MIN 1.0E-12
// Relative tolerance for Jacobian element evaluation against GSL absolute
// errors
#define JAC_CHECK_GSL_REL_TOL 1.0e-4
// Absolute Jacobian error tolerance
#define JAC_CHECK_GSL_ABS_TOL 1.0e-9
// Set MAX_TIMESTEP_WARNINGS to a negative number to prevent output
#define MAX_TIMESTEP_WARNINGS -1
// Maximum number of steps in discreet addition guess helper
#define GUESS_MAX_ITER 5

// Status codes for calls to camp_solver functions
#define CAMP_SOLVER_SUCCESS 0
#define CAMP_SOLVER_FAIL 1

#define MPI_RANK_DEBUG 0

/** \brief Get a new solver object
 *
 * Return a pointer to a new SolverData object
 *
 * \param n_state_var Number of variables on the state array per grid cell
 * \param n_cells Number of grid cells to solve simultaneously
 * \param var_type Pointer to array of state variable types (solver, constant,
 *                 PSSA)
 * \param n_rxn Number of reactions to include
 * \param n_rxn_int_param Total number of integer reaction parameters
 * \param n_rxn_float_param Total number of floating-point reaction parameters
 * \param n_rxn_env_param Total number of environment-dependent reaction
 * parameters \param n_aero_phase Number of aerosol phases \param
 * n_aero_phase_int_param Total number of integer aerosol phase parameters
 * \param n_aero_phase_float_param Total number of floating-point aerosol phase
 *                                 parameters
 * \param n_aero_rep Number of aerosol representations
 * \param n_aero_rep_int_param Total number of integer aerosol representation
 *                             parameters
 * \param n_aero_rep_float_param Total number of floating-point aerosol
 *                               representation parameters
 * \param n_aero_rep_env_param Total number of environment-dependent aerosol
 *                             representation parameters
 * \param n_sub_model Number of sub models
 * \param n_sub_model_int_param Total number of integer sub model parameters
 * \param n_sub_model_float_param Total number of floating-point sub model
 *                                parameters
 * \param n_sub_model_env_param Total number of environment-dependent sub model
 *                              parameters
 * \return Pointer to the new SolverData object
 */
void *solver_new(int n_state_var, int n_cells, int *var_type, int n_rxn,
                 int n_rxn_int_param, int n_rxn_float_param,
                 int n_rxn_env_param, int n_aero_phase,
                 int n_aero_phase_int_param, int n_aero_phase_float_param,
                 int n_aero_rep, int n_aero_rep_int_param,
                 int n_aero_rep_float_param, int n_aero_rep_env_param,
                 int n_sub_model, int n_sub_model_int_param,
                 int n_sub_model_float_param, int n_sub_model_env_param,
                 int ncounters, int ntimers) {
  // Create the SolverData object
  SolverData *sd = (SolverData *) malloc(sizeof(SolverData));
  if (sd == NULL) {
    printf("\n\nERROR allocating space for SolverData\n\n");
    exit(EXIT_FAILURE);
  }

#ifdef CAMP_USE_SUNDIALS
#ifdef CAMP_DEBUG
  // Default to no debugging output
  sd->debug_out = SUNFALSE;

  // Initialize the Jac solver flag
  sd->eval_Jac = SUNFALSE;
#endif
#endif

  // Do not output precision loss by default
  sd->output_precision = 0;

  // Use the Jacobian estimated derivative in f() by default
  sd->use_deriv_est = 1;

  // Save the number of state variables per grid cell
  sd->model_data.n_per_cell_state_var = n_state_var;

  // Set number of cells to compute simultaneously
  sd->model_data.n_cells = n_cells;

  // Add the variable types to the solver data
  sd->model_data.var_type = (int *) malloc(n_state_var * sizeof(int));
  if (sd->model_data.var_type == NULL) {
    printf("\n\nERROR allocating space for variable types\n\n");
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < n_state_var; i++)
    sd->model_data.var_type[i] = var_type[i];

  // Get the number of solver variables per grid cell
  int n_dep_var = 0;
  for (int i = 0; i < n_state_var; i++)
    if (var_type[i] == CHEM_SPEC_VARIABLE) n_dep_var++;

  // Save the number of solver variables per grid cell
  sd->model_data.n_per_cell_dep_var = n_dep_var;

#ifdef CAMP_USE_SUNDIALS
#ifdef SWAP_DERIV_LOOP_CELLS
  int n_time_deriv_specs=n_dep_var*n_cells;
#else
  int n_time_deriv_specs=n_dep_var;
#endif

  // Set up a TimeDerivative object to use during solving
  if (time_derivative_initialize(&(sd->time_deriv), n_time_deriv_specs) != 1) {
    printf("\n\nERROR initializing the TimeDerivative\n\n");
    exit(EXIT_FAILURE);
  }

  // Set up the solver variable array and helper derivative array
  sd->y = N_VNew_Serial(n_dep_var * n_cells);
  sd->deriv = N_VNew_Serial(n_dep_var * n_cells);
#endif

  // Allocate space for the reaction data and set the number
  // of reactions (including one int for the number of reactions
  // and one int per reaction to store the reaction type)
  sd->model_data.rxn_int_data =
      (int *) malloc((n_rxn_int_param + n_rxn) * sizeof(int));
  if (sd->model_data.rxn_int_data == NULL) {
    printf("\n\nERROR allocating space for reaction integer data\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.rxn_float_data =
      (double *) malloc(n_rxn_float_param * sizeof(double));
  if (sd->model_data.rxn_float_data == NULL) {
    printf("\n\nERROR allocating space for reaction float data\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.rxn_env_data =
      (double *) calloc(n_cells * n_rxn_env_param, sizeof(double));
  if (sd->model_data.rxn_env_data == NULL) {
    printf(
        "\n\nERROR allocating space for environment-dependent "
        "data\n\n");
    exit(EXIT_FAILURE);
  }

  // Allocate space for the reaction data pointers
  sd->model_data.rxn_int_indices = (int *) malloc((n_rxn + 1) * sizeof(int *));
  if (sd->model_data.rxn_int_indices == NULL) {
    printf("\n\nERROR allocating space for reaction integer indices\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.rxn_float_indices = (int *) malloc((n_rxn + 1) * sizeof(int *));
  if (sd->model_data.rxn_float_indices == NULL) {
    printf("\n\nERROR allocating space for reaction float indices\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.rxn_env_idx = (int *) malloc((n_rxn + 1) * sizeof(int));
  if (sd->model_data.rxn_env_idx == NULL) {
    printf(
        "\n\nERROR allocating space for reaction environment-dependent "
        "data pointers\n\n");
    exit(EXIT_FAILURE);
  }

  sd->model_data.n_rxn = n_rxn;
  sd->model_data.n_rxn_int_param = n_rxn_int_param;
  sd->model_data.n_rxn_float_param = n_rxn_float_param;
  sd->model_data.n_added_rxns = 0;
  sd->model_data.n_rxn_env_data = 0;
  sd->model_data.rxn_int_indices[0] = 0;
  sd->model_data.rxn_float_indices[0] = 0;
  sd->model_data.rxn_env_idx[0] = 0;

  // If there are no reactions, flag the solver not to run
  sd->no_solve = (n_rxn == 0);

  // Allocate space for the aerosol phase data and st the number
  // of aerosol phases (including one int for the number of
  // phases)
  sd->model_data.aero_phase_int_data =
      (int *) malloc(n_aero_phase_int_param * sizeof(int));
  if (sd->model_data.aero_phase_int_data == NULL) {
    printf("\n\nERROR allocating space for aerosol phase integer data\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.aero_phase_float_data =
      (double *) malloc(n_aero_phase_float_param * sizeof(double));
  if (sd->model_data.aero_phase_float_data == NULL) {
    printf(
        "\n\nERROR allocating space for aerosol phase floating-point "
        "data\n\n");
    exit(EXIT_FAILURE);
  }

  // Allocate space for the aerosol phase data pointers
  sd->model_data.aero_phase_int_indices =
      (int *) malloc((n_aero_phase + 1) * sizeof(int *));
  if (sd->model_data.aero_phase_int_indices == NULL) {
    printf("\n\nERROR allocating space for reaction integer indices\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.aero_phase_float_indices =
      (int *) malloc((n_aero_phase + 1) * sizeof(int *));
  if (sd->model_data.aero_phase_float_indices == NULL) {
    printf("\n\nERROR allocating space for reaction float indices\n\n");
    exit(EXIT_FAILURE);
  }

  sd->model_data.n_aero_phase = n_aero_phase;
  sd->model_data.n_aero_phase_int_param = n_aero_phase_int_param;
  sd->model_data.n_aero_phase_float_param = n_aero_phase_float_param;
  sd->model_data.n_added_aero_phases = 0;
  sd->model_data.aero_phase_int_indices[0] = 0;
  sd->model_data.aero_phase_float_indices[0] = 0;

  // Allocate space for the aerosol representation data and set
  // the number of aerosol representations (including one int
  // for the number of aerosol representations and one int per
  // aerosol representation to store the aerosol representation
  // type)
  sd->model_data.aero_rep_int_data =
      (int *) malloc((n_aero_rep_int_param + n_aero_rep) * sizeof(int));
  if (sd->model_data.aero_rep_int_data == NULL) {
    printf(
        "\n\nERROR allocating space for aerosol representation integer "
        "data\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.aero_rep_float_data =
      (double *) malloc(n_aero_rep_float_param * sizeof(double));
  if (sd->model_data.aero_rep_float_data == NULL) {
    printf(
        "\n\nERROR allocating space for aerosol representation "
        "floating-point data\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.aero_rep_env_data =
      (double *) calloc(n_cells * n_aero_rep_env_param, sizeof(double));
  if (sd->model_data.aero_rep_env_data == NULL) {
    printf(
        "\n\nERROR allocating space for aerosol representation "
        "environmental parameters\n\n");
    exit(EXIT_FAILURE);
  }

  // Allocate space for the aerosol representation data pointers
  sd->model_data.aero_rep_int_indices =
      (int *) malloc((n_aero_rep + 1) * sizeof(int *));
  if (sd->model_data.aero_rep_int_indices == NULL) {
    printf("\n\nERROR allocating space for reaction integer indices\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.aero_rep_float_indices =
      (int *) malloc((n_aero_rep + 1) * sizeof(int *));
  if (sd->model_data.aero_rep_float_indices == NULL) {
    printf("\n\nERROR allocating space for reaction float indices\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.aero_rep_env_idx =
      (int *) malloc((n_aero_rep + 1) * sizeof(int));
  if (sd->model_data.aero_rep_env_idx == NULL) {
    printf(
        "\n\nERROR allocating space for aerosol representation "
        "environment-dependent data pointers\n\n");
    exit(EXIT_FAILURE);
  }

  sd->model_data.n_aero_rep = n_aero_rep;
  sd->model_data.n_aero_rep_int_param = n_aero_rep_int_param;
  sd->model_data.n_aero_rep_float_param = n_aero_rep_float_param;
  sd->model_data.n_added_aero_reps = 0;
  sd->model_data.n_aero_rep_env_data = 0;
  sd->model_data.aero_rep_int_indices[0] = 0;
  sd->model_data.aero_rep_float_indices[0] = 0;
  sd->model_data.aero_rep_env_idx[0] = 0;

  // Allocate space for the sub model data and set the number of sub models
  // (including one int for the number of sub models and one int per sub
  // model to store the sub model type)
  sd->model_data.sub_model_int_data =
      (int *) malloc((n_sub_model_int_param + n_sub_model) * sizeof(int));
  if (sd->model_data.sub_model_int_data == NULL) {
    printf("\n\nERROR allocating space for sub model integer data\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.sub_model_float_data =
      (double *) malloc(n_sub_model_float_param * sizeof(double));
  if (sd->model_data.sub_model_float_data == NULL) {
    printf("\n\nERROR allocating space for sub model floating-point data\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.sub_model_env_data =
      (double *) calloc(n_cells * n_sub_model_env_param, sizeof(double));
  if (sd->model_data.sub_model_env_data == NULL) {
    printf(
        "\n\nERROR allocating space for sub model environment-dependent "
        "data\n\n");
    exit(EXIT_FAILURE);
  }

  // Allocate space for the sub-model data pointers
  sd->model_data.sub_model_int_indices =
      (int *) malloc((n_sub_model + 1) * sizeof(int *));
  if (sd->model_data.sub_model_int_indices == NULL) {
    printf("\n\nERROR allocating space for reaction integer indices\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.sub_model_float_indices =
      (int *) malloc((n_sub_model + 1) * sizeof(int *));
  if (sd->model_data.sub_model_float_indices == NULL) {
    printf("\n\nERROR allocating space for reaction float indices\n\n");
    exit(EXIT_FAILURE);
  }
  sd->model_data.sub_model_env_idx =
      (int *) malloc((n_sub_model + 1) * sizeof(int));
  if (sd->model_data.sub_model_env_idx == NULL) {
    printf(
        "\n\nERROR allocating space for sub model environment-dependent "
        "data pointers\n\n");
    exit(EXIT_FAILURE);
  }

  sd->model_data.n_sub_model = n_sub_model;
  sd->model_data.n_added_sub_models = 0;
  sd->model_data.n_sub_model_env_data = 0;
  sd->model_data.sub_model_int_indices[0] = 0;
  sd->model_data.sub_model_float_indices[0] = 0;
  sd->model_data.sub_model_env_idx[0] = 0;

#ifdef CAMP_DEBUG_MOCKMONARCH
  get_camp_config_variables(sd);
#endif

#ifdef CAMP_DEBUG
  if (sd->debug_out) print_data_sizes(&(sd->model_data));
#endif

#ifdef DEBUG_RXN
  sd->model_data.counterPhoto = 0;
#endif

#ifdef CAMP_DEBUG_GPU
  sd->counterBCG = 0;
  sd->counterLS = 0;
  sd->timeCVode = 0.0;
#endif

  sd->ncounters = ncounters;
  sd->ntimers = ntimers;

#ifdef CAMP_SOLVER_SPEC_NAMES
  sd->spec_names = (char **)malloc(sizeof(char *) * n_state_var);
#endif

#ifdef DEBUG_CAMP_SOLVER_NEW
  printf("camp solver_run new  n_state_var %d, n_cells %d n_dep_var %d\n",
         sd->model_data.n_per_cell_state_var, n_cells, sd->model_data.n_per_cell_dep_var);
#endif

  // Return a pointer to the new SolverData object
  return (void *)sd;
}

#ifdef CAMP_SOLVER_SPEC_NAMES
void solver_set_spec_name(void *solver_data, char *spec_name,
                          int size_spec_name, int i) {
#ifdef CAMP_USE_MPI
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  if (rank == MPI_RANK_DEBUG) {
    // printf("%d aaa", size_spec_name);
    SolverData *sd;
    sd = (SolverData *)solver_data;
    sd->spec_names[i] = malloc(sizeof(char) * (size_spec_name + 1));
    // printf("%d bbb", size_spec_name);
    // strncpy(sd->spec_names[i], spec_name, size_spec_name); //Copy size bytes
    for (int j = 0; j < size_spec_name; j++)
      sd->spec_names[i][j] = spec_name[j];

    sd->spec_names[i][size_spec_name] = '\0';  // Add null terminator
    // printf("%d ccc", size_spec_name);
  }
#endif
}
#endif

/** \brief Solver initialization
 *
 * Allocate and initialize solver objects
 *
 * \param solver_data Pointer to a SolverData object
 * \param abs_tol Pointer to array of absolute tolerances
 * \param rel_tol Relative integration tolerance
 * \param max_steps Maximum number of internal integration steps
 * \param max_conv_fails Maximum number of convergence failures
 * \return Pointer to an initialized SolverData object
 */
void solver_initialize(void *solver_data, double *abs_tol, double rel_tol,
                       int max_steps, int max_conv_fails, int n_cells_tstep) {
#ifdef CAMP_USE_SUNDIALS
  SolverData *sd;   // SolverData object
  int flag;         // return code from SUNDIALS functions
  int n_dep_var;    // number of dependent variables per grid cell
  int i_dep_var;    // index of dependent variables in loops
  int n_state_var;  // number of variables on the state array per
                    // grid cell
  int n_cells;      // number of cells to solve simultaneously
  int *var_type;    // state variable types

#ifdef DEBUG_solver_initialize
  printf("camp solver_initialize start \n");
#endif

  // Seed the random number generator
  srand((unsigned int)100);

  // Get a pointer to the SolverData
  sd = (SolverData *)solver_data;

  // Create a new solver object
  sd->cvode_mem = CVodeCreate(CV_BDF, CV_NEWTON);
  check_flag_fail((void *)sd->cvode_mem, "CVodeCreate", 0);

  // Get the number of total and dependent variables on the state array,
  // and the type of each state variable. All values are per-grid-cell.
  n_state_var = sd->model_data.n_per_cell_state_var;
  n_dep_var = sd->model_data.n_per_cell_dep_var;
  var_type = sd->model_data.var_type;
  n_cells = sd->model_data.n_cells;

  // Set the solver data
  flag = CVodeSetUserData(sd->cvode_mem, sd);
  check_flag_fail(&flag, "CVodeSetUserData", 1);

  /* Call CVodeInit to initialize the integrator memory and specify the
   * right-hand side function in y'=f(t,y), the initial time t0, and
   * the initial dependent variable vector y. */
  flag = CVodeInit(sd->cvode_mem, f, (realtype)0.0, sd->y);
  check_flag_fail(&flag, "CVodeInit", 1);

  // Set the relative and absolute tolerances
  sd->abs_tol_nv = N_VNew_Serial(n_dep_var * n_cells);
  i_dep_var = 0;
  for (int i_cell = 0; i_cell < n_cells; ++i_cell)
    for (int i_spec = 0; i_spec < n_state_var; ++i_spec)
      if (var_type[i_spec] == CHEM_SPEC_VARIABLE)
        NV_Ith_S(sd->abs_tol_nv, i_dep_var++) = (realtype)abs_tol[i_spec];
  flag = CVodeSVtolerances(sd->cvode_mem, (realtype)rel_tol, sd->abs_tol_nv);
  check_flag_fail(&flag, "CVodeSVtolerances", 1);

  // Add a pointer in the model data to the absolute tolerances for use during
  // solving.
  sd->model_data.abs_tol = abs_tol;

  // Set the maximum number of iterations
  flag = CVodeSetMaxNumSteps(sd->cvode_mem, max_steps);
  check_flag_fail(&flag, "CVodeSetMaxNumSteps", 1);

  // Set the maximum number of convergence failures
  flag = CVodeSetMaxConvFails(sd->cvode_mem, max_conv_fails);
  check_flag_fail(&flag, "CVodeSetMaxConvFails", 1);

  // Set the maximum number of error test failures
  flag = CVodeSetMaxErrTestFails(sd->cvode_mem, max_conv_fails);
  check_flag_fail(&flag, "CVodeSetMaxErrTestFails", 1);

  // Set the maximum number of warnings about a too-small time step
  flag = CVodeSetMaxHnilWarns(sd->cvode_mem, MAX_TIMESTEP_WARNINGS);
  check_flag_fail(&flag, "CVodeSetMaxHnilWarns", 1);

  // Get the structure of the Jacobian matrix
  sd->J = get_jac_init(sd);

  sd->model_data.J_init = SUNMatClone(sd->J);

  SUNMatCopy(sd->J, sd->model_data.J_init);

  // Create a Jacobian matrix for correcting negative predicted concentrations
  // during solving
  sd->J_guess = SUNMatClone(sd->J);
  SUNMatCopy(sd->J, sd->J_guess);

  // Create a KLU SUNLinearSolver
  sd->ls = SUNKLU(sd->y, sd->J);
  check_flag_fail((void *)sd->ls, "SUNKLU", 0);

  // Attach the linear solver and Jacobian to the CVodeMem object
  flag = CVDlsSetLinearSolver(sd->cvode_mem, sd->ls, sd->J);
  check_flag_fail(&flag, "CVDlsSetLinearSolver", 1);

  // Set the Jacobian function to Jac
  flag = CVDlsSetJacFn(sd->cvode_mem, Jac);
  check_flag_fail(&flag, "CVDlsSetJacFn", 1);

  // Set a function to improve guesses for y sent to the linear solver
  flag = CVodeSetDlsGuessHelper(sd->cvode_mem, guess_helper);
  check_flag_fail(&flag, "CVodeSetDlsGuessHelper", 1);

  sd->icell=0;
#ifdef CAMP_USE_GPU
  if(sd->use_cpu==0){
      constructor_cvode_gpu(sd->cvode_mem, sd);
  }
#endif
#ifdef ENABLE_NETCDF
  sd->n_cells_tstep = n_cells_tstep;
  sd->tstep=0;
#endif
#ifdef FAILURE_DETAIL
  // Set a custom error handling function
  flag = CVodeSetErrHandlerFn(sd->cvode_mem, error_handler, (void *)sd);
  check_flag_fail(&flag, "CVodeSetErrHandlerFn", 0);
  sd->counter_fail_solve_print=0;
#endif
#ifdef DEBUG_solver_initialize
  printf("solver_initialize end\n");
#endif
#ifdef NEW
  rxn_get_ids(sd);
#endif
#endif
}

#ifdef CAMP_DEBUG
/** \brief Set the flag indicating whether to output debugging information
 *
 * \param solver_data A pointer to the solver data
 * \param do_output Whether to output debugging information during solving
 */
int solver_set_debug_out(void *solver_data, bool do_output) {
#ifdef CAMP_USE_SUNDIALS
  SolverData *sd = (SolverData *)solver_data;

  sd->debug_out = do_output == true ? SUNTRUE : SUNFALSE;
  return CAMP_SOLVER_SUCCESS;
#else
  return 0;
#endif
}
#endif

#ifdef CAMP_DEBUG
/** \brief Set the flag indicating whether to evalute the Jacobian during
 **        solving
 *
 * \param solver_data A pointer to the solver data
 * \param eval_Jac Flag indicating whether to evaluate the Jacobian during
 *                 solving
 */
int solver_set_eval_jac(void *solver_data, bool eval_Jac) {
#ifdef CAMP_USE_SUNDIALS
  SolverData *sd = (SolverData *)solver_data;

  sd->eval_Jac = eval_Jac == true ? SUNTRUE : SUNFALSE;
  return CAMP_SOLVER_SUCCESS;
#else
  return 0;
#endif
}
#endif

/** \brief Solve for a given timestep
 *
 * \param solver_data A pointer to the initialized solver data
 * \param state A pointer to the full state array (all grid cells)
 * \param env A pointer to the full array of environmental conditions
 *            (all grid cells)
 * \param t_initial Initial time (s)
 * \param t_final (s)
 * \return Flag indicating CAMP_SOLVER_SUCCESS or CAMP_SOLVER_FAIL
 */
int solver_run(void *solver_data, double *state, double *env, double t_initial,
               double t_final, int n_cells) {
  SolverData *sd = (SolverData *)solver_data;
  ModelData *md = &(sd->model_data);
  int n_state_var = md->n_per_cell_state_var;
  int flag;
  int rank = 0;
  int i_cell = sd->icell;

  // Update model data pointers
  sd->model_data.total_state = state;
  sd->model_data.total_env = env;

#ifdef ENABLE_NETCDF
  cell_netcdf(sd);
#endif

  // Update the dependent variables
  int i_dep_var = 0;
  for (int i_cell = 0; i_cell < n_cells; i_cell++){
    for (int i_spec = 0; i_spec < n_state_var; i_spec++) {
      if (sd->model_data.var_type[i_spec] == CHEM_SPEC_VARIABLE) {
        NV_Ith_S(sd->y, i_dep_var++) =
            state[i_spec + i_cell * n_state_var] > TINY
                ? (realtype)state[i_spec + i_cell * n_state_var]
                : TINY;
        // printf("a%d %-le\n",i_spec,state[i_spec]);
      } else if (md->var_type[i_spec] == CHEM_SPEC_CONSTANT) {
        state[i_spec + i_cell * n_state_var] =
            state[i_spec + i_cell * n_state_var] > TINY
                ? state[i_spec + i_cell * n_state_var]
                : TINY;
      }
    }
    md->grid_cell_id = i_cell;
    md->grid_cell_state = &(md->total_state[i_cell * md->n_per_cell_state_var]);
    md->grid_cell_env = &(md->total_env[i_cell * CAMP_NUM_ENV_PARAM_]);
    md->grid_cell_rxn_env_data = &(md->rxn_env_data[i_cell * md->n_rxn_env_data]);
    md->grid_cell_aero_rep_env_data =
        &(md->aero_rep_env_data[i_cell * md->n_aero_rep_env_data]);
    md->grid_cell_sub_model_env_data =
        &(md->sub_model_env_data[i_cell * md->n_sub_model_env_data]);
    // Update the model for the current environmental state
    aero_rep_update_env_state(md);
    sub_model_update_env_state(md);
    rxn_update_env_state(md);
  }

#ifdef RESET_JAC_SOLVING
  //todo check if needed
  //printf("RESET_JAC_SOLVING start\n");
  N_VConst(0.0, md->J_state);
  N_VConst(0.0, md->J_deriv);
  N_VConst(0.0, md->J_tmp);
  N_VConst(0.0, md->J_tmp2);

  SM_NNZ_S(md->J_solver) = SM_NNZ_S(md->J_init);
  for (int i = 0; i <= SM_NP_S(md->J_solver); i++) {
    (SM_INDEXPTRS_S(md->J_solver))[i] = (SM_INDEXPTRS_S(md->J_init))[i];
  }
  for (int i = 0; i < SM_NNZ_S(md->J_solver); i++) {
    (SM_INDEXVALS_S(md->J_solver))[i] = (SM_INDEXVALS_S(md->J_init))[i];
    (SM_DATA_S(md->J_solver))[i] = 0.0;//(SM_DATA_S(md->J_init))[i]; //0.0
  }
#endif

  sd->Jac_eval_fails = 0;
  // Reset the flag indicating a current J_guess
  sd->curr_J_guess = false;

  sd->t_initial = t_initial;
  sd->t_final = t_final;
  // Set the initial time step
  sd->init_time_step = (t_final - t_initial) * DEFAULT_TIME_STEP;

  // Check whether there is anything to solve (filters empty air masses with no
  // emissions)
  if (is_anything_going_on_here(sd, t_initial, t_final) == false)
    return CAMP_SOLVER_SUCCESS;

  // Reinitialize the solver
  flag = CVodeReInit(sd->cvode_mem, t_initial, sd->y);
  check_flag_fail(&flag, "CVodeReInit", 1);

  // Reinitialize the linear solver
  flag = SUNKLUReInit(sd->ls, sd->J, SM_NNZ_S(sd->J), SUNKLU_REINIT_PARTIAL);
  check_flag_fail(&flag, "SUNKLUReInit", 1);

  // Set the inital time step
  flag = CVodeSetInitStep(sd->cvode_mem, sd->init_time_step);
  check_flag_fail(&flag, "CVodeSetInitStep", 1);

  // Run the solver
  realtype t_rt = (realtype)t_initial;

  if (!sd->no_solve) {
#ifdef CAMP_DEBUG_GPU
  double starttimeCvode = MPI_Wtime();
#endif
#ifdef CAMP_USE_GPU
    if(sd->use_cpu==1){
      flag = CVode(sd->cvode_mem, (realtype)t_final, sd->y, &t_rt, CV_NORMAL);
    }
    else{
      if(sd->use_gpu_cvode==1){
        flag = cudaCVode(sd->cvode_mem, (realtype)t_final, sd->y,
          &t_rt, CV_NORMAL, sd);
      }
#ifdef ONLY_BCG
      else{
        flag = CVode_gpu(sd->cvode_mem, (realtype)t_final, sd->y,
             &t_rt, CV_NORMAL, sd);
      }
#endif
    }
#else
    flag = CVode(sd->cvode_mem, (realtype)t_final, sd->y, &t_rt, CV_NORMAL);
#endif
#ifdef CAMP_DEBUG_GPU
    sd->timeCVode += (MPI_Wtime() - starttimeCvode);
#endif
    sd->solver_flag = flag;
#ifdef FAILURE_DETAIL
    if (flag < 0) {
#else
    if (check_flag(&flag, "CVode", 1) != CAMP_SOLVER_SUCCESS) {
      if (flag == -6) {
        long int lsflag;
        int lastflag = CVDlsGetLastFlag(sd->cvode_mem, &lsflag);
        printf("\nLinear Solver Setup Fail: %d %ld", lastflag, lsflag);
      }
      N_Vector deriv = N_VClone(sd->y);
      flag = f(t_initial, sd->y, deriv, sd);
#ifdef CAMP_USE_MPI
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif
      if (flag != 0)
      printf("\nCall to f() at failed state failed with flag %d, rank %d \n",
    flag, rank);
    solver_print_stats(sd->cvode_mem);
#endif
      return CAMP_SOLVER_FAIL;
    }
  }
  // Update the species concentrations on the state array
  i_dep_var = 0;
  //printf("NV_Ith_S(sd->y, i_dep_var)\n");
  for (int i_cell = 0; i_cell < n_cells; i_cell++) {
    for (int i_spec = 0; i_spec < n_state_var; i_spec++) {
      if (md->var_type[i_spec] == CHEM_SPEC_VARIABLE) {
        //printf("%lf ",NV_Ith_S(sd->y, i_dep_var));
        state[i_spec + i_cell * n_state_var] =
            (double)(NV_Ith_S(sd->y, i_dep_var) > 0.0
                         ? NV_Ith_S(sd->y, i_dep_var)
                         : 0.0);
        i_dep_var++;
      }
    }
  }

#ifdef FAILURE_DETAIL
  sd->counter_fail_solve_print=0;
#endif

  // Re-run the pre-derivative calculations to update equilibrium species
  // and apply adjustments to final state
  sub_model_calculate(md);

  return CAMP_SOLVER_SUCCESS;
}

/** \brief Get solver statistics after an integration attempt
 *
 * \param solver_data           Pointer to the solver data
 * \param solver_flag           Last flag returned by the solver
 * \param num_steps             Pointer to set to the number of integration
 *                              steps
 * \param RHS_evals             Pointer to set to the number of right-hand side
 *                              evaluations
 * \param LS_setups             Pointer to set to the number of linear solver
 *                              setups
 * \param error_test_fails      Pointer to set to the number of error test
 *                              failures
 * \param NLS_iters             Pointer to set to the non-linear solver
 *                              iterations
 * \param NLS_convergence_fails Pointer to set to the non-linear solver
 *                              convergence failures
 * \param DLS_Jac_evals         Pointer to set to the direct linear solver
 *                              Jacobian evaluations
 * \param DLS_RHS_evals         Pointer to set to the direct linear solver
 *                              right-hand side evaluations
 * \param last_time_step__s     Pointer to set to the last time step size [s]
 * \param next_time_step__s     Pointer to set to the next time step size [s]
 * \param Jac_eval_fails        Number of Jacobian evaluation failures
 * \param RHS_evals_total       Total calls to `f()`
 * \param Jac_evals_total       Total calls to `Jac()`
 * \param RHS_time__s           Compute time for calls to f() [s]
 * \param Jac_time__s           Compute time for calls to Jac() [s]
 * \param max_loss_precision    Indicators of loss of precision in derivative
 *                              calculation for each species
 */
void solver_get_statistics(void *solver_data, int *solver_flag, int *num_steps,
                           int *RHS_evals, int *LS_setups,
                           int *error_test_fails, int *NLS_iters,
                           int *NLS_convergence_fails, int *DLS_Jac_evals,
                           int *DLS_RHS_evals, double *last_time_step__s,
                           double *next_time_step__s, int *Jac_eval_fails,
                           int *RHS_evals_total, int *Jac_evals_total,
                           double *RHS_time__s, double *Jac_time__s,
                           double *max_loss_precision,
                           int *counters, double *times
                           ) {
  SolverData *sd = (SolverData *)solver_data;
  long int nst, nfe, nsetups, nje, nfeLS, nni, ncfn, netf, nge;
  realtype last_h, curr_h;
  int flag;

  *solver_flag = sd->solver_flag;
  flag = CVodeGetNumSteps(sd->cvode_mem, &nst);
  if (check_flag(&flag, "CVodeGetNumSteps", 1) == CAMP_SOLVER_FAIL) return;
  *num_steps = (int)nst;
  flag = CVodeGetNumRhsEvals(sd->cvode_mem, &nfe);
  if (check_flag(&flag, "CVodeGetNumRhsEvals", 1) == CAMP_SOLVER_FAIL) return;
  *RHS_evals = (int)nfe;
  flag = CVodeGetNumLinSolvSetups(sd->cvode_mem, &nsetups);
  if (check_flag(&flag, "CVodeGetNumLinSolveSetups", 1) == CAMP_SOLVER_FAIL)
    return;
  *LS_setups = (int)nsetups;
  flag = CVodeGetNumErrTestFails(sd->cvode_mem, &netf);
  if (check_flag(&flag, "CVodeGetNumErrTestFails", 1) == CAMP_SOLVER_FAIL)
    return;
  *error_test_fails = (int)netf;
  flag = CVodeGetNumNonlinSolvIters(sd->cvode_mem, &nni);
  if (check_flag(&flag, "CVodeGetNonlinSolvIters", 1) == CAMP_SOLVER_FAIL)
    return;
  *NLS_iters = (int)nni;
  flag = CVodeGetNumNonlinSolvConvFails(sd->cvode_mem, &ncfn);
  if (check_flag(&flag, "CVodeGetNumNonlinSolvConvFails", 1) ==
      CAMP_SOLVER_FAIL)
    return;
  *NLS_convergence_fails = ncfn;
  flag = CVDlsGetNumJacEvals(sd->cvode_mem, &nje);
  if (check_flag(&flag, "CVDlsGetNumJacEvals", 1) == CAMP_SOLVER_FAIL) return;
  *DLS_Jac_evals = (int)nje;
  flag = CVDlsGetNumRhsEvals(sd->cvode_mem, &nfeLS);
  if (check_flag(&flag, "CVDlsGetNumRhsEvals", 1) == CAMP_SOLVER_FAIL) return;
  *DLS_RHS_evals = (int)nfeLS;
  flag = CVodeGetLastStep(sd->cvode_mem, &last_h);
  if (check_flag(&flag, "CVodeGetLastStep", 1) == CAMP_SOLVER_FAIL) return;
  *last_time_step__s = (double)last_h;
  flag = CVodeGetCurrentStep(sd->cvode_mem, &curr_h);
  if (check_flag(&flag, "CVodeGetCurrentStep", 1) == CAMP_SOLVER_FAIL) return;
  *next_time_step__s = (double)curr_h;
  *Jac_eval_fails = sd->Jac_eval_fails;
#ifdef CAMP_DEBUG
  *RHS_evals_total = -1;
  *Jac_evals_total = -1;
  *RHS_time__s = 0.0;
  *Jac_time__s = 0.0;
  *max_loss_precision = sd->max_loss_precision;
#else
  *RHS_evals_total = -1;
  *Jac_evals_total = -1;
  *RHS_time__s = 0.0;
  *Jac_time__s = 0.0;
  *max_loss_precision = 0.0;
#endif

  for(int i=0; i<sd->ncounters; i++){
    counters[i]=0;
  }
  for(int i=0; i<sd->ntimers; i++){
    times[i]=0.0;
  }
  //printf("sd->ntimers ncounters %d %d\n",sd->ntimers,sd->ncounters);

#ifdef CAMP_USE_GPU
#ifdef CAMP_DEBUG_GPU
  if(sd->use_cpu==1){
    if(sd->ntimers>0 && sd->ncounters>0){
      //counters[0]=counterBCG;
      CVodeGettimesCounters(sd->cvode_mem, &times[0], &counters[1]);
      times[2]=sd->timeCVode;;
    }
    else{
      printf("WARNING: In function solver_get_statistics trying to assign times "
             "and counters profilign variables with ncounters || ntimers < 1");
    }
  }
  else{
    ModelDataCPU *mCPU = &(sd->mCPU);
#ifdef CAMP_PROFILE_DEVICE_FUNCTIONS
    solver_get_statistics_gpu(sd);
#endif
    ModelDataGPU *mGPU;
    mGPU = sd->mGPU;
    ModelDataVariable mdvCPU=mCPU->mdvCPU;
    int i;
    if(sd->ncounters>0){
      i=0;
#ifdef CAMP_PROFILE_DEVICE_FUNCTIONS
      counters[i]=mCPU->mdvCPU.counterBCGInternal;
      i++;
#else
      counters[i]=0;
      i++;
#endif
      counters[i]=mCPU->counterBCG;
      i++;
      counters[i]=mCPU->countersolveCVODEGPU;
      i++;
#ifdef CAMP_PROFILE_DEVICE_FUNCTIONS
      counters[i]=mCPU->mdvCPU.countercvStep;
      i++;
#else
      counters[i]=0;
      i++;
#endif
    }
    if(sd->ntimers>0){
      i=0;
      times[i]=mCPU->timeBiConjGrad;
      i++;
      times[i]=mCPU->timeBiConjGradMemcpy;
      i++;
      times[i]=sd->timeCVode;
      i++;
#ifdef CAMP_PROFILE_DEVICE_FUNCTIONS
      times[i]=mdvCPU.dtcudaDeviceCVode;
      i++;
      times[i]=mdvCPU.dtPostBCG;
      i++;
#else
      times[i]=0.;
      i++;
      times[i]=0.;
      i++;
#endif
      times[i]=0.;
      i++;
#ifdef CAMP_PROFILE_DEVICE_FUNCTIONS
      times[i]=mdvCPU.timeNewtonIteration;
      i++;
      times[i]=mdvCPU.timeJac;
      i++;
      times[i]=mdvCPU.timelinsolsetup;
      i++;
      times[i]=mdvCPU.timecalc_Jac;
      i++;
      times[i]=0.;//mdvCPU.timeRXNJac;
      i++;
      times[i]=mdvCPU.timef;
      i++;
      times[i]=mdvCPU.timeguess_helper;
      i++;
#else
      times[i]=0.;
      i++;
      times[i]=0.;
      i++;
      times[i]=0.;
      i++;
      times[i]=0.;
      i++;
      times[i]=0.;
      i++;
      times[i]=0.;
      i++;
      times[i]=0.;
      i++;
#endif
#ifdef OLD_DEV_CPUGPU
      CVodeGettimesCounters(sd->cvode_mem, &times[0], &counters[1]);
      times[i]+=mCPU->timecvStep;
#else
      times[i]=mCPU->timecvStep;
#endif
      i++;
      //for(int i=0;i<sd->ntimers;i++)
        //printf("times[%d]=%le\n",i,times[i]);
    }
    else{
      printf("WARNING: In function solver_get_statistics trying to assign times "
             "and counters profilign variables with ncounters || ntimers < 1");
    }
      solver_reset_statistics_gpu(sd);
  }
#endif
#endif
}

void solver_reset_statistics(void *solver_data, int *counters, double *times)
{
  SolverData *sd = (SolverData *)solver_data;
  for(int i=0; i<sd->ncounters; i++){
    counters[i]=0;
  }
  for(int i=0; i<sd->ntimers; i++){
    times[i]=0.0;
  }
  //printf("sd->ntimers ncounters %d %d\n",sd->ntimers,sd->ncounters);
#ifdef CAMP_USE_GPU
#ifdef CAMP_DEBUG_GPU
  if(sd->use_cpu==1){
    if(sd->ntimers>0 && sd->ncounters>0){
      CVodeResettimesCounters(sd->cvode_mem, &times[0], &counters[1]);
      sd->timeCVode=0;
    }
    else{
      printf("WARNING: In function solver_get_statistics trying to assign times "
             "and counters profilign variables with ncounters || ntimers < 1");
    }
  }
  else{
    ModelDataCPU *mCPU = &(sd->mCPU);
    ModelDataGPU *mGPU;
    mGPU = sd->mGPU;
      ModelDataVariable mdvCPU=mCPU->mdvCPU;
      if(sd->ncounters>0){
#ifdef CAMP_PROFILE_DEVICE_FUNCTIONS
        mCPU->mdvCPU.counterBCGInternal=0;
#endif
        mCPU->counterBCG=0;
        mCPU->countersolveCVODEGPU=0;
#ifdef CAMP_PROFILE_DEVICE_FUNCTIONS
        mCPU->mdvCPU.countercvStep=0;
#endif
      }
      if(sd->ntimers>0){
        mCPU->timeBiConjGrad=0;
        mCPU->timeBiConjGradMemcpy=0;
        sd->timeCVode=0;
#ifdef CAMP_PROFILE_DEVICE_FUNCTIONS
        mdvCPU.dtcudaDeviceCVode=0;
        mdvCPU.dtPostBCG=0;
        mdvCPU.timeNewtonIteration=0;
        mdvCPU.timeJac=0;
        mdvCPU.timelinsolsetup=0;
        mdvCPU.timecalc_Jac=0;
        mdvCPU.timef=0;
        mdvCPU.timeguess_helper=0;
#endif
        mCPU->timecvStep=0;
      }
      else{
        printf("WARNING: In function solver_get_statistics trying to assign times "
               "and counters profilign variables with ncounters || ntimers < 1");
      }
    }
  //printf("times[0] %le counters[1] %d\n",times[0],counters[1]);
#endif
#endif
}

#ifdef CAMP_USE_SUNDIALS
/** \brief Update the model state from the current solver state
 *
 * \param solver_state Solver state vector
 * \param model_data Pointer to the model data (including the state array)
 * \param threshhold A lower limit for model concentrations below which the
 *                   solver value is replaced with a replacement value
 * \param replacement_value Replacement value for low concentrations
 * \return CAMP_SOLVER_SUCCESS for successful update or
 *         CAMP_SOLVER_FAIL for negative concentration
 */
int camp_solver_update_model_state(N_Vector solver_state, SolverData *sd,
                                   realtype threshhold0,
                                   realtype replacement_value0) {
  ModelData *model_data = &(sd->model_data);
  int n_state_var = model_data->n_per_cell_state_var;
  int n_dep_var = model_data->n_per_cell_dep_var;
  int n_cells = model_data->n_cells;

  double replacement_value = TINY;
  double threshhold = -SMALL;

#ifdef DEBUG_CAMP_SOLVER_UPDATE_MODEL_STATE
  if(replacement_value==0.0){
    printf("ERROR camp_solver_update_model_state replacement_value"
           " can't be zero to avoid divisions by zero\n")
     exit(0);
  }
#endif

  int i_dep_var = 0;
  for (int i_cell = 0; i_cell < n_cells; i_cell++) {
    for (int i_spec = 0; i_spec < n_state_var; ++i_spec) {
      if (model_data->var_type[i_spec] == CHEM_SPEC_VARIABLE) {
        if (NV_DATA_S(solver_state)[i_dep_var] < -SMALL)
        {
#ifdef FAILURE_DETAIL
          if(sd->counter_fail_solve_print<1){
            printf("Failed model state update (Innacurate results): [spec %d] = %le\n", i_spec,
               NV_DATA_S(solver_state)[i_dep_var]);
          }
          sd->counter_fail_solve_print++;
#endif
          return CAMP_SOLVER_FAIL;
        }
        // Assign model state to solver_state
        model_data->total_state[i_spec + i_cell * n_state_var] =
            NV_DATA_S(solver_state)[i_dep_var] > threshhold
                ? NV_DATA_S(solver_state)[i_dep_var]
                : replacement_value;
        i_dep_var++;
      }
    }
  }

#ifdef CAMP_USE_GPU
  if(sd->use_cpu==0){
      camp_solver_update_model_state_gpu(solver_state, sd);
  }
#endif

  return CAMP_SOLVER_SUCCESS;
}

/** \brief Compute the time derivative f(t,y)
 *
 * \param t Current model time (s)
 * \param y Dependent variable array
 * \param deriv Time derivative vector f(t,y) to calculate
 * \param solver_data Pointer to the solver data
 * \return Status code
 */
int f(realtype t, N_Vector y, N_Vector deriv, void *solver_data) {
  SolverData *sd = (SolverData *)solver_data;
  ModelData *md = &(sd->model_data);
  realtype time_step;
  int MAX_COUNTER_PRINT=1;

#ifdef CAMP_DEBUG
  clock_t start3 = clock();
#endif

#ifdef CAMP_DEBUG
  // Measure calc_deriv time execution
  clock_t start = clock();
#endif

  // Get a pointer to the derivative data
  double *deriv_data = N_VGetArrayPointer(deriv);

  // Get a pointer to the Jacobian estimated derivative data
  double *jac_deriv_data = N_VGetArrayPointer(md->J_tmp);

  // Get the grid cell dimensions
  int n_cells = md->n_cells;
  int n_state_var = md->n_per_cell_state_var;
  int n_dep_var = md->n_per_cell_dep_var;

  // Get the current integrator time step (s)
  CVodeGetCurrentStep(sd->cvode_mem, &time_step);

  // On the first call to f(), the time step hasn't been set yet, so use the
  // default value
  time_step = time_step > ZERO ? time_step : sd->init_time_step;

  // Update the state array with the current dependent variable values.
  // Signal a recoverable error (positive return value) for negative
  // concentrations.
  if (camp_solver_update_model_state(y, sd, -SMALL, TINY) != CAMP_SOLVER_SUCCESS)
    return 1;

  // Get the Jacobian-estimated derivative
  N_VLinearSum(1.0, y, -1.0, md->J_state, md->J_tmp);
  SUNMatMatvec(md->J_solver, md->J_tmp, md->J_tmp2);
  N_VLinearSum(1.0, md->J_deriv, 1.0, md->J_tmp2, md->J_tmp);

#ifdef SWAP_DERIV_LOOP_CELLS

#ifdef CAMP_DEBUG
  // Measure calc_deriv time execution
  clock_t start2 = clock();
#endif

    for (int i_cell = 0; i_cell < n_cells; ++i_cell) {
      // Set the grid cell state pointers
      md->grid_cell_id = i_cell;
      md->grid_cell_state = &(md->total_state[i_cell * n_state_var]);
      md->grid_cell_env = &(md->total_env[i_cell * CAMP_NUM_ENV_PARAM_]);
      md->grid_cell_rxn_env_data =
          &(md->rxn_env_data[i_cell * md->n_rxn_env_data]);
      md->grid_cell_aero_rep_env_data =
          &(md->aero_rep_env_data[i_cell * md->n_aero_rep_env_data]);
      md->grid_cell_sub_model_env_data =
          &(md->sub_model_env_data[i_cell * md->n_sub_model_env_data]);

      // Update the aerosol representations
      aero_rep_update_state(md);

      // Run the sub models
      sub_model_calculate(md);

  }

  time_derivative_reset(sd->time_deriv);

  // Calculate the time derivative f(t,y)
  rxn_calc_deriv(md, sd->time_deriv, (double)time_step);

  // Update the deriv array
  if (sd->use_deriv_est == 1) {
    time_derivative_output(sd->time_deriv, deriv_data, jac_deriv_data,
                           sd->output_precision);
  } else {
    time_derivative_output(sd->time_deriv, deriv_data, NULL,
                           sd->output_precision);
  }

#ifdef CAMP_DEBUG
  sd->max_loss_precision = time_derivative_max_loss_precision(sd->time_deriv);
#endif

  // Not SWAP_DERIV_LOOP_CELLS
#else

  // Loop through the grid cells and update the derivative array
  for (int i_cell = 0; i_cell < n_cells; ++i_cell) {
    // Set the grid cell state pointers
    md->grid_cell_id = i_cell;
    md->grid_cell_state = &(md->total_state[i_cell * n_state_var]);
    md->grid_cell_env = &(md->total_env[i_cell * CAMP_NUM_ENV_PARAM_]);
    md->grid_cell_rxn_env_data =
        &(md->rxn_env_data[i_cell * md->n_rxn_env_data]);
    md->grid_cell_aero_rep_env_data =
        &(md->aero_rep_env_data[i_cell * md->n_aero_rep_env_data]);
    md->grid_cell_sub_model_env_data =
        &(md->sub_model_env_data[i_cell * md->n_sub_model_env_data]);

    // Update the aerosol representations
    aero_rep_update_state(md);

    // Run the sub models
    sub_model_calculate(md);

    // Reset the TimeDerivative
    time_derivative_reset(sd->time_deriv);

    // Calculate the time derivative f(t,y)
    rxn_calc_deriv(md, sd->time_deriv, (double)time_step);

#ifdef NEW
      rxn_calc_deriv_new(sd);
#endif

    // Update the deriv array
    if (sd->use_deriv_est == 1) {
      //printf("jac_deriv_data %-le\n",jac_deriv_data[0]);
      //printf("Pointer jac_deriv_data before time_derivative_output %p\n",(void *)jac_deriv_data);
      time_derivative_output(sd->time_deriv, deriv_data, jac_deriv_data,
                             sd->output_precision);
      //printf("Pointer jac_deriv_data after time_derivative_output %p\n",(void *)jac_deriv_data);
    } else {
      time_derivative_output(sd->time_deriv, deriv_data, NULL,
                             sd->output_precision);
    }

#ifdef CAMP_DEBUG
    sd->max_loss_precision = time_derivative_max_loss_precision(sd->time_deriv);
#endif

    // Advance the derivative for the next cell
    deriv_data += n_dep_var;
    jac_deriv_data += n_dep_var;
  }

// DERIV_LOOP_CELLS_RXN
#endif

  // Return 0 if success
  return (0);
}

/** \brief Compute the Jacobian
 *
 * \param t Current model time (s)
 * \param y Dependent variable array
 * \param deriv Time derivative vector f(t,y)
 * \param J Jacobian to calculate
 * \param solver_data Pointer to the solver data
 * \param tmp1 Unused vector
 * \param tmp2 Unused vector
 * \param tmp3 Unused vector
 * \return Status code
 */
int Jac(realtype t, N_Vector y, N_Vector deriv, SUNMatrix J, void *solver_data,
        N_Vector tmp1, N_Vector tmp2, N_Vector tmp3) {
  SolverData *sd = (SolverData *)solver_data;
  ModelData *md = &(sd->model_data);
  realtype time_step;

  clock_t start4 = clock();

  // Get the grid cell dimensions
  int n_state_var = md->n_per_cell_state_var;
  int n_dep_var = md->n_per_cell_dep_var;
  int n_cells = md->n_cells;

  // Get pointers to the rxn and parameter Jacobian arrays
  double *J_param_data = SM_DATA_S(md->J_params);

  // !!!! Do not use tmp2 - it is the same as y !!!! //
  // Calculate the the derivative for the current state y without
  // the estimated derivative from the last Jacobian calculation
  sd->use_deriv_est = 0;
  if (f(t, y, deriv, solver_data) != 0) {
    printf("\n Derivative calculation failed on Jac.\n");
    sd->use_deriv_est = 1;
    return 1;
  }
  sd->use_deriv_est = 1;

  // Update the state array with the current dependent variable values
  // Signal a recoverable error (positive return value) for negative
  // concentrations.
  if (camp_solver_update_model_state(y, sd, -SMALL, TINY) != CAMP_SOLVER_SUCCESS)
    return 1;

  // Get the current integrator time step (s)
  CVodeGetCurrentStep(sd->cvode_mem, &time_step);

  // Reset the primary Jacobian
  SM_NNZ_S(J) = SM_NNZ_S(md->J_init);
  for (int i = 0; i <= SM_NP_S(J); i++) {
    (SM_INDEXPTRS_S(J))[i] = (SM_INDEXPTRS_S(md->J_init))[i];
  }
  for (int i = 0; i < SM_NNZ_S(J); i++) {
    (SM_INDEXVALS_S(J))[i] = (SM_INDEXVALS_S(md->J_init))[i];
    (SM_DATA_S(J))[i] = (realtype)0.0;
  }

#ifdef CAMP_DEBUG
  clock_t start2 = clock();
#endif

  // Loop over the grid cells to calculate sub-model and rxn Jacobians
  for (int i_cell = 0; i_cell < n_cells; ++i_cell) {
    // Set the grid cell state pointers
    md->grid_cell_id = i_cell;
    md->grid_cell_state = &(md->total_state[i_cell * n_state_var]);
    md->grid_cell_env = &(md->total_env[i_cell * CAMP_NUM_ENV_PARAM_]);
    md->grid_cell_rxn_env_data =
        &(md->rxn_env_data[i_cell * md->n_rxn_env_data]);
    md->grid_cell_aero_rep_env_data =
        &(md->aero_rep_env_data[i_cell * md->n_aero_rep_env_data]);
    md->grid_cell_sub_model_env_data =
        &(md->sub_model_env_data[i_cell * md->n_sub_model_env_data]);

    // Reset the sub-model and reaction Jacobians
    for (int i = 0; i < SM_NNZ_S(md->J_params); ++i)
      SM_DATA_S(md->J_params)[i] = 0.0;
    jacobian_reset(sd->jac);

    // Update the aerosol representations
    aero_rep_update_state(md);

    // Run the sub models and get the sub-model Jacobian
    sub_model_calculate(md);
    sub_model_get_jac_contrib(md, J_param_data, time_step);
    CAMP_DEBUG_JAC(md->J_params, "sub-model Jacobian");

    // Calculate the reaction Jacobian
    rxn_calc_jac(md, sd->jac, time_step);

//#endif

#ifdef CAMP_DEBUG_JAC_CPU
  check_isnand(sd->jac.production_partials,sd->jac.num_elem,"post rxn_calc_jac");
  check_isnand(sd->jac.loss_partials,sd->jac.num_elem,"post rxn_calc_jac");
#endif

    // Set the solver Jacobian using the reaction and sub-model Jacobians
    JacMap *jac_map = md->jac_map;
    SM_DATA_S(md->J_params)[0] = 1.0;  // dummy value for non-sub model calcs
    for (int i_map = 0; i_map < md->n_mapped_values; ++i_map){
      long double drf_dy = sd->jac.production_partials[jac_map[i_map].rxn_id];
      long double drr_dy = sd->jac.loss_partials[jac_map[i_map].rxn_id];

      SM_DATA_S(J)
      [i_cell * md->n_per_cell_solver_jac_elem + jac_map[i_map].solver_id] +=
          (drf_dy - drr_dy) *
          SM_DATA_S(md->J_params)[jac_map[i_map].param_id];
    }
    CAMP_DEBUG_JAC(J, "solver Jacobian");
  }

  //check_isnand(J_param_data,SM_NNZ_S(md->J_params),k++);
  //check_isnand(SM_DATA_S(J),SM_NNZ_S(J),k++);

#ifdef CAMP_DEBUG_JAC_CPU
  check_isnand(SM_DATA_S(md->J_params),SM_NNZ_S(md->J_params),"post J_params");
#endif

  // Save the Jacobian for use with derivative calculations
  for (int i_elem = 0; i_elem < SM_NNZ_S(J); ++i_elem)
    SM_DATA_S(md->J_solver)[i_elem] = SM_DATA_S(J)[i_elem];
  N_VScale(1.0, y, md->J_state);
  N_VScale(1.0, deriv, md->J_deriv);

#ifdef CAMP_USE_GPU
  if(sd->use_cpu==0){
      set_jac_data_gpu(sd, SM_DATA_S(J));
  }
#endif

#ifdef CAMP_DEBUG
  // Evaluate the Jacobian if flagged to do so
  if (sd->eval_Jac == SUNTRUE) {
    if (!check_Jac(t, y, J, deriv, tmp1, tmp3, solver_data)) {
      ++(sd->Jac_eval_fails);
    }
  }
#endif
  return (0);
}

#ifdef CAMP_USE_GPU
int f_cuda(realtype t, N_Vector y, N_Vector deriv, void *solver_data) {
  SolverData *sd = (SolverData *)solver_data;
  ModelData *md = &(sd->model_data);
  realtype time_step;
  int flag=0;
  if(sd->use_gpu_cvode==0){
    flag = f(t, y, deriv, solver_data);
    rxn_calc_deriv_gpu(sd, y, deriv, (double)time_step);
  }else{
    printf("ERROR f_cuda\n");
    exit(0);
  }
  // Return 0 if success
  return flag;
}
#endif

/** \brief Check a Jacobian for accuracy
 *
 * This function compares Jacobian elements against differences in derivative
 * calculations for small changes to the state array:
 * \f[
 *   J_{ij}(x) = \frac{f_i(x+\sum_j e_j) - f_i(x)}{\epsilon}
 * \f]
 * where \f$\epsilon_j = 10^{-8} \left|x_j\right|\f$
 *
 * \param t Current time [s]
 * \param y Current state array
 * \param J Jacobian matrix to evaluate
 * \param deriv Current derivative \f$f(y)\f$
 * \param tmp Working array the size of \f$y\f$
 * \param tmp1 Working array the size of \f$y\f$
 * \param solver_data Solver data
 * \return True if Jacobian values are accurate, false otherwise
 */
bool check_Jac(realtype t, N_Vector y, SUNMatrix J, N_Vector deriv,
               N_Vector tmp, N_Vector tmp1, void *solver_data) {
  realtype *d_state = NV_DATA_S(y);
  realtype *d_deriv = NV_DATA_S(deriv);
  bool retval = true;

#ifdef CAMP_USE_GSL
  GSLParam gsl_param;
  gsl_function gsl_func;

  // Set up gsl parameters needed during numerical differentiation
  gsl_param.t = t;
  gsl_param.y = tmp;
  gsl_param.deriv = tmp1;
  gsl_param.solver_data = (SolverData *)solver_data;

  // Set up the gsl function
  gsl_func.function = &gsl_f;
  gsl_func.params = &gsl_param;
#endif

  // Calculate the the derivative for the current state y
  if (f(t, y, deriv, solver_data) != 0) {
    printf("\n Derivative calculation failed on check_Jac.\n");
    return false;
  }

  // Loop through the independent variables, numerically calculating
  // the partial derivatives d_fy/d_x
  for (int i_ind = 0; i_ind < NV_LENGTH_S(y); ++i_ind) {
    // If GSL is available, use their numerical differentiation to
    // calculate the partial derivatives. Otherwise, estimate them by
    // advancing the state.
#ifdef CAMP_USE_GSL

    // Reset tmp to the initial state
    N_VScale(ONE, y, tmp);

    // Save the independent species concentration and index
    double x = d_state[i_ind];
    gsl_param.ind_var = i_ind;

    // Skip small concentrations
    if (x < SMALL) continue;

    // Do the numerical differentiation for each potentially non-zero
    // Jacobian element
    for (int i_elem = SM_INDEXPTRS_S(J)[i_ind];
         i_elem < SM_INDEXPTRS_S(J)[i_ind + 1]; ++i_elem) {
      int i_dep = SM_INDEXVALS_S(J)[i_elem];

      double abs_err;
      double partial_deriv;

      gsl_param.dep_var = i_dep;

      bool test_pass = false;
      double h, abs_tol, rel_diff, scaling;

      // Evaluate the Jacobian element over a range of initial step sizes
      for (scaling = JAC_CHECK_ADV_MIN;
           scaling <= JAC_CHECK_ADV_MAX && test_pass == false;
           scaling *= 10.0) {
        // Get the current initial step size
        h = x * scaling;

        // Get the partial derivative d_fy/dx
        if (gsl_deriv_forward(&gsl_func, x, h, &partial_deriv, &abs_err) == 1) {
          printf("\nERROR in numerical differentiation for J[%d][%d]", i_ind,
                 i_dep);
        }

        // Evaluate the results
        abs_tol = 1.2 * fabs(abs_err);
        abs_tol =
            abs_tol > JAC_CHECK_GSL_ABS_TOL ? abs_tol : JAC_CHECK_GSL_ABS_TOL;
        rel_diff = 1.0;
        if (partial_deriv != 0.0)
          rel_diff =
              fabs((SM_DATA_S(J)[i_elem] - partial_deriv) / partial_deriv);
        if (fabs(SM_DATA_S(J)[i_elem] - partial_deriv) < abs_tol ||
            rel_diff < JAC_CHECK_GSL_REL_TOL)
          test_pass = true;
      }

      // If the test does not pass with any initial step size, print out the
      // failure, output the local derivative state and return false
      if (test_pass == false) {
        printf(
            "\nError in Jacobian[%d][%d]: Got %le; expected %le"
            "\n  difference %le is greater than error %le",
            i_ind, i_dep, SM_DATA_S(J)[i_elem], partial_deriv,
            fabs(SM_DATA_S(J)[i_elem] - partial_deriv), abs_tol);
        printf("\n  relative error %le intial step size %le", rel_diff, h);
        printf("\n  initial rate %le initial state %le", d_deriv[i_dep],
               d_state[i_ind]);
        printf(" scaling %le", scaling);
        ModelData *md = &(((SolverData *)solver_data)->model_data);
        for (int i_cell = 0; i_cell < md->n_cells; ++i_cell)
          for (int i_spec = 0; i_spec < md->n_per_cell_state_var; ++i_spec)
            printf("\n cell: %d species %d state_id %d conc: %le", i_cell,
                   i_spec, i_cell * md->n_per_cell_state_var + i_spec,
                   md->total_state[i_cell * md->n_per_cell_state_var + i_spec]);
        retval = false;
        output_deriv_local_state(t, y, deriv, solver_data, &f, i_dep, i_ind,
                                 SM_DATA_S(J)[i_elem], h / 10.0);
      }
    }
#endif
  }
  return retval;
}

#ifdef CAMP_USE_GSL
/** \brief Wrapper function for derivative calculations for numerical solving
 *
 * Wraps the f(t,y) function for use by the GSL numerical differentiation
 * functions.
 *
 * \param x Independent variable \f$x\f$ for calculations of \f$df_y/dx\f$
 * \param param Differentiation parameters
 * \return Partial derivative \f$df_y/dx\f$
 */
double gsl_f(double x, void *param) {
  GSLParam *gsl_param = (GSLParam *)param;
  N_Vector y = gsl_param->y;
  N_Vector deriv = gsl_param->deriv;

  // Set the independent variable
  NV_DATA_S(y)[gsl_param->ind_var] = x;

  // Calculate the derivative
  if (f(gsl_param->t, y, deriv, (void *)gsl_param->solver_data) != 0) {
    printf("\nDerivative calculation failed on gsl_f!");
    for (int i_spec = 0; i_spec < NV_LENGTH_S(y); ++i_spec)
      printf("\n species %d conc: %le", i_spec, NV_DATA_S(y)[i_spec]);
    return 0.0 / 0.0;
  }

  // Return the calculated derivative for the dependent variable
  return NV_DATA_S(deriv)[gsl_param->dep_var];
}
#endif

/** \brief Try to improve guesses of y sent to the linear solver
 *
 * This function checks if there are any negative guessed concentrations,
 * and if there are it calculates a set of initial corrections to the
 * guessed state using the state at time \f$t_{n-1}\f$ and the derivative
 * \f$f_{n-1}\f$ and advancing the state according to:
 * \f[
 *   y_n = y_{n-1} + \sum_{j=1}^m h_j * f_j
 * \f]
 * where \f$h_j\f$ is the largest timestep possible where
 * \f[
 *   y_{j-1} + h_j * f_j > 0
 * \f]
 * and
 * \f[
 *   t_n = t_{n-1} + \sum_{j=1}^m h_j
 * \f]
 *
 * \param t_n Current time [s]
 * \param h_n Current time step size [s] If this is set to zero, the change hf
 *            is assumed to be an adjustment where y_n = y_n1 + hf
 * \param y_n Current guess for \f$y(t_n)\f$
 * \param y_n1 \f$y(t_{n-1})\f$
 * \param hf Current guess for change in \f$y\f$ from \f$t_{n-1}\f$ to
 *            \f$t_n\f$ [input/output]
 * \param solver_data Solver data
 * \param tmp1 Temporary vector for calculations
 * \param corr Vector of calculated adjustments to \f$y(t_n)\f$ [output]
 * \return 1 if corrections were calculated, 0 if not
 */
int guess_helper(const realtype t_n, const realtype h_n, N_Vector y_n,
                 N_Vector y_n1, N_Vector hf, void *solver_data, N_Vector tmp1,
                 N_Vector corr) {
  SolverData *sd = (SolverData *)solver_data;
  realtype *ay_n = NV_DATA_S(y_n);
  realtype *ay_n1 = NV_DATA_S(y_n1);
  realtype *atmp1 = NV_DATA_S(tmp1);
  realtype *acorr = NV_DATA_S(corr);
  realtype *ahf = NV_DATA_S(hf);
  int n_elem = NV_LENGTH_S(y_n);

  // Only try improvements when negative concentrations are predicted
  if (N_VMin(y_n) > -SMALL) return 0;

  CAMP_DEBUG_PRINT_FULL("Trying to improve guess");

  // Copy \f$y(t_{n-1})\f$ to working array
  N_VScale(ONE, y_n1, tmp1);

  // Get  \f$f(t_{n-1})\f$
  if (h_n > ZERO) {
    N_VScale(ONE / h_n, hf, corr);
  } else {
    N_VScale(ONE, hf, corr);
  }
  CAMP_DEBUG_PRINT("Got f0");

  // Advance state interatively
  realtype t_0 = h_n > ZERO ? t_n - h_n : t_n - ONE;
  realtype t_j = ZERO;
  int iter = 0;
  for (; iter < GUESS_MAX_ITER && t_0 + t_j < t_n; iter++) {
    // Calculate \f$h_j\f$
    realtype h_j = t_n - (t_0 + t_j);
    int i_fast = -1;
    for (int i = 0; i < n_elem; i++) {
      realtype t_star = -atmp1[i] / acorr[i];
      if ((t_star > ZERO || (t_star == ZERO && acorr[i] < ZERO)) &&
          t_star < h_j) {
        h_j = t_star;
        i_fast = i;
      }
    }

    // Scale incomplete jumps
    if (i_fast >= 0 && h_n > ZERO)
      h_j *= 0.95 + 0.1 * iter / (double)GUESS_MAX_ITER;
    h_j = t_n < t_0 + t_j + h_j ? t_n - (t_0 + t_j) : h_j;

    // Only make small changes to adjustment vectors used in Newton iteration
    if (h_n == ZERO &&
        t_n - (h_j + t_j + t_0) > ((CVodeMem)sd->cvode_mem)->cv_reltol)
      return -1;

    // Advance the state
    N_VLinearSum(ONE, tmp1, h_j, corr, tmp1);
    CAMP_DEBUG_PRINT_FULL("Advanced state");

    // Advance t_j
    t_j += h_j;

    // Recalculate the time derivative \f$f(t_j)\f$
    if (f(t_0 + t_j, tmp1, corr, solver_data) != 0) {
      CAMP_DEBUG_PRINT("Unexpected failure in guess helper!");
      N_VConst(ZERO, corr);
      return -1;
    }
    ((CVodeMem)sd->cvode_mem)->cv_nfe++;

    if (iter == GUESS_MAX_ITER - 1 && t_0 + t_j < t_n) {
      CAMP_DEBUG_PRINT("Max guess iterations reached!");
      if (h_n == ZERO) return -1;
    }
  }

  CAMP_DEBUG_PRINT_INT("Guessed y_h in steps:", iter);

  // Set the correction vector
  N_VLinearSum(ONE, tmp1, -ONE, y_n, corr);

  // Scale the initial corrections
  if (h_n > ZERO) N_VScale(0.999, corr, corr);

  // Update the hf vector
  N_VLinearSum(ONE, tmp1, -ONE, y_n1, hf);

  return 1;
}

/** \brief Create a sparse Jacobian matrix based on model data
 *
 * \param solver_data A pointer to the SolverData
 * \return Sparse Jacobian matrix with all possible non-zero elements intialize
 *         to 1.0
 */
SUNMatrix get_jac_init(SolverData *sd) {
  int n_rxn;                      /* number of reactions in the mechanism
                                   * (stored in first position in *rxn_data) */
  sunindextype n_jac_elem_param;  /* number of potentially non-zero Jacobian
                                     elements in the reaction matrix*/
  sunindextype n_jac_elem_solver; /* number of potentially non-zero Jacobian
                                     elements in the reaction matrix*/
#ifdef DEBUG_get_jac_init
  printf("get_jac_init start \n");
#endif
  // Number of grid cells
  int n_cells = sd->model_data.n_cells;

  int mattype = CSC_MAT;

  // Number of variables on the state array per grid cell
  // (these are the ids the reactions are initialized with)
  int n_state_var = sd->model_data.n_per_cell_state_var;

  // Number of total state variables
  int n_state_var_total = n_state_var * n_cells;

  // Number of solver variables per grid cell (excludes constants, parameters,
  // etc.)
  int n_dep_var = sd->model_data.n_per_cell_dep_var;

  // Number of total solver variables
  int n_dep_var_total = n_dep_var * n_cells;

  // Initialize the Jacobian for reactions
  if (jacobian_initialize_empty(&(sd->jac),
                                (unsigned int)n_state_var) != 1) {
    printf("\n\nERROR allocating Jacobian structure\n\n");
    exit(EXIT_FAILURE);
  }

  // Add diagonal elements by default
  for (unsigned int i_spec = 0; i_spec < n_state_var; ++i_spec) {
    jacobian_register_element(&(sd->jac), i_spec, i_spec);
  }

  // Fill in the 2D array of flags with Jacobian elements used by the
  // mechanism reactions for a single grid cell
  rxn_get_used_jac_elem(&(sd->model_data), &(sd->jac));

  // Build the sparse Jacobian
  if (jacobian_build_matrix(&(sd->jac)) != 1) {
    printf("\n\nERROR building sparse full-state Jacobian\n\n");
    exit(EXIT_FAILURE);
  }

  // Build the set of time derivative ids
  int *deriv_ids = (int *)malloc(sizeof(int) * n_state_var);

  if (deriv_ids == NULL) {
    printf("\n\nERROR allocating space for derivative ids\n\n");
    exit(EXIT_FAILURE);
  }
  int i_dep_var = 0;
  for (int i_spec = 0; i_spec < n_state_var; i_spec++) {
    if (sd->model_data.var_type[i_spec] == CHEM_SPEC_VARIABLE) {
      deriv_ids[i_spec] = i_dep_var++;
    } else {
      deriv_ids[i_spec] = -1;
    }
  }

  // Update the ids in the reaction data
  rxn_update_ids(&(sd->model_data), deriv_ids, sd->jac);

  ////////////////////////////////////////////////////////////////////////
  // Get the Jacobian elements used in sub model parameter calculations //
  ////////////////////////////////////////////////////////////////////////

  // Initialize the Jacobian for sub-model parameters
  Jacobian param_jac;
  if (jacobian_initialize_empty(&param_jac, (unsigned int)n_state_var) != 1) {
    printf("\n\nERROR allocating sub-model Jacobian structure\n\n");
    exit(EXIT_FAILURE);
  }

  // Set up a dummy element at the first position
  jacobian_register_element(&param_jac, 0, 0);

  // Fill in the 2D array of flags with Jacobian elements used by the
  // mechanism sub models
  sub_model_get_used_jac_elem(&(sd->model_data), &param_jac);

  // Build the sparse Jacobian for sub-model parameters
  if (jacobian_build_matrix(&param_jac) != 1) {
    printf("\n\nERROR building sparse Jacobian for sub-model parameters\n\n");
    exit(EXIT_FAILURE);
  }

  // Save the number of sub model Jacobian elements per grid cell
  n_jac_elem_param = jacobian_number_of_elements(param_jac);
  sd->model_data.n_per_cell_param_jac_elem = (int)n_jac_elem_param;

  // Set up the parameter Jacobian (sized for one grid cell)
  // Initialize the sparse matrix with one extra element (at the first position)
  // for use in mapping that is set to 1.0. (This is safe because there can be
  // no elements on the diagonal in the sub model Jacobian.)
  sd->model_data.J_params =
          SUNSparseMatrix(n_state_var, n_state_var, n_jac_elem_param, mattype);

  // Set the column and row indices
  for (unsigned int i_col = 0; i_col <= n_state_var; ++i_col) {
    (SM_INDEXPTRS_S(sd->model_data.J_params))[i_col] =
                        param_jac.col_ptrs[i_col];
  }
  for (unsigned int i_elem = 0; i_elem < n_jac_elem_param; ++i_elem) {
    (SM_DATA_S(sd->model_data.J_params))[i_elem] = (realtype)0.0;
    (SM_INDEXVALS_S(sd->model_data.J_params))[i_elem] =
            param_jac.row_ids[i_elem];

  }

  // Update the ids in the sub model data
  sub_model_update_ids(&(sd->model_data), deriv_ids, param_jac);

  ////////////////////////////////
  // Set up the solver Jacobian //
  ////////////////////////////////

  // Initialize the Jacobian for sub-model parameters
  Jacobian solver_jac;
  if (jacobian_initialize_empty(&solver_jac, (unsigned int)n_state_var) != 1) {
    printf("\n\nERROR allocating solver Jacobian structure\n\n");
    exit(EXIT_FAILURE);
  }

  // Determine the structure of the solver Jacobian and number of mapped values
  int n_mapped_values = 0;
  for (int i_ind = 0; i_ind < n_state_var; ++i_ind) {
    for (int i_dep = 0; i_dep < n_state_var; ++i_dep) {
      // skip dependent species that are not solver variables and
      // depenedent species that aren't used by any reaction
      if (sd->model_data.var_type[i_dep] != CHEM_SPEC_VARIABLE ||
          jacobian_get_element_id(sd->jac, i_dep, i_ind) == -1)
        continue;
      // If both elements are variable, use the rxn Jacobian only
      if (sd->model_data.var_type[i_ind] == CHEM_SPEC_VARIABLE &&
          sd->model_data.var_type[i_dep] == CHEM_SPEC_VARIABLE) {
        jacobian_register_element(&solver_jac, i_dep, i_ind);
        ++n_mapped_values;
        continue;
      }
      // Check the sub model Jacobian for remaining conditions
      /// \todo Make the Jacobian mapping recursive for sub model parameters
      ///       that depend on other sub model parameters
      for (int j_ind = 0; j_ind < n_state_var; ++j_ind) {
        if (jacobian_get_element_id(param_jac, i_ind, j_ind) != -1 &&
            sd->model_data.var_type[j_ind] == CHEM_SPEC_VARIABLE) {
          jacobian_register_element(&solver_jac, i_dep, j_ind);
          ++n_mapped_values;
        }
      }
    }
  }

  // Build the sparse solver Jacobian
  if (jacobian_build_matrix(&solver_jac) != 1) {
    printf("\n\nERROR building sparse Jacobian for the solver\n\n");
    exit(EXIT_FAILURE);
  }

  // Save the number of non-zero Jacobian elements
  n_jac_elem_solver = jacobian_number_of_elements(solver_jac);
  sd->model_data.n_per_cell_solver_jac_elem = (int)n_jac_elem_solver;

  // Initialize the sparse matrix (for solver state array including all cells)
  SUNMatrix M = SUNSparseMatrix(n_dep_var_total, n_dep_var_total,
                                n_jac_elem_solver * n_cells, mattype);
  sd->model_data.J_solver = SUNSparseMatrix(
          n_dep_var_total, n_dep_var_total, n_jac_elem_solver * n_cells, mattype);

  // Set the column and row indices
  for (unsigned int i_cell = 0; i_cell < n_cells; ++i_cell) {
    for (unsigned int cell_col = 0; cell_col < n_state_var; ++cell_col) {
      if (deriv_ids[cell_col] == -1) continue;
      unsigned int i_col = deriv_ids[cell_col] + i_cell * n_dep_var;
      (SM_INDEXPTRS_S(M))[i_col] =
      (SM_INDEXPTRS_S(sd->model_data.J_solver))[i_col] =
              solver_jac.col_ptrs[cell_col] +
              i_cell * n_jac_elem_solver;
    }
    for (unsigned int cell_elem = 0; cell_elem < n_jac_elem_solver;
         ++cell_elem) {
      unsigned int i_elem = cell_elem + i_cell * n_jac_elem_solver;
      (SM_DATA_S(M))[i_elem] =
      (SM_DATA_S(sd->model_data.J_solver))[i_elem] = (realtype)0.0;
      (SM_INDEXVALS_S(M))[i_elem] =
          (SM_INDEXVALS_S(sd->model_data.J_solver))[i_elem] =
              deriv_ids[jacobian_row_index(solver_jac, cell_elem)] +
              i_cell * n_dep_var;
    }
  }
  (SM_INDEXPTRS_S(M))[n_cells * n_dep_var] =
  (SM_INDEXPTRS_S(sd->model_data.J_solver))[n_cells * n_dep_var] =
          n_cells * n_jac_elem_solver;

  // Allocate space for the map
  sd->model_data.n_mapped_values = n_mapped_values;
  sd->model_data.jac_map =
          (JacMap *)malloc(sizeof(JacMap) * n_mapped_values);
  if (sd->model_data.jac_map == NULL) {
    printf("\n\nERROR allocating space for jacobian map\n\n");
    exit(EXIT_FAILURE);
  }
  JacMap *map = sd->model_data.jac_map;

  // Set map indices (when no sub-model value is used, the param_id is
  // set to 0 which maps to a fixed value of 1.0
  int i_mapped_value = 0;
  for (unsigned int i_ind = 0; i_ind < n_state_var; ++i_ind) {
    for (int i_elem =
             jacobian_column_pointer_value(sd->jac, i_ind);
         i_elem < jacobian_column_pointer_value(sd->jac, i_ind + 1);
         ++i_elem) {
      unsigned int i_dep = sd->jac.row_ids[i_elem];
      // skip dependent species that are not solver variables and
      // depenedent species that aren't used by any reaction
      if (sd->model_data.var_type[i_dep] != CHEM_SPEC_VARIABLE ||
          jacobian_get_element_id(sd->jac, i_dep, i_ind) == -1)
        continue;
      // If both elements are variable, use the rxn Jacobian only
      if (sd->model_data.var_type[i_ind] == CHEM_SPEC_VARIABLE &&
          sd->model_data.var_type[i_dep] == CHEM_SPEC_VARIABLE) {
        map[i_mapped_value].solver_id =
                jacobian_get_element_id(solver_jac, i_dep, i_ind);
        map[i_mapped_value].rxn_id = i_elem;
        map[i_mapped_value].param_id = 0;
        ++i_mapped_value;
        continue;
      }
      // Check the sub model Jacobian for remaining conditions
      // (variable dependent species; independent parameter from sub model)
      for (int j_ind = 0; j_ind < n_state_var; ++j_ind) {
        if (jacobian_get_element_id(param_jac, i_ind, j_ind) != -1 &&
            sd->model_data.var_type[j_ind] == CHEM_SPEC_VARIABLE) {
          map[i_mapped_value].solver_id =
                  jacobian_get_element_id(solver_jac, i_dep, j_ind);
          map[i_mapped_value].rxn_id = i_elem;
          map[i_mapped_value].param_id =
                  jacobian_get_element_id(param_jac, i_ind, j_ind);
          ++i_mapped_value;
        }
      }
    }
  }

  CAMP_DEBUG_JAC_STRUCT(sd->model_data.J_params, "Param struct");
  CAMP_DEBUG_JAC_STRUCT(M, "Solver struct");

  if (i_mapped_value != n_mapped_values) {
    printf("[ERROR-340355266] Internal error");
    exit(EXIT_FAILURE);
  }

  // Create vectors to store Jacobian state and derivative data
  sd->model_data.J_state = N_VClone(sd->y);
  sd->model_data.J_deriv = N_VClone(sd->y);
  sd->model_data.J_tmp = N_VClone(sd->y);
  sd->model_data.J_tmp2 = N_VClone(sd->y);

  // Initialize the Jacobian state and derivative arrays to zero
  // for use before the first call to Jac()
  N_VConst(0.0, sd->model_data.J_state);
  N_VConst(0.0, sd->model_data.J_deriv);

  // Free the memory used
  jacobian_free(&param_jac);
  jacobian_free(&solver_jac);
  free(deriv_ids);

#ifdef DEBUG_get_jac_init
  printf("get_jac_init end \n");
#endif
  return M;
}

/** \brief Check the return value of a SUNDIALS function
 *
 * \param flag_value A pointer to check (either for NULL, or as an int pointer
 *                   giving the flag value
 * \param func_name A string giving the function name returning this result code
 * \param opt A flag indicating the type of check to perform (0 for NULL
 *            pointer check; 1 for integer flag check)
 * \return Flag indicating CAMP_SOLVER_SUCCESS or CAMP_SOLVER_FAIL
 */
int check_flag(void *flag_value, char *func_name, int opt) {
  int *err_flag;
  int rank = 999;
#ifdef CAMP_USE_MPI
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif

  /* Check for a NULL pointer */
  if (opt == 0 && flag_value == NULL) {
    if (rank == 0) {
      fprintf(stderr,
              "\nSUNDIALS_ERROR: %s() failed - returned NULL pointer\n\n",
              func_name);
    }
    return CAMP_SOLVER_FAIL;
  }

  /* Check if flag < 0 */
  else if (opt == 1) {
    err_flag = (int *)flag_value;
    if (*err_flag < 0) {
      if (rank == 0) {
        fprintf(stderr,
                "\nSUNDIALS_ERROR: %s() failed with flag = %d, rank %d\n\n",
                func_name, *err_flag, rank);
      }
      return CAMP_SOLVER_FAIL;
    }
  }
  return CAMP_SOLVER_SUCCESS;
}

/** \brief Check the return value of a SUNDIALS function and exit on failure
 *
 * \param flag_value A pointer to check (either for NULL, or as an int pointer
 *                   giving the flag value
 * \param func_name A string giving the function name returning this result code
 * \param opt A flag indicating the type of check to perform (0 for NULL
 *            pointer check; 1 for integer flag check)
 */
void check_flag_fail(void *flag_value, char *func_name, int opt) {
  if (check_flag(flag_value, func_name, opt) == CAMP_SOLVER_FAIL) {
    exit(EXIT_FAILURE);
  }
}

/** \brief Print solver statistics
 *
 * \param cvode_mem Solver object
 */
static void solver_print_stats(void *cvode_mem) {
  long int nst, nfe, nsetups, nje, nfeLS, nni, ncfn, netf, nge;
  realtype last_h, curr_h;
  int flag;

  flag = CVodeGetNumSteps(cvode_mem, &nst);
  if (check_flag(&flag, "CVodeGetNumSteps", 1) == CAMP_SOLVER_FAIL) return;
  flag = CVodeGetNumRhsEvals(cvode_mem, &nfe);
  if (check_flag(&flag, "CVodeGetNumRhsEvals", 1) == CAMP_SOLVER_FAIL) return;
  flag = CVodeGetNumLinSolvSetups(cvode_mem, &nsetups);
  if (check_flag(&flag, "CVodeGetNumLinSolveSetups", 1) == CAMP_SOLVER_FAIL)
    return;
  flag = CVodeGetNumErrTestFails(cvode_mem, &netf);
  if (check_flag(&flag, "CVodeGetNumErrTestFails", 1) == CAMP_SOLVER_FAIL)
    return;
  flag = CVodeGetNumNonlinSolvIters(cvode_mem, &nni);
  if (check_flag(&flag, "CVodeGetNonlinSolvIters", 1) == CAMP_SOLVER_FAIL)
    return;
  flag = CVodeGetNumNonlinSolvConvFails(cvode_mem, &ncfn);
  if (check_flag(&flag, "CVodeGetNumNonlinSolvConvFails", 1) ==
      CAMP_SOLVER_FAIL)
    return;
  flag = CVDlsGetNumJacEvals(cvode_mem, &nje);
  if (check_flag(&flag, "CVDlsGetNumJacEvals", 1) == CAMP_SOLVER_FAIL) return;
  flag = CVDlsGetNumRhsEvals(cvode_mem, &nfeLS);
  if (check_flag(&flag, "CVDlsGetNumRhsEvals", 1) == CAMP_SOLVER_FAIL) return;
  flag = CVodeGetNumGEvals(cvode_mem, &nge);
  if (check_flag(&flag, "CVodeGetNumGEvals", 1) == CAMP_SOLVER_FAIL) return;
  flag = CVodeGetLastStep(cvode_mem, &last_h);
  if (check_flag(&flag, "CVodeGetLastStep", 1) == CAMP_SOLVER_FAIL) return;
  flag = CVodeGetCurrentStep(cvode_mem, &curr_h);
  if (check_flag(&flag, "CVodeGetCurrentStep", 1) == CAMP_SOLVER_FAIL) return;

  printf("\nSUNDIALS Solver Statistics:\n");
  printf("number of steps = %-6ld RHS evals = %-6ld LS setups = %-6ld\n", nst,
         nfe, nsetups);
  printf("error test fails = %-6ld LS iters = %-6ld NLS iters = %-6ld\n", netf,
         nni, ncfn);
  printf(
      "NL conv fails = %-6ld Dls Jac evals = %-6ld Dls RHS evals = %-6ld G "
      "evals ="
      " %-6ld\n",
      ncfn, nje, nfeLS, nge);
  printf("Last time step = %le Next time step = %le\n", last_h, curr_h);
}

#endif  // CAMP_USE_SUNDIALS

/** \brief Free a SolverData object
 *
 * \param solver_data Pointer to the SolverData object to free
 */
void solver_free(void *solver_data) {
  SolverData *sd = (SolverData *)solver_data;
  ModelData *md = &(sd->model_data);

  //printf("solver_free start\n");

#ifdef CAMP_USE_SUNDIALS
  // free the SUNDIALS solver
  CVodeFree(&(sd->cvode_mem));

  // free the absolute tolerance vector
  N_VDestroy(sd->abs_tol_nv);

  // free the TimeDerivative
  time_derivative_free(sd->time_deriv);

  // free the Jacobian
  jacobian_free(&(sd->jac));

  // free the derivative vectors
  N_VDestroy(sd->y);
  N_VDestroy(sd->deriv);

  // destroy the Jacobian marix
  SUNMatDestroy(sd->J);

  // destroy Jacobian matrix for guessing state
  SUNMatDestroy(sd->J_guess);

  // free the linear solver
  SUNLinSolFree(sd->ls);
#endif

#ifdef CAMP_USE_GPU
  if(sd->use_cpu==0){
      free_gpu_cu(sd);
  }
#endif

#ifdef NEW
  rxn_free();
#endif

}

#ifdef CAMP_USE_SUNDIALS
/** \brief Determine if there is anything to solve
 *
 * If the solver state concentrations and the derivative vector are very small,
 * there is no point running the solver
 */
bool is_anything_going_on_here(SolverData *sd, realtype t_initial,
                               realtype t_final) {
  ModelData *md = &(sd->model_data);
  if (f(t_initial, sd->y, sd->deriv, sd)) {
    int i_dep_var = 0;
    for (int i_cell = 0; i_cell < md->n_cells; ++i_cell) {
      for (int i_spec = 0; i_spec < md->n_per_cell_state_var; ++i_spec) {
        if (md->var_type[i_spec] == CHEM_SPEC_VARIABLE) {
          if (NV_Ith_S(sd->y, i_dep_var) >
              NV_Ith_S(sd->abs_tol_nv, i_dep_var) * 1.0e-10)
            return true;
          if (NV_Ith_S(sd->deriv, i_dep_var) * (t_final - t_initial) >
              NV_Ith_S(sd->abs_tol_nv, i_dep_var) * 1.0e-10)
            return true;
          i_dep_var++;
        }
      }
    }
    printf("DEBUG: is_anything_going_on_here is false, returning success without cvode computing\n");
    return false;
  }
  return true;
}
#endif

/** \brief Custom error handling function
 *
 * This is used for quiet operation. Solver failures are returned with a flag
 * from the solver_run() function.
 */
void error_handler(int error_code, const char *module, const char *function,
                   char *msg, void *sd) {
  // Do nothing
}

/** \brief Free a ModelData object
 *
 * \param model_data Pointer to the ModelData object to free
 */
void model_free(ModelData model_data) {

#ifdef CAMP_USE_SUNDIALS
  // Destroy the initialized Jacbobian matrix
  SUNMatDestroy(model_data.J_init);
  SUNMatDestroy(model_data.J_params);
  SUNMatDestroy(model_data.J_solver);
  N_VDestroy(model_data.J_state);
  N_VDestroy(model_data.J_deriv);
  N_VDestroy(model_data.J_tmp);
  N_VDestroy(model_data.J_tmp2);
#endif
  free(model_data.jac_map);
  free(model_data.jac_map_params);
  free(model_data.var_type);
  free(model_data.rxn_int_data);
  free(model_data.rxn_float_data);
  free(model_data.rxn_env_data);
  free(model_data.rxn_int_indices);
  free(model_data.rxn_float_indices);
  free(model_data.rxn_env_idx);
  free(model_data.aero_phase_int_data);
  free(model_data.aero_phase_float_data);
  free(model_data.aero_phase_int_indices);
  free(model_data.aero_phase_float_indices);
  free(model_data.aero_rep_int_data);
  free(model_data.aero_rep_float_data);
  free(model_data.aero_rep_env_data);
  free(model_data.aero_rep_int_indices);
  free(model_data.aero_rep_float_indices);
  free(model_data.aero_rep_env_idx);
  free(model_data.sub_model_int_data);
  free(model_data.sub_model_float_data);
  free(model_data.sub_model_env_data);
  free(model_data.sub_model_int_indices);
  free(model_data.sub_model_float_indices);
  free(model_data.sub_model_env_idx);
}

/** \brief Free update data
 *
 * \param update_data Object to free
 */
void solver_free_update_data(void *update_data) { free(update_data); }
