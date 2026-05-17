#ifndef LUA_PLUGIN_H
#define LUA_PLUGIN_H

#include <stdbool.h>
#include <time.h>

#define MAX_LUA_PLUGIN_PATH 256
#define MAX_LUA_PLUGIN_OUTPUT 256

struct lua_plugin {
    char path[MAX_LUA_PLUGIN_PATH];
    void *state;
    char output[MAX_LUA_PLUGIN_OUTPUT];
    time_t last_check;
    int interval;
};

int lua_plugin_init(struct lua_plugin *p, const char *path);
void lua_plugin_destroy(struct lua_plugin *p);
bool lua_plugin_tick(struct lua_plugin *p);
const char *lua_plugin_get_tooltip(struct lua_plugin *p);
void lua_plugin_call_onclick(struct lua_plugin *p);

#endif
