#include "plot_actions.hpp"
#include "addr2sym.hpp"
#include "alloc_action.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>

struct LifeBlock {
    AllocOp start_op;
    AllocOp end_op;
    uint32_t start_tid;
    uint32_t end_tid;
    size_t size;
    void *start_caller;
    void *end_caller;
    int64_t start_time;
    int64_t end_time;
};

struct LifeBlockCompare {
    bool operator()(LifeBlock const &a, LifeBlock const &b) const {
        return a.start_time < b.start_time;
    }
};

std::string hsvToRgb(double hue, double sat, double val) {
    int i = (int)(hue * 6);
    double f = hue * 6 - i;
    double p = val * (1 - sat);
    double q = val * (1 - f * sat);
    double t = val * (1 - (1 - f) * sat);
    double r, g, b;
    switch (i % 6) {
    case 0:  r = val, g = t, b = p; break;
    case 1:  r = q, g = val, b = p; break;
    case 2:  r = p, g = val, b = t; break;
    case 3:  r = p, g = q, b = val; break;
    case 4:  r = t, g = p, b = val; break;
    case 5:  r = val, g = p, b = q; break;
    default: throw;
    }
    std::stringstream ss;
    ss << "#" << std::setfill('0') << std::setw(2) << std::hex << (int)(r * 255)
       << std::setfill('0') << std::setw(2) << std::hex << (int)(g * 255)
       << std::setfill('0') << std::setw(2) << std::hex << (int)(b * 255);
    return ss.str();
}

struct SvgWriter {
    std::ofstream out;
    std::ostringstream defs;
    std::map<std::string, int> gradients;
    size_t gradientId = 0;
    double fullWidth;
    double fullHeight;
    double margin;
    bool isHtml;

    explicit SvgWriter(std::string path, double width, double height, double margin = 0)
        : out(path),
          fullWidth(width + margin * 2),
          fullHeight(height),
          margin(margin),
          isHtml(path.rfind(".htm") != std::string::npos) {
        if (isHtml) {
            out << "<!DOCTYPE html>\n<html>\n<head>\n";
            out << "<style>\n";
            out << "body { background-color: #222222; margin: 0; }\n";
            out << "#container { position: absolute; top: 0%; left: 0%; width: "
                   "100%; height: 100%; max-width: 100%; max-height: 100%; "
                   "overflow: hidden; }\n";
            out << "#slide { position: absolute; top: 0%; left: 0%; transform-origin: left top; }\n";
            out << "</style>\n";
            out << "</head>\n<body>\n<div id=\"container\">\n";
            out << "<svg id=\"slide\" width=\"" << width + margin * 2 << "\" height=\""
                << height << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";
        } else {
            out << "<svg width=\"" << width + margin * 2 << "\" height=\"" << height
                << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";
        }
    }

    std::string defGradient(std::string const &color1,
                            std::string const &color2) {
        auto key = color1 + ',' + color2;
        auto it = gradients.find(key);
        if (it != gradients.end()) {
            return "url(#g" + std::to_string(it->second) + ")";
        }
        size_t id = gradientId++;
        defs << "<linearGradient id=\"g" << id << "\">\n";
        defs << "<stop offset=\"0%\" stop-color=\"" << color1 << "\"/>\n";
        defs << "<stop offset=\"100%\" stop-color=\"" << color2 << "\"/>\n";
        defs << "</linearGradient>\n";
        gradients.insert({key, id});
        return "url(#g" + std::to_string(id) + ")";
    }

    void rect(double x, double y, double width, double height,
              std::string const &color) {
        x += margin;
        out << "<rect width=\"" << width << "\" height=\"" << height
            << "\" x=\"" << x << "\" y=\"" << y << "\" fill=\"" << color
            << "\"/>\n";
    }

