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
CFLAGS ?= -O2 -Wall $(shell sdl-config --cflags) -DPREFIX=\"$(PREFIX)\" -I$(SRCDIR)
LDFLAGS ?= $(shell sdl-config --libs) $(SOCKETLIBS)

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
	ui.c

TERM_SRCS := \
	xfer.c \
	xmodem.c \
	punter.c \
	rainbow.c \
	diskimage.c \
	dir.c \
	fileselector.c \
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

.PHONY: all clean install installdirs assets info

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
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_LOCAL_DIR)/cgterm$(EXESUFFIX): $(OBJDIR)/cgterm.o $(COMMON_OBJS) $(TERM_OBJS) | $(BIN_LOCAL_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BIN_LOCAL_DIR)/cgchat$(EXESUFFIX): $(OBJDIR)/cgchat.o $(COMMON_OBJS) $(CHAT_OBJS) | $(BIN_LOCAL_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BIN_LOCAL_DIR)/cgedit$(EXESUFFIX): $(OBJDIR)/cgedit.o $(COMMON_OBJS) $(EDIT_OBJS) | $(BIN_LOCAL_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BIN_LOCAL_DIR)/testkbd$(EXESUFFIX): $(OBJDIR)/testkbd.o | $(BIN_LOCAL_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)

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
	@if ls $(ASSETDIR)/*.txt >/dev/null 2>&1; then $(INSTALL) -m 0644 $(ASSETDIR)/*.txt $(DESTDIR)$(DATADIR)/; fi

clean:
	$(RM) -r $(OBJDIR)
	$(RM) -r ./dist
	$(RM) $(TARGETS)


