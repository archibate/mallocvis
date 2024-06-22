#include "alloc_action.hpp"
#include <condition_variable>
#include <deque>
#include <fstream>
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

enum class Modifier {
    kNone,
    kCtrl,
    kShift,
    kAlt,
};

enum class MouseButton {
    kLMB,
    kRMB,
    kMMB,
    kWheel,
    kNone,
};

struct MouseBinding {
    Modifier modifier = Modifier::kNone;
    MouseButton mouseBtn = MouseButton::kLMB;

    bool check_is_scrolled(GLFWwindow *window) {
        bool modPressed = true;
        bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                     glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        bool alt = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                   glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        auto mods = (shift ? GLFW_MOD_SHIFT : 0) |
                    (ctrl ? GLFW_MOD_CONTROL : 0) | (alt ? GLFW_MOD_ALT : 0);
        switch (modifier) {
        case Modifier::kShift: modPressed = mods == GLFW_MOD_SHIFT; break;
        case Modifier::kCtrl:  modPressed = mods == GLFW_MOD_CONTROL; break;
        case Modifier::kAlt:   modPressed = mods == GLFW_MOD_ALT; break;
        default:               modPressed = mods == 0; break;
        }
        bool wheelScrolled = false;
        switch (mouseBtn) {
        case MouseButton::kWheel: wheelScrolled = true; break;
        default:                  break;
        }
        return wheelScrolled && modPressed;
    }

    bool check_is_pressed(GLFWwindow *window) {
        bool modPressed = true;
        bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                     glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        bool alt = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                   glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        auto mods = (shift ? GLFW_MOD_SHIFT : 0) |
                    (ctrl ? GLFW_MOD_CONTROL : 0) | (alt ? GLFW_MOD_ALT : 0);
        switch (modifier) {
        case Modifier::kShift: modPressed = mods == GLFW_MOD_SHIFT; break;
        case Modifier::kCtrl:  modPressed = mods == GLFW_MOD_CONTROL; break;
        case Modifier::kAlt:   modPressed = mods == GLFW_MOD_ALT; break;
        default:               modPressed = mods == 0; break;
        }
        int button = GLFW_MOUSE_BUTTON_LEFT;
        switch (mouseBtn) {
        case MouseButton::kLMB: button = GLFW_MOUSE_BUTTON_LEFT; break;
        case MouseButton::kMMB: button = GLFW_MOUSE_BUTTON_MIDDLE; break;
        case MouseButton::kRMB: button = GLFW_MOUSE_BUTTON_RIGHT; break;
        case MouseButton::kNone:
            return modPressed &&
                   glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) !=
                       GLFW_PRESS &&
                   glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) !=
                       GLFW_PRESS &&
                   glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) !=
                       GLFW_PRESS;
        default: return false;
        }
        return modPressed && glfwGetMouseButton(window, button) == GLFW_PRESS;
    }
};

struct InputPreference {
    float zoom_speed = 0.2f;
    float orbit_speed = 1.0f;
    float drift_speed = 1.0f;
    float pan_speed = 2.0f;
    MouseBinding orbit_binding = {Modifier::kNone, MouseButton::kLMB};
    MouseBinding drift_binding = {Modifier::kNone, MouseButton::kMMB};
    MouseBinding pan_binding = {Modifier::kNone, MouseButton::kRMB};
    MouseBinding zoom_binding = {Modifier::kNone, MouseButton::kWheel};
    MouseBinding hitchcock_binding = {Modifier::kShift, MouseButton::kWheel};
    int zoom_axis = 1;
    bool clamp_cursor = true;
};

struct CameraState {
    glm::vec3 eye = {0, 0, 5};
    glm::vec3 lookat = {0, 0, 0};
    glm::vec3 up_vector = {0, 1, 0};
    glm::vec3 keep_up_axis = {0, 1, 0};
    float focal_len = 40.0f;
    float film_height = 24.0f;
    float film_width = 32.0f;
    int width = 1920;
    int height = 1080;

    void pan(InputPreference const &pref, glm::vec2 delta) {
        delta *= -pref.pan_speed;

        auto front_vector = glm::normalize(lookat - eye);
        auto right_vector = glm::normalize(glm::cross(front_vector, up_vector));
        auto fixed_up_vector =
            glm::normalize(glm::cross(right_vector, front_vector));

        auto delta3d = delta.x * right_vector + delta.y * fixed_up_vector;

        eye += delta3d;
        lookat += delta3d;
    }

