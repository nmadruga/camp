#!/usr/bin/env python

import scipy.io
import sys
import numpy as np

sys.path.append("../../tool")
import camp
import matplotlib
import mpl_helper
import config
#matplotlib.use("PDF")
#import matplotlib.pyplot as plt


seconds = np.loadtxt("seconds.txt")
rh = np.loadtxt("rh.txt")

diam_not_activate = np.loadtxt("diam_not_activate.txt")
diam_evaporation = np.loadtxt("diam_evaporation.txt")
diam_deactivation = np.loadtxt("diam_deactivation.txt")
diam_inertial = np.loadtxt("diam_inertial.txt")
rh_eq_not_activate = np.loadtxt("rh_eq_not_activate.txt")
rh_eq_evaporation = np.loadtxt("rh_eq_evaporation.txt")
rh_eq_deactivation = np.loadtxt("rh_eq_deactivation.txt")
rh_eq_inertial = np.loadtxt("rh_eq_inertial.txt")
     
rh_c_not_activate = np.loadtxt("rh_c_not_activate.txt")
rh_c_evaporation =  np.loadtxt("rh_c_evaporation.txt")
rh_c_deactivation = np.loadtxt("rh_c_deactivation.txt")
rh_c_inertial =  np.loadtxt("rh_c_inertial.txt")
d_c_not_activate = np.loadtxt("d_c_not_activate.txt")
d_c_evaporation = np.loadtxt("d_c_evaporation.txt")
d_c_deactivation = np.loadtxt("d_c_deactivation.txt")
d_c_inertial = np.loadtxt("d_c_inertial.txt")

wet_diameter_not_activate = np.loadtxt("wet_diameters_not_activate.txt")
wet_diameter_evaporation = np.loadtxt("wet_diameters_evaporation.txt")
wet_diameter_deactivation =  np.loadtxt("wet_diameters_deactivation.txt")
wet_diameter_inertial = np.loadtxt("wet_diameters_inertial.txt")

equilibrium_rhs_not_activate_grid_080 = np.loadtxt("equilib_rhs_not_activate_grid_080.txt")
equilibrium_rhs_evaporation_grid_080  = np.loadtxt("equilib_rhs_evaporation_grid_080.txt")
equilibrium_rhs_deactivation_grid_080 =  np.loadtxt("equilib_rhs_deactivation_grid_080.txt")
equilibrium_rhs_inertial_grid_080 = np.loadtxt("equilib_rhs_inertial_grid_080.txt")

equilibrium_rhs_not_activate_grid_100 = np.loadtxt("equilib_rhs_not_activate_grid_100.txt")
equilibrium_rhs_evaporation_grid_100  = np.loadtxt("equilib_rhs_evaporation_grid_100.txt")
equilibrium_rhs_deactivation_grid_100 =  np.loadtxt("equilib_rhs_deactivation_grid_100.txt")
equilibrium_rhs_inertial_grid_100 = np.loadtxt("equilib_rhs_inertial_grid_100.txt")

equilibrium_rhs_not_activate_grid_130 = np.loadtxt("equilib_rhs_not_activate_grid_130.txt")
equilibrium_rhs_evaporation_grid_130  = np.loadtxt("equilib_rhs_evaporation_grid_130.txt")
equilibrium_rhs_deactivation_grid_130 =  np.loadtxt("equilib_rhs_deactivation_grid_130.txt")
equilibrium_rhs_inertial_grid_130 = np.loadtxt("equilib_rhs_inertial_grid_130.txt")

equilibrium_rhs_not_activate_grid_150 = np.loadtxt("equilib_rhs_not_activate_grid_150.txt")
equilibrium_rhs_evaporation_grid_150  = np.loadtxt("equilib_rhs_evaporation_grid_150.txt")
equilibrium_rhs_deactivation_grid_150 =  np.loadtxt("equilib_rhs_deactivation_grid_150.txt")
equilibrium_rhs_inertial_grid_150 = np.loadtxt("equilib_rhs_inertial_grid_150.txt")

(figure, axes) = mpl_helper.make_fig()
l1 = axes.plot(seconds, diam_not_activate)
l2 = axes.plot(seconds, diam_evaporation)
l3 = axes.plot(seconds, diam_deactivation)
l4 = axes.plot(seconds, diam_inertial)
axes.grid(True)
axes.set_yscale("log")
axes.set_xlabel(r"time in seconds ")
axes.set_ylabel(r"wet diameter")
axes.legend((l1, l2, l3, l4), ("not activate", "evaporation", "deactivation", "inertial"))
figure.savefig("figs/diameters.pdf")

