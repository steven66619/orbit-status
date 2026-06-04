#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ctime>
#include <poll.h>
#include <sys/timerfd.h>
#include <sys/inotify.h>
#include <csignal>
#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <cctype>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/Xcomposite.h>
#include <cairo.h>
#include <cairo-xlib.h>
#include <pango/pangocairo.h>
#include "bar.hpp"
#include "config.hpp"

#define SYSTEM_TRAY_REQUEST_DOCK 0
#define SYSTEM_TRAY_BEGIN_MESSAGE 1
#define SYSTEM_TRAY_CANCEL_MESSAGE 2
#define XEMBED_EMBEDDED_NOTIFY 0
#define XEMBED_WINDOW_ACTIVATE 1
#define XEMBED_FOCUS_IN 4
#define XEMBED_MAPPED 1 << 0

#define MAX_TRAY_CLIENTS 16

struct TrayClient {
    Window win;
    int x, y, w, h;
    bool mapped;
    Pixmap pixmap;
    cairo_surface_t *surface;
};

struct OrbitStatus {
    Display *dpy = nullptr;
    Window win = 0;
    int screen = 0;
    GC gc = 0;
    int width = 0, height = 0;

    cairo_surface_t *cairo_surface = nullptr;
    cairo_t *cr = nullptr;

    Bar *bar = nullptr;
    Config *cfg = nullptr;
    bool running = false;

    int timer_fd = -1;
    int inotify_fd = -1;

    int pointer_x = 0, pointer_y = 0;

    Atom tray_selection;
    Atom tray_opcode;
    Atom tray_dock_request;
    Atom xembed_info;
    Atom manager_atom;
    Window tray_win = 0;
    TrayClient tray_clients[MAX_TRAY_CLIENTS];
    int n_tray_clients = 0;
    int tray_icon_size = 24;

    int power_hovered = -1;
    int hovered_workspace = -1;

    struct {
        Window win = 0;
        int width = 0, height = 0;
        bool visible = false;
        char text[512]{};
        int hovered_clickable = -1;
    } tooltip;

    struct {
        Window win = 0;
        int width = 0, height = 0;
        bool visible = false;
        int action = 0;
        int hovered_btn = -1;
        int confirm_btn_x = 0, confirm_btn_y = 0, confirm_btn_w = 0, confirm_btn_h = 0;
        int cancel_btn_x = 0, cancel_btn_y = 0, cancel_btn_w = 0, cancel_btn_h = 0;
    } popup;

    Pixmap back_pixmap = 0;
    cairo_surface_t *back_cairo_surface = nullptr;
    cairo_t *back_cr = nullptr;
};

static volatile sig_atomic_t reload_requested;

static void handle_sighup(int) {
    reload_requested = 1;
}

static int x11_error_handler(Display *dpy, XErrorEvent *ev) {
    if (ev->error_code == BadMatch &&
        ev->request_code == 142 &&
        ev->minor_code == 6) {
        return 0;
    }
    char buf[128] = {};
    XGetErrorText(dpy, ev->error_code, buf, sizeof(buf));
    fprintf(stderr, "X11 error: req=%d minor=%d code=%d (%s)\n",
        ev->request_code, ev->minor_code, ev->error_code, buf);
    return 0;
}

static const char *config_path() {
    const char *home = getenv("HOME");
    if (!home) return nullptr;
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s/.config/orbit-status/config", home);
    return buf;
}

static void render(OrbitStatus *ws);
static void popup_destroy(OrbitStatus *ws);
static void tooltip_destroy(OrbitStatus *ws);

static Atom get_atom(Display *dpy, const char *name) {
    return XInternAtom(dpy, name, False);
}

static void init_atoms(OrbitStatus *ws) {
    char buf[32];
    snprintf(buf, sizeof(buf), "_NET_SYSTEM_TRAY_S%d", ws->screen);
    ws->tray_selection = get_atom(ws->dpy, buf);
    ws->tray_opcode = get_atom(ws->dpy, "_NET_SYSTEM_TRAY_OPCODE");
    ws->manager_atom = get_atom(ws->dpy, "MANAGER");
    ws->xembed_info = get_atom(ws->dpy, "_XEMBED_INFO");
}

