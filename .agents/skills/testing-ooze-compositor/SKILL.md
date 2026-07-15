---
name: testing-ooze-compositor
description: Build and run the Ooze compositor (a Mutter 18 plugin) for testing on a VM that does not have Mutter 18 / Ubuntu 26.04. Use when verifying compositor/shell changes (dock, panel, window chrome, global menu, window tracker, lock screen) in Zadagast/ooze.
---

# Testing the Ooze compositor

Ooze is a **Mutter 18 plugin** (the "glue"; Mutter is the engine). It targets
**Ubuntu 26.04 + Mutter 18**. Dev VMs are usually Ubuntu 22.04 with only Mutter 10
headers, so you **cannot build or run it directly** — build and run it inside an
**Ubuntu 26.04 Docker container** instead. This saves a lot of setup time.

## Why not build locally
- `meson.build` requires `libmutter-18`, `mutter-clutter-18`, `mutter-cogl-18`,
  `mutter-mtk-18`. On the 22.04 VM only Mutter 10 headers exist, and the plugin
  files include `mtk/mtk.h`, which Mutter 10 lacks → syntax checks fail.
- The distro `meson` (0.61) is too old; `meson.build` needs `>= 1.0.0`.
- The authoritative compile is CI's "Compile (Ubuntu 26.04)" job. For local
  runtime testing, use the container below.

## One-time container setup
```bash
docker pull ubuntu:26.04
# Share the host X socket so GUI/screenshots can work if a GPU is present.
docker run -d --name oozebuild \
  -v /home/ubuntu/ooze:/ooze \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e DISPLAY=:0 --network host \
  ubuntu:26.04 sleep infinity

docker exec oozebuild bash -c "apt-get update -qq && DEBIAN_FRONTEND=noninteractive \
  apt-get install -y -qq meson ninja-build libmutter-18-dev mutter-dev-bin \
  libgtk-4-dev libvte-2.91-gtk4-dev libadwaita-1-dev dbus-x11 x11-utils \
  libpolkit-agent-1-dev libpolkit-gobject-1-dev \
  xwayland pipewire pipewire-pulse wireplumber gsettings-desktop-schemas \
  mutter-common adwaita-icon-theme fonts-dejavu-core imagemagick"
```
Notes:
- `libadwaita-1-dev` IS required to build (OozeKit apps `#include <adwaita.h>`).
  This does not violate the "no libadwaita as an Ooze app *default*" rule — it is
  a build dependency of the kit, not a new app dependency you are adding.
- `libvte-2.91-gtk4-dev` is only in 26.04, not 22.04 — another reason to use the container.

## Build
```bash
docker exec oozebuild bash -c "cd /ooze && rm -rf build-docker && \
  meson setup build-docker && ninja -C build-docker"
# run-devkit.sh expects ./build; point it at the container build:
docker exec oozebuild bash -c "cd /ooze && rm -rf build && ln -sfn build-docker build"
```
A clean build ends with `Linking target ooze-ear` (all 136 targets).

## Run (nested session)
```bash
# allow the container to talk to the host X server
DISPLAY=:0 xhost +local:
docker exec -d oozebuild bash -c '
  export XDG_RUNTIME_DIR=/tmp/xdg; mkdir -p /tmp/xdg && chmod 700 /tmp/xdg
  pipewire >/tmp/pw.log 2>&1 & wireplumber >/tmp/wp.log 2>&1 & sleep 2
  cd /ooze && DISPLAY=:0 LIBGL_ALWAYS_SOFTWARE=1 HOME=/root ./run-devkit.sh >/tmp/ooze-run.log 2>&1'
sleep 12
docker exec oozebuild tail -20 /tmp/ooze-run.log
docker exec oozebuild pgrep -af 'ooze|mutter-devkit'   # confirm processes
```
Healthy startup log lines: `Running Ooze (using mutter 50.1)`,
`Added virtual monitor Meta-0`, `Ooze SNI: StatusNotifierWatcher ready`.

## IMPORTANT display limitation
`mutter-devkit` (the nested output window, "mdk") needs a GPU. If the host has no
`/dev/dri` (common on cloud VMs — the `docker run --device /dev/dri` form fails
with "no such file or directory"), Mutter logs
`Created surfaceless renderer without GPU` and **produces no visible window**, so
`import -window root` / `wmctrl` will NOT show the Ooze desktop — you only see the
host Plasma/Chrome. Do not burn time trying to make the window appear; it can't
without a GPU device node.

### Workaround: log / instrumentation-based proof
When no GPU is available, prove compositor behavior from **logs**, not screenshots:
- `OOZE_STALL_MS=<ms> ./run-devkit.sh` → `g_warning ("OozeStall: <tag> took N ms")`
  when the main thread stalls (see `compositor/ooze-stall.c`). Use to prove
  the compositor thread is not blocked.
- Add **temporary** `g_message(...)` probes (test-only, container build, never
  committed) at the code path under test, then drive events and read
  `/tmp/ooze-run.log`. Example for the window-tracker (Tier 2.1): log in the
  dock's `ooze_dock_refresh_running_state` — an idle desktop should produce
  **zero** refreshes (proving the old 600ms poll is gone), and launching/closing
  a client should produce exactly one refresh per event (proving it is
  event-driven). A GPU-less run still creates windows on the virtual monitor, so
  `window-created`/`unmanaged` still fire.
- Drive a client into the nested Wayland session with its `WAYLAND_DISPLAY`
  (from the mdk env / `/tmp/xdg/wayland-0`), e.g. run one of the built Ooze apps
  (`build/ooze-eye`, `build/ooze-about`) against it.

## Cleanup
```bash
docker exec oozebuild pkill -f 'build/ooze' || true
```

## Devin Secrets Needed
None. Everything runs locally in the container; no external credentials required.
