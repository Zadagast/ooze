#!/usr/bin/env python3
"""Standalone ScreenCast portal probe for Ooze.

Drives org.freedesktop.portal.Desktop's ScreenCast interface end-to-end
(CreateSession -> SelectSources -> Start) so the Ooze monitor picker is
invoked directly, independent of any application. Prints the PipeWire node
ids on success. Run it inside the Ooze session.
"""
import random
import string
import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
sender = bus.get_unique_name()[1:].replace(".", "_")
loop = GLib.MainLoop()
state = {}


def tok():
    return "ooze" + "".join(random.choices(string.ascii_lowercase, k=10))


def fail(msg):
    print("FAIL:", msg)
    loop.quit()


def do(method, fixed_args, options, on_ok):
    t = tok()
    options["handle_token"] = GLib.Variant("s", t)
    path = "/org/freedesktop/portal/desktop/request/%s/%s" % (sender, t)

    def handler(conn, s, p, i, sig, params):
        conn.signal_unsubscribe(state["sub"])
        resp, results = params.unpack()
        print("  <- %s response=%s" % (method, resp))
        if resp != 0:
            fail("%s response=%s (1=cancelled, 2=failed)" % (method, resp))
        else:
            on_ok(results)

    state["sub"] = bus.signal_subscribe(
        "org.freedesktop.portal.Desktop",
        "org.freedesktop.portal.Request",
        "Response",
        path,
        None,
        Gio.DBusSignalFlags.NONE,
        handler,
    )
    children = list(fixed_args) + [GLib.Variant("a{sv}", options)]
    args = GLib.Variant.new_tuple(*children)
    print("-> %s" % method)
    bus.call_sync(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.ScreenCast",
        method,
        args,
        GLib.VariantType.new("(o)"),
        Gio.DBusCallFlags.NONE,
        -1,
        None,
    )


def start_done(results):
    streams = results.get("streams", [])
    print("SUCCESS: %d stream(s)" % len(streams))
    for node_id, props in streams:
        print("  PipeWire node id = %s  props=%s" % (node_id, dict(props)))
    loop.quit()


def sources_done(results):
    do(
        "Start",
        [GLib.Variant("o", state["session"]), GLib.Variant("s", "")],
        {},
        start_done,
    )


def session_done(results):
    state["session"] = results["session_handle"]
    print("  session = %s" % state["session"])
    do(
        "SelectSources",
        [GLib.Variant("o", state["session"])],
        {
            "types": GLib.Variant("u", 3),
            "multiple": GLib.Variant("b", False),
            "cursor_mode": GLib.Variant("u", 1),
        },
        sources_done,
    )


do("CreateSession", [], {"session_handle_token": GLib.Variant("s", tok())}, session_done)
loop.run()
