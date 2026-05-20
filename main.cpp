#include <array>
#include <memory>
#include <algorithm>
#include <chrono>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cmath>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/timerfd.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <csignal>
#include <cerrno>
#include <poll.h>
#include <cctype>

#if !defined(BUILD_WAYLAND) && !defined(BUILD_XORG)
#error "Either BUILD_WAYLAND or BUILD_XORG must be defined"
#endif

#if defined(BUILD_WAYLAND)
#include <wayland-client.h>
#ifdef __cplusplus
#define namespace namespace_
#endif
#include "wlr-layer-shell-unstable-v1-client.h"
#ifdef __cplusplus
#undef namespace
#endif
#include <xkbcommon/xkbcommon.h>
#elif defined(BUILD_XORG)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cairo-xlib.h>
#endif

#include <cairo.h>
#include <pango/pangocairo.h>

#include "bar.hpp"
#include "config.hpp"

namespace {

volatile std::sig_atomic_t reload_requested = 0;

extern "C" void handle_sighup(int) {
    reload_requested = 1;
}

const char* config_path() {
    const char* home = std::getenv("HOME");
    if (!home) return nullptr;
    static std::array<char, 512> buf{};
    std::snprintf(buf.data(), buf.size(), "%s/.config/wlstatus/config", home);
    return buf.data();
}

#if defined(BUILD_WAYLAND)
void execute_command(const char* cmd) {
    pid_t pid = ::fork();
    if (pid == 0) {
        ::execl("/bin/sh", "sh", "-c", cmd, nullptr);
        ::_exit(1);
    }
}
#endif

} // anonymous namespace

// ============================================================================
//  Wayland Backend
// ============================================================================
#if defined(BUILD_WAYLAND)

namespace {

std::string read_command_output(const char* cmd) {
    std::array<char, 256> line{};
    std::string result;
    FILE* fp = ::popen(cmd, "r");
    if (fp) {
        while (::fgets(line.data(), static_cast<int>(line.size()), fp))
            result += line.data();
        ::pclose(fp);
    }
    return result;
}

int create_shared_fd(std::size_t size) {
    int fd = ::memfd_create("wlstatus", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (::ftruncate(fd, static_cast<off_t>(size)) < 0) { ::close(fd); return -1; }
    return fd;
}

} // anonymous namespace

// Listener variable declarations (defined after WlStatus class)
extern const zwlr_layer_surface_v1_listener popup_layer_surface_listener;
extern const zwlr_layer_surface_v1_listener tooltip_layer_surface_listener;
extern const zwlr_layer_surface_v1_listener layer_surface_listener;
extern const wl_keyboard_listener keyboard_listener;
extern const wl_pointer_listener pointer_listener;
extern const wl_seat_listener seat_listener;
extern const wl_registry_listener registry_listener;

class WlStatus final {
public:
    wl_display* display = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    zwlr_layer_shell_v1* layer_shell = nullptr;
    zwlr_layer_surface_v1* layer_surface = nullptr;
    wl_surface* surface = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_keyboard* keyboard = nullptr;
    xkb_context* xkb_ctx = nullptr;
    xkb_keymap* xkb_kmap = nullptr;
    xkb_state* xkb_kstate = nullptr;

    wl_buffer* buffer = nullptr;
    cairo_surface_t* cairo_surface = nullptr;
    cairo_t* cr = nullptr;
    std::byte* shm_data = nullptr;
    int width = 1920;
    int height = BAR_HEIGHT;

    Bar* bar = nullptr;
    Config* cfg = nullptr;

    std::array<TrackedWindow, MAX_TRACKED_WINDOWS> tracked_windows{};
    int n_tracked_windows = 0;

    bool configured = false;
    bool running = false;
    uint32_t configure_serial = 0;
    int pointer_x = 0;
    int pointer_y = 0;
    wl_surface* current_pointer_surface = nullptr;

    int timer_fd = -1;
    int inotify_fd = -1;
    int plugin_ifd = -1;
    int hypr_ev_fd = -1;

    struct TooltipState {
        wl_surface* surface = nullptr;
        zwlr_layer_surface_v1* layer_surface = nullptr;
        wl_buffer* buffer = nullptr;
        cairo_surface_t* cairo_surface = nullptr;
        cairo_t* cr = nullptr;
        std::byte* shm_data = nullptr;
        int width = 0;
        int height = 0;
        bool visible = false;
        bool configured = false;
        std::array<char, 512> text{};
        int hovered_clickable = -1;
        int hover_x = 0;
        int hover_y = 0;
    } tooltip;

    struct PopupState {
        wl_surface* surface = nullptr;
        zwlr_layer_surface_v1* layer_surface = nullptr;
        wl_buffer* buffer = nullptr;
        cairo_surface_t* cairo_surface = nullptr;
        cairo_t* cr = nullptr;
        std::byte* shm_data = nullptr;
        int width = 0;
        int height = 0;
        bool visible = false;
        bool configured = false;
        int action = 0;
        int hovered_btn = -1;
        int confirm_btn_x = 0, confirm_btn_y = 0;
        int confirm_btn_w = 0, confirm_btn_h = 0;
        int cancel_btn_x = 0, cancel_btn_y = 0;
        int cancel_btn_w = 0, cancel_btn_h = 0;
    } popup;

    WlStatus() = default;

    ~WlStatus() {
        cleanup();
    }

    WlStatus(const WlStatus&) = delete;
    WlStatus& operator=(const WlStatus&) = delete;

    void cleanup() {
        if (popup.visible) destroy_popup();
        if (tooltip.visible) destroy_tooltip();
        destroy_buffer();
        if (pointer) wl_pointer_destroy(pointer);
        if (keyboard) wl_keyboard_destroy(keyboard);
        if (seat) wl_seat_destroy(seat);
        if (layer_surface) zwlr_layer_surface_v1_destroy(layer_surface);
        if (surface) wl_surface_destroy(surface);
        if (layer_shell) zwlr_layer_shell_v1_destroy(layer_shell);
        if (compositor) wl_compositor_destroy(compositor);
        if (shm) wl_shm_destroy(shm);
        if (xkb_kstate) xkb_state_unref(xkb_kstate);
        if (xkb_kmap) xkb_keymap_unref(xkb_kmap);
        if (xkb_ctx) xkb_context_unref(xkb_ctx);
        if (bar) bar_destroy(bar);
        if (cfg) config_destroy(cfg);
        if (display) wl_display_disconnect(display);
        if (timer_fd >= 0) ::close(timer_fd);
        if (inotify_fd >= 0) ::close(inotify_fd);
        if (plugin_ifd >= 0) ::close(plugin_ifd);
        if (hypr_ev_fd >= 0) ::close(hypr_ev_fd);
    }

    int create_buffer() {
        if (buffer) return 0;
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
        int size = stride * height;
        int fd = create_shared_fd(static_cast<std::size_t>(size));
        if (fd < 0) return -1;

        wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
        buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
            stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);

