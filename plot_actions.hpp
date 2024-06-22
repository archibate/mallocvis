#pragma once

#include "alloc_action.hpp"
#include <cstddef>
#include <deque>
#include <string>

struct PlotOptions {
    enum PlotFormat {
        Console,
        Svg,
        Obj,
    };

    enum PlotScale {
        Linear,
        Log,
        Sqrt,
    };

    enum PlotIndicate {
        Thread,
        Caller,
    };

    PlotFormat format = Svg;
    std::string path = "";

    PlotScale height_scale = Log;
    PlotIndicate z_indicates = Thread;
    bool show_text = true;
    size_t text_max_height = 24;
    double text_height_fraction = 0.4;

    bool filter_cpp = true;
    bool filter_c = true;
    bool filter_cuda = true;

    size_t svg_margin = 420;
    size_t svg_width = 2000;
    size_t svg_height = 1460;
};

PlotOptions parse_plot_options_from_env();
void plot_alloc_actions(std::deque<AllocAction> const &actions, PlotOptions const &options = parse_plot_options_from_env());
