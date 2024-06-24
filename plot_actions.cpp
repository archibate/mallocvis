#include "plot_actions.hpp"
#include "addr2sym.hpp"
#include "alloc_action.hpp"
#include <algorithm>
#ifdef __has_include
# if __cplusplus >= 201703L && __has_include(<charconv>)
#  include <charconv>
# endif
#endif
#if __cpp_lib_to_chars
# include <charconv>
#endif
#include <cmath>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#undef min
#undef max

namespace {

struct LifeBlock {
    AllocOp start_op;
    AllocOp end_op;
    uint32_t start_tid;
    uint32_t end_tid;
    void *ptr;
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

    explicit SvgWriter(std::string path, double width, double height,
                       double margin = 0)
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
            out << "#slide { position: absolute; top: 0%; left: 0%; "
                   "transform-origin: left top; }\n";
            out << "</style>\n";
            out << "</head>\n<body>\n<div id=\"container\">\n";
            out << "<svg id=\"slide\" width=\"" << width + margin * 2
                << "\" height=\"" << height
                << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";
        } else {
            out << "<svg width=\"" << width + margin * 2 << "\" height=\""
                << height << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";
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

struct ObjWriter {
    std::string verts;
    std::string faces;
    size_t nverts = 0;
    std::ofstream out;

    ObjWriter(std::string path) : out(path) {}

#if __cpp_lib_to_chars
    static std::string double_to_string(double d) {
        char buf[std::numeric_limits<double>::max_digits10 + 8];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), d);
        if (ec == std::errc()) {
            return std::string(buf, p - buf);
        } else {
            return {};
        }
    }

    static std::string int_to_string(size_t n) {
        char buf[21];
        auto [p, ec] = std::to_chars(buf, buf + sizeof(buf), n);
        if (ec == std::errc()) {
            return std::string(buf, p - buf);
        } else {
            return {};
        }
    }
#else
    static std::string double_to_string(double d) {
        std::ostringstream os;
        os << std::fixed
           << std::setprecision(std::numeric_limits<double>::digits10) << d;
        std::string s = os.str();
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
        return s;
    }

    static std::string int_to_string(size_t n) {
        return std::to_string(n);
    }
#endif

    void line(double y, double x0, double z0, double x1, double z1) {
        auto ys = double_to_string(y);
        ys += ' ';
        std::string x0s;
        x0s += 'v';
        x0s += ' ';
        x0s += double_to_string(x0);
        x0s += ' ';
        x0s += ys;
        std::string x1s;
        x1s += 'v';
        x1s += ' ';
        x1s += double_to_string(x1);
        x1s += ' ';
        x1s += ys;
        verts += x0s;
        verts += double_to_string(z0);
        verts += '\n';
        ++nverts;
        verts += x1s;
        verts += double_to_string(z1);
        verts += '\n';
        ++nverts;
        faces += 'l';
        faces += ' ';
        faces += int_to_string(nverts - 1);
        faces += ' ';
        faces += int_to_string(nverts);
        faces += '\n';
    }

    ObjWriter(ObjWriter &&) = delete;

    ~ObjWriter() {
        out << verts;
        out << faces;
        out.close();
    }
};

