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
    LUA_PKG := lua5.4
    # Standard Linux pkg-config paths
    PKG_CONFIG_PATH := /usr/lib/pkgconfig:/usr/share/pkgconfig
endif

# Export for the shell commands below
export PKG_CONFIG_PATH

# Build flags
CXXFLAGS := $(CXXFLAGS) $(STD) $(WARN) $(EXTRA_FLAGS) \
    $(shell pkg-config --cflags x11 cairo pangocairo $(LUA_PKG))

LDLIBS := $(EXTRA_FLAGS) \
    $(shell pkg-config --libs x11 cairo pangocairo $(LUA_PKG)) \
    -lm -lXext -lXcomposite

OBJS := build/main.o build/bar.o build/lua_plugin.o

build/%.o: %.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

orbit-status: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -rf orbit-status build

install: orbit-status
	install -Dm755 orbit-status $(DESTDIR)$(PREFIX)/bin/orbit-status
	install -Dm755 scripts/bar-update $(DESTDIR)$(PREFIX)/bin/orbit-status-update
	install -d $(PLUGINS_DIR)
	install -m644 plugins/*.lua $(PLUGINS_DIR)/ 2>/dev/null || true

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/orbit-status
	rm -rf $(DESTDIR)$(PREFIX)/share/orbit-status/plugins

.PHONY: clean install uninstall
