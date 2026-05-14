#include "bar.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <pango/pangocairo.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 (M_PI / 2.0)
#endif

struct bar *bar_create(int width, int height, struct config *cfg)
{
    struct bar *bar = calloc(1, sizeof(*bar));
    if (!bar) return NULL;
    bar->width = width;
    bar->height = height;
    bar->cfg = cfg;
    bar->n_clickables = 0;
    bar->n_icons = 0;
    bar->power_hovered = -1;
    bar->hovered_workspace = -1;
    bar->n_workspaces = 0;
    bar->cpu_prev_total = 0;
    bar->cpu_prev_idle = 0;
    bar->cpu_percent = 0;
    bar->mem_percent = 0;
    bar->updates_count = 0;
    bar->updates_last_check = 0;
    bar->disk_percent = 0;
    bar->disk_last_check = 0;

    return bar;
}

void bar_destroy(struct bar *bar)
{
    if (!bar) return;
    for (int i = 0; i < bar->n_icons; i++)
        if (bar->icons[i])
            cairo_surface_destroy(bar->icons[i]);
    free(bar);
}

void bar_load_png_icon(struct bar *bar, const char *path, int index)
{
    if (index < 0 || index >= 8) return;
    cairo_surface_t *img = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS)
        bar->icons[index] = img;
    else
        cairo_surface_destroy(img);
}

void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    cairo_move_to(cr, x + r, y);
    cairo_line_to(cr, x + w - r, y);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
    cairo_line_to(cr, x + w, y + h - r);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
    cairo_line_to(cr, x + r, y + h);
    cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
    cairo_line_to(cr, x, y + r);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
}

static void draw_power_btn_icon(cairo_t *cr, int x, int y, int size, int type, bool hovered, struct config *cfg)
{
    double cx = x + size / 2.0;
    double cy = y + size / 2.0;
    double r = size / 2.0 - 2;

    float col[4];
    const char *ckey = (type == 0) ? "poweroff_color" : (type == 1) ? "reboot_color" : "suspend_color";
    float def_col[4];
    if (type == 0)      { def_col[0]=1.0f; def_col[1]=0.2f; def_col[2]=0.3f; def_col[3]=1.0f; }
    else if (type == 1) { def_col[0]=1.0f; def_col[1]=0.6f; def_col[2]=0.0f; def_col[3]=1.0f; }
    else                { def_col[0]=0.6f; def_col[1]=0.2f; def_col[2]=1.0f; def_col[3]=1.0f; }
    config_get_color(cfg, ckey, col, def_col);

    int glow_w = config_get_int(cfg, "glow_width", 8);
    float glow_a = (float)config_get_int(cfg, "glow_alpha_percent", 25) / 100.0f;
    float ico_a = (float)config_get_int(cfg, "icon_alpha_percent", 90) / 100.0f;
    float ico_ha = (float)config_get_int(cfg, "icon_hover_alpha_percent", 100) / 100.0f;

    if (hovered) {
        cairo_set_source_rgba(cr, col[0], col[1], col[2], glow_a);
        cairo_set_line_width(cr, glow_w);
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        cairo_stroke(cr);
    }

    cairo_set_line_width(cr, 2.5);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_source_rgba(cr, col[0], col[1], col[2], hovered ? ico_ha : ico_a);

    if (type == 0) {
        cairo_arc(cr, cx, cy, r, -M_PI * 0.75, M_PI * 0.75);
        cairo_stroke(cr);
        cairo_move_to(cr, cx, cy - r - 1);
        cairo_line_to(cr, cx, cy + 2);
        cairo_stroke(cr);
        cairo_arc(cr, cx, cy, 2.5, 0, 2 * M_PI);
        cairo_fill(cr);
    } else if (type == 1) {
        cairo_arc(cr, cx, cy, r, 0, 11 * M_PI / 6);
        cairo_stroke(cr);
        double ax = cx + r * cos(11 * M_PI / 6);
        double ay = cy + r * sin(11 * M_PI / 6);
        cairo_move_to(cr, ax, ay);
        cairo_line_to(cr, ax - 3, ay - 3);
        cairo_move_to(cr, ax, ay);
        cairo_line_to(cr, ax - 3, ay + 3);
        cairo_stroke(cr);
    } else if (type == 2) {
        double bw = 2.5;
        double bh = r * 1.3;
        double gap = 3;
        cairo_rectangle(cr, cx - gap / 2 - bw, cy - bh / 2, bw, bh);
        cairo_rectangle(cr, cx + gap / 2, cy - bh / 2, bw, bh);
        cairo_fill(cr);
    }
}

