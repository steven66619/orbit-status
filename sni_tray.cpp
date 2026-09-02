#include "sni_tray.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#include <librsvg/rsvg.h>

#define SNI_WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH "/StatusNotifierWatcher"
#define SNI_WATCHER_IFACE "org.kde.StatusNotifierWatcher"
#define SNI_ITEM_IFACE "org.kde.StatusNotifierItem"
#define SNI_HOST_IFACE "org.kde.StatusNotifierHost"
#define PROPS_IFACE "org.freedesktop.DBus.Properties"
#define DBUS_IFACE "org.freedesktop.DBus"

/* ------------------------------------------------------------------ */
/* Icon theme lookup                                                  */
/* ------------------------------------------------------------------ */

// Recursively search a theme base dir for <name>.png or <name>.svg.
// Returns true and fills out_path if found.
static bool find_icon_in_dir(const char *base, const char *name, char *out, size_t outsz) {
    DIR *d = opendir(base);
    if (!d) return false;

    bool found = false;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        char sub[1024];
        snprintf(sub, sizeof(sub), "%s/%s", base, e->d_name);

        struct stat st;
        if (stat(sub, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (find_icon_in_dir(sub, name, out, outsz)) {
                found = true;
                break;
            }
        } else if (S_ISREG(st.st_mode)) {
            // Match <name>.png or <name>.svg (case-insensitive on the name).
            const char *dot = strrchr(e->d_name, '.');
            if (!dot) continue;
            size_t namelen = dot - e->d_name;
            if (namelen != strlen(name)) continue;
            if (strncasecmp(e->d_name, name, namelen) != 0) continue;
            if (strcmp(dot, ".png") != 0 && strcmp(dot, ".svg") != 0) continue;
            snprintf(out, outsz, "%s", sub);
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

static bool find_icon_file(const char *name, const char *theme_path,
                           char *out, size_t outsz) {
    if (!name || !name[0]) return false;

    std::vector<std::string> bases;

    const char *home = getenv("HOME");
    if (home) {
        bases.push_back(std::string(home) + "/.local/share/icons");
        bases.push_back(std::string(home) + "/.icons");
    }
    bases.push_back("/usr/share/icons");
    bases.push_back("/usr/local/share/icons");

    // If the item provides a custom theme path, search it first.
    if (theme_path && theme_path[0])
        bases.insert(bases.begin(), theme_path);

    // Common theme names to try (in priority order).
    static const char *themes[] = {
        "Adwaita", "AdwaitaLegacy", "hicolor", "elementary", "Papirus", "breeze",
        "gnome", "ubuntu-mono-dark", "ubuntu-mono-light", nullptr
    };

    for (const auto &base : bases) {
        for (int t = 0; themes[t]; t++) {
            char dir[1024];
            snprintf(dir, sizeof(dir), "%s/%s", base.c_str(), themes[t]);
            if (find_icon_in_dir(dir, name, out, outsz))
                return true;
        }
        // Also search the base dir itself (hicolor-style flat layouts).
        if (find_icon_in_dir(base.c_str(), name, out, outsz))
            return true;
    }
    return false;
}

// Load an icon file (PNG or SVG) into a cairo surface at the requested size.
static cairo_surface_t *load_icon_file(const char *path, int size) {
    if (!path || !path[0]) return nullptr;

    const char *dot = strrchr(path, '.');
    bool is_svg = dot && strcasecmp(dot, ".svg") == 0;

    if (is_svg) {
        GError *err = nullptr;
        RsvgHandle *handle = rsvg_handle_new_from_file(path, &err);
        if (!handle) {
            if (err) g_error_free(err);
            return nullptr;
        }
        double w = 0, h = 0;
        if (!rsvg_handle_get_intrinsic_size_in_pixels(handle, &w, &h) ||
            w <= 0 || h <= 0) {
            g_object_unref(handle);
            return nullptr;
        }

        double scale = (double)size / (double)(w > h ? w : h);
        int iw = (int)(w * scale);
        int ih = (int)(h * scale);
        if (iw < 1) iw = 1;
        if (ih < 1) ih = 1;

        cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
        cairo_t *cr = cairo_create(surf);
        cairo_scale(cr, scale, scale);

        RsvgRectangle viewport = {0, 0, w, h};
        if (!rsvg_handle_render_document(handle, cr, &viewport, &err)) {
            if (err) g_error_free(err);
            cairo_destroy(cr);
            cairo_surface_destroy(surf);
            g_object_unref(handle);
            return nullptr;
        }
        cairo_destroy(cr);
        g_object_unref(handle);
        return surf;
    }

    // PNG (or other raster formats cairo can read).
    cairo_surface_t *img = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(img);
        return nullptr;
    }
    int iw = cairo_image_surface_get_width(img);
    int ih = cairo_image_surface_get_height(img);
    if (iw <= 0 || ih <= 0) {
        cairo_surface_destroy(img);
        return nullptr;
    }
    if (iw == size && ih == size)
        return img;

    // Scale to the requested size.
    cairo_surface_t *scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *cr = cairo_create(scaled);
    cairo_scale(cr, (double)size / iw, (double)size / ih);
    cairo_set_source_surface(cr, img, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(img);
    return scaled;
}

// Build a cairo surface from a raw ARGB32 pixmap (from IconPixmap).
static cairo_surface_t *surface_from_pixmap(const unsigned char *data,
                                            int w, int h, int size) {
    if (!data || w <= 0 || h <= 0) return nullptr;

    // The pixmap data is ARGB32, one byte per channel, in the order
    // A, R, G, B (network byte order / big-endian per the spec).
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(img);
        return nullptr;
    }
    unsigned char *dst = cairo_image_surface_get_data(img);
    int stride = cairo_image_surface_get_stride(img);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int si = (y * w + x) * 4;
            unsigned char a = data[si + 0];
            unsigned char r = data[si + 1];
            unsigned char g = data[si + 2];
            unsigned char b = data[si + 3];
            // Cairo ARGB32 is native-endian 0xAARRGGBB.
            uint32_t px = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                          ((uint32_t)g << 8) | (uint32_t)b;
            *(uint32_t *)(dst + y * stride + x * 4) = px;
        }
    }
    cairo_surface_mark_dirty(img);

    if (w == size && h == size)
        return img;

    cairo_surface_t *scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *cr = cairo_create(scaled);
    cairo_scale(cr, (double)size / w, (double)size / h);
    cairo_set_source_surface(cr, img, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(img);
    return scaled;
}

/* ------------------------------------------------------------------ */
/* DBus helpers                                                       */
/* ------------------------------------------------------------------ */

static DBusHandlerResult sni_filter(DBusConnection *conn, DBusMessage *msg, void *data);

static void emit_item_registered(SniTray *tray, const char *service) {
    DBusMessage *sig = dbus_message_new_signal(SNI_WATCHER_PATH,
        SNI_WATCHER_IFACE, "StatusNotifierItemRegistered");
    if (!sig) return;
    dbus_message_append_args(sig, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID);
    dbus_connection_send(tray->conn, sig, nullptr);
    dbus_message_unref(sig);
}

static void emit_item_unregistered(SniTray *tray, const char *service) {
    DBusMessage *sig = dbus_message_new_signal(SNI_WATCHER_PATH,
        SNI_WATCHER_IFACE, "StatusNotifierItemUnregistered");
    if (!sig) return;
    dbus_message_append_args(sig, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID);
    dbus_connection_send(tray->conn, sig, nullptr);
    dbus_message_unref(sig);
}

static void emit_host_registered(SniTray *tray) {
    DBusMessage *sig = dbus_message_new_signal(SNI_WATCHER_PATH,
        SNI_WATCHER_IFACE, "StatusNotifierHostRegistered");
    if (!sig) return;
    dbus_connection_send(tray->conn, sig, nullptr);
    dbus_message_unref(sig);
}

static void emit_properties_changed(SniTray *tray) {
    DBusMessage *sig = dbus_message_new_signal(SNI_WATCHER_PATH,
        PROPS_IFACE, "PropertiesChanged");
    if (!sig) return;
    // Signature: sa{sv}as  (interface, changed-properties dict, invalidated array)
    DBusMessageIter it, sub;
    dbus_message_iter_init_append(sig, &it);
    const char *iface = SNI_WATCHER_IFACE;
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &sub);
    dbus_message_iter_close_container(&it, &sub);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &sub);
    dbus_message_iter_close_container(&it, &sub);
    dbus_connection_send(tray->conn, sig, nullptr);
    dbus_message_unref(sig);
}

