# Building Ooze

## Dependencies and build

**Dependencies (Debian/Ubuntu):**

- `meson`, `ninja-build`, `pkg-config`
- Mutter 18 development packages (`libmutter-18-dev` and related)
- `libgtk-4-dev`, `libadwaita-1-dev`, `libcairo2-dev`
- `libvte-2.91-gtk4-dev` (Ooze Command)
- `libgdk-pixbuf-2.0-dev`, `libpng-dev`
- `mutter-dev-bin` (provides `/usr/libexec/mutter-devkit`, required by `./run-devkit.sh`)
- PipeWire running on the session bus (the devkit window cannot start without it)
- `fonts-noto-color-emoji` (Unicode 16) — the Ooze menu button glyph 🫟 renders as a blank box without it
- Optional: `appmenu-gtk3-module`, `appmenu-registrar` — only if debugging foreign global menus with `OOZE_FOREIGN_GLOBAL_MENU=1` (`./scripts/install-appmenu.sh`)
- Optional: WhiteSur GTK theme (foreign-app traffic lights) — `./scripts/install-whitesur-theme.sh`

```bash
meson setup build
ninja -C build
```

Elementary icons are vendored as `data/icons/elementary-icons.tar.xz` and expanded on demand (`ninja -C build elementary-icons.stamp` or first `./run-devkit.sh`).

**Ooze Torrent** needs a one-time libtransmission fetch before it is built:

```bash
./scripts/fetch-libtransmission.sh
meson setup --reconfigure build   # if build/ already exists
ninja -C build ooze-torrent
```

Requires cmake (or a portable cmake under `.cache/cmake/`), ninja/make, and system libraries for curl, libevent, openssl, libdeflate, miniupnpc, natpmp, and libb64.

## Run

```bash
./run-devkit.sh
```

Launches a nested Mutter session with:

- `build/` on `PATH`
- Project-local GSettings / XDG config under `data/xdg-config/` (does not rewrite your user dconf or host MIME defaults)
- Elementary icons under `data/` (fetched or extracted on first need)
- Xwayland enabled by default so GTK3 appmenu clients can register (pass `OOZE_NO_X11=1` to disable)

Toggle light and dark from the **Ooze** menu. First-party apps follow automatically.

For live rebuilds during development:

```bash
./watch-devkit.sh
```

### AppImage (try without installing)

Build a single-file nested demo (binaries + icons; **host Mutter 18** still required):

```bash
./scripts/build-appimage.sh
./dist/Ooze-*-x86_64.AppImage
```

Or let **GitHub Actions** build it in an Ubuntu 26.04 container:

- **Manual:** Actions → **AppImage** → Run workflow (downloads as a workflow artifact)
- **Release:** `git push origin v0.3.0` — CI builds and attaches the AppImage to that GitHub Release

The AppImage puts the compositor (`ooze`) plus Spot, Command, King, Ear, Pak, Themes, Eye, Monitor, and About on `PATH` inside the nested session. It does not replace your login desktop.

### `.deb` (native GDM session + nested tester)

Build a system package that installs under `/usr`:

```bash
./scripts/build-deb.sh
sudo apt install ./dist/ooze_0.3.0_amd64.deb
```

**Native login (Wayland):** log out, and at GDM pick **Ooze**. That runs `ooze-wayland-session` → `ooze --wayland` on real displays (no `--devkit`). Xwayland stays enabled unless you set `OOZE_NO_X11=1`.

**Nested tester:** from an existing Ubuntu login, launch **Ooze (nested)** from the applications menu, or run `ooze-session` (`ooze --wayland --devkit` in a nest window). Useful for day-to-day shell work without leaving GNOME.

Global menus for classic GTK3 apps (Inkscape) need Xwayland (on by default in both launchers).

Portals: the package installs `ooze-portals.conf` plus `UseIn=ooze` wrappers that still talk to **xdg-desktop-portal-gtk** / **xdg-desktop-portal-gnome** (FileChooser themed via session `GTK_THEME=WhiteSur-*` when WhiteSur is installed). A full Ooze Gel portal daemon is not shipped yet. Other gaps: idle/lock and some GNOME autostarts without a full `gnome-session` wrapper.

**Runtime packages (Ubuntu 26.04 / Mutter 18)** — install before or with the `.deb`:

```bash
sudo apt install mutter libmutter-18-0 xwayland dbus-user-session \
  xdg-desktop-portal xdg-desktop-portal-gtk xdg-desktop-portal-gnome gnome-keyring \
  libgtk-4-1 libadwaita-1-0 \
  libvte-2.91-gtk4-0 libgtop-2.0-11 libudisks2-0 libpipewire-0.3-0t64 \
  libgdk-pixbuf-2.0-0 libcairo2 libpango-1.0-0 libpangocairo-1.0-0 libx11-6 \
  appmenu-gtk3-module appmenu-gtk-module-common appmenu-registrar
```

| Package | Role |
| --- | --- |
| `mutter` / `libmutter-18-0` | Compositor host libraries |
| `xwayland` | X11 path for Inkscape / appmenu |
| `dbus-user-session` | Session bus + AppMenu registrar |
| `xdg-desktop-portal` | Portal front-end (**Depends**) |
| `xdg-desktop-portal-gtk` / `-gnome` | FileChooser / ScreenCast backends (**Recommends**) |
| `gnome-keyring` | Secret portal (**Recommends**) |
| `libgtk-4-1`, `libadwaita-1-0` | First-party apps |
| `libvte-2.91-gtk4-0` | Ooze Command |
| `libgtop-2.0-11`, `libudisks2-0` | Ooze King |
| `libpipewire-0.3-0t64` | Ooze Ear |
| `appmenu-gtk3-module`, `appmenu-registrar` | Classic GTK3 global menus (**Recommends**) |

Optional polish (not always in apt):

```bash
./scripts/install-whitesur-theme.sh    # foreign-app traffic lights (no global gtk-4.0 symlink)
./scripts/install-elementary-icons.sh  # if icons were not shipped in the package
```
