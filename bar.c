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
    bar->updates_last_sync = 0;
    bar->updates_auto_notified = false;
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
        double mr = r * 0.75;
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_arc(cr, cx, cy, mr, 0, 2 * M_PI);
        cairo_arc(cr, cx - mr * 0.35, cy, mr * 0.7, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
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

static void draw_background(cairo_t *cr, int w, int h, struct config *cfg)
{
    float bg_col[4];
    if (config_get_color(cfg, "bar_bg_color", bg_col, NULL) && bg_col[3] > 0.0f) {
        cairo_set_source_rgba(cr, bg_col[0], bg_col[1], bg_col[2], bg_col[3]);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_fill(cr);
    } else {
        cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 0, h);
        cairo_pattern_add_color_stop_rgba(pat, 0,   0.02, 0.02, 0.06, 0.99);
        cairo_pattern_add_color_stop_rgba(pat, 0.5, 0.04, 0.04, 0.10, 0.99);
        cairo_pattern_add_color_stop_rgba(pat, 1,   0.03, 0.03, 0.08, 0.99);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_set_source(cr, pat);
        cairo_fill(cr);
        cairo_pattern_destroy(pat);
    }

    float acc[4];
    float def_acc[] = {0.0f, 0.90f, 1.0f, 1.0f};
    config_get_color(cfg, "accent_color", acc, def_acc);

    cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.15);
    cairo_set_line_width(cr, 6);
    cairo_move_to(cr, 0, h - 0.5);
    cairo_line_to(cr, w, h - 0.5);
    cairo_stroke(cr);

    cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.5);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, 0, h - 0.5);
    cairo_line_to(cr, w, h - 0.5);
    cairo_stroke(cr);
}

