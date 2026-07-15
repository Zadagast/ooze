#include "ooze-dock-shell.h"
#include "ooze-dock-pins.h"
#include "ooze-aqua-draw.h"
#include "ooze-aqua-menu.h"
#include "ooze-dnd-bridge.h"
#include "ooze-foreign-gtk.h"
#include "ooze-icon-lookup.h"
#include "ooze-window-tracker.h"
#include "ooze-shared-appmenu.h"
#include "ooze-shared-icons.h"
#include "ooze-stall.h"
#include "ooze-theme.h"

#define __COGL_H_INSIDE__
#include "cogl/cogl-texture-2d.h"
#undef __COGL_H_INSIDE__

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gdesktopappinfo.h>

#include <math.h>
#include <string.h>

#include <meta/meta-backend.h>
#include <meta/meta-background-content.h>
#include <meta/meta-background.h>
#include <meta/meta-dnd.h>
#include <meta/meta-wayland-client.h>
#include <meta/window.h>
#include <mtk/mtk.h>

#define DOCK_HEIGHT       48.0f
#define LAUNCHER_SIZE     40.0f
#define ICON_SIZE         32
#define LAUNCHER_SPACING  8.0f
#define DOCK_SPRING_MS    700
#define DOCK_HOLD_MS      400
#define DOCK_DRAG_MS      150
#define DOCK_UNPIN_MS     180
#define DOCK_ACTION_PIN   1
#define DOCK_ACTION_UNPIN 2

/* Running-app indicator: small downward triangle below each dock icon */
#define INDICATOR_W       6
#define INDICATOR_H       4

/* Weak pointer to the live dock icons container (for hit-tests / DnD). */
static ClutterActor *ooze_dock_icons_container = NULL;

struct _OozeDock
{
  MetaContext *context;
  MetaDisplay *display;
  ClutterActor *actor;
};

static const char *spot_dock_icon_names[] = {
  "system-file-manager",
  "folder-home",
  "folder",
  NULL,
};

typedef struct
{
  const char *app_icon;
  const char *elementary_icon;
} OozeDockIconAlias;

static const OozeDockIconAlias dock_icon_aliases[] = {
  { "org.gnome.Ptyxis", "utilities-terminal" },
  { "org.gnome.Ptyxis-symbolic", "utilities-terminal" },
  { "org.gnome.TextEditor", "accessories-text-editor" },
  { "org.gnome.TextEditor-symbolic", "accessories-text-editor" },
  { "org.gnome.Calculator", "accessories-calculator" },
  { "org.gnome.Calculator-symbolic", "accessories-calculator" },
  /* Map legacy org.ooze.* icon ids to Freedesktop names so Themes packs apply. */
  { "org.ooze.Spot",            "system-file-manager"  },
  { "org.ooze.Spot-symbolic",   "system-file-manager"  },
  { "org.ooze.Command",         "utilities-terminal"   },
  { "org.ooze.Command-symbolic","utilities-terminal"   },
  { "org.ooze.King",            "preferences-system"   },
  { "org.ooze.King-symbolic",   "preferences-system"   },
  { "org.ooze.Ear",             "audio-headphones"     },
  { "org.ooze.Ear-symbolic",    "audio-headphones-symbolic" },
  { "org.ooze.Pak",             "system-software-install" },
  { "org.ooze.Pak-symbolic",    "system-software-install-symbolic" },
  { "org.ooze.About",           "help-about" },
  { "org.ooze.About-symbolic",  "help-about" },
  { "org.ooze.Monitor",         "video-display" },
  { "org.ooze.Monitor-symbolic","video-display" },
  { "org.ooze.Themes",          "preferences-desktop-theme" },
  { "org.ooze.Themes-symbolic", "preferences-desktop-theme" },
  { "org.ooze.Eye",             "image-x-generic" },
  { "org.ooze.Eye-symbolic",    "image-x-generic" },
  { "org.ooze.Shot",            "camera-photo" },
  { "org.ooze.Shot-symbolic",   "camera-photo" },
  { "org.ooze.Torrent",         "application-x-bittorrent" },
  { "org.ooze.Torrent-symbolic","application-x-bittorrent" },
  { NULL, NULL },
};

static const char *
ooze_dock_icon_alias (const char *icon_name)
{
  gsize i;

  if (!icon_name)
    return NULL;

  for (i = 0; dock_icon_aliases[i].app_icon != NULL; i++)
    {
      if (g_strcmp0 (dock_icon_aliases[i].app_icon, icon_name) == 0)
        return dock_icon_aliases[i].elementary_icon;
    }

  return NULL;
}

static gboolean  ooze_dock_window_is_visible    (MetaWindow *window);
static MetaWindow *ooze_dock_get_most_recent_window (GList *windows);
static gboolean  ooze_dock_window_matches_app   (MetaWindow *window, GDesktopAppInfo *app_info);
static gboolean  ooze_dock_window_matches_app_id (MetaWindow *window, const char *app_id);
static void      ooze_dock_attach_indicator     (ClutterActor *launcher, float launcher_w);

typedef void (*OozeDockLaunchFn) (MetaContext *context);
static void      ooze_dock_wire_app_launcher    (ClutterActor  *button,
                                               MetaDisplay   *display,
                                               const char    *app_id,
                                               OozeDockLaunchFn launch);

typedef struct {
  const char *app_id;
  const char *binary;
  const char * const *icons;
  float fr, fg, fb;
} OozeDockAppSpec;

static const char *eye_dock_icon_names[] = {
  "image-x-generic", "image-viewer", NULL,
};
static const char *torrent_dock_icon_names[] = {
  "application-x-bittorrent", "network-workgroup", NULL,
};
static const char *monitor_dock_icon_names[] = {
  "video-display", "preferences-desktop-display", NULL,
};
static const char *about_dock_icon_names[] = {
  "help-about", "dialog-information", NULL,
};

static void ooze_dock_launch_binary (MetaContext *context, const char *binary);
static void ooze_dock_launch_eye (MetaContext *context);
static void ooze_dock_launch_torrent (MetaContext *context);
static void ooze_dock_launch_monitor (MetaContext *context);
static ClutterActor *ooze_dock_create_app_launcher (ClutterActor *stage,
                                                    MetaDisplay  *display,
                                                    const OozeDockAppSpec *spec,
                                                    OozeDockLaunchFn launch);
static ClutterActor *ooze_dock_create_desktop_launcher (ClutterActor    *stage,
                                                        MetaDisplay     *display,
                                                        GDesktopAppInfo *app_info);
static void ooze_dock_notify_changed (ClutterActor *container);
static void ooze_dock_save_pins_from_container (ClutterActor *container);
static gboolean ooze_dock_app_is_pinned (ClutterActor *launcher);
static void ooze_dock_rebuild_icons (ClutterActor *container);
static void ooze_dock_schedule_rebuild_icons (ClutterActor *container);
static ClutterActor *ooze_dock_find_by_app_id (ClutterActor *container,
                                               const char *app_id);
static void ooze_dock_fill_icons (ClutterActor *container,
                                  MetaDisplay  *display,
                                  ClutterActor *stage);
static char *ooze_dock_desktop_id_key (GDesktopAppInfo *app_info);
static GDesktopAppInfo *ooze_dock_lookup_desktop_info (const char *app_id);
static char *ooze_dock_resolve_app_id_from_window (MetaWindow *window);
static gboolean ooze_dock_window_skippable_for_temp (MetaWindow *window);
static void ooze_dock_launch_desktop_info (GDesktopAppInfo *app_info);
static void ooze_dock_handle_desktop_click (MetaDisplay     *display,
                                            GDesktopAppInfo *app_info);
static CoglTexture *ooze_dock_texture_from_pixbuf (ClutterActor *actor,
                                                   GdkPixbuf    *pixbuf);
static void ooze_dock_on_icon_theme_changed (GSettings   *settings,
                                             const char  *key,
                                             gpointer     user_data);

typedef struct _OozeDockUnpinAnim {
  ClutterActor *container;
  ClutterActor *target;
  guint timeout_id;
  gboolean finished;
} OozeDockUnpinAnim;

static void ooze_dock_unpin_anim_done (ClutterActor *actor,
                                       gpointer user_data);

void
ooze_dock_launch_spot_path (MetaContext *context,
                          const char *path)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *spot = g_find_program_in_path ("spot");
  const char *argv[5];
  int argc = 1;

  if (!context)
    {
      g_warning ("OozeDock: no compositor context for launching spot");
      return;
    }

  if (!spot)
    {
      g_warning ("OozeDock: spot not found in PATH (is build/ on PATH?)");
      return;
    }

  argv[0] = spot;
  if (path && path[0] != '\0')
    {
      argv[1] = "--path";
      argv[2] = path;
      argc = 3;
    }
  argv[argc] = NULL;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP);
  ooze_icons_apply_to_launcher (launcher);
  ooze_appmenu_apply_to_launcher (launcher);
  ooze_appmenu_force_wayland_backend (launcher);

  if (!meta_wayland_client_new_subprocess (context,
                                           launcher,
                                           argv,
                                           &error))
    g_warning ("OozeDock: failed to launch spot: %s", error->message);
  else
    g_print ("OozeDock: launched spot%s%s\n",
             path && path[0] ? " at " : "",
             path && path[0] ? path : "");
}

void
ooze_dock_launch_spot (MetaContext *context)
{
  ooze_dock_launch_spot_path (context, NULL);
}

/* ── Ooze Command (terminal) launcher ──────────────────────────────────── */

static const char *command_dock_icon_names[] = {
  "utilities-terminal",
  NULL,
};

static void
ooze_dock_launch_command (MetaContext *context)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError)              error    = NULL;
  g_autofree char *cmd = g_find_program_in_path ("ooze-command");
  const char *argv[2]  = { NULL, NULL };

  if (!context)
    {
      g_warning ("OozeDock: no compositor context for launching ooze-command");
      return;
    }

  if (!cmd)
    {
      g_warning ("OozeDock: ooze-command not found in PATH");
      return;
    }

  argv[0] = cmd;
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP);
  ooze_icons_apply_to_launcher (launcher);
  ooze_appmenu_apply_to_launcher (launcher);
  ooze_appmenu_force_wayland_backend (launcher);

  if (!meta_wayland_client_new_subprocess (context, launcher, argv, &error))
    g_warning ("OozeDock: failed to launch ooze-command: %s", error->message);
  else
    g_print ("OozeDock: launched ooze-command\n");
}

