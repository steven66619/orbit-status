#include "bar.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
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

    bar->n_lua_plugins = 0;
    for (int i = 0; i < MAX_LUA_PLUGINS; i++) {
        char cmdkey[32], pathkey[32], ikey[32];
        snprintf(cmdkey, sizeof(cmdkey), "lua_plugin_%d_cmd", i + 1);
        snprintf(pathkey, sizeof(pathkey), "lua_plugin_%d_path", i + 1);
        snprintf(ikey, sizeof(ikey), "lua_plugin_%d_interval", i + 1);
        const char *cmd = config_get(cfg, cmdkey, "");
        const char *path = config_get(cfg, pathkey, "");

        if (cmd[0]) {
            int interval = config_get_int(cfg, ikey, 5);
            if (lua_plugin_init_shell(&bar->lua_plugins[bar->n_lua_plugins], cmd, interval) == 0)
                bar->n_lua_plugins++;
        } else if (path[0]) {
            if (lua_plugin_init(&bar->lua_plugins[bar->n_lua_plugins], path) == 0)
                bar->n_lua_plugins++;
        }
    }

    return bar;
}

void bar_destroy(struct bar *bar)
{
    if (!bar) return;
    for (int i = 0; i < bar->n_icons; i++)
        if (bar->icons[i])
            cairo_surface_destroy(bar->icons[i]);
    bar_lua_plugins_destroy(bar);
    free(bar);
}

void bar_lua_plugins_destroy(struct bar *bar)
{
    for (int i = 0; i < bar->n_lua_plugins; i++)
        lua_plugin_destroy(&bar->lua_plugins[i]);
    bar->n_lua_plugins = 0;
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
                .lua_plugin_idx = -1,
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
            cl->lua_plugin_idx = -1;
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

    int ws_start = BAR_PADDING;
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

    const char *font_family = config_get(bar->cfg, "font_family", "Sans");
    int clock_font_size = config_get_int(bar->cfg, "clock_font_size", 11);
    int pill_font_size = config_get_int(bar->cfg, "pill_font_size", 9);

    if (show_clock) {
        char desc[64];
        snprintf(desc, sizeof(desc), "%s %d", font_family, clock_font_size);
        PangoLayout *lay = pango_cairo_create_layout(cr);
        PangoFontDescription *fd = pango_font_description_from_string(desc);
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

    struct pill {
        int plugin_idx;
        char label[64];
        char click_key[32];
        int w, h;
        float border[3];
        float fill[4];
    } pills[16];
    int npills = 0;

    for (int li = 0; li < bar->n_lua_plugins; li++) {
        char showkey[32];
        snprintf(showkey, sizeof(showkey), "show_lua_plugin_%d", li + 1);
        if (!config_get_int(bar->cfg, showkey, 1)) continue;

        if (!bar->lua_plugins[li].output[0]) continue;

        char prefix[64] = "";
        char prefkey[32];
        snprintf(prefkey, sizeof(prefkey), "lua_plugin_%d_prefix", li + 1);
        const char *p = config_get(bar->cfg, prefkey, "");
        if (p[0]) snprintf(prefix, sizeof(prefix), "%s ", p);

        snprintf(pills[npills].label, sizeof(pills[npills].label), "%s%s", prefix, bar->lua_plugins[li].output);

        char clickkey[32];
        snprintf(clickkey, sizeof(clickkey), "click_lua_plugin_%d", li + 1);
        snprintf(pills[npills].click_key, sizeof(pills[npills].click_key), "%s", clickkey);

        char colkey[32];
        float col[4];
        float def_col[] = {0.6f, 0.3f, 0.9f, 1.0f};
        snprintf(colkey, sizeof(colkey), "lua_plugin_%d_color", li + 1);
        if (!config_get_color(bar->cfg, colkey, col, def_col)) {
            col[0] = def_col[0]; col[1] = def_col[1]; col[2] = def_col[2];
        }
        pills[npills].plugin_idx = li;
        pills[npills].border[0] = col[0]; pills[npills].border[1] = col[1]; pills[npills].border[2] = col[2];
        pills[npills].fill[0] = col[0]*0.3f; pills[npills].fill[1] = col[1]*0.3f; pills[npills].fill[2] = col[2]*0.3f; pills[npills].fill[3] = 0.3f;
        npills++;
    }

    const char *order_str = config_get(bar->cfg, "pill_order", "");
    if (order_str[0]) {
        int order[16], norder = 0;
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%s", order_str);
        for (char *tok = strtok(tmp, ","); tok && norder < 16; tok = strtok(NULL, ",")) {
            int idx = atoi(tok) - 1;
            if (idx >= 0 && idx < npills) order[norder++] = idx;
        }
        if (norder > 0) {
            struct pill sorted[16];
            for (int i = 0; i < norder && i < 16; i++)
                sorted[i] = pills[order[i]];
            memcpy(pills, sorted, sizeof(pills[0]) * (norder < 16 ? norder : 16));
            npills = norder < 16 ? norder : 16;
        }
    }

    if (show_clock || npills > 0) {
        int pill_pad_h = config_get_int(bar->cfg, "pill_pad_h", 4);
        int pill_pad_w = config_get_int(bar->cfg, "pill_pad_w", 8);
        int pill_gap = config_get_int(bar->cfg, "pill_gap", 6);
        int clock_pad = (show_clock && npills > 0) ? 14 : 0;

        char pill_desc[64];
        snprintf(pill_desc, sizeof(pill_desc), "%s Bold %d", font_family, pill_font_size);
        PangoFontDescription *fd_s = pango_font_description_from_string(pill_desc);
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
            char clock_desc[64];
            snprintf(clock_desc, sizeof(clock_desc), "%s %d", font_family, clock_font_size);
            PangoFontDescription *fd = pango_font_description_from_string(clock_desc);
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
                        .lua_plugin_idx = -1,
                    };
                    if (cmd[0])
                        snprintf(bar->clickables[bar->n_clickables].command,
                            sizeof(bar->clickables[bar->n_clickables].command), "%s", cmd);

                    if (strncmp(pills[i].click_key, "click_lua_plugin_", 17) == 0) {
                        int idx = atoi(pills[i].click_key + 17) - 1;
                        if (idx >= 0 && idx < bar->n_lua_plugins) {
                            bar->clickables[bar->n_clickables].lua_plugin_idx = idx;
                            const char *tt = lua_plugin_get_tooltip(&bar->lua_plugins[idx]);
                            if (tt)
                                snprintf(bar->clickables[bar->n_clickables].tooltip_text,
                                    sizeof(bar->clickables[bar->n_clickables].tooltip_text), "%s", tt);
                        }
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

void bar_update_lua_plugins(struct bar *bar)
{
    for (int i = 0; i < bar->n_lua_plugins; i++) {
        time_t now = time(NULL);
        if (now - bar->lua_plugins[i].last_check < bar->lua_plugins[i].interval)
            continue;
        bar->lua_plugins[i].last_check = now;
        lua_plugin_tick(&bar->lua_plugins[i]);
    }
}
