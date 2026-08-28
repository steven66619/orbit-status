#include "sway_ipc.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#if defined(__FreeBSD__)
#include <sys/endian.h>
#else
#include <endian.h>
#endif

#define IPC_MAGIC "i3-ipc"
#define IPC_GET_WORKSPACES 1
#define IPC_GET_TREE 4

/* ------------------------------------------------------------------ */
/* Minimal JSON parser (objects, arrays, strings, numbers, bool, null) */
/* ------------------------------------------------------------------ */

struct JsonValue {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    const JsonValue *find(const char *key) const {
        if (type != OBJ) return nullptr;
        for (const auto &kv : obj)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static bool parse_string(const char *&p, std::string &out) {
    p = skip_ws(p);
    if (*p != '"') return false;
    p++;
    out.clear();
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case '"': out += '"'; break;
            case 'u': {
                char hex[5] = {p[1], p[2], p[3], p[4], 0};
                unsigned cp = (unsigned)strtoul(hex, nullptr, 16);
                if (cp < 0x80) {
                    out += (char)cp;
                } else if (cp < 0x800) {
                    out += (char)(0xC0 | (cp >> 6));
                    out += (char)(0x80 | (cp & 0x3F));
                } else {
                    out += (char)(0xE0 | (cp >> 12));
                    out += (char)(0x80 | ((cp >> 6) & 0x3F));
                    out += (char)(0x80 | (cp & 0x3F));
                }
                p += 4;
                break;
            }
            default: out += *p; break;
            }
            p++;
        } else {
            out += *p++;
        }
    }
    if (*p != '"') return false;
    p++;
    return true;
}

static bool parse_value(const char *&p, JsonValue &v);

static bool parse_array(const char *&p, JsonValue &v) {
    p = skip_ws(p);
    if (*p != '[') return false;
    p++;
    v.type = JsonValue::ARR;
    p = skip_ws(p);
    if (*p == ']') { p++; return true; }
    for (;;) {
        JsonValue item;
        if (!parse_value(p, item)) return false;
        v.arr.push_back(std::move(item));
        p = skip_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') { p++; return true; }
        return false;
    }
}

static bool parse_object(const char *&p, JsonValue &v) {
    p = skip_ws(p);
    if (*p != '{') return false;
    p++;
    v.type = JsonValue::OBJ;
    p = skip_ws(p);
    if (*p == '}') { p++; return true; }
    for (;;) {
        std::string key;
        if (!parse_string(p, key)) return false;
        p = skip_ws(p);
        if (*p != ':') return false;
        p++;
        JsonValue val;
        if (!parse_value(p, val)) return false;
        v.obj.emplace_back(std::move(key), std::move(val));
        p = skip_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') { p++; return true; }
        return false;
    }
}

static bool parse_value(const char *&p, JsonValue &v) {
    p = skip_ws(p);
    if (*p == '{') return parse_object(p, v);
    if (*p == '[') return parse_array(p, v);
    if (*p == '"') {
        if (!parse_string(p, v.str)) return false;
        v.type = JsonValue::STR;
        return true;
    }
    if (strncmp(p, "true", 4) == 0) { v.type = JsonValue::BOOL; v.b = true; p += 4; return true; }
    if (strncmp(p, "false", 5) == 0) { v.type = JsonValue::BOOL; v.b = false; p += 5; return true; }
    if (strncmp(p, "null", 4) == 0) { v.type = JsonValue::NUL; p += 4; return true; }
    char *end = nullptr;
    double d = strtod(p, &end);
    if (end == p) return false;
    v.type = JsonValue::NUM;
    v.num = d;
    p = end;
    return true;
}

/* ------------------------------------------------------------------ */
/* IPC transport                                                       */
/* ------------------------------------------------------------------ */

// Persistent connection to the sway/i3 IPC socket. Opened lazily on first
// use and reused across ticks; reset to -1 if the connection drops so the
// next call reconnects (e.g. after sway restarts).
static int sway_ipc_fd = -1;

static int sway_ipc_connect() {
    if (sway_ipc_fd >= 0) return sway_ipc_fd;

    const char *sock = getenv("SWAYSOCK");
    if (!sock) sock = getenv("I3SOCK");
    if (!sock) return -1;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    sway_ipc_fd = fd;
    return fd;
}

// Close the persistent connection (e.g. on shutdown).
void sway_ipc_disconnect() {
    if (sway_ipc_fd >= 0) {
        close(sway_ipc_fd);
        sway_ipc_fd = -1;
    }
}