    void orbit(InputPreference const &pref, glm::vec2 delta, bool isDrift) {
        if (isDrift) {
            delta *= -pref.drift_speed;
            delta *= std::atan(film_height / (2 * focal_len));
        } else {
            delta *= pref.orbit_speed;
        }

        auto angle_X_inc = delta.x;
        auto angle_Y_inc = delta.y;

        // pivot choose: drift mode rotates around eye center, orbit mode
        // rotates around target object
        auto rotation_pivot = isDrift ? eye : lookat;

        auto front_vector = glm::normalize(lookat - eye);

        // new right vector (orthogonal to front, up)
        auto right_vector = glm::normalize(glm::cross(front_vector, up_vector));

        // new up vector (orthogonal to right, front)
        up_vector = glm::normalize(glm::cross(right_vector, front_vector));

        // rotation 1: based on the mouse horizontal axis
        glm::mat4x4 rotation_matrixX =
            glm::rotate(glm::mat4x4(1), -angle_X_inc, up_vector);

        // rotation 2: based on the mouse vertical axis
        glm::mat4x4 rotation_matrixY =
            glm::rotate(glm::mat4x4(1), angle_Y_inc, right_vector);

        // translate back to the origin, rotate and translate back to the pivot
        // location
        auto transformation = glm::translate(glm::mat4x4(1), rotation_pivot) *
                              rotation_matrixY * rotation_matrixX *
                              glm::translate(glm::mat4x4(1), -rotation_pivot);

        // update eye and lookat coordinates
        eye = glm::vec3(transformation * glm::vec4(eye, 1));
        lookat = glm::vec3(transformation * glm::vec4(lookat, 1));

        // try to keep the camera horizontal line correct (eval right axis
        // error)
        float right_o_up = glm::dot(right_vector, keep_up_axis);
        float right_handness =
            glm::dot(glm::cross(keep_up_axis, right_vector), front_vector);
        float angle_Z_err = glm::asin(right_o_up);
        angle_Z_err *= glm::atan(right_handness);
        // rotation for up: cancel out the camera horizontal line drift
        glm::mat4x4 rotation_matrixZ =
            glm::rotate(glm::mat4x4(1), angle_Z_err, front_vector);
        up_vector = glm::mat3x3(rotation_matrixZ) * up_vector;
    }

    void zoom(InputPreference const &pref, float delta, bool isHitchcock) {
        float inv_zoom_factor = glm::exp(-pref.zoom_speed * delta);
        eye = (eye - lookat) * inv_zoom_factor + lookat;
        if (isHitchcock) {
            focal_len *= inv_zoom_factor;
        }
    }

    glm::mat4x4 view_matrix() const {
        return glm::lookAt(eye, lookat, up_vector);
    }

    glm::mat4x4 projection_matrix() {
        auto fov = 2 * std::atan(film_height / (2 * focal_len));
        auto aspect = (float)width / height;
        return glm::perspective(fov, aspect, 0.01f, 100.0f);
    }
};

CameraState camState;
InputPreference inputPref;
glm::vec2 lastpos;
bool moving = false;

glm::vec2 get_cursor_pos(GLFWwindow *window) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    int width, height;
    glfwGetWindowSize(window, &width, &height);

    float x = (float)(2 * xpos / width - 1);
    float y = (float)(2 * (height - ypos) / height - 1);
    return glm::vec2(x, y);
}

void cursor_pos_callback(GLFWwindow *window, double xpos, double ypos) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);

    float x = (float)(2 * xpos / width - 1);
    float y = (float)(2 * (height - ypos) / height - 1);
    glm::vec2 pos(x, y);

    moving = true;
    auto delta = glm::fract((pos - lastpos) * 0.5f + 0.5f) * 2.0f - 1.0f;
    if (inputPref.orbit_binding.check_is_pressed(window)) {
        camState.orbit(inputPref, delta, false);
    } else if (inputPref.drift_binding.check_is_pressed(window)) {
        camState.orbit(inputPref, delta, true);
    } else if (inputPref.pan_binding.check_is_pressed(window)) {
        camState.pan(inputPref, delta);
    } else if (inputPref.zoom_binding.check_is_pressed(window)) {
        camState.zoom(inputPref, delta[inputPref.zoom_axis], false);
    } else if (inputPref.hitchcock_binding.check_is_pressed(window)) {
        camState.zoom(inputPref, delta[inputPref.zoom_axis], true);
    } else {
        moving = false;
    }
    lastpos = pos;

    if (moving && inputPref.clamp_cursor && (xpos >= width - 1 || ypos >= height - 1 || xpos <= 1 || ypos <= 1)) {
        // clamp mouse cursor inside the window (ZHI JING Blender)
        xpos = std::fmod(xpos + width - 3, width - 2) + 1;
        ypos = std::fmod(ypos + height - 3, height - 2) + 1;
        glfwSetCursorPos(window, xpos, ypos);
    }
}

void mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
    (void)button;
    (void)action;
    (void)mods;

    auto pos = get_cursor_pos(window);
    lastpos = pos;

    moving = inputPref.orbit_binding.check_is_pressed(window)
        || inputPref.drift_binding.check_is_pressed(window)
        || inputPref.pan_binding.check_is_pressed(window)
        || inputPref.zoom_binding.check_is_pressed(window)
        || inputPref.hitchcock_binding.check_is_pressed(window);

    GLFWcursor *cursor = glfwCreateStandardCursor(moving ? GLFW_CROSSHAIR_CURSOR : GLFW_ARROW_CURSOR);
    glfwSetCursor(window, cursor);
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
    float deltax = xoffset < 0 ? -1 : xoffset > 0 ? 1 : 0;
    float deltay = yoffset < 0 ? -1 : yoffset > 0 ? 1 : 0;
    glm::vec2 delta(deltax, deltay);

    if (inputPref.orbit_binding.check_is_scrolled(window)) {
        camState.orbit(inputPref, delta, false);
    } else if (inputPref.drift_binding.check_is_scrolled(window)) {
        camState.orbit(inputPref, delta, true);
    } else if (inputPref.pan_binding.check_is_scrolled(window)) {
        camState.pan(inputPref, delta);
    } else if (inputPref.zoom_binding.check_is_scrolled(window)) {
        camState.zoom(inputPref, delta[inputPref.zoom_axis], false);
    } else if (inputPref.hitchcock_binding.check_is_scrolled(window)) {
        camState.zoom(inputPref, delta[inputPref.zoom_axis], true);
    }
}

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    (void)key;
    (void)scancode;
    (void)action;
    (void)mods;
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
    camState.width = width;
    camState.height = height;
}

glm::vec3 hsvToRgb(float h, float s, float v) {
    int i = (int)glm::floor(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);
    float r, g, b;
    switch (i % 6) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        case 5: r = v, g = p, b = q; break;
    }
    return glm::vec3(r, g, b);
}

glm::vec3 randColor(uintptr_t x) {
    std::mt19937 rng(x);
    std::uniform_real_distribution<float> hue(0.0f, 1.0f);
    std::uniform_real_distribution<float> sat(0.5f, 0.8f);
    std::uniform_real_distribution<float> val(0.6f, 0.9f);
    return hsvToRgb(hue(rng), sat(rng), val(rng));
}

std::deque<AllocAction> actions;
std::condition_variable cv;
std::mutex mtx;

struct Life {
    struct Endpoint {
        AllocOp op;
        int64_t time;
        uint32_t tid;
        uintptr_t caller;
    };

    uintptr_t ptr;
    size_t size;
    size_t align;
    Endpoint start;
    Endpoint end;
    bool has_end;
};

std::map<uintptr_t, Life> lifes;
std::deque<Life> dead;

struct Rect {
    int64_t x0;
    int64_t x1;
    uintptr_t y0;
    uintptr_t y1;
    uintptr_t z0;
    uintptr_t z1;

    explicit Rect(Life const &life, int64_t xmax) {
        x0 = life.start.time;
        x1 = life.has_end ? life.end.time : xmax;
        y0 = life.ptr;
        y1 = life.ptr + life.size;
        z0 = life.start.caller;
        z1 = life.has_end ? life.end.caller : life.start.caller;
    }
};

void handle_action(AllocAction &action) {
    if (kAllocOpIsAllocation[(size_t)action.op]) {
        lifes.insert({
            (uintptr_t)action.ptr,
            {
                (uintptr_t)action.ptr,
                action.size,
                action.align,
                {
                    action.op,
                    action.time,
                    action.tid,
                    (uintptr_t)action.caller,
                },
                {},
                false,
            },
        });
    } else {
        auto it = lifes.find((uintptr_t)action.ptr);
        if (it != lifes.end()) {
            Life &life = it->second;
            life.has_end = true;
            life.end = {
                action.op,
                action.time,
                action.tid,
                (uintptr_t)action.caller,
            };
            dead.push_back(life);
            lifes.erase(it);
        }
    }
}