    void text(double x, double y, std::string const &color,
              std::string const &alignment, std::string const &text) {
        x += margin;
        // HTML escape text:
        std::string html;
        html.reserve(text.size());
        for (auto c: text) {
            switch (c) {
            case '&':  html += "&amp;"; break;
            case '<':  html += "&lt;"; break;
            case '>':  html += "&gt;"; break;
            case '"':  html += "&quot;"; break;
            case '\'': html += "&apos;"; break;
            default:   html += c; break;
            }
        }
        out << "<text x=\"" << x << "\" y=\"" << y << "\" fill=\"" << color
            << "\"" << alignment << ">" << html << "</text>\n";
    }

    SvgWriter(SvgWriter &&) = delete;

    ~SvgWriter() {
        out << "<defs>\n";
        out << defs.str();
        out << "</defs>\n";
        out << "</svg>\n";
        if (isHtml) {
            out << "</div>\n<script "
                   "src=\"https://cdn.bootcdn.net/ajax/libs/jquery/3.7.1/"
                   "jquery.min.js\"></script>\n";
            out << "<script>\nvar svgFullWidth = " << fullWidth << ";\n";
            out << "var svgFullHeight = " << fullHeight << ";\n";
            out << "function fit() {\n";
            out << "    var width = $(window).width();\n";
            out << "    var height = $(window).height();\n";
            out << "    if (width / height > svgFullWidth / svgFullHeight) {\n";
            out << "        var scale = height / svgFullHeight;\n";
            out << "        var margin = (width - svgFullWidth * scale) / 2;\n";
            out << "        $(\"#slide\").css({\n";
            out << "            \"transform\": \"translate3d(\" + margin + "
                   "\"px, 0, 0) scale(\" + scale + \")\",\n";
            out << "            \"transform-origin\": \"left top\",\n";
            out << "            \"transition\": \"\",\n";
            out << "        });\n";
            out << "    } else {\n";
            out << "        var scale = width / svgFullWidth;\n";
            out << "        var margin = (height - svgFullHeight * scale) / "
                   "2;\n";
            out << "        $(\"#slide\").css({\n";
            out << "            \"transform\": \"translate3d(0, \" + margin + "
                   "\"px, 0) scale(\" + scale + \")\",\n";
            out << "            \"transform-origin\": \"left top\",\n";
            out << "            \"transition\": \"\",\n";
            out << "        });\n";
            out << "    }\n";
            out << "}\n";
            out << "</script>\n";
            out << R"html(<script>
$(function() {
    fit();
    $("#container").on("mousewheel DOMMouseScroll", function (e) {
        var translateX = parseFloat($("#slide").css("transform").split(",")[4]);
        var translateY = parseFloat($("#slide").css("transform").split(",")[5]);
        var scale = parseFloat($("#slide").css("transform").split(",")[0].split("(")[1]);
        translateX = isNaN(translateX) ? 0 : translateX;
        translateY = isNaN(translateY) ? 0 : translateY;
        scale = isNaN(scale) ? 1 : scale;

        e.preventDefault();
        var delta = (e.originalEvent.wheelDelta && (e.originalEvent.wheelDelta > 0 ? 1 : -1)) || // chrome & ie
                    (e.originalEvent.detail && (e.originalEvent.detail > 0 ? -1 : 1)); // firefox
        var newScale = scale * Math.pow(1.5, delta);

        var mouseX = e.pageX - $("#container").offset().left;
        var mouseY = e.pageY - $("#container").offset().top;
        console.log(e.pageX, e.pageY, $("#container").offset().left, $("#container").offset().top);
        // (mouseX - translateX) / scale = (mouseX - newTranslateX) / newScale
        var newTranslateX = mouseX - (mouseX - translateX) / scale * newScale;
        var newTranslateY = mouseY - (mouseY - translateY) / scale * newScale;
        translateX = newTranslateX;
        translateY = newTranslateY;
        scale = newScale;

        $("#slide").css("transform", 'translate3d(' + translateX + 'px, ' + translateY + 'px, 0) scale(' + scale + ')');
        $("#slide").css("transition", 'transform 100ms');
    });
    $("#container").on("mousedown", function (e) {
        var translateX = parseFloat($("#slide").css("transform").split(",")[4]);
        var translateY = parseFloat($("#slide").css("transform").split(",")[5]);
        var scale = parseFloat($("#slide").css("transform").split(",")[0].split("(")[1]);
        translateX = isNaN(translateX) ? 0 : translateX;
        translateY = isNaN(translateY) ? 0 : translateY;
        scale = isNaN(scale) ? 1 : scale;

        e.preventDefault();
        var lastX = e.pageX;
        var lastY = e.pageY;
        $("#container").on("mousemove", function (e) {
            var deltaX = e.pageX - lastX;
            var deltaY = e.pageY - lastY;
            translateX += deltaX;
            translateY += deltaY;
            $("#slide").css("transform", 'translate3d(' + translateX + 'px, ' + translateY + 'px, 0) scale(' + scale + ')');
            $("#slide").css("transition", '');
            lastX = e.pageX;
            lastY = e.pageY;
        });
        $("#container").on("mouseenter mouseleave", function () {
            $("#container").off("mousemove");
            $("#container").off("mouseup");
            $("#container").off("mouseenter");
            $("#container").off("mouseleave");
        });
        $("#container").on("mouseup", function () {
            $("#container").off("mousemove");
            $("#container").off("mouseup");
            $("#container").off("mouseenter");
            $("#container").off("mouseleave");
        });
    });
    $("#container").on("dblclick", function (e) {
        e.preventDefault();
        fit();
        $("#slide").css("transition", 'transform 200ms');
    });
});
</script>)html";
            out << "</body>\n</html>\n";
        }
        out.close();
    }
};

