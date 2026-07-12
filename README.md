# Ooze

**Ooze** is a Wayland desktop environment built on [Mutter](https://gitlab.gnome.org/GNOME/mutter). It pairs a cohesive Aqua-inspired shell — menu bar, dock, and traffic-light window chrome — with first-party GTK4 applications that share one visual system.

![Ooze light and dark mode](docs/ooze-theme-demo.gif)

*Light and dark mode with Spot and Ooze Command.*

---

## Overview

| Component | Description |
| --- | --- |
| **Shell** | Global menu bar, dock, desktop icons, system appearance |
| **Spot** | File manager with sidebar, column, and grid views |
| **Ooze Command** | Terminal with matching chrome and global menu support |
| **OozeKit** | Shared drawing library for surfaces, pinstripes, buttons, and palette |

Shell and apps use one design language: aluminum surfaces, subtle pinstripes, custom traffic lights, and light/dark mode via `org.gnome.desktop.interface color-scheme`.

---

## Features

### Desktop shell
- Mutter-based Wayland compositor
- Global menu bar with appearance toggle
- Floating dock with Spot and Ooze Command
- Running-app indicators
- Desktop icons with the elementary icon theme

### Spot
- Places sidebar, toolbar, and status bar
- Column (Miller) and grid views
- Column browser rooted at the active sidebar place
- Theme-aware chrome through OozeKit and Adwaita

### Ooze Command
- VTE terminal
- Shared header bar and traffic lights
- Application menu for the shell global menu

### OozeKit
- `ooze-palette` — light and dark color tables
- `ooze-draw` — surfaces, pinstripes, separators, button fills
- `ooze-surface` — header, toolbar, sidebar, and status bar widgets
- `ooze-button` — toolbar and push-button chrome

---

## Screenshots

| Dark | Light |
| --- | --- |
| ![Dark mode](docs/ooze-dark.png) | ![Light mode](docs/ooze-light.png) |

---

## Build

**Dependencies (Debian/Ubuntu):**

- `meson`, `ninja-build`, `pkg-config`
- Mutter 18 development packages (`libmutter-18-dev` and related)
- `libgtk-4-dev`, `libadwaita-1-dev`, `libcairo2-dev`
- `libvte-2.91-gtk4-dev` (Ooze Command)
- `libgdk-pixbuf-2.0-dev`, `libpng-dev`

```bash
meson setup build
ninja -C build
```

---

## Run

```bash
./run-devkit.sh
```

Launches a nested Mutter session with:

- `build/` on `PATH`
- Project-local GSettings (does not modify your user dconf)
- Elementary icons under `data/` (fetched on first build if needed)

Toggle light and dark from the **Ooze** menu. Spot and Ooze Command follow automatically.

For live rebuilds during development:

```bash
./watch-devkit.sh
```

---

## Repository layout

```
src/           Compositor shell (panel, dock, theme, menus, desktop icons)
spot/          Spot file manager
ooze-command/  Terminal
ooze-kit/      Shared drawing toolkit
ooze-ui/       Window chrome (header bar, traffic lights)
common/        Shared chrome constants
data/          Icons, desktop entries, branding
docs/          Screenshots and demo media
scripts/       Install helpers
```

---

## License

Compositor pieces inherit upstream Mutter licensing. First-party Ooze code in this repository is part of the same project unless otherwise noted.
