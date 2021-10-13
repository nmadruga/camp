#!/usr/bin/env python

import scipy.io
import sys
import numpy as np
import matplotlib
matplotlib.use("PDF")
import matplotlib.pyplot as plt
sys.path.append("../../tool")
import camp
import config

i_loop_max = config.i_loop_max
i_ens_max = config.i_ens_max

x_axis = camp.log_grid(min=1e-10,max=1e-4,n_bin=100)
x_centers = x_axis.centers()

netcdf_dir = "/home/nriemer/subversion/camp/branches/nriemer/local_scenarios/brownian_test_paper/out/"

array_num = np.zeros([i_loop_max*i_ens_max, len(x_centers)])
array_mass = np.zeros([i_loop_max*i_ens_max, len(x_centers)])

for i_loop in range (0, i_ens_max*i_loop_max):
    filename = "brownian_part_%05d_00000002.nc"  % (i_loop+1)
    print filename
    ncf = scipy.io.netcdf.netcdf_file(netcdf_dir+filename, 'r')
    particles = camp.aero_particle_array_t(ncf)
    env_state = camp.env_state_t(ncf)
    ncf.close()

    wet_diameters = particles.diameters()
    hist = camp.histogram_1d(wet_diameters, x_axis, weights = 1 / particles.comp_vols)

    array_num[i_loop,:]= hist

    hist = camp.histogram_1d(wet_diameters, x_axis, weights = particles.masses() / particles.comp_vols)
    array_mass[i_loop,:]= hist

f1 = "data/ensemble_size_dist_num_brownian_10p.txt"
f2 = "data/ensemble_size_dist_mass_brownian_10p.txt"

np.savetxt(f1, array_num)
np.savetxt(f2, array_mass)



    