static void tray_add_client(OrbitStatus *ws, Window client_win) {
    if (ws->n_tray_clients >= MAX_TRAY_CLIENTS) return;

    XWindowAttributes attr;
    if (!XGetWindowAttributes(ws->dpy, client_win, &attr)) return;

    XSelectInput(ws->dpy, client_win, StructureNotifyMask | PropertyChangeMask);

    TrayClient *tc = &ws->tray_clients[ws->n_tray_clients++];
    tc->win = client_win;
    tc->w = ws->tray_icon_size;
    tc->h = ws->tray_icon_size;
    tc->mapped = true;
    tc->pixmap = 0;
    tc->surface = nullptr;

    XReparentWindow(ws->dpy, client_win, ws->tray_win, 0, 0);
    XResizeWindow(ws->dpy, client_win, tc->w, tc->h);
    XMapWindow(ws->dpy, client_win);

    tc->pixmap = XCompositeNameWindowPixmap(ws->dpy, client_win);
    if (tc->pixmap) {
        tc->surface = cairo_xlib_surface_create(ws->dpy, tc->pixmap,
            DefaultVisual(ws->dpy, ws->screen), tc->w, tc->h);
    } else {
        fprintf(stderr, "tray: XCompositeNameWindowPixmap failed for 0x%lx\n", client_win);
    }

    long xembed_data[2] = {XEMBED_MAPPED, 1};
    XChangeProperty(ws->dpy, client_win, ws->xembed_info,
        ws->xembed_info, 32, PropModeReplace,
        (unsigned char *)xembed_data, 2);

    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = client_win;
    ev.xclient.message_type = ws->xembed_info;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = CurrentTime;
    ev.xclient.data.l[1] = XEMBED_EMBEDDED_NOTIFY;
    ev.xclient.data.l[2] = 0;
    ev.xclient.data.l[3] = ws->tray_win;
    ev.xclient.data.l[4] = 0;
    XSendEvent(ws->dpy, client_win, False, NoEventMask, &ev);
}

static void tray_remove_client(OrbitStatus *ws, Window win) {
    for (int i = 0; i < ws->n_tray_clients; i++) {
        if (ws->tray_clients[i].win == win) {
            if (ws->tray_clients[i].surface)
                cairo_surface_destroy(ws->tray_clients[i].surface);
            if (ws->tray_clients[i].pixmap)
                XFreePixmap(ws->dpy, ws->tray_clients[i].pixmap);
            ws->tray_clients[i] = ws->tray_clients[--ws->n_tray_clients];
            render(ws);
            return;
        }
    }
}

static void tray_handle_client_message(OrbitStatus *ws, XClientMessageEvent *ev) {
    if (ev->message_type != ws->tray_opcode) return;
    if ((Atom)ev->data.l[1] != SYSTEM_TRAY_REQUEST_DOCK) return;
    Window client_win = (Window)ev->data.l[2];
    tray_add_client(ws, client_win);
    render(ws);
}

static bool tray_claim_selection(OrbitStatus *ws) {
    ws->tray_win = XCreateSimpleWindow(ws->dpy, RootWindow(ws->dpy, ws->screen),
        0, 0, 1, 1, 0, 0, 0);
    XSetSelectionOwner(ws->dpy, ws->tray_selection, ws->tray_win, CurrentTime);
    if (XGetSelectionOwner(ws->dpy, ws->tray_selection) != ws->tray_win)
        return false;

    XClientMessageEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.window = RootWindow(ws->dpy, ws->screen);
    ev.message_type = ws->manager_atom;
    ev.format = 32;
    ev.data.l[0] = CurrentTime;
    ev.data.l[1] = ws->tray_selection;
    ev.data.l[2] = ws->tray_win;
    ev.data.l[3] = 0;
    ev.data.l[4] = 0;
    XSendEvent(ws->dpy, RootWindow(ws->dpy, ws->screen), False,
        StructureNotifyMask, (XEvent *)&ev);

    return true;
}

static void setup_tray(OrbitStatus *ws) {
    init_atoms(ws);

    int comp_event_base, comp_error_base;
    if (!XCompositeQueryExtension(ws->dpy, &comp_event_base, &comp_error_base)) {
        fprintf(stderr, "warning: Composite extension not available, tray may not work\n");
    } else {
        fprintf(stderr, "composite available (event_base=%d)\n", comp_event_base);
    }

    if (tray_claim_selection(ws)) {
        fprintf(stderr, "system tray claimed\n");
    } else {
        fprintf(stderr, "warning: could not claim system tray\n");
    }
}

