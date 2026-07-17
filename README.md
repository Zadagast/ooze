# Ooze

**An Aqua-inspired Linux desktop that brings you back to a time when the desktop
made sense.**

Ooze is a complete Wayland desktop for people who loved classic Mac OS or old
GNOME — and still want Linux's freedom, privacy, and control.

![Ooze desktop demo](docs/ooze-desktop-demo.gif)

[Full-quality video](docs/ooze-desktop-demo.mp4) ·
[Website](https://zadagast.github.io/ooze/)

## What makes Ooze different

Most Linux desktops give you a shell and a pile of unrelated apps. Ooze is built
as one complete, opinionated desktop.

- **Your whole screen looks like one desktop.** Ooze puts its Gel window frames
  and red, yellow, and green traffic lights on its own apps and supported
  third-party Wayland windows.
- **A real desktop, not just a launcher.** You get a menu bar, floating dock,
  desktop icons, wallpaper, and lively window animations designed together.
- **One switch changes everything.** Light and dark mode follow through the
  shell, Ooze apps, and supported foreign GTK applications.
- **A full set of built-in apps matches.** Files, settings, terminal,
  screenshots, images, sound, torrents, software, displays, and more share one
  visual language.
- **It bridges old and new.** Wayland, X11, classic app menus, foreign GTK
  themes, notifications, and system-tray applications can live together.
- **The desktop and its apps are built together.** OozeKit gives the first-party
  suite the same windows, controls, menus, and behavior.
- **It is made to be tinkered with.** The project includes an installable
  toolkit and a nested build-and-run workflow.

## The built-in app suite

- **Spot** is the file manager for browsing folders, opening files, making
  folders, moving items to the Trash, and switching between icon and column
  views.
- **Ooze King** is the settings hub for launching Displays, Themes, Default
  Apps, Sound Settings, Software, Torrent, Spot, Terminal, and About.
- **Ooze Command** is a tabbed terminal for running shell commands.
- **Ooze Launch** is a searchable application launcher with categories,
  custom groups, and keyboard navigation.
- **Ooze Shot** captures desktop screenshots and lets you preview, copy,
  reveal, or open them in Ooze Eye.
- **Ooze Eye** views common image formats with zoom, fit-to-window, and
  previous/next image controls.
- **Ooze Ear** manages audio devices, volume, mute, and per-application stream
  routing.
- **Ooze Torrent** downloads torrents and magnet links with progress, speeds,
  pause/resume, removal, and download-folder actions.
- **Software (Ooze Pak)** lists installed Flatpaks and installs or removes
  Flatpak applications.
- **Ooze Monitor** configures resolution, refresh rate, scale, orientation,
  primary display, and multi-monitor layout.
- **Ooze Defaults** chooses the default applications for web, mail, folders,
  images, audio, video, text, and torrent links.
- **Ooze Themes** controls appearance, icon, cursor, and foreign-GTK theme
  preferences.
- **Ooze About** shows information about your computer and installed Ooze apps.

## Plays nice with your existing Linux apps

Ooze does not trap you in its own applications — it makes your applications
feel at home too.

- Native Wayland and X11 applications run side by side through built-in
  Xwayland support.
- Supported third-party Wayland windows receive Ooze Gel frames and traffic
  lights even when they were not designed for Ooze.
- Foreign GTK applications can use the bundled WhiteSur light and dark themes,
  which follow the Ooze appearance live through XSETTINGS.
- The global menu bar shows menus from Ooze apps and supported foreign apps
  through the Wayland and KDE AppMenu protocols.
- Ooze can provide sensible App, File, Window, and Help menus for applications
  that do not expose their own menus.
- StatusNotifier and AppIndicator applications can use the system tray and
  open their D-Bus menus.
- The built-in notification server provides notification cards, icons,
  timeouts, close actions, and clickable notification actions.
- Eligible XDG autostart applications start with the Ooze session.
- The in-compositor polkit agent handles administrator authentication.
- The PAM-backed lock screen protects native Wayland sessions.
- The session menu handles logout, restart, shutdown, and suspend with
  confirmation and countdowns.
- The first-party ScreenCast portal lets Flatpak Discord and browser calls
  share a monitor through a native picker.

## Install

Download the current Alpha 0.2.1 AppImage or Debian package from
[Releases](https://github.com/Zadagast/ooze/releases).

The AppImage runs Ooze as a nested window on an existing Wayland desktop:

```sh
chmod +x Ooze-*.AppImage
./Ooze-*.AppImage
```

The Debian package installs a native session you can select from your login
screen:

```sh
sudo apt install ./ooze_0.2.1_amd64.deb
```

## Build from source

Ooze currently builds against Mutter 18 and is developed on Ubuntu 26.04.
See [docs/BUILDING.md](docs/BUILDING.md) for dependencies, the nested devkit,
and Debian/AppImage packaging instructions.

## Develop and build an Ooze app

Use the nested development loop without replacing your current desktop:

```sh
ooze dev
```

You can also launch the nested devkit directly with `./run-devkit.sh`, or use
`ooze update` to pull, rebuild, and reinstall the package. See
[DEVELOPING.md](DEVELOPING.md) for the workflow and
[docs/build-an-ooze-app.md](docs/build-an-ooze-app.md) for the OozeKit SDK.

## Status

**Ooze is Alpha 0.2.1** — a bold, early, work-in-progress desktop that already
feels like coming home.

## License

Compositor pieces inherit upstream Mutter licensing; first-party Ooze code is
part of the same project unless otherwise noted.