        shm_data = static_cast<std::byte*>(::mmap(nullptr, size,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        ::close(fd);
        if (shm_data == MAP_FAILED) return -1;

        cairo_surface = cairo_image_surface_create_for_data(
            reinterpret_cast<unsigned char*>(shm_data), CAIRO_FORMAT_ARGB32,
            width, height, stride);
        cr = cairo_create(cairo_surface);
        return 0;
    }

    void destroy_buffer() {
        if (cr) { cairo_destroy(cr); cr = nullptr; }
        if (cairo_surface) { cairo_surface_destroy(cairo_surface); cairo_surface = nullptr; }
        if (shm_data) {
            int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
            ::munmap(shm_data, static_cast<std::size_t>(stride) * height);
            shm_data = nullptr;
        }
        if (buffer) { wl_buffer_destroy(buffer); buffer = nullptr; }
    }

    void render() {
        bar_render(bar, cr);
        cairo_surface_flush(cairo_surface);
        wl_surface_attach(surface, buffer, 0, 0);
        wl_surface_damage_buffer(surface, 0, 0, width, height);
        wl_surface_commit(surface);

        if (popup.visible && popup.buffer && popup.surface) {
            wl_surface_attach(popup.surface, popup.buffer, 0, 0);
            wl_surface_damage_buffer(popup.surface, 0, 0, popup.width, popup.height);
            wl_surface_commit(popup.surface);
        }
    }

    int popup_create_buffer() {
        if (popup.buffer) return 0;
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, popup.width);
        int size = stride * popup.height;
        int fd = create_shared_fd(static_cast<std::size_t>(size));
        if (fd < 0) return -1;

        wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
        popup.buffer = wl_shm_pool_create_buffer(pool, 0,
            popup.width, popup.height, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);

        popup.shm_data = static_cast<std::byte*>(::mmap(nullptr, size,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        ::close(fd);
        if (popup.shm_data == MAP_FAILED) return -1;

        popup.cairo_surface = cairo_image_surface_create_for_data(
            reinterpret_cast<unsigned char*>(popup.shm_data), CAIRO_FORMAT_ARGB32,
            popup.width, popup.height, stride);
        popup.cr = cairo_create(popup.cairo_surface);
        return 0;
    }

    void popup_destroy_buffer() {
        if (popup.cr) { cairo_destroy(popup.cr); popup.cr = nullptr; }
        if (popup.cairo_surface) { cairo_surface_destroy(popup.cairo_surface); popup.cairo_surface = nullptr; }
        if (popup.shm_data) {
            int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, popup.width);
            ::munmap(popup.shm_data, static_cast<std::size_t>(stride) * popup.height);
            popup.shm_data = nullptr;
        }
        if (popup.buffer) { wl_buffer_destroy(popup.buffer); popup.buffer = nullptr; }
    }

    void popup_render() {
        cairo_t* pc = popup.cr;
        int w = popup.width, h = popup.height;

        cairo_set_operator(pc, CAIRO_OPERATOR_CLEAR);
        cairo_paint(pc);
        cairo_set_operator(pc, CAIRO_OPERATOR_OVER);

        draw_rounded_rect(pc, 0, 0, w, h, 8);
        cairo_set_source_rgba(pc, 0.12, 0.12, 0.22, 0.96);
        cairo_fill(pc);

        cairo_set_source_rgba(pc, 0.0, 0.90, 1.0, 0.3);
        cairo_set_line_width(pc, 1);
        draw_rounded_rect(pc, 0, 0, w, h, 8);
        cairo_stroke(pc);

        const char* ff = config_get(cfg, "font_family", "Sans");
        std::array<char, 64> popup_title_font{};
        std::snprintf(popup_title_font.data(), popup_title_font.size(), "%s Bold 13", ff);

        PangoLayout* lay = pango_cairo_create_layout(pc);
        PangoFontDescription* pfd = pango_font_description_from_string(popup_title_font.data());
        pango_layout_set_font_description(lay, pfd);
        pango_font_description_free(pfd);

        static const char* labels[] = {"Power Off", "Reboot", "Suspend"};
        pango_layout_set_text(lay, labels[popup.action], -1);
        int tw, th;
        pango_layout_get_pixel_size(lay, &tw, &th);
        cairo_set_source_rgb(pc, 1, 1, 1);
        cairo_move_to(pc, (w - tw) / 2, 24);
        pango_cairo_show_layout(pc, lay);

        int btn_w = 65, btn_h = 28, btn_gap = 12;
        int btn_y = h - btn_h - 14;
        int confirm_x = (w - btn_w * 2 - btn_gap) / 2;
        int cancel_x = confirm_x + btn_w + btn_gap;

        bool con_hover = (popup.hovered_btn == 0);
        bool can_hover = (popup.hovered_btn == 1);

        cairo_set_source_rgba(pc, 0.2, 0.8, 0.3, con_hover ? 0.9 : 0.6);
        draw_rounded_rect(pc, confirm_x, btn_y, btn_w, btn_h, 5);
        cairo_fill(pc);

        cairo_set_source_rgba(pc, 0.9, 0.2, 0.2, can_hover ? 0.9 : 0.6);
        draw_rounded_rect(pc, cancel_x, btn_y, btn_w, btn_h, 5);
        cairo_fill(pc);

        std::array<char, 64> btn_font{};
        std::snprintf(btn_font.data(), btn_font.size(), "%s Bold 11", ff);
        PangoFontDescription* fb = pango_font_description_from_string(btn_font.data());

        PangoLayout* lc = pango_cairo_create_layout(pc);
        pango_layout_set_font_description(lc, fb);
        pango_layout_set_text(lc, "Confirm", -1);
        int cw, ch;
        pango_layout_get_pixel_size(lc, &cw, &ch);
        cairo_set_source_rgb(pc, 1, 1, 1);
        cairo_move_to(pc, confirm_x + (btn_w - cw) / 2, btn_y + (btn_h - ch) / 2 + 1);
        pango_cairo_show_layout(pc, lc);

        PangoLayout* lx = pango_cairo_create_layout(pc);
        pango_layout_set_font_description(lx, fb);
        pango_layout_set_text(lx, "Cancel", -1);
        pango_layout_get_pixel_size(lx, &cw, &ch);
        cairo_move_to(pc, cancel_x + (btn_w - cw) / 2, btn_y + (btn_h - ch) / 2 + 1);
        pango_cairo_show_layout(pc, lx);

        pango_font_description_free(fb);
        g_object_unref(lc);
        g_object_unref(lx);
        g_object_unref(lay);

        popup.confirm_btn_x = confirm_x;
        popup.confirm_btn_y = btn_y;
        popup.confirm_btn_w = btn_w;
        popup.confirm_btn_h = btn_h;
        popup.cancel_btn_x = cancel_x;
        popup.cancel_btn_y = btn_y;
        popup.cancel_btn_w = btn_w;
        popup.cancel_btn_h = btn_h;

        cairo_surface_flush(popup.cairo_surface);
        wl_surface_attach(popup.surface, popup.buffer, 0, 0);
        wl_surface_damage_buffer(popup.surface, 0, 0, w, h);
        wl_surface_commit(popup.surface);
    }

    void destroy_popup() {
        if (!popup.visible) return;
        popup.visible = false;
        popup_destroy_buffer();
        if (popup.layer_surface) {
            zwlr_layer_surface_v1_destroy(popup.layer_surface);
            popup.layer_surface = nullptr;
        }
        if (popup.surface) {
            wl_surface_destroy(popup.surface);
            popup.surface = nullptr;
        }
        popup.configured = false;
    }

    void create_popup(int action) {
        if (popup.visible) destroy_popup();

        popup = PopupState{};
        popup.width = 175;
        popup.height = 105;
        popup.action = action;
        popup.hovered_btn = -1;

        popup.surface = wl_compositor_create_surface(compositor);
        popup.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            layer_shell, popup.surface, nullptr,
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlstatus-popup");

        zwlr_layer_surface_v1_add_listener(popup.layer_surface,
            &popup_layer_surface_listener, this);

        zwlr_layer_surface_v1_set_size(popup.layer_surface, popup.width, popup.height);
        zwlr_layer_surface_v1_set_anchor(popup.layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

        const char* anchor_str = config_get(cfg, "bar_anchor", "top");
        bool bar_on_bottom = (std::strcmp(anchor_str, "bottom") == 0);
        if (bar_on_bottom) {
            zwlr_layer_surface_v1_set_anchor(popup.layer_surface,
                ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
            zwlr_layer_surface_v1_set_margin(popup.layer_surface,
                0, BAR_PADDING, height + 4, 0);
        } else {
            zwlr_layer_surface_v1_set_margin(popup.layer_surface,
                height + 4, BAR_PADDING, 0, 0);
        }
        zwlr_layer_surface_v1_set_exclusive_zone(popup.layer_surface, 0);

        popup.visible = true;
        wl_surface_commit(popup.surface);
        wl_display_roundtrip(display);
    }

    void destroy_tooltip() {
        if (!tooltip.visible) return;
        tooltip.visible = false;
        tooltip.hovered_clickable = -1;
        if (tooltip.buffer) {
            if (tooltip.cr) cairo_destroy(tooltip.cr);
            tooltip.cr = nullptr;
            if (tooltip.cairo_surface) cairo_surface_destroy(tooltip.cairo_surface);
            tooltip.cairo_surface = nullptr;
            if (tooltip.shm_data) {
                int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, tooltip.width);
                ::munmap(tooltip.shm_data, static_cast<std::size_t>(stride) * tooltip.height);
            }
            tooltip.shm_data = nullptr;
            wl_buffer_destroy(tooltip.buffer);
            tooltip.buffer = nullptr;
        }
        if (tooltip.layer_surface) {
            zwlr_layer_surface_v1_destroy(tooltip.layer_surface);
            tooltip.layer_surface = nullptr;
        }
        if (tooltip.surface) {
            wl_surface_destroy(tooltip.surface);
            tooltip.surface = nullptr;
        }
        tooltip.configured = false;
    }

    void show_tooltip(const char* text, int hover_x, int hover_y) {
        if (tooltip.visible && std::strcmp(tooltip.text.data(), text) == 0)
            return;

        destroy_tooltip();

        tooltip = TooltipState{};
        tooltip.hover_x = hover_x;
        tooltip.hover_y = hover_y;
        tooltip.hovered_clickable = 0;
        std::strncpy(tooltip.text.data(), text, tooltip.text.size() - 1);

        int nlines = 1;
        for (const char* p = text; *p; p++)
            if (*p == '\n') nlines++;
        tooltip.width = 280;
        tooltip.height = nlines * 16 + 14;

        tooltip.surface = wl_compositor_create_surface(compositor);
        tooltip.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            layer_shell, tooltip.surface, nullptr,
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlstatus-tooltip");

        zwlr_layer_surface_v1_add_listener(tooltip.layer_surface,
            &tooltip_layer_surface_listener, this);

        zwlr_layer_surface_v1_set_size(tooltip.layer_surface, tooltip.width, tooltip.height);
        zwlr_layer_surface_v1_set_anchor(tooltip.layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

        const char* anchor_str = config_get(cfg, "bar_anchor", "top");
        bool bar_on_bottom = (std::strcmp(anchor_str, "bottom") == 0);
        if (bar_on_bottom)
            zwlr_layer_surface_v1_set_margin(tooltip.layer_surface,
                hover_y - tooltip.height - 4, BAR_PADDING, 0, 0);
        else
            zwlr_layer_surface_v1_set_margin(tooltip.layer_surface,
                height + 4, BAR_PADDING, 0, 0);

        zwlr_layer_surface_v1_set_exclusive_zone(tooltip.layer_surface, 0);
        tooltip.visible = true;
        wl_surface_commit(tooltip.surface);
    }

    int hypr_connect_socket(const char* sock_name) {
        const char* xdg = std::getenv("XDG_RUNTIME_DIR");
        const char* inst = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
        if (!xdg || !inst) return -1;

        std::array<char, 256> path{};
        std::snprintf(path.data(), path.size(), "%s/hypr/%s/%s", xdg, inst, sock_name);

        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;

        ::fcntl(fd, F_SETFD, FD_CLOEXEC);

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.data());

        if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd);
            return -1;
        }

        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        return fd;
    }