static ClutterActor *
ooze_dock_create_command_launcher (ClutterActor *stage,
                                 MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = ooze_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = ooze_dock_themed_icon_content (stage,
                                         display,
                                         command_dock_icon_names,
                                         logical);
  /* Fallback: a dark blue pill so it looks like a terminal even without icon */
  if (!content)
    content = ooze_aqua_dock_icon_content (stage, texture, 0.10f, 0.18f, 0.35f);

  if (content)
    ooze_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical, logical,
                                      texture, texture);

  ooze_dock_wire_app_launcher (button, display, "org.ooze.Command",
                             ooze_dock_launch_command);
  return button;
}

/* ── Ooze King (System Settings) launcher ───────────────────────────────── */

static const char *king_dock_icon_names[] = {
  "preferences-system",
  "preferences-desktop",
  NULL,
};

static void
ooze_dock_launch_king (MetaContext *context)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError)              error    = NULL;
  g_autofree char *cmd = g_find_program_in_path ("ooze-king");
  const char *argv[2]  = { NULL, NULL };

  if (!context)
    {
      g_warning ("OozeDock: no compositor context for launching ooze-king");
      return;
    }

  if (!cmd)
    {
      g_warning ("OozeDock: ooze-king not found in PATH");
      return;
    }

  argv[0] = cmd;
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP);
  ooze_icons_apply_to_launcher (launcher);
  ooze_appmenu_apply_to_launcher (launcher);
  ooze_appmenu_force_wayland_backend (launcher);

  if (!meta_wayland_client_new_subprocess (context, launcher, argv, &error))
    g_warning ("OozeDock: failed to launch ooze-king: %s", error->message);
  else
    g_print ("OozeDock: launched ooze-king\n");
}

static ClutterActor *
ooze_dock_create_king_launcher (ClutterActor *stage,
                              MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = ooze_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = ooze_dock_themed_icon_content (stage,
                                         display,
                                         king_dock_icon_names,
                                         logical);
  if (!content)
    content = ooze_aqua_dock_icon_content (stage, texture, 0.35f, 0.45f, 0.70f);

  if (content)
    ooze_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical, logical,
                                      texture, texture);

  ooze_dock_wire_app_launcher (button, display, "org.ooze.King",
                             ooze_dock_launch_king);
  return button;
}

/* ── Ooze Ear (PipeWire mixer) launcher ─────────────────────────────────── */

static const char *ear_dock_icon_names[] = {
  "audio-headphones",
  "multimedia-volume-control",
  "preferences-desktop-sound",
  NULL,
};

static void
ooze_dock_launch_ear (MetaContext *context)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError)              error    = NULL;
  g_autofree char *cmd = g_find_program_in_path ("ooze-ear");
  const char *argv[2]  = { NULL, NULL };

  if (!context)
    {
      g_warning ("OozeDock: no compositor context for launching ooze-ear");
      return;
    }

  if (!cmd)
    {
      g_warning ("OozeDock: ooze-ear not found in PATH");
      return;
    }

  argv[0] = cmd;
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP);
  ooze_icons_apply_to_launcher (launcher);
  ooze_appmenu_apply_to_launcher (launcher);
  ooze_appmenu_force_wayland_backend (launcher);

  if (!meta_wayland_client_new_subprocess (context, launcher, argv, &error))
    g_warning ("OozeDock: failed to launch ooze-ear: %s", error->message);
  else
    g_print ("OozeDock: launched ooze-ear\n");
}

static ClutterActor *
ooze_dock_create_ear_launcher (ClutterActor *stage,
                             MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = ooze_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = ooze_dock_themed_icon_content (stage,
                                         display,
                                         ear_dock_icon_names,
                                         logical);
  if (!content)
    content = ooze_aqua_dock_icon_content (stage, texture, 0.20f, 0.55f, 0.35f);

  if (content)
    ooze_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical, logical,
                                      texture, texture);

  ooze_dock_wire_app_launcher (button, display, "org.ooze.Ear",
                             ooze_dock_launch_ear);
  return button;
}

/* ── Ooze Pak (Flatpak manager) launcher ────────────────────────────────── */

static const char *pak_dock_icon_names[] = {
  "system-software-install",
  "package-x-generic",
  NULL,
};

void
ooze_dock_launch_pak (MetaContext *context)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError)              error    = NULL;
  g_autofree char *cmd = g_find_program_in_path ("ooze-pak");
  const char *argv[2]  = { NULL, NULL };

  if (!context)
    {
      g_warning ("OozeDock: no compositor context for launching ooze-pak");
      return;
    }

  if (!cmd)
    {
      g_warning ("OozeDock: ooze-pak not found in PATH");
      return;
    }

  argv[0] = cmd;
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP);
  ooze_icons_apply_to_launcher (launcher);
  ooze_appmenu_apply_to_launcher (launcher);
  ooze_appmenu_force_wayland_backend (launcher);

  if (!meta_wayland_client_new_subprocess (context, launcher, argv, &error))
    g_warning ("OozeDock: failed to launch ooze-pak: %s", error->message);
  else
    g_print ("OozeDock: launched ooze-pak\n");
}

void
ooze_dock_launch_about (MetaContext *context)
{
  ooze_dock_launch_binary (context, "ooze-about");
}

static void
ooze_dock_launch_binary (MetaContext *context, const char *binary)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *cmd = NULL;
  const char *argv[2] = { NULL, NULL };

  if (!context || !binary)
    return;

  cmd = g_find_program_in_path (binary);
  if (!cmd)
    {
      g_warning ("OozeDock: %s not found in PATH", binary);
      return;
    }

  argv[0] = cmd;
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP);
  ooze_icons_apply_to_launcher (launcher);
  ooze_appmenu_apply_to_launcher (launcher);
  ooze_appmenu_force_wayland_backend (launcher);

  if (!meta_wayland_client_new_subprocess (context, launcher, argv, &error))
    g_warning ("OozeDock: failed to launch %s: %s", binary, error->message);
  else
    g_print ("OozeDock: launched %s\n", binary);
}

static void
ooze_dock_launch_eye (MetaContext *context)
{
  ooze_dock_launch_binary (context, "ooze-eye");
}

static void
ooze_dock_launch_torrent (MetaContext *context)
{
  ooze_dock_launch_binary (context, "ooze-torrent");
}

static void
ooze_dock_launch_monitor (MetaContext *context)
{
  ooze_dock_launch_binary (context, "ooze-monitor");
}

static const OozeDockAppSpec ooze_dock_app_catalog[] = {
  { "org.ooze.Spot",    "spot",          spot_dock_icon_names,     0.22f, 0.48f, 0.92f },
  { "org.ooze.Command", "ooze-command",  command_dock_icon_names,  0.10f, 0.18f, 0.35f },
  { "org.ooze.Eye",     "ooze-eye",      eye_dock_icon_names,      0.55f, 0.35f, 0.65f },
  { "org.ooze.Torrent", "ooze-torrent",  torrent_dock_icon_names,  0.20f, 0.55f, 0.35f },
  { "org.ooze.Ear",     "ooze-ear",      ear_dock_icon_names,      0.20f, 0.55f, 0.35f },
  { "org.ooze.Monitor", "ooze-monitor",  monitor_dock_icon_names,  0.30f, 0.45f, 0.70f },
  { "org.ooze.King",    "ooze-king",     king_dock_icon_names,     0.35f, 0.45f, 0.70f },
  { "org.ooze.Pak",     "ooze-pak",      pak_dock_icon_names,      0.25f, 0.55f, 0.85f },
  { "org.ooze.About",   "ooze-about",    about_dock_icon_names,    0.45f, 0.50f, 0.60f },
};

static const OozeDockAppSpec *
ooze_dock_find_spec (const char *app_id)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (ooze_dock_app_catalog); i++)
    {
      if (g_strcmp0 (ooze_dock_app_catalog[i].app_id, app_id) == 0)
        return &ooze_dock_app_catalog[i];
    }
  return NULL;
}

static OozeDockLaunchFn
ooze_dock_launch_fn_for_id (const char *app_id)
{
  if (g_strcmp0 (app_id, "org.ooze.Spot") == 0)
    return ooze_dock_launch_spot;
  if (g_strcmp0 (app_id, "org.ooze.Command") == 0)
    return ooze_dock_launch_command;
  if (g_strcmp0 (app_id, "org.ooze.Eye") == 0)
    return ooze_dock_launch_eye;
  if (g_strcmp0 (app_id, "org.ooze.Torrent") == 0)
    return ooze_dock_launch_torrent;
  if (g_strcmp0 (app_id, "org.ooze.Ear") == 0)
    return ooze_dock_launch_ear;
  if (g_strcmp0 (app_id, "org.ooze.Monitor") == 0)
    return ooze_dock_launch_monitor;
  if (g_strcmp0 (app_id, "org.ooze.King") == 0)
    return ooze_dock_launch_king;
  if (g_strcmp0 (app_id, "org.ooze.Pak") == 0)
    return ooze_dock_launch_pak;
  if (g_strcmp0 (app_id, "org.ooze.About") == 0)
    return ooze_dock_launch_about;
  return NULL;
}

static ClutterActor *
ooze_dock_create_app_launcher (ClutterActor           *stage,
                               MetaDisplay            *display,
                               const OozeDockAppSpec  *spec,
                               OozeDockLaunchFn        launch)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = ooze_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = ooze_dock_themed_icon_content (stage, display, spec->icons, logical);
  if (!content && g_strcmp0 (spec->app_id, "org.ooze.Spot") == 0)
    content = ooze_aqua_spot_icon_content (stage, display, logical);
  if (!content)
    content = ooze_aqua_dock_icon_content (stage, texture, spec->fr, spec->fg, spec->fb);

  if (content)
    ooze_aqua_actor_set_scaled_content (button,
                                        g_steal_pointer (&content),
                                        logical, logical,
                                        texture, texture);

  ooze_dock_wire_app_launcher (button, display, spec->app_id, launch);
  return button;
}

static char *
ooze_dock_desktop_id_key (GDesktopAppInfo *app_info)
{
  const char *id;

  if (!app_info)
    return NULL;
  id = g_app_info_get_id (G_APP_INFO (app_info));
  if (!id || !*id)
    return NULL;
  if (g_str_has_suffix (id, ".desktop"))
    return g_strndup (id, strlen (id) - 8);
  return g_strdup (id);
}

static GDesktopAppInfo *
ooze_dock_lookup_desktop_info (const char *app_id)
{
  g_autofree char *desktop = NULL;
  GDesktopAppInfo *info;

  if (!app_id || !*app_id)
    return NULL;

  if (g_str_has_suffix (app_id, ".desktop"))
    return g_desktop_app_info_new (app_id);

  desktop = g_strdup_printf ("%s.desktop", app_id);
  info = g_desktop_app_info_new (desktop);
  if (info)
    return info;

  return g_desktop_app_info_new (app_id);
}

