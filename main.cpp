#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ctime>
#include <poll.h>
#include <sys/timerfd.h>
#include <sys/inotify.h>
#include <csignal>
#include <cerrno>
#include <cctype>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client.h"
#include <cairo.h>
#include <pango/pangocairo.h>
#include "bar.hpp"
#include "config.hpp"
#include "sway_ipc.hpp"
#include "sni_tray.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct WlBuffer {
    wl_buffer *buffer = nullptr;
    cairo_surface_t *surface = nullptr;
    cairo_t *cr = nullptr;
    void *data = nullptr;
    size_t size = 0;
    bool in_use = false;
};

struct OrbitStatus {
    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    wl_shm *shm = nullptr;
    zwlr_layer_shell_v1 *layer_shell = nullptr;
    wl_surface *surface = nullptr;
    zwlr_layer_surface_v1 *layer_surface = nullptr;
    wl_seat *seat = nullptr;
    wl_pointer *pointer = nullptr;

    WlBuffer buffers[2];
    int width = 0, height = 0;
    bool configured = false;
    bool running = false;

    Bar *bar = nullptr;
    Config *cfg = nullptr;

    SniTray tray;
    int timer_fd = -1;
    int inotify_fd = -1;

    int pointer_x = 0, pointer_y = 0;
    wl_surface *current_pointer_surface = nullptr;

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

    struct {
        wl_surface *surface = nullptr;
        zwlr_layer_surface_v1 *layer_surface = nullptr;
        wl_buffer *buffer = nullptr;
        cairo_surface_t *cairo_surface = nullptr;
        cairo_t *cr = nullptr;
        void *shm_data = nullptr;
        int width = 0, height = 0;
        bool visible = false, configured = false;
        int volume = 0;          // 0..100
        bool dragging = false;   // handle being dragged
        // Slider geometry (set during render).
        int track_x = 0, track_y = 0, track_w = 0, track_h = 0;
        int handle_x = 0, handle_y = 0, handle_w = 0, handle_h = 0;
    } volume;
};

static volatile sig_atomic_t reload_requested;

static void handle_sighup(int) {
    reload_requested = 1;
}

// Reap fire-and-forget children (execute_command) so they don't become
// zombies. do_tick_fork() in lua_plugin.cpp does its own waitpid and already
// tolerates ECHILD, so this handler is safe to reap everything.
static void handle_sigchld(int) {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
}

static const char *config_path() {
    const char *home = getenv("HOME");
    if (!home) return nullptr;
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s/.config/orbit-status/config", home);
    return buf;
}

static void render(OrbitStatus *ws);
static void popup_destroy(OrbitStatus *ws);
static void tooltip_destroy(OrbitStatus *ws);
static void volume_slider_destroy(OrbitStatus *ws);

static void on_tray_change(void *userdata) {
    OrbitStatus *ws = (OrbitStatus *)userdata;
    if (ws->bar) {
        render(ws);
    }
}

/* ------------------------------------------------------------------ */
/* Shared memory buffers                                              */
/* ------------------------------------------------------------------ */

static int create_shm_fd(size_t size) {
    int fd = memfd_create("orbit-status", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return -1; }
    return fd;
}

static void buffer_release(void *data, wl_buffer *buffer) {
    WlBuffer *wb = (WlBuffer *)data;
    wb->in_use = false;
}

static const wl_buffer_listener buffer_listener = { buffer_release };

static int create_buffer(OrbitStatus *ws, WlBuffer *wb, int w, int h) {
    if (wb->buffer) return 0;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
    size_t size = (size_t)stride * h;
    int fd = create_shm_fd(size);
    if (fd < 0) return -1;

    wl_shm_pool *pool = wl_shm_create_pool(ws->shm, fd, size);
    wb->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    wb->data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (wb->data == MAP_FAILED) { wb->data = nullptr; return -1; }
    wb->size = size;

    wb->surface = cairo_image_surface_create_for_data(
        (unsigned char *)wb->data, CAIRO_FORMAT_ARGB32, w, h, stride);
    wb->cr = cairo_create(wb->surface);
    wl_buffer_add_listener(wb->buffer, &buffer_listener, wb);
    return 0;
}

static void destroy_buffer(WlBuffer *wb) {
    if (wb->cr) cairo_destroy(wb->cr);
    wb->cr = nullptr;
    if (wb->surface) cairo_surface_destroy(wb->surface);
    wb->surface = nullptr;
    if (wb->data) munmap(wb->data, wb->size);
    wb->data = nullptr;
    wb->size = 0;
    if (wb->buffer) wl_buffer_destroy(wb->buffer);
    wb->buffer = nullptr;
    wb->in_use = false;
}

static void destroy_all_buffers(OrbitStatus *ws) {
    destroy_buffer(&ws->buffers[0]);
    destroy_buffer(&ws->buffers[1]);
}

/* ------------------------------------------------------------------ */
/* Rendering                                                          */
/* ------------------------------------------------------------------ */

