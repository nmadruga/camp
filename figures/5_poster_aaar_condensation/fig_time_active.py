#!/usr/bin/env python
# Copyright (C) 2007-2009 Matthew West
# Licensed under the GNU General Public License version 2 or (at your
# option) any later version. See the file COPYING for details.

import os, sys
import copy as module_copy
from Scientific.IO.NetCDF import *
from pyx import *
sys.path.append("../../tool")
from camp_data_nc import *
from camp_pyx import *
from config import *

out_filename_max_ss = "figs/time_max_ss.pdf"
out_filename_active = "figs/time_active.pdf"

particle_data = [
    [1,  0.2702290464, 26.3342],
    [7,  0.1948935877, 35.4721],
    [15, 0.1542968152, 35.884 ],
    [24, 0.1863218212, 50.2302],
    ]

size_avg_data = [
    [1,  0.2679829668, 26.6288],
    [7,  0.1916430448, 34.8996],
    [15, 0.1515491835, 32.2625],
    [24, 0.1843106541, 51.9073],
    ]

comp_avg_data = [
    [1,  0.2696551705, 28.7525],
    [7,  0.193847129 , 35.5539],
    [15, 0.1540759951, 35.7574],
    [24, 0.1848908958, 51.1509],
    ]

g_max_ss = graph.graphxy(
    width = graph_width,
    x = graph.axis.linear(min = 0,
                          max = 24,
                          title = "time (h)",
                          painter = grid_painter,
                          parter = graph.axis.parter.linear(tickdists
                                                            = [6, 3])),
    y = graph.axis.linear(min = 0,
                          max = 0.3,
                          title = "maximum supersaturation ($\%$)",
                          painter = grid_painter),
    key = graph.key.key(pos = "tr",
                        keyattrs = [deco.stroked,
                                    deco.filled([color.rgb.white])],
                        #hdist = 0.3 * unit.v_cm,
                        #vdist = 0.2 * unit.v_cm,
                        ))

g_active = graph.graphxy(
    width = graph_width,
    x = graph.axis.linear(min = 0,
                          max = 24,
                          title = "time (h)",
                          painter = grid_painter,
                          parter = graph.axis.parter.linear(tickdists
                                                            = [6, 3])),
    y = graph.axis.linear(min = 0,
                          max = 100,
                          title = "activated fraction ($\%$)",
                          painter = grid_painter),
    key = graph.key.key(pos = "tl",
                        keyattrs = [deco.stroked,
                                    deco.filled([color.rgb.white])],
                        #hdist = 0.3 * unit.v_cm,
                        #vdist = 0.2 * unit.v_cm,
                        ))

g_max_ss.doaxes()
g_active.doaxes()

for (i_data, (data, name)) in enumerate(zip([particle_data, size_avg_data, comp_avg_data],
                                            ["particle", "size-avg", "comp-avg"])):
    max_ss_data = [[d[0], d[1]] for d in data]
    active_data = [[d[0], d[2]] for d in data]
    g_max_ss.plot(graph.data.points(max_ss_data, x = 1, y = 2, title = name),
                  styles = [graph.style.line(lineattrs = [color_list[i_data]])])
    g_active.plot(graph.data.points(active_data, x = 1, y = 2, title = name),
                  styles = [graph.style.line(lineattrs = [color_list[i_data]])])
    #label_plot_line_boxed(g_max_ss, max_ss_data, 15, name, [1, 1])
    #label_plot_line_boxed(g_active, active_data, 15, name, [1, 1])

g_max_ss.writePDFfile(out_filename_max_ss)
g_active.writePDFfile(out_filename_active)