static void setup_plugin_watches(OrbitStatus *ws) {
    if (!ws->bar) return;
    ws->inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (ws->inotify_fd < 0) return;

    for (int i = 0; i < ws->bar->n_lua_plugins; i++) {
        char watchkey[32];
        snprintf(watchkey, sizeof(watchkey), "lua_plugin_%d_watch", i + 1);
        const char *watch = config_get(ws->cfg, watchkey, "");
        if (watch[0]) {
            int wd = inotify_add_watch(ws->inotify_fd, watch, IN_MODIFY);
            if (wd >= 0) ws->bar->lua_plugins[i].watch_wd = wd;
        }
    }

    const char *plugins_dir = config_get(ws->cfg, "lua_plugins_dir", nullptr);
    if (!plugins_dir) {
        const char *home = getenv("HOME");
        if (home) {
            static char dir[256];
            snprintf(dir, sizeof(dir), "%s/.config/orbit-status/plugins", home);
            plugins_dir = dir;
        }
    }

    if (plugins_dir) {
        auto add_watch_for_plugin = [&](int idx, const char *sysfs) {
            if (idx < 0 || idx >= ws->bar->n_lua_plugins) return;
            if (ws->bar->lua_plugins[idx].watch_wd >= 0) return;
            if (access(sysfs, F_OK) != 0) return;
            int wd = inotify_add_watch(ws->inotify_fd, sysfs, IN_MODIFY);
            if (wd >= 0) ws->bar->lua_plugins[idx].watch_wd = wd;
        };

        for (int i = 0; i < ws->bar->n_lua_plugins; i++) {
            const char *pname = ws->bar->lua_plugins[i].path;
            if (!pname[0]) continue;

            if (strstr(pname, "battery")) {
                add_watch_for_plugin(i, "/sys/class/power_supply/BAT0/uevent");
            } else if (strstr(pname, "brightness")) {
                static const char *backlights[] = {
                    "/sys/class/backlight/intel_backlight/brightness",
                    "/sys/class/backlight/amdgpu_bl0/brightness",
                    "/sys/class/backlight/nvidia_0/brightness",
                    nullptr
                };
                for (int b = 0; backlights[b]; b++) {
                    if (access(backlights[b], F_OK) == 0) {
                        add_watch_for_plugin(i, backlights[b]);
                        break;
                    }
                }
            } else if (strstr(pname, "network") || strstr(pname, "net")) {
                add_watch_for_plugin(i, "/sys/class/net");
            }
        }
    }
}

static void create_back_buffer(OrbitStatus *ws) {
    if (ws->back_pixmap) {
        cairo_destroy(ws->back_cr);
        cairo_surface_destroy(ws->back_cairo_surface);
        XFreePixmap(ws->dpy, ws->back_pixmap);
    }
    ws->back_pixmap = XCreatePixmap(ws->dpy, ws->win, ws->width, ws->height,
        DefaultDepth(ws->dpy, ws->screen));
    ws->back_cairo_surface = cairo_xlib_surface_create(ws->dpy, ws->back_pixmap,
        DefaultVisual(ws->dpy, ws->screen), ws->width, ws->height);
    ws->back_cr = cairo_create(ws->back_cairo_surface);
}

static void destroy_back_buffer(OrbitStatus *ws) {
    if (ws->back_cr) cairo_destroy(ws->back_cr);
    ws->back_cr = nullptr;
    if (ws->back_cairo_surface) cairo_surface_destroy(ws->back_cairo_surface);
    ws->back_cairo_surface = nullptr;
    if (ws->back_pixmap) XFreePixmap(ws->dpy, ws->back_pixmap);
    ws->back_pixmap = 0;
}

static void render(OrbitStatus *ws) {
    if (!ws->back_cr) create_back_buffer(ws);

    int tray_w = ws->n_tray_clients * (ws->tray_icon_size + 4);
    ws->bar->tray_width = tray_w;


    cairo_t *cr = ws->back_cr;
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    bar_render(ws->bar, cr);

    int tray_x = ws->width - BAR_PADDING - tray_w;

    for (int i = 0; i < ws->n_tray_clients; i++) {
        TrayClient *tc = &ws->tray_clients[i];
        tc->x = tray_x;
        tc->y = (ws->height - tc->h) / 2;

        if (tc->surface) {
            cairo_surface_destroy(tc->surface);
            tc->surface = nullptr;
        }
        if (tc->pixmap) {
            XFreePixmap(ws->dpy, tc->pixmap);
        }
        tc->pixmap = XCompositeNameWindowPixmap(ws->dpy, tc->win);
        if (tc->pixmap) {
            tc->surface = cairo_xlib_surface_create(ws->dpy, tc->pixmap,
                DefaultVisual(ws->dpy, ws->screen), tc->w, tc->h);
        }

        cairo_save(cr);
        cairo_rectangle(cr, tc->x, tc->y, tc->w, tc->h);
        cairo_clip(cr);

        cairo_set_source_rgba(cr, 0.2, 0.2, 0.3, 0.5);
        cairo_rectangle(cr, tc->x, tc->y, tc->w, tc->h);
        cairo_fill(cr);

        if (tc->surface) {
            cairo_set_source_surface(cr, tc->surface, tc->x, tc->y);
            cairo_paint(cr);
        }

        cairo_restore(cr);

        tray_x += tc->w + 4;
    }

    cairo_surface_flush(ws->back_cairo_surface);
    XCopyArea(ws->dpy, ws->back_pixmap, ws->win, ws->gc, 0, 0, ws->width, ws->height, 0, 0);
    XFlush(ws->dpy);
}