static void render(OrbitStatus *ws) {
    if (!ws->configured || !ws->bar) return;

    WlBuffer *wb = nullptr;
    for (int i = 0; i < 2; i++) {
        if (!ws->buffers[i].in_use) { wb = &ws->buffers[i]; break; }
    }
    if (!wb) return;
    if (create_buffer(ws, wb, ws->width, ws->height) < 0) return;

    cairo_t *cr = wb->cr;
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    bar_render(ws->bar, cr);
    cairo_surface_flush(wb->surface);

    wb->in_use = true;
    wl_surface_attach(ws->surface, wb->buffer, 0, 0);
    wl_surface_damage_buffer(ws->surface, 0, 0, ws->width, ws->height);
    wl_surface_commit(ws->surface);

    if (ws->popup.visible && ws->popup.buffer && ws->popup.surface) {
        wl_surface_attach(ws->popup.surface, ws->popup.buffer, 0, 0);
        wl_surface_damage_buffer(ws->popup.surface, 0, 0, ws->popup.width, ws->popup.height);
        wl_surface_commit(ws->popup.surface);
    }
}

/* ------------------------------------------------------------------ */
/* Popup (power/reboot/suspend confirmation)                          */
/* ------------------------------------------------------------------ */

static int popup_create_buffer(OrbitStatus *ws) {
    if (ws->popup.buffer) return 0;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->popup.width);
    size_t size = (size_t)stride * ws->popup.height;
    int fd = create_shm_fd(size);
    if (fd < 0) return -1;

    wl_shm_pool *pool = wl_shm_create_pool(ws->shm, fd, size);
    ws->popup.buffer = wl_shm_pool_create_buffer(pool, 0,
        ws->popup.width, ws->popup.height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    ws->popup.shm_data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ws->popup.shm_data == MAP_FAILED) { ws->popup.shm_data = nullptr; return -1; }

    ws->popup.cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char *)ws->popup.shm_data, CAIRO_FORMAT_ARGB32,
        ws->popup.width, ws->popup.height, stride);
    ws->popup.cr = cairo_create(ws->popup.cairo_surface);
    return 0;
}

static void popup_destroy_buffer(OrbitStatus *ws) {
    if (ws->popup.cr) cairo_destroy(ws->popup.cr);
    ws->popup.cr = nullptr;
    if (ws->popup.cairo_surface) cairo_surface_destroy(ws->popup.cairo_surface);
    ws->popup.cairo_surface = nullptr;
    if (ws->popup.shm_data) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->popup.width);
        munmap(ws->popup.shm_data, (size_t)stride * ws->popup.height);
    }
    ws->popup.shm_data = nullptr;
    if (ws->popup.buffer) wl_buffer_destroy(ws->popup.buffer);
    ws->popup.buffer = nullptr;
}

static void popup_render(OrbitStatus *ws) {
    if (!ws->popup.visible || !ws->popup.cr) return;
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

    const char *ff = config_get(ws->cfg, "font_family", "Sans");
    char popup_title_font[64];
    snprintf(popup_title_font, sizeof(popup_title_font), "%s Bold 13", ff);
    PangoLayout *lay = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(popup_title_font);
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

    char btn_font[64];
    snprintf(btn_font, sizeof(btn_font), "%s Bold 11", ff);
    PangoFontDescription *fb = pango_font_description_from_string(btn_font);
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

static void popup_destroy(OrbitStatus *ws) {
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
    OrbitStatus *ws = (OrbitStatus *)data;
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
    zwlr_layer_surface_v1 *surface) {
    ((OrbitStatus *)data)->popup.visible = false;
}

static const zwlr_layer_surface_v1_listener popup_layer_surface_listener = {
    .configure = popup_layer_surface_configure,
    .closed = popup_layer_surface_closed,
};

static void popup_create(OrbitStatus *ws, int action) {
    if (ws->popup.visible) popup_destroy(ws);

    ws->popup.width = 175;
    ws->popup.height = 105;
    ws->popup.action = action;
    ws->popup.hovered_btn = -1;

    ws->popup.surface = wl_compositor_create_surface(ws->compositor);
    ws->popup.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ws->layer_shell, ws->popup.surface, nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "orbit-status-popup");

    zwlr_layer_surface_v1_add_listener(ws->popup.layer_surface,
        &popup_layer_surface_listener, ws);

    zwlr_layer_surface_v1_set_size(ws->popup.layer_surface, ws->popup.width, ws->popup.height);

    const char *anchor_str = config_get(ws->cfg, "bar_anchor", "top");
    bool bar_on_bottom = (strcmp(anchor_str, "bottom") == 0);
    if (bar_on_bottom) {
        zwlr_layer_surface_v1_set_anchor(ws->popup.layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_margin(ws->popup.layer_surface,
            0, BAR_PADDING, ws->height + 4, 0);
    } else {
        zwlr_layer_surface_v1_set_anchor(ws->popup.layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_margin(ws->popup.layer_surface,
            ws->height + 4, BAR_PADDING, 0, 0);
    }
    zwlr_layer_surface_v1_set_exclusive_zone(ws->popup.layer_surface, 0);

    ws->popup.visible = true;
    wl_surface_commit(ws->popup.surface);
    wl_display_roundtrip(ws->display);
}

/* ------------------------------------------------------------------ */
/* Volume slider                                                      */
/* ------------------------------------------------------------------ */

// Read the current volume (0..100) from pactl.
static int volume_read_current(void) {
    FILE *fp = popen("pactl get-sink-volume @DEFAULT_SINK@", "r");
    if (!fp) return 0;
    char buf[256] = {0};
    if (fgets(buf, sizeof(buf), fp) == nullptr) { pclose(fp); return 0; }
    pclose(fp);
    const char *pct = strstr(buf, "/");
    if (!pct) return 0;
    pct++;
    while (*pct == ' ') pct++;
    int v = atoi(pct);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return v;
}

static void volume_set(int v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %d%%", v);
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)nullptr);
        _exit(127);
    }
}

