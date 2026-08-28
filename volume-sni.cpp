// volume-sni: a Wayland-native volume control that registers as a
// StatusNotifierItem with the session's StatusNotifierWatcher. It integrates
// with orbit-status's SNI tray (and any other SNI host) without needing X11 /
// XEmbed, unlike the abandoned volumeicon.
//
// Interactions:
//   left click  -> toggle mute
//   scroll      -> change volume (via the tray's Scroll method)
//   right click -> open pavucontrol if available
//
// Volume is controlled through pactl (PulseAudio/PipeWire).
#include <cairo.h>
#include <dbus/dbus.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#define WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define WATCHER_PATH "/StatusNotifierWatcher"
#define WATCHER_IFACE "org.kde.StatusNotifierWatcher"
#define ITEM_IFACE "org.kde.StatusNotifierItem"
#define PROPS_IFACE "org.freedesktop.DBus.Properties"
#define ITEM_PATH "/StatusNotifierItem"

static DBusConnection *conn = nullptr;
static int current_volume = 0;   // 0..100
static bool current_muted = false;

/* ------------------------------------------------------------------ */
/* Volume control via pactl                                           */
/* ------------------------------------------------------------------ */

static void run_cmd(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)nullptr);
        _exit(127);
    }
}

// Read the first line of a command's stdout into buf (truncated).
static void read_cmd(const char *cmd, char *buf, size_t bufsz) {
    buf[0] = '\0';
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    if (fgets(buf, (int)bufsz, fp) == nullptr) buf[0] = '\0';
    pclose(fp);
    // Strip trailing newline.
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
}

static void refresh_state(void) {
    char buf[128];

    // Volume: pactl get-sink-volume prints "Volume: front-left: 72085 / 110% / ..."
    read_cmd("pactl get-sink-volume @DEFAULT_SINK@", buf, sizeof(buf));
    const char *pct = strstr(buf, "/");
    if (pct) {
        pct++;  // skip '/'
        while (*pct == ' ') pct++;
        int v = atoi(pct);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        current_volume = v;
    }

    // Mute: pactl get-sink-mute prints "Mute: yes" or "Mute: no".
    read_cmd("pactl get-sink-mute @DEFAULT_SINK@", buf, sizeof(buf));
    current_muted = (strstr(buf, "yes") != nullptr);
}

static const char *icon_for_state(void) {
    if (current_muted || current_volume == 0) return "audio-volume-muted";
    if (current_volume < 34) return "audio-volume-low";
    if (current_volume < 67) return "audio-volume-medium";
    return "audio-volume-high";
}

static void emit_new_icon(void) {
    DBusMessage *sig = dbus_message_new_signal(ITEM_PATH, ITEM_IFACE, "NewIcon");
    if (!sig) return;
    dbus_connection_send(conn, sig, nullptr);
    dbus_message_unref(sig);
    dbus_connection_flush(conn);
}

static void set_volume(int v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %d%%", v);
    run_cmd(cmd);
    current_volume = v;
    emit_new_icon();
}

static void toggle_mute(void) {
    run_cmd("pactl set-sink-mute @DEFAULT_SINK@ toggle");
    current_muted = !current_muted;
    emit_new_icon();
}

/* ------------------------------------------------------------------ */
/* DBus message handling                                              */
/* ------------------------------------------------------------------ */

static void append_variant_string(DBusMessageIter *iter, const char *s) {
    DBusMessageIter sub;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &sub);
    dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &s);
    dbus_message_iter_close_container(iter, &sub);
}

static void append_variant_bool(DBusMessageIter *iter, int b) {
    DBusMessageIter sub;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "b", &sub);
    dbus_bool_t v = b ? TRUE : FALSE;
    dbus_message_iter_append_basic(&sub, DBUS_TYPE_BOOLEAN, &v);
    dbus_message_iter_close_container(iter, &sub);
}

