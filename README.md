# orbit-status

An ultra-lightweight, high-performance, information-dense status bar built in pure, type-safe C++17 and fully scriptable via modular Lua plugins. 

Inspired by the clean, responsive aesthetic of the **Distro Tube Operating System (DTOS)**, `orbit-status` provides a modern alternative to traditional monolithic bars. It operates on a zero-fork architecture, querying performance metrics directly from the Linux kernel to ensure an ultra-low footprint across all Linux distributions.

## Key Features

* **Zero-Fork Statistics Engine**: Parses `/proc/stat` and `/proc/meminfo` continuously using static `std::ifstream` data streams. It never calls external system applications or subshells, guaranteeing near-zero CPU cycles are wasted on updating the bar itself.
* **Dual-Protocol Compositor Architecture**: Employs compile-time conditional C++ preprocessing flags to seamlessly swap backend layers. It natively drives Wayland events (via Hyprland UNIX domain socket bindings) or legacy Xorg events (via X11 property notify root window atom listeners for XMonad).
* **Isolated Lua Sandboxing**: Loads every discrete status pill into its own independent, sandboxed Lua engine state. Plugins execute safely in separate frames without risking memory access collisions or UI lock-ups.
* **Systemd-Independent Compatibility**: Retains absolute portability. Because data tracking bypasses systemd APIs entirely, `orbit-status` runs out of the box on alternative init systems including **OpenRC**, **runit**, and **s6**, making it a perfect fit for distributions like Void Linux, Artix, or Alpine.

## Architectural Layout

```text
                   ┌──► Wayland Target ──► Reads Hyprland Unix Socket (.socket2.sock)
                   │
[orbit-status C++ Core]┤
                   │
                   └──► Xorg Target ─────► Listens to X11 Root Window (_XMONAD_LOG)
                           │
                           └─► Loads Isolated Lua States ─► [cpu.lua] [mem.lua]
```

## How It Works (Developer Documentation)

### 1. C++ Master Loop & Environment Isolation
The central execution hub in `main.cpp` checks for preprocessor directives passed during compilation. Rather than utilizing expensive background loops, the Xorg target sleeps efficiently until the Xserver broadcasts a `PropertyNotify` event, indicating that your window manager shifted workspaces or changed active window titles.

### 2. Lua Plugin Interface (`.lua`)
Every configuration module placed inside your user configuration directory must implement a core execution function. By utilizing persistent scoped states, the C++ engine feeds information to Lua seamlessly:

```lua
-- Sample real-time CPU metric loop structure
interval = 1 -- Tells the engine core to update this block every 1 second

function tick()
    -- Your pure Lua string manipulation or system calculation logic here
    return "    12% "
end
```

## Building & Installation

For a comprehensive layout of required software headers, compilation instructions, and package manager execution strings across Arch, Debian, Fedora, and Void Linux, refer directly to our **[build.md](./build.md)** specification document.

