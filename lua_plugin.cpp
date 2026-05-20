#include "lua_plugin.hpp"

extern "C" {
#include <lua5.4/lua.h>
#include <lua5.4/lauxlib.h>
#include <lua5.4/lualib.h>
}

#include <array>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>

#include <unistd.h>
#include <csignal>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>

#if defined(BUILD_XORG)
#include <X11/Xlib.h>
#include <X11/Xatom.h>

static Display* g_x11_display = nullptr;
static Window g_x11_root = 0;
static Atom g_xmonad_log_atom = 0;
static Atom g_utf8_string_atom = 0;
#endif

namespace {

using clock_type = std::chrono::steady_clock;

bool do_tick_fork(LuaPlugin* p) {
    std::array<int, 2> pipefd{};
    if (::pipe2(pipefd.data(), O_CLOEXEC) < 0) return false;

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        ::close(pipefd[0]);
        if (p->type == PLUGIN_SHELL) {
            FILE* f = ::popen(p->cmd, "r");
            if (f) {
                std::array<char, 256> buf{};
                if (::fgets(buf.data(), static_cast<int>(buf.size()), f))
                    ::write(pipefd[1], buf.data(), std::strlen(buf.data()));
                ::pclose(f);
            }
        } else {
#if defined(BUILD_XORG)
            // Forked child inherits parent's X11 fd.  Close it and clear the
            // display pointer so get_xmonad_workspaces() returns empty table
            // instead of issuing X11 requests that corrupt the parent's state.
            if (g_x11_display) {
                ::close(ConnectionNumber(g_x11_display));
                g_x11_display = nullptr;
            }
#endif
            lua_State* L = static_cast<lua_State*>(p->state);
            lua_getglobal(L, "tick");
            if (lua_isfunction(L, -1)) {
                if (lua_pcall(L, 0, 1, 0) == LUA_OK && lua_isstring(L, -1)) {
                    const char* result = lua_tostring(L, -1);
                    if (result) {
                        std::size_t len = std::strlen(result);
                        if (len > 0)
                            ::write(pipefd[1], result, len);
                    }
                }
            }
        }
        ::close(pipefd[1]);
        ::_exit(0);
    }

    ::close(pipefd[1]);

    struct pollfd pfd{ pipefd[0], POLLIN, 0 };
    std::array<char, MAX_LUA_PLUGIN_OUTPUT> buf{};
    ssize_t total = 0;
    bool ok = false;

    while (total < static_cast<ssize_t>(buf.size()) - 1) {
        int ret = ::poll(&pfd, 1, 2000);
        if (ret <= 0) break;
        ssize_t n = ::read(pipefd[0],
            buf.data() + total, buf.size() - 1 - static_cast<std::size_t>(total));
        if (n <= 0) break;
        total += n;
    }

    if (total > 0) {
        buf[static_cast<std::size_t>(total)] = '\0';
        while (total > 0 && (buf[static_cast<std::size_t>(total) - 1] == '\n' ||
                             buf[static_cast<std::size_t>(total) - 1] == '\r'))
            buf[--total] = '\0';
        std::snprintf(p->output, sizeof(p->output), "%s", buf.data());
        ok = true;
    }

    int wst;
    pid_t wp = ::waitpid(pid, &wst, WNOHANG);
    if (wp == 0) { ::kill(pid, SIGKILL); ::waitpid(pid, nullptr, 0); }
    ::close(pipefd[0]);
    return ok;
}

} // anonymous namespace

void lua_plugin_set_x11_display(void* display, unsigned long root) {
#if defined(BUILD_XORG)
    g_x11_display = static_cast<Display*>(display);
    g_x11_root = static_cast<Window>(root);
    g_xmonad_log_atom = XInternAtom(g_x11_display, "_XMONAD_LOG", False);
    g_utf8_string_atom = XInternAtom(g_x11_display, "UTF8_STRING", False);
    int x11_fd = ConnectionNumber(g_x11_display);
    int fl = ::fcntl(x11_fd, F_GETFD);
    if (fl != -1) ::fcntl(x11_fd, F_SETFD, fl | FD_CLOEXEC);
#else
    (void)display;
    (void)root;
#endif
}

