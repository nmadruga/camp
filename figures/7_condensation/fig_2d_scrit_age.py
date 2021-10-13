#!/usr/bin/env python2.5

import scipy.io
import sys
import numpy as np
import matplotlib
matplotlib.use("PDF")
import matplotlib.pyplot as plt
sys.path.append("../../tool")
import camp

def make_plot(in_filename,out_filename,time,title):
    ncf = scipy.io.netcdf.netcdf_file(in_filename, 'r')
    particles = camp.aero_particle_array_t(ncf)
    env_state = camp.env_state_t(ncf)
    ncf.close()

    age = abs(particles.least_create_times / 3600. - time)
    dry_diameters = particles.dry_diameters()
    s_crit = (particles.critical_rel_humids(env_state) - 1)*100

    x_axis = camp.log_grid(min=1e-8,max=1e-6,n_bin=140)
    y_axis = camp.log_grid(min=1e-3,max=1e2,n_bin=100)

    vals2d = camp.multival_2d(dry_diameters, s_crit, age, x_axis, y_axis)

    plt.clf()
    plt.pcolor(x_axis.edges(), y_axis.edges(), vals2d.transpose(), linewidths = 0.1)
    a = plt.gca()
    a.set_xscale("log")
    a.set_yscale("log")
    plt.axis([x_axis.min, x_axis.max, y_axis.min, y_axis.max])
    plt.xlabel("dry diameter (m)")
    plt.ylabel("S_crit (%)")
    cbar = plt.colorbar()
    cbar.set_label("age (h)")
    plt.title(title)
    fig = plt.gcf()
    fig.savefig(out_filename)

for hour in range(49, 50):
    print "hour = ", hour
    time = hour - 1
    filename_in1 = "../../scenarios/2_urban_plume2/out/urban_plume_nc_0001_000000%02d.nc" % hour
    filename_out1 = "figs/2d_scrit_age_nc_%02d.pdf" % (hour-1)
    titel = "%02d hours" % (hour-1)
    print filename_in1
    print filename_out1
    print titel

    make_plot(filename_in1, filename_out1, time, titel)


