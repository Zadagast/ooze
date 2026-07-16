#!/usr/bin/env python3
"""Minimal StatusNotifierItem for testing the Ooze tray.

Registers with org.kde.StatusNotifierWatcher, exports an SNI with a themed
IconName + a com.canonical.dbusmenu menu, and logs when methods are invoked.
Run inside the nested compositor's session bus after the compositor is up.
"""
import sys
from gi.repository import Gio, GLib

ITEM_XML = """
<node>
  <interface name='org.kde.StatusNotifierItem'>
    <property name='Category' type='s' access='read'/>
    <property name='Id' type='s' access='read'/>
    <property name='Title' type='s' access='read'/>
    <property name='Status' type='s' access='read'/>
    <property name='IconName' type='s' access='read'/>
    <property name='IconThemePath' type='s' access='read'/>
    <property name='ItemIsMenu' type='b' access='read'/>
    <property name='Menu' type='o' access='read'/>
    <method name='Activate'><arg name='x' type='i' direction='in'/><arg name='y' type='i' direction='in'/></method>
    <method name='SecondaryActivate'><arg name='x' type='i' direction='in'/><arg name='y' type='i' direction='in'/></method>
    <method name='ContextMenu'><arg name='x' type='i' direction='in'/><arg name='y' type='i' direction='in'/></method>
    <signal name='NewIcon'/>
  </interface>
</node>
"""

MENU_XML = """
<node>
  <interface name='com.canonical.dbusmenu'>
    <property name='Version' type='u' access='read'/>
    <property name='Status' type='s' access='read'/>
    <method name='GetLayout'>
      <arg type='i' name='parentId' direction='in'/>
      <arg type='i' name='recursionDepth' direction='in'/>
      <arg type='as' name='propertyNames' direction='in'/>
      <arg type='u' name='revision' direction='out'/>
      <arg type='(ia{sv}av)' name='layout' direction='out'/>
    </method>
    <method name='Event'>
      <arg type='i' name='id' direction='in'/><arg type='s' name='eventId' direction='in'/>
      <arg type='v' name='data' direction='in'/><arg type='u' name='timestamp' direction='in'/>
    </method>
    <method name='AboutToShow'>
      <arg type='i' name='id' direction='in'/><arg type='b' name='needUpdate' direction='out'/>
    </method>
    <signal name='LayoutUpdated'><arg type='u' name='revision'/><arg type='i' name='parent'/></signal>
  </interface>
</node>
"""

ICON_NAME = sys.argv[1] if len(sys.argv) > 1 else "audio-volume-high"


def item_get_prop(conn, sender, path, iface, prop):
    vals = {
        "Category": GLib.Variant("s", "ApplicationStatus"),
        "Id": GLib.Variant("s", "ooze-test"),
        "Title": GLib.Variant("s", "Ooze Test"),
        "Status": GLib.Variant("s", "Active"),
        "IconName": GLib.Variant("s", ICON_NAME),
        "IconThemePath": GLib.Variant("s", ""),
        "ItemIsMenu": GLib.Variant("b", False),
        "Menu": GLib.Variant("o", "/MenuBar"),
    }
    return vals.get(prop)


def item_method(conn, sender, path, iface, method, params, invocation):
    print(f"ITEM METHOD CALLED: {method} {params.unpack()}", flush=True)
    invocation.return_value(None)


def menu_method(conn, sender, path, iface, method, params, invocation):
    print(f"MENU METHOD CALLED: {method} {params.unpack()}", flush=True)
    if method == "GetLayout":
        item0 = GLib.Variant("(ia{sv}av)", (
            0, {"children-display": GLib.Variant("s", "submenu")},
            [GLib.Variant("(ia{sv}av)", (
                1, {"label": GLib.Variant("s", "Test Item One"),
                    "enabled": GLib.Variant("b", True),
                    "visible": GLib.Variant("b", True)}, []))],
        ))
        invocation.return_value(GLib.Variant("(u(ia{sv}av))", (1, item0)))
    elif method == "AboutToShow":
        invocation.return_value(GLib.Variant("(b)", (False,)))
    else:
        invocation.return_value(None)


def menu_get_prop(conn, sender, path, iface, prop):
    if prop == "Version":
        return GLib.Variant("u", 3)
    if prop == "Status":
        return GLib.Variant("s", "normal")
    return None


def on_bus_acquired(conn, name, *_):
    node_item = Gio.DBusNodeInfo.new_for_xml(ITEM_XML)
    conn.register_object("/StatusNotifierItem", node_item.interfaces[0],
                         item_method, item_get_prop, None)
    node_menu = Gio.DBusNodeInfo.new_for_xml(MENU_XML)
    conn.register_object("/MenuBar", node_menu.interfaces[0],
                         menu_method, menu_get_prop, None)
    watcher = Gio.DBusProxy.new_sync(
        conn, Gio.DBusProxyFlags.NONE, None,
        "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher", None)
    watcher.call_sync("RegisterStatusNotifierItem",
                      GLib.Variant("(s)", (name,)),
                      Gio.DBusCallFlags.NONE, -1, None)
    print(f"Registered SNI as {name} with icon {ICON_NAME}", flush=True)


bus_name = "org.kde.StatusNotifierItem-test"
Gio.bus_own_name(Gio.BusType.SESSION, bus_name,
                 Gio.BusNameOwnerFlags.NONE, on_bus_acquired, None, None)
GLib.MainLoop().run()