static void draw_power_buttons(struct bar *bar, cairo_t *cr, int h)
{
    struct config *cfg = bar->cfg;
    int pad = config_get_int(cfg, "bar_padding", BAR_PADDING);
    int btn_size = config_get_int(cfg, "icon_size", 24);
    int btn_y = (h - btn_size) / 2;
    int gap = 6;
    int x = bar->width - pad;

    int group_w = btn_size * 3 + gap * 2;
    int bg_pad = 3;
    int bg_x = bar->width - pad - group_w - bg_pad;
    int bg_y = btn_y - bg_pad;
    int bg_w = group_w + bg_pad * 2;
    int bg_h = btn_size + bg_pad * 2;
    double bg_r = bg_h / 2.0;
    cairo_set_source_rgba(cr, 0.15, 0.15, 0.25, 0.4);
    draw_rounded_rect(cr, bg_x, bg_y, bg_w, bg_h, bg_r);
    cairo_fill(cr);

    for (int i = 2; i >= 0; i--) {
        x -= btn_size;
        bool hovered = (bar->power_hovered == i);

        draw_power_btn_icon(cr, x, btn_y, btn_size, i, hovered, bar->cfg);

        if (bar->n_clickables < 16) {
            enum click_action actions[] = {CLICK_POWEROFF, CLICK_REBOOT, CLICK_SUSPEND};
            bar->clickables[bar->n_clickables++] = (struct clickable){
                .x = x, .y = btn_y, .w = btn_size, .h = btn_size,
                .action = actions[i],
            };
        }

        x -= gap;
    }
}

static void draw_background(cairo_t *cr, int w, int h)
{
    cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 0, h);
    cairo_pattern_add_color_stop_rgba(pat, 0,   0.02, 0.02, 0.06, 0.99);
    cairo_pattern_add_color_stop_rgba(pat, 0.5, 0.04, 0.04, 0.10, 0.99);
    cairo_pattern_add_color_stop_rgba(pat, 1,   0.03, 0.03, 0.08, 0.99);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, 0.15);
    cairo_set_line_width(cr, 6);
    cairo_move_to(cr, 0, h - 0.5);
    cairo_line_to(cr, w, h - 0.5);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, 0.5);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, h - 0.5);
    cairo_line_to(cr, w, h - 0.5);
    cairo_stroke(cr);
}