static int volume_slider_create_buffer(OrbitStatus *ws) {
    if (ws->volume.buffer) return 0;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->volume.width);
    size_t size = (size_t)stride * ws->volume.height;
    int fd = create_shm_fd(size);
    if (fd < 0) return -1;

    wl_shm_pool *pool = wl_shm_create_pool(ws->shm, fd, size);
    ws->volume.buffer = wl_shm_pool_create_buffer(pool, 0,
        ws->volume.width, ws->volume.height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    ws->volume.shm_data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ws->volume.shm_data == MAP_FAILED) { ws->volume.shm_data = nullptr; return -1; }

    ws->volume.cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char *)ws->volume.shm_data, CAIRO_FORMAT_ARGB32,
        ws->volume.width, ws->volume.height, stride);
    ws->volume.cr = cairo_create(ws->volume.cairo_surface);
    return 0;
}

static void volume_slider_destroy_buffer(OrbitStatus *ws) {
    if (ws->volume.cr) cairo_destroy(ws->volume.cr);
    ws->volume.cr = nullptr;
    if (ws->volume.cairo_surface) cairo_surface_destroy(ws->volume.cairo_surface);
    ws->volume.cairo_surface = nullptr;
    if (ws->volume.shm_data) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->volume.width);
        munmap(ws->volume.shm_data, (size_t)stride * ws->volume.height);
    }
    ws->volume.shm_data = nullptr;
    if (ws->volume.buffer) wl_buffer_destroy(ws->volume.buffer);
    ws->volume.buffer = nullptr;
}

static void volume_slider_render(OrbitStatus *ws) {
    if (!ws->volume.visible || !ws->volume.cr) return;
    cairo_t *cr = ws->volume.cr;
    int w = ws->volume.width, h = ws->volume.height;

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

    // Volume percentage label at the top.
    const char *ff = config_get(ws->cfg, "font_family", "Sans");
    char vol_font[64];
    snprintf(vol_font, sizeof(vol_font), "%s Bold 14", ff);
    PangoLayout *lay = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(vol_font);
    pango_layout_set_font_description(lay, fd);
    pango_font_description_free(fd);
    char vol_text[16];
    snprintf(vol_text, sizeof(vol_text), "%d%%", ws->volume.volume);
    pango_layout_set_text(lay, vol_text, -1);
    int tw, th;
    pango_layout_get_pixel_size(lay, &tw, &th);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, (w - tw) / 2, 14);
    pango_cairo_show_layout(cr, lay);
    g_object_unref(lay);

    // Vertical track.
    int track_w = 8;
    int track_x = (w - track_w) / 2;
    int track_y = 44;
    int track_h = h - track_y - 24;

    // Track background.
    cairo_set_source_rgba(cr, 0.25, 0.25, 0.35, 0.8);
    draw_rounded_rect(cr, track_x, track_y, track_w, track_h, track_w / 2);
    cairo_fill(cr);

    // Filled portion (from bottom up to the volume level).
    int fill_h = (int)((double)track_h * ws->volume.volume / 100.0);
    int fill_y = track_y + track_h - fill_h;
    cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, 0.9);
    draw_rounded_rect(cr, track_x, fill_y, track_w, fill_h, track_w / 2);
    cairo_fill(cr);

    // Handle: a circle centered on the track at the volume level.
    int handle_r = 9;
    int handle_cx = track_x + track_w / 2;
    int handle_cy = track_y + track_h - fill_h;
    cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
    cairo_arc(cr, handle_cx, handle_cy, handle_r, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, 1.0);
    cairo_arc(cr, handle_cx, handle_cy, handle_r - 3, 0, 2 * M_PI);
    cairo_fill(cr);

    // Store geometry for hit-testing.
    ws->volume.track_x = track_x;
    ws->volume.track_y = track_y;
    ws->volume.track_w = track_w;
    ws->volume.track_h = track_h;
    ws->volume.handle_x = handle_cx - handle_r;
    ws->volume.handle_y = handle_cy - handle_r;
    ws->volume.handle_w = handle_r * 2;
    ws->volume.handle_h = handle_r * 2;

    cairo_surface_flush(ws->volume.cairo_surface);
    wl_surface_attach(ws->volume.surface, ws->volume.buffer, 0, 0);
    wl_surface_damage_buffer(ws->volume.surface, 0, 0, w, h);
    wl_surface_commit(ws->volume.surface);
}