void plot_alloc_actions(std::deque<AllocAction> const &actions,
                        PlotOptions const &options) {
    std::map<void *, LifeBlock> living;
    std::set<LifeBlock, LifeBlockCompare> dead;
    for (auto const &action: actions) {
        if (!options.filter_c && kAllocOpIsC[(size_t)action.op]) {
            continue;
        }
        if (!options.filter_cpp && kAllocOpIsCpp[(size_t)action.op]) {
            continue;
        }
        if (!options.filter_cuda && kAllocOpIsCpp[(size_t)action.op]) {
            continue;
        }
        if (kAllocOpIsAllocation[(size_t)action.op]) {
            living.insert(
                {action.ptr,
                 {action.op, action.op, action.tid, action.tid, action.size,
                  action.caller, action.caller, action.time, action.time}});
        } else {
            auto it = living.find(action.ptr);
            if (it != living.end()) {
                it->second.end_op = action.op;
                it->second.end_tid = action.tid;
                it->second.end_time = action.time;
                it->second.end_caller = action.caller;
                dead.insert(it->second);
                living.erase(it);
            }
        }
    }

    double (*eval_height)(LifeBlock const &);
    if (options.heightScale == PlotOptions::Log) {
        eval_height = [](LifeBlock const &block) -> double {
            return std::max(std::log2(block.size), 0.0);
        };
    } else if (options.heightScale == PlotOptions::Sqrt) {
        eval_height = [](LifeBlock const &block) -> double {
            return std::sqrt(block.size);
        };
    } else {
        eval_height = [](LifeBlock const &block) -> double {
            return block.size;
        };
    }

    int64_t start_time = std::numeric_limits<int64_t>::max();
    int64_t end_time = std::numeric_limits<int64_t>::min();
    for (auto const &block: dead) {
        start_time = std::min(start_time, block.start_time);
        if (block.end_time > block.start_time) {
            end_time = std::max(end_time, block.end_time);
        }
    }

    for (auto &[_, block]: living) {
        block.end_time = end_time + 1;
        block.end_caller = nullptr;
        dead.insert(block);
    }
    living.clear();

    std::set<void *> callers;
    double total_height = 0;
    for (auto const &block: dead) {
        double height = eval_height(block);
        total_height += height;
        callers.insert(block.start_caller);
        callers.insert(block.end_caller);
    }
    double total_width = end_time - start_time + 1;

    double width_scale = options.width / total_width;
    double height_scale = options.height / total_height;
    total_width *= width_scale;
    total_height *= height_scale;

    std::map<void *, size_t> caller_index;
    size_t num_callers = 0;
    for (auto const &caller: callers) {
        if (caller) {
            caller_index.insert({caller, num_callers++});
        }
    }
    caller_index.insert({nullptr, kNone});

    SvgWriter svg(options.path, total_width, total_height, options.margin);

    auto caller_color = [&](void *caller) -> std::string {
        size_t index = caller_index.at(caller);
        if (index == kNone) {
            return "black";
        }
        double hue = index * 1.0 / num_callers;
        return hsvToRgb(hue, 0.7, 0.7);
    };

    auto eval_color =
        [&](LifeBlock const &block) -> std::pair<std::string, std::string> {
        return {caller_color(block.start_caller),
                caller_color(block.end_caller)};
    };

    auto eval_text =
        [](LifeBlock const &block) -> std::pair<std::string, std::string> {
        return {addr2sym(block.start_caller), addr2sym(block.end_caller)};
    };

    if (options.format == PlotOptions::Svg) {
        double y = 0;
        for (auto const &block: dead) {
            double width = (block.end_time - block.start_time) * width_scale;
            double height = eval_height(block) * height_scale;
            double x = (block.start_time - start_time) * width_scale;
            auto [color1, color2] = eval_color(block);
            auto gradColor = svg.defGradient(color1, color2);
            svg.rect(x, y, width, height, gradColor);
            if (options.show_text) {
                auto [text1, text2] = eval_text(block);
                auto fontHeight = std::min((size_t)(height * options.text_height_fraction + 0.5),
                                     options.text_max_height);
                if (!text1.empty()) {
                    auto max_width = options.margin + x;
                    auto fontHeight1 = fontHeight;
                    if (fontHeight * 0.5 * text1.size() > max_width) {
                        fontHeight1 *= max_width / (fontHeight * 0.5 * text1.size());
                    }
                    svg.text(x, y + height * 0.5, color1,
                             " style=\"dominant-baseline:central;text-anchor:"
                             "end;font-size:" +
                                 std::to_string(fontHeight1) + "px;\"",
                             text1);
                }
                if (!text2.empty()) {
                    auto max_width = options.width + options.margin - x;
                    auto fontHeight1 = fontHeight;
                    if (fontHeight * 0.5 * text2.size() > max_width) {
                        fontHeight1 *= max_width / (fontHeight * 0.5 * text2.size());
                    }
                    svg.text(x + width, y + height * 0.5, color2,
                             " style=\"dominant-baseline:central;text-anchor:"
                             "start;font-size:" +
                                 std::to_string(fontHeight1) + "px;\"",
                             text2);
                }
            }
            y += height;
        }
    } else if (options.format == PlotOptions::Console) {
        const int64_t screen_width = 60;
        auto repeat = [&](int64_t d, char const *s, char const *end) {
            size_t n = std::max(d * screen_width, (int64_t)0) /
                       (end_time - start_time + 1);
            std::string r;
            for (size_t i = 0; i < n; i++) {
                r += s;
            }
            r += end;
            return r;
        };
        for (auto const &block: dead) {
            std::cout << repeat(block.start_time - start_time, " ", "┌");
            std::cout << repeat(block.end_time - block.start_time, "─", "┐");
            std::cout << block.size << '\n';
        }
    }
}

/* void dump_alloc_actions_to_file(std::deque<AllocAction> const &actions,
 * std::string const &path) { */
/*     std::ofstream out(path, std::ios::binary); */
/*     if (out) { */
/*         for (auto const &action: actions) { */
/*             out.write((char const *)&action, sizeof(action)); */
/*         } */
/*     } */
/* } */

/* void plot_alloc_actions_from_file(std::string const &path) { */
/*     std::ifstream in(path, std::ios::binary); */
/*     if (!in) return; */
/*     std::deque<AllocAction> actions; */
/*     AllocAction action; */
/*     while (in.read((char *)&action, sizeof(action))) { */
/*         actions.push_back(action); */
/*     } */
/*     plot_alloc_actions(actions); */
/* } */
