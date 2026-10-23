# plymouth-demo - build, install and test a demoscene Plymouth splash
#
#   make            build build/$(DEMO_NAME).so and the theme file
#   make run        boot it in a QEMU window (host kernel + tiny initramfs)
#   make debug      same, with plymouthd debug log on this terminal
#   make preview    run the demo in an SDL window (no Plymouth, fastest loop)
#   make run-vm     real boot of a Debian VM with the theme installed
#   make clean-vm   delete the VM image and its run files (make clean keeps them)
#   make pxe        lightweight Debian live image for PXE boot (rootless mmdebstrap)
#   make run-pxe    network-boot that image in QEMU
#   make clean-pxe  delete the PXE image and its apt cache
#   make compdb     write compile_commands.json for clangd (Zed, VS Code, ...)
#   sudo make install / uninstall
#
# Per-demo settings live in config.mk.

include config.mk

BUILD     := build
SO        := $(BUILD)/$(DEMO_NAME).so
THEME     := $(BUILD)/$(DEMO_NAME).plymouth

# Plymouth headers, in order of preference:
#  1. pkg-config (libplymouth-dev plus the -dev packages its .pc file
#     requires: libevdev, xkbcommon, libudev)
#  2. the system headers in /usr/include/plymouth-1 (libplymouth-dev alone;
#     its pkg-config file fails without those extra -dev packages)
#  3. headers unpacked by `make plydev` (no root needed)
PLYDEV    := $(BUILD)/plydev
ifeq ($(shell pkg-config --exists ply-splash-core && echo yes),yes)
PLY_CFLAGS := $(shell pkg-config --cflags ply-splash-core)
PLY_LIBS   := $(shell pkg-config --libs ply-splash-core)
PLUGINDIR  := $(shell pkg-config --variable=pluginsdir ply-splash-core)
THEMESDIR  := $(shell pkg-config --variable=themesdir ply-splash-core)
else
PLYINC     := $(firstword $(wildcard /usr/include/plymouth-1) $(PLYDEV)/usr/include/plymouth-1)
PLY_CFLAGS := -I$(PLYINC)/ply -I$(PLYINC)/ply-splash-core
PLY_LIBS   :=       # symbols resolve against plymouthd at load time
PLUGINDIR  := $(shell plymouth --get-splash-plugin-path 2>/dev/null || echo /usr/lib/plymouth)
THEMESDIR  := /usr/share/plymouth/themes
endif
THEME_DIR := $(patsubst %/,%,$(THEMESDIR))/$(DEMO_NAME)

CC        ?= cc
CFLAGS    ?= -O2 -g
CFLAGS    += -std=gnu11 -Wall -Wextra -fPIC -fvisibility=hidden -Icore
LDFLAGS   += -shared

