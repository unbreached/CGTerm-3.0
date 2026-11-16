PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/cgterm/assets
BUILD_DIR ?= build
OBJDIR ?= $(BUILD_DIR)/obj
BIN_LOCAL_DIR ?= bin
ASSETDIR := assets
SRCDIR := src
EXESUFFIX ?=
SOCKETLIBS ?=
STRIP ?= strip
INSTALL ?= install
MKDIR_P ?= mkdir -p
RM ?= rm -f

# For Cygwin / MinGW you may set EXESUFFIX=.exe
# EXESUFFIX=.exe

CC ?= gcc

UNAME_S := $(shell uname -s)

# Native MinGW / MSYS2 build: the sources select the Windows code paths with
# -DWINDOWS (winsock, GetTempPath, SDL_mixer music). Without this the Unix
# paths (readlink, libopenmpt) were compiled and the build failed.
ifneq (,$(findstring MINGW,$(UNAME_S))$(findstring MSYS,$(UNAME_S)))
  WINDOWS_BUILD := 1
  EXESUFFIX := .exe
  SOCKETLIBS := -lws2_32
  PLATFORM_CFLAGS := -DWINDOWS -DHAVE_SDL_MIXER
  OPENMPT_LIBS := -lSDL_mixer   # music.c's Windows build uses SDL_mixer, not libopenmpt
else
  WINDOWS_BUILD := 0
  PLATFORM_CFLAGS :=
  # libopenmpt via pkg-config when available, else assume default paths
  OPENMPT_CFLAGS := $(shell pkg-config --cflags libopenmpt 2>/dev/null)
  OPENMPT_LIBS := $(shell pkg-config --libs libopenmpt 2>/dev/null)
  ifeq ($(OPENMPT_LIBS),)
    OPENMPT_LIBS := -lopenmpt
  endif
endif

ifeq ($(shell sdl-config --version 2>/dev/null),)
  $(error sdl-config not found: install SDL 1.2 / sdl12-compat development files)
endif

# Music is played through libopenmpt (see OPENMPT_* above). The old
# SDL_mixer switch is gone: it was hard-wired off and printed "music support
# disabled" on every build, which was not true.
SDL_MIXER_CFLAGS :=
SDL_MIXER_LDFLAGS :=

# User-overridable optimization/warning flags.
CFLAGS ?= -O2 -Wall
LDFLAGS ?=

# Security hardening for this network-facing parser (native builds). Disable
# with `make HARDEN_CFLAGS= HARDEN_LDFLAGS=` if your toolchain lacks support.
HARDEN_CFLAGS ?= -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE
# macOS links PIE by default and warns on an explicit -pie; only pass it on Linux.
ifeq ($(UNAME_S),Darwin)
HARDEN_LDFLAGS ?=
else ifeq ($(UNAME_S),Linux)
# full RELRO (GOT read-only after startup) and a non-executable stack
HARDEN_LDFLAGS ?= -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack
else
HARDEN_LDFLAGS ?= -pie
endif