    int hypr_connect_event() {
        return hypr_connect_socket(".socket2.sock");
    }

    int hypr_send_command(const char* cmd) {
        int fd = hypr_connect_socket(".socket.sock");
        if (fd < 0) return -1;
        ::write(fd, cmd, std::strlen(cmd));
        constexpr char nl = '\n';
        ::write(fd, &nl, 1);
        std::array<char, 4096> buf{};
        int n = static_cast<int>(::read(fd, buf.data(), buf.size() - 1));
        ::close(fd);
        return n;
    }

    void hypr_disconnect() {
        if (hypr_ev_fd >= 0) {
            ::close(hypr_ev_fd);
            hypr_ev_fd = -1;
        }
    }

    bool hypr_read_events() {
        std::array<char, 4096> buf{};
        ssize_t n = ::read(hypr_ev_fd, buf.data(), buf.size() - 1);
        if (n <= 0) {
            if (n == 0 || (n < 0 && errno == ECONNRESET))
                hypr_disconnect();
            return false;
        }
        buf[n] = '\0';

        bool need_update = false;
        char* p = buf.data();
        while (p && *p) {
            char* newline = std::strchr(p, '\n');
            if (newline) *newline = '\0';

            char* delim = std::strstr(p, ">>");
            if (delim) {
                *delim = '\0';
                const char* event = p;
                const char* data = delim + 2;

                if (std::strcmp(event, "activewindow") == 0) {
                    const char* comma = std::strchr(data, ',');
                    if (comma) {
                        std::size_t class_len = static_cast<std::size_t>(comma - data);
                        if (class_len > 63) class_len = 63;
                        std::array<char, 64> cls{};
                        std::memcpy(cls.data(), data, class_len);
                        const char* title = comma + 1;
                        while (*title == ' ') title++;
                        bar_set_active_window(bar, cls.data(), title);
                    }
                    need_update = true;
                } else if (std::strcmp(event, "windowtitle") == 0) {
                    bar_set_active_window(bar, nullptr, data);
                    need_update = true;
                } else if (std::strcmp(event, "openwindow") == 0) {
                    std::array<char, 32> addr{};
                    std::array<char, 64> cls{};
                    int ws_id;
                    if (std::sscanf(data, "%31[^,],%d,%63[^,]", addr.data(), &ws_id, cls.data()) >= 2)
                        track_window_add(addr.data(), ws_id, cls.data());
                    need_update = true;
                } else if (std::strcmp(event, "closewindow") == 0) {
                    track_window_remove(data);
                    need_update = true;
                } else if (std::strcmp(event, "movewindow") == 0) {
                    std::array<char, 32> addr{};
                    int ws_id;
                    if (std::sscanf(data, "%31[^,],%d", addr.data(), &ws_id) == 2)
                        track_window_move(addr.data(), ws_id);
                    need_update = true;
                } else if (std::strcmp(event, "movetoworkspace") == 0) {
                    std::array<char, 32> addr{};
                    int ws_id;
                    if (std::sscanf(data, "%d,%31s", &ws_id, addr.data()) == 2)
                        track_window_move(addr.data(), ws_id);
                    need_update = true;
                } else if (std::strcmp(event, "workspace") == 0 ||
                           std::strcmp(event, "workspacev2") == 0 ||
                           std::strcmp(event, "focusedmon") == 0) {
                    need_update = true;
                }
            }

            p = newline ? newline + 1 : nullptr;
        }

        return need_update;
    }

    void track_window_add(const char* address, int workspace_id, const char* cls) {
        if (n_tracked_windows >= MAX_TRACKED_WINDOWS) return;
        auto& tw = tracked_windows[n_tracked_windows++];
        std::snprintf(tw.address, sizeof(tw.address), "%s", address);
        tw.workspace_id = workspace_id;
        std::snprintf(tw.cls, sizeof(tw.cls), "%s", cls);
    }

    void track_window_remove(const char* address) {
        for (int i = 0; i < n_tracked_windows; i++) {
            if (std::strcmp(tracked_windows[i].address, address) == 0) {
                tracked_windows[i] = tracked_windows[--n_tracked_windows];
                return;
            }
        }
    }

    void track_window_move(const char* address, int workspace_id) {
        for (auto& tw : tracked_windows) {
            if (tw.address[0] == '\0') break;
            if (std::strcmp(tw.address, address) == 0) {
                tw.workspace_id = workspace_id;
                return;
            }
        }
    }

    void init_window_tracking() {
        n_tracked_windows = 0;
        std::string output = read_command_output("hyprctl clients 2>/dev/null");
        if (output.empty()) return;

        std::array<char, 32> cur_addr{};
        int cur_ws = -1;

        char* line = output.data();
        char* saveptr;
        char* tok = ::strtok_r(line, "\n", &saveptr);
        while (tok) {
            std::array<char, 32> addr{};
            int ws_id;
            if (std::sscanf(tok, "Window %31s -> workspace ID %d", addr.data(), &ws_id) == 2) {
                std::snprintf(cur_addr.data(), cur_addr.size(), "%s", addr.data());
                cur_ws = ws_id;
            } else if (cur_addr[0]) {
                std::array<char, 64> cls{};
                if (std::sscanf(tok, "\tclass: %63[^\n]", cls.data()) == 1) {
                    track_window_add(cur_addr.data(), cur_ws, cls.data());
                    cur_addr[0] = '\0';
                }
            }
            tok = ::strtok_r(nullptr, "\n", &saveptr);
        }

        bar_update_workspace_names(bar, tracked_windows.data(), n_tracked_windows);
    }

