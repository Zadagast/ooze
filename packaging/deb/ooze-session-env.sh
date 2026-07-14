#!/usr/bin/env bash
# Shared env for Ooze session launchers (native + nested).
# Sourced by packaging/deb/ooze-*-session and optionally run-devkit.sh.
#
# Native GDM (ooze-wayland-session): login session bus + systemd --user.
# Nested (ooze-session / run-devkit): private bus via dbus-run-session —
# never call ooze_push_activation_environment there (would pollute host).

# Portal backends match UseIn=ooze (see data/xdg-desktop-portal/portals/).
export XDG_CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP:-Ooze}"
export XDG_SESSION_DESKTOP="${XDG_SESSION_DESKTOP:-Ooze}"
export DESKTOP_SESSION="${DESKTOP_SESSION:-Ooze}"
export XDG_DATA_DIRS="/usr/share/ooze${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"

# Qt apps follow the session light/dark via the GTK platform theme, which
# tracks org.gnome.desktop.interface color-scheme (same authority as the
# shell, XSETTINGS, and the Settings portal). Respect a user override.
export QT_QPA_PLATFORMTHEME="${QT_QPA_PLATFORMTHEME:-gtk3}"

# Do NOT export GTK_THEME into the compositor session.
# Session-wide GTK_THEME bleeds into portals/devkit and Ooze apps, and causes
# WhiteSur CSS warnings. Foreign apps get WhiteSur only via launch helpers
# (ooze_foreign_gtk_apply_* / ooze_appmenu_*) and XSETTINGS for X11 clients.
ooze_export_foreign_gtk_theme () {
  unset GTK_THEME
}

# Foreign X11 AppMenu is OFF by default (compositor freezes on sync dbusmenu).
# Opt in only for debugging: OOZE_FOREIGN_GLOBAL_MENU=1
ooze_export_appmenu_modules () {
  case "${OOZE_FOREIGN_GLOBAL_MENU:-}" in
    1|true|TRUE|yes|YES|on|ON) ;;
    *) return 0 ;;
  esac
  if [[ -n "${GTK_MODULES:-}" ]]; then
    case ":$GTK_MODULES:" in
      *:appmenu-gtk-module:*) ;;
      *) export GTK_MODULES="appmenu-gtk-module:$GTK_MODULES" ;;
    esac
  else
    export GTK_MODULES=appmenu-gtk-module
  fi
  export UBUNTU_MENUPROXY="${UBUNTU_MENUPROXY:-1}"
}

# Push desktop / display env into D-Bus activation and systemd --user so
# xdg-desktop-portal / gnome-keyring see XDG_CURRENT_DESKTOP=Ooze.
# Soft-fail if tools are missing. Native GDM only — not for nest.
ooze_push_activation_environment () {
  if ! command -v dbus-update-activation-environment >/dev/null 2>&1; then
    return 0
  fi
  # Explicit short list: avoid dumping nest-only or accidental GTK_THEME.
  # --systemd mirrors env into systemd --user for socket-activated services.
  dbus-update-activation-environment --systemd \
    XDG_CURRENT_DESKTOP \
    XDG_SESSION_TYPE \
    XDG_SESSION_DESKTOP \
    DESKTOP_SESSION \
    XDG_RUNTIME_DIR \
    DBUS_SESSION_BUS_ADDRESS \
    WAYLAND_DISPLAY \
    DISPLAY \
    XAUTHORITY \
    2>/dev/null || true
}

# Start gnome-keyring on the *current* session bus when available.
# Soft-fail if not installed (package Recommends). Safe on nest private bus.
ooze_start_gnome_keyring () {
  if ! command -v gnome-keyring-daemon >/dev/null 2>&1; then
    return 0
  fi
  # Prefer eval so SSH_AUTH_SOCK / GNOME_KEYRING_CONTROL export when present.
  # secrets is required for the Secret portal; ssh/pkcs11 match common Ubuntu.
  local _out=""
  _out="$(gnome-keyring-daemon --start --components=secrets,ssh,pkcs11 2>/dev/null)" || true
  if [[ -n "$_out" ]]; then
    # shellcheck disable=SC2086
    eval $_out || true
  fi
}

# Best-effort: ensure the portal front-end can start under our desktop id.
# Activation-environment is usually enough; try a soft user-service kick too.
# Native only — nest should not touch host systemd --user units.
ooze_kick_portals () {
  if ! command -v systemctl >/dev/null 2>&1; then
    return 0
  fi
  # try-restart if already running (picks up new XDG_CURRENT_DESKTOP);
  # start if idle. Soft-fail either way.
  systemctl --user try-restart xdg-desktop-portal.service 2>/dev/null \
    || systemctl --user start xdg-desktop-portal.service 2>/dev/null \
    || true
}
