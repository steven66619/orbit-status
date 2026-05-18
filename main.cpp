#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ctime>
#include <wayland-client.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <sys/inotify.h>
#include <csignal>
#include <cerrno>
#include <cctype>
#ifdef __cplusplus
#define namespace namespace_
#endif
#include "wlr-layer-shell-unstable-v1-client.h"
#ifdef __cplusplus
#undef namespace
#endif
#include <cairo.h>
#include <pango/pangocairo.h>
#include <xkbcommon/xkbcommon.h>
#include "bar.hpp"
#include "config.hpp"

struct WlStatus {
    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    wl_shm *shm = nullptr;
    zwlr_layer_shell_v1 *layer_shell = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_surface *surface = nullptr;
    wl_seat *seat = nullptr;
    wl_pointer *pointer = nullptr;
    wl_keyboard *keyboard = nullptr;
    xkb_context *xkb_ctx = nullptr;
    struct xkb_keymap *xkb_kmap = nullptr;
    struct xkb_state *xkb_kstate = nullptr;

    wl_buffer *buffer = nullptr;
    cairo_surface_t *cairo_surface = nullptr;
    cairo_t *cr = nullptr;
    void *shm_data = nullptr;
    int width = 0, height = 0;

    Bar *bar = nullptr;
    Config *cfg = nullptr;
    bool configured = false;
    bool running = false;
    uint32_t configure_serial = 0;
    int pointer_x = 0, pointer_y = 0;
    wl_surface *current_pointer_surface = nullptr;

    int timer_fd = -1;
    int inotify_fd = -1;

    struct {
        wl_surface *surface = nullptr;
        zwlr_layer_surface_v1 *layer_surface = nullptr;
        wl_buffer *buffer = nullptr;
        cairo_surface_t *cairo_surface = nullptr;
        cairo_t *cr = nullptr;
        void *shm_data = nullptr;
        int width = 0, height = 0;
        bool visible = false, configured = false;
        char text[512]{};
        int hovered_clickable = -1;
        int hover_x = 0, hover_y = 0;
    } tooltip;

    struct {
        wl_surface *surface = nullptr;
        zwlr_layer_surface_v1 *layer_surface = nullptr;
        wl_buffer *buffer = nullptr;
        cairo_surface_t *cairo_surface = nullptr;
        cairo_t *cr = nullptr;
        void *shm_data = nullptr;
        int width = 0, height = 0;
        bool visible = false, configured = false;
        int action = 0;
        int hovered_btn = -1;
        int confirm_btn_x = 0, confirm_btn_y = 0, confirm_btn_w = 0, confirm_btn_h = 0;
        int cancel_btn_x = 0, cancel_btn_y = 0, cancel_btn_w = 0, cancel_btn_h = 0;
    } popup;
};

static int create_shm_fd(size_t size) {
    int fd = memfd_create("wlstatus", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return -1; }
    return fd;
}

static int create_buffer(WlStatus *ws) {
    if (ws->buffer) return 0;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->width);
    int size = stride * ws->height;
    int fd = create_shm_fd(size);
    if (fd < 0) return -1;

    wl_shm_pool *pool = wl_shm_create_pool(ws->shm, fd, size);
    ws->buffer = wl_shm_pool_create_buffer(pool, 0, ws->width, ws->height,
        stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    ws->shm_data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ws->shm_data == MAP_FAILED) return -1;

    ws->cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char *)ws->shm_data, CAIRO_FORMAT_ARGB32, ws->width, ws->height, stride);
    ws->cr = cairo_create(ws->cairo_surface);
    return 0;
}

static void destroy_buffer(WlStatus *ws) {
    if (ws->cr) cairo_destroy(ws->cr);
    ws->cr = nullptr;
    if (ws->cairo_surface) cairo_surface_destroy(ws->cairo_surface);
    ws->cairo_surface = nullptr;
    if (ws->shm_data) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->width);
        munmap(ws->shm_data, stride * ws->height);
    }
    ws->shm_data = nullptr;
    if (ws->buffer) wl_buffer_destroy(ws->buffer);
    ws->buffer = nullptr;
}