    void setup_plugin_watches() {
        if (!bar) return;
        plugin_ifd = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        if (plugin_ifd < 0) return;

        for (int i = 0; i < bar->n_lua_plugins; i++) {
            std::array<char, 32> watchkey{};
            std::snprintf(watchkey.data(), watchkey.size(), "lua_plugin_%d_watch", i + 1);
            const char* watch = config_get(cfg, watchkey.data(), "");
            if (watch[0]) {
                int wd = ::inotify_add_watch(plugin_ifd, watch, IN_MODIFY);
                if (wd >= 0) bar->lua_plugins[i].watch_wd = wd;
            }
        }

        const char* plugins_dir = config_get(cfg, "lua_plugins_dir", nullptr);
        if (!plugins_dir) {
            const char* home = std::getenv("HOME");
            if (home) {
                static std::array<char, 256> dir{};
                std::snprintf(dir.data(), dir.size(), "%s/.config/wlstatus/plugins", home);
                plugins_dir = dir.data();
            }
        }

        if (plugins_dir) {
            auto add_watch = [&](int idx, const char* sysfs) {
                if (idx < 0 || idx >= bar->n_lua_plugins) return;
                if (bar->lua_plugins[idx].watch_wd >= 0) return;
                if (::access(sysfs, F_OK) != 0) return;
                int wd = ::inotify_add_watch(plugin_ifd, sysfs, IN_MODIFY);
                if (wd >= 0) bar->lua_plugins[idx].watch_wd = wd;
            };

            for (int i = 0; i < bar->n_lua_plugins; i++) {
                const char* pname = bar->lua_plugins[i].path;
                if (!pname[0]) continue;

                if (std::strstr(pname, "battery")) {
                    add_watch(i, "/sys/class/power_supply/BAT0/uevent");
                } else if (std::strstr(pname, "brightness")) {
                    static const char* backlights[] = {
                        "/sys/class/backlight/intel_backlight/brightness",
                        "/sys/class/backlight/amdgpu_bl0/brightness",
                        "/sys/class/backlight/nvidia_0/brightness",
                        nullptr
                    };
                    for (int b = 0; backlights[b]; b++) {
                        if (::access(backlights[b], F_OK) == 0) {
                            add_watch(i, backlights[b]);
                            break;
                        }
                    }
                } else if (std::strstr(pname, "network") || std::strstr(pname, "net")) {
                    add_watch(i, "/sys/class/net");
                }
            }
        }
    }

    void reload() {
        if (popup.visible) destroy_popup();
        if (tooltip.visible) destroy_tooltip();
        destroy_buffer();
        if (bar) bar_destroy(bar);
        config_destroy(cfg);
        if (plugin_ifd >= 0) { ::close(plugin_ifd); plugin_ifd = -1; }
        cfg = config_load(config_path());
        bar = bar_create(width, height, cfg);
        create_buffer();
        setup_plugin_watches();
        bar_update_workspaces(bar);
        init_window_tracking();
        bar_update_lua_plugins(bar);
        render();
    }

    void on_timer() {
        if (!bar || !running) return;

        if (hypr_ev_fd < 0) {
            hypr_ev_fd = hypr_connect_event();
            if (hypr_ev_fd < 0)
                bar_update_workspaces(bar);
        }

        bar_update_lua_plugins(bar);
        render();
    }

    bool setup_layer_surface() {
        surface = wl_compositor_create_surface(compositor);
        if (!surface) return false;

        const char* layer_str = config_get(cfg, "bar_layer", "top");
        auto layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
        if (std::strcmp(layer_str, "overlay") == 0)
            layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;

        layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            layer_shell, surface, nullptr, layer, "wlstatus");
        if (!layer_surface) return false;

        zwlr_layer_surface_v1_add_listener(layer_surface,
            &layer_surface_listener, this);

        int bh = config_get_int(cfg, "bar_height", BAR_HEIGHT);
        zwlr_layer_surface_v1_set_size(layer_surface, 0, bh);

        uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                          ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
        const char* anchor_str = config_get(cfg, "bar_anchor", "top");
        if (std::strcmp(anchor_str, "bottom") == 0)
            anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
        else
            anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
        zwlr_layer_surface_v1_set_anchor(layer_surface, anchor);

        if (layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)
            zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, 0);
        else
            zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, bh);

        wl_surface_commit(surface);
        wl_display_roundtrip(display);

        if (!configured) {
            std::fprintf(stderr, "surface was not configured\n");
            return false;
        }
        return true;
    }
};

// Wayland listener callbacks (static, dispatch into WlStatus)

static void popup_layer_surface_configure(void* data,
    zwlr_layer_surface_v1* surface, uint32_t serial,
    uint32_t width, uint32_t height) {
    auto* ws = static_cast<WlStatus*>(data);
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    if (width > 0) ws->popup.width = static_cast<int>(width);
    if (height > 0) ws->popup.height = static_cast<int>(height);
    if (!ws->popup.buffer) {
        ws->popup_create_buffer();
        ws->popup_render();
    }
    ws->popup.configured = true;
}

static void popup_layer_surface_closed(void* data,
    zwlr_layer_surface_v1*) {
    static_cast<WlStatus*>(data)->popup.visible = false;
}

const zwlr_layer_surface_v1_listener popup_layer_surface_listener = {
    .configure = popup_layer_surface_configure,
    .closed = popup_layer_surface_closed,
};

static void tooltip_layer_surface_configure(void* data,
    zwlr_layer_surface_v1* surface, uint32_t serial,
    uint32_t width, uint32_t height) {
    auto* ws = static_cast<WlStatus*>(data);
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    if (width > 0) ws->tooltip.width = static_cast<int>(width);
    if (height > 0) ws->tooltip.height = static_cast<int>(height);
    if (!ws->tooltip.buffer) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->tooltip.width);
        int size = stride * ws->tooltip.height;
        int shm_fd = create_shared_fd(static_cast<std::size_t>(size));
        if (shm_fd < 0) return;

        wl_shm_pool* pool = wl_shm_create_pool(ws->shm, shm_fd, size);
        ws->tooltip.buffer = wl_shm_pool_create_buffer(pool, 0,
            ws->tooltip.width, ws->tooltip.height, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);

        ws->tooltip.shm_data = static_cast<std::byte*>(::mmap(nullptr, size,
            PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
        ::close(shm_fd);
        if (ws->tooltip.shm_data == MAP_FAILED) { ws->tooltip.shm_data = nullptr; return; }

        ws->tooltip.cairo_surface = cairo_image_surface_create_for_data(
            reinterpret_cast<unsigned char*>(ws->tooltip.shm_data),
            CAIRO_FORMAT_ARGB32, ws->tooltip.width, ws->tooltip.height, stride);
        ws->tooltip.cr = cairo_create(ws->tooltip.cairo_surface);

        cairo_t* tc = ws->tooltip.cr;
        cairo_set_operator(tc, CAIRO_OPERATOR_CLEAR);
        cairo_paint(tc);
        cairo_set_operator(tc, CAIRO_OPERATOR_OVER);
        draw_rounded_rect(tc, 0, 0, ws->tooltip.width, ws->tooltip.height, 6);
        cairo_set_source_rgba(tc, 0.10, 0.10, 0.18, 0.96);
        cairo_fill(tc);

        const char* tip_ff = config_get(ws->cfg, "font_family", "Sans");
        std::array<char, 64> tip_font{};
        std::snprintf(tip_font.data(), tip_font.size(), "%s 10", tip_ff);

        PangoLayout* lay = pango_cairo_create_layout(tc);
        PangoFontDescription* fdesc = pango_font_description_from_string(tip_font.data());
        pango_layout_set_font_description(lay, fdesc);
        pango_font_description_free(fdesc);
        pango_layout_set_text(lay, ws->tooltip.text.data(), -1);
        cairo_set_source_rgb(tc, 1, 1, 1);
        cairo_move_to(tc, 6, 6);
        pango_cairo_show_layout(tc, lay);
        g_object_unref(lay);

        cairo_set_source_rgba(tc, 0.0, 0.90, 1.0, 0.3);
        cairo_set_line_width(tc, 1);
        draw_rounded_rect(tc, 0, 0, ws->tooltip.width, ws->tooltip.height, 6);
        cairo_stroke(tc);

        cairo_surface_flush(ws->tooltip.cairo_surface);
    }
    ws->tooltip.configured = true;
    if (ws->tooltip.buffer && ws->tooltip.surface) {
        wl_surface_attach(ws->tooltip.surface, ws->tooltip.buffer, 0, 0);
        wl_surface_damage_buffer(ws->tooltip.surface, 0, 0, ws->tooltip.width, ws->tooltip.height);
        wl_surface_commit(ws->tooltip.surface);
    }
}

static void tooltip_layer_surface_closed(void* data,
    zwlr_layer_surface_v1*) {
    static_cast<WlStatus*>(data)->tooltip.visible = false;
}

const zwlr_layer_surface_v1_listener tooltip_layer_surface_listener = {
    .configure = tooltip_layer_surface_configure,
    .closed = tooltip_layer_surface_closed,
};

static void layer_surface_configure(void* data,
    zwlr_layer_surface_v1* surface, uint32_t serial,
    uint32_t width, uint32_t height) {
    auto* ws = static_cast<WlStatus*>(data);
    ws->configure_serial = serial;

    if (width == 0) width = static_cast<uint32_t>(ws->width);
    if (height == 0) height = static_cast<uint32_t>(config_get_int(ws->cfg, "bar_height", BAR_HEIGHT));

    if (ws->width != static_cast<int>(width) || ws->height != static_cast<int>(height)) {
        ws->destroy_buffer();
        ws->width = static_cast<int>(width);
        ws->height = static_cast<int>(height);
        if (ws->bar) bar_destroy(ws->bar);
        ws->bar = bar_create(ws->width, ws->height, ws->cfg);
    }

    if (!ws->bar) ws->bar = bar_create(ws->width, ws->height, ws->cfg);
    ws->create_buffer();
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    ws->configured = true;

    bar_update_workspaces(ws->bar);
    ws->render();
}

