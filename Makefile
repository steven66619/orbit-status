PREFIX ?= /usr/local
CXX := g++
STD := -std=c++17
WARN := -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

PLUGINS_DIR := $(DESTDIR)$(PREFIX)/share/wlstatus/plugins

CXXFLAGS := $(CXXFLAGS) $(STD) $(WARN) \
	$(shell pkg-config --cflags x11 cairo pangocairo lua5.4)
LDLIBS := $(shell pkg-config --libs x11 cairo pangocairo lua5.4) -lm -lXext -lXcomposite
OBJS := build/main.o build/bar.o build/lua_plugin.o

build/%.o: %.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

wlstatus: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -rf wlstatus build

install: wlstatus
	install -Dm755 wlstatus $(DESTDIR)$(PREFIX)/bin/wlstatus
	install -Dm755 scripts/bar-update $(DESTDIR)$(PREFIX)/bin/wlstatus-update
	install -d $(PLUGINS_DIR)
	install -m644 plugins/*.lua $(PLUGINS_DIR)/ 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wlstatus
	rm -rf $(DESTDIR)$(PREFIX)/share/wlstatus/plugins

.PHONY: clean install uninstall