static void render(WlStatus *ws) {
    bar_render(ws->bar, ws->cr);
    cairo_surface_flush(ws->cairo_surface);
    wl_surface_attach(ws->surface, ws->buffer, 0, 0);
    wl_surface_damage_buffer(ws->surface, 0, 0, ws->width, ws->height);
    wl_surface_commit(ws->surface);

    if (ws->popup.visible && ws->popup.buffer && ws->popup.surface) {
        wl_surface_attach(ws->popup.surface, ws->popup.buffer, 0, 0);
        wl_surface_damage_buffer(ws->popup.surface, 0, 0, ws->popup.width, ws->popup.height);
        wl_surface_commit(ws->popup.surface);
    }
}

static int popup_create_buffer(WlStatus *ws) {
    if (ws->popup.buffer) return 0;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->popup.width);
    int size = stride * ws->popup.height;
    int fd = create_shm_fd(size);
    if (fd < 0) return -1;

    wl_shm_pool *pool = wl_shm_create_pool(ws->shm, fd, size);
    ws->popup.buffer = wl_shm_pool_create_buffer(pool, 0,
        ws->popup.width, ws->popup.height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    ws->popup.shm_data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ws->popup.shm_data == MAP_FAILED) return -1;

    ws->popup.cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char *)ws->popup.shm_data, CAIRO_FORMAT_ARGB32,
        ws->popup.width, ws->popup.height, stride);
    ws->popup.cr = cairo_create(ws->popup.cairo_surface);
    return 0;
}

static void popup_destroy_buffer(WlStatus *ws) {
    if (ws->popup.cr) cairo_destroy(ws->popup.cr);
    ws->popup.cr = nullptr;
    if (ws->popup.cairo_surface) cairo_surface_destroy(ws->popup.cairo_surface);
    ws->popup.cairo_surface = nullptr;
    if (ws->popup.shm_data) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->popup.width);
        munmap(ws->popup.shm_data, stride * ws->popup.height);
    }
    ws->popup.shm_data = nullptr;
    if (ws->popup.buffer) wl_buffer_destroy(ws->popup.buffer);
    ws->popup.buffer = nullptr;
}

static void popup_render(WlStatus *ws) {
    cairo_t *cr = ws->popup.cr;
    int w = ws->popup.width, h = ws->popup.height;

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    draw_rounded_rect(cr, 0, 0, w, h, 8);
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.22, 0.96);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, 0.3);
    cairo_set_line_width(cr, 1);
    draw_rounded_rect(cr, 0, 0, w, h, 8);
    cairo_stroke(cr);

    PangoLayout *lay = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string("Sans Bold 13");
    pango_layout_set_font_description(lay, fd);
    pango_font_description_free(fd);
    const char *labels[] = {"Power Off", "Reboot", "Suspend"};
    pango_layout_set_text(lay, labels[ws->popup.action], -1);
    int tw, th;
    pango_layout_get_pixel_size(lay, &tw, &th);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, (w - tw) / 2, 24);
    pango_cairo_show_layout(cr, lay);

    int btn_w = 65, btn_h = 28, btn_gap = 12;
    int btn_y = h - btn_h - 14;
    int confirm_x = (w - btn_w * 2 - btn_gap) / 2;
    int cancel_x = confirm_x + btn_w + btn_gap;

    bool con_hover = (ws->popup.hovered_btn == 0);
    bool can_hover = (ws->popup.hovered_btn == 1);

    cairo_set_source_rgba(cr, 0.2, 0.8, 0.3, con_hover ? 0.9 : 0.6);
    draw_rounded_rect(cr, confirm_x, btn_y, btn_w, btn_h, 5);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, can_hover ? 0.9 : 0.6);
    draw_rounded_rect(cr, cancel_x, btn_y, btn_w, btn_h, 5);
    cairo_fill(cr);

    PangoFontDescription *fb = pango_font_description_from_string("Sans Bold 11");
    PangoLayout *lc = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(lc, fb);
    pango_layout_set_text(lc, "Confirm", -1);
    int cw, ch;
    pango_layout_get_pixel_size(lc, &cw, &ch);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, confirm_x + (btn_w - cw) / 2, btn_y + (btn_h - ch) / 2 + 1);
    pango_cairo_show_layout(cr, lc);

    PangoLayout *lx = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(lx, fb);
    pango_layout_set_text(lx, "Cancel", -1);
    pango_layout_get_pixel_size(lx, &cw, &ch);
    cairo_move_to(cr, cancel_x + (btn_w - cw) / 2, btn_y + (btn_h - ch) / 2 + 1);
    pango_cairo_show_layout(cr, lx);
    pango_font_description_free(fb);
    g_object_unref(lc);
    g_object_unref(lx);
    g_object_unref(lay);

    ws->popup.confirm_btn_x = confirm_x;
    ws->popup.confirm_btn_y = btn_y;
    ws->popup.confirm_btn_w = btn_w;
    ws->popup.confirm_btn_h = btn_h;
    ws->popup.cancel_btn_x = cancel_x;
    ws->popup.cancel_btn_y = btn_y;
    ws->popup.cancel_btn_w = btn_w;
    ws->popup.cancel_btn_h = btn_h;

    cairo_surface_flush(ws->popup.cairo_surface);
    wl_surface_attach(ws->popup.surface, ws->popup.buffer, 0, 0);
    wl_surface_damage_buffer(ws->popup.surface, 0, 0, w, h);
    wl_surface_commit(ws->popup.surface);
}