static void layer_surface_closed(void* data, zwlr_layer_surface_v1*) {
    static_cast<WlStatus*>(data)->running = false;
}

const zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void pointer_enter(void* data, wl_pointer*,
    uint32_t serial, wl_surface* surface,
    wl_fixed_t sx, wl_fixed_t sy) {
    auto* ws = static_cast<WlStatus*>(data);
    ws->current_pointer_surface = surface;
    ws->pointer_x = wl_fixed_to_int(sx);
    ws->pointer_y = wl_fixed_to_int(sy);

    if (ws->popup.visible && surface == ws->popup.surface)
        return;

    bar_update_hover(ws->bar, ws->pointer_x, ws->pointer_y);
    ws->render();

    for (int i = 0; i < ws->bar->n_clickables; i++) {
        auto* c = &ws->bar->clickables[i];
        if (!(ws->pointer_x >= c->x && ws->pointer_x < c->x + c->w &&
              ws->pointer_y >= c->y && ws->pointer_y < c->y + c->h))
            continue;

        if (c->tooltip_text[0]) {
            ws->tooltip.hovered_clickable = i;
            ws->show_tooltip(c->tooltip_text, ws->pointer_x, ws->pointer_y);
        } else if (c->tooltip_cmd[0]) {
            std::string out = read_command_output(c->tooltip_cmd);
            if (!out.empty()) {
                ws->tooltip.hovered_clickable = i;
                ws->show_tooltip(out.c_str(), ws->pointer_x, ws->pointer_y);
            }
        }
        break;
    }
}

static void pointer_leave(void* data, wl_pointer*,
    uint32_t, wl_surface* surface) {
    auto* ws = static_cast<WlStatus*>(data);
    ws->current_pointer_surface = nullptr;

    if (ws->popup.visible && surface == ws->popup.surface) {
        if (ws->popup.hovered_btn != -1) {
            ws->popup.hovered_btn = -1;
            ws->popup_render();
        }
        return;
    }

    bar_clear_hover(ws->bar);
    ws->render();
    if (ws->tooltip.visible)
        ws->destroy_tooltip();
}

static void pointer_motion(void* data, wl_pointer*,
    uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
    auto* ws = static_cast<WlStatus*>(data);
    int x = wl_fixed_to_int(sx);
    int y = wl_fixed_to_int(sy);
    ws->pointer_x = x;
    ws->pointer_y = y;

    if (ws->popup.visible && ws->current_pointer_surface == ws->popup.surface) {
        int old = ws->popup.hovered_btn;
        ws->popup.hovered_btn = -1;
        if (x >= ws->popup.confirm_btn_x && x < ws->popup.confirm_btn_x + ws->popup.confirm_btn_w &&
            y >= ws->popup.confirm_btn_y && y < ws->popup.confirm_btn_y + ws->popup.confirm_btn_h)
            ws->popup.hovered_btn = 0;
        else if (x >= ws->popup.cancel_btn_x && x < ws->popup.cancel_btn_x + ws->popup.cancel_btn_w &&
            y >= ws->popup.cancel_btn_y && y < ws->popup.cancel_btn_y + ws->popup.cancel_btn_h)
            ws->popup.hovered_btn = 1;
        if (old != ws->popup.hovered_btn)
            ws->popup_render();
        return;
    }

    int old_power = ws->bar->power_hovered;
    int old_ws_idx = ws->bar->hovered_workspace;
    bar_update_hover(ws->bar, x, y);
    if (old_power != ws->bar->power_hovered ||
        old_ws_idx != ws->bar->hovered_workspace)
        ws->render();

    int found_tooltip = -1;
    for (int i = 0; i < ws->bar->n_clickables; i++) {
        auto* c = &ws->bar->clickables[i];
        if ((c->tooltip_cmd[0] || c->tooltip_text[0]) &&
            x >= c->x && x < c->x + c->w &&
            y >= c->y && y < c->y + c->h) {
            found_tooltip = i;
            break;
        }
    }

    if (found_tooltip >= 0 && found_tooltip != ws->tooltip.hovered_clickable) {
        auto* c = &ws->bar->clickables[found_tooltip];
        if (c->tooltip_text[0]) {
            ws->tooltip.hovered_clickable = found_tooltip;
            ws->show_tooltip(c->tooltip_text, x, y);
        } else {
            std::string out = read_command_output(c->tooltip_cmd);
            if (!out.empty()) {
                ws->tooltip.hovered_clickable = found_tooltip;
                ws->show_tooltip(out.c_str(), x, y);
            }
        }
    } else if (found_tooltip < 0 && ws->tooltip.visible) {
        ws->destroy_tooltip();
    }
}

static void pointer_button(void* data, wl_pointer*,
    uint32_t, uint32_t, uint32_t button, uint32_t state) {
    auto* ws = static_cast<WlStatus*>(data);
    if (state != 1) return;
    if (button != 0x110) return;

    int x = ws->pointer_x, y = ws->pointer_y;

    if (ws->popup.visible) {
        if (ws->current_pointer_surface == ws->popup.surface) {
            if (x >= ws->popup.confirm_btn_x && x < ws->popup.confirm_btn_x + ws->popup.confirm_btn_w &&
                y >= ws->popup.confirm_btn_y && y < ws->popup.confirm_btn_y + ws->popup.confirm_btn_h) {
                static const char* cmds[] = {"systemctl poweroff", "systemctl reboot", "systemctl suspend"};
                execute_command(cmds[ws->popup.action]);
                ws->destroy_popup();
                return;
            }
            if (x >= ws->popup.cancel_btn_x && x < ws->popup.cancel_btn_x + ws->popup.cancel_btn_w &&
                y >= ws->popup.cancel_btn_y && y < ws->popup.cancel_btn_y + ws->popup.cancel_btn_h) {
                ws->destroy_popup();
                return;
            }
        } else {
            ws->destroy_popup();
        }
    }

    for (int i = 0; i < ws->bar->n_clickables; i++) {
        auto* c = &ws->bar->clickables[i];
        if (x >= c->x && x < c->x + c->w &&
            y >= c->y && y < c->y + c->h) {
            switch (c->action) {
            case CLICK_POWEROFF:
                ws->create_popup(0);
                break;
            case CLICK_REBOOT:
                ws->create_popup(1);
                break;
            case CLICK_SUSPEND:
                ws->create_popup(2);
                break;
            case CLICK_HYPRCTL: {
                const char* dispatch = std::strstr(c->command, "dispatch");
                if (dispatch)
                    ws->hypr_send_command(dispatch);
                else
                    execute_command(c->command);
                break;
            }
            case CLICK_RUN:
                execute_command(c->command);
                break;
            default:
                break;
            }
            break;
        }
    }
}

static void pointer_axis(void* data, wl_pointer*,
    uint32_t, uint32_t axis, wl_fixed_t value) {
    auto* ws = static_cast<WlStatus*>(data);
    if (axis != 0) return;

    int x = ws->pointer_x, y = ws->pointer_y;
    for (int i = 0; i < ws->bar->n_clickables; i++) {
        auto* c = &ws->bar->clickables[i];
        if (x >= c->x && x < c->x + c->w &&
            y >= c->y && y < c->y + c->h) {
            if (c->lua_plugin_idx >= 0) {
                int direction = (value > 0) ? -1 : 1;
                lua_plugin_call_onscroll(
                    &ws->bar->lua_plugins[c->lua_plugin_idx], direction);
                bar_update_lua_plugins(ws->bar);
                ws->render();
            }
            break;
        }
    }
}

static void pointer_frame(void*, wl_pointer*) {}

