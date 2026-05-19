PREFIX ?= /usr/local
CXXFLAGS ?= -O2 -pipe

WAYLAND_SCANNER := $(shell pkg-config --variable=wayland_scanner wayland-scanner)
PROTOCOLS_DIR := /usr/share/wayland-protocols

CXXFLAGS += $(shell pkg-config --cflags wayland-client cairo pangocairo xkbcommon lua5.4) \
	-std=c++20 -Wall -Wextra -Wno-unused-parameter
LDLIBS = $(shell pkg-config --libs wayland-client cairo pangocairo xkbcommon lua5.4) -lm

WLROOT := wlr-layer-shell-unstable-v1
WLHEADER := $(WLROOT)-client.h
WLCODE := $(WLROOT)-client.c

XDG   := xdg-shell
XDGXML := $(PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
XDGHDR := $(XDG)-client.h
XDGCOD := $(XDG)-client.c

OBJS := main.o bar.o lua_plugin.o $(WLCODE:.c=.o) $(XDGCOD:.c=.o)
PLUGINS_DIR := $(DESTDIR)$(PREFIX)/share/wlstatus/plugins

wlstatus: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(WLHEADER): $(WLROOT).xml
	$(WAYLAND_SCANNER) client-header < $< > $@

$(WLCODE): $(WLROOT).xml
	$(WAYLAND_SCANNER) private-code < $< > $@

$(XDGHDR): $(XDGXML)
	$(WAYLAND_SCANNER) client-header < $< > $@

$(XDGCOD): $(XDGXML)
	$(WAYLAND_SCANNER) private-code < $< > $@

%.o: %.cpp bar.hpp config.hpp lua_plugin.hpp $(WLHEADER)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) -c -o $@ $<

clean:
	rm -f wlstatus *.o $(WLHEADER) $(WLCODE) $(XDGHDR) $(XDGCOD)

install: wlstatus
	install -Dm755 wlstatus $(DESTDIR)$(PREFIX)/bin/wlstatus
	install -Dm755 scripts/bar-update $(DESTDIR)$(PREFIX)/bin/wlstatus-update
	install -d $(PLUGINS_DIR)
	install -m644 plugins/*.lua $(PLUGINS_DIR)/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wlstatus
	rm -rf $(DESTDIR)$(PREFIX)/share/wlstatus/plugins

.PHONY: clean install uninstall
