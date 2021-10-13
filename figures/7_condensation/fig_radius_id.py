#!/usr/bin/env python

import scipy.io
import sys
import numpy as np
import matplotlib
matplotlib.use("PDF")
import matplotlib.pyplot as plt
sys.path.append("../../tool")
import camp

def check_num(in_dir, in_filename, in_file_pattern, out_filename, counter):
    time_filename_list = camp.get_time_filename_list(in_dir, in_file_pattern)

    id_p_array = np.array([16388, 10, 33311, 9212, 451, 11769])
    d = np.zeros((len(id_p_array),len(time_filename_list)))
    seconds = np.zeros(len(time_filename_list))
    i_count = 0

    for [time, filename, key] in time_filename_list:
	print time, filename, key
        ncf = scipy.io.netcdf.netcdf_file(filename, 'r')
        particles = camp.aero_particle_array_t(ncf) 
        ncf.close()
        wet_diameters = particles.diameters()
        id_list = list(particles.ids)
        for i in range(0,6):
             i_index = id_list.index(id_p_array[i])
             
             d[i,i_count] = wet_diameters[i_index]
        seconds[i_count] = i_count
        i_count = i_count + 1

    plt.figure()
    plt.semilogy(seconds, d[0,:], 'b-', label = 'act1_2')
    plt.semilogy(seconds, d[1,:], 'g-', label = 'not_1_2')
    plt.semilogy(seconds, d[2,:], 'r-', label = 'act2_not_act1')
    plt.semilogy(seconds, d[3,:], 'b-', label = 'act1_2')
    plt.semilogy(seconds, d[4,:], 'g-', label = 'not_1_2')
    plt.semilogy(seconds, d[5,:], 'r-', label = 'act2_not_act1')
    plt.xlabel("time (s)")
    plt.ylabel("diameter (m)")
    plt.legend(loc = 'upper left')
    fig = plt.gcf()
    fig.savefig(out_filename)

for counter in range(5,6):

    in_dir = "../../scenarios/3_condense/out/"
    filename_in = "cond_%02d_ref_0001_00000601.nc" % counter
    in_file_pattern = "cond_%02d_ref_0001_(.*).nc" % counter
    out_filename = "figs/radius_id_%02d.pdf" % counter
    check_num(in_dir, filename_in, in_file_pattern, out_filename, counter)
