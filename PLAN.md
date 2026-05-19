# Xorg/X11 Support Plan

## Goal
Make wlstatus work with X11 window managers (primarily Xmonad via EWMH).

## Approach
Abstract the display backend so both Wayland and X11 work from the same binary.

## Steps

### 1. Create a backend abstraction layer
- Define a `DisplayBackend` interface/struct with virtual methods:
  - `get_workspaces()` — list of workspaces with id, name, active state
  - `get_active_window()` — active window class + title
  - `track_windows()` — window list per workspace
  - `get_outputs()` — monitor list
  - `render()` — draw the bar surface
  - `handle_events()` — event loop (fd-based like hypr_ev_fd)

### 2. Wayland backend (existing, refactor)
- Move current `main.cpp` Wayland logic into `backend/wayland.cpp`
- wlr-layer-shell for surface positioning
- hyprctl for workspace/window info (keep as-is)

### 3. X11 backend (new)
- `backend/x11.cpp` and `backend/x11.hpp`
- Use Xlib or XCB for basic display connection
- EWMH atoms:
  - `_NET_DESKTOP_NAMES` — workspace names
  - `_NET_CURRENT_DESKKTOP` — active workspace
  - `_NET_NUMBER_OF_DESKTOPS` — workspace count
  - `_NET_ACTIVE_WINDOW` — active window
  - `_NET_WM_NAME` / `WM_CLASS` — window title/class
- Xdummy or override-redirect window for the bar surface
- XSelectInput for event-driven updates

### 4. Config option
- `backend = wayland` or `backend = x11` in config
- Auto-detect based on `$WAYLAND_DISPLAY` or `$DISPLAY`

### 5. Build system
- Optional X11 deps: `pkg-config --cflags --libs x11 xcb`
- Conditional compilation with `#ifdef WITH_X11`
- Makefile target: `make WITH_X11=1`

## Future Considerations
- System tray support (XEmbed protocol) for X11
- Multi-monitor support on X11
- Fallback when neither Wayland nor X11 is detected