static void draw_hyperion_logo(cairo_t *cr, int x, int y, int size, float *color)
{
    double cx = x + size / 2.0;
    double cy = y + size / 2.0;
    double outer_r = size / 2.0 - 1;
    double tip_r = outer_r * 0.35;
    double center_r = outer_r * 0.12;

    cairo_set_source_rgba(cr, color[0], color[1], color[2], 0.9);
    cairo_set_line_width(cr, 2.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    cairo_arc(cr, cx, cy, outer_r, 0, 2 * M_PI);
    cairo_stroke(cr);

    for (int i = 0; i < 3; i++) {
        double a = i * 2 * M_PI / 3 - M_PI / 2;
        double half_w = 0.5;
        double tx = cx + cos(a) * tip_r;
        double ty = cy + sin(a) * tip_r;
        double b1x = cx + cos(a + half_w) * outer_r;
        double b1y = cy + sin(a + half_w) * outer_r;
        double b2x = cx + cos(a - half_w) * outer_r;
        double b2y = cy + sin(a - half_w) * outer_r;
        cairo_move_to(cr, tx, ty);
        cairo_line_to(cr, b1x, b1y);
        cairo_line_to(cr, b2x, b2y);
        cairo_close_path(cr);
        cairo_fill(cr);
    }

    cairo_arc(cr, cx, cy, center_r, 0, 2 * M_PI);
    cairo_fill(cr);
}

static void draw_workspaces(struct bar *bar, cairo_t *cr, int h, int x)
{
    if (!config_get_int(bar->cfg, "show_hyperion", 1))
        return;

    if (bar->n_workspaces == 0) return;

    float hcol[4];
    float def_hcol[] = {0.92f, 0.72f, 0.0f, 1.0f};
    config_get_color(bar->cfg, "hyperion_color", hcol, def_hcol);

    int y = BAR_PADDING;
    int btn_h = h - BAR_PADDING * 2;
    double btn_r = btn_h / 2.0;

    if (config_get_int(bar->cfg, "show_hyperion_logo", 1)) {
        draw_hyperion_logo(cr, x, y, btn_h, hcol);
        x += btn_h + 8;
    }

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
                cairo_set_source_rgba(cr, hcol[0], hcol[1], hcol[2], 0.10);
                cairo_set_line_width(cr, 6);
                draw_rounded_rect(cr, x, y, btn_w, btn_h, btn_r);
                cairo_stroke(cr);
            }
            cairo_set_source_rgba(cr, hcol[0], hcol[1], hcol[2], hovered ? 0.95 : 0.80);
            draw_rounded_rect(cr, x, y, btn_w, btn_h, btn_r);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0.02, 0.02, 0.06);
        } else {
            if (hovered) {
                cairo_set_source_rgba(cr, hcol[0], hcol[1], hcol[2], 0.10);
                cairo_set_line_width(cr, 6);
                draw_rounded_rect(cr, x, y, btn_w, btn_h, btn_r);
                cairo_stroke(cr);
            }
            cairo_set_source_rgba(cr, hcol[0] * 0.6f, hcol[1] * 0.6f, hcol[2] * 0.6f, hovered ? 0.7 : 0.35);
            cairo_set_line_width(cr, 1.2);
            draw_rounded_rect(cr, x, y, btn_w, btn_h, btn_r);
            cairo_stroke(cr);
        }

        if (!active)
            cairo_set_source_rgba(cr, hcol[0] * 0.6f, hcol[1] * 0.6f, hcol[2] * 0.6f, hovered ? 0.9 : 0.5);
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

    draw_background(cr, bar->width, bar->height, bar->cfg);

    int launcher_x = BAR_PADDING;
    if (config_get_int(bar->cfg, "show_applauncher", 0)) {
        int lh = 22;
        int ly = (bar->height - lh) / 2;
        draw_rounded_rect(cr, launcher_x, ly, 36, lh, lh / 2);
        float acc[4];
        float def_acc[] = {0.0f, 0.90f, 1.0f, 1.0f};
        config_get_color(bar->cfg, "accent_color", acc, def_acc);
        cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.25);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.8);
        cairo_set_line_width(cr, 1.5);
        draw_rounded_rect(cr, launcher_x, ly, 36, lh, lh / 2);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, acc[0], acc[1], acc[2], 0.9);
        cairo_arc(cr, launcher_x + 12, ly + lh / 2, 4, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_arc(cr, launcher_x + 22, ly + lh / 2, 4, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_arc(cr, launcher_x + 32, ly + lh / 2, 4, 0, 2 * M_PI);
        cairo_fill(cr);

        if (bar->n_clickables < 32) {
            bar->clickables[bar->n_clickables++] = (struct clickable){
                .x = launcher_x, .y = ly, .w = 36, .h = lh,
                .action = CLICK_LAUNCHER,
            };
        }
        launcher_x += 36 + 8;
    }

    int ws_start = launcher_x;
    draw_workspaces(bar, cr, bar->height, ws_start);

    if (config_get_int(bar->cfg, "show_power", 1))
        draw_power_buttons(bar, cr, bar->height);

    int pw_btn_size = 24;
    int pw_total_w = pw_btn_size * 3 + 6 * 2;
    int pw_start = config_get_int(bar->cfg, "show_power", 1)
        ? bar->width - BAR_PADDING - pw_total_w
        : bar->width;

    int cx = ws_start + 10;
    int cx_end = pw_start - 10;

    float acc[4];
    float def_acc[] = {0.0f, 0.90f, 1.0f, 1.0f};
    config_get_color(bar->cfg, "accent_color", acc, def_acc);

    int tw = 0, th = 0;
    int show_clock = config_get_int(bar->cfg, "show_clock", 1);

    if (show_clock) {
        PangoLayout *lay = pango_cairo_create_layout(cr);
        PangoFontDescription *fd = pango_font_description_from_string("Sans 11");
        pango_layout_set_font_description(lay, fd);
        pango_font_description_free(fd);
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        char buf[64];
        strftime(buf, sizeof(buf), "%a %b %d  %H:%M", tm);
        pango_layout_set_text(lay, buf, -1);
        pango_layout_get_pixel_size(lay, &tw, &th);
        g_object_unref(lay);
    }

    struct {
        char label[64];
        char click_key[32];
        int w, h;
        float border[3];
        float fill[4];
    } pills[8];
    int npills = 0;

    if (config_get_int(bar->cfg, "show_cpu", 1)) {
        const char *ico = config_get(bar->cfg, "cpu_icon", "CPU");
        snprintf(pills[npills].label, sizeof(pills[npills].label), "%s %d%%", ico, bar->cpu_percent);
        snprintf(pills[npills].click_key, sizeof(pills[npills].click_key), "click_cpu");
        pills[npills].border[0] = acc[0]; pills[npills].border[1] = acc[1]; pills[npills].border[2] = acc[2];
        pills[npills].fill[0] = acc[0]*0.28f; pills[npills].fill[1] = acc[1]*0.28f; pills[npills].fill[2] = acc[2]*0.28f; pills[npills].fill[3] = 0.35f;
        npills++;
    }
    if (config_get_int(bar->cfg, "show_mem", 1)) {
        const char *ico = config_get(bar->cfg, "mem_icon", "MEM");
        snprintf(pills[npills].label, sizeof(pills[npills].label), "%s %d%%", ico, bar->mem_percent);
        snprintf(pills[npills].click_key, sizeof(pills[npills].click_key), "click_mem");
        pills[npills].border[0] = acc[0]; pills[npills].border[1] = acc[1]; pills[npills].border[2] = acc[2];
        pills[npills].fill[0] = acc[0]*0.28f; pills[npills].fill[1] = acc[1]*0.28f; pills[npills].fill[2] = acc[2]*0.28f; pills[npills].fill[3] = 0.35f;
        npills++;
    }
    if (config_get_int(bar->cfg, "show_updates", 1)) {
        const char *ico = config_get(bar->cfg, "updates_icon", "UPD");
        snprintf(pills[npills].label, sizeof(pills[npills].label), "%s %d", ico, bar->updates_count);
        snprintf(pills[npills].click_key, sizeof(pills[npills].click_key), "click_updates");
        pills[npills].border[0] = 0.80f; pills[npills].border[1] = 0.20f; pills[npills].border[2] = 1.0f;
        pills[npills].fill[0] = 0.30f; pills[npills].fill[1] = 0.05f; pills[npills].fill[2] = 0.40f; pills[npills].fill[3] = 0.30f;
        if (bar->updates_count > 0) {
            float alert[4];
            float def_alert[] = {1.0f, 0.2f, 0.2f, 1.0f};
            if (config_get_color(bar->cfg, "updates_alert_color", alert, def_alert)) {
                pills[npills].border[0] = alert[0]; pills[npills].border[1] = alert[1]; pills[npills].border[2] = alert[2];
                pills[npills].fill[0] = alert[0]*0.3f; pills[npills].fill[1] = alert[1]*0.3f; pills[npills].fill[2] = alert[2]*0.3f; pills[npills].fill[3] = 0.3f;
            }
        }
        npills++;
    }
    if (config_get_int(bar->cfg, "show_disk", 1)) {
        const char *ico = config_get(bar->cfg, "disk_icon", "DSK");
        snprintf(pills[npills].label, sizeof(pills[npills].label), "%s %d%%", ico, bar->disk_percent);
        snprintf(pills[npills].click_key, sizeof(pills[npills].click_key), "click_disk");
        pills[npills].border[0] = 0.20f; pills[npills].border[1] = 0.85f; pills[npills].border[2] = 0.40f;
        pills[npills].fill[0] = 0.05f; pills[npills].fill[1] = 0.30f; pills[npills].fill[2] = 0.10f; pills[npills].fill[3] = 0.30f;
        int warn_thresh = config_get_int(bar->cfg, "disk_warn_threshold", 90);
        if (bar->disk_percent >= warn_thresh) {
            float warn[4];
            float def_warn[] = {1.0f, 0.6f, 0.0f, 1.0f};
            if (config_get_color(bar->cfg, "disk_warn_color", warn, def_warn)) {
                pills[npills].border[0] = warn[0]; pills[npills].border[1] = warn[1]; pills[npills].border[2] = warn[2];
                pills[npills].fill[0] = warn[0]*0.3f; pills[npills].fill[1] = warn[1]*0.3f; pills[npills].fill[2] = warn[2]*0.3f; pills[npills].fill[3] = 0.3f;
            }
        }
        npills++;
    }
    if (config_get_int(bar->cfg, "show_volume", 1)) {
        const char *ico = config_get(bar->cfg, "volume_icon", "VOL");
        if (bar->volume_muted)
            snprintf(pills[npills].label, sizeof(pills[npills].label), "%s MUTED", ico);
        else
            snprintf(pills[npills].label, sizeof(pills[npills].label), "%s %d%%", ico, bar->volume_percent);
        snprintf(pills[npills].click_key, sizeof(pills[npills].click_key), "click_volume");
        pills[npills].border[0] = 0.20f; pills[npills].border[1] = 0.60f; pills[npills].border[2] = 1.0f;
        pills[npills].fill[0] = 0.05f; pills[npills].fill[1] = 0.20f; pills[npills].fill[2] = 0.40f; pills[npills].fill[3] = 0.30f;
        npills++;
    }
    if (config_get_int(bar->cfg, "show_network", 1)) {
        if (bar->network_ssid[0])
            snprintf(pills[npills].label, sizeof(pills[npills].label), "%s", bar->network_ssid);
        else
            snprintf(pills[npills].label, sizeof(pills[npills].label), "NO NET");
        snprintf(pills[npills].click_key, sizeof(pills[npills].click_key), "click_network");
        pills[npills].border[0] = 0.20f; pills[npills].border[1] = 0.90f; pills[npills].border[2] = 0.20f;
        pills[npills].fill[0] = 0.05f; pills[npills].fill[1] = 0.30f; pills[npills].fill[2] = 0.05f; pills[npills].fill[3] = 0.30f;
        npills++;
    }
    if (config_get_int(bar->cfg, "show_battery", 1) && bar->battery_present) {
        const char *ico = config_get(bar->cfg, "battery_icon", "BAT");
        if (bar->battery_charging)
            snprintf(pills[npills].label, sizeof(pills[npills].label), "%s %d%%+", ico, bar->battery_percent);
        else
            snprintf(pills[npills].label, sizeof(pills[npills].label), "%s %d%%", ico, bar->battery_percent);
        snprintf(pills[npills].click_key, sizeof(pills[npills].click_key), "click_battery");
        pills[npills].border[0] = 0.90f; pills[npills].border[1] = 0.90f; pills[npills].border[2] = 0.20f;
        pills[npills].fill[0] = 0.30f; pills[npills].fill[1] = 0.30f; pills[npills].fill[2] = 0.05f; pills[npills].fill[3] = 0.30f;
        npills++;
    }

    for (int ci = 0; ci < MAX_CUSTOM_MODULES; ci++) {
        char showkey[32];
        snprintf(showkey, sizeof(showkey), "show_custom_%d", ci + 1);
        if (!config_get_int(bar->cfg, showkey, 0)) continue;

        if (!bar->custom_modules[ci].output[0]) continue;

        char prefix[64] = "";
        char prefkey[32];
        snprintf(prefkey, sizeof(prefkey), "custom_%d_prefix", ci + 1);
        const char *p = config_get(bar->cfg, prefkey, "");
        if (p[0]) snprintf(prefix, sizeof(prefix), "%s ", p);

        snprintf(pills[npills].label, sizeof(pills[npills].label), "%s%s", prefix, bar->custom_modules[ci].output);

        char clickkey[32];
        snprintf(clickkey, sizeof(clickkey), "click_custom_%d", ci + 1);
        snprintf(pills[npills].click_key, sizeof(pills[npills].click_key), "%s", clickkey);

        char colkey[32];
        float col[4];
        float def_col[] = {0.5f, 0.5f, 1.0f, 1.0f};
        snprintf(colkey, sizeof(colkey), "custom_%d_color", ci + 1);
        if (!config_get_color(bar->cfg, colkey, col, def_col)) {
            col[0] = def_col[0]; col[1] = def_col[1]; col[2] = def_col[2];
        }
        pills[npills].border[0] = col[0]; pills[npills].border[1] = col[1]; pills[npills].border[2] = col[2];
        pills[npills].fill[0] = col[0]*0.3f; pills[npills].fill[1] = col[1]*0.3f; pills[npills].fill[2] = col[2]*0.3f; pills[npills].fill[3] = 0.3f;
        npills++;
    }

    if (show_clock || npills > 0) {
        int pill_pad_h = config_get_int(bar->cfg, "pill_pad_h", 4);
        int pill_pad_w = config_get_int(bar->cfg, "pill_pad_w", 8);
        int pill_gap = config_get_int(bar->cfg, "pill_gap", 6);
        int clock_pad = (show_clock && npills > 0) ? 14 : 0;

        PangoFontDescription *fd_s = pango_font_description_from_string("Sans Bold 9");
        PangoLayout *l = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(l, fd_s);

        int total_pill_w = 0;
        int pill_h = 0;
        for (int i = 0; i < npills; i++) {
            pango_layout_set_text(l, pills[i].label, -1);
            pango_layout_get_pixel_size(l, &pills[i].w, &pills[i].h);
            total_pill_w += pills[i].w + pill_pad_w * 2;
            if (i < npills - 1) total_pill_w += pill_gap;
            if (pills[i].h > pill_h) pill_h = pills[i].h;
        }
        if (npills > 0) pill_h += pill_pad_h * 2;

        int total_w = tw + clock_pad + total_pill_w;
        int center = cx + (cx_end - cx - total_w) / 2;
        if (center < cx) center = cx;

        int clock_x = center;
        int pill_x = center + tw + clock_pad;

        if (show_clock) {
            const char *dfmt = config_get(bar->cfg, "date_format", "%a %b %d  %H:%M");
            cairo_set_source_rgb(cr, acc[0], acc[1], acc[2]);
            cairo_move_to(cr, clock_x, (bar->height - th) / 2.0 + 1);
            PangoLayout *lay = pango_cairo_create_layout(cr);
            PangoFontDescription *fd = pango_font_description_from_string("Sans 11");
            pango_layout_set_font_description(lay, fd);
            pango_font_description_free(fd);
            time_t t = time(NULL);
            struct tm *tm = localtime(&t);
            char buf[64];
            strftime(buf, sizeof(buf), dfmt, tm);
            pango_layout_set_text(lay, buf, -1);
            pango_cairo_show_layout(cr, lay);
            g_object_unref(lay);
        }

        if (npills > 0) {
            int pill_y = (bar->height - pill_h) / 2;
            double pill_r = pill_h / 2.0;
            int pill_cy = pill_y + pill_h / 2;

            for (int i = 0; i < npills; i++) {
                int pw = pills[i].w;
                int ph = pills[i].h;

                cairo_set_source_rgba(cr, pills[i].fill[0], pills[i].fill[1], pills[i].fill[2], pills[i].fill[3]);
                draw_rounded_rect(cr, pill_x, pill_y, pw + pill_pad_w * 2, pill_h, pill_r);
                cairo_fill(cr);

                cairo_set_source_rgba(cr, pills[i].border[0], pills[i].border[1], pills[i].border[2], 0.5);
                cairo_set_line_width(cr, 1);
                draw_rounded_rect(cr, pill_x, pill_y, pw + pill_pad_w * 2, pill_h, pill_r);
                cairo_stroke(cr);

                cairo_set_source_rgb(cr, pills[i].border[0], pills[i].border[1], pills[i].border[2]);
                cairo_move_to(cr, pill_x + pill_pad_w, pill_cy - ph / 2.0 + 1);
                pango_layout_set_text(l, pills[i].label, -1);
                pango_cairo_show_layout(cr, l);

                if (bar->n_clickables < 32 && pills[i].click_key[0]) {
                    const char *cmd = config_get(bar->cfg, pills[i].click_key, "");
                    bar->clickables[bar->n_clickables] = (struct clickable){
                        .x = pill_x, .y = pill_y,
                        .w = pw + pill_pad_w * 2, .h = pill_h,
                        .action = cmd[0] ? CLICK_RUN : CLICK_NONE,
                    };
                    if (cmd[0])
                        snprintf(bar->clickables[bar->n_clickables].command,
                            sizeof(bar->clickables[bar->n_clickables].command), "%s", cmd);

                    const char *tcmd = config_get(bar->cfg, "tooltip_cmd", "");
                    if (!tcmd[0]) {
                        const char *tkeys[] = {"cpu", "mem", "updates", "disk", "volume", "network", "battery"};
                        const char *tcmds[] = {
                            "ps -eo pid,%%cpu,comm --sort=-%%cpu 2>/dev/null | head -6",
                            "ps -eo pid,%%mem,comm --sort=-%%mem 2>/dev/null | head -6",
                            "pacman -Qu 2>/dev/null | head -8 || echo none",
                            "df -h 2>/dev/null",
                            "pamixer --get-volume; pamixer --get-mute",
                            "iw dev 2>/dev/null | awk '/Interface/{print $2}' | head -1 | xargs -r iw dev link 2>/dev/null || nmcli -t dev status 2>/dev/null | head -3",
                            "cat /sys/class/power_supply/BAT0/uevent 2>/dev/null || echo no battery",
                        };
                        int tsz = sizeof(tkeys) / sizeof(tkeys[0]);
                        for (int ti = 0; ti < tsz && ti < 7; ti++) {
                            char tk[32];
                            snprintf(tk, sizeof(tk), "click_%s", tkeys[ti]);
                            if (strcmp(pills[i].click_key, tk) == 0) {
                                snprintf(bar->clickables[bar->n_clickables].tooltip_cmd,
                                    sizeof(bar->clickables[bar->n_clickables].tooltip_cmd), "%s", tcmds[ti]);
                                break;
                            }
                        }
                    } else {
                        snprintf(bar->clickables[bar->n_clickables].tooltip_cmd,
                            sizeof(bar->clickables[bar->n_clickables].tooltip_cmd), "%s", tcmd);
                    }
                    bar->n_clickables++;
                }

                pill_x += pw + pill_pad_w * 2 + pill_gap;
            }
        }

        if (l) { pango_font_description_free(fd_s); g_object_unref(l); }
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
    int interval = config_get_int(bar->cfg, "update_interval", 30);
    if (now - bar->updates_last_check < interval) return;
    bar->updates_last_check = now;

    /* Periodically sync remote DB so repo changes are detected */
    if (access("/usr/bin/pacman", X_OK) == 0 && now - bar->updates_last_sync > 3600) {
        bar->updates_last_sync = now;
        if (fork() == 0) {
            execl("/bin/sh", "sh", "-c",
                "sudo pacman -Sy --noconfirm 2>/dev/null", (char *)NULL);
            _exit(1);
        }
    }

    const char *custom_cmd = config_get(bar->cfg, "update_cmd", "");
    if (custom_cmd[0]) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s 2>/dev/null | wc -l", custom_cmd);
        FILE *fp = popen(buf, "r");
        if (fp) {
            char line[32];
            if (fgets(line, sizeof(line), fp))
                bar->updates_count = atoi(line);
            pclose(fp);
        }
        return;
    }

    FILE *fp = NULL;
    if (access("/usr/bin/pacman", X_OK) == 0)
        fp = popen("pacman -Qu 2>/dev/null | wc -l", "r");
    else if (access("/usr/bin/dnf", X_OK) == 0)
        fp = popen("dnf check-update -q 2>/dev/null | wc -l", "r");
    else if (access("/usr/bin/apt-get", X_OK) == 0)
        fp = popen("apt list --upgradable 2>/dev/null | grep -c upgradable", "r");

    if (fp) {
        char line[32];
        if (fgets(line, sizeof(line), fp))
            bar->updates_count = atoi(line);
        pclose(fp);
    }

    if (bar->updates_count == 0) {
        bar->updates_auto_notified = false;
    } else if (config_get_int(bar->cfg, "auto_update", 0) && !bar->updates_auto_notified) {
        bar->updates_auto_notified = true;
        const char *cmd = config_get(bar->cfg, "auto_update_cmd", "");
        if (!cmd[0]) cmd = "wlstatus-update";
        if (fork() == 0) {
            setsid();
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
            _exit(1);
        }
    }
}