static gboolean
ooze_dock_window_skippable_for_temp (MetaWindow *window)
{
  if (!window)
    return TRUE;
  if (meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    return TRUE;
  if (meta_window_get_transient_for (window) != NULL)
    return TRUE;
  if (meta_window_is_override_redirect (window))
    return TRUE;
  return FALSE;
}

static char *
ooze_dock_try_desktop_key (const char *candidate)
{
  g_autoptr (GDesktopAppInfo) info = NULL;

  if (!candidate || !*candidate)
    return NULL;
  info = ooze_dock_lookup_desktop_info (candidate);
  if (!info)
    return NULL;
  return ooze_dock_desktop_id_key (info);
}

static char *
ooze_dock_resolve_app_id_from_window (MetaWindow *window)
{
  const char *gtk_app_id;
  const char *sandboxed;
  const char *wm_class;
  const char *wm_instance;
  char *key;

  if (ooze_dock_window_skippable_for_temp (window))
    return NULL;

  gtk_app_id = meta_window_get_gtk_application_id (window);
  key = ooze_dock_try_desktop_key (gtk_app_id);
  if (key)
    return key;

  sandboxed = meta_window_get_sandboxed_app_id (window);
  key = ooze_dock_try_desktop_key (sandboxed);
  if (key)
    return key;

  wm_class = meta_window_get_wm_class (window);
  key = ooze_dock_try_desktop_key (wm_class);
  if (key)
    return key;

  wm_instance = meta_window_get_wm_class_instance (window);
  return ooze_dock_try_desktop_key (wm_instance);
}

static void
ooze_dock_launch_desktop_info (GDesktopAppInfo *app_info)
{
  g_autoptr (GAppLaunchContext) ctx = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *key = NULL;

  if (!app_info)
    return;

  ctx = g_app_launch_context_new ();
  key = ooze_dock_desktop_id_key (app_info);
  if (key && g_str_has_prefix (key, "org.ooze."))
    {
      ooze_appmenu_prepare_ooze_launch_context (ctx);
    }
  else
    {
      ooze_appmenu_prepare_launch_context (ctx);
    }

  if (!g_app_info_launch (G_APP_INFO (app_info), NULL, ctx, &error))
    g_warning ("OozeDock: failed to launch %s: %s",
               g_app_info_get_id (G_APP_INFO (app_info)),
               error ? error->message : "unknown");
}

static void
ooze_dock_handle_desktop_click (MetaDisplay     *display,
                                GDesktopAppInfo *app_info)
{
  g_autoptr (GList) windows = NULL;
  GList *all;
  GList *l;
  gboolean any_visible = FALSE;
  MetaWindow *focus_window;
  MetaWindow *target;
  gboolean app_is_focused = FALSE;
  g_autofree char *app_id = NULL;

  if (!display || !app_info)
    return;

  app_id = ooze_dock_desktop_id_key (app_info);
  all = meta_display_list_all_windows (display);
  for (l = all; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;

      if (ooze_dock_window_matches_app (window, app_info) ||
          (app_id && ooze_dock_window_matches_app_id (window, app_id)))
        windows = g_list_prepend (windows, window);
    }
  g_list_free (all);
  windows = g_list_reverse (windows);

  if (!windows)
    {
      ooze_dock_launch_desktop_info (app_info);
      return;
    }

  for (l = windows; l != NULL; l = l->next)
    {
      if (ooze_dock_window_is_visible (l->data))
        {
          any_visible = TRUE;
          break;
        }
    }

  focus_window = meta_display_get_focus_window (display);
  if (focus_window)
    {
      app_is_focused = ooze_dock_window_matches_app (focus_window, app_info) ||
                       (app_id && ooze_dock_window_matches_app_id (focus_window, app_id));
    }

  if (any_visible && app_is_focused)
    {
      for (l = windows; l != NULL; l = l->next)
        {
          MetaWindow *window = l->data;

          if (ooze_dock_window_is_visible (window) &&
              meta_window_can_minimize (window))
            meta_window_minimize (window);
        }
      return;
    }

  for (l = windows; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;

      if (meta_window_get_window_type (window) != META_WINDOW_NORMAL)
        continue;

      meta_window_unminimize (window);
      meta_window_raise (window);
    }

  target = ooze_dock_get_most_recent_window (windows);
  if (target)
    meta_window_activate (target, clutter_get_current_event_time ());
}

static ClutterActor *
ooze_dock_create_desktop_launcher (ClutterActor    *stage,
                                   MetaDisplay     *display,
                                   GDesktopAppInfo *app_info)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  GIcon *icon;
  g_autofree char *app_id = NULL;
  g_autofree char *icon_key = NULL;
  const char *icon_names[4] = { NULL, NULL, NULL, NULL };
  int logical = 48;
  int texture = ooze_aqua_icon_texture_size (display, logical);
  int n_names = 0;

  if (!app_info)
    return NULL;

  app_id = ooze_dock_desktop_id_key (app_info);
  if (!app_id)
    return NULL;

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  /* g_app_info_get_icon is transfer-none — do not unref. */
  icon = g_app_info_get_icon (G_APP_INFO (app_info));
  if (icon)
    {
      g_autoptr (GdkPixbuf) pixbuf = NULL;
      int load_size = ooze_aqua_icon_texture_size (display, logical);

      pixbuf = ooze_icon_lookup_from_gicon (icon, load_size);
      if (pixbuf)
        {
          g_autoptr (CoglTexture) texture =
            ooze_dock_texture_from_pixbuf (stage, pixbuf);

          if (texture)
            content = clutter_texture_content_new_from_texture (texture, NULL);
        }
    }

  if (!content)
    {
      if (G_IS_THEMED_ICON (icon))
        {
          const char * const *names = g_themed_icon_get_names (G_THEMED_ICON (icon));
          int i;

          for (i = 0; names && names[i] && n_names < 3; i++)
            icon_names[n_names++] = names[i];
        }
      else
        {
          icon_key = g_desktop_app_info_get_string (app_info, "Icon");
          if (icon_key && *icon_key)
            icon_names[n_names++] = icon_key;
        }
      if (app_id && n_names < 3)
        icon_names[n_names++] = app_id;
      icon_names[n_names] = NULL;

      if (n_names > 0)
        content = ooze_dock_themed_icon_content (stage, display, icon_names, logical);
    }
  if (!content)
    content = ooze_aqua_dock_icon_content (stage, texture, 0.40f, 0.45f, 0.55f);

  if (content)
    ooze_aqua_actor_set_scaled_content (button,
                                        g_steal_pointer (&content),
                                        logical, logical,
                                        texture, texture);

  g_object_set_data_full (G_OBJECT (button), "app-info",
                          g_object_ref (app_info), g_object_unref);
  /* launch-fn NULL: click path uses app-info. */
  ooze_dock_wire_app_launcher (button, display, app_id, NULL);
  /* wire stored a non-owned pointer; replace with owned copy. */
  g_object_set_data_full (G_OBJECT (button), "app-id",
                          g_steal_pointer (&app_id), g_free);
  return button;
}

static ClutterActor *
ooze_dock_create_pak_launcher (ClutterActor *stage,
                             MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = ooze_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = ooze_dock_themed_icon_content (stage,
                                         display,
                                         pak_dock_icon_names,
                                         logical);
  if (!content)
    content = ooze_aqua_dock_icon_content (stage, texture, 0.25f, 0.55f, 0.85f);

  if (content)
    ooze_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical, logical,
                                      texture, texture);

  ooze_dock_wire_app_launcher (button, display, "org.ooze.Pak",
                             ooze_dock_launch_pak);
  return button;
}

/* ── Trash can — open Trash in Spot; Pak uninstall uses in-app trash strip ─ */

static const char *trash_dock_icon_names[] = {
  "user-trash-full",
  "user-trash",
  "edit-delete",
  NULL,
};

static void
ooze_dock_launch_trash (MetaContext *context)
{
  g_autofree char *trash_files = NULL;

  trash_files = g_build_filename (g_get_user_data_dir (),
                                  "Trash", "files", NULL);
  g_mkdir_with_parents (trash_files, 0700);
  ooze_dock_launch_spot_path (context, trash_files);
}

static gboolean
on_trash_launcher_pressed (ClutterActor *actor G_GNUC_UNUSED,
                           ClutterEvent *event,
                           gpointer      user_data G_GNUC_UNUSED)
{
  MetaContext *context;

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  context = g_object_get_data (G_OBJECT (actor), "launch-context");
  ooze_dock_launch_trash (context);
  return CLUTTER_EVENT_STOP;
}

static ClutterActor *
ooze_dock_create_trash_launcher (ClutterActor *stage,
                               MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  MetaContext *context;
  int logical = 48;
  int texture = ooze_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  context = meta_display_get_context (display);
  g_object_set_data_full (G_OBJECT (button),
                          "launch-context",
                          g_object_ref (context),
                          g_object_unref);

  content = ooze_dock_themed_icon_content (stage,
                                         display,
                                         trash_dock_icon_names,
                                         logical);
  if (!content)
    content = ooze_aqua_dock_icon_content (stage, texture, 0.45f, 0.45f, 0.48f);

  if (content)
    ooze_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical, logical,
                                      texture, texture);

  g_signal_connect (button, "button-press-event",
                    G_CALLBACK (on_trash_launcher_pressed), NULL);

  /* No running-app indicator for Trash. */
  g_object_set_data (G_OBJECT (button), "app-id", "org.ooze.Trash");

  return button;
}

/* ── Running-app indicators ─────────────────────────────────────────────────
 *
 * Each dock icon gets a small white dot placed 1 px below the icon.
 * Implemented as a plain solid-colour ClutterActor (no Cairo/ClutterCanvas
 * needed — Mutter's Clutter fork does not export ClutterCanvas).
 * Opacity is toggled by a 600 ms timer that queries MetaDisplay.
 * ────────────────────────────────────────────────────────────────────────── */

static ClutterActor *
ooze_dock_create_indicator (void)
{
  ClutterActor *indicator;
  CoglColor     color;

  indicator = clutter_actor_new ();
  clutter_actor_set_size (indicator, (float) INDICATOR_W, (float) INDICATOR_H);

  /* Bright white dot, slightly transparent so it reads on any wallpaper */
  cogl_color_init_from_4f (&color, 1.0f, 1.0f, 1.0f, 0.88f);
  clutter_actor_set_background_color (indicator, &color);

  clutter_actor_set_opacity (indicator, 0);   /* hidden until app is running */
  return indicator;
}

/* Attach a running-indicator child to a launcher actor.
 * launcher_w is the launcher's logical width (used to centre the triangle). */