/* ------------------------------------------------------------------ */
/* Item property fetching (async)                                     */
/* ------------------------------------------------------------------ */

// Start an asynchronous Get() for a string property. The reply is processed
// later in sni_tray_dispatch via item_fetch_advance. Returns true if issued.
static bool start_string_prop(SniTray *tray, SniItem *item, const char *prop) {
    DBusMessage *call = dbus_message_new_method_call(item->service,
        item->object_path, PROPS_IFACE, "Get");
    if (!call) return false;
    const char *iface = SNI_ITEM_IFACE;
    dbus_message_append_args(call,
        DBUS_TYPE_STRING, &iface,
        DBUS_TYPE_STRING, &prop,
        DBUS_TYPE_INVALID);

    DBusPendingCall *pc = nullptr;
    if (!dbus_connection_send_with_reply(tray->conn, call, &pc, 1000)) {
        dbus_message_unref(call);
        return false;
    }
    dbus_message_unref(call);
    if (!pc) return false;
    item->pending = pc;
    return true;
}

// Start an asynchronous Get() for IconPixmap.
static bool start_icon_pixmap(SniTray *tray, SniItem *item) {
    DBusMessage *call = dbus_message_new_method_call(item->service,
        item->object_path, PROPS_IFACE, "Get");
    if (!call) return false;
    const char *iface = SNI_ITEM_IFACE;
    const char *prop = "IconPixmap";
    dbus_message_append_args(call,
        DBUS_TYPE_STRING, &iface,
        DBUS_TYPE_STRING, &prop,
        DBUS_TYPE_INVALID);

    DBusPendingCall *pc = nullptr;
    if (!dbus_connection_send_with_reply(tray->conn, call, &pc, 1000)) {
        dbus_message_unref(call);
        return false;
    }
    dbus_message_unref(call);
    if (!pc) return false;
    item->pending = pc;
    return true;
}