static void popup_destroy(WlStatus *ws) {
    if (!ws->popup.visible) return;
    ws->popup.visible = false;
    popup_destroy_buffer(ws);
    if (ws->popup.layer_surface) {
        zwlr_layer_surface_v1_destroy(ws->popup.layer_surface);
        ws->popup.layer_surface = nullptr;
    }
    if (ws->popup.surface) {
        wl_surface_destroy(ws->popup.surface);
        ws->popup.surface = nullptr;
    }
    ws->popup.configured = false;
}

static void popup_layer_surface_configure(void *data,
    zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t width, uint32_t height) {
    auto *ws = (WlStatus *)data;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    if (width > 0) ws->popup.width = width;
    if (height > 0) ws->popup.height = height;
    if (!ws->popup.buffer) {
        popup_create_buffer(ws);
        popup_render(ws);
    }
    ws->popup.configured = true;
}

static void popup_layer_surface_closed(void *data,
    zwlr_layer_surface_v1 *) {
    ((WlStatus *)data)->popup.visible = false;
}

static const zwlr_layer_surface_v1_listener popup_layer_surface_listener = {
    .configure = popup_layer_surface_configure,
    .closed = popup_layer_surface_closed,
};

static void popup_create(WlStatus *ws, int action) {
    if (ws->popup.visible) popup_destroy(ws);

    ws->popup = {};
    ws->popup.width = 175;
    ws->popup.height = 105;
    ws->popup.action = action;
    ws->popup.hovered_btn = -1;

    ws->popup.surface = wl_compositor_create_surface(ws->compositor);
    ws->popup.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ws->layer_shell, ws->popup.surface, nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlstatus-popup");

    zwlr_layer_surface_v1_add_listener(ws->popup.layer_surface,
        &popup_layer_surface_listener, ws);

    zwlr_layer_surface_v1_set_size(ws->popup.layer_surface, ws->popup.width, ws->popup.height);
    zwlr_layer_surface_v1_set_anchor(ws->popup.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    const char *anchor_str = config_get(ws->cfg, "bar_anchor", "top");
    bool bar_on_bottom = (strcmp(anchor_str, "bottom") == 0);
    if (bar_on_bottom) {
        zwlr_layer_surface_v1_set_anchor(ws->popup.layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_margin(ws->popup.layer_surface,
            0, BAR_PADDING, ws->height + 4, 0);
    } else {
        zwlr_layer_surface_v1_set_margin(ws->popup.layer_surface,
            ws->height + 4, BAR_PADDING, 0, 0);
    }
    zwlr_layer_surface_v1_set_exclusive_zone(ws->popup.layer_surface, 0);

    ws->popup.visible = true;
    wl_surface_commit(ws->popup.surface);
    wl_display_roundtrip(ws->display);
}

static void tooltip_destroy(WlStatus *ws) {
    if (!ws->tooltip.visible) return;
    ws->tooltip.visible = false;
    ws->tooltip.hovered_clickable = -1;
    if (ws->tooltip.buffer) {
        cairo_destroy(ws->tooltip.cr);
        ws->tooltip.cr = nullptr;
        cairo_surface_destroy(ws->tooltip.cairo_surface);
        ws->tooltip.cairo_surface = nullptr;
        if (ws->tooltip.shm_data) {
            int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->tooltip.width);
            munmap(ws->tooltip.shm_data, stride * ws->tooltip.height);
        }
        ws->tooltip.shm_data = nullptr;
        wl_buffer_destroy(ws->tooltip.buffer);
        ws->tooltip.buffer = nullptr;
    }
    if (ws->tooltip.layer_surface) {
        zwlr_layer_surface_v1_destroy(ws->tooltip.layer_surface);
        ws->tooltip.layer_surface = nullptr;
    }
    if (ws->tooltip.surface) {
        wl_surface_destroy(ws->tooltip.surface);
        ws->tooltip.surface = nullptr;
    }
    ws->tooltip.configured = false;
}

static void tooltip_layer_surface_configure(void *data,
    zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t width, uint32_t height) {
    auto *ws = (WlStatus *)data;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    if (width > 0) ws->tooltip.width = width;
    if (height > 0) ws->tooltip.height = height;
    if (!ws->tooltip.buffer) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->tooltip.width);
        int size = stride * ws->tooltip.height;
        int shm_fd = create_shm_fd(size);
        if (shm_fd < 0) return;
        wl_shm_pool *pool = wl_shm_create_pool(ws->shm, shm_fd, size);
        ws->tooltip.buffer = wl_shm_pool_create_buffer(pool, 0,
            ws->tooltip.width, ws->tooltip.height, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        ws->tooltip.shm_data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        close(shm_fd);
        if (ws->tooltip.shm_data == MAP_FAILED) { ws->tooltip.shm_data = nullptr; return; }
        ws->tooltip.cairo_surface = cairo_image_surface_create_for_data(
            (unsigned char *)ws->tooltip.shm_data, CAIRO_FORMAT_ARGB32,
            ws->tooltip.width, ws->tooltip.height, stride);
        ws->tooltip.cr = cairo_create(ws->tooltip.cairo_surface);

        cairo_t *cr = ws->tooltip.cr;
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        draw_rounded_rect(cr, 0, 0, ws->tooltip.width, ws->tooltip.height, 6);
        cairo_set_source_rgba(cr, 0.10, 0.10, 0.18, 0.96);
        cairo_fill(cr);

        PangoLayout *lay = pango_cairo_create_layout(cr);
        PangoFontDescription *fdesc = pango_font_description_from_string("Sans 10");
        pango_layout_set_font_description(lay, fdesc);
        pango_font_description_free(fdesc);
        pango_layout_set_text(lay, ws->tooltip.text, -1);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, 6, 6);
        pango_cairo_show_layout(cr, lay);
        g_object_unref(lay);

        cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, 0.3);
        cairo_set_line_width(cr, 1);
        draw_rounded_rect(cr, 0, 0, ws->tooltip.width, ws->tooltip.height, 6);
        cairo_stroke(cr);

        cairo_surface_flush(ws->tooltip.cairo_surface);
    }
    ws->tooltip.configured = true;
    if (ws->tooltip.buffer && ws->tooltip.surface) {
        wl_surface_attach(ws->tooltip.surface, ws->tooltip.buffer, 0, 0);
        wl_surface_damage_buffer(ws->tooltip.surface, 0, 0, ws->tooltip.width, ws->tooltip.height);
        wl_surface_commit(ws->tooltip.surface);
    }
}

