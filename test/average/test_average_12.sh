#!/bin/bash

# exit on error
set -e
# turn on command echoing
set -v
# make sure that the current directory is the one where this script is
cd ${0%/*}

../../extract_aero_particles -o out/average_sizevol_particles_unsorted.txt out/average_sizevol_0001_00000001.nc
sort out/average_sizevol_particles_unsorted.txt > out/average_sizevol_particles.txt
../../numeric_diff out/average_particles.txt out/average_sizevol_particles.txt 0 0.1 0 0 3 3