void bar_update_disk(struct bar *bar)
{
    time_t now = time(NULL);
    int interval = config_get_int(bar->cfg, "disk_interval", 120);
    if (now - bar->disk_last_check < interval) return;
    bar->disk_last_check = now;

    const char *path = config_get(bar->cfg, "disk_path", "/");
    struct statvfs buf;
    if (statvfs(path, &buf) == 0) {
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

void bar_update_volume(struct bar *bar)
{
    FILE *fp = popen("pamixer --get-volume 2>/dev/null", "r");
    if (fp) {
        char line[16];
        if (fgets(line, sizeof(line), fp))
            bar->volume_percent = atoi(line);
        pclose(fp);
    }
    fp = popen("pamixer --get-mute 2>/dev/null", "r");
    if (fp) {
        char line[16];
        if (fgets(line, sizeof(line), fp))
            bar->volume_muted = (line[0] == 't');
        pclose(fp);
    }
}

void bar_update_network(struct bar *bar)
{
    bar->network_ssid[0] = '\0';

    FILE *fp = popen("nmcli -t -f active,ssid dev wifi 2>/dev/null | grep '^yes:' | cut -d: -f2", "r");
    if (fp) {
        if (fgets(bar->network_ssid, sizeof(bar->network_ssid), fp)) {
            char *nl = strchr(bar->network_ssid, '\n');
            if (nl) *nl = '\0';
        }
        pclose(fp);
        if (bar->network_ssid[0]) return;
    }

    const char *cfg_iface = config_get(bar->cfg, "network_iface", "");
    if (cfg_iface[0]) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "iw dev %s link 2>/dev/null | awk '/SSID/{print $2}'", cfg_iface);
        FILE *f2 = popen(cmd, "r");
        if (f2) {
            if (fgets(bar->network_ssid, sizeof(bar->network_ssid), f2)) {
                char *nl2 = strchr(bar->network_ssid, '\n');
                if (nl2) *nl2 = '\0';
            }
            pclose(f2);
        }
    } else {
        fp = popen("iw dev 2>/dev/null | awk '/Interface/{print $2}'", "r");
        if (fp) {
            char iface[32];
            while (fgets(iface, sizeof(iface), fp)) {
                char *nl = strchr(iface, '\n');
                if (nl) *nl = '\0';
                if (!iface[0]) continue;
                char cmd[128];
                snprintf(cmd, sizeof(cmd), "iw dev %s link 2>/dev/null | awk '/SSID/{print $2}'", iface);
                FILE *f2 = popen(cmd, "r");
                if (f2) {
                    if (fgets(bar->network_ssid, sizeof(bar->network_ssid), f2)) {
                        char *nl2 = strchr(bar->network_ssid, '\n');
                        if (nl2) *nl2 = '\0';
                    }
                    pclose(f2);
                    if (bar->network_ssid[0]) break;
                }
            }
            pclose(fp);
        }
    }
}

