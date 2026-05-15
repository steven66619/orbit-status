#ifndef BAR_H
#define BAR_H

#include <stdbool.h>
#include <time.h>
#include <cairo.h>
#include "config.h"

#define BAR_HEIGHT 38
#define BAR_PADDING 8
#define MAX_WORKSPACES 10
#define MAX_CUSTOM_MODULES 4

enum click_action {
    CLICK_NONE,
    CLICK_POWEROFF,
    CLICK_REBOOT,
    CLICK_SUSPEND,
    CLICK_HYPRCTL,
    CLICK_RUN,
};

struct clickable {
    int x, y, w, h;
    enum click_action action;
    char command[256];
    char tooltip_cmd[128];
};

struct workspace {
    int id;
    bool active;
};

struct custom_module {
    char output[64];
    time_t last_check;
};

struct bar {
    int width, height;
    struct config *cfg;
    int n_clickables;
    struct clickable clickables[32];
    cairo_surface_t *icons[8];
    int n_icons;
    int power_hovered;
    int hovered_workspace;

    struct workspace workspaces[MAX_WORKSPACES];
    int n_workspaces;

    long cpu_prev_total;
    long cpu_prev_idle;
    int cpu_percent;
    int mem_percent;
    int updates_count;
    time_t updates_last_check;

    int disk_percent;
    time_t disk_last_check;

    int volume_percent;
    bool volume_muted;

    char network_ssid[64];

    int battery_percent;
    bool battery_charging;
    bool battery_present;

    struct custom_module custom_modules[MAX_CUSTOM_MODULES];
};

struct bar *bar_create(int width, int height, struct config *cfg);
void bar_destroy(struct bar *bar);
void bar_render(struct bar *bar, cairo_t *cr);
enum click_action bar_handle_click(struct bar *bar, int x, int y);
void bar_update_hover(struct bar *bar, int x, int y);
void bar_clear_hover(struct bar *bar);
void bar_load_png_icon(struct bar *bar, const char *path, int index);
void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r);
void bar_update_workspaces(struct bar *bar);
void bar_update_system_info(struct bar *bar);
void bar_update_updates(struct bar *bar);
void bar_update_disk(struct bar *bar);
void bar_update_volume(struct bar *bar);
void bar_update_network(struct bar *bar);
void bar_update_battery(struct bar *bar);
void bar_update_custom_modules(struct bar *bar);

#endif