static void
ooze_dock_attach_indicator (ClutterActor *launcher, float launcher_w)
{
  ClutterActor *indicator = ooze_dock_create_indicator ();
  float         x         = (launcher_w - INDICATOR_W) / 2.0f;
  /* 1 px gap below the icon; slightly outside the launcher bounds but
   * Clutter does not clip children to the parent allocation by default. */
  float         y         = launcher_w + 1.0f;

  clutter_actor_set_position (indicator, x, y);
  clutter_actor_add_child (launcher, indicator);
  g_object_set_data (G_OBJECT (launcher), "indicator", indicator);
}

/* ── Running-state refresh (event-driven via OozeWindowTracker) ───────────────────────────────────────────────── */



static gboolean
ooze_dock_check_by_id (MetaDisplay *display, const char *app_id)
{
  GList    *all;
  GList    *l;
  gboolean  found = FALSE;

  all = meta_display_list_all_windows (display);
  for (l = all; l && !found; l = l->next)
    {
      if (ooze_dock_window_matches_app_id (l->data, app_id))
        found = TRUE;
    }
  g_list_free (all);
  return found;
}

static gboolean
ooze_dock_check_by_info (MetaDisplay *display, GDesktopAppInfo *app_info)
{
  GList    *all;
  GList    *l;
  gboolean  found = FALSE;

  all = meta_display_list_all_windows (display);
  for (l = all; l && !found; l = l->next)
    {
      if (ooze_dock_window_matches_app (l->data, app_info))
        found = TRUE;
    }
  g_list_free (all);
  return found;
}

static void
ooze_dock_set_geometry_for_app_id (MetaDisplay  *display,
                                 const char   *app_id,
                                 MtkRectangle *rect)
{
  GList *all;
  GList *l;

  if (!app_id || !rect)
    return;

  all = meta_display_list_all_windows (display);
  for (l = all; l != NULL; l = l->next)
    {
      MetaWindow *win = l->data;

      if (ooze_dock_window_matches_app_id (win, app_id))
        meta_window_set_icon_geometry (win, rect);
    }
  g_list_free (all);
}

static void
ooze_dock_set_geometry_for_app_info (MetaDisplay     *display,
                                   GDesktopAppInfo *app_info,
                                   MtkRectangle    *rect)
{
  GList *all;
  GList *l;

  if (!app_info || !rect)
    return;

  all = meta_display_list_all_windows (display);
  for (l = all; l != NULL; l = l->next)
    {
      MetaWindow *win = l->data;

      if (ooze_dock_window_matches_app (win, app_info))
        meta_window_set_icon_geometry (win, rect);
    }
  g_list_free (all);
}

void
ooze_dock_update_icon_geometries (MetaDisplay  *display,
                                ClutterActor *icons_container)
{
  ClutterActor *child;
  graphene_point3d_t origin;

  if (!display || !icons_container)
    return;

  for (child = clutter_actor_get_first_child (icons_container);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      const char *app_id;
      GDesktopAppInfo *app_info;
      MtkRectangle rect;
      gfloat w, h;

      w = clutter_actor_get_width (child);
      h = clutter_actor_get_height (child);
      clutter_actor_apply_transform_to_point (child,
                                             &GRAPHENE_POINT3D_INIT (0.f, 0.f, 0.f),
                                             &origin);

      rect.x = (int) origin.x;
      rect.y = (int) origin.y;
      rect.width = MAX (1, (int) w);
      rect.height = MAX (1, (int) h);

      app_id = g_object_get_data (G_OBJECT (child), "app-id");
      if (app_id)
        ooze_dock_set_geometry_for_app_id (display, app_id, &rect);

      app_info = g_object_get_data (G_OBJECT (child), "app-info");
      if (app_info)
        ooze_dock_set_geometry_for_app_info (display, app_info, &rect);
    }
}

static void
ooze_dock_refresh_running_state (MetaDisplay  *display,
                                 ClutterActor *container)
{
  ClutterActor *child;

  for (child = clutter_actor_get_first_child (container);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      ClutterActor    *indicator;
      gboolean         running = FALSE;

      indicator = g_object_get_data (G_OBJECT (child), "indicator");
      if (!indicator)
        continue;

      /* Try app-id (Spot, OozeCommand) first */
      {
        const char *app_id = g_object_get_data (G_OBJECT (child), "app-id");
        if (app_id)
          running = ooze_dock_check_by_id (display, app_id);
      }

      /* Then try GDesktopAppInfo (generic launchers) */
      if (!running)
        {
          GDesktopAppInfo *info = g_object_get_data (G_OBJECT (child), "app-info");
          if (info)
            running = ooze_dock_check_by_info (display, info);
        }

      clutter_actor_set_opacity (indicator, running ? 255 : 0);
    }

  ooze_dock_update_icon_geometries (display, container);

  /* Keep temporary running icons in sync with the pin list. */
  {
    gboolean need_rebuild = FALSE;
    gsize i;
    GList *all;
    GList *l;
    ClutterActor *child;

    for (i = 0; i < G_N_ELEMENTS (ooze_dock_app_catalog); i++)
      {
        const char *id = ooze_dock_app_catalog[i].app_id;
        ClutterActor *icon = ooze_dock_find_by_app_id (container, id);
        gboolean running = ooze_dock_check_by_id (display, id);
        gboolean pinned = icon && ooze_dock_app_is_pinned (icon);

        if (running && !icon)
          need_rebuild = TRUE;
        else if (!running && icon && !pinned &&
                 GPOINTER_TO_INT (g_object_get_data (G_OBJECT (icon), "dock-temp")))
          need_rebuild = TRUE;
      }

    if (!need_rebuild)
      {
        all = meta_display_list_all_windows (display);
        for (l = all; l != NULL; l = l->next)
          {
            g_autofree char *app_id = NULL;

            app_id = ooze_dock_resolve_app_id_from_window (l->data);
            if (!app_id)
              continue;
            if (ooze_dock_find_spec (app_id))
              continue;
            if (!ooze_dock_find_by_app_id (container, app_id))
              {
                need_rebuild = TRUE;
                break;
              }
          }
        g_list_free (all);
      }

    if (!need_rebuild)
      {
        for (child = clutter_actor_get_first_child (container);
             child != NULL;
             child = clutter_actor_get_next_sibling (child))
          {
            const char *app_id;
            GDesktopAppInfo *info;
            gboolean running = FALSE;

            if (!GPOINTER_TO_INT (g_object_get_data (G_OBJECT (child), "dock-temp")))
              continue;
            if (ooze_dock_app_is_pinned (child))
              continue;

            app_id = g_object_get_data (G_OBJECT (child), "app-id");
            info = g_object_get_data (G_OBJECT (child), "app-info");
            if (app_id)
              running = ooze_dock_check_by_id (display, app_id);
            if (!running && info)
              running = ooze_dock_check_by_info (display, info);
            if (!running)
              {
                need_rebuild = TRUE;
                break;
              }
          }
      }

    if (need_rebuild &&
        !g_object_get_data (G_OBJECT (container), "dock-dragging-any"))
      ooze_dock_schedule_rebuild_icons (container);
  }
}

static gboolean
ooze_dock_window_matches_app_id (MetaWindow *window,
                               const char *app_id)
{
  const char *gtk_app_id;
  const char *wm_class;
  const char *wm_instance;

  if (!window || !app_id || !*app_id)
    return FALSE;

  if (meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    return FALSE;

  gtk_app_id = meta_window_get_gtk_application_id (window);
  if (gtk_app_id && g_strcmp0 (gtk_app_id, app_id) == 0)
    return TRUE;

  /* GTK4 often exposes the same id as WM_CLASS when app-id props lag. */
  wm_class = meta_window_get_wm_class (window);
  wm_instance = meta_window_get_wm_class_instance (window);
  if ((wm_class && g_strcmp0 (wm_class, app_id) == 0) ||
      (wm_instance && g_strcmp0 (wm_instance, app_id) == 0))
    return TRUE;

  return FALSE;
}

static GList *
ooze_dock_get_app_windows (MetaDisplay *display,
                         const char  *app_id)
{
  GList *all_windows;
  GList *l;
  GList *matches = NULL;

  all_windows = meta_display_list_all_windows (display);
  for (l = all_windows; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;

      if (ooze_dock_window_matches_app_id (window, app_id))
        matches = g_list_prepend (matches, window);
    }

  g_list_free (all_windows);
  return g_list_reverse (matches);
}

static void
ooze_dock_handle_app_click (MetaDisplay   *display,
                          MetaContext   *context,
                          const char    *app_id,
                          OozeDockLaunchFn launch)
{
  g_autoptr (GList) windows = NULL;
  GList *l;
  gboolean any_visible = FALSE;
  MetaWindow *focus_window;
  MetaWindow *target;
  gboolean app_is_focused;

  if (!display || !app_id || !launch)
    return;

  windows = ooze_dock_get_app_windows (display, app_id);
  if (!windows)
    {
      launch (context);
      return;
    }

  for (l = windows; l != NULL; l = l->next)
    {
      if (ooze_dock_window_is_visible (l->data))
        {
          any_visible = TRUE;
          break;
        }
    }

  focus_window = meta_display_get_focus_window (display);
  app_is_focused = ooze_dock_window_matches_app_id (focus_window, app_id);

  /* Frontmost → minimize all. Otherwise raise / unminimize every window
   * of this app (macOS Dock style), then focus the most recent one. */
  if (any_visible && app_is_focused)
    {
      for (l = windows; l != NULL; l = l->next)
        {
          MetaWindow *window = l->data;

          if (ooze_dock_window_is_visible (window) &&
              meta_window_can_minimize (window))
            meta_window_minimize (window);
        }
      return;
    }

  target = NULL;
  for (l = windows; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;

      if (meta_window_get_window_type (window) != META_WINDOW_NORMAL)
        continue;

      meta_window_unminimize (window);
      meta_window_raise (window);
    }

  target = ooze_dock_get_most_recent_window (windows);
  if (target)
    meta_window_activate (target, clutter_get_current_event_time ());
}

static gboolean
on_app_launcher_pressed (ClutterActor *actor,
                         ClutterEvent *event,
                         gpointer      user_data G_GNUC_UNUSED);

static gboolean
on_app_launcher_released (ClutterActor *actor,
                          ClutterEvent *event,
                          gpointer      user_data G_GNUC_UNUSED);

static gboolean
on_app_launcher_motion (ClutterActor *actor,
                        ClutterEvent *event,
                        gpointer      user_data G_GNUC_UNUSED);

static void
ooze_dock_notify_changed (ClutterActor *container)
{
  OozeDockChangedFn cb;
  gpointer data;

  if (!container)
    return;
  cb = g_object_get_data (G_OBJECT (container), "dock-changed-fn");
  data = g_object_get_data (G_OBJECT (container), "dock-changed-data");
  if (cb)
    cb (data);
}

void
ooze_dock_set_changed_callback (ClutterActor     *container,
                                OozeDockChangedFn  callback,
                                gpointer           user_data)
{
  if (!container)
    return;
  g_object_set_data (G_OBJECT (container), "dock-changed-fn", callback);
  g_object_set_data (G_OBJECT (container), "dock-changed-data", user_data);
}

static gboolean
ooze_dock_app_is_pinned (ClutterActor *launcher)
{
  return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (launcher), "dock-pinned")) != 0;
}

