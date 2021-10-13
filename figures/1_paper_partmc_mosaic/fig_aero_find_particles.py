#!/usr/bin/env python
# Copyright (C) 2007-2008 Matthew West
# Licensed under the GNU General Public License version 2 or (at your
# option) any later version. See the file COPYING for details.

import os, sys, math
import copy as module_copy
from Scientific.IO.NetCDF import *
from pyx import *
sys.path.append("../tool")
from camp_data_nc import *
from camp_pyx import *
import numpy
sys.path.append(".")
from fig_helper import *

early_time = 1 # hours elapsed
late_time = 24 # hours elapsed

time_filename_list = get_time_filename_list(netcdf_dir_wc, netcdf_pattern_wc)
early_filename = file_filename_at_time(time_filename_list, early_time * 3600)
late_filename = file_filename_at_time(time_filename_list, late_time * 3600)
early_ncf = NetCDFFile(early_filename)
late_ncf = NetCDFFile(late_filename)
early_particles = aero_particle_array_t(early_ncf)
late_particles = aero_particle_array_t(late_ncf)
early_ncf.close()
late_ncf.close()
early_diameter = early_particles.dry_diameter() * 1e6
late_diameter = late_particles.dry_diameter() * 1e6
early_comp_frac = early_particles.mass(include = ["BC"]) \
                  / early_particles.mass(exclude = ["H2O"]) * 100
late_comp_frac = late_particles.mass(include = ["BC"]) \
                 / late_particles.mass(exclude = ["H2O"]) * 100
early_water_frac = early_particles.mass(include = ["H2O"]) \
                   / early_particles.mass() * 100
late_water_frac = late_particles.mass(include = ["H2O"]) \
                  / late_particles.mass() * 100
early_oc_frac = early_particles.mass(include = ["BC"]) \
                / early_particles.mass(include = ["BC", "OC"]) * 100
late_oc_frac = late_particles.mass(include = ["BC"]) \
               / late_particles.mass(include = ["BC", "OC"]) * 100
early_id = array([int(id) for id in early_particles.id])
late_id = array([int(id) for id in late_particles.id])
early_found = {}
for i in range(early_particles.n_particles):
    if early_id[i] in late_id:
        type = ""
        if early_comp_frac[i] > 50:
            if early_water_frac[i] == 0.0:
                type = "dry diesel"
            else:
                type = "wet diesel"
        elif early_comp_frac[i] > 10:
            if early_water_frac[i] == 0.0:
                type = "dry gasoline"
            else:
                type = "wet gasoline"
        if type != "":
            if type not in early_found.keys():
                early_found[type] = []
            early_found[type].append(i)
late_found = {}
for i in range(late_particles.n_particles):
    type = ""
    if late_comp_frac[i] > 64:
        type = "late diesel"
    if type != "":
        if type not in late_found.keys():
            late_found[type] = []
        late_found[type].append(i)
for type in early_found.keys():
    early_found[type].sort(cmp = lambda x,y : cmp(early_id[x],
                                                  early_id[y]))
    for i in early_found[type]:
        for j in range(len(late_id)):
            if early_id[i] == late_id[j]:
                late_i = j
        n_coag = int(late_particles.n_orig_part[late_i]) - 1
        if n_coag == 0:
            print "%s: id = %d, d0 = %f, bc0 = %g, water0 = %f, d1 = %f, bc1 = %g, water1 = %f, nco = %d" \
                % (type, early_id[i], early_diameter[i],
                   early_comp_frac[i], early_water_frac[i], late_diameter[late_i],
                   late_comp_frac[late_i], late_water_frac[late_i], n_coag)
for type in late_found.keys():
    late_found[type].sort(cmp = lambda x,y : cmp(late_id[x],
                                                 late_id[y]))
    for i in late_found[type]:
        if late_diameter[i] < 0.1:
            n_coag = int(late_particles.n_orig_part[i]) - 1
            if n_coag == 0:
                print "%s: id = %d, d = %f, bc = %g, water = %f, emit_time = %f, nco = %d" \
                    % (type, late_id[i], late_diameter[i],
                       late_comp_frac[i], late_water_frac[i],
                       late_particles.least_create_time[i], n_coag)