// Process a completed string-property reply into `out`.
static bool finish_string_prop(DBusPendingCall *pc, char *out, size_t outsz) {
    DBusMessage *reply = dbus_pending_call_steal_reply(pc);
    if (!reply) return false;
    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        dbus_message_unref(reply);
        return false;
    }
    bool ok = false;
    DBusMessageIter it, sub;
    dbus_message_iter_init(reply, &it);
    if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&it, &sub);
        if (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_STRING) {
            const char *val;
            dbus_message_iter_get_basic(&sub, &val);
            snprintf(out, outsz, "%s", val ? val : "");
            ok = true;
        }
    }
    dbus_message_unref(reply);
    return ok;
}

// Process a completed IconPixmap reply into a cairo surface.
static cairo_surface_t *finish_icon_pixmap(DBusPendingCall *pc, int icon_size) {
    DBusMessage *reply = dbus_pending_call_steal_reply(pc);
    if (!reply) return nullptr;
    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        dbus_message_unref(reply);
        return nullptr;
    }

    cairo_surface_t *result = nullptr;
    DBusMessageIter it, var, arr;
    dbus_message_iter_init(reply, &it);
    if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&it, &var);
        if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&var, &arr);
            // Iterate all pixmaps, keep the one closest to (but not over) icon_size.
            int best_w = 0, best_h = 0;
            const unsigned char *best_data = nullptr;
            while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT) {
                DBusMessageIter st, st2;
                dbus_message_iter_recurse(&arr, &st);
                int w = 0, h = 0;
                dbus_message_iter_get_basic(&st, &w);
                dbus_message_iter_next(&st);
                dbus_message_iter_get_basic(&st, &h);
                dbus_message_iter_next(&st);
                // Third member: array of bytes (ay).
                if (dbus_message_iter_get_arg_type(&st) == DBUS_TYPE_ARRAY) {
                    dbus_message_iter_recurse(&st, &st2);
                    int len = 0;
                    const unsigned char *data = nullptr;
                    dbus_message_iter_get_fixed_array(&st2, &data, &len);
                    if (data && len >= w * h * 4) {
                        bool better = (best_data == nullptr) ||
                            (w <= icon_size && w > best_w);
                        if (better) {
                            best_w = w;
                            best_h = h;
                            best_data = data;
                        }
                    }
                }
                dbus_message_iter_next(&arr);
            }
            if (best_data)
                result = surface_from_pixmap(best_data, best_w, best_h, icon_size);
        }
    }
    dbus_message_unref(reply);
    return result;
}

// Load the item's icon from a themed icon name (fallback when no pixmap).
static void load_icon_by_name(SniTray *tray, SniItem *item) {
    char path[1024];
    if (find_icon_file(item->icon_name, item->icon_theme_path, path, sizeof(path))) {
        item->icon = load_icon_file(path, tray->icon_size);
        if (item->icon)
            item->has_icon = true;
    }
}

// Advance the item's async fetch state machine. Called from sni_tray_dispatch
// when the item's pending call completes. Returns true if the item changed.
static bool item_fetch_advance(SniTray *tray, SniItem *item) {
    if (!item->pending) return false;

    DBusPendingCall *pc = item->pending;
    item->pending = nullptr;

    bool changed = false;
    switch (item->fetch_stage) {
        case SNI_FETCH_ID:
            finish_string_prop(pc, item->id, sizeof(item->id));
            item->fetch_stage = SNI_FETCH_TITLE;
            changed = true;
            break;
        case SNI_FETCH_TITLE:
            finish_string_prop(pc, item->title, sizeof(item->title));
            item->fetch_stage = SNI_FETCH_ICON_NAME;
            changed = true;
            break;
        case SNI_FETCH_ICON_NAME:
            finish_string_prop(pc, item->icon_name, sizeof(item->icon_name));
            item->fetch_stage = SNI_FETCH_ICON_THEME_PATH;
            changed = true;
            break;
        case SNI_FETCH_ICON_THEME_PATH:
            finish_string_prop(pc, item->icon_theme_path, sizeof(item->icon_theme_path));
            item->fetch_stage = SNI_FETCH_ICON_PIXMAP;
            changed = true;
            break;
        case SNI_FETCH_ICON_PIXMAP: {
            if (item->icon) {
                cairo_surface_destroy(item->icon);
                item->icon = nullptr;
                item->has_icon = false;
            }
            item->icon = finish_icon_pixmap(pc, tray->icon_size);
            if (item->icon) {
                item->has_icon = true;
            } else {
                load_icon_by_name(tray, item);
            }
            item->fetch_stage = SNI_FETCH_DONE;
            changed = true;
            break;
        }
        default:
            break;
    }
    dbus_pending_call_unref(pc);

    // Kick off the next stage.
    if (item->fetch_stage >= SNI_FETCH_ID && item->fetch_stage <= SNI_FETCH_ICON_PIXMAP) {
        const char *prop = nullptr;
        switch (item->fetch_stage) {
            case SNI_FETCH_ID: prop = "Id"; break;
            case SNI_FETCH_TITLE: prop = "Title"; break;
            case SNI_FETCH_ICON_NAME: prop = "IconName"; break;
            case SNI_FETCH_ICON_THEME_PATH: prop = "IconThemePath"; break;
            case SNI_FETCH_ICON_PIXMAP: break;
            default: break;
        }
        if (item->fetch_stage == SNI_FETCH_ICON_PIXMAP) {
            start_icon_pixmap(tray, item);
        } else if (prop) {
            start_string_prop(tray, item, prop);
        }
    }
    return changed;
}