static void tooltip_layer_surface_closed(void *data,
    zwlr_layer_surface_v1 *) {
    ((WlStatus *)data)->tooltip.visible = false;
}

static const zwlr_layer_surface_v1_listener tooltip_layer_surface_listener = {
    .configure = tooltip_layer_surface_configure,
    .closed = tooltip_layer_surface_closed,
};

static void tooltip_show(WlStatus *ws, const char *text, int hover_x, int hover_y) {
    if (ws->tooltip.visible && strcmp(ws->tooltip.text, text) == 0)
        return;

    tooltip_destroy(ws);

    ws->tooltip = {};
    ws->tooltip.hover_x = hover_x;
    ws->tooltip.hover_y = hover_y;
    ws->tooltip.hovered_clickable = 0;
    strncpy(ws->tooltip.text, text, sizeof(ws->tooltip.text) - 1);

    int nlines = 1;
    for (const char *p = text; *p; p++)
        if (*p == '\n') nlines++;
    ws->tooltip.width = 280;
    ws->tooltip.height = nlines * 16 + 14;

    ws->tooltip.surface = wl_compositor_create_surface(ws->compositor);
    ws->tooltip.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ws->layer_shell, ws->tooltip.surface, nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlstatus-tooltip");

    zwlr_layer_surface_v1_add_listener(ws->tooltip.layer_surface,
        &tooltip_layer_surface_listener, ws);

    zwlr_layer_surface_v1_set_size(ws->tooltip.layer_surface, ws->tooltip.width, ws->tooltip.height);
    zwlr_layer_surface_v1_set_anchor(ws->tooltip.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

    const char *anchor_str = config_get(ws->cfg, "bar_anchor", "top");
    bool bar_on_bottom = (strcmp(anchor_str, "bottom") == 0);
    if (bar_on_bottom)
        zwlr_layer_surface_v1_set_margin(ws->tooltip.layer_surface,
            hover_y - ws->tooltip.height - 4, BAR_PADDING, 0, 0);
    else
        zwlr_layer_surface_v1_set_margin(ws->tooltip.layer_surface,
            ws->height + 4, BAR_PADDING, 0, 0);

    zwlr_layer_surface_v1_set_exclusive_zone(ws->tooltip.layer_surface, 0);
    ws->tooltip.visible = true;
    wl_surface_commit(ws->tooltip.surface);
}

static volatile sig_atomic_t reload_requested;

static void handle_sighup(int) {
    reload_requested = 1;
}

static const char *config_path() {
    const char *home = getenv("HOME");
    if (!home) return nullptr;
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s/.config/wlstatus/config", home);
    return buf;
}

static void reload(WlStatus *ws) {
    if (ws->popup.visible) popup_destroy(ws);
    if (ws->tooltip.visible) tooltip_destroy(ws);
    destroy_buffer(ws);
    if (ws->bar) bar_destroy(ws->bar);
    config_destroy(ws->cfg);
    ws->cfg = config_load(config_path());
    ws->bar = bar_create(ws->width, ws->height, ws->cfg);
    create_buffer(ws);
    bar_update_workspaces(ws->bar);
    bar_update_lua_plugins(ws->bar);
    render(ws);
}

static void on_timer(WlStatus *ws) {
    if (!ws->bar || !ws->running) return;
    bar_update_workspaces(ws->bar);
    bar_update_lua_plugins(ws->bar);
    render(ws);
}

static void layer_surface_configure(void *data,
    zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t width, uint32_t height) {
    auto *ws = (WlStatus *)data;
    ws->configure_serial = serial;

    if (width == 0) width = ws->width;
    if (height == 0) height = config_get_int(ws->cfg, "bar_height", BAR_HEIGHT);

    if (ws->width != (int)width || ws->height != (int)height) {
        destroy_buffer(ws);
        ws->width = width;
        ws->height = height;
        if (ws->bar) bar_destroy(ws->bar);
        ws->bar = bar_create(ws->width, ws->height, ws->cfg);
    }

    if (!ws->bar) ws->bar = bar_create(ws->width, ws->height, ws->cfg);
    create_buffer(ws);
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    ws->configured = true;

    bar_update_workspaces(ws->bar);
    render(ws);
}

static void layer_surface_closed(void *data,
    zwlr_layer_surface_v1 *) {
    ((WlStatus *)data)->running = false;
}

static const zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void pointer_enter(void *data, wl_pointer *pointer,
    uint32_t serial, wl_surface *surface,
    wl_fixed_t sx, wl_fixed_t sy) {
    auto *ws = (WlStatus *)data;
    ws->current_pointer_surface = surface;
    ws->pointer_x = wl_fixed_to_int(sx);
    ws->pointer_y = wl_fixed_to_int(sy);

    if (ws->popup.visible && surface == ws->popup.surface)
        return;

    bar_update_hover(ws->bar, ws->pointer_x, ws->pointer_y);
    render(ws);

    for (int i = 0; i < ws->bar->n_clickables; i++) {
        Clickable *c = &ws->bar->clickables[i];
        if (!(ws->pointer_x >= c->x && ws->pointer_x < c->x + c->w &&
              ws->pointer_y >= c->y && ws->pointer_y < c->y + c->h))
            continue;

        if (c->tooltip_text[0]) {
            ws->tooltip.hovered_clickable = i;
            tooltip_show(ws, c->tooltip_text, ws->pointer_x, ws->pointer_y);
        } else if (c->tooltip_cmd[0]) {
            FILE *fp = popen(c->tooltip_cmd, "r");
            if (fp) {
                char buf[512] = {0};
                size_t total = 0;
                char line[256];
                while (fgets(line, sizeof(line), fp) && total < sizeof(buf) - 1) {
                    int len = snprintf(buf + total, sizeof(buf) - total, "%s", line);
                    if (len > 0) total += len;
                }
                pclose(fp);
                ws->tooltip.hovered_clickable = i;
                tooltip_show(ws, buf, ws->pointer_x, ws->pointer_y);
            }
        }
        break;
    }
}

static void pointer_leave(void *data, wl_pointer *,
    uint32_t serial, wl_surface *surface) {
    auto *ws = (WlStatus *)data;
    ws->current_pointer_surface = nullptr;

    if (ws->popup.visible && surface == ws->popup.surface) {
        if (ws->popup.hovered_btn != -1) {
            ws->popup.hovered_btn = -1;
            popup_render(ws);
        }
        return;
    }

    bar_clear_hover(ws->bar);
    render(ws);
    if (ws->tooltip.visible)
        tooltip_destroy(ws);
}

static void pointer_motion(void *data, wl_pointer *,
    uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    auto *ws = (WlStatus *)data;
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
            popup_render(ws);
        return;
    }

    int old_power = ws->bar->power_hovered;
    int old_ws = ws->bar->hovered_workspace;
    bar_update_hover(ws->bar, x, y);
    if (old_power != ws->bar->power_hovered ||
        old_ws != ws->bar->hovered_workspace)
        render(ws);

    int found_tooltip = -1;
    for (int i = 0; i < ws->bar->n_clickables; i++) {
        Clickable *c = &ws->bar->clickables[i];
        if ((c->tooltip_cmd[0] || c->tooltip_text[0]) &&
            x >= c->x && x < c->x + c->w &&
            y >= c->y && y < c->y + c->h) {
            found_tooltip = i;
            break;
        }
    }
    if (found_tooltip >= 0 && found_tooltip != ws->tooltip.hovered_clickable) {
        Clickable *c = &ws->bar->clickables[found_tooltip];
        if (c->tooltip_text[0]) {
            ws->tooltip.hovered_clickable = found_tooltip;
            tooltip_show(ws, c->tooltip_text, x, y);
        } else {
            FILE *fp = popen(c->tooltip_cmd, "r");
            if (fp) {
                char buf[512] = {0};
                size_t total = 0;
                char line[256];
                while (fgets(line, sizeof(line), fp) && total < sizeof(buf) - 1) {
                    int len = snprintf(buf + total, sizeof(buf) - total, "%s", line);
                    if (len > 0) total += len;
                }
                pclose(fp);
                ws->tooltip.hovered_clickable = found_tooltip;
                tooltip_show(ws, buf, x, y);
            }
        }
    } else if (found_tooltip < 0) {
        if (ws->tooltip.visible)
            tooltip_destroy(ws);
    }
}

