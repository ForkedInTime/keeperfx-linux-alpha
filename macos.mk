# macos.mk — EXPERIMENTAL macOS build spike.
#
# This is a SPIKE, not a supported target. Its job is to find out how far the
# Linux port actually gets on Darwin and to produce an honest list of what
# breaks. It is expected to fail; the failures are the deliverable.
#
# It deliberately does not touch linux.mk, Makefile or GNUmakefile, and nothing
# here is wired into any existing workflow. Build with:
#
#     make -f macos.mk
#
# Differences from linux.mk, and why:
#
#  * Architecture. linux.mk hardcodes -march=x86-64 in three places. Apple
#    Silicon is arm64, so the arch flag is chosen from uname. Note the fork has
#    been bitten by codegen before: -march=x86-64-v2 once miscompiled the
#    ariadne pathfinding and crashed at level start, so nothing more exotic
#    than the host baseline is requested here.
#
#  * -fsigned-char. Plain char signedness is platform-defined, and the engine
#    stores negative sentinels in plain char fields (game.music_track = -1
#    marks custom music; game_legacy.h alone has ~54 char fields). Forcing
#    signed char removes a whole class of silent, hard-to-find breakage rather
#    than discovering it one field at a time.
#
#  * No -ldl. On macOS dlopen lives in libSystem; there is no separate libdl.
#
#  * Homebrew prefix. /opt/homebrew on Apple Silicon, /usr/local on Intel.
#
#  * Vendored deps. linux.mk downloads prebuilt *-lin64.tar.gz archives for
#    astronomy, centijson, enet6 and libcurl. Those are Linux binaries and
#    there are no macOS equivalents published, so this expects them to have
#    been built from source into deps/ beforehand (see the spike workflow),
#    except libcurl which comes from Homebrew instead.
#
#  * Source list. Discovered by wildcard rather than duplicating linux.mk's
#    explicit list, so this file cannot drift out of sync with it. windows.cpp
#    is excluded; linux.cpp is kept, because the platform split in this tree is
#    really "Windows vs POSIX" (12 #ifndef _WIN32 against a single
#    #if defined(__linux__)), which is the main reason this spike is worth
#    running at all.

UNAME_M := $(shell uname -m)
BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /usr/local)

ifeq ($(UNAME_M),arm64)
  ARCH_FLAGS :=
else
  ARCH_FLAGS := -march=x86-64
endif

PKG_CONFIG_PATH := $(BREW_PREFIX)/lib/pkgconfig:$(BREW_PREFIX)/opt/openssl@3/lib/pkgconfig:$(BREW_PREFIX)/opt/curl/lib/pkgconfig
export PKG_CONFIG_PATH