static void keyboard_keymap(void* data, wl_keyboard*,
    uint32_t format, int fd, uint32_t size) {
    auto* ws = static_cast<WlStatus*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { ::close(fd); return; }
    auto* map = static_cast<char*>(::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (map == MAP_FAILED) { ::close(fd); return; }
    ws->xkb_kmap = xkb_keymap_new_from_string(ws->xkb_ctx, map,
        XKB_KEYMAP_FORMAT_TEXT_V1, static_cast<xkb_keymap_compile_flags>(0));
    ::munmap(map, size);
    ::close(fd);
    if (!ws->xkb_kmap) return;
    ws->xkb_kstate = xkb_state_new(ws->xkb_kmap);
}

static void keyboard_enter(void*, wl_keyboard*,
    uint32_t, wl_surface*, wl_array*) {}
static void keyboard_leave(void*, wl_keyboard*,
    uint32_t, wl_surface*) {}
static void keyboard_key(void*, wl_keyboard*,
    uint32_t, uint32_t, uint32_t, uint32_t) {}

static void keyboard_modifiers(void* data, wl_keyboard*,
    uint32_t, uint32_t mods_depressed, uint32_t mods_latched,
    uint32_t mods_locked, uint32_t group) {
    auto* ws = static_cast<WlStatus*>(data);
    if (ws->xkb_kstate)
        xkb_state_update_mask(ws->xkb_kstate, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void keyboard_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}

const wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

const wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
};

static void seat_capabilities(void* data, wl_seat* seat,
    uint32_t capabilities) {
    auto* ws = static_cast<WlStatus*>(data);
    if ((capabilities & 1) && !ws->pointer) {
        ws->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(ws->pointer, &pointer_listener, ws);
    } else if (!(capabilities & 1) && ws->pointer) {
        wl_pointer_destroy(ws->pointer);
        ws->pointer = nullptr;
    }
    if ((capabilities & 2) && !ws->keyboard) {
        ws->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(ws->keyboard, &keyboard_listener, ws);
    } else if (!(capabilities & 2) && ws->keyboard) {
        wl_keyboard_destroy(ws->keyboard);
        ws->keyboard = nullptr;
    }
}

static void seat_name(void*, wl_seat*, const char*) {}

const wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void registry_global(void* data, wl_registry* registry,
    uint32_t name, const char* interface, uint32_t) {
    auto* ws = static_cast<WlStatus*>(data);

    if (std::strcmp(interface, wl_compositor_interface.name) == 0)
        ws->compositor = static_cast<wl_compositor*>(wl_registry_bind(
            registry, name, &wl_compositor_interface, 4));
    else if (std::strcmp(interface, wl_shm_interface.name) == 0)
        ws->shm = static_cast<wl_shm*>(wl_registry_bind(
            registry, name, &wl_shm_interface, 1));
    else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
        ws->layer_shell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(
            registry, name, &zwlr_layer_shell_v1_interface, 4));
    else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        ws->seat = static_cast<wl_seat*>(wl_registry_bind(
            registry, name, &wl_seat_interface, 7));
        wl_seat_add_listener(ws->seat, &seat_listener, ws);
    }
}

static void registry_global_remove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int wayland_main() {
    WlStatus ws;

    ws.cfg = config_load(config_path());
    ws.height = config_get_int(ws.cfg, "bar_height", BAR_HEIGHT);
    ws.running = true;

    ws.display = wl_display_connect(nullptr);
    if (!ws.display) {
        std::fprintf(stderr, "failed to connect to wayland display\n");
        return 1;
    }

    wl_registry* registry = wl_display_get_registry(ws.display);
    wl_registry_add_listener(registry, &registry_listener, &ws);
    wl_display_roundtrip(ws.display);

    if (!ws.compositor || !ws.shm || !ws.layer_shell) {
        std::fprintf(stderr, "missing required wayland globals\n");
        return 1;
    }

    ws.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ws.xkb_ctx)
        std::fprintf(stderr, "warning: failed to create xkb context\n");

    if (!ws.setup_layer_surface())
        return 1;

    ws.timer_fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (ws.timer_fd >= 0) {
        struct itimerspec ts{};
        ts.it_value.tv_sec = 1;
        ts.it_interval.tv_sec = 1;
        ::timerfd_settime(ws.timer_fd, 0, &ts, nullptr);
    }

    ws.hypr_ev_fd = ws.hypr_connect_event();
    if (ws.hypr_ev_fd < 0)
        std::fprintf(stderr, "warning: could not connect to hyprland event socket\n");

    struct sigaction sa{};
    sa.sa_handler = handle_sighup;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, nullptr);

    ws.inotify_fd = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (ws.inotify_fd >= 0) {
        const char* cfg_path = config_path();
        if (cfg_path) {
            std::array<char, 512> cfg_dir{};
            std::snprintf(cfg_dir.data(), cfg_dir.size(), "%s", cfg_path);
            char* slash = std::strrchr(cfg_dir.data(), '/');
            if (slash) {
                *slash = '\0';
                ::inotify_add_watch(ws.inotify_fd, cfg_dir.data(),
                    IN_CLOSE_WRITE | IN_MOVED_TO);
            }
        }
    }

    ws.setup_plugin_watches();
    ws.init_window_tracking();

    while (ws.running) {
        std::array<struct pollfd, 5> fds{};
        fds[0] = { ::wl_display_get_fd(ws.display), POLLIN, 0 };
        fds[1] = { ws.timer_fd, POLLIN, 0 };
        fds[2] = { ws.inotify_fd, POLLIN, 0 };
        fds[3] = { ws.hypr_ev_fd, POLLIN, 0 };
        fds[4] = { ws.plugin_ifd, POLLIN, 0 };

        int nfds = 1;
        if (ws.timer_fd >= 0) nfds = 2;
        if (ws.inotify_fd >= 0) nfds = 3;
        if (ws.hypr_ev_fd >= 0) nfds = 4;
        if (ws.plugin_ifd >= 0) nfds = 5;

        bool has_display_data = false;

        while (wl_display_prepare_read(ws.display) != 0)
            wl_display_dispatch_pending(ws.display);
        wl_display_flush(ws.display);

        if (::poll(fds.data(), nfds, -1) < 0) {
            if (errno == EINTR) {
                wl_display_cancel_read(ws.display);
                goto check_reload;
            }
            break;
        }

        if (fds[0].revents & POLLIN) {
            wl_display_read_events(ws.display);
            has_display_data = true;
        } else {
            wl_display_cancel_read(ws.display);
        }

        if (has_display_data)
            wl_display_dispatch_pending(ws.display);

        if (ws.timer_fd >= 0 && (fds[1].revents & POLLIN)) {
            uint64_t exp;
            ::read(ws.timer_fd, &exp, sizeof(exp));
            ws.on_timer();
        }

        if (ws.hypr_ev_fd >= 0 && (fds[3].revents & POLLIN)) {
            if (ws.hypr_read_events()) {
                bar_update_workspaces(ws.bar);
                bar_update_workspace_names(ws.bar, ws.tracked_windows.data(), ws.n_tracked_windows);
                bar_update_lua_plugins(ws.bar);
                ws.render();
            }
        }

        if (ws.plugin_ifd >= 0 && (fds[4].revents & POLLIN)) {
            alignas(struct inotify_event) std::array<char, 4096> buf{};
            ssize_t len = ::read(ws.plugin_ifd, buf.data(), buf.size());
            if (len > 0) {
                bool ticked = false;
                for (char* p = buf.data(); p < buf.data() + len;
                     p += sizeof(struct inotify_event) +
                          static_cast<const struct inotify_event*>(
                              static_cast<const void*>(p))->len) {
                    const auto* ev = static_cast<const struct inotify_event*>(
                        static_cast<const void*>(p));
                    for (int i = 0; i < ws.bar->n_lua_plugins; i++) {
                        if (ws.bar->lua_plugins[i].watch_wd == static_cast<int>(ev->wd)) {
                            ws.bar->lua_plugins[i].last_check = 0;
                            ticked = true;
                        }
                    }
                }
                if (ticked) {
                    bar_update_lua_plugins(ws.bar);
                    ws.render();
                }
            }
        }

        if (ws.inotify_fd >= 0 && (fds[2].revents & POLLIN)) {
            alignas(struct inotify_event) std::array<char, 4096> buf{};
            ssize_t len = ::read(ws.inotify_fd, buf.data(), buf.size());
            if (len > 0) {
                for (char* p = buf.data(); p < buf.data() + len;
                     p += sizeof(struct inotify_event) +
                          static_cast<const struct inotify_event*>(
                              static_cast<const void*>(p))->len) {
                    const auto* ev = static_cast<const struct inotify_event*>(
                        static_cast<const void*>(p));
                    if (ev->len > 0 && std::strcmp(ev->name, "config") == 0) {
                        ws.reload();
                        break;
                    }
                }
            }
        }

check_reload:
        if (reload_requested) {
            reload_requested = 0;
            ws.reload();
        }
    }

    return 0;
}

// ============================================================================
//  Xorg Backend
// ============================================================================
#elif defined(BUILD_XORG)

struct X11Bar final {
    Display* display = nullptr;
    int screen = 0;
    Window root = 0;
    Window win = 0;
    Atom xmonad_log_atom = 0;
    Atom net_client_list_atom = 0;
    Atom net_wm_desktop_atom = 0;
    int width = 1920;
    int height = BAR_HEIGHT;
    bool running = false;