static void execute_command(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, nullptr);
        _exit(1);
    }
}

static void pointer_button(void *data, wl_pointer *,
    uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    auto *ws = (WlStatus *)data;
    if (state != 1) return;
    if (button != 0x110) return;

    int x = ws->pointer_x, y = ws->pointer_y;

    if (ws->popup.visible) {
        if (ws->current_pointer_surface == ws->popup.surface) {
            if (x >= ws->popup.confirm_btn_x && x < ws->popup.confirm_btn_x + ws->popup.confirm_btn_w &&
                y >= ws->popup.confirm_btn_y && y < ws->popup.confirm_btn_y + ws->popup.confirm_btn_h) {
                const char *cmds[] = {"systemctl poweroff", "systemctl reboot", "systemctl suspend"};
                execute_command(cmds[ws->popup.action]);
                popup_destroy(ws);
                return;
            }
            if (x >= ws->popup.cancel_btn_x && x < ws->popup.cancel_btn_x + ws->popup.cancel_btn_w &&
                y >= ws->popup.cancel_btn_y && y < ws->popup.cancel_btn_y + ws->popup.cancel_btn_h) {
                popup_destroy(ws);
                return;
            }
        } else {
            popup_destroy(ws);
        }
    }

    for (int i = 0; i < ws->bar->n_clickables; i++) {
        Clickable *c = &ws->bar->clickables[i];
        if (x >= c->x && x < c->x + c->w &&
            y >= c->y && y < c->y + c->h) {
            switch (c->action) {
            case CLICK_POWEROFF:
                popup_create(ws, 0);
                break;
            case CLICK_REBOOT:
                popup_create(ws, 1);
                break;
            case CLICK_SUSPEND:
                popup_create(ws, 2);
                break;
            case CLICK_HYPRCTL:
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

static void pointer_axis(void *data, wl_pointer *,
    uint32_t time, uint32_t axis, wl_fixed_t value) {
    auto *ws = (WlStatus *)data;
    if (axis != 0) return;

    int x = ws->pointer_x, y = ws->pointer_y;
    for (int i = 0; i < ws->bar->n_clickables; i++) {
        Clickable *c = &ws->bar->clickables[i];
        if (x >= c->x && x < c->x + c->w &&
            y >= c->y && y < c->y + c->h) {
            if (c->lua_plugin_idx >= 0) {
                int direction = (value > 0) ? -1 : 1;
                lua_plugin_call_onscroll(
                    &ws->bar->lua_plugins[c->lua_plugin_idx], direction);
                bar_update_lua_plugins(ws->bar);
                render(ws);
            }
            break;
        }
    }
}

static void pointer_frame(void *, wl_pointer *) {}

static void keyboard_keymap(void *data, wl_keyboard *,
    uint32_t format, int fd, uint32_t size) {
    auto *ws = (WlStatus *)data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
    char *map = (char *)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) { close(fd); return; }
    ws->xkb_kmap = xkb_keymap_new_from_string(ws->xkb_ctx, map,
        XKB_KEYMAP_FORMAT_TEXT_V1, (xkb_keymap_compile_flags)0);
    munmap(map, size);
    close(fd);
    if (!ws->xkb_kmap) return;
    ws->xkb_kstate = xkb_state_new(ws->xkb_kmap);
}

static void keyboard_enter(void *, wl_keyboard *,
    uint32_t, wl_surface *, wl_array *) {}
static void keyboard_leave(void *, wl_keyboard *,
    uint32_t, wl_surface *) {}
static void keyboard_key(void *, wl_keyboard *,
    uint32_t, uint32_t, uint32_t, uint32_t) {}

static void keyboard_modifiers(void *data, wl_keyboard *,
    uint32_t, uint32_t mods_depressed, uint32_t mods_latched,
    uint32_t mods_locked, uint32_t group) {
    auto *ws = (WlStatus *)data;
    if (ws->xkb_kstate)
        xkb_state_update_mask(ws->xkb_kstate, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void keyboard_repeat_info(void *, wl_keyboard *,
    int32_t, int32_t) {}

static const wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static const wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
};

static void seat_capabilities(void *data, wl_seat *seat,
    uint32_t capabilities) {
    auto *ws = (WlStatus *)data;
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

static void seat_name(void *, wl_seat *, const char *) {}

static const wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void registry_global(void *data, wl_registry *registry,
    uint32_t name, const char *interface, uint32_t) {
    auto *ws = (WlStatus *)data;

    if (strcmp(interface, wl_compositor_interface.name) == 0)
        ws->compositor = (wl_compositor *)wl_registry_bind(
            registry, name, &wl_compositor_interface, 4);
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        ws->shm = (wl_shm *)wl_registry_bind(
            registry, name, &wl_shm_interface, 1);
    else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
        ws->layer_shell = (zwlr_layer_shell_v1 *)wl_registry_bind(
            registry, name, &zwlr_layer_shell_v1_interface, 4);
    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        ws->seat = (wl_seat *)wl_registry_bind(
            registry, name, &wl_seat_interface, 7);
        wl_seat_add_listener(ws->seat, &seat_listener, ws);
    }
}

static void registry_global_remove(void *, wl_registry *, uint32_t) {}

static const wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int setup_layer_surface(WlStatus *ws) {
    ws->surface = wl_compositor_create_surface(ws->compositor);
    if (!ws->surface) return -1;

    const char *layer_str = config_get(ws->cfg, "bar_layer", "top");
    auto layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    if (strcmp(layer_str, "overlay") == 0)
        layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    ws->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ws->layer_shell, ws->surface, nullptr, layer, "wlstatus");
    if (!ws->layer_surface) return -1;

    zwlr_layer_surface_v1_add_listener(ws->layer_surface,
        &layer_surface_listener, ws);

    int bh = config_get_int(ws->cfg, "bar_height", BAR_HEIGHT);
    zwlr_layer_surface_v1_set_size(ws->layer_surface, 0, bh);

    uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    const char *anchor_str = config_get(ws->cfg, "bar_anchor", "top");
    if (strcmp(anchor_str, "bottom") == 0)
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    else
        anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    zwlr_layer_surface_v1_set_anchor(ws->layer_surface, anchor);
    if (layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)
        zwlr_layer_surface_v1_set_exclusive_zone(ws->layer_surface, 0);
    else
        zwlr_layer_surface_v1_set_exclusive_zone(ws->layer_surface, bh);

    wl_surface_commit(ws->surface);
    wl_display_roundtrip(ws->display);

    if (!ws->configured) {
        fprintf(stderr, "surface was not configured\n");
        return -1;
    }
    return 0;
}

int main() {
    WlStatus ws;
    ws.cfg = config_load(config_path());
    int bh = config_get_int(ws.cfg, "bar_height", BAR_HEIGHT);
    ws.width = 1920;
    ws.height = bh;
    ws.running = true;

    ws.display = wl_display_connect(nullptr);
    if (!ws.display) {
        fprintf(stderr, "failed to connect to wayland display\n");
        return 1;
    }

    wl_registry *registry = wl_display_get_registry(ws.display);
    wl_registry_add_listener(registry, &registry_listener, &ws);
    wl_display_roundtrip(ws.display);

    if (!ws.compositor || !ws.shm || !ws.layer_shell) {
        fprintf(stderr, "missing required wayland globals\n");
        return 1;
    }

    ws.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ws.xkb_ctx)
        fprintf(stderr, "warning: failed to create xkb context\n");

    if (setup_layer_surface(&ws) < 0)
        return 1;

    ws.timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (ws.timer_fd >= 0) {
        struct itimerspec ts = {};
        ts.it_value.tv_sec = 1;
        ts.it_interval.tv_sec = 1;
        timerfd_settime(ws.timer_fd, 0, &ts, nullptr);
    }

    struct sigaction sa = {};
    sa.sa_handler = handle_sighup;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, nullptr);

    ws.inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (ws.inotify_fd >= 0) {
        const char *cfg_path = config_path();
        if (cfg_path) {
            char cfg_dir[512];
            snprintf(cfg_dir, sizeof(cfg_dir), "%s", cfg_path);
            char *slash = strrchr(cfg_dir, '/');
            if (slash) {
                *slash = '\0';
                inotify_add_watch(ws.inotify_fd, cfg_dir,
                    IN_CLOSE_WRITE | IN_MOVED_TO);
            }
        }
    }

    while (ws.running) {
        struct pollfd fds[3] = {
            { .fd = wl_display_get_fd(ws.display), .events = POLLIN },
            { .fd = ws.timer_fd, .events = POLLIN },
            { .fd = ws.inotify_fd, .events = POLLIN },
        };
        int nfds = 1;
        if (ws.timer_fd >= 0) nfds = 2;
        if (ws.inotify_fd >= 0) nfds = 3;
        bool has_display_data = false;

        while (wl_display_prepare_read(ws.display) != 0)
            wl_display_dispatch_pending(ws.display);
        wl_display_flush(ws.display);

        if (poll(fds, nfds, -1) < 0) {
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
            read(ws.timer_fd, &exp, sizeof(exp));
            on_timer(&ws);
        }

        if (ws.inotify_fd >= 0 && (fds[2].revents & POLLIN)) {
            char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
            ssize_t len = read(ws.inotify_fd, buf, sizeof(buf));
            if (len > 0) {
                for (char *p = buf; p < buf + len; p += sizeof(struct inotify_event) + ((const struct inotify_event *)p)->len) {
                    const auto *ev = (const struct inotify_event *)p;
                    if (ev->len > 0 && strcmp(ev->name, "config") == 0) {
                        reload(&ws);
                        break;
                    }
                }
            }
        }

check_reload:
        if (reload_requested) {
            reload_requested = 0;
            reload(&ws);
        }
    }

    if (ws.timer_fd >= 0) close(ws.timer_fd);
    if (ws.inotify_fd >= 0) close(ws.inotify_fd);
    if (ws.popup.visible) popup_destroy(&ws);
    if (ws.tooltip.visible) tooltip_destroy(&ws);
    destroy_buffer(&ws);
    if (ws.pointer) wl_pointer_destroy(ws.pointer);
    if (ws.seat) wl_seat_destroy(ws.seat);
    if (ws.layer_surface) zwlr_layer_surface_v1_destroy(ws.layer_surface);
    if (ws.surface) wl_surface_destroy(ws.surface);
    if (ws.layer_shell) zwlr_layer_shell_v1_destroy(ws.layer_shell);
    if (ws.compositor) wl_compositor_destroy(ws.compositor);
    if (ws.shm) wl_shm_destroy(ws.shm);
    bar_destroy(ws.bar);
    config_destroy(ws.cfg);
    wl_display_disconnect(ws.display);
    return 0;
}
