PREFIX ?= /usr/local
CFLAGS ?= -O2 -pipe

WAYLAND_SCANNER := $(shell pkg-config --variable=wayland_scanner wayland-scanner)
PROTOCOLS_DIR := /usr/share/wayland-protocols

CFLAGS += $(shell pkg-config --cflags wayland-client cairo pangocairo xkbcommon) \
	-std=c99 -Wall -Wextra -D_POSIX_C_SOURCE=200809L \
	-Wno-unused-parameter
LDLIBS = $(shell pkg-config --libs wayland-client cairo pangocairo xkbcommon) -lm

WLROOT := wlr-layer-shell-unstable-v1
WLHEADER := $(WLROOT)-client.h
WLCODE := $(WLROOT)-client.c

XDG   := xdg-shell
XDGXML := $(PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
XDGHDR := $(XDG)-client.h
XDGCOD := $(XDG)-client.c

OBJS := main.o bar.o config.o $(WLCODE:.c=.o) $(XDGCOD:.c=.o)

wlstatus: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(WLHEADER): $(WLROOT).xml
	$(WAYLAND_SCANNER) client-header < $< > $@

$(WLCODE): $(WLROOT).xml
	$(WAYLAND_SCANNER) private-code < $< > $@

$(XDGHDR): $(XDGXML)
	$(WAYLAND_SCANNER) client-header < $< > $@

$(XDGCOD): $(XDGXML)
	$(WAYLAND_SCANNER) private-code < $< > $@

main.o: main.c bar.h config.h $(WLHEADER)
bar.o: bar.c bar.h config.h $(WLHEADER)
config.o: config.c config.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f wlstatus *.o $(WLHEADER) $(WLCODE) $(XDGHDR) $(XDGCOD)

install: wlstatus
	install -Dm755 wlstatus $(DESTDIR)$(PREFIX)/bin/wlstatus
	install -Dm755 scripts/bar-update $(DESTDIR)$(PREFIX)/bin/wlstatus-update

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wlstatus

.PHONY: clean install uninstall