# Mandatory flags — always applied even when CFLAGS/LDFLAGS are overridden in the
# environment, otherwise the include path, -DPREFIX and the SDL/openmpt libs
# would be silently dropped and the build would break.
SDL_CFLAGS := $(shell sdl-config --cflags)
SDL_LIBS := $(shell sdl-config --libs)
ifeq ($(WINDOWS_BUILD),1)
  # MSYS2: sdl-config (sdl12-compat) prints POSIX paths such as
  # -I/mingw64/include/SDL. mingw32-make starts the native compiler
  # directly, with no MSYS path translation, and the compiler then cannot
  # find SDL.h. Rewrite every -I/... and -L/... to its Windows form
  # (-ID:/msys64/mingw64/include/SDL) with cygpath; other words pass through.
  # The case patterns carry an opening bracket so the brackets balance:
  # make would otherwise end $(shell ...) at the first lone closing one.
  winpath = $(shell for f in $(1); do case "$$f" in (-I/*|-L/*) printf '%s%s ' "$$(printf '%s' "$$f" | cut -c1-2)" "$$(cygpath -m "$$(printf '%s' "$$f" | cut -c3-)")";; (*) printf '%s ' "$$f";; esac; done)
  ifneq ($(shell cygpath -m / 2>/dev/null),)
    SDL_CFLAGS := $(call winpath,$(SDL_CFLAGS))
    SDL_LIBS := $(call winpath,$(SDL_LIBS))
  endif
endif
REQUIRED_CFLAGS := $(SDL_CFLAGS) -DPREFIX=\"$(PREFIX)\" -DCGTERM_DATADIR=\"$(DATADIR)\" -I$(SRCDIR) $(PLATFORM_CFLAGS) $(SDL_MIXER_CFLAGS) $(OPENMPT_CFLAGS) -I/opt/homebrew/include
REQUIRED_LDFLAGS := $(SDL_LIBS) $(SOCKETLIBS) $(SDL_MIXER_LDFLAGS) -L/opt/homebrew/lib $(OPENMPT_LIBS) -lm

# -MMD -MP emit per-object .d header-dependency files (see -include below) so a
# changed .h triggers a rebuild of the objects that use it.
# The hardening flags are repeated at link time: MinGW only pulls in libssp
# (__stack_chk_fail) when gcc sees -fstack-protector on the link line.
ALL_CFLAGS := $(CFLAGS) $(HARDEN_CFLAGS) $(REQUIRED_CFLAGS) -MMD -MP
ALL_LDFLAGS := $(HARDEN_CFLAGS) $(HARDEN_LDFLAGS) $(LDFLAGS) $(REQUIRED_LDFLAGS)

COMMON_SRCS := \
	kernal.c \
	gfx.c \
	net.c \
	config.c \
	paths.c \
	keyboard.c \
	menu.c \
	font.c \
	timer.c \
	crc.c \
	sound.c \
	macro.c \
	clipboard.c \
	modem.c \
	music.c \
	music_preload.c \
	ansi.c \
	cp437font.c \
	login.c \
	ui.c

TERM_SRCS := \
	xfer.c \
	xmodem.c \
	punter.c \
	rainbow.c \
	zmodem.c \
	diskimage.c \
	dir.c \
	fileselector.c \
	session.c \
	ui_term.c

CHAT_SRCS := \
	chat.c \
	status.c \
	ui_chat.c

EDIT_SRCS := \
	ui_edit.c \
	fileselector.c \
	dir.c \
	diskimage.c

COMMON_OBJS := $(addprefix $(OBJDIR)/,$(COMMON_SRCS:.c=.o))
TERM_OBJS := $(addprefix $(OBJDIR)/,$(TERM_SRCS:.c=.o))
CHAT_OBJS := $(addprefix $(OBJDIR)/,$(CHAT_SRCS:.c=.o))
EDIT_OBJS := $(addprefix $(OBJDIR)/,$(EDIT_SRCS:.c=.o))

TARGETS := \
	$(BIN_LOCAL_DIR)/cgterm$(EXESUFFIX) \
	$(BIN_LOCAL_DIR)/cgchat$(EXESUFFIX) \
	$(BIN_LOCAL_DIR)/cgedit$(EXESUFFIX) \
	$(BIN_LOCAL_DIR)/testkbd$(EXESUFFIX)

.PHONY: all clean distclean uninstall install installdirs assets info test test-asan check fuzz dist-src checksums

all: $(TARGETS)

info:
	@echo "PREFIX=$(PREFIX)"
	@echo "BINDIR=$(BINDIR)"
	@echo "DATADIR=$(DATADIR)"
	@echo "CC=$(CC)"

$(OBJDIR):
	$(MKDIR_P) $(OBJDIR)

$(BIN_LOCAL_DIR):
	$(MKDIR_P) $(BIN_LOCAL_DIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# pull in auto-generated header dependencies (.d files from -MMD)
-include $(wildcard $(OBJDIR)/*.d)

$(BIN_LOCAL_DIR)/cgterm$(EXESUFFIX): $(OBJDIR)/cgterm.o $(COMMON_OBJS) $(TERM_OBJS) | $(BIN_LOCAL_DIR)
	$(CC) -o $@ $^ $(ALL_LDFLAGS)
ifeq ($(HAVE_SDL_MIXER),1)
	@# macOS: copy libmikmod next to binary (dlopen can't find it otherwise due to SIP)
	@case "$$(uname)" in Darwin) \
		for lib in /opt/homebrew/lib/libmikmod.3.dylib /usr/local/lib/libmikmod.3.dylib; do \
			if [ -f "$$lib" ]; then cp "$$lib" $(BIN_LOCAL_DIR)/libmikmod.dylib 2>/dev/null; break; fi; \
		done ;; esac
endif

$(BIN_LOCAL_DIR)/cgchat$(EXESUFFIX): $(OBJDIR)/cgchat.o $(COMMON_OBJS) $(CHAT_OBJS) | $(BIN_LOCAL_DIR)
	$(CC) -o $@ $^ $(ALL_LDFLAGS)

$(BIN_LOCAL_DIR)/cgedit$(EXESUFFIX): $(OBJDIR)/cgedit.o $(COMMON_OBJS) $(EDIT_OBJS) | $(BIN_LOCAL_DIR)
	$(CC) -o $@ $^ $(ALL_LDFLAGS)

$(BIN_LOCAL_DIR)/testkbd$(EXESUFFIX): $(OBJDIR)/testkbd.o | $(BIN_LOCAL_DIR)
	$(CC) -o $@ $^ $(ALL_LDFLAGS)

assets:
	@echo "Assets live in $(ASSETDIR)"

installdirs:
	$(MKDIR_P) $(DESTDIR)$(BINDIR)
	$(MKDIR_P) $(DESTDIR)$(DATADIR)

install: all installdirs
	@if [ -x "$(BIN_LOCAL_DIR)/cgterm$(EXESUFFIX)" ]; then $(STRIP) $(BIN_LOCAL_DIR)/cgterm$(EXESUFFIX) || true; fi
	@if [ -x "$(BIN_LOCAL_DIR)/cgchat$(EXESUFFIX)" ]; then $(STRIP) $(BIN_LOCAL_DIR)/cgchat$(EXESUFFIX) || true; fi
	@if [ -x "$(BIN_LOCAL_DIR)/cgedit$(EXESUFFIX)" ]; then $(STRIP) $(BIN_LOCAL_DIR)/cgedit$(EXESUFFIX) || true; fi
	$(INSTALL) -m 0755 $(BIN_LOCAL_DIR)/cgterm$(EXESUFFIX) $(DESTDIR)$(BINDIR)/cgterm$(EXESUFFIX)
	$(INSTALL) -m 0755 $(BIN_LOCAL_DIR)/cgchat$(EXESUFFIX) $(DESTDIR)$(BINDIR)/cgchat$(EXESUFFIX)
	$(INSTALL) -m 0755 $(BIN_LOCAL_DIR)/cgedit$(EXESUFFIX) $(DESTDIR)$(BINDIR)/cgedit$(EXESUFFIX)
	$(INSTALL) -m 0644 $(ASSETDIR)/*.bmp $(DESTDIR)$(DATADIR)/
	$(INSTALL) -m 0644 $(ASSETDIR)/*.kbd $(DESTDIR)$(DATADIR)/
	$(INSTALL) -m 0644 $(ASSETDIR)/*.wav $(DESTDIR)$(DATADIR)/
	@if ls $(ASSETDIR)/*.ans >/dev/null 2>&1; then $(INSTALL) -m 0644 $(ASSETDIR)/*.ans $(DESTDIR)$(DATADIR)/; fi
	@if ls $(ASSETDIR)/*.xm >/dev/null 2>&1; then $(INSTALL) -m 0644 $(ASSETDIR)/*.xm $(DESTDIR)$(DATADIR)/; fi
	@if ls $(ASSETDIR)/*.mod >/dev/null 2>&1; then $(INSTALL) -m 0644 $(ASSETDIR)/*.mod $(DESTDIR)$(DATADIR)/; fi
	@if ls $(ASSETDIR)/*.txt >/dev/null 2>&1; then $(INSTALL) -m 0644 $(ASSETDIR)/*.txt $(DESTDIR)$(DATADIR)/; fi
	$(MKDIR_P) $(DESTDIR)$(DATADIR)/fonts
	$(INSTALL) -m 0644 $(ASSETDIR)/fonts/*.bmp $(DESTDIR)$(DATADIR)/fonts/
	$(INSTALL) -m 0644 $(ASSETDIR)/fonts/LICENSES.txt $(DESTDIR)$(DATADIR)/fonts/

# 'clean' keeps ./dist: that is where package-macos.sh and cross-compile-win.sh
# leave the release zips. Use 'distclean' to remove those too.
clean:
	$(RM) -r $(OBJDIR)
	$(RM) $(TARGETS)

distclean: clean
	$(RM) -r ./dist

# Regression harnesses (no window, no network): disk-image geometry/BAM/
# directory logic, and the XMODEM/Punter state machines against a scripted
# peer with a virtual clock. They write scratch files under /tmp.
# SAN is empty for the normal run; `make test-asan` fills it in.
SAN ?=
TEST_CFLAGS := -O1 -g -Wall $(SAN) $(REQUIRED_CFLAGS)

# The same suites under AddressSanitizer + UndefinedBehaviorSanitizer.
# Meant for Linux and CI: on recent macOS a sanitized binary that links the
# SDL/Cocoa libraries can hang in the dynamic loader before main() runs.
test-asan:
	$(MAKE) test SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"

check: test

test: | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) -o $(OBJDIR)/test_diskimage tests/test_diskimage.c $(SRCDIR)/diskimage.c $(SRCDIR)/dir.c $(ALL_LDFLAGS)
	$(CC) $(TEST_CFLAGS) -o $(OBJDIR)/test_xfer tests/test_xfer.c $(SRCDIR)/xfer.c $(SRCDIR)/xmodem.c $(SRCDIR)/punter.c $(SRCDIR)/rainbow.c $(SRCDIR)/zmodem.c $(SRCDIR)/crc.c $(SRCDIR)/diskimage.c $(SRCDIR)/dir.c $(ALL_LDFLAGS)
	$(CC) $(TEST_CFLAGS) -o $(OBJDIR)/test_ansi tests/test_ansi.c $(SRCDIR)/ansi.c $(SRCDIR)/gfx.c $(SRCDIR)/font.c $(SRCDIR)/cp437font.c $(SRCDIR)/config.c $(SRCDIR)/paths.c $(SRCDIR)/menu.c $(SRCDIR)/kernal.c $(ALL_LDFLAGS)
	$(OBJDIR)/test_diskimage
	$(OBJDIR)/test_xfer
	CGTERM_ASSET_DIR=assets SDL_VIDEODRIVER=dummy $(OBJDIR)/test_ansi
	$(CC) $(TEST_CFLAGS) -o $(OBJDIR)/test_zmodem tests/test_zmodem.c $(SRCDIR)/zmodem.c $(SRCDIR)/crc.c $(ALL_LDFLAGS)
	@if command -v sz >/dev/null 2>&1 || [ -x /opt/homebrew/bin/sz ]; then \
	  PATH="/opt/homebrew/bin:$$PATH" $(OBJDIR)/test_zmodem; \
	else echo "lrzsz (sz/rz) not installed - skipping the ZMODEM interop test"; fi

# Mutation fuzzing of the disk-image parser with the sanitizers on.
# FUZZ_ITER=20000 make fuzz   for a longer run; FUZZ_TYPE=d81 for D81.
FUZZ_ITER ?= 2000
FUZZ_TYPE ?= d64
fuzz: | $(OBJDIR)
	$(CC) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(REQUIRED_CFLAGS) -o $(OBJDIR)/fuzz_diskimage tests/fuzz_diskimage.c $(SRCDIR)/diskimage.c $(SRCDIR)/dir.c -lm
	$(OBJDIR)/fuzz_diskimage $(FUZZ_ITER) $(FUZZ_TYPE)

# Source tarball exactly as released: tracked files only, minus the paths
# marked export-ignore in .gitattributes, plus a SHA256SUMS for everything
# currently in dist/.
dist-src:
	mkdir -p dist
	git archive --format=tar.gz --prefix=CGTerm-3.0/ -o dist/CGTerm-3.0-source.tar.gz HEAD
	@echo "[+] dist/CGTerm-3.0-source.tar.gz"

checksums:
	cd dist && shasum -a 256 CGTerm-3.0-*.zip CGTerm-3.0-*.tar.gz > SHA256SUMS && cat SHA256SUMS

uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/cgterm$(EXESUFFIX) $(DESTDIR)$(BINDIR)/cgchat$(EXESUFFIX) $(DESTDIR)$(BINDIR)/cgedit$(EXESUFFIX)
	$(RM) -r $(DESTDIR)$(DATADIR)