KFX_C_SOURCES   := $(filter-out src/windows.c,$(wildcard src/*.c))
KFX_CXX_SOURCES := $(filter-out src/windows.cpp,$(wildcard src/*.cpp)) $(wildcard src/kfx/lense/*.cpp)
TOML_SOURCES    := $(wildcard deps/centitoml/*.c)

KFX_C_OBJECTS    := $(patsubst src/%.c,obj-macos/%.o,$(KFX_C_SOURCES))
KFX_CXX_OBJECTS  := $(patsubst src/%.cpp,obj-macos/%.o,$(KFX_CXX_SOURCES))
TOML_OBJECTS     := $(patsubst deps/centitoml/%.c,obj-macos/centitoml/%.o,$(TOML_SOURCES))

KFX_INCLUDES := \
	-Ideps/centijson/include \
	-Ideps/centitoml \
	-Ideps/astronomy/include \
	-Ideps/enet6/include \
	-I$(BREW_PREFIX)/include \
	$(shell pkg-config --cflags-only-I luajit 2>/dev/null) \
	$(shell pkg-config --cflags epoxy 2>/dev/null) \
	$(shell pkg-config --cflags-only-I libavformat 2>/dev/null) \
	$(shell pkg-config --cflags-only-I sdl2 2>/dev/null)

COMMON_FLAGS := -g -DDEBUG -DBFDEBUG_LEVEL=0 -O2 $(ARCH_FLAGS) -fsigned-char \
	$(KFX_INCLUDES) -Wall -Wextra -Wno-error -Wno-unused-parameter \
	-Wno-unknown-pragmas -Wno-sign-compare \
	-Wno-deprecated-declarations

KFX_CFLAGS   += $(COMMON_FLAGS) -Wno-absolute-value
KFX_CXXFLAGS += $(COMMON_FLAGS)

# Version header. linux.mk generates this from version.mk and it is NOT in git,
# so without it every translation unit dies at globals.h -> version.h before the
# compiler reaches a single line of real code. The first run of this spike hit
# exactly that and learned nothing about macOS as a result.
include version.mk
# BUILD_NUMBER and VER_SUFFIX must be set BEFORE VER_STRING expands it, or the
# version comes out as "1.4.0. " with an empty build number.
BUILD_NUMBER ?= $(shell git rev-list --count HEAD 2>/dev/null || echo 0)
VER_SUFFIX ?= macos-spike
VER_STRING := $(VER_MAJOR).$(VER_MINOR).$(VER_RELEASE).$(BUILD_NUMBER) $(VER_SUFFIX)

src/ver_defs.h: version.mk
	@echo "#define VER_MAJOR   $(VER_MAJOR)"                       >  $@
	@echo "#define VER_MINOR   $(VER_MINOR)"                       >> $@
	@echo "#define VER_RELEASE $(VER_RELEASE)"                     >> $@
	@echo "#define VER_BUILD   $(BUILD_NUMBER)"                    >> $@
	@echo "#define VER_STRING  \"$(VER_STRING)\""                  >> $@
	@echo "#define PACKAGE_SUFFIX  \"$(VER_SUFFIX)\""              >> $@
	@echo "#define GIT_REVISION  \"$(shell git describe --always 2>/dev/null || echo unknown)\"" >> $@

KFX_LDFLAGS += \
	-g -rdynamic \
	-L$(BREW_PREFIX)/lib \
	-Ldeps/astronomy -lastronomy \
	-Ldeps/centijson -ljson \
	-Ldeps/enet6 -lenet6 \
	$(shell pkg-config --libs-only-l epoxy 2>/dev/null) \
	$(shell pkg-config --libs-only-l sdl2 2>/dev/null) \
	$(shell pkg-config --libs-only-l SDL2_mixer 2>/dev/null) \
	$(shell pkg-config --libs-only-l SDL2_net 2>/dev/null) \
	$(shell pkg-config --libs-only-l SDL2_image 2>/dev/null) \
	$(shell pkg-config --libs-only-l libavformat 2>/dev/null) \
	$(shell pkg-config --libs-only-l libavcodec 2>/dev/null) \
	$(shell pkg-config --libs-only-l libswresample 2>/dev/null) \
	$(shell pkg-config --libs-only-l libswscale 2>/dev/null) \
	$(shell pkg-config --libs-only-l libavutil 2>/dev/null) \
	$(shell pkg-config --libs-only-l openal 2>/dev/null) \
	$(shell pkg-config --libs-only-l luajit 2>/dev/null) \
	$(shell pkg-config --libs-only-l libcurl 2>/dev/null) \
	-lspng -lminizip -lz -lminiupnpc -lnatpmp -lssl -lcrypto -lzstd

TOML_CFLAGS += -O2 $(ARCH_FLAGS) -fsigned-char -Ideps/centijson/include \
	-Wall -Wextra -Wno-error -Wno-unused-parameter

.PHONY: all clean probe
all: bin-macos/keeperfx

# Every object needs the generated version header. Listing it as a prerequisite
# of `all` is not enough: make does not order the prerequisites of a target, so
# under -j the first compile can start before the header exists.
$(KFX_C_OBJECTS) $(KFX_CXX_OBJECTS): src/ver_defs.h

probe:
	@echo "uname -m      : $(UNAME_M)"
	@echo "arch flags    : $(ARCH_FLAGS)"
	@echo "brew prefix   : $(BREW_PREFIX)"
	@echo "C sources     : $(words $(KFX_C_SOURCES))"
	@echo "C++ sources   : $(words $(KFX_CXX_SOURCES))"
	@echo "compiler      : $$(cc --version | head -1)"

bin-macos obj-macos obj-macos/kfx/lense obj-macos/centitoml:
	@mkdir -p $@

obj-macos/%.o: src/%.c | obj-macos
	@mkdir -p $(dir $@)
	$(CC) $(KFX_CFLAGS) -c $< -o $@

obj-macos/%.o: src/%.cpp | obj-macos obj-macos/kfx/lense
	@mkdir -p $(dir $@)
	$(CXX) $(KFX_CXXFLAGS) -c $< -o $@

obj-macos/centitoml/%.o: deps/centitoml/%.c | obj-macos/centitoml
	$(CC) $(TOML_CFLAGS) -c $< -o $@

bin-macos/keeperfx: $(KFX_C_OBJECTS) $(KFX_CXX_OBJECTS) $(TOML_OBJECTS) | bin-macos
	$(CXX) -o $@ $^ $(KFX_LDFLAGS)

clean:
	rm -rf obj-macos bin-macos