(figure, axes) = mpl_helper.make_fig()
axes.plot(seconds, rh_eq_not_activate)
axes.plot(seconds, rh_eq_evaporation)
axes.plot(seconds, rh_eq_deactivation)
axes.plot(seconds, rh_eq_inertial)
axes.grid(True)
axes.set_xlabel(r"time in seconds ")
axes.set_ylabel(r"equilib rh of particle")
figure.savefig("figs/rh_eqs.pdf")

(figure, axes) = mpl_helper.make_fig()
l1=axes.plot(diam_not_activate*1e6, rh-rh_eq_not_activate)
l2=axes.plot(diam_evaporation*1e6, rh-rh_eq_evaporation)
l3=axes.plot(diam_deactivation*1e6, rh-rh_eq_deactivation)
#axes.plot(diam_inertial*1e6, rh_eq_inertial)
axes.grid(True)
axes.set_xscale("log")
axes.set_xlabel(r"diameter ")
axes.set_ylabel(r"equilib rh of particle")
axes.legend((l1, l2, l3), ("not activate", "evaporation", "deactivation"), loc='center left')
figure.savefig("figs/d_rh_not_activate.pdf")


##### COOL FIGURES START HERE #####

(figure, axes) = mpl_helper.make_fig()
l1=axes.plot(diam_not_activate*1e6, rh)
l2=axes.plot(wet_diameter_not_activate*1e6, equilibrium_rhs_not_activate_grid_080)
l3=axes.plot(wet_diameter_not_activate*1e6, equilibrium_rhs_not_activate_grid_100)
l4=axes.plot(wet_diameter_not_activate*1e6, equilibrium_rhs_not_activate_grid_130)
l5=axes.plot(wet_diameter_not_activate*1e6, equilibrium_rhs_not_activate_grid_150)
axes.grid(True)
axes.set_xscale("log")
axes.set_xlabel(r"diameter / $\rm \mu m$")
axes.set_ylabel(r"rh")
axes.set_xlim(0.1, 10)
axes.set_ylim(1, 1.003)
axes.legend((l1, l2, l3, l4, l5), ("env rh", "eq rh 080", "eq rh 100", "eq rh 130", "eq rh 150"), loc='center left')
figure.savefig("figs/rh_versus_d_not_activate.pdf")

(figure, axes) = mpl_helper.make_fig()
l1=axes.plot(diam_evaporation*1e6, rh)
l2=axes.plot(wet_diameter_evaporation*1e6, equilibrium_rhs_evaporation_grid_080)
l3=axes.plot(wet_diameter_evaporation*1e6, equilibrium_rhs_evaporation_grid_100)
l4=axes.plot(wet_diameter_evaporation*1e6, equilibrium_rhs_evaporation_grid_130)
l5=axes.plot(wet_diameter_evaporation*1e6, equilibrium_rhs_evaporation_grid_150)
axes.grid(True)
axes.set_xscale("log")
axes.set_xlabel(r"diameter / $\rm \mu m$")
axes.set_ylabel(r"rh")
axes.set_xlim(0.1, 10)
axes.set_ylim(1, 1.003)
axes.legend((l1, l2, l3, l4, l5), ("env rh", "eq rh 080", "eq rh 100", "eq rh 130", "eq rh 150"), loc='center right')
figure.savefig("figs/rh_versus_d_evaporation.pdf")

(figure, axes) = mpl_helper.make_fig()
l1=axes.plot(diam_deactivation*1e6, rh)
l2=axes.plot(wet_diameter_deactivation*1e6, equilibrium_rhs_deactivation_grid_080)
l3=axes.plot(wet_diameter_deactivation*1e6, equilibrium_rhs_deactivation_grid_100)
l4=axes.plot(wet_diameter_deactivation*1e6, equilibrium_rhs_deactivation_grid_130)
l5=axes.plot(wet_diameter_deactivation*1e6, equilibrium_rhs_deactivation_grid_150)
axes.grid(True)
axes.set_xscale("log")
axes.set_xlabel(r"diameter / $\rm \mu m$")
axes.set_ylabel(r"rh")
axes.set_xlim(0.1, 10)
axes.set_ylim(1, 1.003)
axes.legend((l1, l2, l3, l4, l5), ("env rh", "eq rh 080", "eq rh 100", "eq rh 130", "eq rh 150"), loc='center left')
figure.savefig("figs/rh_versus_d_deactivation.pdf")

