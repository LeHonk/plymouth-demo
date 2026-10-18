# plymouth-demo

A base for old-school demoscene boot splashes (plasma, copper bars, starfields, sine scrollers) built as a **native Plymouth splash plugin**. You write effects against a tiny framebuffer API. The core handles Plymouth, scaling, boot events and password prompts.

Each demo is a **git fork** of this repo.

## Quick start

```sh
make preview     # SDL window, fastest edit/run loop (needs libsdl2-dev)
make run         # real plymouthd in a QEMU window (host kernel + 4 MB initramfs)
make debug       # same, plymouthd debug log on this terminal
```

`make run` needs no root and no disk image. It packs busybox, plymouthd, the plugin and the bochs DRM modules into an initramfs, boots `/boot/vmlinuz-$(uname -r)`, plays fake boot messages for `RUN_SECONDS` and powers off. Press Ctrl-A X in the terminal (or close the window) to quit early.

Useful variations:

```sh
make run APPEND=demo.password   # pop up a LUKS-style password prompt after 3 s
make run KVER=7.2.9+deb14-amd64 # a different installed kernel
make run RUN_SECONDS=0          # run forever
```

### Real boot in a VM

```sh
sudo apt install libguestfs-tools
make vm          # once: Debian 13 cloud image + plymouth + toolchain
make run-vm      # copy tree in, build + install there, boot through GRUB
make clean-vm    # delete the VM image (make clean leaves it alone)
```

`run-vm` builds the plugin **inside** the VM, because the plugin ABI follows the VM's plymouth version. It also holds the splash for `HOLD_SECONDS` (default 15) so the boot doesn't blow past it. To reuse an existing Debian qcow2 instead of downloading one, run `make vm BASE_IMAGE=/path/to/image.qcow2`.

### PXE live image

A small Debian 13 live system for network boot, with the demo as its boot splash. It's built rootless with `mmdebstrap`.

```sh
sudo apt install mmdebstrap squashfs-tools
make pxe PXE_URL=http://my-server/demo   # -> build/pxe/out/
make run-pxe                             # network-boot it in QEMU (iPXE, TFTP + HTTP)
make clean-pxe                           # delete the image and the apt cache
```

`build/pxe/out/` contains `vmlinuz` (12 MB), `initrd.img` (29 MB), `filesystem.squashfs` (88 MB) and a `boot.ipxe` that loads them from `PXE_URL`. Copy them to an HTTP server, then chain-load `boot.ipxe` from your DHCP/TFTP setup. The kernel command line is `boot=live fetch=<url>/filesystem.squashfs ip=dhcp quiet splash plymouth.ignore-serial-consoles`. The splash starts in the initrd, so it covers the squashfs download. iPXE fetches the kernel and initrd before that, so those show iPXE's own text screen.

The booted system has root on a RAM overlay, DHCP via systemd-networkd, and a root console login with no password.

How it stays small:
- The plugin is compiled in a separate throwaway build chroot against the image's own plymouth, so the image carries no compiler or `-dev` packages.
- Recommends, docs, man pages and locales are left out.
- The initrd uses `MODULES=netboot` and xz compression. The splash in the initrd runs on the kernel's built-in EFI/VESA framebuffer, and the full GPU drivers load later from the root.
- Kernel modules a diskless demo box doesn't need are removed: wireless, Bluetooth, media, sound, infiniband and similar. So are amdgpu, radeon, nouveau and xe, which need firmware packages the image doesn't ship. Loaded without firmware, they replace the working EFI framebuffer with a black screen.

Settings, all passed to `make pxe`:
- `PXE_PRUNE_MODULES`: subtrees of `kernel/` to remove. Set it to `""` to keep every module, and add e.g. `firmware-amd-graphics` to the package list in `tools/mkpxe.sh` if you need AMD KMS.
- `PXE_INITRD_MODULES`: initramfs-tools `MODULES=` value.
- `PXE_SUITE`, `PXE_MIRROR`: Debian release and mirror.

Downloaded packages are cached in `build/pxe/apt-cache`, so rebuilds take under a minute.

### Installing on a real machine

```sh
sudo apt install libplymouth-dev   # optional, see below
make && sudo make install
sudo plymouth-set-default-theme -R demo-base
```