    Bar* bar = nullptr;
    Config* cfg = nullptr;

    cairo_surface_t* surface = nullptr;
    cairo_t* cr = nullptr;

    int timer_fd = -1;
    int inotify_fd = -1;
    int plugin_ifd = -1;

    TrackedWindow tracked_windows[MAX_TRACKED_WINDOWS]{};
    int n_tracked_windows = 0;

    X11Bar() = default;

    ~X11Bar() {
        cleanup();
    }

    X11Bar(const X11Bar&) = delete;
    X11Bar& operator=(const X11Bar&) = delete;

    void cleanup() {
        if (cr) cairo_destroy(cr);
        if (surface) cairo_surface_destroy(surface);
        if (win && display) XDestroyWindow(display, win);
        if (display) XCloseDisplay(display);
        if (bar) bar_destroy(bar);
        if (cfg) config_destroy(cfg);
        if (timer_fd >= 0) ::close(timer_fd);
        if (inotify_fd >= 0) ::close(inotify_fd);
        if (plugin_ifd >= 0) ::close(plugin_ifd);
    }

    void render() {
        bar_render(bar, cr);
        cairo_surface_flush(surface);
        XFlush(display);
    }

    void parse_xmonad_log(const std::string& log) {
        bar->n_workspaces = 0;

        std::string input = log;
        while (!input.empty() && (input.back() == '\n' || input.back() == '\r'))
            input.pop_back();

        // XMonad _XMONAD_LOG format: typically space-separated workspace tokens
        // with the active workspace wrapped in delimiters (e.g., _WS_, %WS%, WS*, etc.)
        // Everything before the first '|' is the workspace section.
        std::string ws_section;
        auto pipe_pos = input.find('|');
        if (pipe_pos != std::string::npos)
            ws_section = input.substr(0, pipe_pos);
        else
            ws_section = input;

        std::size_t pos = 0;
        while (pos < ws_section.size() && bar->n_workspaces < MAX_WORKSPACES) {
            while (pos < ws_section.size() &&
                   (ws_section[pos] == ' ' || ws_section[pos] == '\t'))
                pos++;
            if (pos >= ws_section.size()) break;

            bool is_active = false;
            if (ws_section[pos] == '_' || ws_section[pos] == '%' ||
                ws_section[pos] == '*' || ws_section[pos] == '#') {
                is_active = true;
                pos++;
            }

            std::string name;
            while (pos < ws_section.size() &&
                   ws_section[pos] != ' ' && ws_section[pos] != '\t' &&
                   ws_section[pos] != '|' &&
                   ws_section[pos] != '_' && ws_section[pos] != '%' &&
                   ws_section[pos] != '*' && ws_section[pos] != '#') {
                name += ws_section[pos++];
            }

            if (is_active && pos < ws_section.size() &&
                (ws_section[pos] == '_' || ws_section[pos] == '%' ||
                 ws_section[pos] == '*' || ws_section[pos] == '#'))
                pos++;

            if (name.empty()) break;

            auto& ws = bar->workspaces[bar->n_workspaces];
            ws.id = bar->n_workspaces + 1;
            ws.active = is_active;
            std::snprintf(ws.name, sizeof(ws.name), "%s", name.c_str());
            bar->n_workspaces++;
        }
    }

    void handle_xevent(const XEvent& ev) {
        switch (ev.type) {
        case PropertyNotify:
            if (ev.xproperty.atom == xmonad_log_atom &&
                ev.xproperty.state == PropertyNewValue) {
                read_xmonad_log();
                query_client_windows();
                render();
            } else if (ev.xproperty.atom == net_client_list_atom &&
                       ev.xproperty.state == PropertyNewValue) {
                query_client_windows();
                render();
            }
            break;

        case Expose:
            if (ev.xexpose.window == win && ev.xexpose.count == 0)
                render();
            break;

        case DestroyNotify:
            if (ev.xdestroywindow.window == win)
                running = false;
            break;

        default:
            break;
        }
    }

    void setup_plugin_watches() {
        if (!bar) return;
        plugin_ifd = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        if (plugin_ifd < 0) return;

        for (int i = 0; i < bar->n_lua_plugins; i++) {
            std::array<char, 32> watchkey{};
            std::snprintf(watchkey.data(), watchkey.size(), "lua_plugin_%d_watch", i + 1);
            const char* watch = config_get(cfg, watchkey.data(), "");
            if (watch[0]) {
                int wd = ::inotify_add_watch(plugin_ifd, watch, IN_MODIFY);
                if (wd >= 0) bar->lua_plugins[i].watch_wd = wd;
            }
        }

        const char* plugins_dir = config_get(cfg, "lua_plugins_dir", nullptr);
        if (!plugins_dir) {
            const char* home = std::getenv("HOME");
            if (home) {
                static std::array<char, 256> dir{};
                std::snprintf(dir.data(), dir.size(), "%s/.config/wlstatus/plugins", home);
                plugins_dir = dir.data();
            }
        }

        if (plugins_dir) {
            auto add_watch = [&](int idx, const char* sysfs) {
                if (idx < 0 || idx >= bar->n_lua_plugins) return;
                if (bar->lua_plugins[idx].watch_wd >= 0) return;
                if (::access(sysfs, F_OK) != 0) return;
                int wd = ::inotify_add_watch(plugin_ifd, sysfs, IN_MODIFY);
                if (wd >= 0) bar->lua_plugins[idx].watch_wd = wd;
            };

            for (int i = 0; i < bar->n_lua_plugins; i++) {
                const char* pname = bar->lua_plugins[i].path;
                if (!pname[0]) continue;

                if (std::strstr(pname, "battery"))
                    add_watch(i, "/sys/class/power_supply/BAT0/uevent");
                else if (std::strstr(pname, "brightness")) {
                    static const char* backlights[] = {
                        "/sys/class/backlight/intel_backlight/brightness",
                        "/sys/class/backlight/amdgpu_bl0/brightness",
                        "/sys/class/backlight/nvidia_0/brightness",
                        nullptr
                    };
                    for (int b = 0; backlights[b]; b++) {
                        if (::access(backlights[b], F_OK) == 0) {
                            add_watch(i, backlights[b]);
                            break;
                        }
                    }
                } else if (std::strstr(pname, "network") || std::strstr(pname, "net"))
                    add_watch(i, "/sys/class/net");
            }
        }
    }

    void read_xmonad_log() {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* log_data = nullptr;
        if (XGetWindowProperty(display, root, xmonad_log_atom,
                0, 4096, False, XA_STRING,
                &actual_type, &actual_format,
                &nitems, &bytes_after,
                &log_data) == Success && log_data) {
            std::string log_str(reinterpret_cast<const char*>(log_data), nitems);
            if (!log_str.empty() && log_str[0] != '\0')
                parse_xmonad_log(log_str);
            XFree(log_data);
        }
        // If no workspaces parsed, set defaults
        if (bar->n_workspaces == 0) {
            for (int i = 0; i < 9 && i < MAX_WORKSPACES; i++) {
                bar->workspaces[i].id = i + 1;
                bar->workspaces[i].active = (i == 0);
                std::snprintf(bar->workspaces[i].name, sizeof(bar->workspaces[i].name), "%d", i + 1);
                bar->n_workspaces++;
            }
        }
    }

    void on_timer() {
        if (!bar || !running) return;

        read_xmonad_log();
        query_client_windows();
        bar_update_lua_plugins(bar);
        render();
    }

    void query_client_windows() {
        n_tracked_windows = 0;
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* data = nullptr;

        if (XGetWindowProperty(display, root, net_client_list_atom,
                0, 16384, False, 0L,
                &actual_type, &actual_format,
                &nitems, &bytes_after,
                &data) == Success && data && actual_format == 32) {

            auto* windows = reinterpret_cast< ::Window*>(data);
            for (unsigned long i = 0; i < nitems && n_tracked_windows < MAX_TRACKED_WINDOWS; i++) {
                ::Window w = windows[i];

                unsigned char* ddata = nullptr;
                if (XGetWindowProperty(display, w, net_wm_desktop_atom,
                        0, 1, False, 0L,
                        &actual_type, &actual_format,
                        &nitems, &bytes_after,
                        &ddata) == Success && ddata && actual_format == 32 && nitems > 0) {

                    long desktop = *reinterpret_cast<long*>(ddata);
                    XFree(ddata);

                    XClassHint ch{};
                    if (XGetClassHint(display, w, &ch) && ch.res_class) {
                        auto& tw = tracked_windows[n_tracked_windows++];
                        std::snprintf(tw.address, sizeof(tw.address), "%lx",
                            static_cast<unsigned long>(w));
                        tw.workspace_id = static_cast<int>(desktop + 1);
                        std::snprintf(tw.cls, sizeof(tw.cls), "%s", ch.res_class);
                        XFree(ch.res_name);
                        XFree(ch.res_class);
                    }
                }
            }
            XFree(data);
        }
    }