static void volume_slider_layer_surface_configure(void *data,
    zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t width, uint32_t height) {
    OrbitStatus *ws = (OrbitStatus *)data;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    if (width > 0) ws->volume.width = width;
    if (height > 0) ws->volume.height = height;
    if (!ws->volume.configured) {
        ws->volume.configured = true;
        if (volume_slider_create_buffer(ws) == 0)
            volume_slider_render(ws);
    }
}

static void volume_slider_layer_surface_closed(void *data,
    zwlr_layer_surface_v1 *surface) {
    OrbitStatus *ws = (OrbitStatus *)data;
    volume_slider_destroy(ws);
}

static const zwlr_layer_surface_v1_listener volume_slider_layer_surface_listener = {
    .configure = volume_slider_layer_surface_configure,
    .closed = volume_slider_layer_surface_closed,
};

static void volume_slider_show(OrbitStatus *ws) {
    if (ws->volume.visible) volume_slider_destroy(ws);
    if (ws->popup.visible) popup_destroy(ws);

    ws->volume.width = 64;
    ws->volume.height = 200;
    ws->volume.volume = volume_read_current();
    ws->volume.dragging = false;

    ws->volume.surface = wl_compositor_create_surface(ws->compositor);
    ws->volume.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ws->layer_shell, ws->volume.surface, nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "orbit-status-volume");

    zwlr_layer_surface_v1_add_listener(ws->volume.layer_surface,
        &volume_slider_layer_surface_listener, ws);

    zwlr_layer_surface_v1_set_size(ws->volume.layer_surface, ws->volume.width, ws->volume.height);

    const char *anchor_str = config_get(ws->cfg, "bar_anchor", "top");
    bool bar_on_bottom = (strcmp(anchor_str, "bottom") == 0);
    if (bar_on_bottom) {
        zwlr_layer_surface_v1_set_anchor(ws->volume.layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_margin(ws->volume.layer_surface,
            0, BAR_PADDING, ws->height + 4, 0);
    } else {
        zwlr_layer_surface_v1_set_anchor(ws->volume.layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_margin(ws->volume.layer_surface,
            ws->height + 4, BAR_PADDING, 0, 0);
    }
    zwlr_layer_surface_v1_set_exclusive_zone(ws->volume.layer_surface, 0);

    ws->volume.visible = true;
    wl_surface_commit(ws->volume.surface);
    wl_display_roundtrip(ws->display);
}

static void volume_slider_destroy(OrbitStatus *ws) {
    if (!ws->volume.visible) return;
    ws->volume.visible = false;
    ws->volume.dragging = false;
    volume_slider_destroy_buffer(ws);
    if (ws->volume.layer_surface) {
        zwlr_layer_surface_v1_destroy(ws->volume.layer_surface);
        ws->volume.layer_surface = nullptr;
    }
    if (ws->volume.surface) {
        wl_surface_destroy(ws->volume.surface);
        ws->volume.surface = nullptr;
    }
    ws->volume.configured = false;
}

// Update the volume from a pointer y position within the slider. Returns the
// new volume (0..100).
static int volume_slider_volume_from_y(OrbitStatus *ws, int y) {
    int track_top = ws->volume.track_y;
    int track_bottom = ws->volume.track_y + ws->volume.track_h;
    if (y <= track_top) return 100;
    if (y >= track_bottom) return 0;
    // Volume increases as the handle moves up.
    double frac = (double)(track_bottom - y) / (double)ws->volume.track_h;
    int v = (int)(frac * 100.0 + 0.5);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return v;
}

// Handle pointer motion over the slider. If dragging, update the volume.
static void volume_slider_handle_motion(OrbitStatus *ws, int x, int y) {
    if (!ws->volume.visible || !ws->volume.dragging) return;
    int v = volume_slider_volume_from_y(ws, y);
    if (v != ws->volume.volume) {
        ws->volume.volume = v;
        volume_set(v);
        volume_slider_render(ws);
    }
}

// Handle a pointer button event over the slider. Returns true if handled.
static bool volume_slider_handle_button(OrbitStatus *ws, int x, int y,
                                        uint32_t button, uint32_t state) {
    if (!ws->volume.visible) return false;
    if (button != 0x110) return true;  // only left button; consume others

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        // If clicking on the handle, start dragging.
        if (x >= ws->volume.handle_x && x < ws->volume.handle_x + ws->volume.handle_w &&
            y >= ws->volume.handle_y && y < ws->volume.handle_y + ws->volume.handle_h) {
            ws->volume.dragging = true;
            return true;
        }
        // Otherwise, jump to the clicked position on the track.
        int v = volume_slider_volume_from_y(ws, y);
        ws->volume.volume = v;
        volume_set(v);
        volume_slider_render(ws);
        return true;
    } else {  // release
        ws->volume.dragging = false;
        return true;
    }
}

