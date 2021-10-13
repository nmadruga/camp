#!/usr/bin/env python

import scipy.io
import sys
import numpy as np
import matplotlib
matplotlib.use("PDF")
matplotlib.use('Agg')
import matplotlib.pyplot as plt
sys.path.append("../../tool")
import camp

def make_plot(in_filename,out_filename,title):
    ncf = scipy.io.netcdf.netcdf_file(in_filename, 'r')
    particles = camp.aero_particle_array_t(ncf)
    ncf.close()

    so4 = particles.masses(include = ["SO4"])/particles.aero_data.molec_weights[0]
    nh4 =  particles.masses(include = ["NH4"])/particles.aero_data.molec_weights[3]
    no3 =  particles.masses(include = ["NO3"])/particles.aero_data.molec_weights[1]
    bc =  particles.masses(include = ["BC"])/particles.aero_data.molec_weights[18]
    oc =  particles.masses(include = ["OC"])/particles.aero_data.molec_weights[17]

    print 'min nh4 ', min(particles.masses(include = ["NH4"])), max(nh4), min(no3), max(no3)

    ion_ratio = (2*so4 + no3) / nh4

    is_neutral = (ion_ratio < 2)
    dry_diameters = particles.dry_diameters()

    x_axis = camp.log_grid(min=1e-8,max=1e-6,n_bin=70)
    y_axis = camp.linear_grid(min=0,max=30.0,n_bin=100)
    x_centers = x_axis.centers()

    bin_so4 = camp.histogram_1d(dry_diameters, x_axis, weights = so4)
    bin_nh4 = camp.histogram_1d(dry_diameters, x_axis, weights = nh4)
    bin_no3 = camp.histogram_1d(dry_diameters, x_axis, weights = no3)
    
    print 'bin_so4 ', bin_so4[40]
    print 'bin_nh4 ', bin_nh4[40]
    print 'bin_no3 ', bin_no3[40]

    bin_ratio = (2*bin_so4 + bin_no3)/ bin_nh4
    np.isnan(bin_ratio) # checks which elements in c are NaN (produces array with True and False)
    bin_ratio[np.isnan(bin_ratio)] = 0 # replaces NaN with 0. useful for plotting
    print 'bin_ratio ', bin_ratio[40]

    diameter_bins = x_axis.find(dry_diameters)
    print 'diameter_bins ', diameter_bins
    is_40 = (diameter_bins == 40)
#    for i in range(len(dry_diameters)):
#        if diameter_bins[i] == 40:
#            print 'particle info', so4[i], nh4[i], no3[i], ion_ratio[i]
    so4_40 = so4[is_40]
    nh4_40 = nh4[is_40]
    no3_40 = no3[is_40]
    bc_40 = bc[is_40]
    oc_40 = oc[is_40]

    ion_ratio_40 = ion_ratio[is_40]
#    data = [(so4_40[i],nh4_40[i], no3_40[i], ion_ratio_40[i]) for i in range(len(so4_40)
    data = zip(so4_40, nh4_40, no3_40, bc_40, oc_40, ion_ratio_40)
    data.sort(key = lambda x: x[5])
    for (so,nh,no,bc,oc,ir) in data:
        print so,nh,no,bc,oc,ir

    print 'sums ', sum(so4[is_40]), sum(nh4[is_40]), sum(no3[is_40]), (2*sum(so4[is_40])+ sum(no3[is_40])) / sum(nh4[is_40])
    print 'sums/number ',  sum(so4[is_40])/len(so4_40), sum(nh4[is_40])/len(nh4_40), sum(no3[is_40])/len(no3_40)
    
    
    hist2d = camp.histogram_2d(dry_diameters, ion_ratio, x_axis, y_axis, weights = 1/particles.comp_vols)

    plt.clf()
    plt.pcolor(x_axis.edges(), y_axis.edges(), hist2d.transpose(),norm = matplotlib.colors.LogNorm(), linewidths = 0.1)
    a = plt.gca()
    plt.semilogx(x_centers, bin_ratio, 'w-', linewidth = 3)
    plt.semilogx(x_centers, bin_ratio, 'k-', linewidth = 1)
    a.set_xscale("log")
    a.set_yscale("linear")
    plt.axis([x_axis.min, x_axis.max, y_axis.min, y_axis.max])
    plt.xlabel("dry diameter (m)")
    plt.ylabel("ion ratio")
    cbar = plt.colorbar()
    cbar.set_label("number density (m^{-3})")
    plt.title(title)
    fig = plt.gcf()
    fig.savefig(out_filename)

for hour in range(1, 14):
    print "hour = ", hour
    
    filename_in1 = "../../scenarios/2_urban_plume2/out/urban_plume2_wc_0001_000000%02d.nc" % hour
    filename_out1 = "figs/2d_neutral_wc_%02d.png" % (hour-1)
    titel = "%02d hours" % (hour-1)
    print filename_in1
    print filename_out1
    print titel

    make_plot(filename_in1, filename_out1, titel)


