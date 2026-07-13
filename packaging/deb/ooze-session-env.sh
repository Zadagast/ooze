#!/usr/bin/env bash
# Shared env for Ooze session launchers (native + nested).
# Sourced by packaging/deb/ooze-*-session and optionally run-devkit.sh.

# Portal backends match UseIn=ooze (see data/xdg-desktop-portal/portals/).
export XDG_CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP:-Ooze}"
export XDG_SESSION_DESKTOP="${XDG_SESSION_DESKTOP:-Ooze}"
export DESKTOP_SESSION="${DESKTOP_SESSION:-Ooze}"
export XDG_DATA_DIRS="/usr/share/ooze${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"

# Do NOT export GTK_THEME into the compositor session.
# Session-wide GTK_THEME bleeds into portals/devkit and Ooze apps, and causes
# WhiteSur CSS warnings. Foreign apps get WhiteSur only via launch helpers
# (ooze_foreign_gtk_apply_* / ooze_appmenu_*) and XSETTINGS for X11 clients.
ooze_export_foreign_gtk_theme () {
  unset GTK_THEME
}

ooze_export_appmenu_modules () {
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
