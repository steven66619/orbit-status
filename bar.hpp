#ifndef BAR_HPP
#define BAR_HPP

#include <ctime>
#include <cairo.h>
#include <X11/Xlib.h>
#include "config.hpp"
#include "lua_plugin.hpp"

#define BAR_HEIGHT 38
#define BAR_PADDING 8
#define MAX_WORKSPACES 10
#define MAX_LUA_PLUGINS 12
#define MAX_TRACKED_WINDOWS 64

enum ClickAction {
    CLICK_NONE,
    CLICK_POWEROFF,
    CLICK_REBOOT,
    CLICK_SUSPEND,
    CLICK_HYPRCTL,
    CLICK_RUN,
};

struct Clickable {
    int x, y, w, h;
    ClickAction action;
    char command[256];
    char tooltip_cmd[128];
    char tooltip_text[512];
    int lua_plugin_idx;
};

struct Workspace {
    int id;
    bool active;
    char name[64];
};

struct TrackedWindow {
    char address[32];
    int workspace_id;
    char cls[64];
};

struct Bar {
    int width, height;
    Config *cfg;
    int n_clickables;
    Clickable clickables[32];
    cairo_surface_t *icons[8];
    int n_icons;
    int power_hovered;
    int hovered_workspace;

    Workspace workspaces[MAX_WORKSPACES];
    int n_workspaces;

    LuaPlugin lua_plugins[MAX_LUA_PLUGINS];
    int n_lua_plugins;

    char active_window_class[64];
    char active_window_title[192];

    int tray_width;

    Bar() : width(0), height(0), cfg(nullptr), n_clickables(0), n_icons(0),
            power_hovered(-1), hovered_workspace(-1), n_workspaces(0), n_lua_plugins(0),
            tray_width(0) {
        for (int i = 0; i < 8; i++) icons[i] = nullptr;
        active_window_class[0] = '\0';
        active_window_title[0] = '\0';
        for (int i = 0; i < MAX_WORKSPACES; i++)
            workspaces[i].name[0] = '\0';
    }
};

Bar *bar_create(int width, int height, Config *cfg);
void bar_destroy(Bar *bar);
void bar_render(Bar *bar, cairo_t *cr);
ClickAction bar_handle_click(Bar *bar, int x, int y);
void bar_update_hover(Bar *bar, int x, int y);
void bar_clear_hover(Bar *bar);
void bar_load_png_icon(Bar *bar, const char *path, int index);
void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r);
int draw_workspaces(Bar *bar, cairo_t *cr, int h, int x);
void bar_update_workspaces(Bar *bar, Display *dpy);
void bar_update_lua_plugins(Bar *bar);
void bar_update_workspace_names(Bar *bar, TrackedWindow *windows, int n_windows);
const char *prettify_class(const char *cls);
void bar_lua_plugins_destroy(Bar *bar);
void bar_set_active_window(Bar *bar, const char *cls, const char *title);

#endif
