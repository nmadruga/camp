/* Copyright (C) 2020 Christian Guzman and Guillermo Oyarzun
 * Licensed under the GNU General Public License version 1 or (at your
 * option) any later version. See the file COPYING for details.
 *
 * Iterative GPU solver
 *
 */

#ifndef ITSOLVERGPU_H
#define ITSOLVERGPU_H

#include<stdio.h>
#include<stdlib.h>
#include<math.h>
#include<iostream>
#include"libsolv.h"
#include<cuda.h>
#include<cuda_runtime.h>
#include<cuda_runtime_api.h>

extern "C" {
#include "../camp_solver.h"
}

void createSolver(itsolver *bicg);
void CSRtoCSC(itsolver *bicg);
void CSCtoCSR(itsolver *bicg);
void solveGPU(itsolver *bicg, double *dA, int *djA, int *diA, double *dx, double *dtempv);
void solveGPU_block(itsolver *bicg, double *dA, int *djA, int *diA, double *dx, double *dtempv);
void free_itsolver(itsolver *bicg);

#endif