// Begin the async property fetch for a newly-registered item.
static void item_fetch_props(SniTray *tray, SniItem *item) {
    item->fetch_stage = SNI_FETCH_ID;
    start_string_prop(tray, item, "Id");
}

// Refresh the item's icon asynchronously (used on NewIcon signals).
static void item_refresh_icon(SniTray *tray, SniItem *item) {
    if (item->pending) {
        dbus_pending_call_cancel(item->pending);
        dbus_pending_call_unref(item->pending);
        item->pending = nullptr;
    }
    if (item->icon) {
        cairo_surface_destroy(item->icon);
        item->icon = nullptr;
        item->has_icon = false;
    }
    item->fetch_stage = SNI_FETCH_ICON_PIXMAP;
    start_icon_pixmap(tray, item);
}

/* ------------------------------------------------------------------ */
/* Item management                                                    */
/* ------------------------------------------------------------------ */

// Defensive clamp of n_items to the array bound. A corrupted n_items must
// never cause an out-of-bounds read in the iteration loops below.
static int tray_item_count(const SniTray *tray) {
    if (!tray) return 0;
    int n = tray->n_items;
    if (n < 0) return 0;
    if (n > SNI_MAX_ITEMS) return SNI_MAX_ITEMS;
    return n;
}

static SniItem *find_item(SniTray *tray, const char *service) {
    for (int i = 0; i < tray->n_items; i++)
        if (strcmp(tray->items[i].service, service) == 0)
            return &tray->items[i];
    return nullptr;
}

static void remove_item(SniTray *tray, int idx) {
    if (idx < 0 || idx >= tray->n_items) return;
    SniItem *item = &tray->items[idx];
    if (item->pending) {
        dbus_pending_call_cancel(item->pending);
        dbus_pending_call_unref(item->pending);
        item->pending = nullptr;
    }
    if (item->icon) cairo_surface_destroy(item->icon);
    emit_item_unregistered(tray, item->service);
    tray->items[idx] = tray->items[--tray->n_items];
    if (tray->hovered_index >= tray->n_items)
        tray->hovered_index = -1;
    if (tray->on_change) tray->on_change(tray->userdata);
}

static void add_item(SniTray *tray, const char *service, const char *object_path) {
    if (tray->n_items >= SNI_MAX_ITEMS) return;
    if (find_item(tray, service)) return;

    SniItem *item = &tray->items[tray->n_items++];
    *item = SniItem{};
    snprintf(item->service, sizeof(item->service), "%s", service);
    snprintf(item->object_path, sizeof(item->object_path), "%s", object_path);

    item_fetch_props(tray, item);
    emit_item_registered(tray, service);
    if (tray->on_change) tray->on_change(tray->userdata);
}

/* ------------------------------------------------------------------ */
/* DBus message filter                                                */
/* ------------------------------------------------------------------ */

static void handle_register_item(SniTray *tray, DBusMessage *msg) {
    DBusMessageIter it;
    dbus_message_iter_init(msg, &it);
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) return;
    const char *service;
    dbus_message_iter_get_basic(&it, &service);
    if (!service) return;

    // The sender's unique bus name (e.g. ":1.42"). Some items (notably Steam)
    // register using the watcher's own well-known name as their service, which
    // would make our property queries resolve back to ourselves. In those cases
    // we must query the actual sender instead.
    const char *sender = dbus_message_get_sender(msg);

    if (service[0] == '/') {
        // Item is on the watcher's own well-known name (object path given).
        add_item(tray, sender ? sender : SNI_WATCHER_NAME, service);
    } else if (strcmp(service, SNI_WATCHER_NAME) == 0) {
        // Steam registers with the watcher's well-known name as its service.
        // Resolve to the sender's unique name so property queries reach Steam.
        add_item(tray, sender ? sender : SNI_WATCHER_NAME, "/StatusNotifierItem");
    } else {
        add_item(tray, service, "/StatusNotifierItem");
    }
}

