#ifndef SNI_TRAY_HPP
#define SNI_TRAY_HPP

#include <cairo.h>
#include <dbus/dbus.h>

#define SNI_MAX_ITEMS 16

// Asynchronous property-fetch stages for an item. Property fetches are issued
// with dbus_connection_send_with_reply (non-blocking) and completed in
// sni_tray_dispatch so a slow/unresponsive tray item never blocks the event loop.
enum SniFetchStage {
    SNI_FETCH_NONE = 0,
    SNI_FETCH_ID = 1,
    SNI_FETCH_TITLE = 2,
    SNI_FETCH_ICON_NAME = 3,
    SNI_FETCH_ICON_THEME_PATH = 4,
    SNI_FETCH_ICON_PIXMAP = 5,
    SNI_FETCH_DONE = 6,
};

// A single StatusNotifierItem as tracked by the tray.
struct SniItem {
    char service[256];      // bus name of the item (e.g. ":1.42")
    char object_path[256];  // object path of the item (e.g. "/StatusNotifierItem")
    char id[128];
    char title[256];
    char icon_name[256];
    char icon_theme_path[512];
    cairo_surface_t *icon = nullptr;  // rendered icon, already scaled to tray size
    int icon_w = 0, icon_h = 0;
    bool has_icon = false;

    // Async property-fetch state.
    DBusPendingCall *pending = nullptr;  // in-flight Get() call, if any
    SniFetchStage fetch_stage = SNI_FETCH_NONE;

    // Clickable region, set during render.
    int x = 0, y = 0, w = 0, h = 0;
    bool hovered = false;
};

struct SniTray {
    DBusConnection *conn = nullptr;
    bool watcher_owned = false;
    bool host_registered = false;

    SniItem items[SNI_MAX_ITEMS];
    int n_items = 0;

    int icon_size = 24;
    int spacing = 6;
    int width = 0;          // total width consumed by the tray (computed on render)
    int hovered_index = -1;

    // Called whenever the tray contents change so the caller can re-render.
    void (*on_change)(void *userdata) = nullptr;
    void *userdata = nullptr;
};

// Connect to the session bus, own the StatusNotifierWatcher name, and register
// as a StatusNotifierHost. Returns 0 on success, -1 on failure.
int sni_tray_init(SniTray *tray, int icon_size,
                  void (*on_change)(void *userdata), void *userdata);

// File descriptor to add to the caller's poll() loop.
int sni_tray_get_fd(SniTray *tray);

// Call when the DBus fd is readable (or periodically) to process messages.
void sni_tray_dispatch(SniTray *tray);

// Draw the tray icons right-aligned ending at `right_x` (the left edge of the
// power buttons). Returns the new right edge (moving left) after the tray.
int sni_tray_render(SniTray *tray, cairo_t *cr, int bar_height, int right_x);

// Update hover state for a pointer at (x, y). Returns true if hover changed.
bool sni_tray_update_hover(SniTray *tray, int x, int y);

// Handle a pointer button press over the tray. button is the Linux button
// code (0x110 = left, 0x111 = middle, 0x113 = right). Returns true if handled.
bool sni_tray_handle_click(SniTray *tray, int x, int y, int button);

// Total width of the tray (sum of icons + spacing).
int sni_tray_width(SniTray *tray);

// Clean up all DBus resources.
void sni_tray_destroy(SniTray *tray);

#endif
