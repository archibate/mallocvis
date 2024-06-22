#pragma once

#include "alloc_action.hpp"
#include <cstddef>
#include <deque>
#include <string>

struct PlotOptions {
    enum PlotFormat {
        Console,
        Svg,
    };

    enum PlotScale {
        Linear,
        Log,
        Sqrt,
    };

    PlotFormat format = Svg;
    std::string path = "malloc.html";

    PlotScale heightScale = Sqrt;
    bool show_text = false;
    bool show_color = true;
    size_t text_max_height = 24;
    double text_height_fraction = 0.4;

    bool filter_cpp = true;
    bool filter_c = false;
    bool filter_cuda = true;

    size_t margin = 420;
    size_t width = 2000;
    size_t height = 1460;
};

void plot_alloc_actions(std::deque<AllocAction> const &actions, PlotOptions const &options);