static void handle_register_host(SniTray *tray) {
    if (!tray->host_registered) {
        tray->host_registered = true;
        emit_host_registered(tray);
        emit_properties_changed(tray);
    }
}

static void handle_get(SniTray *tray, DBusMessage *msg, DBusMessage *reply) {
    DBusMessageIter it;
    dbus_message_iter_init(msg, &it);
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) return;
    const char *iface;
    dbus_message_iter_get_basic(&it, &iface);
    dbus_message_iter_next(&it);
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) return;
    const char *prop;
    dbus_message_iter_get_basic(&it, &prop);

    DBusMessageIter r, v;
    dbus_message_iter_init_append(reply, &r);

    // The VARIANT container's signature depends on the property type.
    const char *var_sig = nullptr;
    if (strcmp(prop, "RegisteredStatusNotifierItems") == 0)
        var_sig = "as";
    else if (strcmp(prop, "IsStatusNotifierHostRegistered") == 0)
        var_sig = "b";
    else if (strcmp(prop, "ProtocolVersion") == 0)
        var_sig = "i";
    if (!var_sig) {
        // Unknown property: return an empty variant.
        var_sig = "s";
        dbus_message_iter_open_container(&r, DBUS_TYPE_VARIANT, var_sig, &v);
        const char *empty = "";
        dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &empty);
        dbus_message_iter_close_container(&r, &v);
        return;
    }
    dbus_message_iter_open_container(&r, DBUS_TYPE_VARIANT, var_sig, &v);

    if (strcmp(prop, "RegisteredStatusNotifierItems") == 0) {
        // The variant's content is an array of strings; open the array first.
        DBusMessageIter arr;
        dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "s", &arr);
        for (int i = 0; i < tray->n_items; i++) {
            const char *s = tray->items[i].service;
            dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &s);
        }
        dbus_message_iter_close_container(&v, &arr);
    } else if (strcmp(prop, "IsStatusNotifierHostRegistered") == 0) {
        dbus_bool_t b = tray->host_registered;
        dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &b);
    } else if (strcmp(prop, "ProtocolVersion") == 0) {
        int ver = 0;
        dbus_message_iter_append_basic(&v, DBUS_TYPE_INT32, &ver);
    }
    dbus_message_iter_close_container(&r, &v);
}

static void handle_get_all(SniTray *tray, DBusMessage *msg, DBusMessage *reply) {
    DBusMessageIter r, dict, entry, v;
    dbus_message_iter_init_append(reply, &r);
    dbus_message_iter_open_container(&r, DBUS_TYPE_ARRAY, "{sv}", &dict);

    // RegisteredStatusNotifierItems (as)
    {
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        const char *k = "RegisteredStatusNotifierItems";
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "as", &v);
        DBusMessageIter arr;
        dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "s", &arr);
        for (int i = 0; i < tray->n_items; i++) {
            const char *s = tray->items[i].service;
            dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &s);
        }
        dbus_message_iter_close_container(&v, &arr);
        dbus_message_iter_close_container(&entry, &v);
        dbus_message_iter_close_container(&dict, &entry);
    }
    // IsStatusNotifierHostRegistered (b)
    {
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        const char *k = "IsStatusNotifierHostRegistered";
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &v);
        dbus_bool_t b = tray->host_registered;
        dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &b);
        dbus_message_iter_close_container(&entry, &v);
        dbus_message_iter_close_container(&dict, &entry);
    }
    // ProtocolVersion (i)
    {
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        const char *k = "ProtocolVersion";
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &v);
        int ver = 0;
        dbus_message_iter_append_basic(&v, DBUS_TYPE_INT32, &ver);
        dbus_message_iter_close_container(&entry, &v);
        dbus_message_iter_close_container(&dict, &entry);
    }

    dbus_message_iter_close_container(&r, &dict);
}