static gboolean
ooze_dock_app_is_fixed (ClutterActor *launcher)
{
  return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (launcher), "dock-fixed")) != 0;
}

static void
ooze_dock_save_pins_from_container (ClutterActor *container)
{
  GPtrArray *ids;
  ClutterActor *child;

  ids = g_ptr_array_new_with_free_func (g_free);
  for (child = clutter_actor_get_first_child (container);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      const char *app_id;

      if (!ooze_dock_app_is_pinned (child) || ooze_dock_app_is_fixed (child))
        continue;
      app_id = g_object_get_data (G_OBJECT (child), "app-id");
      if (app_id && *app_id)
        g_ptr_array_add (ids, g_strdup (app_id));
    }
  g_ptr_array_add (ids, NULL);
  ooze_dock_pins_save ((char **) ids->pdata);
  g_ptr_array_free (ids, TRUE);
}

static char *
ooze_dock_downloads_path (void)
{
  const char *dir = g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD);
  if (dir && *dir)
    return g_strdup (dir);
  return g_build_filename (g_get_home_dir (), "Downloads", NULL);
}

static void
ooze_dock_launch_downloads (MetaContext *context)
{
  g_autofree char *path = ooze_dock_downloads_path ();
  g_mkdir_with_parents (path, 0700);
  ooze_dock_launch_spot_path (context, path);
}

static const char *downloads_dock_icon_names[] = {
  "folder-download", "folder-downloads", "folder", NULL,
};

static gboolean
on_downloads_pressed (ClutterActor *actor G_GNUC_UNUSED,
                      ClutterEvent *event,
                      gpointer      user_data G_GNUC_UNUSED)
{
  MetaContext *context;

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  context = g_object_get_data (G_OBJECT (actor), "launch-context");
  ooze_dock_launch_downloads (context);
  return CLUTTER_EVENT_STOP;
}

static ClutterActor *
ooze_dock_create_downloads_launcher (ClutterActor *stage,
                                     MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  MetaContext *context;
  int logical = 48;
  int texture = ooze_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  context = meta_display_get_context (display);
  g_object_set_data_full (G_OBJECT (button), "launch-context",
                          g_object_ref (context), g_object_unref);
  g_object_set_data (G_OBJECT (button), "app-id", (gpointer) "org.ooze.Downloads");
  g_object_set_data (G_OBJECT (button), "dock-fixed", GINT_TO_POINTER (1));
  g_object_set_data (G_OBJECT (button), "dock-pinned", GINT_TO_POINTER (0));

  content = ooze_dock_themed_icon_content (stage, display,
                                           downloads_dock_icon_names, logical);
  if (!content)
    content = ooze_aqua_dock_icon_content (stage, texture, 0.35f, 0.55f, 0.40f);
  if (content)
    ooze_aqua_actor_set_scaled_content (button, g_steal_pointer (&content),
                                        logical, logical, texture, texture);

  g_signal_connect (button, "button-press-event",
                    G_CALLBACK (on_downloads_pressed), NULL);
  return button;
}

static void
ooze_dock_clear_icons (ClutterActor *container)
{
  ClutterActor *child;

  while ((child = clutter_actor_get_first_child (container)))
    {
      OozeDockUnpinAnim *anim;

      anim = g_object_get_data (G_OBJECT (child), "unpin-anim");
      if (anim)
        {
          anim->finished = TRUE;
          if (anim->timeout_id)
            {
              g_source_remove (anim->timeout_id);
              anim->timeout_id = 0;
            }
          g_signal_handlers_disconnect_by_func (child,
                                                ooze_dock_unpin_anim_done,
                                                anim);
          g_object_set_data (G_OBJECT (child), "unpin-anim", NULL);
          g_free (anim);
        }
      clutter_actor_destroy (child);
    }
}

static void
ooze_dock_add_launcher (ClutterActor *container,
                        ClutterActor *launcher)
{
  clutter_actor_set_y_align (launcher, CLUTTER_ACTOR_ALIGN_CENTER);
  clutter_actor_add_child (container, launcher);
  clutter_actor_show (launcher);
}

static ClutterActor *
ooze_dock_find_by_app_id (ClutterActor *container, const char *app_id)
{
  ClutterActor *child;

  for (child = clutter_actor_get_first_child (container);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      const char *id = g_object_get_data (G_OBJECT (child), "app-id");
      if (g_strcmp0 (id, app_id) == 0)
        return child;
    }
  return NULL;
}

static void
ooze_dock_fill_icons (ClutterActor *container,
                      MetaDisplay  *display,
                      ClutterActor *stage)
{
  g_auto (GStrv) pins = NULL;
  gsize i;
  GList *all;
  GList *l;

  ooze_dock_clear_icons (container);

  pins = ooze_dock_pins_load ();
  for (i = 0; pins && pins[i]; i++)
    {
      const OozeDockAppSpec *spec = ooze_dock_find_spec (pins[i]);
      OozeDockLaunchFn launch;
      ClutterActor *launcher;

      if (spec)
        {
          launch = ooze_dock_launch_fn_for_id (pins[i]);
          if (!launch)
            continue;
          launcher = ooze_dock_create_app_launcher (stage, display, spec, launch);
        }
      else
        {
          g_autoptr (GDesktopAppInfo) info = ooze_dock_lookup_desktop_info (pins[i]);

          if (!info)
            continue;
          launcher = ooze_dock_create_desktop_launcher (stage, display, info);
          if (!launcher)
            continue;
        }
      g_object_set_data (G_OBJECT (launcher), "dock-pinned", GINT_TO_POINTER (1));
      ooze_dock_add_launcher (container, launcher);
    }

  /* Running unpinned Ooze catalog apps appear as temporary icons. */
  for (i = 0; i < G_N_ELEMENTS (ooze_dock_app_catalog); i++)
    {
      const OozeDockAppSpec *spec = &ooze_dock_app_catalog[i];
      OozeDockLaunchFn launch;
      ClutterActor *launcher;

      if (ooze_dock_find_by_app_id (container, spec->app_id))
        continue;
      if (!ooze_dock_check_by_id (display, spec->app_id))
        continue;
      launch = ooze_dock_launch_fn_for_id (spec->app_id);
      if (!launch)
        continue;
      launcher = ooze_dock_create_app_launcher (stage, display, spec, launch);
      g_object_set_data (G_OBJECT (launcher), "dock-pinned", GINT_TO_POINTER (0));
      g_object_set_data (G_OBJECT (launcher), "dock-temp", GINT_TO_POINTER (1));
      ooze_dock_add_launcher (container, launcher);
    }

  /* Any other running app (foreign) as a temp before Downloads. */
  all = meta_display_list_all_windows (display);
  for (l = all; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;
      g_autofree char *app_id = NULL;
      g_autoptr (GDesktopAppInfo) info = NULL;
      ClutterActor *launcher;

      app_id = ooze_dock_resolve_app_id_from_window (window);
      if (!app_id)
        continue;
      if (ooze_dock_find_by_app_id (container, app_id))
        continue;
      if (ooze_dock_find_spec (app_id))
        continue; /* catalog miss above means not running / already handled */

      info = ooze_dock_lookup_desktop_info (app_id);
      if (!info)
        continue;
      launcher = ooze_dock_create_desktop_launcher (stage, display, info);
      if (!launcher)
        continue;
      g_object_set_data (G_OBJECT (launcher), "dock-pinned", GINT_TO_POINTER (0));
      g_object_set_data (G_OBJECT (launcher), "dock-temp", GINT_TO_POINTER (1));
      ooze_dock_add_launcher (container, launcher);
    }
  g_list_free (all);

  ooze_dock_add_launcher (container,
                          ooze_dock_create_downloads_launcher (stage, display));
  {
    ClutterActor *trash = ooze_dock_create_trash_launcher (stage, display);
    g_object_set_data (G_OBJECT (trash), "dock-fixed", GINT_TO_POINTER (1));
    ooze_dock_add_launcher (container, trash);
  }
}

static void
ooze_dock_rebuild_icons (ClutterActor *container)
{
  g_autoptr (OozeStallScope) stall = NULL;
  MetaDisplay *display;
  ClutterActor *stage;

  display = g_object_get_data (G_OBJECT (container), "dock-display");
  stage = g_object_get_data (G_OBJECT (container), "dock-stage");
  if (!display || !stage)
    return;

  /* Freedesktop icon walks for every launcher — never call from the
   * GSettings/click stack; only via ooze_dock_schedule_rebuild_icons. */
  stall = ooze_stall_begin ("dock-rebuild-icons");
  ooze_dock_fill_icons (container, display, stage);
  ooze_dock_refresh_running_state (display, container);
  ooze_dock_notify_changed (container);
}

typedef struct _OozeDockUnpinAnim OozeDockUnpinAnim;

static void
ooze_dock_unpin_anim_finish (OozeDockUnpinAnim *anim)
{
  ClutterActor *container;

  if (!anim || anim->finished)
    return;
  anim->finished = TRUE;

  container = anim->container;
  if (anim->timeout_id)
    {
      g_source_remove (anim->timeout_id);
      anim->timeout_id = 0;
    }
  if (anim->target && CLUTTER_IS_ACTOR (anim->target))
    {
      g_signal_handlers_disconnect_by_func (anim->target,
                                            ooze_dock_unpin_anim_done,
                                            anim);
      g_object_set_data (G_OBJECT (anim->target), "unpin-anim", NULL);
    }
  g_free (anim);
  if (CLUTTER_IS_ACTOR (container))
    ooze_dock_schedule_rebuild_icons (container);
}

static void
ooze_dock_unpin_anim_done (ClutterActor *actor G_GNUC_UNUSED,
                           gpointer user_data)
{
  ooze_dock_unpin_anim_finish (user_data);
}

static gboolean
ooze_dock_unpin_anim_timeout (gpointer user_data)
{
  OozeDockUnpinAnim *anim = user_data;

  anim->timeout_id = 0;
  ooze_dock_unpin_anim_finish (anim);
  return G_SOURCE_REMOVE;
}

