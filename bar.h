#ifndef BAR_H
#define BAR_H

#include <stdbool.h>
#include <time.h>
#include <cairo.h>
#include "config.h"
#include "lua_plugin.h"

#define BAR_HEIGHT 38
#define BAR_PADDING 8
#define MAX_WORKSPACES 10
#define MAX_LUA_PLUGINS 12

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
    char tooltip_text[512];
};

struct workspace {
    int id;
    bool active;
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

    struct lua_plugin lua_plugins[MAX_LUA_PLUGINS];
    int n_lua_plugins;
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
void bar_update_lua_plugins(struct bar *bar);
void bar_lua_plugins_destroy(struct bar *bar);

#endif
