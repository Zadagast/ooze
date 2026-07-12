# Ooze

**Ooze** is a Wayland desktop environment built on [Mutter](https://gitlab.gnome.org/GNOME/mutter). It pairs a cohesive Aqua-inspired shell — menu bar, dock, and **Ooze Gel** window dressing — with first-party GTK4 applications that share one visual system.

![Ooze light/dark and magic lamp](docs/ooze-theme-demo.gif)

*Nested Ooze session: light/dark swap and magic-lamp minimize (Spot + Ooze Command).*

---

## Overview

| Component | Description |
| --- | --- |
| **Shell** | Global menu bar, dock, desktop icons, system appearance |
| **Spot** | File manager with sidebar, column, and grid views |
| **Ooze Command** | Terminal with matching Gel chrome and global menu support |
| **Ooze Gel** | App window dressing — header bar, traffic lights, drag and resize |
| **OozeKit** | Shared drawing library for surfaces, pinstripes, buttons, and palette |

Shell and apps use one design language: aluminum surfaces, subtle pinstripes, custom traffic lights, and light/dark mode via `org.gnome.desktop.interface color-scheme`.

---

## Features

### Desktop shell
- Mutter-based Wayland compositor
- Global menu bar with appearance toggle
- Floating dock with Spot, Command, and other Ooze apps
- Running-app indicators with focus / minimize on dock click
- Magic-lamp minimize animation into the dock
- Desktop icons with the elementary icon theme

### Spot
- Places sidebar, toolbar, and status bar
- Column (Miller) and grid views
- Column browser rooted at the active sidebar place
- Theme-aware surfaces through OozeKit, Gel, and Adwaita

### Ooze Command
- VTE terminal
- Shared Ooze Gel header bar and traffic lights
- Application menu for the shell global menu

### Ooze Gel
- `ooze-header-bar` — titled bar with traffic lights
- `ooze-traffic-lights` — close / minimize / zoom
- `ooze-gel` — window drag and edge resize grips

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
ooze-ui/       Ooze Gel (header bar, traffic lights, drag/resize)
common/        Shared Gel / traffic-light constants
data/          Icons, desktop entries, branding
docs/          Screenshots and demo media
scripts/       Install helpers
```

---

## License

Compositor pieces inherit upstream Mutter licensing. First-party Ooze code in this repository is part of the same project unless otherwise noted.
