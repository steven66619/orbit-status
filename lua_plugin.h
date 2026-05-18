#ifndef LUA_PLUGIN_H
#define LUA_PLUGIN_H

#include <stdbool.h>
#include <time.h>

#define MAX_LUA_PLUGIN_PATH 256
#define MAX_LUA_PLUGIN_OUTPUT 256

enum plugin_type { PLUGIN_LUA, PLUGIN_SHELL };

struct lua_plugin {
    enum plugin_type type;
    char path[MAX_LUA_PLUGIN_PATH];
    char cmd[256];
    void *state;
    char output[MAX_LUA_PLUGIN_OUTPUT];
    time_t last_check;
    int interval;
};

int lua_plugin_init(struct lua_plugin *p, const char *path);
int lua_plugin_init_shell(struct lua_plugin *p, const char *cmd, int interval_sec);
void lua_plugin_destroy(struct lua_plugin *p);
bool lua_plugin_tick(struct lua_plugin *p);
const char *lua_plugin_get_tooltip(struct lua_plugin *p);
void lua_plugin_call_onclick(struct lua_plugin *p);
void lua_plugin_call_onscroll(struct lua_plugin *p, int direction);

#endif
