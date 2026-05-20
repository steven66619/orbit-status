PREFIX ?= /usr/local
CXX := g++
CC := gcc
STD := -std=c++17
WARN := -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

WAYLAND_SCANNER := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null)
PROTOCOLS_DIR := /usr/share/wayland-protocols

WLROOT := wlr-layer-shell-unstable-v1
WLHEADER := $(WLROOT)-client.h
WLCODE := $(WLROOT)-client.c

XDG := xdg-shell
XDGXML := $(PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
XDGHDR := $(XDG)-client.h
XDGCOD := $(XDG)-client.c

PLUGINS_DIR := $(DESTDIR)$(PREFIX)/share/wlstatus/plugins

# ============================================================
#  Wayland Target (wlstatus)
# ============================================================
WAYLAND_CXXFLAGS := $(CXXFLAGS) $(STD) $(WARN) -DBUILD_WAYLAND \
	$(shell pkg-config --cflags wayland-client cairo pangocairo xkbcommon lua5.4)
WAYLAND_LDLIBS := $(shell pkg-config --libs wayland-client cairo pangocairo xkbcommon lua5.4) -lm
WAYLAND_OBJS := build-wayland/main.o build-wayland/bar.o build-wayland/lua_plugin.o
WAYLAND_PROTO_OBJS := build-wayland/$(WLCODE:.c=.o) build-wayland/$(XDGCOD:.c=.o)

$(WLHEADER): $(WLROOT).xml
	$(WAYLAND_SCANNER) client-header < $< > $@

$(WLCODE): $(WLROOT).xml
	$(WAYLAND_SCANNER) private-code < $< > $@

$(XDGHDR): $(XDGXML)
	$(WAYLAND_SCANNER) client-header < $< > $@

$(XDGCOD): $(XDGXML)
	$(WAYLAND_SCANNER) private-code < $< > $@

build-wayland/%.o: %.cpp $(WLHEADER)
	@mkdir -p build-wayland
	$(CXX) $(WAYLAND_CXXFLAGS) -c -o $@ $<

build-wayland/%.o: %.c
	@mkdir -p build-wayland
	$(CC) -c -o $@ $<

wayland: build-wayland/wlstatus

build-wayland/wlstatus: $(WAYLAND_OBJS) $(WAYLAND_PROTO_OBJS)
	$(CXX) $(WAYLAND_CXXFLAGS) -o $@ $^ $(WAYLAND_LDLIBS)

# ============================================================
#  Xorg Target (wlstatus-x11)
# ============================================================
XORG_CXXFLAGS := $(CXXFLAGS) $(STD) $(WARN) -DBUILD_XORG \
	$(shell pkg-config --cflags x11 cairo pangocairo lua5.4)
XORG_LDLIBS := $(shell pkg-config --libs x11 cairo pangocairo lua5.4) -lm -lXext
XORG_OBJS := build-xorg/main.o build-xorg/bar.o build-xorg/lua_plugin.o

build-xorg/%.o: %.cpp
	@mkdir -p build-xorg
	$(CXX) $(XORG_CXXFLAGS) -c -o $@ $<

xorg: build-xorg/wlstatus-x11

build-xorg/wlstatus-x11: $(XORG_OBJS)
	$(CXX) $(XORG_CXXFLAGS) -o $@ $^ $(XORG_LDLIBS)

# ============================================================
#  Clean / Install / Uninstall
# ============================================================
clean:
	rm -rf wlstatus wlstatus-x11 build-wayland build-xorg *.o \
		$(WLHEADER) $(WLCODE) $(XDGHDR) $(XDGCOD)

install: wlstatus
	install -Dm755 build-wayland/wlstatus $(DESTDIR)$(PREFIX)/bin/wlstatus
	install -Dm755 scripts/bar-update $(DESTDIR)$(PREFIX)/bin/wlstatus-update
	install -d $(PLUGINS_DIR)
	install -m644 plugins/*.lua $(PLUGINS_DIR)/ 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wlstatus
	rm -rf $(DESTDIR)$(PREFIX)/share/wlstatus/plugins

.PHONY: clean install uninstall wayland xorg
