#!/bin/bash

# exit on error
set -e
# turn on command echoing
set -v
# make sure that the current directory is the one where this script is
cd ${0%/*}

mpirun -v -np 4 ../../partmc run_part_parallel_dist_single.spec
../../extract_aero_size --num --dmin 1e-10 --dmax 1e-4 --nbin 220 out/parallel_dist_single_0001
../../extract_aero_size --mass --dmin 1e-10 --dmax 1e-4 --nbin 220 out/parallel_dist_single_0001
../../extract_aero_total out/parallel_dist_single_0001_ out/parallel_dist_single_aero_total.txt

../../numeric_diff out/sect_aero_size_num.txt out/parallel_dist_single_0001_size_num.txt 0 0.3 0 0 2 0
../../numeric_diff out/sect_aero_size_mass.txt out/parallel_dist_single_0001_size_mass.txt 0 0.3 0 0 2 0
../../numeric_diff out/sect_aero_total.txt out/parallel_dist_single_aero_total.txt 0 0.3 0 0 2 2
../../numeric_diff out/sect_aero_total.txt out/parallel_dist_single_aero_total.txt 0 0.3 0 0 3 3
