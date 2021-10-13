#!/usr/bin/env python

import Scientific.IO.NetCDF
import sys
import numpy as np
import matplotlib
matplotlib.use("PDF")
import matplotlib.pyplot as plt
sys.path.append("../../tool")
import camp
import config

def make_plot(dir_name,in_files,out_filename1, out_filename2):
    x_axis = camp.log_grid(min=1e-9,max=1e-5,n_bin=70)
    y_axis = camp.log_grid(min=1e-3,max=1e2,n_bin=50)
    x_centers = x_axis.centers()
    y_centers = y_axis.centers()
    counter = 0
    hist_array = np.zeros([len(x_centers), len(y_centers), config.i_loop_max])
    hist_average = np.zeros([len(x_centers), len(y_centers)])
    hist_std = np.zeros([len(x_centers), len(y_centers)])
    hist_std_norm = np.zeros([len(x_centers), len(y_centers)])

    for file in in_files:
        ncf = Scientific.IO.NetCDF.NetCDFFile(dir_name+file)
        particles = camp.aero_particle_array_t(ncf)
        env_state = camp.env_state_t(ncf)
        ncf.close()

        dry_diameters = particles.dry_diameters()
        s_crit = (particles.critical_rel_humids(env_state) - 1)*100
        hist2d = camp.histogram_2d(dry_diameters, s_crit, x_axis, y_axis, weights = particles.masses(include = ["BC"])/particles.comp_vols)
        hist_array[:,:,counter] = hist2d
        counter = counter + 1

    hist_average = np.average(hist_array, axis = 2)
    hist_std = np.std(hist_array, axis = 2)
    hist_std_norm = hist_std/hist_average
    hist_std_norm = np.ma.masked_invalid(hist_std_norm)

    print 'hist_array ', hist2d.shape, hist_array[35,:,0] 
    print 'hist_std ', hist_average[35,:], hist_std[35,:], hist_std_norm[35,:]
    
    plt.clf()
    plt.pcolor(x_axis.edges(), y_axis.edges(), hist_average.transpose(), norm = matplotlib.colors.LogNorm(vmin=1e-11, vmax=1e-7), linewidths = 0.1)
    a = plt.gca()
    a.set_xscale("log")
    a.set_yscale("log")
    plt.grid()
    plt.axis([5e-9, 5e-6, y_axis.min, y_axis.max])
    plt.xlabel("dry diameter (m)")
    plt.ylabel("S_crit in %")
    cbar = plt.colorbar()
    cbar.set_label("mass density (kg m^{-3})")
    fig = plt.gcf()
    fig.savefig(out_filename1)

    plt.clf()
    plt.pcolor(x_axis.edges(), y_axis.edges(), hist_std_norm.transpose(), norm = matplotlib.colors.LogNorm(vmin=1e-2, vmax = 10), linewidths = 0.1)
    a = plt.gca()
    a.set_xscale("log")
    a.set_yscale("log")
    plt.grid()
    plt.axis([5e-9, 5e-6, y_axis.min, y_axis.max])
    plt.xlabel("dry diameter (m)")
    plt.ylabel("S_crit in %")
    cbar = plt.colorbar()
    cbar.set_label("std/avg")
    fig = plt.gcf()
    fig.savefig(out_filename2)

dir_name = "../../scenarios/5_weighted/out/"
for hour in range(12,13):
    print "hour = ", hour
    for counter in ["10K_wei+1", "10K_flat", "10K_wei-1", "10K_wei-2", "10K_wei-3", "10K_wei-4"]:
#    for counter in ["1K_flat"]:
        print 'counter ', counter
        files = []
        for i_loop in range(0,config.i_loop_max):
            filename_in = "urban_plume_wc_%s_0%03d_000000%02d.nc" % (counter,i_loop+1,hour)
            files.append(filename_in)
        filename_out1 = "figs/2d_scrit_mass_%s_%02d.pdf" % (counter, hour)
        filename_out2 = "figs/2d_scrit_std_mass_%s_%02d.pdf" % (counter, hour)
        make_plot(dir_name, files, filename_out1, filename_out2)