CORE_SRCS := core/gfx.c core/font8x8.c core/dmath.c core/overlay.c
DEMO_SRCS := $(wildcard demo/*.c)
PLUGIN_OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,core/plugin.c $(CORE_SRCS) $(DEMO_SRCS))

.PHONY: all clean clean-vm pxe run-pxe clean-pxe install uninstall run debug preview run-vm vm plydev initramfs compdb FORCE

all: $(SO) $(THEME)

$(BUILD)/obj/core/plugin.o: CFLAGS += $(PLY_CFLAGS) -DPLY_ENABLE_TRACING
$(BUILD)/obj/core/plugin.o: core/plugin.c | $(if $(PLYINC),$(PLYINC))

$(BUILD)/obj/%.o: %.c core/*.h config.mk Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(SO): $(PLUGIN_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(PLY_LIBS) -lm

$(THEME): theme/demo.plymouth.in config.mk
	@mkdir -p $(BUILD)
	sed -e 's|@DEMO_NAME@|$(DEMO_NAME)|g' -e 's|@DEMO_TITLE@|$(DEMO_TITLE)|g' \
	    -e 's|@DEMO_DESC@|$(DEMO_DESC)|g' -e 's|@THEME_DIR@|$(THEME_DIR)|g' \
	    -e 's|@RENDER_W@|$(RENDER_W)|g' -e 's|@RENDER_H@|$(RENDER_H)|g' \
	    -e 's|@FPS@|$(FPS)|g' $< > $@

$(PLYDEV)/usr/include/plymouth-1:
	@echo "libplymouth-dev not installed; fetching headers into $(PLYDEV) (no root needed)"
	rm -rf $(PLYDEV) && mkdir -p $(PLYDEV)
	cd $(PLYDEV) && apt-get download libplymouth-dev && dpkg-deb -x libplymouth-dev_*.deb .

plydev: $(PLYDEV)/usr/include/plymouth-1

install: all
	install -d $(DESTDIR)$(PLUGINDIR) $(DESTDIR)$(THEME_DIR)
	install -m 0644 $(SO) $(DESTDIR)$(PLUGINDIR)/$(DEMO_NAME).so
	install -m 0644 $(THEME) $(DESTDIR)$(THEME_DIR)/
	cp -r assets/. $(DESTDIR)$(THEME_DIR)/
	rm -f $(DESTDIR)$(THEME_DIR)/.gitkeep
	@echo "Installed. Activate with: sudo plymouth-set-default-theme -R $(DEMO_NAME)"

uninstall:
	rm -f $(DESTDIR)$(PLUGINDIR)/$(DEMO_NAME).so
	rm -rf $(DESTDIR)$(THEME_DIR)

# ---- QEMU: host kernel + minimal initramfs --------------------------------

KVER      ?= $(shell uname -r)
INITRAMFS := $(BUILD)/initramfs.cpio.gz

initramfs: $(INITRAMFS)

# Always repacked (about a second) so RUN_SECONDS=/KVER= overrides apply
$(INITRAMFS): $(SO) $(THEME) FORCE
	DEMO_NAME=$(DEMO_NAME) PLUGINDIR=$(PLUGINDIR) THEME_DIR=$(THEME_DIR) \
	RUN_SECONDS=$(RUN_SECONDS) KVER=$(KVER) tools/mkinitramfs.sh $(BUILD) $@

run: $(INITRAMFS)
	KVER=$(KVER) tools/run-qemu.sh $(INITRAMFS)

debug: $(INITRAMFS)
	KVER=$(KVER) DEBUG=1 tools/run-qemu.sh $(INITRAMFS)

# ---- SDL previewer ---------------------------------------------------------

PREVIEW := $(BUILD)/preview
PREVIEW_CFLAGS = -std=gnu11 -O2 -g -Wall -Wextra -Icore \
    -DRENDER_W=$(RENDER_W) -DRENDER_H=$(RENDER_H) -DFPS=$(FPS) \
    $(shell pkg-config --cflags sdl2 2>/dev/null)

$(PREVIEW): preview/sdl_main.c $(CORE_SRCS) $(DEMO_SRCS) core/*.h config.mk
	@mkdir -p $(BUILD)
	$(CC) $(PREVIEW_CFLAGS) \
	    -o $@ preview/sdl_main.c $(CORE_SRCS) $(DEMO_SRCS) \
	    $$(pkg-config --libs sdl2) -lm

preview: $(PREVIEW)
	$(PREVIEW)

# ---- clangd compilation database --------------------------------------------

# Same flags as the real build; regenerate after changing them or adding files.
# core/ and demo/ use the plugin flags, preview/ the SDL previewer flags.
compdb: compile_commands.json

compile_commands.json: Makefile config.mk FORCE | $(if $(PLYINC),$(PLYINC))
	@{ echo '['; \
	   for f in core/plugin.c $(CORE_SRCS) $(DEMO_SRCS); do \
	     printf '  {"directory": "%s", "file": "%s", "command": "%s -c %s"},\n' \
	       '$(CURDIR)' "$$f" '$(CC) $(CFLAGS) $(PLY_CFLAGS) -DPLY_ENABLE_TRACING' "$$f"; \
	   done; \
	   printf '  {"directory": "%s", "file": "%s", "command": "%s -c %s"}\n' \
	     '$(CURDIR)' preview/sdl_main.c '$(CC) $(PREVIEW_CFLAGS)' preview/sdl_main.c; \
	   echo ']'; } > $@
	@echo "Wrote $@; restart the language server to pick it up"

# ---- Full Debian VM ---------------------------------------------------------

vm:
	tools/mkvm.sh $(BUILD)/vm

run-vm:
	DEMO_NAME=$(DEMO_NAME) tools/run-vm.sh $(BUILD)/vm

# Separate from clean: the base image takes a download and a provisioning boot
clean-vm:
	rm -rf $(BUILD)/vm

# ---- PXE live image ----------------------------------------------------------

PXE_SUITE  ?= trixie
PXE_URL    ?= http://10.0.2.2:8080

pxe:
	DEMO_NAME=$(DEMO_NAME) PXE_SUITE=$(PXE_SUITE) PXE_URL=$(PXE_URL) tools/mkpxe.sh $(BUILD)/pxe

run-pxe:
	tools/run-pxe.sh $(BUILD)/pxe/out

clean-pxe:
	rm -rf $(BUILD)/pxe

clean:
	rm -rf $(BUILD)/obj $(BUILD)/initramfs $(SO) $(THEME) $(INITRAMFS) $(PREVIEW)
