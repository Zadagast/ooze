#include "ooze-xsettings.h"
#include "ooze-theme.h"
#include "ooze-stall.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <gio/gio.h>
#include <string.h>

typedef struct
{
  Display *dpy;
  Window   window;
  Atom     selection;
  Atom     settings_atom;
  Atom     manager_atom;
  int      screen;
  guint32  serial;
  char    *display_name;
  gboolean owns_connection; /* TRUE if we XOpenDisplay'd — must XCloseDisplay */
} OozeXsettings;

static OozeXsettings *xsettings_singleton = NULL;

static void
ooze_xsettings_append_u8 (GByteArray *buf,
                        guint8      v)
{
  g_byte_array_append (buf, &v, 1);
}

static void
ooze_xsettings_append_u16 (GByteArray *buf,
                         guint16     v)
{
  guint8 b[2] = { (guint8) (v & 0xff), (guint8) ((v >> 8) & 0xff) };

  g_byte_array_append (buf, b, 2);
}

static void
ooze_xsettings_append_u32 (GByteArray *buf,
                         guint32     v)
{
  guint8 b[4] = {
    (guint8) (v & 0xff),
    (guint8) ((v >> 8) & 0xff),
    (guint8) ((v >> 16) & 0xff),
    (guint8) ((v >> 24) & 0xff),
  };

  g_byte_array_append (buf, b, 4);
}

static void
ooze_xsettings_pad4 (GByteArray *buf)
{
  while (buf->len % 4 != 0)
    ooze_xsettings_append_u8 (buf, 0);
}

static void
ooze_xsettings_append_name (GByteArray *buf,
                          const char *name)
{
  gsize len = strlen (name);

  ooze_xsettings_append_u16 (buf, (guint16) len);
  g_byte_array_append (buf, (const guint8 *) name, (guint) len);
  ooze_xsettings_pad4 (buf);
}

static void
ooze_xsettings_append_int (GByteArray *buf,
                         const char *name,
                         gint32      value,
                         guint32     serial)
{
  ooze_xsettings_append_u8 (buf, 0);
  ooze_xsettings_append_u8 (buf, 0);
  ooze_xsettings_append_name (buf, name);
  ooze_xsettings_append_u32 (buf, serial);
  ooze_xsettings_append_u32 (buf, (guint32) value);
}

static void
ooze_xsettings_append_string (GByteArray *buf,
                              const char *name,
                              const char *value,
                              guint32     serial)
{
  gsize len = strlen (value);

  /* XSETTINGS type 1 = string */
  ooze_xsettings_append_u8 (buf, 1);
  ooze_xsettings_append_u8 (buf, 0);
  ooze_xsettings_append_name (buf, name);
  ooze_xsettings_append_u32 (buf, serial);
  ooze_xsettings_append_u32 (buf, (guint32) len);
  g_byte_array_append (buf, (const guint8 *) value, (guint) len);
  ooze_xsettings_pad4 (buf);
}

static const char *
ooze_xsettings_current_gtk_theme (void)
{
  static char *foreign;

  /*
   * XSETTINGS is only consumed by X11 / Xwayland clients. Foreign GTK apps
   * are forced onto X11 for appmenu, so publish WhiteSur here.
   *
   * Do NOT mirror this into org.gnome.desktop.interface gtk-theme — Wayland
   * Ooze apps read GSettings and WhiteSur there breaks Ooze Gel / OozeKit.
   */
  g_free (foreign);
  foreign = ooze_theme_foreign_gtk_theme_for_session ();
  if (foreign)
    return foreign;

  return "Adwaita";
}