// Handle a Properties.Get call for a single property.
static void handle_get(DBusMessage *msg, DBusMessage *reply) {
    DBusMessageIter it;
    dbus_message_iter_init(msg, &it);
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) return;
    const char *iface;
    dbus_message_iter_get_basic(&it, &iface);
    dbus_message_iter_next(&it);
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) return;
    const char *prop;
    dbus_message_iter_get_basic(&it, &prop);
    if (!iface || !prop || strcmp(iface, ITEM_IFACE) != 0) return;

    DBusMessageIter out;
    dbus_message_iter_init_append(reply, &out);

    if (strcmp(prop, "Category") == 0) {
        append_variant_string(&out, "Hardware");
    } else if (strcmp(prop, "Id") == 0) {
        append_variant_string(&out, "volume");
    } else if (strcmp(prop, "Title") == 0) {
        append_variant_string(&out, "Volume");
    } else if (strcmp(prop, "Status") == 0) {
        append_variant_string(&out, "Active");
    } else if (strcmp(prop, "IconName") == 0) {
        append_variant_string(&out, icon_for_state());
    } else if (strcmp(prop, "ItemIsMenu") == 0) {
        append_variant_bool(&out, 0);
    } else if (strcmp(prop, "IconThemePath") == 0) {
        append_variant_string(&out, "");
    }
}

// Handle a Properties.GetAll call.
static void handle_get_all(DBusMessage *msg, DBusMessage *reply) {
    DBusMessageIter it;
    dbus_message_iter_init(msg, &it);
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING) return;
    const char *iface;
    dbus_message_iter_get_basic(&it, &iface);
    if (!iface || strcmp(iface, ITEM_IFACE) != 0) return;

    DBusMessageIter out, arr;
    dbus_message_iter_init_append(reply, &out);
    dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "{sv}", &arr);

    const char *cat = "Hardware";
    const char *id = "volume";
    const char *title = "Volume";
    const char *status = "Active";
    const char *icon = icon_for_state();
    dbus_bool_t is_menu = FALSE;

    struct { const char *k; const char *v; } strs[] = {
        {"Category", cat}, {"Id", id}, {"Title", title},
        {"Status", status}, {"IconName", icon},
    };
    for (auto &e : strs) {
        DBusMessageIter entry, var;
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &e.k);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &e.v);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&arr, &entry);
    }
    {
        DBusMessageIter entry, var;
        dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        const char *k = "ItemIsMenu";
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &is_menu);
        dbus_message_iter_close_container(&entry, &var);
        dbus_message_iter_close_container(&arr, &entry);
    }

    dbus_message_iter_close_container(&out, &arr);
}

static void handle_activate(void) {
    toggle_mute();
}

static void handle_secondary_activate(void) {
    toggle_mute();
}

static void handle_context_menu(void) {
    // Open pavucontrol if available.
    run_cmd("pavucontrol &");
}

static void handle_scroll(DBusMessage *msg) {
    DBusMessageIter it;
    dbus_message_iter_init(msg, &it);
    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_INT32) return;
    int32_t delta;
    dbus_message_iter_get_basic(&it, &delta);
    // Positive delta = scroll up = increase volume.
    set_volume(current_volume + (delta > 0 ? 5 : -5));
}

