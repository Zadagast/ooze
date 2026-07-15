# Ooze

[![CI](https://github.com/Zadagast/ooze/actions/workflows/ci.yml/badge.svg)](https://github.com/Zadagast/ooze/actions/workflows/ci.yml)
[![AppImage](https://github.com/Zadagast/ooze/actions/workflows/appimage.yml/badge.svg)](https://github.com/Zadagast/ooze/actions/workflows/appimage.yml)
[![Website](https://github.com/Zadagast/ooze/actions/workflows/pages.yml/badge.svg)](https://zadagast.github.io/ooze/)

An Aqua-inspired Wayland desktop built on [Mutter](https://gitlab.gnome.org/GNOME/mutter), with its own file manager, terminal, and system apps that all share one look.

![Ooze desktop demo](docs/ooze-desktop-demo.gif)

[Full-quality video](docs/ooze-desktop-demo.mp4) · [Website](https://zadagast.github.io/ooze/)

## Highlights

- Global menu bar, floating dock, desktop icons, and StatusNotifier / AppIndicator tray
- One-click light / dark mode for the whole desktop
- Compositor session lock (PAM) for native Wayland sessions
- First-party apps: files (Spot), terminal (Command), image viewer (Eye), and settings (King), including Default Applications
- Native screen-share via the first-party Ooze ScreenCast portal; share a monitor from Flatpak Discord or browser `getDisplayMedia` (window share and screenshots to come)
- Bundled WhiteSur themes and elementary icons; no hunting for files
- Runs any Wayland or X11 app via Xwayland
- Stable `.deb` is the desktop you log into; iterate with `./run-devkit.sh` in a nested window — see [DEVELOPING.md](DEVELOPING.md)

| Light | Dark |
| --- | --- |
| ![Light](docs/ooze-light.png) | ![Dark](docs/ooze-dark.png) |

## Try it

Grab the AppImage or `.deb` from [Releases](https://github.com/Zadagast/ooze/releases) — the AppImage runs as a nested window on any Wayland desktop:

```sh
chmod +x Ooze-*.AppImage && ./Ooze-*.AppImage
```

```sh
sudo apt install ./ooze_0.6.0_amd64.deb
```

Or build from source (needs Mutter 18 dev packages — Ubuntu 26.04):

```sh
sudo apt install meson ninja-build libmutter-18-dev mutter-dev-bin libgtk-4-dev libvte-2.91-gtk4-dev
./run-devkit.sh   # builds and launches a nested Ooze session
```

See [docs/BUILDING.md](docs/BUILDING.md) for the full dependency list and packaging (`.deb`, AppImage).

## License

Compositor pieces inherit upstream Mutter licensing; first-party Ooze code is part of the same project unless otherwise noted.