std::deque<std::string> string_split(std::string const &s, char delim) {
    std::deque<std::string> elems;
    std::istringstream iss(s);
    std::string item;
    while (getline(iss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

PlotOptions parse_plot_options_from_env() {
    PlotOptions options;
    auto env = std::getenv("MALLOCVIS");
    if (!env) {
        return options;
    }
    // MALLOCVIS=format:obj;path:/tmp/malloc.obj;height_scale:log;z_indicates:thread;layout:timeline;show_text:0;text_max_height:24;text_height_fraction:0.4;filter_cpp:1;filter_c:1;filter_cuda:1;svg_margin:420;svg_width:2000;svg_height:1460
    std::string s(env);
    auto splits = string_split(s, ';');
    bool has_format = false;
    for (auto &split: splits) {
        auto kv = string_split(split, ':');
        if (kv.size() != 2) {
            continue;
        }
        auto k = kv[0];
        auto v = kv[1];
        if (k == "format") {
            if (v == "svg") {
                options.format = PlotOptions::Svg;
            } else if (v == "obj") {
                options.format = PlotOptions::Obj;
            } else if (v == "console") {
                options.format = PlotOptions::Console;
            }
            has_format = true;
        } else if (k == "path") {
            options.path = v;
            if (!has_format) {
                if (v.size() >= 4 && v.substr(v.size() - 4) == ".svg") {
                    options.format = PlotOptions::Svg;
                } else if (v.size() >= 5 && v.substr(v.size() - 5) == ".html") {
                    options.format = PlotOptions::Svg;
                } else if (v.size() >= 4 && v.substr(v.size() - 4) == ".obj") {
                    options.format = PlotOptions::Obj;
                }
                has_format = true;
            }
        } else if (k == "height_scale") {
            if (v == "linear") {
                options.height_scale = PlotOptions::Linear;
            } else if (v == "log") {
                options.height_scale = PlotOptions::Log;
            }
        } else if (k == "z_indicates") {
            if (v == "thread") {
                options.z_indicates = PlotOptions::Thread;
            } else if (v == "caller") {
                options.z_indicates = PlotOptions::Caller;
            }
        } else if (k == "layout") {
            if (v == "timeline") {
                options.layout = PlotOptions::Timeline;
            } else if (v == "address") {
                options.layout = PlotOptions::Address;
            }
        } else if (k == "show_text") {
            options.show_text = v == "1";
        } else if (k == "text_max_height") {
            options.text_max_height = std::stoi(v);
        } else if (k == "text_height_fraction") {
            options.text_height_fraction = std::stod(v);
        } else if (k == "filter_cpp") {
            options.filter_cpp = v == "1";
        } else if (k == "filter_c") {
            options.filter_c = v == "1";
        } else if (k == "filter_cuda") {
            options.filter_cuda = v == "1";
        } else if (k == "svg_margin") {
            options.svg_margin = std::stoi(v);
        } else if (k == "svg_width") {
            options.svg_width = std::stoi(v);
        } else if (k == "svg_height") {
            options.svg_height = std::stoi(v);
        }
    }
    return options;
}

} // namespace

void mallocvis_plot_alloc_actions(std::vector<AllocAction> actions) {
    PlotOptions options = parse_plot_options_from_env();

    if (actions.empty()) {
        return;
    }
    std::sort(actions.begin(), actions.end(),
              [](AllocAction const &a, AllocAction const &b) {
                  return a.time < b.time;
              });

    std::cerr << "Ploting " << actions.size() << " actions...\n";
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
            living.insert({action.ptr,
                           {action.op, action.op, action.tid, action.tid,
                            action.ptr, action.size, action.caller,
                            action.caller, action.time, action.time}});
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
    if (options.height_scale == PlotOptions::Log) {
        eval_height = [](LifeBlock const &block) -> double {
            return std::max(std::log2(block.size), 0.0);
        };
    } else if (options.height_scale == PlotOptions::Sqrt) {
        eval_height = [](LifeBlock const &block) -> double {
            return std::sqrt(block.size);
        };
    } else {
        eval_height = [](LifeBlock const &block) -> double {
            return block.size;
        };
    }

    std::cerr << "Calculating boundary...\n";
    int64_t start_time = std::numeric_limits<int64_t>::max();
    int64_t end_time = std::numeric_limits<int64_t>::min();
    uintptr_t start_caller = std::numeric_limits<uintptr_t>::max();
    uintptr_t end_caller = std::numeric_limits<uintptr_t>::min();
    uintptr_t start_ptr = std::numeric_limits<uintptr_t>::max();
    uintptr_t end_ptr = std::numeric_limits<uintptr_t>::min();
    std::unordered_map<uint32_t, uint32_t> tids;
    for (auto const &block: dead) {
        start_time = std::min(start_time, block.start_time);
        if (block.end_time > block.start_time) {
            end_time = std::max(end_time, block.end_time);
        }
        start_caller = std::min(start_caller, (uintptr_t)block.start_caller);
        end_caller = std::max(end_caller, (uintptr_t)block.start_caller);
        start_caller = std::min(start_caller, (uintptr_t)block.end_caller);
        end_caller = std::max(end_caller, (uintptr_t)block.end_caller);
        start_ptr = std::min(start_ptr, (uintptr_t)block.ptr);
        end_ptr = std::max(end_ptr, (uintptr_t)block.ptr + block.size);
        tids.insert({block.start_tid, tids.size()});
        tids.insert({block.end_tid, tids.size()});
    }

    std::cerr << "Searching for leakage...\n";
    for (auto &[_, block]: living) {
        start_time = std::min(start_time, block.start_time);
        block.end_time = end_time;
        block.end_caller = nullptr;
        start_caller = std::min(start_caller, (uintptr_t)block.start_caller);
        end_caller = std::max(end_caller, (uintptr_t)block.start_caller);
        start_caller = std::min(start_caller, (uintptr_t)block.end_caller);
        end_caller = std::max(end_caller, (uintptr_t)block.end_caller);
        start_ptr = std::min(start_ptr, (uintptr_t)block.ptr);
        end_ptr = std::max(end_ptr, (uintptr_t)block.ptr + block.size);
        tids.insert({block.start_tid, tids.size()});
        tids.insert({block.end_tid, tids.size()});
        dead.insert(block);
    }
    living.clear();

    if (dead.empty()) {
        return;
    }

    if (options.format == PlotOptions::Obj) {
        ObjWriter obj(options.path.empty() ? "malloc.obj" : options.path);
        double time_scale = 1.0 / (end_time - start_time);
        double caller_scale = 1.0 / (end_caller - start_caller);

        std::cerr << "Finding maximum height...\n";
        double max_height = 0.01;
        for (auto const &block: dead) {
            double y = eval_height(block);
            max_height = std::max(max_height, y);
        }
        double height_scale = 1.0 / max_height;

        std::cerr << "Finding maximum Z...\n";
        auto eval_z = [&](LifeBlock const &block) -> std::pair<double, double> {
            double z0 = 0;
            double z1 = 0;
            if (options.z_indicates == PlotOptions::Caller) {
                z0 = ((uintptr_t)block.start_caller - start_caller) *
                     caller_scale;
                z1 =
                    ((uintptr_t)block.end_caller - start_caller) * caller_scale;
            } else if (options.z_indicates == PlotOptions::Thread) {
                z0 = tids.at(block.start_tid);
                z1 = tids.at(block.end_tid);
            }
            return {z0, z1};
        };
        double max_z = 0.01;
        for (auto const &block: dead) {
            auto [z0, z1] = eval_z(block);
            max_z = std::max(max_z, z0);
            max_z = std::max(max_z, z1);
        }
        double z_scale = 1.0 / max_z;

        std::cerr << "Generating 3D model...\n";
        for (auto const &block: dead) {
            // x for time, y for size, z for caller
            double x0 = (block.start_time - start_time) * time_scale;
            double x1 = (block.end_time - start_time) * time_scale;
            double y = eval_height(block) * height_scale;
            auto [z0, z1] = eval_z(block);
            z0 *= z_scale;
            z1 *= z_scale;
            obj.line(y, x0, z0, x1, z1);
        }
        std::cerr << "Writing 3D model...\n";

    } else if (options.format == PlotOptions::Svg) {
        std::set<void *> callers;
        double total_height = 0;
        std::cerr << "Calculating caller indices...\n";
        for (auto const &block: dead) {
            double height = eval_height(block);
            total_height += height;
            callers.insert(block.start_caller);
            callers.insert(block.end_caller);
        }
        if (options.layout == PlotOptions::Address) {
            total_height = end_ptr - start_ptr;
        }
        double total_width = end_time - start_time + 1;

        double width_scale = options.svg_width / total_width;
        double height_scale = options.svg_height / total_height;
        total_width *= width_scale;
        total_height *= height_scale;

        std::cerr << "Finalizing caller indices...\n";
        std::map<void *, size_t> caller_index;
        size_t num_callers = 0;
        for (auto const &caller: callers) {
            if (caller) {
                caller_index.insert({caller, num_callers++});
            }
        }
        caller_index.insert({nullptr, kNone});

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

        SvgWriter svg(options.path.empty() ? "malloc.html" : options.path,
                      total_width, total_height, options.svg_margin);
        double y = 0;
        std::cerr << "Generating SVG graph...\n";
        if (options.layout == PlotOptions::Address) {
            for (auto const &block: dead) {
                double width =
                    (block.end_time - block.start_time) * width_scale;
                double height = block.size * height_scale;
                double y = ((uintptr_t)block.ptr - start_ptr) * height_scale;
                double x = (block.start_time - start_time) * width_scale;
                auto [color1, color2] = eval_color(block);
                auto gradColor = svg.defGradient(color1, color2);
                svg.rect(x, y, width, height, gradColor);
                if (options.show_text) {
                    auto [text1, text2] = eval_text(block);
                    auto fontHeight = std::min(
                        (size_t)(height * options.text_height_fraction + 0.5),
                        options.text_max_height);
                    if (!text1.empty()) {
                        auto max_width = options.svg_margin + x;
                        auto fontHeight1 = fontHeight;
                        if (fontHeight * 0.5 * text1.size() > max_width) {
                            fontHeight1 *=
                                max_width / (fontHeight * 0.5 * text1.size());
                        }
                        svg.text(
                            x, y + height * 0.5, color1,
                            " style=\"dominant-baseline:central;text-anchor:"
                            "end;font-size:" +
                                std::to_string(fontHeight1) + "px;\"",
                            text1);
                    }
                    if (!text2.empty()) {
                        auto max_width =
                            options.svg_width + options.svg_margin - x;
                        auto fontHeight1 = fontHeight;
                        if (fontHeight * 0.5 * text2.size() > max_width) {
                            fontHeight1 *=
                                max_width / (fontHeight * 0.5 * text2.size());
                        }
                        svg.text(
                            x + width, y + height * 0.5, color2,
                            " style=\"dominant-baseline:central;text-anchor:"
                            "start;font-size:" +
                                std::to_string(fontHeight1) + "px;\"",
                            text2);
                    }
                }
            }
        } else {
            for (auto const &block: dead) {
                double width =
                    (block.end_time - block.start_time) * width_scale;
                double height = eval_height(block) * height_scale;
                double x = (block.start_time - start_time) * width_scale;
                auto [color1, color2] = eval_color(block);
                auto gradColor = svg.defGradient(color1, color2);
                svg.rect(x, y, width, height, gradColor);
                if (options.show_text) {
                    auto [text1, text2] = eval_text(block);
                    auto fontHeight = std::min(
                        (size_t)(height * options.text_height_fraction + 0.5),
                        options.text_max_height);
                    if (!text1.empty()) {
                        auto max_width = options.svg_margin + x;
                        auto fontHeight1 = fontHeight;
                        if (fontHeight * 0.5 * text1.size() > max_width) {
                            fontHeight1 *=
                                max_width / (fontHeight * 0.5 * text1.size());
                        }
                        svg.text(
                            x, y + height * 0.5, color1,
                            " style=\"dominant-baseline:central;text-anchor:"
                            "end;font-size:" +
                                std::to_string(fontHeight1) + "px;\"",
                            text1);
                    }
                    if (!text2.empty()) {
                        auto max_width =
                            options.svg_width + options.svg_margin - x;
                        auto fontHeight1 = fontHeight;
                        if (fontHeight * 0.5 * text2.size() > max_width) {
                            fontHeight1 *=
                                max_width / (fontHeight * 0.5 * text2.size());
                        }
                        svg.text(
                            x + width, y + height * 0.5, color2,
                            " style=\"dominant-baseline:central;text-anchor:"
                            "start;font-size:" +
                                std::to_string(fontHeight1) + "px;\"",
                            text2);
                    }
                }
                y += height;
            }
        }

        std::cerr << "Writing SVG file...\n";

    } else if (options.format == PlotOptions::Console) {
        int64_t const screen_width = 60;
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
#if _WIN32
            std::cout << repeat(block.start_time - start_time, " ", "|");
            std::cout << repeat(block.end_time - block.start_time, "-", "|");
#else
            std::cout << repeat(block.start_time - start_time, " ", "┌");
            std::cout << repeat(block.end_time - block.start_time, "─", "┐");
#endif
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