static void execute_command(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, nullptr);
        _exit(1);
    }
}

static void popup_render(OrbitStatus *ws) {
    if (!ws->popup.visible) return;
    int w = ws->popup.width, h = ws->popup.height;

    Pixmap pm = XCreatePixmap(ws->dpy, ws->popup.win, w, h,
        DefaultDepth(ws->dpy, ws->screen));
    cairo_surface_t *surf = cairo_xlib_surface_create(ws->dpy, pm,
        DefaultVisual(ws->dpy, ws->screen), w, h);
    cairo_t *cr = cairo_create(surf);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    draw_rounded_rect(cr, 0, 0, w, h, 8);
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.22, 0.96);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, 0.3);
    cairo_set_line_width(cr, 1);
    draw_rounded_rect(cr, 0, 0, w, h, 8);
    cairo_stroke(cr);

    const char *ff = config_get(ws->cfg, "font_family", "Sans");
    char popup_title_font[64];
    snprintf(popup_title_font, sizeof(popup_title_font), "%s Bold 13", ff);
    PangoLayout *lay = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(popup_title_font);
    pango_layout_set_font_description(lay, fd);
    pango_font_description_free(fd);
    const char *labels[] = {"Power Off", "Reboot", "Suspend"};
    pango_layout_set_text(lay, labels[ws->popup.action], -1);
    int tw, th;
    pango_layout_get_pixel_size(lay, &tw, &th);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, (w - tw) / 2, 24);
    pango_cairo_show_layout(cr, lay);

    int btn_w = 65, btn_h = 28, btn_gap = 12;
    int btn_y = h - btn_h - 14;
    int confirm_x = (w - btn_w * 2 - btn_gap) / 2;
    int cancel_x = confirm_x + btn_w + btn_gap;

    bool con_hover = (ws->popup.hovered_btn == 0);
    bool can_hover = (ws->popup.hovered_btn == 1);

    cairo_set_source_rgba(cr, 0.2, 0.8, 0.3, con_hover ? 0.9 : 0.6);
    draw_rounded_rect(cr, confirm_x, btn_y, btn_w, btn_h, 5);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, can_hover ? 0.9 : 0.6);
    draw_rounded_rect(cr, cancel_x, btn_y, btn_w, btn_h, 5);
    cairo_fill(cr);

    char btn_font[64];
    snprintf(btn_font, sizeof(btn_font), "%s Bold 11", ff);
    PangoFontDescription *fb = pango_font_description_from_string(btn_font);
    PangoLayout *lc = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(lc, fb);
    pango_layout_set_text(lc, "Confirm", -1);
    int cw, ch;
    pango_layout_get_pixel_size(lc, &cw, &ch);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, confirm_x + (btn_w - cw) / 2, btn_y + (btn_h - ch) / 2 + 1);
    pango_cairo_show_layout(cr, lc);

    PangoLayout *lx = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(lx, fb);
    pango_layout_set_text(lx, "Cancel", -1);
    pango_layout_get_pixel_size(lx, &cw, &ch);
    cairo_move_to(cr, cancel_x + (btn_w - cw) / 2, btn_y + (btn_h - ch) / 2 + 1);
    pango_cairo_show_layout(cr, lx);
    pango_font_description_free(fb);
    g_object_unref(lc);
    g_object_unref(lx);
    g_object_unref(lay);

    ws->popup.confirm_btn_x = confirm_x;
    ws->popup.confirm_btn_y = btn_y;
    ws->popup.confirm_btn_w = btn_w;
    ws->popup.confirm_btn_h = btn_h;
    ws->popup.cancel_btn_x = cancel_x;
    ws->popup.cancel_btn_y = btn_y;
    ws->popup.cancel_btn_w = btn_w;
    ws->popup.cancel_btn_h = btn_h;

    cairo_surface_flush(surf);
    XCopyArea(ws->dpy, pm, ws->popup.win, ws->gc, 0, 0, w, h, 0, 0);
    XFlush(ws->dpy);

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    XFreePixmap(ws->dpy, pm);
}

static void popup_destroy(OrbitStatus *ws) {
    if (ws->popup.visible && ws->popup.win) {
        XUnmapWindow(ws->dpy, ws->popup.win);
        ws->popup.visible = false;
    }
}

