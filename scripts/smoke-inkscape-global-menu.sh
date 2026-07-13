#!/usr/bin/env bash
# Smoke: nest serves ShellShowsMenubar, then X11 Inkscape binds dbusmenu.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Kill prior nests without matching this script's argv (avoid pkill -f self-hit).
while read -r pid; do
  [[ -n "$pid" ]] || continue
  kill "$pid" 2>/dev/null || true
done < <(pgrep -x ooze || true)
sleep 1

rm -f /tmp/ooze-inkscape-smoke.log /tmp/ooze-inkscape-app-smoke.log
export OOZE_DISPLAY_MODES="1920x1080:1280x720"
./run-devkit.sh > /tmp/ooze-inkscape-smoke.log 2>&1 &

for i in $(seq 1 80); do
  if rg -q "Ooze xsettings: serving ShellShowsMenubar" /tmp/ooze-inkscape-smoke.log 2>/dev/null; then
    echo "XSETTINGS ok at try $i"
    break
  fi
  sleep 0.25
done

NEST_PID=$(pgrep -n -f 'build/ooze --wayland --devkit' || true)
echo "NEST_PID=$NEST_PID"
rg -n "plugin started|ShellShowsMenubar|waiting for Meta X11|AppMenu registrar|public X11|Wayland display" /tmp/ooze-inkscape-smoke.log | head -25 || true

DISP=$(rg -o "public X11 display :[0-9]+" /tmp/ooze-inkscape-smoke.log | head -1 | awk '{print $NF}')
WL=$(rg -o "Wayland display name '[^']+'" /tmp/ooze-inkscape-smoke.log | head -1 | sed "s/.*'\\(.*\\)'/\\1/")
AUTH=""
while read -r xwline; do
  case "$xwline" in
    *" ${DISP} "*|*" ${DISP}"|*"${DISP} "*)
      AUTH=$(echo "$xwline" | rg -o '/run/user/[0-9]+/\.mutter-Xwaylandauth\.[A-Za-z0-9]+' | head -1 || true)
      [[ -n "$AUTH" ]] && break
      ;;
  esac
done < <(pgrep -a Xwayland || true)
if [[ -z "$AUTH" ]]; then
  AUTH=$(ls -t /run/user/"$(id -u)"/.mutter-Xwaylandauth.* 2>/dev/null | head -1 || true)
fi
echo "DISP=$DISP WL=$WL AUTH=$AUTH"
pgrep -a Xwayland || true

cleanup () {
  kill "${INK:-}" 2>/dev/null || true
  kill "${NEST_PID:-}" 2>/dev/null || true
  while read -r pid; do
    [[ -n "$pid" ]] || continue
    kill "$pid" 2>/dev/null || true
  done < <(pgrep -x ooze || true)
}
trap cleanup EXIT

if [[ -z "$NEST_PID" || -z "$DISP" || -z "$AUTH" ]] || ! command -v inkscape >/dev/null; then
  echo "SKIP/FAIL: missing nest/display/inkscape"
  tail -50 /tmp/ooze-inkscape-smoke.log || true
  exit 1
fi

# Inkscape must share the nest dbus-run-session bus (AppMenu registrar).
NEST_DBUS=$(tr '\0' '\n' < /proc/"$NEST_PID"/environ | sed -n 's/^DBUS_SESSION_BUS_ADDRESS=//p' | head -1)
echo "NEST_DBUS=$NEST_DBUS"
if [[ -z "$NEST_DBUS" ]]; then
  echo "FAIL: nest has no DBUS_SESSION_BUS_ADDRESS"
  exit 1
fi

DISPLAY="$DISP" XAUTHORITY="$AUTH" WAYLAND_DISPLAY="$WL" \
  DBUS_SESSION_BUS_ADDRESS="$NEST_DBUS" \
  GDK_BACKEND=x11 GTK_MODULES=appmenu-gtk-module UBUNTU_MENUPROXY=1 \
  XDG_CONFIG_HOME="$ROOT/data/xdg-config" \
  timeout 30 inkscape >/tmp/ooze-inkscape-app-smoke.log 2>&1 &
INK=$!

BOUND=0
for i in $(seq 1 20); do
  if rg -q "dbusmenu bound|gtk-shell bound" /tmp/ooze-inkscape-smoke.log 2>/dev/null; then
    echo "BOUND at try $i (${i}s)"
    BOUND=1
    break
  fi
  sleep 1
done

echo "--- nest menu lines ---"
rg -n "dbusmenu bound|gtk-shell bound|WindowRegistered|Wayland classic|Needs X11|ShellShowsMenubar|no dbusmenu|Inkscape|registrar" /tmp/ooze-inkscape-smoke.log | tail -40 || true
echo "--- app log tail ---"
tail -20 /tmp/ooze-inkscape-app-smoke.log || true

if [[ "$BOUND" -eq 1 ]]; then
  echo "SMOKE PASS"
  exit 0
fi

echo "SMOKE FAIL: no dbusmenu/gtk-shell bound"
exit 1