/* ------------------------------------------------------------------ */
/* Tooltip                                                            */
/* ------------------------------------------------------------------ */

static void tooltip_destroy(OrbitStatus *ws) {
    if (!ws->tooltip.visible) return;
    ws->tooltip.visible = false;
    ws->tooltip.hovered_clickable = -1;
    if (ws->tooltip.cr) cairo_destroy(ws->tooltip.cr);
    ws->tooltip.cr = nullptr;
    if (ws->tooltip.cairo_surface) cairo_surface_destroy(ws->tooltip.cairo_surface);
    ws->tooltip.cairo_surface = nullptr;
    if (ws->tooltip.shm_data) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->tooltip.width);
        munmap(ws->tooltip.shm_data, (size_t)stride * ws->tooltip.height);
    }
    ws->tooltip.shm_data = nullptr;
    if (ws->tooltip.buffer) wl_buffer_destroy(ws->tooltip.buffer);
    ws->tooltip.buffer = nullptr;
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
    OrbitStatus *ws = (OrbitStatus *)data;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    if (width > 0) ws->tooltip.width = width;
    if (height > 0) ws->tooltip.height = height;
    if (!ws->tooltip.buffer) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->tooltip.width);
        size_t size = (size_t)stride * ws->tooltip.height;
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

        const char *tip_ff = config_get(ws->cfg, "font_family", "Sans");
        char tip_font[64];
        snprintf(tip_font, sizeof(tip_font), "%s 10", tip_ff);
        PangoLayout *lay = pango_cairo_create_layout(cr);
        PangoFontDescription *fdesc = pango_font_description_from_string(tip_font);
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
    zwlr_layer_surface_v1 *surface) {
    ((OrbitStatus *)data)->tooltip.visible = false;
}

static const zwlr_layer_surface_v1_listener tooltip_layer_surface_listener = {
    .configure = tooltip_layer_surface_configure,
    .closed = tooltip_layer_surface_closed,
};

static void tooltip_show(OrbitStatus *ws, const char *text, int hover_x, int hover_y) {
    if (ws->tooltip.visible && strcmp(ws->tooltip.text, text) == 0)
        return;

    tooltip_destroy(ws);

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
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "orbit-status-tooltip");

    zwlr_layer_surface_v1_add_listener(ws->tooltip.layer_surface,
        &tooltip_layer_surface_listener, ws);

    zwlr_layer_surface_v1_set_size(ws->tooltip.layer_surface, ws->tooltip.width, ws->tooltip.height);

    const char *anchor_str = config_get(ws->cfg, "bar_anchor", "top");
    bool bar_on_bottom = (strcmp(anchor_str, "bottom") == 0);
    if (bar_on_bottom) {
        zwlr_layer_surface_v1_set_anchor(ws->tooltip.layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_margin(ws->tooltip.layer_surface,
            hover_y - ws->tooltip.height - 4, BAR_PADDING, 0, 0);
    } else {
        zwlr_layer_surface_v1_set_anchor(ws->tooltip.layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_margin(ws->tooltip.layer_surface,
            ws->height + 4, BAR_PADDING, 0, 0);
    }
    zwlr_layer_surface_v1_set_exclusive_zone(ws->tooltip.layer_surface, 0);

    ws->tooltip.visible = true;
    wl_surface_commit(ws->tooltip.surface);
}

/* ------------------------------------------------------------------ */
/* Input                                                              */
/* ------------------------------------------------------------------ */

static void execute_command(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, nullptr);
        _exit(1);
    }
}

