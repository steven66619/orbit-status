#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <wayland-client.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <sys/inotify.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include "wlr-layer-shell-unstable-v1-client.h"
#include <cairo.h>
#include <pango/pangocairo.h>
#include <xkbcommon/xkbcommon.h>
#include "bar.h"
#include "config.h"

void config_destroy(struct config *cfg);

struct wl_status {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_surface *surface;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct xkb_context *xkb_ctx;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;

    struct wl_buffer *buffer;
    cairo_surface_t *cairo_surface;
    cairo_t *cr;
    void *shm_data;
    int width, height;

    struct bar *bar;
    struct config *cfg;
    bool configured;
    bool running;
    uint32_t configure_serial;
    int pointer_x, pointer_y;
    struct wl_surface *current_pointer_surface;

    int timer_fd;

        int inotify_fd;

    struct {
        struct wl_surface *surface;
        struct zwlr_layer_surface_v1 *layer_surface;
        struct wl_buffer *buffer;
        cairo_surface_t *cairo_surface;
        cairo_t *cr;
        void *shm_data;
        int width, height;
        bool visible, configured;
        int n_entries;
        struct { char name[64]; char exec[256]; } entries[64];
        int filtered[64];
        int n_filtered;
        int scroll_offset;
        char search[64];
        int hovered_idx;
        int cols;
        int entry_w, entry_h;
        int grid_x, grid_y;
    } launcher;

    struct {
        struct wl_surface *surface;
        struct zwlr_layer_surface_v1 *layer_surface;
        struct wl_buffer *buffer;
        cairo_surface_t *cairo_surface;
        cairo_t *cr;
        void *shm_data;
        int width, height;
        bool visible, configured;
        char text[512];
        int hovered_clickable;
        int hover_x, hover_y;
    } tooltip;

    struct {
        struct wl_surface *surface;
        struct zwlr_layer_surface_v1 *layer_surface;
        struct wl_buffer *buffer;
        cairo_surface_t *cairo_surface;
        cairo_t *cr;
        void *shm_data;
        int width, height;
        bool visible, configured;
        int action;
        int hovered_btn;
        int confirm_btn_x, confirm_btn_y, confirm_btn_w, confirm_btn_h;
        int cancel_btn_x, cancel_btn_y, cancel_btn_w, cancel_btn_h;
    } popup;
};

static int create_shm_fd(size_t size)
{
    int fd = memfd_create("wlstatus", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return -1; }
    return fd;
}

static int create_buffer(struct wl_status *ws)
{
    if (ws->buffer) return 0;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->width);
    int size = stride * ws->height;
    int fd = create_shm_fd(size);
    if (fd < 0) return -1;

    struct wl_shm_pool *pool = wl_shm_create_pool(ws->shm, fd, size);
    ws->buffer = wl_shm_pool_create_buffer(pool, 0, ws->width, ws->height,
        stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    ws->shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ws->shm_data == MAP_FAILED) return -1;

    ws->cairo_surface = cairo_image_surface_create_for_data(
        ws->shm_data, CAIRO_FORMAT_ARGB32, ws->width, ws->height, stride);
    ws->cr = cairo_create(ws->cairo_surface);
    return 0;
}

static void destroy_buffer(struct wl_status *ws)
{
    if (ws->cr) cairo_destroy(ws->cr);
    ws->cr = NULL;
    if (ws->cairo_surface) cairo_surface_destroy(ws->cairo_surface);
    ws->cairo_surface = NULL;
    if (ws->shm_data) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->width);
        munmap(ws->shm_data, stride * ws->height);
    }
    ws->shm_data = NULL;
    if (ws->buffer) wl_buffer_destroy(ws->buffer);
    ws->buffer = NULL;
}

static void render(struct wl_status *ws)
{
    bar_render(ws->bar, ws->cr);
    cairo_surface_flush(ws->cairo_surface);
    wl_surface_attach(ws->surface, ws->buffer, 0, 0);
    wl_surface_damage_buffer(ws->surface, 0, 0, ws->width, ws->height);
    wl_surface_commit(ws->surface);
}

static int popup_create_buffer(struct wl_status *ws)
{
    if (ws->popup.buffer) return 0;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->popup.width);
    int size = stride * ws->popup.height;
    int fd = create_shm_fd(size);
    if (fd < 0) return -1;

    struct wl_shm_pool *pool = wl_shm_create_pool(ws->shm, fd, size);
    ws->popup.buffer = wl_shm_pool_create_buffer(pool, 0,
        ws->popup.width, ws->popup.height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    ws->popup.shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ws->popup.shm_data == MAP_FAILED) return -1;

    ws->popup.cairo_surface = cairo_image_surface_create_for_data(
        ws->popup.shm_data, CAIRO_FORMAT_ARGB32, ws->popup.width, ws->popup.height, stride);
    ws->popup.cr = cairo_create(ws->popup.cairo_surface);
    return 0;
}

static void popup_destroy_buffer(struct wl_status *ws)
{
    if (ws->popup.cr) cairo_destroy(ws->popup.cr);
    ws->popup.cr = NULL;
    if (ws->popup.cairo_surface) cairo_surface_destroy(ws->popup.cairo_surface);
    ws->popup.cairo_surface = NULL;
    if (ws->popup.shm_data) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->popup.width);
        munmap(ws->popup.shm_data, stride * ws->popup.height);
    }
    ws->popup.shm_data = NULL;
    if (ws->popup.buffer) wl_buffer_destroy(ws->popup.buffer);
    ws->popup.buffer = NULL;
}

