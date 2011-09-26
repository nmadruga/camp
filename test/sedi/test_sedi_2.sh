#!/bin/bash

# exit on error
set -e
# turn on command echoing
set -v
# make sure that the current directory is the one where this script is
cd ${0%/*}

../../extract_aero_size --mass --dmin 1e-8 --dmax 1e-2 --nbin 100 out/sedi_part_0001
../../extract_sectional_aero_size_mass out/sedi_sect_ out/sedi_sect_size_mass.txt

../../numeric_diff out/sedi_part_0001_size_mass.txt out/sedi_sect_size_mass.txt 0 0.8 0 0 2 0