static void pointer_enter(void *data, wl_pointer *pointer,
    uint32_t serial, wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
    OrbitStatus *ws = (OrbitStatus *)data;
    ws->current_pointer_surface = surface;
    ws->pointer_x = wl_fixed_to_int(sx);
    ws->pointer_y = wl_fixed_to_int(sy);

    if (ws->popup.visible && surface == ws->popup.surface)
        return;

    bar_update_hover(ws->bar, ws->pointer_x, ws->pointer_y);
    sni_tray_update_hover(&ws->tray, ws->pointer_x, ws->pointer_y);
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

static void pointer_leave(void *data, wl_pointer *pointer,
    uint32_t serial, wl_surface *surface) {
    OrbitStatus *ws = (OrbitStatus *)data;
    ws->current_pointer_surface = nullptr;

    if (ws->popup.visible && surface == ws->popup.surface) {
        if (ws->popup.hovered_btn != -1) {
            ws->popup.hovered_btn = -1;
            popup_render(ws);
        }
        return;
    }

    if (ws->volume.visible && surface == ws->volume.surface) {
        // Stop dragging but keep the slider visible.
        ws->volume.dragging = false;
        return;
    }

    bar_clear_hover(ws->bar);
    render(ws);
    if (ws->tooltip.visible)
        tooltip_destroy(ws);
}

static void pointer_motion(void *data, wl_pointer *pointer,
    uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    OrbitStatus *ws = (OrbitStatus *)data;
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

    // Volume slider drag.
    if (ws->volume.visible && ws->current_pointer_surface == ws->volume.surface) {
        volume_slider_handle_motion(ws, x, y);
        return;
    }

    int old_power = ws->bar->power_hovered;
    int old_ws = ws->bar->hovered_workspace;
    bool tray_hover_changed = false;
    if (ws->tray.conn)
        tray_hover_changed = sni_tray_update_hover(&ws->tray, x, y);
    bar_update_hover(ws->bar, x, y);
    if (old_power != ws->bar->power_hovered || old_ws != ws->bar->hovered_workspace || tray_hover_changed)
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

static void pointer_button(void *data, wl_pointer *pointer,
    uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    OrbitStatus *ws = (OrbitStatus *)data;

    int x = ws->pointer_x, y = ws->pointer_y;

    // Volume slider handles its own button press/release (for dragging).
    if (ws->volume.visible && ws->current_pointer_surface == ws->volume.surface) {
        if (volume_slider_handle_button(ws, x, y, button, state))
            return;
    }

    if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;

    // Left-click on the volume tray item toggles the slider.
    if (button == 0x110 && ws->tray.conn) {
        int idx = sni_tray_item_at(&ws->tray, x, y);
        if (idx >= 0 && strcmp(ws->tray.items[idx].id, "volume") == 0) {
            if (ws->volume.visible)
                volume_slider_destroy(ws);
            else
                volume_slider_show(ws);
            return;
        }
    }

    // If the volume slider is visible and the click is outside it, close it.
    if (ws->volume.visible && ws->current_pointer_surface != ws->volume.surface)
        volume_slider_destroy(ws);

    // Tray handles left / middle / right clicks on its icons.
    if (ws->tray.conn && sni_tray_handle_click(&ws->tray, x, y, button))
        return;

    /* All remaining clicks are left button only. */
    if (button != 0x110) return; /* BTN_LEFT */

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
            // Lua plugin pills: invoke the plugin's on_click handler.
            if (c->lua_plugin_idx >= 0 &&
                c->lua_plugin_idx < ws->bar->n_lua_plugins) {
                lua_plugin_call_onclick(
                    &ws->bar->lua_plugins[c->lua_plugin_idx]);
                bar_update_lua_plugins(ws->bar);
                render(ws);
            }
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
            case CLICK_WORKSPACE:
                execute_command(c->command);
                break;
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

static void pointer_axis(void *data, wl_pointer *pointer,
    uint32_t time, uint32_t axis, wl_fixed_t value) {
    OrbitStatus *ws = (OrbitStatus *)data;
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;

    int x = ws->pointer_x, y = ws->pointer_y;

    // Tray handles scroll over its icons (e.g. volume control).
    if (ws->tray.conn && sni_tray_handle_scroll(&ws->tray, x, y, wl_fixed_to_int(value)))
        return;

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

static void pointer_frame(void *data, wl_pointer *pointer) {}

static const wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
};

static void seat_capabilities(void *data, wl_seat *seat, uint32_t capabilities) {
    OrbitStatus *ws = (OrbitStatus *)data;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !ws->pointer) {
        ws->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(ws->pointer, &pointer_listener, ws);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && ws->pointer) {
        wl_pointer_destroy(ws->pointer);
        ws->pointer = nullptr;
    }
}

static void seat_name(void *data, wl_seat *seat, const char *name) {}

static const wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

/* ------------------------------------------------------------------ */
/* Registry / layer surface                                           */
/* ------------------------------------------------------------------ */

static void registry_global(void *data, wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version) {
    OrbitStatus *ws = (OrbitStatus *)data;

    // Bind at the minimum of the version we need and the version the
    // compositor advertises, to avoid requesting an unsupported version.
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        ws->compositor = static_cast<wl_compositor *>(
            wl_registry_bind(registry, name, &wl_compositor_interface,
                version < 4 ? version : 4));
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        ws->shm = static_cast<wl_shm *>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
        ws->layer_shell = static_cast<zwlr_layer_shell_v1 *>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface,
                version < 1 ? version : 1));
    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        ws->seat = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface,
                version < 1 ? version : 1));
        wl_seat_add_listener(ws->seat, &seat_listener, ws);
    }
}

static void registry_global_remove(void *data,
    wl_registry *registry, uint32_t name) {}

