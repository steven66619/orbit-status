#ifndef LUA_PLUGIN_HPP
#define LUA_PLUGIN_HPP

#include <ctime>

#define MAX_LUA_PLUGIN_PATH 256
#define MAX_LUA_PLUGIN_OUTPUT 256

enum PluginType { PLUGIN_LUA, PLUGIN_SHELL };

struct LuaPlugin {
    PluginType type;
    char path[MAX_LUA_PLUGIN_PATH];
    char cmd[256];
    void *state;
    char output[MAX_LUA_PLUGIN_OUTPUT];
    time_t last_check;
    int interval;

    LuaPlugin() : type(PLUGIN_LUA), state(nullptr), last_check(0), interval(5) {
        path[0] = '\0';
        cmd[0] = '\0';
        output[0] = '\0';
    }
};

int lua_plugin_init(LuaPlugin *p, const char *path);
int lua_plugin_init_shell(LuaPlugin *p, const char *cmd, int interval_sec);
void lua_plugin_destroy(LuaPlugin *p);
bool lua_plugin_tick(LuaPlugin *p);
const char *lua_plugin_get_tooltip(LuaPlugin *p);
void lua_plugin_call_onclick(LuaPlugin *p);
void lua_plugin_call_onscroll(LuaPlugin *p, int direction);

#endif
