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
sys.path.append(".")
from camp_pyx import *

from fig_helper import *

time_hour = 24

y_axis_label = r"BC-POA mass frac. $w_{{\rm BC},{\rm POA}}$ ($\%$)"
out_prefix = "figs/aero_2d_oc"

def get_plot_data(filename, value_max = None):
    ncf = NetCDFFile(filename)
    particles = aero_particle_array_t(ncf)
    env_state = env_state_t(ncf)
    ncf.close()

    diameter = particles.dry_diameter() * 1e6
    comp_frac = particles.mass(include = ["BC"]) \
                / particles.mass(include = ["BC", "OC"]) * 100

    x_axis = camp_log_axis(min = diameter_axis_min, max = diameter_axis_max,
                          n_bin = num_diameter_bins)
    y_axis = camp_linear_axis(min = bc_axis_min, max = bc_axis_max,
                             n_bin = num_bc_bins)
    x_bin = x_axis.find(diameter)
    # hack to avoid landing just around the integer boundaries
    comp_frac *= (1.0 + 1e-12)
    y_bin = y_axis.find(comp_frac)

    num_den_array = numpy.zeros([x_axis.n_bin, y_axis.n_bin])
    for i in range(particles.n_particles):
        if x_axis.valid_bin(x_bin[i]) and y_axis.valid_bin(y_bin[i]):
            scale = particles.comp_vol[i] * x_axis.grid_size(x_bin[i]) \
                * (y_axis.grid_size(y_bin[i]) / 100)
            num_den_array[x_bin[i], y_bin[i]] += 1.0 / scale

    value = num_den_array / num_den_array.sum() \
            / x_axis.grid_size(0) / (y_axis.grid_size(0) / 100.0)
    if value_max == None:
        value_max = value.max()
    if value_max > 0.0:
        value = value / value_max
    value = value.clip(0.0, 1.0)

    rects = camp_histogram_2d_multi([value],
                                    x_axis, y_axis)
    return (rects, env_state)

time_filename_list_wc = get_time_filename_list(netcdf_dir_wc, netcdf_pattern_wc)
time_filename_list_nc = get_time_filename_list(netcdf_dir_nc, netcdf_pattern_nc)
for color in [True, False]:
    graphs = make_2x1_graph_grid(y_axis_label)
    for with_coag in [True, False]:
        time = time_hour * 3600.0
        if with_coag:
            filename = file_filename_at_time(time_filename_list_wc, time)
            g = graphs["g11"]
            extra_text = "with coagulation"
        else:
            filename = file_filename_at_time(time_filename_list_nc, time)
            g = graphs["g21"]
            extra_text = "no coagulation"
        (rects, env_state) = get_plot_data(filename, max_val)
        if color:
            palette = rainbow_palette
        else:
            palette = gray_palette
        g.plot(graph.data.points(rects,
                                 xmin = 1, xmax = 2, ymin = 3, ymax = 4,
                                 color = 5),
               styles = [hsb_rect(palette)])

        write_time(g, env_state)
        boxed_text(g, extra_text, point = [1, 1], anchor_point_rel = [1, 1])

        g.dolayout()
        for axisname in ["x", "y"]:
            for t in g.axes[axisname].data.ticks:
                if t.ticklevel is not None:
                    g.stroke(g.axes[axisname].positioner.vgridpath(t.temp_v),
                             [style.linestyle.dotted])
        g.dodata()
        g.doaxes()

    c = graphs["c"]
    add_canvas_color_bar(c,
                         min = 0.0,
                         max = max_val,
                         xpos = graphs["g21"].xpos + graphs["g21"].width + grid_h_space,
                         ybottom = graphs["g21"].ypos,
                         ytop = graphs["g21"].ypos + graphs["g21"].height,
                         bar_height_ratio = 0.9,
                         title = r"normalized number conc. $\hat{n}_{\rm BC,POA}(D,w)$",
                         palette = palette)

    if color:
        out_filename = "%s_color.pdf" % out_prefix
    else:
        out_filename = "%s_bw.pdf" % out_prefix
    c.writePDFfile(out_filename)
    if not color:
        print "figure height = %.1f cm" % unit.tocm(c.bbox().height())
        print "figure width = %.1f cm" % unit.tocm(c.bbox().width())