static const wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void layer_surface_configure(void *data,
    zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t width, uint32_t height) {
    OrbitStatus *ws = (OrbitStatus *)data;

    if (width == 0) width = ws->width;
    if (height == 0) height = config_get_int(ws->cfg, "bar_height", BAR_HEIGHT);

    if (ws->width != (int)width || ws->height != (int)height) {
        destroy_all_buffers(ws);
        ws->width = width;
        ws->height = height;
        if (ws->bar) bar_destroy(ws->bar);
        ws->bar = bar_create(ws->width, ws->height, ws->cfg, "swaymsg workspace number %d");
        ws->bar->tray = &ws->tray;
    }
    if (!ws->bar) {
        ws->bar = bar_create(ws->width, ws->height, ws->cfg, "swaymsg workspace number %d");
        ws->bar->tray = &ws->tray;
    }

    zwlr_layer_surface_v1_ack_configure(surface, serial);
    ws->configured = true;

    sway_ipc_update_workspaces(ws->bar);
    if (config_get_int(ws->cfg, "show_active_window", 1))
        sway_ipc_update_active_window(ws->bar);
    bar_update_lua_plugins(ws->bar);
    render(ws);
}

static void layer_surface_closed(void *data,
    zwlr_layer_surface_v1 *surface) {
    ((OrbitStatus *)data)->running = false;
}

static const zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static int setup_layer_surface(OrbitStatus *ws) {
    ws->surface = wl_compositor_create_surface(ws->compositor);
    if (!ws->surface) return -1;

    const char *layer_str = config_get(ws->cfg, "bar_layer", "top");
    enum zwlr_layer_shell_v1_layer layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    if (strcmp(layer_str, "overlay") == 0)
        layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;

    ws->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ws->layer_shell, ws->surface, nullptr, layer, "orbit-status");
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
        fprintf(stderr, "orbit-status: layer surface was not configured\n");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Plugin watches / reload                                            */
/* ------------------------------------------------------------------ */

static void setup_plugin_watches(OrbitStatus *ws) {
    if (!ws->bar) return;
    // Close any previous inotify fd (e.g. from an earlier reload).
    if (ws->inotify_fd >= 0) {
        close(ws->inotify_fd);
        ws->inotify_fd = -1;
    }
    ws->inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (ws->inotify_fd < 0) return;

    for (int i = 0; i < ws->bar->n_lua_plugins; i++) {
        char watchkey[32];
        snprintf(watchkey, sizeof(watchkey), "lua_plugin_%d_watch", i + 1);
        const char *watch = config_get(ws->cfg, watchkey, "");
        if (watch[0]) {
            int wd = inotify_add_watch(ws->inotify_fd, watch, IN_MODIFY);
            if (wd >= 0) ws->bar->lua_plugins[i].watch_wd = wd;
        }
    }

    const char *plugins_dir = config_get(ws->cfg, "lua_plugins_dir", nullptr);
    if (!plugins_dir) {
        const char *home = getenv("HOME");
        if (home) {
            static char dir[256];
            snprintf(dir, sizeof(dir), "%s/.config/orbit-status/plugins", home);
            plugins_dir = dir;
        }
    }

    if (plugins_dir) {
        auto add_watch_for_plugin = [&](int idx, const char *sysfs) {
            if (idx < 0 || idx >= ws->bar->n_lua_plugins) return;
            if (ws->bar->lua_plugins[idx].watch_wd >= 0) return;
            if (access(sysfs, F_OK) != 0) return;
            int wd = inotify_add_watch(ws->inotify_fd, sysfs, IN_MODIFY);
            if (wd >= 0) ws->bar->lua_plugins[idx].watch_wd = wd;
        };

        for (int i = 0; i < ws->bar->n_lua_plugins; i++) {
            const char *pname = ws->bar->lua_plugins[i].path;
            if (!pname[0]) continue;

            if (strstr(pname, "battery")) {
                add_watch_for_plugin(i, "/sys/class/power_supply/BAT0/uevent");
            } else if (strstr(pname, "brightness")) {
                static const char *backlights[] = {
                    "/sys/class/backlight/intel_backlight/brightness",
                    "/sys/class/backlight/amdgpu_bl0/brightness",
                    "/sys/class/backlight/nvidia_0/brightness",
                    nullptr
                };
                for (int b = 0; backlights[b]; b++) {
                    if (access(backlights[b], F_OK) == 0) {
                        add_watch_for_plugin(i, backlights[b]);
                        break;
                    }
                }
            } else if (strstr(pname, "network") || strstr(pname, "net")) {
                add_watch_for_plugin(i, "/sys/class/net");
            }
        }
    }
}