static void popup_render(struct wl_status *ws)
{
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

    const char *labels[] = {"Power Off", "Reboot", "Suspend"};
    PangoLayout *lay = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string("Sans Bold 13");
    pango_layout_set_font_description(lay, fd);
    pango_font_description_free(fd);
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

static void popup_destroy(struct wl_status *ws)
{
    if (!ws->popup.visible) return;
    ws->popup.visible = false;
    popup_destroy_buffer(ws);
    if (ws->popup.layer_surface) {
        zwlr_layer_surface_v1_destroy(ws->popup.layer_surface);
        ws->popup.layer_surface = NULL;
    }
    if (ws->popup.surface) {
        wl_surface_destroy(ws->popup.surface);
        ws->popup.surface = NULL;
    }
    ws->popup.configured = false;
}

static void popup_layer_surface_configure(void *data,
    struct zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t width, uint32_t height)
{
    struct wl_status *ws = data;
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
    struct zwlr_layer_surface_v1 *surface)
{
    ((struct wl_status *)data)->popup.visible = false;
}

static const struct zwlr_layer_surface_v1_listener popup_layer_surface_listener = {
    .configure = popup_layer_surface_configure,
    .closed = popup_layer_surface_closed,
};

static void popup_create(struct wl_status *ws, int action)
{
    if (ws->popup.visible) popup_destroy(ws);

    memset(&ws->popup, 0, sizeof(ws->popup));
    ws->popup.width = 175;
    ws->popup.height = 105;
    ws->popup.action = action;
    ws->popup.hovered_btn = -1;

    ws->popup.surface = wl_compositor_create_surface(ws->compositor);
    ws->popup.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ws->layer_shell, ws->popup.surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlstatus-popup");

    zwlr_layer_surface_v1_add_listener(ws->popup.layer_surface,
        &popup_layer_surface_listener, ws);

    zwlr_layer_surface_v1_set_size(ws->popup.layer_surface, ws->popup.width, ws->popup.height);
    zwlr_layer_surface_v1_set_anchor(ws->popup.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    const char *anchor_str = config_get(ws->cfg, "bar_anchor", "top");
    int bar_on_bottom = (strcmp(anchor_str, "bottom") == 0);
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
}

static void launcher_update_filter(struct wl_status *ws)
{
    ws->launcher.n_filtered = 0;
    ws->launcher.scroll_offset = 0;
    for (int i = 0; i < ws->launcher.n_entries && ws->launcher.n_filtered < 64; i++) {
        if (ws->launcher.search[0] == '\0' ||
            strcasestr(ws->launcher.entries[i].name, ws->launcher.search)) {
            ws->launcher.filtered[ws->launcher.n_filtered++] = i;
        }
    }
}

static void launcher_populate_entries(struct wl_status *ws)
{
    ws->launcher.n_entries = 0;
    FILE *fp = popen(
        "for f in /usr/share/applications/*.desktop; do "
        "  name=$(grep '^Name=' \"$f\" | head -1 | cut -d= -f2); "
        "  exec=$(grep '^Exec=' \"$f\" | head -1 | cut -d= -f2 | sed 's/%.//g' | cut -d' ' -f1); "
        "  [ -n \"$name\" ] && [ -n \"$exec\" ] && echo \"$name|$exec\"; "
        "done | sort -f", "r");
    if (!fp) return;
    char line[384];
    while (fgets(line, sizeof(line), fp) && ws->launcher.n_entries < 64) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *sep = strchr(line, '|');
        if (!sep) continue;
        *sep = '\0';
        sep++;
        char *nl2 = strchr(sep, '\n');
        if (nl2) *nl2 = '\0';
        int i = ws->launcher.n_entries++;
        line[sizeof(ws->launcher.entries[i].name) - 1] = '\0';
        snprintf(ws->launcher.entries[i].name, sizeof(ws->launcher.entries[i].name), "%s", line);
        sep[sizeof(ws->launcher.entries[i].exec) - 1] = '\0';
        snprintf(ws->launcher.entries[i].exec, sizeof(ws->launcher.entries[i].exec), "%s", sep);
    }
    pclose(fp);
    launcher_update_filter(ws);
}

static void launcher_destroy_buffer(struct wl_status *ws)
{
    if (ws->launcher.cr) cairo_destroy(ws->launcher.cr);
    ws->launcher.cr = NULL;
    if (ws->launcher.cairo_surface) cairo_surface_destroy(ws->launcher.cairo_surface);
    ws->launcher.cairo_surface = NULL;
    if (ws->launcher.shm_data) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->launcher.width);
        munmap(ws->launcher.shm_data, stride * ws->launcher.height);
    }
    ws->launcher.shm_data = NULL;
    if (ws->launcher.buffer) wl_buffer_destroy(ws->launcher.buffer);
    ws->launcher.buffer = NULL;
}