static DBusHandlerResult sni_filter(DBusConnection *conn, DBusMessage *msg, void *data) {
    SniTray *tray = (SniTray *)data;

    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL) {
        const char *iface = dbus_message_get_interface(msg);
        const char *member = dbus_message_get_member(msg);

        if (iface && strcmp(iface, DBUS_IFACE) == 0 &&
            member && strcmp(member, "NameOwnerChanged") == 0) {
            DBusMessageIter it;
            dbus_message_iter_init(msg, &it);
            if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            const char *name, *old_owner, *new_owner;
            dbus_message_iter_get_basic(&it, &name);
            dbus_message_iter_next(&it);
            dbus_message_iter_get_basic(&it, &old_owner);
            dbus_message_iter_next(&it);
            dbus_message_iter_get_basic(&it, &new_owner);

            // If a registered item's owner disappears, remove it.
            if (new_owner && new_owner[0] == '\0') {
                for (int i = 0; i < tray->n_items; i++) {
                    if (strcmp(tray->items[i].service, name) == 0) {
                        remove_item(tray, i);
                        break;
                    }
                }
            }
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        // Item signals: refresh icon / tooltip.
        if (iface && strcmp(iface, SNI_ITEM_IFACE) == 0) {
            const char *sender = dbus_message_get_sender(msg);
            SniItem *item = sender ? find_item(tray, sender) : nullptr;
            if (item) {
                if (member && (strcmp(member, "NewIcon") == 0 ||
                               strcmp(member, "NewAttentionIcon") == 0 ||
                               strcmp(member, "NewOverlayIcon") == 0)) {
                    item_refresh_icon(tray, item);
                    if (tray->on_change) tray->on_change(tray->userdata);
                } else if (member && strcmp(member, "NewToolTip") == 0) {
                    // Re-fetch the Title asynchronously.
                    if (item->pending) {
                        dbus_pending_call_cancel(item->pending);
                        dbus_pending_call_unref(item->pending);
                        item->pending = nullptr;
                    }
                    item->fetch_stage = SNI_FETCH_TITLE;
                    start_string_prop(tray, item, "Title");
                    if (tray->on_change) tray->on_change(tray->userdata);
                }
            }
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    const char *iface = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);
    const char *path = dbus_message_get_path(msg);

    if (!path || strcmp(path, SNI_WATCHER_PATH) != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    DBusMessage *reply = nullptr;

    if (iface && strcmp(iface, SNI_WATCHER_IFACE) == 0) {
        if (member && strcmp(member, "RegisterStatusNotifierItem") == 0) {
            handle_register_item(tray, msg);
            reply = dbus_message_new_method_return(msg);
        } else if (member && strcmp(member, "RegisterStatusNotifierHost") == 0) {
            handle_register_host(tray);
            reply = dbus_message_new_method_return(msg);
        }
    } else if (iface && strcmp(iface, PROPS_IFACE) == 0) {
        if (member && strcmp(member, "Get") == 0) {
            reply = dbus_message_new_method_return(msg);
            handle_get(tray, msg, reply);
        } else if (member && strcmp(member, "GetAll") == 0) {
            reply = dbus_message_new_method_return(msg);
            handle_get_all(tray, msg, reply);
        }
    } else if (iface && strcmp(iface, "org.freedesktop.DBus.Introspectable") == 0 &&
               member && strcmp(member, "Introspect") == 0) {
        reply = dbus_message_new_method_return(msg);
        const char *xml =
            "<node>"
            "  <interface name='org.kde.StatusNotifierWatcher'>"
            "    <method name='RegisterStatusNotifierItem'><arg type='s' direction='in'/></method>"
            "    <method name='RegisterStatusNotifierHost'><arg type='s' direction='in'/></method>"
            "    <property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
            "    <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
            "    <property name='ProtocolVersion' type='i' access='read'/>"
            "    <signal name='StatusNotifierItemRegistered'><arg type='s'/></signal>"
            "    <signal name='StatusNotifierItemUnregistered'><arg type='s'/></signal>"
            "    <signal name='StatusNotifierHostRegistered'/>"
            "  </interface>"
            "  <interface name='org.freedesktop.DBus.Properties'>"
            "    <method name='Get'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method>"
            "    <method name='GetAll'><arg type='s' direction='in'/><arg type='a{sv}' direction='out'/></method>"
            "  </interface>"
            "</node>";
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
    }

    if (reply) {
        dbus_connection_send(conn, reply, nullptr);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult sni_filter_wrapper(DBusConnection *conn,
                                            DBusMessage *msg, void *data) {
    return sni_filter(conn, msg, data);
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int sni_tray_init(SniTray *tray, int icon_size,
                  void (*on_change)(void *userdata), void *userdata) {
    *tray = SniTray{};
    tray->icon_size = icon_size;
    tray->spacing = 6;
    tray->on_change = on_change;
    tray->userdata = userdata;

    DBusError err;
    dbus_error_init(&err);
    // Use a private connection: shared connections must not be closed, and we
    // close ours in sni_tray_destroy.
    tray->conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!tray->conn) {
        fprintf(stderr, "orbit-status: tray: failed to connect to session bus: %s\n",
                err.message ? err.message : "unknown");
        dbus_error_free(&err);
        return -1;
    }
    dbus_connection_set_exit_on_disconnect(tray->conn, false);

    // Own the watcher well-known name.
    int ret = dbus_bus_request_name(tray->conn, SNI_WATCHER_NAME,
        DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (ret == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        tray->watcher_owned = true;
    } else {
        fprintf(stderr, "orbit-status: tray: could not own %s (another watcher exists)\n",
                SNI_WATCHER_NAME);
        dbus_error_free(&err);
        // Still register as a host so items can find us via the existing watcher.
    }

    // Add the message filter.
    if (!dbus_connection_add_filter(tray->conn, sni_filter_wrapper, tray, nullptr)) {
        fprintf(stderr, "orbit-status: tray: failed to add dbus filter\n");
        return -1;
    }

    // Watch for name owner changes so we can drop vanished items.
    DBusMessage *match = dbus_message_new_method_call(DBUS_SERVICE_DBUS,
        DBUS_PATH_DBUS, DBUS_IFACE, "AddMatch");
    if (match) {
        const char *rule = "type='signal',interface='org.freedesktop.DBus',member='NameOwnerChanged'";
        dbus_message_append_args(match, DBUS_TYPE_STRING, &rule, DBUS_TYPE_INVALID);
        DBusMessage *mr = dbus_connection_send_with_reply_and_block(tray->conn, match, 1000, &err);
        if (mr) dbus_message_unref(mr);
        else dbus_error_free(&err);
        dbus_message_unref(match);
    }

    // Register ourselves as a StatusNotifierHost.
    if (tray->watcher_owned) {
        // We own the watcher name, so there is no remote watcher to notify.
        // Calling RegisterStatusNotifierHost on our own connection would
        // deadlock (the reply is handled by our filter, which never runs while
        // we block), so just mark ourselves registered and emit the signal.
        tray->host_registered = true;
        emit_host_registered(tray);
        emit_properties_changed(tray);
    } else {
        // Another process owns the watcher name; notify it that we are a host.
        DBusMessage *host = dbus_message_new_method_call(SNI_WATCHER_NAME,
            SNI_WATCHER_PATH, SNI_WATCHER_IFACE, "RegisterStatusNotifierHost");
        if (host) {
            const char *hostname = "org.kde.StatusNotifierHost-orbit-status";
            dbus_message_append_args(host, DBUS_TYPE_STRING, &hostname, DBUS_TYPE_INVALID);
            DBusMessage *hr = dbus_connection_send_with_reply_and_block(tray->conn, host, 1000, &err);
            if (hr) dbus_message_unref(hr);
            else dbus_error_free(&err);
            dbus_message_unref(host);
        }
        tray->host_registered = true;
    }

    // Flush pending messages.
    dbus_connection_flush(tray->conn);
    return 0;
}

int sni_tray_get_fd(SniTray *tray) {
    if (!tray || !tray->conn) return -1;
    int fd = -1;
    if (!dbus_connection_get_unix_fd(tray->conn, &fd))
        return -1;
    return fd;
}

// (Re)acquire the watcher well-known name. If the initial request in
// sni_tray_init lost a race with the previous owner's teardown (e.g. the bar
// was restarted immediately after being killed), the tray would otherwise stay
// name-less forever, showing no icons for the rest of the session. Called from
// sni_tray_dispatch whenever we detect we do not own the name yet.
static void try_acquire_watcher(SniTray *tray) {
    if (!tray->conn || tray->watcher_owned) return;
    DBusError err;
    dbus_error_init(&err);
    int ret = dbus_bus_request_name(tray->conn, SNI_WATCHER_NAME,
        DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (ret == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        tray->watcher_owned = true;
        tray->host_registered = true;
        emit_host_registered(tray);
        emit_properties_changed(tray);
        fprintf(stderr, "orbit-status: tray: acquired watcher name\n");
    }
    dbus_error_free(&err);
}

void sni_tray_dispatch(SniTray *tray) {
    if (!tray || !tray->conn) return;
    // Read from the fd (non-blocking, 0 timeout) and dispatch all pending
    // messages. Returns immediately if nothing is available.
    dbus_connection_read_write_dispatch(tray->conn, 0);

    // If we lost the initial watcher-name request race, retry periodically
    // (rate-limited). The NameOwnerChanged signal wakes this up right when the
    // previous owner's name is actually released.
    if (!tray->watcher_owned && tray->ownership_retries++ % 50 == 0)
        try_acquire_watcher(tray);

    // Advance any completed async property fetches. This must run after
    // dispatch so that replies are available, and it must not re-enter
    // dispatch (the fetch handlers only parse replies and issue new sends).
    bool changed = false;
    int n = tray_item_count(tray);
    for (int i = 0; i < n; i++) {
        SniItem *item = &tray->items[i];
        if (item->pending && dbus_pending_call_get_completed(item->pending)) {
            if (item_fetch_advance(tray, item))
                changed = true;
        }
    }
    if (changed && tray->on_change)
        tray->on_change(tray->userdata);
}

int sni_tray_width(SniTray *tray) {
    if (!tray) return 0;
    int n = tray_item_count(tray);
    if (n == 0) return 0;
    return n * tray->icon_size + (n - 1) * tray->spacing;
}

int sni_tray_render(SniTray *tray, cairo_t *cr, int bar_height, int right_x) {
    if (!tray || !cr) return right_x;

    int x = right_x;
    int y = (bar_height - tray->icon_size) / 2;

    int n = tray_item_count(tray);
    for (int i = 0; i < n; i++) {
        SniItem *item = &tray->items[i];
        x -= tray->icon_size;

        item->x = x;
        item->y = y;
        item->w = tray->icon_size;
        item->h = tray->icon_size;
        item->hovered = (i == tray->hovered_index);

        if (item->icon) {
            if (item->hovered) {
                cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, 0.25);
                cairo_rectangle(cr, x - 2, y - 2, tray->icon_size + 4, tray->icon_size + 4);
                cairo_fill(cr);
            }
            cairo_set_source_surface(cr, item->icon, x, y);
            cairo_paint(cr);
        } else {
            // Placeholder for items without an icon.
            cairo_set_source_rgba(cr, 0.5, 0.5, 0.6, 0.4);
            cairo_rectangle(cr, x + 4, y + 4, tray->icon_size - 8, tray->icon_size - 8);
            cairo_fill(cr);
        }

        x -= tray->spacing;
    }

    tray->width = sni_tray_width(tray);
    return x;
}

bool sni_tray_update_hover(SniTray *tray, int x, int y) {
    if (!tray) return false;
    int old = tray->hovered_index;
    tray->hovered_index = -1;
    // Clamp to the array bound defensively: a corrupted n_items must never
    // cause an out-of-bounds read here.
    int n = tray->n_items < 0 ? 0 : (tray->n_items > SNI_MAX_ITEMS ? SNI_MAX_ITEMS : tray->n_items);
    for (int i = 0; i < n; i++) {
        SniItem *item = &tray->items[i];
        if (x >= item->x && x < item->x + item->w &&
            y >= item->y && y < item->y + item->h) {
            tray->hovered_index = i;
            break;
        }
    }
    return old != tray->hovered_index;
}

static void send_item_method(SniTray *tray, SniItem *item, const char *method,
                             int x, int y) {
    DBusMessage *call = dbus_message_new_method_call(item->service,
        item->object_path, SNI_ITEM_IFACE, method);
    if (!call) return;
    dbus_message_append_args(call,
        DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32, &y, DBUS_TYPE_INVALID);
    dbus_connection_send(tray->conn, call, nullptr);
    dbus_message_unref(call);
}

bool sni_tray_handle_click(SniTray *tray, int x, int y, int button) {
    if (!tray) return false;
    int n = tray_item_count(tray);
    for (int i = 0; i < n; i++) {
        SniItem *item = &tray->items[i];
        if (x >= item->x && x < item->x + item->w &&
            y >= item->y && y < item->y + item->h) {
            if (button == 0x110)        // left
                send_item_method(tray, item, "Activate", x, y);
            else if (button == 0x111)   // middle
                send_item_method(tray, item, "SecondaryActivate", x, y);
            else if (button == 0x113)   // right
                send_item_method(tray, item, "ContextMenu", x, y);
            return true;
        }
    }
    return false;
}

int sni_tray_item_at(SniTray *tray, int x, int y) {
    if (!tray) return -1;
    int n = tray_item_count(tray);
    for (int i = 0; i < n; i++) {
        SniItem *item = &tray->items[i];
        if (x >= item->x && x < item->x + item->w &&
            y >= item->y && y < item->y + item->h)
            return i;
    }
    return -1;
}

bool sni_tray_handle_scroll(SniTray *tray, int x, int y, int delta) {
    if (!tray) return false;
    int n = tray_item_count(tray);
    for (int i = 0; i < n; i++) {
        SniItem *item = &tray->items[i];
        if (x >= item->x && x < item->x + item->w &&
            y >= item->y && y < item->y + item->h) {
            // Send the SNI Scroll method: Scroll(i delta, s orientation).
            DBusMessage *call = dbus_message_new_method_call(item->service,
                item->object_path, SNI_ITEM_IFACE, "Scroll");
            if (!call) return true;
            const char *orientation = "vertical";
            dbus_message_append_args(call,
                DBUS_TYPE_INT32, &delta,
                DBUS_TYPE_STRING, &orientation,
                DBUS_TYPE_INVALID);
            dbus_connection_send(tray->conn, call, nullptr);
            dbus_message_unref(call);
            return true;
        }
    }
    return false;
}

void sni_tray_destroy(SniTray *tray) {
    if (!tray) return;
    for (int i = 0; i < tray->n_items; i++) {
        if (tray->items[i].pending) {
            dbus_pending_call_cancel(tray->items[i].pending);
            dbus_pending_call_unref(tray->items[i].pending);
            tray->items[i].pending = nullptr;
        }
        if (tray->items[i].icon)
            cairo_surface_destroy(tray->items[i].icon);
    }
    tray->n_items = 0;
    if (tray->conn) {
        dbus_connection_remove_filter(tray->conn, sni_filter_wrapper, tray);
        dbus_connection_close(tray->conn);
        dbus_connection_unref(tray->conn);
        tray->conn = nullptr;
    }
}