static void reload(OrbitStatus *ws) {
    if (ws->popup.visible) popup_destroy(ws);
    if (ws->volume.visible) volume_slider_destroy(ws);
    if (ws->tooltip.visible) tooltip_destroy(ws);
    destroy_all_buffers(ws);
    if (ws->bar) bar_destroy(ws->bar);
    config_destroy(ws->cfg);
    ws->cfg = config_load(config_path());
    ws->bar = bar_create(ws->width, ws->height, ws->cfg, "swaymsg workspace number %d");
    ws->bar->tray = &ws->tray;

    // Refresh tray icon size / enabled state from the new config.
    if (config_get_int(ws->cfg, "show_tray", 1)) {
        ws->tray.icon_size = config_get_int(ws->cfg, "tray_icon_size", 24);
        // The initial tray may be uninitialized if show_tray was 0 at launch.
        if (!ws->tray.conn) {
            if (sni_tray_init(&ws->tray, ws->tray.icon_size, on_tray_change, ws) < 0)
                fprintf(stderr, "orbit-status: tray disabled (DBus unavailable)\n");
        }
    } else if (ws->tray.conn) {
        // show_tray was turned off: tear down the tray.
        sni_tray_destroy(&ws->tray);
    }
    setup_plugin_watches(ws);
    sway_ipc_update_workspaces(ws->bar);
    if (config_get_int(ws->cfg, "show_active_window", 1))
        sway_ipc_update_active_window(ws->bar);
    bar_update_lua_plugins(ws->bar);
    render(ws);
}

static void on_timer(OrbitStatus *ws) {
    if (!ws->bar || !ws->running) return;
    sway_ipc_update_workspaces(ws->bar);
    if (config_get_int(ws->cfg, "show_active_window", 1))
        sway_ipc_update_active_window(ws->bar);
    bar_update_lua_plugins(ws->bar);
    render(ws);
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main() {
    OrbitStatus ws;

    ws.cfg = config_load(config_path());
    int bh = config_get_int(ws.cfg, "bar_height", BAR_HEIGHT);
    ws.width = 1920;
    ws.height = bh;
    ws.running = true;

    // Initialize the StatusNotifier tray (DBus).
    if (config_get_int(ws.cfg, "show_tray", 1)) {
        int tray_icon_size = config_get_int(ws.cfg, "tray_icon_size", 24);
        if (sni_tray_init(&ws.tray, tray_icon_size, on_tray_change, &ws) < 0)
            fprintf(stderr, "orbit-status: tray disabled (DBus unavailable)\n");
    }

    ws.display = wl_display_connect(nullptr);
    if (!ws.display) {
        fprintf(stderr, "orbit-status: failed to connect to Wayland display\n");
        return 1;
    }

    wl_registry *registry = wl_display_get_registry(ws.display);
    wl_registry_add_listener(registry, &registry_listener, &ws);
    wl_display_roundtrip(ws.display);

    if (!ws.compositor || !ws.shm || !ws.layer_shell) {
        fprintf(stderr, "orbit-status: missing required Wayland globals "
            "(compositor/shm/layer-shell)\n");
        return 1;
    }

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

    struct sigaction sc = {};
    sc.sa_handler = handle_sigchld;
    sigemptyset(&sc.sa_mask);
    sigaction(SIGCHLD, &sc, nullptr);

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
        int tray_fd = ws.tray.conn ? sni_tray_get_fd(&ws.tray) : -1;
        struct pollfd fds[4] = {
            {.fd = wl_display_get_fd(ws.display), .events = POLLIN},
            {.fd = ws.timer_fd, .events = POLLIN},
            {.fd = ws.inotify_fd, .events = POLLIN},
            {.fd = tray_fd, .events = POLLIN},
        };
        int nfds = 1;
        if (ws.timer_fd >= 0) nfds = 2;
        if (ws.inotify_fd >= 0) nfds = 3;
        if (tray_fd >= 0) nfds = 4;
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
                for (char *p = buf; p < buf + len; ) {
                    const auto *ev = (const struct inotify_event *)p;
                    if (ev->len > 0 && strcmp(ev->name, "config") == 0) {
                        reload(&ws);
                        break;
                    }
                    for (int i = 0; i < ws.bar->n_lua_plugins; i++) {
                        if (ws.bar->lua_plugins[i].watch_wd == (int)ev->wd) {
                            ws.bar->lua_plugins[i].last_check = 0;
                        }
                    }
                    p += sizeof(struct inotify_event) + ev->len;
                }
                bar_update_lua_plugins(ws.bar);
                render(&ws);
            }
        }

        if (tray_fd >= 0 && (fds[3].revents & POLLIN)) {
            sni_tray_dispatch(&ws.tray);
        }

check_reload:
        if (reload_requested) {
            reload_requested = 0;
            reload(&ws);
        }
    }

    if (ws.timer_fd >= 0) close(ws.timer_fd);
    if (ws.inotify_fd >= 0) close(ws.inotify_fd);
    if (ws.tray.conn) sni_tray_destroy(&ws.tray);
    sway_ipc_disconnect();
    if (ws.popup.visible) popup_destroy(&ws);
    if (ws.volume.visible) volume_slider_destroy(&ws);
    if (ws.tooltip.visible) tooltip_destroy(&ws);
    destroy_all_buffers(&ws);
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