static int launcher_create_buffer(struct wl_status *ws)
{
    if (ws->launcher.buffer) return 0;
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->launcher.width);
    int size = stride * ws->launcher.height;
    int fd = create_shm_fd(size);
    if (fd < 0) return -1;

    struct wl_shm_pool *pool = wl_shm_create_pool(ws->shm, fd, size);
    ws->launcher.buffer = wl_shm_pool_create_buffer(pool, 0,
        ws->launcher.width, ws->launcher.height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    ws->launcher.shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ws->launcher.shm_data == MAP_FAILED) return -1;

    ws->launcher.cairo_surface = cairo_image_surface_create_for_data(
        ws->launcher.shm_data, CAIRO_FORMAT_ARGB32, ws->launcher.width, ws->launcher.height, stride);
    ws->launcher.cr = cairo_create(ws->launcher.cairo_surface);
    return 0;
}

static void launcher_render(struct wl_status *ws)
{
    cairo_t *cr = ws->launcher.cr;
    int w = ws->launcher.width, h = ws->launcher.height;

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    draw_rounded_rect(cr, 0, 0, w, h, 10);
    cairo_set_source_rgba(cr, 0.04, 0.02, 0.10, 0.97);
    cairo_fill(cr);

    float acc[4];
    float def_acc[] = {0.0f, 0.90f, 1.0f, 1.0f};
    config_get_color(ws->cfg, "accent_color", acc, def_acc);

    cairo_set_source_rgba(cr, acc[0] * 0.5, acc[1] * 0.5, acc[2] * 0.5, 0.08);
    cairo_set_line_width(cr, 1);
    for (int y = 0; y < h; y += 8) {
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, w, y);
    }
    for (int x = 0; x < w; x += 8) {
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, h);
    }
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.4);
    cairo_set_line_width(cr, 1.5);
    draw_rounded_rect(cr, 0, 0, w, h, 10);
    cairo_stroke(cr);

    PangoFontDescription *fd_title = pango_font_description_from_string("Monospace Bold 11");
    PangoLayout *lay_title = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(lay_title, fd_title);
    pango_font_description_free(fd_title);
    pango_layout_set_text(lay_title, "// LAUNCHER", -1);
    int tw, th;
    pango_layout_get_pixel_size(lay_title, &tw, &th);
    cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.9);
    cairo_move_to(cr, 14, 18);
    pango_cairo_show_layout(cr, lay_title);
    g_object_unref(lay_title);

    int search_box_y = 32;
    int sb_h = 26;
    char search_display[128];
    if (ws->launcher.search[0] == '\0') {
        snprintf(search_display, sizeof(search_display), ">_");
    } else {
        snprintf(search_display, sizeof(search_display), "> %s_", ws->launcher.search);
    }
    PangoFontDescription *fd_search = pango_font_description_from_string("Monospace 11");
    PangoLayout *lay_search = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(lay_search, fd_search);
    pango_layout_set_text(lay_search, search_display, -1);
    int stw, sth;
    pango_layout_get_pixel_size(lay_search, &stw, &sth);

    int sb_margin = 10;
    int sb_w = w - 2 * sb_margin;
    cairo_set_source_rgba(cr, 0.08, 0.06, 0.18, 0.9);
    draw_rounded_rect(cr, sb_margin, search_box_y, sb_w, sb_h, 4);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.5);
    cairo_set_line_width(cr, 1);
    draw_rounded_rect(cr, sb_margin, search_box_y, sb_w, sb_h, 4);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, 0.7, 0.75, 1.0);
    cairo_move_to(cr, sb_margin + 8, search_box_y + (sb_h - sth) / 2);
    pango_cairo_show_layout(cr, lay_search);
    g_object_unref(lay_search);
    pango_font_description_free(fd_search);

    int cols = ws->launcher.cols;
    int ew = ws->launcher.entry_w;
    int eh = ws->launcher.entry_h;
    int gx = ws->launcher.grid_x;
    int gy = ws->launcher.grid_y;

    int grid_skip = ws->launcher.scroll_offset * cols;
    int n_visible = ws->launcher.n_filtered - grid_skip;
    if (n_visible < 0) n_visible = 0;

    int grid_top = gy;
    int max_rows = (h - grid_top) / (eh + 6);
    if (n_visible > max_rows * cols) n_visible = max_rows * cols;

    PangoFontDescription *fd_entry = pango_font_description_from_string("Sans 10");
    for (int j = 0; j < n_visible; j++) {
        int i = ws->launcher.filtered[grid_skip + j];
        int row = j / cols;
        int col = j % cols;
        int ex = gx + col * (ew + 8);
        int ey = grid_top + row * (eh + 6);

        if (ex + ew > w || ey + eh > h) break;

        bool hovered = (i == ws->launcher.hovered_idx);
        double r = 6;

        if (hovered) {
            cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.15);
            cairo_set_line_width(cr, 0);
            draw_rounded_rect(cr, ex - 2, ey - 2, ew + 4, eh + 4, r + 1);
            cairo_fill(cr);
        }

        cairo_set_source_rgba(cr, 0.12, 0.10, 0.22, 0.8);
        draw_rounded_rect(cr, ex, ey, ew, eh, r);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], hovered ? 0.9 : 0.35);
        cairo_set_line_width(cr, hovered ? 1.5 : 1);
        draw_rounded_rect(cr, ex, ey, ew, eh, r);
        cairo_stroke(cr);

        PangoLayout *lay = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(lay, fd_entry);
        pango_layout_set_width(lay, (ew - 12) * PANGO_SCALE);
        pango_layout_set_ellipsize(lay, PANGO_ELLIPSIZE_END);
        pango_layout_set_alignment(lay, PANGO_ALIGN_CENTER);
        pango_layout_set_text(lay, ws->launcher.entries[i].name, -1);
        int etw, eth;
        pango_layout_get_pixel_size(lay, &etw, &eth);
        cairo_set_source_rgb(cr, 0.9, 0.92, 1.0);
        cairo_move_to(cr, ex + (ew - etw) / 2, ey + (eh - eth) / 2);
        pango_cairo_show_layout(cr, lay);
        g_object_unref(lay);
    }
    pango_font_description_free(fd_entry);

    cairo_surface_flush(ws->launcher.cairo_surface);
    wl_surface_attach(ws->launcher.surface, ws->launcher.buffer, 0, 0);
    wl_surface_damage_buffer(ws->launcher.surface, 0, 0, w, h);
    wl_surface_commit(ws->launcher.surface);
}

