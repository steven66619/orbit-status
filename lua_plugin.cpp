#include "lua_plugin.hpp"
extern "C" {
#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>
#include <lua5.4/lualib.h>
}
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>

int lua_plugin_init(LuaPlugin *p, const char *path) {
    p->type = PLUGIN_LUA;
    lua_State *L = luaL_newstate();
    if (!L) return -1;
    p->state = L;
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

    p->last_check = 0;
    return 0;
}

int lua_plugin_init_shell(LuaPlugin *p, const char *cmd, int interval_sec) {
    p->type = PLUGIN_SHELL;
    p->state = nullptr;
    p->path[0] = '\0';
    snprintf(p->cmd, sizeof(p->cmd), "%s", cmd);
    p->output[0] = '\0';
    p->last_check = 0;
    p->interval = interval_sec > 0 ? interval_sec : 5;
    p->last_check = 0;
    return 0;
}

void lua_plugin_destroy(LuaPlugin *p) {
    if (p->type == PLUGIN_LUA && p->state) {
        lua_close((lua_State *)p->state);
        p->state = nullptr;
    }
}

static bool do_tick_fork(LuaPlugin *p) {
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) return false;

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return false; }

    if (pid == 0) {
        close(pipefd[0]);
        if (p->type == PLUGIN_SHELL) {
            FILE *f = popen(p->cmd, "r");
            if (f) {
                char buf[256];
                if (fgets(buf, sizeof(buf), f)) {
                    write(pipefd[1], buf, strlen(buf));
                }
                pclose(f);
            }
        } else {
            lua_State *L = (lua_State *)p->state;
            lua_getglobal(L, "tick");
            if (lua_isfunction(L, -1)) {
                if (lua_pcall(L, 0, 1, 0) == LUA_OK && lua_isstring(L, -1)) {
                    const char *result = lua_tostring(L, -1);
                    if (result) {
                        size_t len = strlen(result);
                        if (len > 0) write(pipefd[1], result, len);
                    }
                }
            }
        }
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);

    struct pollfd pfd = { pipefd[0], POLLIN, 0 };
    char buf[MAX_LUA_PLUGIN_OUTPUT];
    ssize_t total = 0;
    bool ok = false;

    while (total < (ssize_t)sizeof(buf) - 1) {
        int ret = poll(&pfd, 1, 2000);
        if (ret <= 0) break;
        ssize_t n = read(pipefd[0], buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += n;
    }

    if (total > 0) {
        buf[total] = '\0';
        while (total > 0 && (buf[total-1] == '\n' || buf[total-1] == '\r')) buf[--total] = '\0';
        snprintf(p->output, sizeof(p->output), "%s", buf);
        ok = true;
    }

    int wst;
    pid_t wp = waitpid(pid, &wst, WNOHANG);
    if (wp == 0) { kill(pid, SIGKILL); waitpid(pid, nullptr, 0); }
    close(pipefd[0]);
    return ok;
}

bool lua_plugin_tick(LuaPlugin *p) {
    if (p->type == PLUGIN_SHELL) {
        if (!p->cmd[0]) return false;
        return do_tick_fork(p);
    }

    if (!p->state) return false;

    lua_State *L = (lua_State *)p->state;
    lua_getglobal(L, "tick");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_pop(L, 1);

    return do_tick_fork(p);
}

const char *lua_plugin_get_tooltip(LuaPlugin *p) {
    if (p->type == PLUGIN_SHELL || !p->state) return nullptr;
    static char buf[512];

    lua_State *L = (lua_State *)p->state;
    lua_getglobal(L, "on_tooltip");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            return nullptr;
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
    return nullptr;
}

void lua_plugin_call_onclick(LuaPlugin *p) {
    if (p->type == PLUGIN_SHELL || !p->state) return;

    lua_State *L = (lua_State *)p->state;
    lua_getglobal(L, "on_click");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK)
            fprintf(stderr, "lua on_click error: %s\n", lua_tostring(L, -1));
    }
    lua_pop(L, 1);
}

void lua_plugin_call_onscroll(LuaPlugin *p, int direction) {
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