#if defined(BUILD_XORG)
static int l_xmonad_workspaces(lua_State* L) {
    lua_newtable(L);
    if (!g_x11_display) return 1;

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(g_x11_display, g_x11_root, g_xmonad_log_atom,
            0, 4096, False, g_utf8_string_atom,
            &actual_type, &actual_format,
            &nitems, &bytes_after,
            &prop) == Success && prop) {
        std::string log(reinterpret_cast<char*>(prop), nitems);
        XFree(prop);

        int idx = 1;
        std::size_t pos = 0;
        while (pos < log.size()) {
            while (pos < log.size() && log[pos] == ' ')
                pos++;
            if (pos >= log.size()) break;
            std::size_t start = pos;
            while (pos < log.size() && log[pos] != ' ')
                pos++;
            lua_pushinteger(L, idx++);
            lua_pushlstring(L, log.c_str() + start, pos - start);
            lua_settable(L, -3);
        }
    }
    return 1;
}
#else
static int l_xmonad_workspaces_stub(lua_State* L) {
    lua_newtable(L);
    return 1;
}
#endif

void lua_plugin_register_native_functions(lua_State* L) {
#if defined(BUILD_XORG)
    lua_pushcfunction(L, l_xmonad_workspaces);
#else
    lua_pushcfunction(L, l_xmonad_workspaces_stub);
#endif
    lua_setglobal(L, "get_xmonad_workspaces");
}

int lua_plugin_init(LuaPlugin* p, const char* path) {
    p->type = PLUGIN_LUA;
    auto* L = luaL_newstate();
    if (!L) return -1;
    p->state = L;
    luaL_openlibs(L);
    lua_plugin_register_native_functions(L);

    std::snprintf(p->path, sizeof(p->path), "%s", path);
    p->cmd[0] = '\0';
    p->output[0] = '\0';
    p->interval = 5;

    // Use steady clock for interval tracking
    p->last_check = static_cast<time_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            clock_type::now().time_since_epoch()).count());

    if (luaL_dofile(L, path) != LUA_OK) {
        std::fprintf(stderr, "lua: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }

    lua_getglobal(L, "interval");
    if (lua_isinteger(L, -1))
        p->interval = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);

    return 0;
}

int lua_plugin_init_shell(LuaPlugin* p, const char* cmd, int interval_sec) {
    p->type = PLUGIN_SHELL;
    p->state = nullptr;
    p->path[0] = '\0';
    std::snprintf(p->cmd, sizeof(p->cmd), "%s", cmd);
    p->output[0] = '\0';
    p->interval = interval_sec > 0 ? interval_sec : 5;

    p->last_check = static_cast<time_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            clock_type::now().time_since_epoch()).count());
    return 0;
}

void lua_plugin_destroy(LuaPlugin* p) {
    if (p->type == PLUGIN_LUA && p->state) {
        lua_close(static_cast<lua_State*>(p->state));
        p->state = nullptr;
    }
}

bool lua_plugin_tick(LuaPlugin* p) {
    if (p->type == PLUGIN_SHELL) {
        if (!p->cmd[0]) return false;
        return do_tick_fork(p);
    }

    if (!p->state) return false;

    auto* L = static_cast<lua_State*>(p->state);
    lua_getglobal(L, "tick");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    lua_pop(L, 1);

    return do_tick_fork(p);
}

const char* lua_plugin_get_tooltip(LuaPlugin* p) {
    if (p->type == PLUGIN_SHELL || !p->state) return nullptr;
    static std::array<char, 512> buf{};

    auto* L = static_cast<lua_State*>(p->state);
    lua_getglobal(L, "on_tooltip");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
            lua_pop(L, 1);
            return nullptr;
        }
        if (lua_isstring(L, -1)) {
            const char* s = lua_tostring(L, -1);
            if (s) {
                std::snprintf(buf.data(), buf.size(), "%s", s);
                lua_pop(L, 1);
                return buf.data();
            }
        }
        lua_pop(L, 1);
    } else {
        lua_pop(L, 1);
    }
    return nullptr;
}

void lua_plugin_call_onclick(LuaPlugin* p) {
    if (p->type == PLUGIN_SHELL || !p->state) return;

    auto* L = static_cast<lua_State*>(p->state);
    lua_getglobal(L, "on_click");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK)
            std::fprintf(stderr, "lua on_click error: %s\n", lua_tostring(L, -1));
    }
    lua_pop(L, 1);
}

void lua_plugin_call_onscroll(LuaPlugin* p, int direction) {
    if (p->type == PLUGIN_SHELL || !p->state) return;

    auto* L = static_cast<lua_State*>(p->state);
    lua_getglobal(L, "on_scroll");
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, direction);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            std::fprintf(stderr, "lua on_scroll error: %s\n", lua_tostring(L, -1));
    }
    lua_pop(L, 1);
}
