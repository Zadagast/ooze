#!/usr/bin/env bash
# Shared env for Ooze session launchers (native + nested).
# Sourced by packaging/deb/ooze-*-session and optionally run-devkit.sh.
#
# Native GDM (ooze-wayland-session): login session bus + systemd --user.
# Nested (ooze-session / run-devkit): private bus via dbus-run-session —
# never call ooze_push_activation_environment there (would pollute host).

# Portal backends match UseIn=ooze (see data/xdg-desktop-portal/portals/).
export XDG_CURRENT_DESKTOP="Ooze"
export XDG_SESSION_DESKTOP="Ooze"
export DESKTOP_SESSION="Ooze"
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

# Foreign AppMenu is ON by default (dbusmenu path is fully async now).
# Opt out with OOZE_FOREIGN_GLOBAL_MENU=0
ooze_export_appmenu_modules () {
  case "${OOZE_FOREIGN_GLOBAL_MENU:-1}" in
    0|false|FALSE|no|NO|off|OFF)
      local _modules="${GTK_MODULES:-}"
      local _module
      local -a _kept=()

      IFS=: read -r -a _modules <<< "$_modules"
      for _module in "${_modules[@]}"; do
        case "$_module" in
          ""|appmenu-gtk-module|appmenu-gtk3-module|unity-gtk-module|unity-gtk3-module)
            ;;
          *) _kept+=("$_module") ;;
        esac
      done

      if ((${#_kept[@]})); then
        local IFS=:
        export GTK_MODULES="${_kept[*]}"
      else
        unset GTK_MODULES
      fi
      return 0
      ;;
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
  local _env_vars=(
    XDG_CURRENT_DESKTOP
    XDG_SESSION_DESKTOP
    DESKTOP_SESSION
    XDG_SESSION_TYPE
    XDG_RUNTIME_DIR
    DBUS_SESSION_BUS_ADDRESS
    WAYLAND_DISPLAY
    DISPLAY
    XAUTHORITY
  )

  if ! command -v dbus-update-activation-environment >/dev/null 2>&1; then
    return 0
  fi

  # Explicit short list: avoid dumping nest-only or accidental GTK_THEME.
  # --systemd mirrors env into systemd --user for socket-activated services.
  dbus-update-activation-environment --systemd "${_env_vars[@]}" 2>/dev/null || true
  if command -v systemctl >/dev/null 2>&1; then
    systemctl --user import-environment "${_env_vars[@]}" 2>/dev/null || true
  fi
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
  systemctl --user --no-block reset-failed xdg-desktop-portal.service 2>/dev/null || true
  # Restart the GTK backend before the portal front-end so the frontend re-reads
  # the Ooze desktop identity and can resolve the gnome-free routing.
  systemctl --user --no-block try-restart xdg-desktop-portal-gtk.service 2>/dev/null \
    || systemctl --user --no-block start xdg-desktop-portal-gtk.service 2>/dev/null \
    || true
  systemctl --user --no-block try-restart xdg-desktop-portal.service 2>/dev/null \
    || systemctl --user --no-block start xdg-desktop-portal.service 2>/dev/null \
    || true
}