static void
ooze_dock_pin_menu_action (gpointer user_data, int action_id)
{
  ClutterActor *container = user_data;
  ClutterActor *target;
  const char *app_id;
  g_auto (GStrv) pins = NULL;
  GPtrArray *next;
  gsize i;
  gboolean found = FALSE;

  target = g_object_get_data (G_OBJECT (container), "pin-menu-target");
  if (!target)
    return;
  app_id = g_object_get_data (G_OBJECT (target), "app-id");
  if (!app_id || ooze_dock_app_is_fixed (target))
    return;

  pins = ooze_dock_pins_load ();
  next = g_ptr_array_new_with_free_func (g_free);

  if (action_id == DOCK_ACTION_UNPIN)
    {
      OozeDockUnpinAnim *anim;

      for (i = 0; pins && pins[i]; i++)
        {
          if (g_strcmp0 (pins[i], app_id) == 0)
            continue;
          g_ptr_array_add (next, g_strdup (pins[i]));
        }
      g_ptr_array_add (next, NULL);
      ooze_dock_pins_save ((char **) next->pdata);
      g_ptr_array_free (next, TRUE);

      g_object_set_data (G_OBJECT (target), "dock-pinned", GINT_TO_POINTER (0));
      anim = g_new0 (OozeDockUnpinAnim, 1);
      anim->container = container;
      anim->target = target;
      g_object_set_data (G_OBJECT (target), "unpin-anim", anim);
      g_signal_connect (target, "transitions-completed",
                        G_CALLBACK (ooze_dock_unpin_anim_done), anim);
      anim->timeout_id = g_timeout_add (DOCK_UNPIN_MS + 40,
                                        ooze_dock_unpin_anim_timeout, anim);

      clutter_actor_set_pivot_point (target, 0.5f, 0.5f);
      clutter_actor_set_easing_duration (target, DOCK_UNPIN_MS);
      clutter_actor_set_easing_mode (target, CLUTTER_EASE_OUT_CUBIC);
      clutter_actor_set_opacity (target, 0);
      clutter_actor_set_scale (target, 0.85, 0.85);
      return;
    }
  else if (action_id == DOCK_ACTION_PIN)
    {
      for (i = 0; pins && pins[i]; i++)
        {
          if (g_strcmp0 (pins[i], app_id) == 0)
            found = TRUE;
          g_ptr_array_add (next, g_strdup (pins[i]));
        }
      if (!found)
        g_ptr_array_add (next, g_strdup (app_id));
    }

  g_ptr_array_add (next, NULL);
  ooze_dock_pins_save ((char **) next->pdata);
  g_ptr_array_free (next, TRUE);
  ooze_dock_schedule_rebuild_icons (container);
}

static void
ooze_dock_show_pin_menu (ClutterActor *container, ClutterActor *launcher)
{
  OozeAquaMenu *menu;
  MetaContext *context;
  ClutterActor *stage;
  OozeAquaMenuEntry entries[1];
  gboolean pinned;

  if (ooze_dock_app_is_fixed (launcher))
    return;

  menu = g_object_get_data (G_OBJECT (container), "pin-menu");
  context = g_object_get_data (G_OBJECT (container), "dock-context");
  stage = g_object_get_data (G_OBJECT (container), "dock-stage");
  if (!context || !stage)
    return;

  if (!menu)
    {
      menu = ooze_aqua_menu_new (context, stage, ooze_dock_pin_menu_action, container);
      g_object_set_data (G_OBJECT (container), "pin-menu", menu);
    }

  pinned = ooze_dock_app_is_pinned (launcher);
  entries[0].label = pinned ? "Remove from Dock" : "Keep in Dock";
  entries[0].action_id = pinned ? DOCK_ACTION_UNPIN : DOCK_ACTION_PIN;
  entries[0].sensitive = TRUE;

  g_object_set_data (G_OBJECT (container), "pin-menu-target", launcher);
  ooze_aqua_menu_attach_anchor (menu, launcher);
  ooze_aqua_menu_show_for_anchor (menu, launcher, entries, 1);
}

typedef struct {
  ClutterActor *actor;
  ClutterActor *container;
} OozeDockHoldData;

static void
ooze_dock_cancel_hold (ClutterActor *actor)
{
  guint hold_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (actor), "hold-id"));
  if (hold_id)
    {
      g_source_remove (hold_id);
      g_object_set_data (G_OBJECT (actor), "hold-id", NULL);
    }
}

static gboolean
ooze_dock_hold_timeout (gpointer user_data)
{
  OozeDockHoldData *data = user_data;
  ClutterActor *actor = data->actor;
  ClutterActor *container = data->container;
  ClutterActor *stage;
  ClutterGrab *grab;

  g_object_set_data (G_OBJECT (actor), "hold-id", NULL);
  g_object_set_data (G_OBJECT (actor), "dock-dragging", GINT_TO_POINTER (1));
  if (container)
    g_object_set_data (G_OBJECT (container), "dock-dragging-any", GINT_TO_POINTER (1));

  clutter_actor_set_pivot_point (actor, 0.5f, 0.5f);
  clutter_actor_set_easing_duration (actor, DOCK_DRAG_MS);
  clutter_actor_set_easing_mode (actor, CLUTTER_EASE_OUT_CUBIC);
  clutter_actor_set_opacity (actor, 180);
  clutter_actor_set_scale (actor, 1.08, 1.08);
  clutter_actor_set_translation (actor, 0.f, -4.f, 0.f);

  stage = g_object_get_data (G_OBJECT (container), "dock-stage");
  if (stage)
    {
      grab = clutter_stage_grab (CLUTTER_STAGE (stage), actor);
      g_object_set_data_full (G_OBJECT (actor), "dock-grab", grab,
                              (GDestroyNotify) clutter_grab_dismiss);
    }

  g_free (data);
  return G_SOURCE_REMOVE;
}

static int
ooze_dock_index_at_x (ClutterActor *container, gfloat stage_x)
{
  ClutterActor *child;
  int index = 0;
  int best = 0;
  gfloat best_dist = G_MAXFLOAT;

  for (child = clutter_actor_get_first_child (container);
       child != NULL;
       child = clutter_actor_get_next_sibling (child), index++)
    {
      graphene_point3d_t origin;
      gfloat mid;
      gfloat dist;

      if (ooze_dock_app_is_fixed (child))
        continue;

      clutter_actor_apply_transform_to_point (child,
                                              &GRAPHENE_POINT3D_INIT (0.f, 0.f, 0.f),
                                              &origin);
      mid = origin.x + clutter_actor_get_width (child) / 2.0f;
      dist = fabsf (stage_x - mid);
      if (dist < best_dist)
        {
          best_dist = dist;
          best = index;
        }
    }
  return best;
}

static int
ooze_dock_child_index (ClutterActor *container, ClutterActor *actor)
{
  ClutterActor *child;
  int index = 0;

  for (child = clutter_actor_get_first_child (container);
       child != NULL;
       child = clutter_actor_get_next_sibling (child), index++)
    {
      if (child == actor)
        return index;
    }
  return -1;
}

static gboolean
on_app_launcher_motion (ClutterActor *actor,
                        ClutterEvent *event,
                        gpointer      user_data G_GNUC_UNUSED)
{
  ClutterActor *container;
  gfloat x, y;
  gfloat px, py;

  if (!GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "dock-dragging")))
    {
      /* Cancel hold if the pointer moves too far before hold fires. */
      if (g_object_get_data (G_OBJECT (actor), "hold-id"))
        {
          clutter_event_get_coords (event, &x, &y);
          px = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "press-x"));
          py = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "press-y"));
          if (fabsf (x - px) > 8.0f || fabsf (y - py) > 8.0f)
            ooze_dock_cancel_hold (actor);
        }
      return CLUTTER_EVENT_PROPAGATE;
    }

  container = clutter_actor_get_parent (actor);
  if (!container)
    return CLUTTER_EVENT_STOP;

  clutter_event_get_coords (event, &x, &y);
  {
    int cur = ooze_dock_child_index (container, actor);
    int want = ooze_dock_index_at_x (container, x);
    ClutterActor *at;
    int i;

    if (want < 0 || want == cur)
      return CLUTTER_EVENT_STOP;

    /* Do not move past Downloads/Trash. */
    at = clutter_actor_get_first_child (container);
    for (i = 0; at && i < want; i++)
      at = clutter_actor_get_next_sibling (at);
    if (at && ooze_dock_app_is_fixed (at))
      return CLUTTER_EVENT_STOP;

    clutter_actor_set_child_at_index (container, actor, want);
  }
  return CLUTTER_EVENT_STOP;
}

static gboolean
on_app_launcher_released (ClutterActor *actor,
                          ClutterEvent *event,
                          gpointer      user_data G_GNUC_UNUSED)
{
  MetaDisplay *display;
  MetaContext *context;
  const char *app_id;
  OozeDockLaunchFn launch;
  GDesktopAppInfo *app_info;
  ClutterActor *container;
  gboolean was_dragging;
  guint button;

  button = clutter_event_get_button (event);
  ooze_dock_cancel_hold (actor);

  was_dragging = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (actor), "dock-dragging"));
  g_object_set_data (G_OBJECT (actor), "dock-dragging", NULL);
  g_object_set_data (G_OBJECT (actor), "dock-grab", NULL);
  container = clutter_actor_get_parent (actor);
  if (container)
    g_object_set_data (G_OBJECT (container), "dock-dragging-any", NULL);

  clutter_actor_set_pivot_point (actor, 0.5f, 0.5f);
  clutter_actor_set_easing_duration (actor, DOCK_DRAG_MS);
  clutter_actor_set_easing_mode (actor, CLUTTER_EASE_OUT_CUBIC);
  clutter_actor_set_opacity (actor, 255);
  clutter_actor_set_scale (actor, 1.0, 1.0);
  clutter_actor_set_translation (actor, 0.f, 0.f, 0.f);

  if (button != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  if (was_dragging)
    {
      if (container && ooze_dock_app_is_pinned (actor))
        {
          ooze_dock_save_pins_from_container (container);
          ooze_dock_notify_changed (container);
        }
      return CLUTTER_EVENT_STOP;
    }

  display = g_object_get_data (G_OBJECT (actor), "launch-display");
  context = g_object_get_data (G_OBJECT (actor), "launch-context");
  app_id = g_object_get_data (G_OBJECT (actor), "app-id");
  launch = g_object_get_data (G_OBJECT (actor), "launch-fn");
  app_info = g_object_get_data (G_OBJECT (actor), "app-info");
  if (!display || !context || !app_id)
    return CLUTTER_EVENT_STOP;

  if (app_info && !launch)
    ooze_dock_handle_desktop_click (display, app_info);
  else if (launch)
    ooze_dock_handle_app_click (display, context, app_id, launch);
  return CLUTTER_EVENT_STOP;
}