static GByteArray *
ooze_xsettings_build_blob (guint32 serial)
{
  GByteArray *buf = g_byte_array_new ();
  const char *theme_name;

  theme_name = ooze_xsettings_current_gtk_theme ();

  /* GDK compares against Xlib LSBFirst/MSBFirst (0/1), not 'l'/'B'. */
  ooze_xsettings_append_u8 (buf, (guint8) LSBFirst);
  ooze_xsettings_append_u8 (buf, 0);
  ooze_xsettings_append_u8 (buf, 0);
  ooze_xsettings_append_u8 (buf, 0);
  ooze_xsettings_append_u32 (buf, serial);
  ooze_xsettings_append_u32 (buf, 5);

  ooze_xsettings_append_int (buf, "Gtk/ShellShowsMenubar", 1, serial);
  ooze_xsettings_append_int (buf, "Gtk/ShellShowsAppmenu", 1, serial);
  /* Match org.gnome.desktop.wm.preferences button-layout (controls on left). */
  ooze_xsettings_append_string (buf, "Gtk/DecorationLayout",
                                "close,minimize,maximize:", serial);
  ooze_xsettings_append_string (buf, "Gtk/ThemeName",
                                theme_name ? theme_name : "Adwaita",
                                serial);
  /* GTK2 / some GTK3 paths still read Net/ThemeName. */
  ooze_xsettings_append_string (buf, "Net/ThemeName",
                                theme_name ? theme_name : "Adwaita",
                                serial);
  return buf;
}

static void
ooze_xsettings_publish (OozeXsettings *xs)
{
  g_autoptr (OozeStallScope) stall = NULL;
  g_autoptr (GByteArray) blob = NULL;

  /* XSync can block against nest Xwayland — callers must idle-defer off
   * the GSettings/click stack (see ooze_theme_schedule_xsettings_republish). */
  stall = ooze_stall_begin ("xsettings-publish");
  blob = ooze_xsettings_build_blob (xs->serial);

  XChangeProperty (xs->dpy,
                   xs->window,
                   xs->settings_atom,
                   xs->settings_atom,
                   8,
                   PropModeReplace,
                   blob->data,
                   (int) blob->len);
  /* XSync ensures Xwayland commits the property before any client reads it. */
  XSync (xs->dpy, False);
}

static void
ooze_xsettings_handle_selection_request (OozeXsettings *xs,
                                       XEvent      *event)
{
  XSelectionRequestEvent *req = &event->xselectionrequest;
  XSelectionEvent notify;
  Atom targets;
  Atom multiple;

  targets = XInternAtom (xs->dpy, "TARGETS", False);
  multiple = XInternAtom (xs->dpy, "MULTIPLE", False);

  memset (&notify, 0, sizeof notify);
  notify.type = SelectionNotify;
  notify.display = req->display;
  notify.requestor = req->requestor;
  notify.selection = req->selection;
  notify.target = req->target;
  notify.property = None;
  notify.time = req->time;

  if (req->target == targets)
    {
      Atom atoms[2] = { xs->settings_atom, multiple };

      XChangeProperty (xs->dpy, req->requestor, req->property, XA_ATOM, 32,
                       PropModeReplace, (unsigned char *) atoms, 2);
      notify.property = req->property;
    }
  else if (req->target == xs->settings_atom)
    {
      g_autoptr (GByteArray) blob = ooze_xsettings_build_blob (xs->serial);

      XChangeProperty (xs->dpy, req->requestor, req->property,
                       xs->settings_atom, 8, PropModeReplace,
                       blob->data, (int) blob->len);
      notify.property = req->property;
    }

  XSendEvent (xs->dpy, req->requestor, False, 0, (XEvent *) &notify);
  XFlush (xs->dpy);
}

void
ooze_xsettings_handle_xevent (XEvent *event)
{
  OozeXsettings *xs = xsettings_singleton;

  if (!xs || !event)
    return;

  if (event->type == SelectionRequest &&
      event->xselectionrequest.selection == xs->selection)
    {
      ooze_xsettings_handle_selection_request (xs, event);
    }
  else if (event->type == SelectionClear &&
           event->xselectionclear.selection == xs->selection)
    {
      g_warning ("Ooze xsettings: lost _XSETTINGS selection on %s — reclaiming",
                 xs->display_name ? xs->display_name : "?");
      XSetSelectionOwner (xs->dpy, xs->selection, xs->window, CurrentTime);
      if (XGetSelectionOwner (xs->dpy, xs->selection) == xs->window)
        {
          xs->serial++;
          ooze_xsettings_publish (xs);
        }
    }
}