static void draw_workspaces(struct bar *bar, cairo_t *cr, int h, int x)
{
    int y = BAR_PADDING;
    int btn_h = h - BAR_PADDING * 2;
    double btn_r = btn_h / 2.0;

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string(
        "Sans Bold 12");
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);

    for (int i = 0; i < bar->n_workspaces; i++) {
        int id = bar->workspaces[i].id;
        char num[8];
        snprintf(num, sizeof(num), "%d", id);

        pango_layout_set_text(layout, num, -1);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);

        int pad_h = 10;
        int btn_w = tw + pad_h * 2;
        int text_x = x + (btn_w - tw) / 2.0;
        int text_y = y + (btn_h - th) / 2.0;

        bool hovered = (bar->hovered_workspace == i);
        bool active = bar->workspaces[i].active;

        if (active) {
            if (hovered) {
                cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, 0.10);
                cairo_set_line_width(cr, 6);
                draw_rounded_rect(cr, x, y, btn_w, btn_h, btn_r);
                cairo_stroke(cr);
            }
            cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, hovered ? 0.95 : 0.80);
            draw_rounded_rect(cr, x, y, btn_w, btn_h, btn_r);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0.02, 0.02, 0.06);
        } else {
            if (hovered) {
                cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, 0.10);
                cairo_set_line_width(cr, 6);
                draw_rounded_rect(cr, x, y, btn_w, btn_h, btn_r);
                cairo_stroke(cr);
            }
            cairo_set_source_rgba(cr, 0.0, 0.55, 0.80, hovered ? 0.7 : 0.35);
            cairo_set_line_width(cr, 1.2);
            draw_rounded_rect(cr, x, y, btn_w, btn_h, btn_r);
            cairo_stroke(cr);
        }

        if (!active)
            cairo_set_source_rgba(cr, 0.0, 0.55, 0.80, hovered ? 0.9 : 0.5);
        cairo_move_to(cr, text_x, text_y + 1);
        pango_cairo_show_layout(cr, layout);

        if (bar->n_clickables < 16) {
            struct clickable *cl = &bar->clickables[bar->n_clickables++];
            cl->x = x;
            cl->y = y;
            cl->w = btn_w;
            cl->h = btn_h;
            cl->action = CLICK_HYPRCTL;
            snprintf(cl->command, sizeof(cl->command),
                "hyprctl dispatch workspace %d", id);
        }

        x += btn_w + 6;
    }

    g_object_unref(layout);
}