static void launcher_destroy(struct wl_status *ws)
{
    if (!ws->launcher.visible) return;
    ws->launcher.visible = false;
    ws->launcher.search[0] = '\0';
    ws->launcher.scroll_offset = 0;
    ws->launcher.hovered_idx = -1;
    launcher_destroy_buffer(ws);
    if (ws->launcher.layer_surface) {
        zwlr_layer_surface_v1_destroy(ws->launcher.layer_surface);
        ws->launcher.layer_surface = NULL;
    }
    if (ws->launcher.surface) {
        wl_surface_destroy(ws->launcher.surface);
        ws->launcher.surface = NULL;
    }
    ws->launcher.configured = false;
}

static void launcher_layer_surface_configure(void *data,
    struct zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t width, uint32_t height)
{
    struct wl_status *ws = data;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    if (width > 0) ws->launcher.width = width;
    if (height > 0) ws->launcher.height = height;
    if (!ws->launcher.buffer) {
        launcher_populate_entries(ws);
        int cols = 4;
        int ew = 130, eh = 34;
        ws->launcher.cols = cols;
        ws->launcher.entry_w = ew;
        ws->launcher.entry_h = eh;
        ws->launcher.grid_x = 14;
        ws->launcher.grid_y = 70;
        int max_rows = (ws->launcher.n_filtered + cols - 1) / cols;
        if (max_rows > 7) max_rows = 7;
        ws->launcher.width = 14 + cols * (ew + 8) + 14;
        ws->launcher.height = 70 + max_rows * (eh + 6) + 14;
        if (ws->launcher.height > 600) ws->launcher.height = 600;
        launcher_create_buffer(ws);
        launcher_render(ws);
    }
    ws->launcher.configured = true;
}

static void launcher_layer_surface_closed(void *data,
    struct zwlr_layer_surface_v1 *surface)
{
    ((struct wl_status *)data)->launcher.visible = false;
}

static const struct zwlr_layer_surface_v1_listener launcher_layer_surface_listener = {
    .configure = launcher_layer_surface_configure,
    .closed = launcher_layer_surface_closed,
};

static void launcher_show(struct wl_status *ws)
{
    if (ws->popup.visible) popup_destroy(ws);
    if (ws->launcher.visible) launcher_destroy(ws);

    memset(&ws->launcher, 0, sizeof(ws->launcher));
    ws->launcher.hovered_idx = -1;
    ws->launcher.width = 400;
    ws->launcher.height = 400;

    ws->launcher.surface = wl_compositor_create_surface(ws->compositor);
    ws->launcher.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ws->layer_shell, ws->launcher.surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlstatus-launcher");

    zwlr_layer_surface_v1_add_listener(ws->launcher.layer_surface,
        &launcher_layer_surface_listener, ws);

    zwlr_layer_surface_v1_set_size(ws->launcher.layer_surface, ws->launcher.width, ws->launcher.height);
    zwlr_layer_surface_v1_set_anchor(ws->launcher.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);

    const char *anchor_str = config_get(ws->cfg, "bar_anchor", "top");
    int bar_on_bottom = (strcmp(anchor_str, "bottom") == 0);
    if (bar_on_bottom) {
        zwlr_layer_surface_v1_set_anchor(ws->launcher.layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
        zwlr_layer_surface_v1_set_margin(ws->launcher.layer_surface,
            0, 0, ws->height + 4, BAR_PADDING);
    } else {
        zwlr_layer_surface_v1_set_margin(ws->launcher.layer_surface,
            ws->height + 4, 0, 0, BAR_PADDING);
    }
    zwlr_layer_surface_v1_set_exclusive_zone(ws->launcher.layer_surface, 0);
    ws->launcher.visible = true;
    wl_surface_commit(ws->launcher.surface);
}

static void tooltip_destroy(struct wl_status *ws)
{
    if (!ws->tooltip.visible) return;
    ws->tooltip.visible = false;
    ws->tooltip.hovered_clickable = -1;
    if (ws->tooltip.buffer) {
        cairo_destroy(ws->tooltip.cr);
        ws->tooltip.cr = NULL;
        cairo_surface_destroy(ws->tooltip.cairo_surface);
        ws->tooltip.cairo_surface = NULL;
        if (ws->tooltip.shm_data) {
            int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->tooltip.width);
            munmap(ws->tooltip.shm_data, stride * ws->tooltip.height);
        }
        ws->tooltip.shm_data = NULL;
        wl_buffer_destroy(ws->tooltip.buffer);
        ws->tooltip.buffer = NULL;
    }
    if (ws->tooltip.layer_surface) {
        zwlr_layer_surface_v1_destroy(ws->tooltip.layer_surface);
        ws->tooltip.layer_surface = NULL;
    }
    if (ws->tooltip.surface) {
        wl_surface_destroy(ws->tooltip.surface);
        ws->tooltip.surface = NULL;
    }
    ws->tooltip.configured = false;
}