(figure, axes) = mpl_helper.make_fig()
l1=axes.plot(diam_inertial*1e6, rh)
l2=axes.plot(wet_diameter_inertial*1e6, equilibrium_rhs_inertial_grid_080)
l3=axes.plot(wet_diameter_inertial*1e6, equilibrium_rhs_inertial_grid_100)
l4=axes.plot(wet_diameter_inertial*1e6, equilibrium_rhs_inertial_grid_130)
l5=axes.plot(wet_diameter_inertial*1e6, equilibrium_rhs_inertial_grid_150)
axes.grid(True)
axes.set_xscale("log")
axes.set_xlabel(r"diameter / $\rm \mu m$")
axes.set_ylabel(r"rh")
axes.set_xlim(1, 100)
axes.set_ylim(1, 1.003)
axes.legend((l1, l2, l3, l4, l5), ("env rh", "eq rh 080", "eq rh 100", "eq rh 130", "eq rh 150"), loc='center left')
figure.savefig("figs/rh_versus_d_inertial.pdf")


(figure, axes_array) = mpl_helper.make_fig_array(2,1, share_x_axes=True)
axes = axes_array[0][0]
l1=axes.plot(seconds, diam_not_activate*1e6)
l2=axes.plot(seconds, d_c_not_activate*1e6)
axes.grid(True)
axes.set_xlabel(r"time / s")
axes.set_ylabel(r"diameter / $\rm \mu m$")
axes.legend((l1, l2), ("wet diameter", "critical diameter"), loc='center right')

axes = axes_array[1][0]
l1=axes.plot(seconds, rh)
l2=axes.plot(seconds, rh_c_not_activate)
l3=axes.plot(seconds, rh_eq_not_activate)
axes.grid(True)
axes.set_ylim(1, 1.005)
axes.set_ylabel(r"rh")
axes.legend((l1, l2), ("env", "critical", "eq"), loc='center right')
mpl_helper.remove_fig_array_axes(axes_array, remove_y_axes=False)
figure.savefig("figs/rhs_diams_not_activate.pdf")

(figure, axes_array) = mpl_helper.make_fig_array(2,1, share_x_axes=True)
axes = axes_array[0][0]
l1=axes.plot(seconds, diam_evaporation*1e6)
l2=axes.plot(seconds, d_c_evaporation*1e6)
axes.grid(True)
axes.set_xlabel(r"time / s")
axes.set_ylabel(r"diameter / $\rm \mu m$")
axes.legend((l1, l2), ("wet diameter", "critical diameter"), loc='center right')

axes = axes_array[1][0]
l1=axes.plot(seconds, rh)
l2=axes.plot(seconds, rh_c_evaporation)
l3=axes.plot(seconds, rh_eq_evaporation)
axes.grid(True)
axes.set_ylabel(r"rh")
axes.set_ylim(1, 1.005)
axes.legend((l1, l2, l3), ("env", "critical", "eq"), loc='center right')
mpl_helper.remove_fig_array_axes(axes_array, remove_y_axes=False)
figure.savefig("figs/rhs_diams_evaporation.pdf")

(figure, axes_array) = mpl_helper.make_fig_array(2,1, share_x_axes=True)
axes = axes_array[0][0]
l1=axes.plot(seconds, diam_deactivation*1e6)
l2=axes.plot(seconds, d_c_deactivation*1e6)
axes.grid(True)
axes.set_xlabel(r"time / s")
axes.set_ylabel(r"diameter / $\rm \mu m$")
axes.legend((l1, l2), ("wet diameter", "critical diameter"), loc='center right')

axes = axes_array[1][0]
l1=axes.plot(seconds, rh)
l2=axes.plot(seconds, rh_c_deactivation)
l3=axes.plot(seconds, rh_eq_deactivation)
axes.grid(True)
axes.set_ylabel(r"rh")
axes.set_ylim(1, 1.005)
axes.legend((l1, l2, l3), ("env", "critical", "eq"), loc='center right')
mpl_helper.remove_fig_array_axes(axes_array, remove_y_axes=False)
figure.savefig("figs/rhs_diams_deactivation.pdf")

(figure, axes_array) = mpl_helper.make_fig_array(2,1, share_x_axes=True)
axes = axes_array[0][0]
l1=axes.plot(seconds, diam_inertial*1e6)
l2=axes.plot(seconds, d_c_inertial*1e6)
axes.grid(True)
axes.set_xlabel(r"time / s")
axes.set_ylabel(r"diameter / $\rm \mu m$")
axes.legend((l1, l2), ("wet diameter", "critical diameter"), loc='center right')

axes = axes_array[1][0]
l1=axes.plot(seconds, rh)
l2=axes.plot(seconds, rh_c_inertial)
l3=axes.plot(seconds, rh_eq_inertial)
axes.grid(True)
axes.set_ylabel(r"rh")
axes.set_ylim(1, 1.005)
axes.legend((l1, l2, l3), ("env", "critical", "eq"), loc='center right')
mpl_helper.remove_fig_array_axes(axes_array, remove_y_axes=False)
figure.savefig("figs/rhs_diams_inertial.pdf")