void bar_render(struct bar *bar, cairo_t *cr)
{
    bar->n_clickables = 0;

    draw_background(cr, bar->width, bar->height);

    int ws_start = 48;
    draw_workspaces(bar, cr, bar->height, ws_start);

    draw_power_buttons(bar, cr, bar->height);

    int pw_btn_size = 24;
    int pw_total_w = pw_btn_size * 3 + 6 * 2;
    int pw_start = bar->width - BAR_PADDING - pw_total_w;

    int cx = ws_start + 10;
    int cx_end = pw_start - 10;
    int cw = 0;

    {
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        char buf[64];
        strftime(buf, sizeof(buf), "%a %b %d  %H:%M", tm);
        PangoLayout *lay = pango_cairo_create_layout(cr);
        PangoFontDescription *fd = pango_font_description_from_string("Sans 11");
        pango_layout_set_font_description(lay, fd);
        pango_font_description_free(fd);
        pango_layout_set_text(lay, buf, -1);
        int tw, th;
        pango_layout_get_pixel_size(lay, &tw, &th);

        char cpu_buf[32], mem_buf[32], upd_buf[32], dsk_buf[32];
        snprintf(cpu_buf, sizeof(cpu_buf), "CPU %d%%", bar->cpu_percent);
        snprintf(mem_buf, sizeof(mem_buf), "MEM %d%%", bar->mem_percent);
        snprintf(upd_buf, sizeof(upd_buf), "UPD %d", bar->updates_count);
        snprintf(dsk_buf, sizeof(dsk_buf), "DSK %d%%", bar->disk_percent);

        PangoFontDescription *fd_s = pango_font_description_from_string("Sans Bold 9");
        PangoLayout *lay_cpu = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(lay_cpu, fd_s);
        pango_layout_set_text(lay_cpu, cpu_buf, -1);
        int cpu_w, cpu_h;
        pango_layout_get_pixel_size(lay_cpu, &cpu_w, &cpu_h);

        PangoLayout *lay_mem = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(lay_mem, fd_s);
        pango_layout_set_text(lay_mem, mem_buf, -1);
        int mem_w, mem_h;
        pango_layout_get_pixel_size(lay_mem, &mem_w, &mem_h);

        PangoLayout *lay_upd = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(lay_upd, fd_s);
        pango_layout_set_text(lay_upd, upd_buf, -1);
        int upd_w, upd_h;
        pango_layout_get_pixel_size(lay_upd, &upd_w, &upd_h);

        PangoLayout *lay_dsk = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(lay_dsk, fd_s);
        pango_layout_set_text(lay_dsk, dsk_buf, -1);
        int dsk_w, dsk_h;
        pango_layout_get_pixel_size(lay_dsk, &dsk_w, &dsk_h);
        pango_font_description_free(fd_s);

        int pill_pad_h = 4;
        int pill_pad_w = 8;
        int pill_gap = 6;
        int pill_h = cpu_h + pill_pad_h * 2;
        int total_w = tw + 14
            + (cpu_w + pill_pad_w * 2) + pill_gap
            + (mem_w + pill_pad_w * 2) + pill_gap
            + (upd_w + pill_pad_w * 2) + pill_gap
            + (dsk_w + pill_pad_w * 2);

        cw = total_w;
        int center = cx + (cx_end - cx - cw) / 2;
        if (center < cx) center = cx;

        cairo_set_source_rgb(cr, 0.0, 0.90, 1.0);
        cairo_move_to(cr, center, (bar->height - th) / 2.0 + 1);
        pango_cairo_show_layout(cr, lay);

        int pill_x = center + tw + 14;
        int pill_y = (bar->height - pill_h) / 2;
        double pill_r = pill_h / 2.0;
        int pill_cy = pill_y + pill_h / 2;

        int p_widths[] = {cpu_w, mem_w, upd_w, dsk_w};
        int p_heights[] = {cpu_h, mem_h, upd_h, dsk_h};
        PangoLayout *p_layouts[] = {lay_cpu, lay_mem, lay_upd, lay_dsk};
        float p_border_r[][3] = {
            {0.0, 0.90, 1.0},
            {0.0, 0.90, 1.0},
            {0.80, 0.20, 1.0},
            {0.20, 0.85, 0.40},
        };
        float p_fill_r[][4] = {
            {0.0, 0.25, 0.40, 0.35},
            {0.0, 0.25, 0.40, 0.35},
            {0.30, 0.05, 0.40, 0.30},
            {0.05, 0.30, 0.10, 0.30},
        };

        for (int pi = 0; pi < 4; pi++) {
            int pw = p_widths[pi];
            int ph = p_heights[pi];

            cairo_set_source_rgba(cr, p_fill_r[pi][0], p_fill_r[pi][1], p_fill_r[pi][2], p_fill_r[pi][3]);
            draw_rounded_rect(cr, pill_x, pill_y, pw + pill_pad_w * 2, pill_h, pill_r);
            cairo_fill(cr);

            cairo_set_source_rgba(cr, p_border_r[pi][0], p_border_r[pi][1], p_border_r[pi][2], 0.5);
            cairo_set_line_width(cr, 1);
            draw_rounded_rect(cr, pill_x, pill_y, pw + pill_pad_w * 2, pill_h, pill_r);
            cairo_stroke(cr);

            cairo_set_source_rgb(cr, p_border_r[pi][0], p_border_r[pi][1], p_border_r[pi][2]);
            cairo_move_to(cr, pill_x + pill_pad_w, pill_cy - ph / 2.0 + 1);
            pango_cairo_show_layout(cr, p_layouts[pi]);

            pill_x += pw + pill_pad_w * 2 + pill_gap;
        }

        g_object_unref(lay_dsk);
        g_object_unref(lay_upd);
        g_object_unref(lay_mem);
        g_object_unref(lay_cpu);
        g_object_unref(lay);
    }
}

enum click_action bar_handle_click(struct bar *bar, int x, int y)
{
    for (int i = 0; i < bar->n_clickables; i++) {
        struct clickable *c = &bar->clickables[i];
        if (x >= c->x && x < c->x + c->w &&
            y >= c->y && y < c->y + c->h)
            return c->action;
    }
    return CLICK_NONE;
}

void bar_update_hover(struct bar *bar, int x, int y)
{
    bar->power_hovered = -1;
    bar->hovered_workspace = -1;

    for (int i = 0; i < bar->n_clickables; i++) {
        struct clickable *c = &bar->clickables[i];
        if (x >= c->x && x < c->x + c->w &&
            y >= c->y && y < c->y + c->h) {
            if (c->action == CLICK_HYPRCTL)
                bar->hovered_workspace = i;
            else if (c->action == CLICK_POWEROFF)
                bar->power_hovered = 0;
            else if (c->action == CLICK_REBOOT)
                bar->power_hovered = 1;
            else if (c->action == CLICK_SUSPEND)
                bar->power_hovered = 2;
            break;
        }
    }
}