static void popup_create(OrbitStatus *ws, int action) {
    if (ws->popup.visible) popup_destroy(ws);

    ws->popup.width = 175;
    ws->popup.height = 105;
    ws->popup.action = action;

    if (!ws->popup.win) {
        ws->popup.win = XCreateSimpleWindow(ws->dpy, RootWindow(ws->dpy, ws->screen),
            0, 0, ws->popup.width, ws->popup.height, 0, 0, 0);
        XSelectInput(ws->dpy, ws->popup.win, ExposureMask | ButtonPressMask |
            ButtonReleaseMask | PointerMotionMask | LeaveWindowMask);
    }

    const char *anchor_str = config_get(ws->cfg, "bar_anchor", "top");
    bool bar_on_bottom = (strcmp(anchor_str, "bottom") == 0);
    int px = ws->width - ws->popup.width - BAR_PADDING;
    int py = bar_on_bottom ? ws->height - ws->popup.height - 4 : ws->height + 4;
    XMoveResizeWindow(ws->dpy, ws->popup.win, px, py,
        ws->popup.width, ws->popup.height);
    XMapWindow(ws->dpy, ws->popup.win);
    ws->popup.visible = true;
    popup_render(ws);
}

static void tooltip_render(OrbitStatus *ws) {
    if (!ws->tooltip.visible) return;
    int w = ws->tooltip.width, h = ws->tooltip.height;

    Pixmap pm = XCreatePixmap(ws->dpy, ws->tooltip.win, w, h,
        DefaultDepth(ws->dpy, ws->screen));
    cairo_surface_t *surf = cairo_xlib_surface_create(ws->dpy, pm,
        DefaultVisual(ws->dpy, ws->screen), w, h);
    cairo_t *cr = cairo_create(surf);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    draw_rounded_rect(cr, 0, 0, w, h, 6);
    cairo_set_source_rgba(cr, 0.10, 0.10, 0.18, 0.96);
    cairo_fill(cr);

    const char *tip_ff = config_get(ws->cfg, "font_family", "Sans");
    char tip_font[64];
    snprintf(tip_font, sizeof(tip_font), "%s 10", tip_ff);
    PangoLayout *lay = pango_cairo_create_layout(cr);
    PangoFontDescription *fdesc = pango_font_description_from_string(tip_font);
    pango_layout_set_font_description(lay, fdesc);
    pango_font_description_free(fdesc);
    pango_layout_set_text(lay, ws->tooltip.text, -1);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, 6, 6);
    pango_cairo_show_layout(cr, lay);
    g_object_unref(lay);

    cairo_set_source_rgba(cr, 0.0, 0.90, 1.0, 0.3);
    cairo_set_line_width(cr, 1);
    draw_rounded_rect(cr, 0, 0, w, h, 6);
    cairo_stroke(cr);

    cairo_surface_flush(surf);
    XCopyArea(ws->dpy, pm, ws->tooltip.win, ws->gc, 0, 0, w, h, 0, 0);
    XFlush(ws->dpy);

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    XFreePixmap(ws->dpy, pm);
}

static void tooltip_destroy(OrbitStatus *ws) {
    if (ws->tooltip.visible && ws->tooltip.win) {
        XUnmapWindow(ws->dpy, ws->tooltip.win);
        ws->tooltip.visible = false;
    }
    ws->tooltip.hovered_clickable = -1;
}

static void tooltip_show(OrbitStatus *ws, const char *text, int hover_x, int hover_y) {
    if (ws->tooltip.visible && strcmp(ws->tooltip.text, text) == 0)
        return;

    tooltip_destroy(ws);

    ws->tooltip.hovered_clickable = 0;
    strncpy(ws->tooltip.text, text, sizeof(ws->tooltip.text) - 1);

    int nlines = 1;
    for (const char *p = text; *p; p++)
        if (*p == '\n') nlines++;
    ws->tooltip.width = 280;
    ws->tooltip.height = nlines * 16 + 14;

    if (!ws->tooltip.win) {
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        ws->tooltip.win = XCreateWindow(ws->dpy, RootWindow(ws->dpy, ws->screen),
            0, 0, ws->tooltip.width, ws->tooltip.height, 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect, &attrs);
        XSelectInput(ws->dpy, ws->tooltip.win, ExposureMask);
    }

    const char *anchor_str = config_get(ws->cfg, "bar_anchor", "top");
    bool bar_on_bottom = (strcmp(anchor_str, "bottom") == 0);
    int tx = ws->width - ws->tooltip.width - BAR_PADDING;
    int ty = bar_on_bottom ? ws->height - ws->tooltip.height - 4 : ws->height + 4;
    XMoveResizeWindow(ws->dpy, ws->tooltip.win, tx, ty,
        ws->tooltip.width, ws->tooltip.height);
    XMapWindow(ws->dpy, ws->tooltip.win);
    ws->tooltip.visible = true;
    tooltip_render(ws);
}