static gboolean
on_app_launcher_pressed (ClutterActor *actor,
                         ClutterEvent *event,
                         gpointer      user_data G_GNUC_UNUSED)
{
  MetaDisplay *display;
  MetaContext *context;
  const char *app_id;
  OozeDockLaunchFn launch;
  GDesktopAppInfo *app_info;
  ClutterActor *container;
  guint button;
  gfloat x, y;

  button = clutter_event_get_button (event);
  display = g_object_get_data (G_OBJECT (actor), "launch-display");
  context = g_object_get_data (G_OBJECT (actor), "launch-context");
  app_id = g_object_get_data (G_OBJECT (actor), "app-id");
  launch = g_object_get_data (G_OBJECT (actor), "launch-fn");
  app_info = g_object_get_data (G_OBJECT (actor), "app-info");
  container = clutter_actor_get_parent (actor);

  if (!display || !context || !app_id || (!launch && !app_info))
    return CLUTTER_EVENT_STOP;

  if (button == CLUTTER_BUTTON_SECONDARY)
    {
      if (container)
        ooze_dock_show_pin_menu (container, actor);
      return CLUTTER_EVENT_STOP;
    }

  if (button == CLUTTER_BUTTON_MIDDLE)
    {
      if (app_info && !launch)
        ooze_dock_launch_desktop_info (app_info);
      else if (launch)
        launch (context);
      return CLUTTER_EVENT_STOP;
    }

  if (button != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  clutter_event_get_coords (event, &x, &y);
  g_object_set_data (G_OBJECT (actor), "press-x", GINT_TO_POINTER ((int) x));
  g_object_set_data (G_OBJECT (actor), "press-y", GINT_TO_POINTER ((int) y));
  g_object_set_data (G_OBJECT (actor), "dock-dragging", NULL);
  ooze_dock_cancel_hold (actor);

  if (ooze_dock_app_is_pinned (actor) && !ooze_dock_app_is_fixed (actor) && container)
    {
      OozeDockHoldData *hold = g_new0 (OozeDockHoldData, 1);
      guint id;

      hold->actor = actor;
      hold->container = container;
      id = g_timeout_add (DOCK_HOLD_MS, ooze_dock_hold_timeout, hold);
      g_object_set_data (G_OBJECT (actor), "hold-id", GUINT_TO_POINTER (id));
    }

  return CLUTTER_EVENT_STOP;
}

static void
ooze_dock_wire_app_launcher (ClutterActor  *button,
                           MetaDisplay   *display,
                           const char    *app_id,
                           OozeDockLaunchFn launch)
{
  MetaContext *context;

  context = meta_display_get_context (display);
  g_object_set_data (G_OBJECT (button), "launch-display", display);
  g_object_set_data_full (G_OBJECT (button),
                          "launch-context",
                          g_object_ref (context),
                          g_object_unref);
  g_object_set_data (G_OBJECT (button), "app-id", (gpointer) app_id);
  g_object_set_data (G_OBJECT (button), "launch-fn", launch);
  g_signal_connect (button, "button-press-event",
                    G_CALLBACK (on_app_launcher_pressed), NULL);
  g_signal_connect (button, "button-release-event",
                    G_CALLBACK (on_app_launcher_released), NULL);
  g_signal_connect (button, "motion-event",
                    G_CALLBACK (on_app_launcher_motion), NULL);
  ooze_dock_attach_indicator (button, clutter_actor_get_width (button));
}

ClutterActor *
ooze_dock_create_spot_launcher (ClutterActor *stage,
                              MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = ooze_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = ooze_dock_themed_icon_content (stage,
                                         display,
                                         spot_dock_icon_names,
                                         logical);
  if (!content)
    content = ooze_aqua_spot_icon_content (stage, display, logical);
  if (!content)
    content = ooze_aqua_dock_icon_content (stage, texture, 0.22f, 0.48f, 0.92f);
  if (content)
    ooze_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical,
                                      logical,
                                      texture,
                                      texture);

  ooze_dock_wire_app_launcher (button, display, "org.ooze.Spot",
                             ooze_dock_launch_spot);
  return button;
}

static gboolean
ooze_dock_window_matches_app (MetaWindow       *window,
                            GDesktopAppInfo  *app_info)
{
  const char *wm_class;
  const char *wm_instance;
  const char *gtk_app_id;
  const char *startup_wm_class;
  const char *exec;
  g_autofree char *desktop_id = NULL;

  if (meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    return FALSE;

  if (meta_window_get_transient_for (window) != NULL)
    return FALSE;

  wm_class = meta_window_get_wm_class (window);
  wm_instance = meta_window_get_wm_class_instance (window);
  gtk_app_id = meta_window_get_gtk_application_id (window);
  startup_wm_class = g_desktop_app_info_get_startup_wm_class (app_info);
  exec = g_app_info_get_executable (G_APP_INFO (app_info));
  if (g_str_has_suffix (g_app_info_get_id (G_APP_INFO (app_info)), ".desktop"))
    desktop_id = g_strndup (g_app_info_get_id (G_APP_INFO (app_info)),
                            strlen (g_app_info_get_id (G_APP_INFO (app_info))) - 8);

  if (startup_wm_class &&
      ((wm_class && g_strcmp0 (startup_wm_class, wm_class) == 0) ||
       (wm_instance && g_strcmp0 (startup_wm_class, wm_instance) == 0)))
    return TRUE;

  if (gtk_app_id && desktop_id && g_strcmp0 (gtk_app_id, desktop_id) == 0)
    return TRUE;

  if (exec && wm_instance &&
      g_ascii_strcasecmp (g_path_get_basename (exec), wm_instance) == 0)
    return TRUE;

  return FALSE;
}

static gboolean
ooze_dock_window_is_visible (MetaWindow *window)
{
  return meta_window_showing_on_its_workspace (window) &&
         !meta_window_is_hidden (window);
}

static MetaWindow *
ooze_dock_get_most_recent_window (GList *windows)
{
  MetaWindow *best = NULL;
  guint32 best_time = 0;
  GList *l;

  for (l = windows; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;
      guint32 user_time = meta_window_get_user_time (window);

      if (!best || user_time >= best_time)
        {
          best = window;
          best_time = user_time;
        }
    }

  return best;
}

static CoglTexture *
ooze_dock_texture_from_pixbuf (ClutterActor *actor,
                             GdkPixbuf    *pixbuf)
{
  ClutterContext *clutter_context;
  ClutterBackend *backend;
  CoglContext *cogl_context;
  CoglTexture *texture;
  CoglPixelFormat format;
  g_autoptr (GError) error = NULL;
  int width;
  int height;
  int rowstride;

  clutter_context = clutter_actor_get_context (actor);
  backend = clutter_context_get_backend (clutter_context);
  cogl_context = clutter_backend_get_cogl_context (backend);

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  format = gdk_pixbuf_get_has_alpha (pixbuf)
    ? COGL_PIXEL_FORMAT_RGBA_8888
    : COGL_PIXEL_FORMAT_RGB_888;

  texture = cogl_texture_2d_new_from_data (cogl_context,
                                           width,
                                           height,
                                           format,
                                           rowstride,
                                           gdk_pixbuf_get_pixels (pixbuf),
                                           &error);
  if (!texture)
    g_warning ("OozeDock: failed to upload icon texture: %s",
               error ? error->message : "unknown error");

  return texture;
}


static GdkPixbuf *
ooze_dock_load_themed_icon (const char *icon_name,
                          int         size)
{
  const char *alias;
  GdkPixbuf *pixbuf;

  if (!icon_name || icon_name[0] == '\0')
    return NULL;

  pixbuf = ooze_icon_lookup_load (icon_name, size);
  if (pixbuf)
    return pixbuf;

  alias = ooze_dock_icon_alias (icon_name);
  if (alias && g_strcmp0 (alias, icon_name) != 0)
    return ooze_icon_lookup_load (alias, size);

  return NULL;
}

ClutterContent *
ooze_dock_themed_icon_content (ClutterActor       *ref_actor,
                             MetaDisplay        *display,
                             const char * const *icon_names,
                             int                 logical_size)
{
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autoptr (CoglTexture) texture = NULL;
  int load_size;
  gsize i;

  if (!icon_names)
    return NULL;

  load_size = ooze_aqua_icon_texture_size (display, logical_size);

  for (i = 0; icon_names[i] != NULL; i++)
    {
      pixbuf = ooze_dock_load_themed_icon (icon_names[i], load_size);
      if (pixbuf)
        break;
    }

  if (!pixbuf)
    return NULL;

  texture = ooze_dock_texture_from_pixbuf (ref_actor, pixbuf);
  if (!texture)
    return NULL;

  return clutter_texture_content_new_from_texture (texture, NULL);
}

static void
ooze_dock_layout_launchers (OozeDock *dock,
                          gfloat  dock_width)
{
  ClutterActor *launchers;
  ClutterActor *child;
  gsize count = 0;
  gfloat total_width;
  gfloat x;

  launchers = clutter_actor_get_first_child (dock->actor);
  if (!launchers)
    return;

  for (child = clutter_actor_get_first_child (launchers);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    count++;

  if (count == 0)
    return;

  total_width = count * LAUNCHER_SIZE + (count - 1) * LAUNCHER_SPACING;
  x = (dock_width - total_width) / 2.0f;

  for (child = clutter_actor_get_first_child (launchers);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      clutter_actor_set_position (child,
                                  x,
                                  (DOCK_HEIGHT - LAUNCHER_SIZE) / 2.0f);
      x += LAUNCHER_SIZE + LAUNCHER_SPACING;
    }
}

OozeDock *
ooze_dock_new (MetaContext     *context,
             MetaDisplay     *display,
             MetaCompositor  *compositor)
{
  OozeDock *dock;
  ClutterActor *stage;
  ClutterActor *window_group;
  ClutterActor *launchers;
  CoglColor dock_color;
  g_autoptr (MetaBackground) background = NULL;
  ClutterContent *content;
  MetaBackgroundContent *background_content;

  dock = g_new0 (OozeDock, 1);
  dock->context = g_object_ref (context);
  dock->display = display;

  stage = CLUTTER_ACTOR (meta_compositor_get_stage (compositor));
  window_group = meta_compositor_get_window_group (compositor);

  cogl_color_init_from_4f (&dock_color, 0.11f, 0.12f, 0.16f, 1.0f);

  content = meta_background_content_new (display, 0);
  background_content = META_BACKGROUND_CONTENT (content);
  background = meta_background_new (display);
  meta_background_set_color (background, &dock_color);
  meta_background_content_set_background (background_content, background);

  dock->actor = clutter_actor_new ();
  clutter_actor_set_content (dock->actor, content);
  g_object_unref (content);
  clutter_actor_set_reactive (dock->actor, FALSE);

  launchers = clutter_actor_new ();
  clutter_actor_set_reactive (launchers, FALSE);
  clutter_actor_add_child (dock->actor, launchers);

  clutter_actor_add_child (stage, dock->actor);
  clutter_actor_set_child_above_sibling (stage, dock->actor, window_group);

  ooze_dock_populate_container (dock->context, dock->display, stage, launchers);

  clutter_actor_show (dock->actor);

  return dock;
}

void
ooze_dock_resize (OozeDock      *dock,
                MetaDisplay  *display)
{
  int width;
  int height;

  if (!dock || !dock->actor)
    return;

  meta_display_get_size (display, &width, &height);
  clutter_actor_set_size (dock->actor, (gfloat) width, DOCK_HEIGHT);
  clutter_actor_set_position (dock->actor,
                              0.0f,
                              (gfloat) height - DOCK_HEIGHT);

  ooze_dock_layout_launchers (dock, (gfloat) width);
}

ClutterActor *
ooze_dock_get_actor (OozeDock *dock)
{
  return dock ? dock->actor : NULL;
}

static ClutterActor *ooze_dock_downloads_actor (void);

static void
ooze_dock_spring_cancel (ClutterActor *container)
{
  guint id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (container), "dock-spring-id"));
  if (id)
    {
      g_source_remove (id);
      g_object_set_data (G_OBJECT (container), "dock-spring-id", NULL);
    }
}

