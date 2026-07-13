#!/usr/bin/env bash
# Install XFCE/Unity-style GTK menu exporter + AppMenu registrar for Ooze.
set -euo pipefail

pkgs=(
  appmenu-gtk3-module
  appmenu-gtk-module-common
  appmenu-registrar
  xsettingsd
)

echo "Installing ${pkgs[*]} ..."
sudo apt-get install -y "${pkgs[@]}"

module=""
for candidate in \
  /usr/lib/x86_64-linux-gnu/gtk-3.0/modules/libappmenu-gtk-module.so \
  /usr/lib/x86_64-linux-gnu/gtk-3.0/modules/libappmenu-gtk3-module.so \
  /usr/lib/gtk-3.0/modules/libappmenu-gtk-module.so \
  /usr/lib/gtk-3.0/modules/libappmenu-gtk3-module.so
do
  if [[ -f "$candidate" ]]; then
    module=$candidate
    break
  fi
done

registrar=""
for candidate in \
  /usr/libexec/vala-panel/appmenu-registrar \
  "$(command -v appmenu-registrar 2>/dev/null || true)"
do
  if [[ -n "$candidate" && -x "$candidate" ]]; then
    registrar=$candidate
    break
  fi
done

if [[ -z "$module" ]]; then
  echo "ERROR: appmenu GTK3 module .so not found after install" >&2
  exit 1
fi

if [[ -z "$registrar" ]]; then
  echo "ERROR: appmenu-registrar binary missing" >&2
  exit 1
fi

echo "OK: module=$module"
echo "OK: registrar=$registrar"
echo
echo "Restart Ooze (./run-devkit.sh). GTK3 apps launched from the dock will"
echo "load appmenu-gtk-module; Xwayland clients register with the AppMenu registrar."
echo "xsettingsd advertises Gtk/ShellShowsMenubar so in-window bars can hide."