static int write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t len) {
    char *p = (char *)buf;
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int ipc_send_recv(int fd, uint32_t type, const char *payload,
                         size_t len, std::string &reply) {
    uint8_t hdr[14];
    memcpy(hdr, IPC_MAGIC, 6);
    uint32_t l = htole32((uint32_t)len);
    uint32_t t = htole32(type);
    memcpy(hdr + 6, &l, 4);
    memcpy(hdr + 10, &t, 4);

    if (write_all(fd, hdr, sizeof(hdr)) < 0) return -1;
    if (len > 0 && write_all(fd, payload, len) < 0) return -1;

    uint8_t rhdr[14];
    if (read_all(fd, rhdr, sizeof(rhdr)) < 0) return -1;
    uint32_t rlen = le32toh(*(const uint32_t *)(rhdr + 6));
    uint32_t rtype = le32toh(*(const uint32_t *)(rhdr + 10));
    if (rtype != type) return -1;

    reply.resize(rlen);
    if (rlen > 0 && read_all(fd, &reply[0], rlen) < 0) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int sway_ipc_update_workspaces(Bar *bar) {
    if (!bar) return -1;

    int fd = sway_ipc_connect();
    if (fd < 0) return -1;

    std::string reply;
    int rc = ipc_send_recv(fd, IPC_GET_WORKSPACES, nullptr, 0, reply);
    if (rc < 0) { sway_ipc_disconnect(); return -1; }

    JsonValue root;
    const char *p = reply.c_str();
    if (!parse_value(p, root) || root.type != JsonValue::ARR) return -1;

    Workspace ws[MAX_WORKSPACES];
    int n = 0;
    for (const auto &item : root.arr) {
        if (n >= MAX_WORKSPACES) break;
        const JsonValue *num = item.find("num");
        const JsonValue *name = item.find("name");
        const JsonValue *focused = item.find("focused");
        if (!num || num->type != JsonValue::NUM) continue;

        ws[n].id = (int)num->num;
        ws[n].active = focused && focused->type == JsonValue::BOOL && focused->b;
        ws[n].name[0] = '\0';
        if (name && name->type == JsonValue::STR)
            snprintf(ws[n].name, sizeof(ws[n].name), "%s", name->str.c_str());
        n++;
    }
    bar_set_workspaces(bar, ws, n);
    return 0;
}

static bool tree_find_focused(const JsonValue &node, std::string &app_id,
                              std::string &cls, std::string &title) {
    if (node.type != JsonValue::OBJ) return false;

    const JsonValue *focused = node.find("focused");
    if (focused && focused->type == JsonValue::BOOL && focused->b) {
        const JsonValue *app = node.find("app_id");
        if (app && app->type == JsonValue::STR) app_id = app->str;
        const JsonValue *name = node.find("name");
        if (name && name->type == JsonValue::STR) title = name->str;
        const JsonValue *props = node.find("window_properties");
        if (props && props->type == JsonValue::OBJ) {
            const JsonValue *c = props->find("class");
            if (c && c->type == JsonValue::STR) cls = c->str;
        }
        return true;
    }

    const JsonValue *nodes = node.find("nodes");
    if (nodes && nodes->type == JsonValue::ARR) {
        for (const auto &child : nodes->arr)
            if (tree_find_focused(child, app_id, cls, title)) return true;
    }
    const JsonValue *floating = node.find("floating_nodes");
    if (floating && floating->type == JsonValue::ARR) {
        for (const auto &child : floating->arr)
            if (tree_find_focused(child, app_id, cls, title)) return true;
    }
    return false;
}

int sway_ipc_update_active_window(Bar *bar) {
    if (!bar) return -1;

    int fd = sway_ipc_connect();
    if (fd < 0) return -1;

    std::string reply;
    int rc = ipc_send_recv(fd, IPC_GET_TREE, nullptr, 0, reply);
    if (rc < 0) { sway_ipc_disconnect(); return -1; }

    JsonValue root;
    const char *p = reply.c_str();
    if (!parse_value(p, root)) return -1;

    std::string app_id, cls, title;
    if (!tree_find_focused(root, app_id, cls, title)) {
        bar_set_active_window(bar, nullptr, nullptr);
        return 0;
    }

    const char *use_cls = cls.empty() ? (app_id.empty() ? nullptr : app_id.c_str()) : cls.c_str();
    const char *use_title = title.empty() ? nullptr : title.c_str();
    bar_set_active_window(bar, use_cls, use_title);
    return 0;
}