static DBusHandlerResult message_filter(DBusConnection *c, DBusMessage *msg,
                                        void *data) {
    (void)c; (void)data;
    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    const char *iface = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);
    const char *path = dbus_message_get_path(msg);
    if (!path || strcmp(path, ITEM_PATH) != 0)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    DBusMessage *reply = nullptr;

    if (iface && strcmp(iface, ITEM_IFACE) == 0) {
        if (member && strcmp(member, "Activate") == 0) {
            handle_activate();
            reply = dbus_message_new_method_return(msg);
        } else if (member && strcmp(member, "SecondaryActivate") == 0) {
            handle_secondary_activate();
            reply = dbus_message_new_method_return(msg);
        } else if (member && strcmp(member, "ContextMenu") == 0) {
            handle_context_menu();
            reply = dbus_message_new_method_return(msg);
        } else if (member && strcmp(member, "Scroll") == 0) {
            handle_scroll(msg);
            reply = dbus_message_new_method_return(msg);
        }
    } else if (iface && strcmp(iface, PROPS_IFACE) == 0) {
        if (member && strcmp(member, "Get") == 0) {
            reply = dbus_message_new_method_return(msg);
            handle_get(msg, reply);
        } else if (member && strcmp(member, "GetAll") == 0) {
            reply = dbus_message_new_method_return(msg);
            handle_get_all(msg, reply);
        }
    } else if (iface && strcmp(iface, "org.freedesktop.DBus.Introspectable") == 0 &&
               member && strcmp(member, "Introspect") == 0) {
        reply = dbus_message_new_method_return(msg);
        const char *xml =
            "<node>"
            "  <interface name='org.kde.StatusNotifierItem'>"
            "    <method name='Activate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
            "    <method name='SecondaryActivate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
            "    <method name='ContextMenu'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
            "    <method name='Scroll'><arg type='i' direction='in'/><arg type='s' direction='in'/></method>"
            "    <property name='Category' type='s' access='read'/>"
            "    <property name='Id' type='s' access='read'/>"
            "    <property name='Title' type='s' access='read'/>"
            "    <property name='Status' type='s' access='read'/>"
            "    <property name='IconName' type='s' access='read'/>"
            "    <property name='ItemIsMenu' type='b' access='read'/>"
            "    <signal name='NewIcon'/>"
            "  </interface>"
            "  <interface name='org.freedesktop.DBus.Properties'>"
            "    <method name='Get'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method>"
            "    <method name='GetAll'><arg type='s' direction='in'/><arg type='a{sv}' direction='out'/></method>"
            "  </interface>"
            "</node>";
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
    }

    if (reply) {
        dbus_connection_send(c, reply, nullptr);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void) {
    DBusError err;
    dbus_error_init(&err);

    conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    if (!conn) {
        fprintf(stderr, "volume-sni: failed to connect to session bus: %s\n",
                err.message ? err.message : "unknown");
        dbus_error_free(&err);
        return 1;
    }
    dbus_connection_set_exit_on_disconnect(conn, false);

    if (!dbus_connection_add_filter(conn, message_filter, nullptr, nullptr)) {
        fprintf(stderr, "volume-sni: failed to add dbus filter\n");
        return 1;
    }

    // Register the item with the watcher using our unique bus name.
    const char *unique = dbus_bus_get_unique_name(conn);
    if (!unique) {
        fprintf(stderr, "volume-sni: could not get unique bus name\n");
        return 1;
    }

    DBusMessage *reg = dbus_message_new_method_call(WATCHER_NAME, WATCHER_PATH,
        WATCHER_IFACE, "RegisterStatusNotifierItem");
    if (!reg) return 1;
    dbus_message_append_args(reg, DBUS_TYPE_STRING, &unique, DBUS_TYPE_INVALID);
    // Fire-and-forget: the watcher registers the item even if we don't wait for
    // the reply, and blocking here can time out depending on the watcher's
    // dispatch timing. The item is confirmed via the watcher's registered list.
    dbus_connection_send(conn, reg, nullptr);
    dbus_connection_flush(conn);
    dbus_message_unref(reg);

    refresh_state();
    fprintf(stderr, "volume-sni: registered, volume=%d%% muted=%d\n",
            current_volume, current_muted);

    // Main loop: dispatch DBus messages as they arrive.
    while (true) {
        if (!dbus_connection_read_write_dispatch(conn, 200)) {
            fprintf(stderr, "volume-sni: dbus connection lost\n");
            break;
        }
    }

    dbus_connection_remove_filter(conn, message_filter, nullptr);
    dbus_connection_close(conn);
    dbus_connection_unref(conn);
    return 0;
}