    void reload() {
        if (bar) bar_destroy(bar);
        config_destroy(cfg);
        if (plugin_ifd >= 0) { ::close(plugin_ifd); plugin_ifd = -1; }
        cfg = config_load(config_path());
        bar = bar_create(width, height, cfg);
        setup_plugin_watches();
        query_client_windows();
        bar_update_workspace_names(bar, tracked_windows, n_tracked_windows);
        bar_update_lua_plugins(bar);
        render();
    }
};

static int xorg_main() {
    X11Bar xc;

    xc.cfg = config_load(config_path());
    xc.height = config_get_int(xc.cfg, "bar_height", BAR_HEIGHT);

    // Custom X11 error handler to print and swallow non-fatal errors.
    // The default handler would print and then abort on some systems.
    auto xerr = [](Display* d, XErrorEvent* e) -> int {
        char buf[128] = {};
        XGetErrorText(d, e->error_code, buf, sizeof(buf));
        std::fprintf(stderr, "X11 error: request=%d minor=%d code=%d (%s)\n",
            e->request_code, e->minor_code, e->error_code, buf);
        return 0;
    };
    XSetErrorHandler(xerr);

    auto xioerr = [](Display* d) -> int {
        std::fprintf(stderr, "X11 I/O error: connection lost\n");
        return 0;
    };
    XSetIOErrorHandler(xioerr);

    xc.display = XOpenDisplay(nullptr);
    if (!xc.display) {
        std::fprintf(stderr, "failed to connect to X display\n");
        return 1;
    }

    xc.screen = DefaultScreen(xc.display);
    xc.root = RootWindow(xc.display, xc.screen);
    xc.width = DisplayWidth(xc.display, xc.screen);

    // Create the bar window
    XSetWindowAttributes wa{};
    wa.override_redirect = True;
    wa.event_mask = ExposureMask | StructureNotifyMask;
    wa.background_pixel = BlackPixel(xc.display, xc.screen);

    const char* anchor_str = config_get(xc.cfg, "bar_anchor", "top");
    bool bar_on_bottom = (std::strcmp(anchor_str, "bottom") == 0);
    int y_pos = bar_on_bottom ? xc.height - xc.height : 0;

    xc.win = XCreateWindow(xc.display, xc.root,
        0, y_pos, xc.width, xc.height, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWEventMask | CWBackPixel, &wa);

    // Set window title
    XStoreName(xc.display, xc.win, "wlstatus");
    XMapWindow(xc.display, xc.win);

    // Create cairo surface for X11 rendering.
    XWindowAttributes root_attr;
    XGetWindowAttributes(xc.display, xc.root, &root_attr);
    xc.surface = cairo_xlib_surface_create(xc.display, xc.win,
        root_attr.visual, xc.width, xc.height);
    cairo_xlib_surface_set_size(xc.surface, xc.width, xc.height);
    xc.cr = cairo_create(xc.surface);

    // Initialize bar
    lua_plugin_set_x11_display(xc.display, xc.root);
    xc.bar = bar_create(xc.width, xc.height, xc.cfg);

    // Set up _XMONAD_LOG atom listener.
    xc.xmonad_log_atom = XInternAtom(xc.display, "_XMONAD_LOG", False);
    XSelectInput(xc.display, xc.root, PropertyChangeMask);

    // Read initial _XMONAD_LOG state (or set defaults)
    xc.net_client_list_atom = XInternAtom(xc.display, "_NET_CLIENT_LIST", False);
    xc.net_wm_desktop_atom = XInternAtom(xc.display, "_NET_WM_DESKTOP", False);

    xc.read_xmonad_log();
    xc.query_client_windows();

    xc.running = true;

    // Timer for plugin updates
    xc.timer_fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (xc.timer_fd >= 0) {
        struct itimerspec ts{};
        ts.it_value.tv_sec = 1;
        ts.it_interval.tv_sec = 1;
        ::timerfd_settime(xc.timer_fd, 0, &ts, nullptr);
    }

    // SIGHUP reload handler
    struct sigaction sa{};
    sa.sa_handler = handle_sighup;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, nullptr);

    // inotify for config changes
    xc.inotify_fd = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (xc.inotify_fd >= 0) {
        const char* cfg_path = config_path();
        if (cfg_path) {
            std::array<char, 512> cfg_dir{};
            std::snprintf(cfg_dir.data(), cfg_dir.size(), "%s", cfg_path);
            char* slash = std::strrchr(cfg_dir.data(), '/');
            if (slash) {
                *slash = '\0';
                ::inotify_add_watch(xc.inotify_fd, cfg_dir.data(),
                    IN_CLOSE_WRITE | IN_MOVED_TO);
            }
        }
    }

    xc.setup_plugin_watches();
    xc.render();

    // Event loop: poll-based with X11 fd integration
    int x11_fd = ConnectionNumber(xc.display);

    while (xc.running) {
        // Drain all pending X events without blocking
        while (XPending(xc.display)) {
            XEvent ev;
            XNextEvent(xc.display, &ev);
            xc.handle_xevent(ev);
        }

        std::array<struct pollfd, 4> fds{};
        fds[0] = { x11_fd, POLLIN, 0 };
        fds[1] = { xc.timer_fd, POLLIN, 0 };
        fds[2] = { xc.inotify_fd, POLLIN, 0 };
        fds[3] = { xc.plugin_ifd, POLLIN, 0 };

        int nfds = 1;
        if (xc.timer_fd >= 0) nfds = 2;
        if (xc.inotify_fd >= 0) nfds = 3;
        if (xc.plugin_ifd >= 0) nfds = 4;

        if (::poll(fds.data(), nfds, -1) < 0) {
            if (errno == EINTR) {
                if (reload_requested) {
                    reload_requested = 0;
                    xc.reload();
                }
                continue;
            }
            break;
        }

        if (fds[0].revents & (POLLERR | POLLHUP)) {
            xc.running = false;
            break;
        }

        if (xc.timer_fd >= 0 && (fds[1].revents & POLLIN)) {
            uint64_t exp;
            ::read(xc.timer_fd, &exp, sizeof(exp));
            xc.on_timer();
        }

        if (xc.plugin_ifd >= 0 && (fds[3].revents & POLLIN)) {
            alignas(struct inotify_event) std::array<char, 4096> buf{};
            ssize_t len = ::read(xc.plugin_ifd, buf.data(), buf.size());
            if (len > 0) {
                bool ticked = false;
                for (char* p = buf.data(); p < buf.data() + len;
                     p += sizeof(struct inotify_event) +
                          static_cast<const struct inotify_event*>(
                              static_cast<const void*>(p))->len) {
                    const auto* ev = static_cast<const struct inotify_event*>(
                        static_cast<const void*>(p));
                    for (int i = 0; i < xc.bar->n_lua_plugins; i++) {
                        if (xc.bar->lua_plugins[i].watch_wd == static_cast<int>(ev->wd)) {
                            xc.bar->lua_plugins[i].last_check = 0;
                            ticked = true;
                        }
                    }
                }
                if (ticked) {
                    bar_update_lua_plugins(xc.bar);
                    xc.render();
                }
            }
        }

        if (xc.inotify_fd >= 0 && (fds[2].revents & POLLIN)) {
            alignas(struct inotify_event) std::array<char, 4096> buf{};
            ssize_t len = ::read(xc.inotify_fd, buf.data(), buf.size());
            if (len > 0) {
                for (char* p = buf.data(); p < buf.data() + len;
                     p += sizeof(struct inotify_event) +
                          static_cast<const struct inotify_event*>(
                              static_cast<const void*>(p))->len) {
                    const auto* ev = static_cast<const struct inotify_event*>(
                        static_cast<const void*>(p));
                    if (ev->len > 0 && std::strcmp(ev->name, "config") == 0) {
                        xc.reload();
                        break;
                    }
                }
            }
        }

        if (reload_requested) {
            reload_requested = 0;
            xc.reload();
        }
    }

    return 0;
}

#endif

// ============================================================================
//  Entry Point
// ============================================================================
int main() {
#if defined(BUILD_WAYLAND)
    return wayland_main();
#elif defined(BUILD_XORG)
    return xorg_main();
#endif
}