static void handle_button(OrbitStatus *ws, XButtonEvent *ev) {
    if (ev->button != Button1) return;
    int x = ev->x, y = ev->y;

    if (ws->popup.visible) {
        if (x >= ws->popup.confirm_btn_x && x < ws->popup.confirm_btn_x + ws->popup.confirm_btn_w &&
            y >= ws->popup.confirm_btn_y && y < ws->popup.confirm_btn_y + ws->popup.confirm_btn_h) {
            const char *cmds[] = {"systemctl poweroff", "systemctl reboot", "systemctl suspend"};
            execute_command(cmds[ws->popup.action]);
            popup_destroy(ws);
            return;
        }
        if (x >= ws->popup.cancel_btn_x && x < ws->popup.cancel_btn_x + ws->popup.cancel_btn_w &&
            y >= ws->popup.cancel_btn_y && y < ws->popup.cancel_btn_y + ws->popup.cancel_btn_h) {
            popup_destroy(ws);
            return;
        }
        popup_destroy(ws);
    }

    for (int i = 0; i < ws->bar->n_clickables; i++) {
        Clickable *c = &ws->bar->clickables[i];
        if (x >= c->x && x < c->x + c->w &&
            y >= c->y && y < c->y + c->h) {
            switch (c->action) {
            case CLICK_POWEROFF:
                popup_create(ws, 0);
                break;
            case CLICK_REBOOT:
                popup_create(ws, 1);
                break;
            case CLICK_SUSPEND:
                popup_create(ws, 2);
                break;
            case CLICK_HYPRCTL:
                execute_command(c->command);
                break;
            case CLICK_RUN:
                execute_command(c->command);
                break;
            default:
                break;
            }
            break;
        }
    }
}

static void handle_motion(OrbitStatus *ws, XMotionEvent *ev) {
    int x = ev->x, y = ev->y;
    ws->pointer_x = x;
    ws->pointer_y = y;

    int old_power = ws->bar->power_hovered;
    int old_ws = ws->bar->hovered_workspace;
    bar_update_hover(ws->bar, x, y);
    if (old_power != ws->bar->power_hovered || old_ws != ws->bar->hovered_workspace)
        render(ws);

    int found_tooltip = -1;
    for (int i = 0; i < ws->bar->n_clickables; i++) {
        Clickable *c = &ws->bar->clickables[i];
        if ((c->tooltip_cmd[0] || c->tooltip_text[0]) &&
            x >= c->x && x < c->x + c->w &&
            y >= c->y && y < c->y + c->h) {
            found_tooltip = i;
            break;
        }
    }
    if (found_tooltip >= 0 && found_tooltip != ws->tooltip.hovered_clickable) {
        Clickable *c = &ws->bar->clickables[found_tooltip];
        if (c->tooltip_text[0]) {
            ws->tooltip.hovered_clickable = found_tooltip;
            tooltip_show(ws, c->tooltip_text, x, y);
        } else {
            FILE *fp = popen(c->tooltip_cmd, "r");
            if (fp) {
                char buf[512] = {0};
                size_t total = 0;
                char line[256];
                while (fgets(line, sizeof(line), fp) && total < sizeof(buf) - 1) {
                    int len = snprintf(buf + total, sizeof(buf) - total, "%s", line);
                    if (len > 0) total += len;
                }
                pclose(fp);
                ws->tooltip.hovered_clickable = found_tooltip;
                tooltip_show(ws, buf, x, y);
            }
        }
    } else if (found_tooltip < 0) {
        if (ws->tooltip.visible)
            tooltip_destroy(ws);
    }
}

static void handle_leave(OrbitStatus *ws) {
    bar_clear_hover(ws->bar);
    render(ws);
    if (ws->tooltip.visible)
        tooltip_destroy(ws);
}

static void reload(OrbitStatus *ws) {
    if (ws->popup.visible) popup_destroy(ws);
    if (ws->tooltip.visible) tooltip_destroy(ws);
    destroy_back_buffer(ws);
    if (ws->bar) bar_destroy(ws->bar);
    config_destroy(ws->cfg);
    ws->cfg = config_load(config_path());
    ws->bar = bar_create(ws->width, ws->height, ws->cfg);
    create_back_buffer(ws);
    setup_plugin_watches(ws);
    bar_update_workspaces(ws->bar, ws->dpy);
    bar_update_lua_plugins(ws->bar);
    render(ws);
}

static void on_timer(OrbitStatus *ws) {
    if (!ws->bar || !ws->running) return;
    bar_update_workspaces(ws->bar, ws->dpy);
    bar_update_lua_plugins(ws->bar);
    render(ws);
}