void gl_render() {
    std::unique_lock<std::mutex> lck(mtx);
    while (!actions.empty()) {
        AllocAction action = actions.front();
        actions.pop_front();
        lck.unlock();
        handle_action(action);
        lck.lock();
    }

    auto proj_mat = camState.projection_matrix();
    auto view_mat = camState.view_matrix();
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(&proj_mat[0][0]);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(&view_mat[0][0]);

    glViewport(0, 0, camState.width, camState.height);

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);

    int64_t xmin = std::numeric_limits<int64_t>::max();
    int64_t xmax = std::numeric_limits<int64_t>::min();
    uintptr_t ymin = std::numeric_limits<uintptr_t>::max();
    uintptr_t ymax = std::numeric_limits<uintptr_t>::min();
    uintptr_t zmin = std::numeric_limits<uintptr_t>::max();
    uintptr_t zmax = std::numeric_limits<uintptr_t>::min();

    auto filter = [](Life const &life) {
        return true;
        // return life.ptr <= 0x7400'0000'0000;
    };

    auto each = [&](auto &&func) {
        for (auto const &life: dead) {
            if (filter(life)) {
                func(life);
            }
        }
    };

    if (!dead.empty()) {
        each([&](Life const &life) {
            Rect rect(life, life.start.time);
            xmin = std::min(xmin, rect.x0);
            xmax = std::max(xmax, rect.x1);
            ymin = std::min(ymin, rect.y0);
            ymax = std::max(ymax, rect.y1);
            zmin = std::min(zmin, rect.z0);
            zmax = std::max(zmax, rect.z0);
            zmin = std::min(zmin, rect.z1);
            zmax = std::max(zmax, rect.z1);
        });

        zmax += (zmax - zmin) * 50;

        glColor3f(0.9f, 0.8f, 0.4f);
        double xs = 1.0 / (xmax - xmin);
        double ys = 1.0 / (ymax - ymin);
        double zs = 1.0 / (zmax - zmin);
        each([&](Life const &life) {
            Rect rect(life, xmax);
            double x0 = (rect.x0 - xmin) * xs;
            double x1 = (rect.x1 - xmin) * xs;
            double y0 = (rect.y0 - ymin) * ys;
            double y1 = (rect.y1 - ymin) * ys;
            double z0 = (rect.z0 - zmin) * zs;
            double z1 = (rect.z1 - zmin) * zs;
            glVertex3d(x0, y0, z0);
            glVertex3d(x0, y1, z0);
            glVertex3d(x1, y1, z1);
            glVertex3d(x1, y0, z1);
        });
    }

    glEnd();

    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
        char const *error = "???";
        switch (e) {
        case GL_INVALID_ENUM:      error = "GL_INVALID_ENUM"; break;
        case GL_INVALID_VALUE:     error = "GL_INVALID_VALUE"; break;
        case GL_INVALID_OPERATION: error = "GL_INVALID_OPERATION"; break;
        case GL_STACK_OVERFLOW:    error = "GL_STACK_OVERFLOW"; break;
        case GL_STACK_UNDERFLOW:   error = "GL_STACK_UNDERFLOW"; break;
        case GL_OUT_OF_MEMORY:     error = "GL_OUT_OF_MEMORY"; break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            error = "GL_INVALID_FRAMEBUFFER_OPERATION";
            break;
        case GL_CONTEXT_LOST: error = "GL_CONTEXT_LOST"; break;
        }
        std::cout << "OpenGL error: " << error << '\n';
    }
}

glm::dvec2 last_mouse;
bool has_last_mouse = false;

void gl_thread() {
    if (!glfwInit()) {
        char const *error = "???";
        glfwGetError(&error);
        std::cout << "Failed to initialize GLFW: " << error << '\n';
        return;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_SAMPLES, 16);

    camState.width = 800;
    camState.height = 600;
    GLFWwindow *window = glfwCreateWindow(camState.width, camState.height,
                                          "mallocVis", nullptr, nullptr);
    if (window == NULL) {
        char const *error = "???";
        glfwGetError(&error);
        std::cout << "Failed to create GLFW window: " << error << '\n';
        glfwTerminate();
        return;
    }
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwMakeContextCurrent(window);
    while (!glfwWindowShouldClose(window)) {
        glfwSwapBuffers(window);
        glfwPollEvents();
        gl_render();
    }
    glfwTerminate();
}

void io_thread() {
    std::string path = "malloc.fifo";
    if (access(path.c_str(), F_OK) == -1) {
        mkfifo(path.c_str(), 0666);
    }
    std::ifstream in(path, std::ios::binary);
    AllocAction action;
    while (in.read((char *)&action, sizeof(AllocAction))) {
        std::lock_guard<std::mutex> lck(mtx);
        actions.push_back(action);
        cv.notify_one();
    }
}

int main() {
    std::ios::sync_with_stdio(false);
    std::thread io_th(io_thread);
    std::thread gl_th(gl_thread);
    gl_th.join();
    io_th.join();
    return 0;
}
