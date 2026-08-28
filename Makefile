PREFIX ?= /usr/local
STD := -std=c++17
WARN := -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

PLUGINS_DIR := $(DESTDIR)$(PREFIX)/share/orbit-status/plugins

# --- OS Detection ---
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), FreeBSD)
    CXX ?= clang++
    EXTRA_FLAGS := -I/usr/local/include -L/usr/local/lib
    LUA_PKG := lua-5.4
    # FreeBSD specific pkg-config paths
    PKG_CONFIG_PATH := /usr/local/libdata/pkgconfig:/usr/local/lib/pkgconfig
else
    CXX ?= g++
    EXTRA_FLAGS :=
    LUA_PKG := lua
    # Standard Linux pkg-config paths
    PKG_CONFIG_PATH := /usr/lib/pkgconfig:/usr/share/pkgconfig
endif

# Export for the shell commands below
export PKG_CONFIG_PATH

# --- Wayland protocol codegen ---
WAYLAND_SCANNER := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)
PROTOCOLS_DIR := /usr/share/wayland-protocols

WLROOT := wlr-layer-shell-unstable-v1
WLHEADER := build/$(WLROOT)-client.h
WLCODE := build/$(WLROOT)-client.c

XDG   := xdg-shell
XDGXML := $(PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
XDGHDR := build/$(XDG)-client.h
XDGCOD := build/$(XDG)-client.c

# Build flags
CXXFLAGS := $(CXXFLAGS) $(STD) $(WARN) $(EXTRA_FLAGS) -Ibuild \
    $(shell pkg-config --cflags wayland-client cairo pangocairo librsvg-2.0 dbus-1 $(LUA_PKG))

LDLIBS := $(EXTRA_FLAGS) \
    $(shell pkg-config --libs wayland-client cairo pangocairo librsvg-2.0 dbus-1 $(LUA_PKG)) \
    -lm

OBJS := build/main.o build/bar.o build/lua_plugin.o build/sway_ipc.o build/sni_tray.o \
    build/$(WLROOT)-client.o build/$(XDG)-client.o

orbit-status: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

# volume-sni: standalone Wayland-native SNI volume control.
volume-sni: build/volume-sni.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: %.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/main.o: main.cpp $(WLHEADER) $(XDGHDR)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/%.o: %.c
	@mkdir -p build
	$(CC) -c -o $@ $<

build/$(WLROOT)-client.o: $(WLCODE) $(WLHEADER)
	@mkdir -p build
	$(CC) -c -o $@ $<

build/$(XDG)-client.o: $(XDGCOD) $(XDGHDR)
	@mkdir -p build
	$(CC) -c -o $@ $<

$(WLHEADER): $(WLROOT).xml
	@mkdir -p build
	$(WAYLAND_SCANNER) client-header < $< | sed 's/namespace/wl_namespace/g' > $@

$(WLCODE): $(WLROOT).xml
	@mkdir -p build
	$(WAYLAND_SCANNER) private-code < $< > $@

$(XDGHDR): $(XDGXML)
	@mkdir -p build
	$(WAYLAND_SCANNER) client-header < $< > $@

$(XDGCOD): $(XDGXML)
	@mkdir -p build
	$(WAYLAND_SCANNER) private-code < $< > $@

clean:
	rm -rf orbit-status volume-sni build

install: orbit-status volume-sni
	install -Dm755 orbit-status $(DESTDIR)$(PREFIX)/bin/orbit-status
	install -Dm755 volume-sni $(DESTDIR)$(PREFIX)/bin/volume-sni
	install -Dm755 scripts/bar-update $(DESTDIR)$(PREFIX)/bin/orbit-status-update
	install -d $(PLUGINS_DIR)
	install -m644 plugins/*.lua $(PLUGINS_DIR)/ 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/orbit-status
	rm -f $(DESTDIR)$(PREFIX)/bin/volume-sni
	rm -rf $(DESTDIR)$(PREFIX)/share/orbit-status/plugins

.PHONY: clean install uninstall