Without `libplymouth-dev`, `make` downloads the headers into `build/plydev` with `apt-get download`, so building needs no root. The plugin doesn't link against libply; its symbols resolve against plymouthd when it is loaded.

## Making a new demo (fork workflow)

```sh
tools/new-demo.sh fire ~/dev/demo-fire   # clone, upstream remote, rename
cd ~/dev/demo-fire
$EDITOR demo/demo.c
make preview
```

Or by hand: `git clone` this repo, `git remote rename origin upstream`, `git config pull.rebase false`, and set `DEMO_NAME` in `config.mk`.

To bring in later fixes from the base, run `git pull upstream main` in the fork. Forks merge rather than rebase, so their own commits keep their history. Without the `pull.rebase` setting, git stops with "Need to specify how to reconcile divergent branches" once both sides have commits. In a fork made before `new-demo.sh` set it, use `git pull --no-rebase upstream main` or run the `git config` line once.

**Keep to the ownership split** so that `git pull upstream main` merges cleanly:

| Path | Owner | What |
|---|---|---|
| `config.mk` | fork | name, title, render size, FPS, test run time |
| `demo/` | fork | your effects: any `*.c` here is compiled in |
| `assets/` | fork | files installed into the theme dir (`ctx->theme_dir`) |
| `core/`, `tools/`, `theme/`, `preview/`, `Makefile` | upstream | improve these here, then pull into forks |

`DEMO_NAME` names the `.so`, the theme directory and `ModuleName`, so different demos can be installed side by side.

## Demo API

See `core/demo.h`. A demo implements four functions:

```c
void *demo_init (const demo_ctx_t *ctx);                           /* allocate state */
void  demo_frame (void *state, const demo_ctx_t *ctx, surface_t *fb); /* draw one frame */
void  demo_key (void *state, const char *key);                     /* keypress (optional) */
void  demo_shutdown (void *state);
```

`demo_ctx_t` gives you:
- `width`/`height`
- `t` (seconds) and `frame`
- `boot_progress` (0..1)
- the latest `status`/`message` text from the boot
- `theme_dir`
- `mode` (boot, shutdown, reboot or updates)

`fb` is a 32-bit `0xAARRGGBB` surface. It keeps its contents between frames, so feedback effects work. Draw on it directly (`fb->px[y * fb->pitch + x]`) or with the helpers:

- `core/gfx.h`: `gfx_pset`, `hline`, `rect`, `line`, `blit`, `mix`, `add`, and 256-colour palettes (`gfx_palette_ramp`, `gfx_indexed_to_surface`)
- `core/font8x8.h`: built-in VGA 8×8 font, `font_draw`, `font_draw_centered`
- `core/dmath.h`: `dsin`/`dcos` lookup (1024 steps per circle, Q15), 16.16 fixed point, `drand` xorshift

The render surface is `RENDER_W`×`RENDER_H` (default 320×200), integer-scaled and letterboxed onto each display. Set both to 0 to render at native resolution. Password and question prompts are drawn on top of your frame by `core/overlay.c`, so encrypted-disk boots keep working whatever the demo does.

All values in `config.mk` end up in the generated `.plymouth` file (`[demo]` group). You can also tweak an installed theme there without rebuilding.

## Layout

```
config.mk              per-demo settings
core/plugin.c          Plymouth glue: the only file that includes ply-* headers
core/{gfx,font8x8,dmath,overlay}.[ch]
demo/demo.c            example effects
theme/demo.plymouth.in theme file template
preview/sdl_main.c     SDL previewer
tools/                 initramfs builder, QEMU/VM runners, new-demo helper
```

## Troubleshooting

- **Text/details screen instead of the demo**: run `make debug`. The log shows which theme and renderer plymouthd picked. Lines from the plugin are tagged `core/plugin.c`.
- **`cannot read /boot/vmlinuz-…`**: some distros make the kernel root-only. Copy it somewhere readable and point `KVER` at a kernel you can read, or `chmod` it.
- **Plugin builds but plymouthd won't load it on another distro**: rebuild there. Plymouth has no stable plugin ABI. The source compiles against plymouth 22.x through 26.x.
