#ifndef BAR_HPP
#define BAR_HPP

#include <ctime>
#include <cairo.h>
#include "config.hpp"
#include "lua_plugin.hpp"
#include "sni_tray.hpp"

#define BAR_HEIGHT 38
#define BAR_PADDING 8
#define MAX_WORKSPACES 10
#define MAX_LUA_PLUGINS 12

enum ClickAction {
    CLICK_NONE,
    CLICK_POWEROFF,
    CLICK_REBOOT,
    CLICK_SUSPEND,
    CLICK_WORKSPACE,
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

struct Bar {
    int width, height;
    Config *cfg;
    int n_clickables;
    Clickable clickables[32];
    int power_hovered;
    int hovered_workspace;
    char workspace_switch_cmd[128];

    Workspace workspaces[MAX_WORKSPACES];
    int n_workspaces;

    LuaPlugin lua_plugins[MAX_LUA_PLUGINS];
    int n_lua_plugins;

    char active_window_class[64];
    char active_window_title[192];

    SniTray *tray = nullptr;   // owned by the caller (main); drawn by the bar
    int tray_width = 0;        // width reserved for the tray (computed on render)

    Bar() : width(0), height(0), cfg(nullptr), n_clickables(0),
            power_hovered(-1), hovered_workspace(-1), n_workspaces(0), n_lua_plugins(0) {
        workspace_switch_cmd[0] = '\0';
        active_window_class[0] = '\0';
        active_window_title[0] = '\0';
        for (int i = 0; i < MAX_WORKSPACES; i++)
            workspaces[i].name[0] = '\0';
    }
};

Bar *bar_create(int width, int height, Config *cfg, const char *ws_cmd_default);
void bar_destroy(Bar *bar);
void bar_render(Bar *bar, cairo_t *cr);
void bar_update_hover(Bar *bar, int x, int y);
void bar_clear_hover(Bar *bar);
void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r);
int draw_workspaces(Bar *bar, cairo_t *cr, int h, int x);
void bar_set_workspaces(Bar *bar, const Workspace *workspaces, int n);
void bar_update_lua_plugins(Bar *bar);
const char *prettify_class(const char *cls);
void bar_lua_plugins_destroy(Bar *bar);
void bar_set_active_window(Bar *bar, const char *cls, const char *title);

#endif