static gboolean
ooze_dock_spring_fire (gpointer user_data)
{
  ClutterActor *container = user_data;
  MetaContext *context;

  g_object_set_data (G_OBJECT (container), "dock-spring-id", NULL);
  context = g_object_get_data (G_OBJECT (container), "dock-context");
  if (context)
    ooze_dock_launch_downloads (context);
  return G_SOURCE_REMOVE;
}

static void
ooze_dock_spring_arm_downloads (ClutterActor *container)
{
  guint id;

  if (GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (container), "dock-spring-id")))
    return;
  id = g_timeout_add (DOCK_SPRING_MS, ooze_dock_spring_fire, container);
  g_object_set_data (G_OBJECT (container), "dock-spring-id", GUINT_TO_POINTER (id));
}

static void
ooze_dock_on_dnd_position (MetaDnd *dnd G_GNUC_UNUSED,
                           int x,
                           int y,
                           ClutterActor *container)
{
  gboolean over = ooze_dock_point_is_downloads (
      g_object_get_data (G_OBJECT (container), "dock-display"), x, y);

  g_object_set_data (G_OBJECT (container), "dock-dnd-over-downloads",
                     GINT_TO_POINTER (over ? 1 : 0));
  if (over)
    {
      g_autofree char *path = ooze_dock_downloads_path ();
      ooze_dnd_bridge_set_hover_dir (path);
      ooze_dock_spring_arm_downloads (container);
    }
  else
    {
      ooze_dock_spring_cancel (container);
    }
}

static void
ooze_dock_on_dnd_leave (MetaDnd *dnd G_GNUC_UNUSED, ClutterActor *container)
{
  g_object_set_data (G_OBJECT (container), "dock-dnd-over-downloads", NULL);
  ooze_dock_spring_cancel (container);
  /* Hover cleared by desktop leave or Spot drag-end; if Downloads was last
   * hover, keep it sticky until Spot consumes (do not clear here). */
}

static ClutterActor *
ooze_dock_downloads_actor (void)
{
  if (!ooze_dock_icons_container)
    return NULL;
  return ooze_dock_find_by_app_id (ooze_dock_icons_container, "org.ooze.Downloads");
}

static void
ooze_dock_on_windows_changed (gpointer user_data)
{
  ClutterActor *container = user_data;
  MetaDisplay *display;

  display = g_object_get_data (G_OBJECT (container), "dock-display");
  if (display)
    ooze_dock_refresh_running_state (display, container);
}

/* Everything wired to the icons container must be torn down with it —
 * the window tracker, the MetaDnd handlers (the backend outlives the
 * dock) and any pending timeouts/idles that hold raw container pointers. */
static void
ooze_dock_on_container_destroy (ClutterActor *container,
                                gpointer      user_data G_GNUC_UNUSED)
{
  OozeWindowTracker *tracker;
  MetaDnd *dnd;
  OozeAquaMenu *pin_menu;
  guint id;

  tracker = g_object_get_data (G_OBJECT (container), "window-tracker");
  if (tracker)
    {
      g_object_set_data (G_OBJECT (container), "window-tracker", NULL);
      ooze_window_tracker_free (tracker);
    }

  dnd = g_object_get_data (G_OBJECT (container), "dock-dnd");
  if (dnd)
    {
      g_signal_handlers_disconnect_by_func (dnd, ooze_dock_on_dnd_position,
                                            container);
      g_signal_handlers_disconnect_by_func (dnd, ooze_dock_on_dnd_leave,
                                            container);
      g_object_set_data (G_OBJECT (container), "dock-dnd", NULL);
    }

  ooze_dock_spring_cancel (container);

  id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (container),
                                            "dock-icon-rebuild-idle"));
  if (id)
    {
      g_source_remove (id);
      g_object_set_data (G_OBJECT (container), "dock-icon-rebuild-idle", NULL);
    }

  pin_menu = g_object_get_data (G_OBJECT (container), "pin-menu");
  if (pin_menu)
    {
      g_object_set_data (G_OBJECT (container), "pin-menu", NULL);
      ooze_aqua_menu_destroy (pin_menu);
    }

  if (ooze_dock_icons_container == container)
    ooze_dock_icons_container = NULL;
}

void
ooze_dock_populate_container (MetaContext   *context,
                            MetaDisplay   *display,
                            ClutterActor  *stage,
                            ClutterActor  *container)
{
  MetaBackend *backend;
  MetaDnd *dnd;

  ooze_dock_icons_container = container;
  if (!g_object_get_data (G_OBJECT (container), "dock-destroy-wired"))
    {
      g_signal_connect (container, "destroy",
                        G_CALLBACK (ooze_dock_on_container_destroy), NULL);
      g_object_set_data (G_OBJECT (container), "dock-destroy-wired",
                         GINT_TO_POINTER (1));
    }
  g_object_set_data (G_OBJECT (container), "dock-display", display);
  g_object_set_data (G_OBJECT (container), "dock-stage", stage);
  if (context)
    g_object_set_data_full (G_OBJECT (container), "dock-context",
                            g_object_ref (context), g_object_unref);

  ooze_dock_fill_icons (container, display, stage);

  if (!g_object_get_data (G_OBJECT (container), "dock-icon-settings"))
    {
      GSettings *settings = g_settings_new ("org.gnome.desktop.interface");

      /* Icon packs only — cursor-theme must not rebuild dock icons. */
      g_signal_connect (settings, "changed::icon-theme",
                        G_CALLBACK (ooze_dock_on_icon_theme_changed), container);
      g_object_set_data_full (G_OBJECT (container), "dock-icon-settings",
                              settings, g_object_unref);
    }

  if (!g_object_get_data (G_OBJECT (container), "window-tracker"))
    {
      OozeWindowTracker *tracker = ooze_window_tracker_new (display);

      ooze_window_tracker_set_changed_callback (tracker,
                                                ooze_dock_on_windows_changed,
                                                container);
      g_object_set_data (G_OBJECT (container), "window-tracker", tracker);
    }

  ooze_dock_refresh_running_state (display, container);

  if (!g_object_get_data (G_OBJECT (container), "dock-dnd-wired") && context)
    {
      backend = meta_context_get_backend (context);
      dnd = backend ? meta_backend_get_dnd (backend) : NULL;
      if (dnd)
        {
          g_signal_connect (dnd, "dnd-position-change",
                            G_CALLBACK (ooze_dock_on_dnd_position), container);
          g_signal_connect (dnd, "dnd-leave",
                            G_CALLBACK (ooze_dock_on_dnd_leave), container);
          g_object_set_data (G_OBJECT (container), "dock-dnd", dnd);
          g_object_set_data (G_OBJECT (container), "dock-dnd-wired",
                             GINT_TO_POINTER (1));
        }
    }
}

static gboolean
ooze_dock_rebuild_icons_idle (gpointer user_data)
{
  ClutterActor *container = CLUTTER_ACTOR (user_data);

  g_object_set_data (G_OBJECT (container), "dock-icon-rebuild-idle", NULL);
  if (CLUTTER_IS_ACTOR (container))
    ooze_dock_rebuild_icons (container);
  return G_SOURCE_REMOVE;
}

static void
ooze_dock_schedule_rebuild_icons (ClutterActor *container)
{
  guint id;

  if (!CLUTTER_IS_ACTOR (container))
    return;
  if (g_object_get_data (G_OBJECT (container), "dock-icon-rebuild-idle"))
    return;

  id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                        ooze_dock_rebuild_icons_idle,
                        g_object_ref (container),
                        g_object_unref);
  g_object_set_data (G_OBJECT (container), "dock-icon-rebuild-idle",
                     GUINT_TO_POINTER (id));
}

static void
ooze_dock_on_icon_theme_changed (GSettings   *settings G_GNUC_UNUSED,
                                 const char  *key G_GNUC_UNUSED,
                                 gpointer     user_data)
{
  /* Drop cached pixbufs before the idle rebuild walks the theme again. */
  ooze_icon_lookup_cache_invalidate ();
  /* Idle so GSettings churn / Themes UI writes do not block the compositor
   * click that changed the pack (heavy Freedesktop walk per dock icon). */
  ooze_dock_schedule_rebuild_icons (CLUTTER_ACTOR (user_data));
}

gboolean
ooze_dock_point_is_downloads (MetaDisplay *display G_GNUC_UNUSED,
                              int          x,
                              int          y)
{
  ClutterActor *btn;
  graphene_point_t stage_pt;
  graphene_point_t local_pt;

  btn = ooze_dock_downloads_actor ();
  if (!btn)
    return FALSE;

  stage_pt.x = (float) x;
  stage_pt.y = (float) y;
  if (!clutter_actor_transform_stage_point (btn, stage_pt.x, stage_pt.y,
                                            &local_pt.x, &local_pt.y))
    return FALSE;

  return local_pt.x >= 0.0f && local_pt.y >= 0.0f &&
         local_pt.x <= clutter_actor_get_width (btn) &&
         local_pt.y <= clutter_actor_get_height (btn);
}

void
ooze_dock_free (OozeDock *dock)
{
  if (!dock)
    return;

  if (ooze_dock_icons_container && dock->actor &&
      clutter_actor_contains (dock->actor, ooze_dock_icons_container))
    ooze_dock_icons_container = NULL;

  g_clear_pointer (&dock->actor, clutter_actor_destroy);
  g_clear_object (&dock->context);
  g_free (dock);
}
