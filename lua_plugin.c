#include "lua_plugin.h"
#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>
#include <lua5.4/lualib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int lua_plugin_init(struct lua_plugin *p, const char *path)
{
    p->type = PLUGIN_LUA;
    p->state = luaL_newstate();
    if (!p->state) return -1;

    lua_State *L = (lua_State *)p->state;
    luaL_openlibs(L);

    snprintf(p->path, sizeof(p->path), "%s", path);
    p->cmd[0] = '\0';
    p->output[0] = '\0';
    p->last_check = 0;
    p->interval = 5;

    if (luaL_dofile(L, path) != LUA_OK) {
        fprintf(stderr, "lua: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }

    lua_getglobal(L, "interval");
    if (lua_isinteger(L, -1))
        p->interval = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);

    return 0;
}

int lua_plugin_init_shell(struct lua_plugin *p, const char *cmd, int interval_sec)
{
    p->type = PLUGIN_SHELL;
    p->state = NULL;
    p->path[0] = '\0';
    snprintf(p->cmd, sizeof(p->cmd), "%s", cmd);
    p->output[0] = '\0';
    p->last_check = 0;
    p->interval = interval_sec > 0 ? interval_sec : 5;
    return 0;
}

void lua_plugin_destroy(struct lua_plugin *p)
{
    if (p->type == PLUGIN_LUA && p->state) {
        lua_close((lua_State *)p->state);
        p->state = NULL;
    }
}

bool lua_plugin_tick(struct lua_plugin *p)
{
    if (p->type == PLUGIN_SHELL) {
        FILE *f = popen(p->cmd, "r");
        if (!f) return false;
        char buf[MAX_LUA_PLUGIN_OUTPUT];
        if (fgets(buf, sizeof(buf), f)) {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
            snprintf(p->output, sizeof(p->output), "%s", buf);
            pclose(f);
            return true;
        }
        pclose(f);
        return false;
    }

    if (!p->state) return false;

    lua_State *L = (lua_State *)p->state;
    lua_getglobal(L, "tick");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
            fprintf(stderr, "lua tick error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
            return false;
        }
        if (lua_isstring(L, -1)) {
            const char *result = lua_tostring(L, -1);
            if (result) {
                snprintf(p->output, sizeof(p->output), "%s", result);
                lua_pop(L, 1);
                return true;
            }
        }
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }
    return false;
}

const char *lua_plugin_get_tooltip(struct lua_plugin *p)
{
    if (p->type == PLUGIN_SHELL || !p->state) return NULL;
    static char buf[512];

    lua_State *L = (lua_State *)p->state;
    lua_getglobal(L, "on_tooltip");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            return NULL;
        }
        if (lua_isstring(L, -1)) {
            const char *s = lua_tostring(L, -1);
            if (s) {
                snprintf(buf, sizeof(buf), "%s", s);
                lua_pop(L, 1);
                return buf;
            }
        }
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }
    return NULL;
}

void lua_plugin_call_onclick(struct lua_plugin *p)
{
    if (p->type == PLUGIN_SHELL || !p->state) return;

    lua_State *L = (lua_State *)p->state;
    lua_getglobal(L, "on_click");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK)
            fprintf(stderr, "lua on_click error: %s\n", lua_tostring(L, -1));
    }
    lua_pop(L, 1);
}

void lua_plugin_call_onscroll(struct lua_plugin *p, int direction)
{
    if (p->type == PLUGIN_SHELL || !p->state) return;

    lua_State *L = (lua_State *)p->state;
    lua_getglobal(L, "on_scroll");
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, direction);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            fprintf(stderr, "lua on_scroll error: %s\n", lua_tostring(L, -1));
    }
    lua_pop(L, 1);
}
