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

def make_plot(dir_name,in_files,out_filename1, out_filename2):
    x_axis = camp.log_grid(min=1e-9,max=1e-5,n_bin=70)
    y_axis = camp.linear_grid(min=0,max=1.,n_bin=50)
    x_centers = x_axis.centers()
    y_centers = y_axis.centers()
    counter = 0
    hist_array = np.zeros([len(x_centers), len(y_centers), config.i_loop_max])
    hist_average = np.zeros([len(x_centers), len(y_centers)])
    hist_std = np.zeros([len(x_centers), len(y_centers)])
    hist_std_norm = np.zeros([len(x_centers), len(y_centers)])

    for file in in_files:
        ncf = scipy.io.netcdf.netcdf_file(dir_name+file, 'r')
        particles = camp.aero_particle_array_t(ncf)
        ncf.close()

        bc = particles.masses(include = ["BC"])
        dry_mass = particles.masses(exclude = ["H2O"])
        bc_frac = bc / dry_mass

        dry_diameters = particles.dry_diameters()

        hist2d = camp.histogram_2d(dry_diameters, bc_frac, x_axis, y_axis, weights = particles.masses(include = ["BC"])/particles.comp_vols)
        hist_array[:,:,counter] = hist2d
        counter = counter + 1

    hist_average = np.average(hist_array, axis = 2)
    hist_std = np.std(hist_array, axis = 2)
    hist_std_norm = hist_std/hist_average
    hist_std_norm = np.ma.masked_invalid(hist_std_norm)

    print 'hist_std ', hist_average[35,:], hist_std[35,:], hist_std_norm[35,:]

    plt.clf()
    plt.pcolor(x_axis.edges(), y_axis.edges(), hist_average.transpose(), norm = matplotlib.colors.LogNorm(), linewidths = 0.1)
    a = plt.gca()
    a.set_xscale("log")
    a.set_yscale("linear")
    plt.axis([5e-9, 5e-6, 0, 0.8])
    plt.xlabel("dry diameter (m)")
    plt.ylabel("BC mass fraction")
    plt.clim(1.e-10, 1.e-7)
    plt.grid()
    cbar = plt.colorbar()
    cbar.set_label("BC mass concentration (kg m^{-3})")
    fig = plt.gcf()
    fig.savefig(out_filename1)

    plt.clf()
    plt.pcolor(x_axis.edges(), y_axis.edges(), hist_std_norm.transpose(), 
	norm = matplotlib.colors.LogNorm(vmin=1e-1, vmax = 10), linewidths = 0.1)
    a = plt.gca()
    a.set_xscale("log")
    a.set_yscale("linear")
    plt.axis([5e-9, 5e-6, 0, 0.8])
    plt.xlabel("dry diameter (m)")
    plt.ylabel("BC mass fraction")
    plt.grid()
    cbar = plt.colorbar()
    cbar.set_label("CV")
    fig = plt.gcf()
    fig.savefig(out_filename2)

dir_name = "../../scenarios/5_weighted/out/"
for hour in range(12,13):
    print "hour = ", hour
#    for counter in ["10K_flat", "10K_wei-1", "10K_wei-2", "10K_wei-3", "10K_wei-4", "100K_flat", "100K_wei-1", "100K_wei-2", "100K_wei-3", "100K_wei-4"]:
    for counter in ["10K_wei+1", "10K_flat", "10K_wei-1", "10K_wei-2", "10K_wei-3", "10K_wei-4"]:
#    for counter in ["10K_wei+1"]:
        print 'counter ', counter
        files = []
        for i_loop in range(0,config.i_loop_max):
            filename_in = "urban_plume_wc_%s_0%03d_000000%02d.nc" % (counter,i_loop+1,hour)
            files.append(filename_in)
        filename_out1 = "figs/2d_bc_mass_%s_%02d.pdf" % (counter, hour)
        filename_out2 = "figs/2d_bc_mass_std_%s_%02d.pdf" % (counter, hour)
        make_plot(dir_name, files, filename_out1, filename_out2)

