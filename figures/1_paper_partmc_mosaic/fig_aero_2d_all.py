#!/usr/bin/env python
# Copyright (C) 2007-2008 Matthew West
# Licensed under the GNU General Public License version 2 or (at your
# option) any later version. See the file COPYING for details.

import os, sys, math
import copy as module_copy
from Scientific.IO.NetCDF import *
from pyx import *
sys.path.append(".")
from fig_helper import *
sys.path.append("../tool")
from camp_data_nc import *
from camp_pyx import *

out_prefix = "figs/aero_2d_all"

y_axis_label = r"BC dry mass frac. $w_{{\rm BC},{\rm dry}}\ (\%)$"

def get_plot_data(filename, value_max = None):
    ncf = NetCDFFile(filename)
    particles = aero_particle_array_t(ncf)
    env_state = env_state_t(ncf)
    ncf.close()

    diameter = particles.dry_diameter() * 1e6
    comp_frac = particles.mass(include = ["BC"]) \
                / particles.mass(exclude = ["H2O"]) * 100

    x_axis = camp_log_axis(min = diameter_axis_min, max = diameter_axis_max,
                          n_bin = num_diameter_bins)
    y_axis = camp_linear_axis(min = bc_axis_min, max = bc_axis_max,
                             n_bin = num_bc_bins)
    x_bin = x_axis.find(diameter)
    # hack to avoid landing just around the integer boundaries
    comp_frac *= (1.0 + 1e-12)
    y_bin = y_axis.find(comp_frac)

    num_den_array = numpy.zeros([x_axis.n_bin, y_axis.n_bin])
    show_coords = [[] for p in show_particles]
    for i in range(particles.n_particles):
        if x_axis.valid_bin(x_bin[i]) and y_axis.valid_bin(y_bin[i]):
            scale = particles.comp_vol[i] * x_axis.grid_size(x_bin[i]) \
                    * (y_axis.grid_size(y_bin[i]) / 100)
            num_den_array[x_bin[i], y_bin[i]] += 1.0 / scale
            for j in range(len(show_particles)):
                if int(particles.id[i]) == show_particles[j]["id"]:
                    show_coords[j] = [diameter[i], comp_frac[i]]

    value = num_den_array / num_den_array.sum() \
            / x_axis.grid_size(0) / (y_axis.grid_size(0) / 100.0)
    if value_max == None:
        value_max = value.max()
    if value_max > 0.0:
        value = value / value_max
    value = value.clip(0.0, 1.0)

    rects = camp_histogram_2d_multi([value],
                                    x_axis, y_axis)
    return (rects, show_coords, env_state)

time_filename_list = get_time_filename_list(netcdf_dir_wc, netcdf_pattern_wc)
for color in [True, False]:
    graphs = make_2x2_graph_grid(y_axis_label)
    for (graph_name, time_hour) in times_hour.iteritems():
        time = time_hour * 3600.0
        filename = file_filename_at_time(time_filename_list, time)
        (rects, show_coords, env_state) = get_plot_data(filename, max_val)
        g = graphs[graph_name]
        if color:
            palette = rainbow_palette
        else:
            palette = gray_palette
        g.plot(graph.data.points(rects,
                                 xmin = 1, xmax = 2, ymin = 3, ymax = 4,
                                 color = 5),
               styles = [hsb_rect(palette)])

        write_time(g, env_state)

        g.dolayout()
        for axisname in ["x", "y"]:
            for t in g.axes[axisname].data.ticks:
                if t.ticklevel is not None:
                    g.stroke(g.axes[axisname].positioner.vgridpath(t.temp_v),
                             [style.linestyle.dotted])
        g.dodata()
        g.doaxes()

        for i in range(len(show_particles)):
            if len(show_coords[i]) > 0:
                label_point(g, show_coords[i][0], show_coords[i][1],
                            show_particles[i]["label pos"][0],
                            show_particles[i]["label pos"][1],
                            show_particles[i]["label"])

    c = graphs["c"]
    add_canvas_color_bar(c,
                         min = 0.0,
                         max = max_val,
                         xpos = graphs["g22"].xpos + graphs["g22"].width + grid_h_space,
                         ybottom = graphs["g22"].ypos,
                         ytop = graphs["g12"].ypos + graphs["g12"].height,
                         title = r"normalized number conc. $\hat{n}_{\rm BC,dry}(D,w)$",
                         palette = palette)

    if color:
        out_filename = "%s_color.pdf" % out_prefix
    else:
        out_filename = "%s_bw.pdf" % out_prefix
    c.writePDFfile(out_filename)
    if not color:
        print "figure height = %.1f cm" % unit.tocm(c.bbox().height())
        print "figure width = %.1f cm" % unit.tocm(c.bbox().width())