int main() {
    OrbitStatus ws;

    ws.cfg = config_load(config_path());
    int bh = config_get_int(ws.cfg, "bar_height", BAR_HEIGHT);
    auto *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        fprintf(stderr, "failed to connect to X11 display\n");
        return 1;
    }
    XSetErrorHandler(x11_error_handler);
    ws.dpy = dpy;
    ws.screen = DefaultScreen(dpy);
    ws.width = DisplayWidth(dpy, ws.screen);
    ws.height = bh;
    ws.running = true;

    Window root = RootWindow(dpy, ws.screen);

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | LeaveWindowMask | SubstructureNotifyMask |
        StructureNotifyMask;

    const char *anchor_str = config_get(ws.cfg, "bar_anchor", "top");
    bool bar_on_bottom = (strcmp(anchor_str, "bottom") == 0);
    int win_y = bar_on_bottom ? DisplayHeight(dpy, ws.screen) - bh : 0;

    ws.win = XCreateWindow(dpy, root, 0, win_y, ws.width, ws.height, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWEventMask, &attrs);

    XMapWindow(dpy, ws.win);
    ws.gc = XCreateGC(dpy, ws.win, 0, nullptr);

    ws.bar = bar_create(ws.width, ws.height, ws.cfg);

    create_back_buffer(&ws);
    setup_tray(&ws);
    setup_plugin_watches(&ws);
    bar_update_workspaces(ws.bar, ws.dpy);
    bar_update_lua_plugins(ws.bar);
    render(&ws);

    ws.timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (ws.timer_fd >= 0) {
        struct itimerspec ts = {};
        ts.it_value.tv_sec = 1;
        ts.it_interval.tv_sec = 1;
        timerfd_settime(ws.timer_fd, 0, &ts, nullptr);
    }

    struct sigaction sa = {};
    sa.sa_handler = handle_sighup;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, nullptr);

    ws.inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (ws.inotify_fd >= 0) {
        const char *cfg_path = config_path();
        if (cfg_path) {
            char cfg_dir[512];
            snprintf(cfg_dir, sizeof(cfg_dir), "%s", cfg_path);
            char *slash = strrchr(cfg_dir, '/');
            if (slash) {
                *slash = '\0';
                inotify_add_watch(ws.inotify_fd, cfg_dir,
                    IN_CLOSE_WRITE | IN_MOVED_TO);
            }
        }
    }

    XSelectInput(dpy, root, SubstructureNotifyMask | PropertyChangeMask);

    int x11_fd = ConnectionNumber(dpy);

    while (ws.running) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);

            switch (ev.type) {
            case Expose:
                if (ev.xexpose.count == 0) {
                    if (ev.xexpose.window == ws.win)
                        render(&ws);
                    else if (ev.xexpose.window == ws.popup.win)
                        popup_render(&ws);
                    else if (ev.xexpose.window == ws.tooltip.win)
                        tooltip_render(&ws);
                }
                break;
            case ButtonPress:
                if (ev.xbutton.window == ws.win)
                    handle_button(&ws, &ev.xbutton);
                else if (ev.xbutton.window == ws.popup.win) {
                    int x = ev.xbutton.x, y = ev.xbutton.y;
                    if (x >= ws.popup.confirm_btn_x && x < ws.popup.confirm_btn_x + ws.popup.confirm_btn_w &&
                        y >= ws.popup.confirm_btn_y && y < ws.popup.confirm_btn_y + ws.popup.confirm_btn_h) {
                        const char *cmds[] = {"systemctl poweroff", "systemctl reboot", "systemctl suspend"};
                        execute_command(cmds[ws.popup.action]);
                        popup_destroy(&ws);
                    } else if (x >= ws.popup.cancel_btn_x && x < ws.popup.cancel_btn_x + ws.popup.cancel_btn_w &&
                        y >= ws.popup.cancel_btn_y && y < ws.popup.cancel_btn_y + ws.popup.cancel_btn_h) {
                        popup_destroy(&ws);
                    } else {
                        popup_destroy(&ws);
                    }
                }
                break;
            case MotionNotify:
                if (ev.xmotion.window == ws.win)
                    handle_motion(&ws, &ev.xmotion);
                else if (ev.xmotion.window == ws.popup.win) {
                    int old = ws.popup.hovered_btn;
                    ws.popup.hovered_btn = -1;
                    int x = ev.xmotion.x, y = ev.xmotion.y;
                    if (x >= ws.popup.confirm_btn_x && x < ws.popup.confirm_btn_x + ws.popup.confirm_btn_w &&
                        y >= ws.popup.confirm_btn_y && y < ws.popup.confirm_btn_y + ws.popup.confirm_btn_h)
                        ws.popup.hovered_btn = 0;
                    else if (x >= ws.popup.cancel_btn_x && x < ws.popup.cancel_btn_x + ws.popup.cancel_btn_w &&
                        y >= ws.popup.cancel_btn_y && y < ws.popup.cancel_btn_y + ws.popup.cancel_btn_h)
                        ws.popup.hovered_btn = 1;
                    if (old != ws.popup.hovered_btn)
                        popup_render(&ws);
                }
                break;
            case LeaveNotify:
                if (ev.xcrossing.window == ws.win)
                    handle_leave(&ws);
                else if (ev.xcrossing.window == ws.popup.win) {
                    if (ws.popup.hovered_btn != -1) {
                        ws.popup.hovered_btn = -1;
                        popup_render(&ws);
                    }
                }
                break;
            case PropertyNotify:
                if (ev.xproperty.window == root &&
                    ev.xproperty.state == PropertyNewValue) {
                    bar_update_workspaces(ws.bar, ws.dpy);
                    render(&ws);
                }
                break;
            case ClientMessage:
                tray_handle_client_message(&ws, &ev.xclient);
                break;
            case DestroyNotify:
                if (ev.xdestroywindow.event == ws.win) {
                    ws.running = false;
                }
                if (ev.xdestroywindow.window == ws.popup.win)
                    ws.popup.visible = false;
                if (ev.xdestroywindow.window == ws.tooltip.win)
                    ws.tooltip.visible = false;
                for (int i = 0; i < ws.n_tray_clients; i++) {
                    if (ws.tray_clients[i].win == ev.xdestroywindow.window) {
                        tray_remove_client(&ws, ev.xdestroywindow.window);
                        break;
                    }
                }
                break;
            case UnmapNotify:
                for (int i = 0; i < ws.n_tray_clients; i++) {
                    if (ws.tray_clients[i].win == ev.xunmap.window) {
                        tray_remove_client(&ws, ev.xunmap.window);
                        break;
                    }
                }
                break;
            }
        }

        struct pollfd fds[3];
        int nfds = 0;
        fds[nfds].fd = x11_fd;
        fds[nfds].events = POLLIN;
        nfds++;

        if (ws.timer_fd >= 0) {
            fds[nfds].fd = ws.timer_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        if (ws.inotify_fd >= 0) {
            fds[nfds].fd = ws.inotify_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        if (poll(fds, nfds, 50) < 0) {
            if (errno == EINTR)
                goto check_reload;
            break;
        }

        if (ws.timer_fd >= 0 && (fds[1].revents & POLLIN)) {
            uint64_t exp;
            read(ws.timer_fd, &exp, sizeof(exp));
            on_timer(&ws);
        }

        if (ws.inotify_fd >= 0 && (fds[2].revents & POLLIN)) {
            char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
            ssize_t len = read(ws.inotify_fd, buf, sizeof(buf));
            if (len > 0) {
                for (char *p = buf; p < buf + len; ) {
                    const auto *ev = (const struct inotify_event *)p;
                    if (ev->len > 0 && strcmp(ev->name, "config") == 0) {
                        reload(&ws);
                        break;
                    }
                    for (int i = 0; i < ws.bar->n_lua_plugins; i++) {
                        if (ws.bar->lua_plugins[i].watch_wd == (int)ev->wd) {
                            ws.bar->lua_plugins[i].last_check = 0;
                        }
                    }
                    p += sizeof(struct inotify_event) + ev->len;
                }
                bar_update_lua_plugins(ws.bar);
                render(&ws);
            }
        }

check_reload:
        if (reload_requested) {
            reload_requested = 0;
            reload(&ws);
        }
    }

    if (ws.timer_fd >= 0) close(ws.timer_fd);
    if (ws.inotify_fd >= 0) close(ws.inotify_fd);
    tooltip_destroy(&ws);
    popup_destroy(&ws);
    destroy_back_buffer(&ws);
    bar_destroy(ws.bar);
    config_destroy(ws.cfg);
    for (int i = 0; i < ws.n_tray_clients; i++) {
        if (ws.tray_clients[i].surface)
            cairo_surface_destroy(ws.tray_clients[i].surface);
        if (ws.tray_clients[i].pixmap)
            XFreePixmap(ws.dpy, ws.tray_clients[i].pixmap);
    }
    if (ws.tray_win) XDestroyWindow(dpy, ws.tray_win);
    if (ws.gc) XFreeGC(dpy, ws.gc);
    if (ws.win) XDestroyWindow(dpy, ws.win);
    XCloseDisplay(dpy);
    return 0;
}