void bar_clear_hover(struct bar *bar)
{
    bar->power_hovered = -1;
    bar->hovered_workspace = -1;
}

void bar_update_workspaces(struct bar *bar)
{
    FILE *fp = popen("hyprctl workspaces 2>/dev/null", "r");
    if (!fp) { bar->n_workspaces = 0; return; }

    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), fp) && n < MAX_WORKSPACES) {
        int id;
        if (sscanf(line, "workspace ID %d on", &id) == 1) {
            bar->workspaces[n].id = id;
            bar->workspaces[n].active = false;
            n++;
        }
    }
    bar->n_workspaces = n;
    pclose(fp);

    if (bar->n_workspaces == 0) return;

    fp = popen("hyprctl activeworkspace 2>/dev/null", "r");
    if (!fp) return;
    while (fgets(line, sizeof(line), fp)) {
        int id;
        if (sscanf(line, "workspace ID %d on", &id) == 1) {
            for (int i = 0; i < bar->n_workspaces; i++) {
                if (bar->workspaces[i].id == id) {
                    bar->workspaces[i].active = true;
                    break;
                }
            }
            break;
        }
    }
    pclose(fp);
}

void bar_update_updates(struct bar *bar)
{
    time_t now = time(NULL);
    if (now - bar->updates_last_check < 120) return;
    bar->updates_last_check = now;

    FILE *fp = NULL;
    if (access("/usr/bin/apt-get", X_OK) == 0)
        fp = popen("apt list --upgradable 2>/dev/null | grep -c upgradable", "r");
    else if (access("/usr/bin/pacman", X_OK) == 0)
        fp = popen("pacman -Qu 2>/dev/null | wc -l", "r");
    else if (access("/usr/bin/dnf", X_OK) == 0)
        fp = popen("dnf check-update -q 2>/dev/null | wc -l", "r");

    if (fp) {
        char line[32];
        if (fgets(line, sizeof(line), fp))
            bar->updates_count = atoi(line);
        pclose(fp);
    }
}

void bar_update_disk(struct bar *bar)
{
    time_t now = time(NULL);
    if (now - bar->disk_last_check < 120) return;
    bar->disk_last_check = now;

    struct statvfs buf;
    if (statvfs("/", &buf) == 0) {
        unsigned long total = buf.f_blocks * buf.f_frsize;
        unsigned long avail = buf.f_bavail * buf.f_frsize;
        unsigned long used = total - avail;
        if (total > 0)
            bar->disk_percent = (int)(100 * used / total);
    }
}

void bar_update_system_info(struct bar *bar)
{
    FILE *fp = fopen("/proc/stat", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            long user, nice, sys, idle, iowait, irq, softirq, steal;
            sscanf(line, "cpu %ld %ld %ld %ld %ld %ld %ld %ld",
                   &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal);
            long total = user + nice + sys + idle + iowait + irq + softirq + steal;
            long dtotal = total - bar->cpu_prev_total;
            long didle = idle - bar->cpu_prev_idle;
            if (dtotal > 0)
                bar->cpu_percent = (int)(100 * (dtotal - didle) / dtotal);
            bar->cpu_prev_total = total;
            bar->cpu_prev_idle = idle;
        }
        fclose(fp);
    }

    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[256];
        long total = 0, available = 0;
        while (fgets(line, sizeof(line), fp)) {
            if (sscanf(line, "MemTotal: %ld kB", &total) == 1) continue;
            if (sscanf(line, "MemAvailable: %ld kB", &available) == 1) break;
        }
        if (total > 0)
            bar->mem_percent = (int)(100 * (total - available) / total);
        fclose(fp);
    }
}