static void tooltip_layer_surface_configure(void *data,
    struct zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t width, uint32_t height)
{
    struct wl_status *ws = data;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    if (width > 0) ws->tooltip.width = width;
    if (height > 0) ws->tooltip.height = height;
    if (!ws->tooltip.buffer) {
        int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, ws->tooltip.width);
        int size = stride * ws->tooltip.height;
        int shm_fd = create_shm_fd(size);
        if (shm_fd < 0) return;
        struct wl_shm_pool *pool = wl_shm_create_pool(ws->shm, shm_fd, size);
        ws->tooltip.buffer = wl_shm_pool_create_buffer(pool, 0,
            ws->tooltip.width, ws->tooltip.height, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        ws->tooltip.shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        close(shm_fd);
        if (ws->tooltip.shm_data == MAP_FAILED) { ws->tooltip.shm_data = NULL; return; }
        ws->tooltip.cairo_surface = cairo_image_surface_create_for_data(
            ws->tooltip.shm_data, CAIRO_FORMAT_ARGB32, ws->tooltip.width, ws->tooltip.height, stride);
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
    struct zwlr_layer_surface_v1 *surface)
{
    ((struct wl_status *)data)->tooltip.visible = false;
}

static const struct zwlr_layer_surface_v1_listener tooltip_layer_surface_listener = {
    .configure = tooltip_layer_surface_configure,
    .closed = tooltip_layer_surface_closed,
};

static void tooltip_show(struct wl_status *ws, const char *text, int hover_x, int hover_y)
{
    if (ws->tooltip.visible && strcmp(ws->tooltip.text, text) == 0)
        return;

    tooltip_destroy(ws);

    memset(&ws->tooltip, 0, sizeof(ws->tooltip));
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
        ws->layer_shell, ws->tooltip.surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlstatus-tooltip");

    zwlr_layer_surface_v1_add_listener(ws->tooltip.layer_surface,
        &tooltip_layer_surface_listener, ws);

    zwlr_layer_surface_v1_set_size(ws->tooltip.layer_surface, ws->tooltip.width, ws->tooltip.height);
    zwlr_layer_surface_v1_set_anchor(ws->tooltip.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);

    const char *anchor_str = config_get(ws->cfg, "bar_anchor", "top");
    int bar_on_bottom = (strcmp(anchor_str, "bottom") == 0);
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

static void handle_sighup(int sig)
{
    (void)sig;
    reload_requested = 1;
}

static const char *config_path(void);

static void reload(struct wl_status *ws)
{
    if (ws->popup.visible) popup_destroy(ws);
    if (ws->launcher.visible) launcher_destroy(ws);
    if (ws->tooltip.visible) tooltip_destroy(ws);
    destroy_buffer(ws);
    if (ws->bar) bar_destroy(ws->bar);
    config_destroy(ws->cfg);
    ws->cfg = config_load(config_path());
    ws->bar = bar_create(ws->width, ws->height, ws->cfg);
    create_buffer(ws);
    bar_update_workspaces(ws->bar);
    bar_update_system_info(ws->bar);
    bar_update_updates(ws->bar);
    bar_update_disk(ws->bar);
    bar_update_volume(ws->bar);
    bar_update_network(ws->bar);
    bar_update_battery(ws->bar);
    bar_update_custom_modules(ws->bar);
    render(ws);
}

static void on_timer(struct wl_status *ws)
{
    if (!ws->bar || !ws->running) return;
    bar_update_workspaces(ws->bar);
    bar_update_system_info(ws->bar);
    bar_update_updates(ws->bar);
    bar_update_disk(ws->bar);
    bar_update_volume(ws->bar);
    bar_update_network(ws->bar);
    bar_update_battery(ws->bar);
    bar_update_custom_modules(ws->bar);
    render(ws);
}

static void layer_surface_configure(void *data,
    struct zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t width, uint32_t height)
{
    struct wl_status *ws = data;
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
    bar_update_system_info(ws->bar);
    bar_update_updates(ws->bar);
    bar_update_disk(ws->bar);
    bar_update_volume(ws->bar);
    bar_update_network(ws->bar);
    bar_update_battery(ws->bar);
    bar_update_custom_modules(ws->bar);
    render(ws);
}

static void layer_surface_closed(void *data,
    struct zwlr_layer_surface_v1 *surface)
{
    ((struct wl_status *)data)->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void pointer_enter(void *data, struct wl_pointer *pointer,
    uint32_t serial, struct wl_surface *surface,
    wl_fixed_t sx, wl_fixed_t sy)
{
    struct wl_status *ws = data;
    ws->current_pointer_surface = surface;
    ws->pointer_x = wl_fixed_to_int(sx);
    ws->pointer_y = wl_fixed_to_int(sy);

    if (ws->popup.visible && surface == ws->popup.surface)
        return;
    if (ws->launcher.visible && surface == ws->launcher.surface)
        return;

    bar_update_hover(ws->bar, ws->pointer_x, ws->pointer_y);
    render(ws);

    for (int i = 0; i < ws->bar->n_clickables; i++) {
        struct clickable *c = &ws->bar->clickables[i];
        if (c->tooltip_cmd[0] &&
            ws->pointer_x >= c->x && ws->pointer_x < c->x + c->w &&
            ws->pointer_y >= c->y && ws->pointer_y < c->y + c->h) {
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
            break;
        }
    }
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
    uint32_t serial, struct wl_surface *surface)
{
    struct wl_status *ws = data;
    ws->current_pointer_surface = NULL;

    if (ws->popup.visible && surface == ws->popup.surface) {
        if (ws->popup.hovered_btn != -1) {
            ws->popup.hovered_btn = -1;
            popup_render(ws);
        }
        return;
    }
    if (ws->launcher.visible && surface == ws->launcher.surface) {
        if (ws->launcher.hovered_idx != -1) {
            ws->launcher.hovered_idx = -1;
            launcher_render(ws);
        }
        return;
    }

    bar_clear_hover(ws->bar);
    render(ws);
    if (ws->tooltip.visible)
        tooltip_destroy(ws);
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
    uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    struct wl_status *ws = data;
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
    if (ws->launcher.visible && ws->current_pointer_surface == ws->launcher.surface) {
        int old = ws->launcher.hovered_idx;
        ws->launcher.hovered_idx = -1;
        int cols = ws->launcher.cols;
        int ew = ws->launcher.entry_w;
        int eh = ws->launcher.entry_h;
        int gx = ws->launcher.grid_x;
        int gy = ws->launcher.grid_y;
        int skip = ws->launcher.scroll_offset * cols;
        for (int j = 0; j < ws->launcher.n_filtered - skip; j++) {
            int i = ws->launcher.filtered[skip + j];
            int row = j / cols;
            int col = j % cols;
            int ex = gx + col * (ew + 8);
            int ey = gy + row * (eh + 6);
            if (x >= ex && x < ex + ew && y >= ey && y < ey + eh) {
                ws->launcher.hovered_idx = i;
                break;
            }
        }
        if (old != ws->launcher.hovered_idx)
            launcher_render(ws);
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
        struct clickable *c = &ws->bar->clickables[i];
        if (c->tooltip_cmd[0] &&
            x >= c->x && x < c->x + c->w &&
            y >= c->y && y < c->y + c->h) {
            found_tooltip = i;
            break;
        }
    }
    if (found_tooltip >= 0 && found_tooltip != ws->tooltip.hovered_clickable) {
        FILE *fp = popen(ws->bar->clickables[found_tooltip].tooltip_cmd, "r");
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
    } else if (found_tooltip < 0) {
        if (ws->tooltip.visible)
            tooltip_destroy(ws);
    }
}

static void execute_command(const char *cmd)
{
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        _exit(1);
    }
}

static void pointer_button(void *data, struct wl_pointer *pointer,
    uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    struct wl_status *ws = data;
    if (state != 1) return;
    if (button != 0x110) return;

    if (ws->popup.visible) {
        if (ws->current_pointer_surface == ws->popup.surface) {
            int x = ws->pointer_x, y = ws->pointer_y;
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
            return;
        }
    }

    if (ws->launcher.visible) {
        if (ws->current_pointer_surface == ws->launcher.surface) {
            int x = ws->pointer_x, y = ws->pointer_y;
            int cols = ws->launcher.cols;
            int ew = ws->launcher.entry_w;
            int eh = ws->launcher.entry_h;
            int gx = ws->launcher.grid_x;
            int gy = ws->launcher.grid_y;
            int skip = ws->launcher.scroll_offset * cols;
            for (int j = 0; j < ws->launcher.n_filtered - skip; j++) {
                int i = ws->launcher.filtered[skip + j];
                int row = j / cols;
                int col = j % cols;
                int ex = gx + col * (ew + 8);
                int ey = gy + row * (eh + 6);
                if (x >= ex && x < ex + ew && y >= ey && y < ey + eh) {
                    execute_command(ws->launcher.entries[i].exec);
                    launcher_destroy(ws);
                    return;
                }
            }
        } else {
            launcher_destroy(ws);
            return;
        }
    }

    enum click_action action = bar_handle_click(ws->bar,
        ws->pointer_x, ws->pointer_y);

    if (action == CLICK_POWEROFF || action == CLICK_REBOOT || action == CLICK_SUSPEND) {
        int a = (action == CLICK_POWEROFF) ? 0 : (action == CLICK_REBOOT) ? 1 : 2;
        popup_create(ws, a);
        return;
    }

    if (action == CLICK_LAUNCHER) {
        launcher_show(ws);
        return;
    }

    if (action == CLICK_HYPRCTL || action == CLICK_RUN) {
        for (int i = 0; i < ws->bar->n_clickables; i++) {
            struct clickable *c = &ws->bar->clickables[i];
            if (c->action == action &&
                ws->pointer_x >= c->x && ws->pointer_x < c->x + c->w &&
                ws->pointer_y >= c->y && ws->pointer_y < c->y + c->h) {
                execute_command(c->command);
                break;
            }
        }
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
    uint32_t time, uint32_t axis, wl_fixed_t value)
{
    struct wl_status *ws = data;
    if (!ws->launcher.visible || axis != 0) return;
    int delta = wl_fixed_to_int(value);
    if (delta == 0) return;
    int cols = ws->launcher.cols;
    int total_rows = (ws->launcher.n_filtered + cols - 1) / cols;
    int eh = ws->launcher.entry_h;
    int grid_h = ws->launcher.height - ws->launcher.grid_y - 14;
    int visible_rows = grid_h / (eh + 6);
    if (visible_rows < 1) visible_rows = 1;
    int max_scroll = total_rows - visible_rows;
    if (max_scroll < 0) max_scroll = 0;
    int old = ws->launcher.scroll_offset;
    ws->launcher.scroll_offset -= delta;
    if (ws->launcher.scroll_offset < 0) ws->launcher.scroll_offset = 0;
    if (ws->launcher.scroll_offset > max_scroll) ws->launcher.scroll_offset = max_scroll;
    if (old != ws->launcher.scroll_offset) {
        ws->launcher.hovered_idx = -1;
        launcher_render(ws);
    }
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {}

static void keyboard_keymap(void *data, struct wl_keyboard *kb,
    uint32_t format, int fd, uint32_t size)
{
    struct wl_status *ws = data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) { close(fd); return; }
    ws->xkb_keymap = xkb_keymap_new_from_string(ws->xkb_ctx, map,
        XKB_KEYMAP_FORMAT_TEXT_V1, 0);
    munmap(map, size);
    close(fd);
    if (!ws->xkb_keymap) return;
    ws->xkb_state = xkb_state_new(ws->xkb_keymap);
}

static void keyboard_enter(void *data, struct wl_keyboard *kb,
    uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {}

static void keyboard_leave(void *data, struct wl_keyboard *kb,
    uint32_t serial, struct wl_surface *surface) {}

static void keyboard_key(void *data, struct wl_keyboard *kb,
    uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    struct wl_status *ws = data;
    if (state != 1 || !ws->launcher.visible) return;
    if (!ws->xkb_state) return;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(ws->xkb_state, key);

    if (sym == XKB_KEY_Escape) {
        launcher_destroy(ws);
        return;
    }
    if (sym == XKB_KEY_Return) {
        int skip = ws->launcher.scroll_offset * ws->launcher.cols;
        for (int j = 0; j < ws->launcher.n_filtered - skip; j++) {
            int i = ws->launcher.filtered[skip + j];
            if (i == ws->launcher.hovered_idx) {
                execute_command(ws->launcher.entries[i].exec);
                launcher_destroy(ws);
                return;
            }
        }
        if (ws->launcher.n_filtered > 0) {
            execute_command(ws->launcher.entries[ws->launcher.filtered[0]].exec);
            launcher_destroy(ws);
        }
        return;
    }
    if (sym == XKB_KEY_BackSpace) {
        int len = strlen(ws->launcher.search);
        if (len > 0) ws->launcher.search[len - 1] = '\0';
        launcher_update_filter(ws);
        launcher_render(ws);
        return;
    }
    if (sym == XKB_KEY_Up || sym == XKB_KEY_KP_Up) {
        int skip = ws->launcher.scroll_offset * ws->launcher.cols;
        int n_visible = ws->launcher.n_filtered - skip;
        if (n_visible > 0) {
            int cur = -1;
            for (int j = 0; j < n_visible; j++) {
                if (ws->launcher.filtered[skip + j] == ws->launcher.hovered_idx) {
                    cur = j;
                    break;
                }
            }
            int cols = ws->launcher.cols;
            int new_cur;
            if (cur < 0) {
                new_cur = 0;
            } else if (cur < cols) {
                if (ws->launcher.scroll_offset > 0) {
                    ws->launcher.scroll_offset--;
                    new_cur = cur + cols;
                } else {
                    new_cur = cur;
                }
            } else {
                new_cur = cur - cols;
            }
            if (new_cur < n_visible) {
                ws->launcher.hovered_idx = ws->launcher.filtered[skip + new_cur];
                launcher_render(ws);
            }
        }
        return;
    }
    if (sym == XKB_KEY_Down || sym == XKB_KEY_KP_Down) {
        int skip = ws->launcher.scroll_offset * ws->launcher.cols;
        int n_visible = ws->launcher.n_filtered - skip;
        if (n_visible > 0) {
            int cur = -1;
            for (int j = 0; j < n_visible; j++) {
                if (ws->launcher.filtered[skip + j] == ws->launcher.hovered_idx) {
                    cur = j;
                    break;
                }
            }
            int cols = ws->launcher.cols;
            int total_rows = (ws->launcher.n_filtered + cols - 1) / cols;
            int eh = ws->launcher.entry_h;
            int grid_h = ws->launcher.height - ws->launcher.grid_y - 14;
            int visible_rows = grid_h / (eh + 6);
            if (visible_rows < 1) visible_rows = 1;
            int max_scroll = total_rows - visible_rows;
            int new_cur;
            if (cur < 0) {
                new_cur = 0;
            } else if (cur + cols >= n_visible) {
                if (ws->launcher.scroll_offset < max_scroll) {
                    ws->launcher.scroll_offset++;
                    new_cur = cur - cols;
                } else {
                    new_cur = cur;
                }
            } else {
                new_cur = cur + cols;
            }
            if (new_cur >= 0 && new_cur < n_visible) {
                ws->launcher.hovered_idx = ws->launcher.filtered[skip + new_cur];
                launcher_render(ws);
            }
        }
        return;
    }
    if (sym == XKB_KEY_Right || sym == XKB_KEY_KP_Right) {
        int skip = ws->launcher.scroll_offset * ws->launcher.cols;
        int n_visible = ws->launcher.n_filtered - skip;
        int cur = -1;
        for (int j = 0; j < n_visible; j++) {
            if (ws->launcher.filtered[skip + j] == ws->launcher.hovered_idx) {
                cur = j;
                break;
            }
        }
        if (cur < 0) cur = -1;
        if (cur + 1 < n_visible) {
            ws->launcher.hovered_idx = ws->launcher.filtered[skip + cur + 1];
            launcher_render(ws);
        }
        return;
    }
    if (sym == XKB_KEY_Left || sym == XKB_KEY_KP_Left) {
        int skip = ws->launcher.scroll_offset * ws->launcher.cols;
        int n_visible = ws->launcher.n_filtered - skip;
        int cur = -1;
        for (int j = 0; j < n_visible; j++) {
            if (ws->launcher.filtered[skip + j] == ws->launcher.hovered_idx) {
                cur = j;
                break;
            }
        }
        if (cur > 0) {
            ws->launcher.hovered_idx = ws->launcher.filtered[skip + cur - 1];
            launcher_render(ws);
        }
        return;
    }

    char buf[8];
    int n = xkb_keysym_to_utf8(sym, buf, sizeof(buf));
    if (n > 0 && isprint((unsigned char)buf[0])) {
        buf[n] = '\0';
        int len = strlen(ws->launcher.search);
        if (len + n < (int)sizeof(ws->launcher.search) - 1) {
            memcpy(ws->launcher.search + len, buf, n);
            ws->launcher.search[len + n] = '\0';
        }
        launcher_update_filter(ws);
        launcher_render(ws);
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *kb,
    uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
    uint32_t mods_locked, uint32_t group)
{
    struct wl_status *ws = data;
    if (ws->xkb_state)
        xkb_state_update_mask(ws->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *kb,
    int32_t rate, int32_t delay) {}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
    uint32_t capabilities)
{
    struct wl_status *ws = data;
    if ((capabilities & 1) && !ws->pointer) {
        ws->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(ws->pointer, &pointer_listener, ws);
    } else if (!(capabilities & 1) && ws->pointer) {
        wl_pointer_destroy(ws->pointer);
        ws->pointer = NULL;
    }
    if ((capabilities & 2) && !ws->keyboard) {
        ws->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(ws->keyboard, &keyboard_listener, ws);
    } else if (!(capabilities & 2) && ws->keyboard) {
        wl_keyboard_destroy(ws->keyboard);
        ws->keyboard = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void registry_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
    struct wl_status *ws = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0)
        ws->compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, 4);
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        ws->shm = wl_registry_bind(registry, name,
            &wl_shm_interface, 1);
    else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
        ws->layer_shell = wl_registry_bind(registry, name,
            &zwlr_layer_shell_v1_interface, 4);
    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        ws->seat = wl_registry_bind(registry, name,
            &wl_seat_interface, 7);
        wl_seat_add_listener(ws->seat, &seat_listener, ws);
    }
}

static void registry_global_remove(void *data,
    struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int setup_layer_surface(struct wl_status *ws)
{
    ws->surface = wl_compositor_create_surface(ws->compositor);
    if (!ws->surface) return -1;

    ws->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ws->layer_shell, ws->surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP, "wlstatus");
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
    zwlr_layer_surface_v1_set_exclusive_zone(ws->layer_surface, bh);

    wl_surface_commit(ws->surface);
    wl_display_roundtrip(ws->display);

    if (!ws->configured) {
        fprintf(stderr, "surface was not configured\n");
        return -1;
    }
    return 0;
}

static const char *config_path(void)
{
    const char *home = getenv("HOME");
    if (!home) return NULL;
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s/.config/wlstatus/config", home);
    return buf;
}

int main(int argc, char *argv[])
{
    struct wl_status ws = {0};
    ws.cfg = config_load(config_path());
    int bh = config_get_int(ws.cfg, "bar_height", BAR_HEIGHT);
    ws.width = 1920;
    ws.height = bh;
    ws.running = true;

    ws.display = wl_display_connect(NULL);
    if (!ws.display) {
        fprintf(stderr, "failed to connect to wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(ws.display);
    wl_registry_add_listener(registry, &registry_listener, &ws);
    wl_display_roundtrip(ws.display);

    if (!ws.compositor || !ws.shm || !ws.layer_shell) {
        fprintf(stderr, "missing required wayland globals\n");
        return 1;
    }

    ws.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ws.xkb_ctx)
        fprintf(stderr, "warning: failed to create xkb context (keyboard input disabled)\n");

    if (setup_layer_surface(&ws) < 0)
        return 1;

    ws.timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (ws.timer_fd >= 0) {
        struct itimerspec ts = {
            .it_value = { .tv_sec = 1 },
            .it_interval = { .tv_sec = 1 },
        };
        timerfd_settime(ws.timer_fd, 0, &ts, NULL);
    }

    struct sigaction sa = { .sa_handler = handle_sighup };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, NULL);

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
        int has_display_data = 0;

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
            has_display_data = 1;
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
                const struct inotify_event *ev;
                for (char *p = buf; p < buf + len; p += sizeof(struct inotify_event) + ev->len) {
                    ev = (const struct inotify_event *)p;
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
    if (ws.launcher.visible) launcher_destroy(&ws);
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