void
ooze_xsettings_republish (void)
{
  if (!xsettings_singleton)
    return;

  xsettings_singleton->serial++;
  ooze_xsettings_publish (xsettings_singleton);
}

gboolean
ooze_xsettings_ensure_with_xdisplay (Display    *dpy,
                                   const char *display_name,
                                   gboolean    owns_connection)
{
  OozeXsettings *xs;
  char sel_name[32];
  Window root;
  XClientMessageEvent cm;

  if (!dpy || !display_name || !*display_name)
    return FALSE;

  if (xsettings_singleton)
    {
      if (g_strcmp0 (xsettings_singleton->display_name, display_name) == 0)
        {
          ooze_xsettings_republish ();
          return TRUE;
        }
      g_warning ("Ooze xsettings: already serving %s; ignoring %s",
                 xsettings_singleton->display_name, display_name);
      return TRUE;
    }

  xs = g_new0 (OozeXsettings, 1);
  xs->dpy = dpy;
  xs->owns_connection = owns_connection;
  xs->screen = DefaultScreen (dpy);
  xs->serial = 1;
  xs->display_name = g_strdup (display_name);
  root = RootWindow (dpy, xs->screen);

  g_snprintf (sel_name, sizeof sel_name, "_XSETTINGS_S%d", xs->screen);
  xs->selection = XInternAtom (dpy, sel_name, False);
  xs->settings_atom = XInternAtom (dpy, "_XSETTINGS_SETTINGS", False);
  xs->manager_atom = XInternAtom (dpy, "MANAGER", False);

  xs->window = XCreateSimpleWindow (dpy, root, -1, -1, 1, 1, 0, 0, 0);
  XSelectInput (dpy, xs->window, PropertyChangeMask | StructureNotifyMask);

  {
    Window prev = XGetSelectionOwner (dpy, xs->selection);

    XSetSelectionOwner (dpy, xs->selection, xs->window, CurrentTime);
    if (XGetSelectionOwner (dpy, xs->selection) != xs->window)
      {
        g_warning ("Ooze xsettings: failed to own %s on DISPLAY=%s",
                   sel_name, display_name);
        XDestroyWindow (dpy, xs->window);
        if (owns_connection)
          XCloseDisplay (dpy);
        g_free (xs->display_name);
        g_free (xs);
        return FALSE;
      }
    if (prev != None && prev != xs->window)
      g_print ("Ooze xsettings: took %s from previous owner on %s\n",
               sel_name, display_name);
  }

  ooze_xsettings_publish (xs);

  memset (&cm, 0, sizeof cm);
  cm.type = ClientMessage;
  cm.window = root;
  cm.message_type = xs->manager_atom;
  cm.format = 32;
  cm.data.l[0] = CurrentTime;
  cm.data.l[1] = (long) xs->selection;
  cm.data.l[2] = (long) xs->window;
  XSendEvent (dpy, root, False, StructureNotifyMask, (XEvent *) &cm);
  XFlush (dpy);

  xsettings_singleton = xs;
  g_print ("Ooze xsettings: serving ShellShowsMenubar on DISPLAY=%s (%s)\n",
           display_name, sel_name);
  return TRUE;
}

gboolean
ooze_xsettings_ensure_shell_shows_menubar (const char *display_name)
{
  Display *dpy;

  if (!display_name || !*display_name)
    return FALSE;

  if (xsettings_singleton)
    return ooze_xsettings_ensure_with_xdisplay (xsettings_singleton->dpy,
                                              display_name, FALSE);

  /*
   * Fallback for callers without Mutter's Display*. Opening a second
   * connection to nest Xwayland from plugin_start can deadlock — prefer
   * ooze_xsettings_ensure_with_xdisplay() on Meta's connection.
   */
  dpy = XOpenDisplay (display_name);
  if (!dpy)
    {
      g_warning ("Ooze xsettings: cannot open DISPLAY=%s", display_name);
      return FALSE;
    }

  return ooze_xsettings_ensure_with_xdisplay (dpy, display_name, TRUE);
}