void bar_update_battery(struct bar *bar)
{
    bar->battery_present = false;
    FILE *fp = fopen("/sys/class/power_supply/BAT0/uevent", "r");
    if (!fp) return;
    bar->battery_present = true;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "POWER_SUPPLY_CAPACITY=%d", &bar->battery_percent) == 1)
            continue;
        if (strncmp(line, "POWER_SUPPLY_STATUS=Charging", 28) == 0)
            bar->battery_charging = true;
        else if (strncmp(line, "POWER_SUPPLY_STATUS=Full", 24) == 0)
            bar->battery_percent = 100;
    }
    fclose(fp);
}

void bar_update_custom_modules(struct bar *bar)
{
    for (int i = 0; i < MAX_CUSTOM_MODULES; i++) {
        char key[32];
        snprintf(key, sizeof(key), "custom_%d_cmd", i + 1);
        const char *cmd = config_get(bar->cfg, key, "");
        if (!cmd[0]) continue;

        char intkey[32];
        snprintf(intkey, sizeof(intkey), "custom_%d_interval", i + 1);
        int interval = config_get_int(bar->cfg, intkey, 60);

        time_t now = time(NULL);
        if (now - bar->custom_modules[i].last_check < interval)
            continue;
        bar->custom_modules[i].last_check = now;

        FILE *fp = popen(cmd, "r");
        if (!fp) continue;
        if (fgets(bar->custom_modules[i].output, sizeof(bar->custom_modules[i].output), fp)) {
            char *nl = strchr(bar->custom_modules[i].output, '\n');
            if (nl) *nl = '\0';
        }
        pclose(fp);
    }
}
