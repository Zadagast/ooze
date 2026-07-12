#include "my-dock.h"
#include "my-aqua-draw.h"
#include "my-icons.h"

#define __COGL_H_INSIDE__
#include "cogl/cogl-texture-2d.h"
#undef __COGL_H_INSIDE__

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gdesktopappinfo.h>
#include <png.h>

#include <errno.h>
#include <stdio.h>

#include <meta/meta-background-content.h>
#include <meta/meta-background.h>
#include <meta/meta-wayland-client.h>
#include <meta/window.h>
#include <mtk/mtk.h>

#define DOCK_HEIGHT       48.0f
#define LAUNCHER_SIZE     40.0f
#define ICON_SIZE         32
#define LAUNCHER_SPACING  8.0f

/* Running-app indicator: small downward triangle below each dock icon */
#define INDICATOR_W       6
#define INDICATOR_H       4

struct _MyDock
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
} MyDockIconAlias;

static const MyDockIconAlias dock_icon_aliases[] = {
  { "org.gnome.Ptyxis", "utilities-terminal" },
  { "org.gnome.Ptyxis-symbolic", "utilities-terminal" },
  { "org.gnome.TextEditor", "accessories-text-editor" },
  { "org.gnome.TextEditor-symbolic", "accessories-text-editor" },
  { "org.gnome.Calculator", "accessories-calculator" },
  { "org.gnome.Calculator-symbolic", "accessories-calculator" },
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
  { NULL, NULL },
};

static const char *
my_dock_icon_alias (const char *icon_name)
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

static gboolean  my_dock_window_is_visible    (MetaWindow *window);
static MetaWindow *my_dock_get_most_recent_window (GList *windows);
static gboolean  my_dock_window_matches_app   (MetaWindow *window, GDesktopAppInfo *app_info);
static gboolean  my_dock_window_matches_app_id (MetaWindow *window, const char *app_id);
static void      my_dock_attach_indicator     (ClutterActor *launcher, float launcher_w);

typedef void (*MyDockLaunchFn) (MetaContext *context);
static void      my_dock_wire_app_launcher    (ClutterActor  *button,
                                               MetaDisplay   *display,
                                               const char    *app_id,
                                               MyDockLaunchFn launch);

void
my_dock_launch_spot_path (MetaContext *context,
                          const char *path)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *spot = g_find_program_in_path ("spot");
  const char *argv[5];
  int argc = 1;

  if (!context)
    {
      g_warning ("MyDock: no compositor context for launching spot");
      return;
    }

  if (!spot)
    {
      g_warning ("MyDock: spot not found in PATH (is build/ on PATH?)");
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
  my_icons_apply_to_launcher (launcher);

  if (!meta_wayland_client_new_subprocess (context,
                                           launcher,
                                           argv,
                                           &error))
    g_warning ("MyDock: failed to launch spot: %s", error->message);
  else
    g_print ("MyDock: launched spot%s%s\n",
             path && path[0] ? " at " : "",
             path && path[0] ? path : "");
}

void
my_dock_launch_spot (MetaContext *context)
{
  my_dock_launch_spot_path (context, NULL);
}

/* ── Ooze Command (terminal) launcher ──────────────────────────────────── */

static const char *command_dock_icon_names[] = {
  "org.ooze.Command",
  "utilities-terminal",
  NULL,
};

static void
my_dock_launch_command (MetaContext *context)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError)              error    = NULL;
  g_autofree char *cmd = g_find_program_in_path ("ooze-command");
  const char *argv[2]  = { NULL, NULL };

  if (!context)
    {
      g_warning ("MyDock: no compositor context for launching ooze-command");
      return;
    }

  if (!cmd)
    {
      g_warning ("MyDock: ooze-command not found in PATH");
      return;
    }

  argv[0] = cmd;
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP);
  my_icons_apply_to_launcher (launcher);

  if (!meta_wayland_client_new_subprocess (context, launcher, argv, &error))
    g_warning ("MyDock: failed to launch ooze-command: %s", error->message);
  else
    g_print ("MyDock: launched ooze-command\n");
}

static ClutterActor *
my_dock_create_command_launcher (ClutterActor *stage,
                                 MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = my_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = my_dock_themed_icon_content (stage,
                                         display,
                                         command_dock_icon_names,
                                         logical);
  /* Fallback: a dark blue pill so it looks like a terminal even without icon */
  if (!content)
    content = my_aqua_dock_icon_content (stage, texture, 0.10f, 0.18f, 0.35f);

  if (content)
    my_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical, logical,
                                      texture, texture);

  my_dock_wire_app_launcher (button, display, "org.ooze.Command",
                             my_dock_launch_command);
  return button;
}

/* ── Ooze King (System Settings) launcher ───────────────────────────────── */

static const char *king_dock_icon_names[] = {
  "org.ooze.King",
  "preferences-system",
  "preferences-desktop",
  NULL,
};

static void
my_dock_launch_king (MetaContext *context)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError)              error    = NULL;
  g_autofree char *cmd = g_find_program_in_path ("ooze-king");
  const char *argv[2]  = { NULL, NULL };

  if (!context)
    {
      g_warning ("MyDock: no compositor context for launching ooze-king");
      return;
    }

  if (!cmd)
    {
      g_warning ("MyDock: ooze-king not found in PATH");
      return;
    }

  argv[0] = cmd;
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP);
  my_icons_apply_to_launcher (launcher);

  if (!meta_wayland_client_new_subprocess (context, launcher, argv, &error))
    g_warning ("MyDock: failed to launch ooze-king: %s", error->message);
  else
    g_print ("MyDock: launched ooze-king\n");
}

static ClutterActor *
my_dock_create_king_launcher (ClutterActor *stage,
                              MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = my_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = my_dock_themed_icon_content (stage,
                                         display,
                                         king_dock_icon_names,
                                         logical);
  if (!content)
    content = my_aqua_dock_icon_content (stage, texture, 0.35f, 0.45f, 0.70f);

  if (content)
    my_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical, logical,
                                      texture, texture);

  my_dock_wire_app_launcher (button, display, "org.ooze.King",
                             my_dock_launch_king);
  return button;
}

/* ── Ooze Ear (PipeWire mixer) launcher ─────────────────────────────────── */

static const char *ear_dock_icon_names[] = {
  "org.ooze.Ear",
  "audio-headphones",
  "multimedia-volume-control",
  "preferences-desktop-sound",
  NULL,
};

static void
my_dock_launch_ear (MetaContext *context)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError)              error    = NULL;
  g_autofree char *cmd = g_find_program_in_path ("ooze-ear");
  const char *argv[2]  = { NULL, NULL };

  if (!context)
    {
      g_warning ("MyDock: no compositor context for launching ooze-ear");
      return;
    }

  if (!cmd)
    {
      g_warning ("MyDock: ooze-ear not found in PATH");
      return;
    }

  argv[0] = cmd;
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP);
  my_icons_apply_to_launcher (launcher);

  if (!meta_wayland_client_new_subprocess (context, launcher, argv, &error))
    g_warning ("MyDock: failed to launch ooze-ear: %s", error->message);
  else
    g_print ("MyDock: launched ooze-ear\n");
}

static ClutterActor *
my_dock_create_ear_launcher (ClutterActor *stage,
                             MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = my_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = my_dock_themed_icon_content (stage,
                                         display,
                                         ear_dock_icon_names,
                                         logical);
  if (!content)
    content = my_aqua_dock_icon_content (stage, texture, 0.20f, 0.55f, 0.35f);

  if (content)
    my_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical, logical,
                                      texture, texture);

  my_dock_wire_app_launcher (button, display, "org.ooze.Ear",
                             my_dock_launch_ear);
  return button;
}

/* ── Ooze Pak (Flatpak manager) launcher ────────────────────────────────── */

static const char *pak_dock_icon_names[] = {
  "org.ooze.Pak",
  "system-software-install",
  "package-x-generic",
  NULL,
};

void
my_dock_launch_pak (MetaContext *context)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GError)              error    = NULL;
  g_autofree char *cmd = g_find_program_in_path ("ooze-pak");
  const char *argv[2]  = { NULL, NULL };

  if (!context)
    {
      g_warning ("MyDock: no compositor context for launching ooze-pak");
      return;
    }

  if (!cmd)
    {
      g_warning ("MyDock: ooze-pak not found in PATH");
      return;
    }

  argv[0] = cmd;
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP);
  my_icons_apply_to_launcher (launcher);

  if (!meta_wayland_client_new_subprocess (context, launcher, argv, &error))
    g_warning ("MyDock: failed to launch ooze-pak: %s", error->message);
  else
    g_print ("MyDock: launched ooze-pak\n");
}

static ClutterActor *
my_dock_create_pak_launcher (ClutterActor *stage,
                             MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = my_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = my_dock_themed_icon_content (stage,
                                         display,
                                         pak_dock_icon_names,
                                         logical);
  if (!content)
    content = my_aqua_dock_icon_content (stage, texture, 0.25f, 0.55f, 0.85f);

  if (content)
    my_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical, logical,
                                      texture, texture);

  my_dock_wire_app_launcher (button, display, "org.ooze.Pak",
                             my_dock_launch_pak);
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
my_dock_launch_trash (MetaContext *context)
{
  g_autofree char *trash_files = NULL;

  trash_files = g_build_filename (g_get_user_data_dir (),
                                  "Trash", "files", NULL);
  g_mkdir_with_parents (trash_files, 0700);
  my_dock_launch_spot_path (context, trash_files);
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
  my_dock_launch_trash (context);
  return CLUTTER_EVENT_STOP;
}

static ClutterActor *
my_dock_create_trash_launcher (ClutterActor *stage,
                               MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  MetaContext *context;
  int logical = 48;
  int texture = my_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  context = meta_display_get_context (display);
  g_object_set_data_full (G_OBJECT (button),
                          "launch-context",
                          g_object_ref (context),
                          g_object_unref);

  content = my_dock_themed_icon_content (stage,
                                         display,
                                         trash_dock_icon_names,
                                         logical);
  if (!content)
    content = my_aqua_dock_icon_content (stage, texture, 0.45f, 0.45f, 0.48f);

  if (content)
    my_aqua_actor_set_scaled_content (button,
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
my_dock_create_indicator (void)
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
my_dock_attach_indicator (ClutterActor *launcher, float launcher_w)
{
  ClutterActor *indicator = my_dock_create_indicator ();
  float         x         = (launcher_w - INDICATOR_W) / 2.0f;
  /* 1 px gap below the icon; slightly outside the launcher bounds but
   * Clutter does not clip children to the parent allocation by default. */
  float         y         = launcher_w + 1.0f;

  clutter_actor_set_position (indicator, x, y);
  clutter_actor_add_child (launcher, indicator);
  g_object_set_data (G_OBJECT (launcher), "indicator", indicator);
}

/* ── Indicator update timer ───────────────────────────────────────────────── */

typedef struct {
  MetaDisplay  *display;
  ClutterActor *container;   /* aqua_dock_icons */
} MyDockIndicatorCtx;

static gboolean
my_dock_check_by_id (MetaDisplay *display, const char *app_id)
{
  GList    *all;
  GList    *l;
  gboolean  found = FALSE;

  all = meta_display_list_all_windows (display);
  for (l = all; l && !found; l = l->next)
    {
      if (my_dock_window_matches_app_id (l->data, app_id))
        found = TRUE;
    }
  g_list_free (all);
  return found;
}

static gboolean
my_dock_check_by_info (MetaDisplay *display, GDesktopAppInfo *app_info)
{
  GList    *all;
  GList    *l;
  gboolean  found = FALSE;

  all = meta_display_list_all_windows (display);
  for (l = all; l && !found; l = l->next)
    {
      if (my_dock_window_matches_app (l->data, app_info))
        found = TRUE;
    }
  g_list_free (all);
  return found;
}

static void
my_dock_set_geometry_for_app_id (MetaDisplay  *display,
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

      if (my_dock_window_matches_app_id (win, app_id))
        meta_window_set_icon_geometry (win, rect);
    }
  g_list_free (all);
}

static void
my_dock_set_geometry_for_app_info (MetaDisplay     *display,
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

      if (my_dock_window_matches_app (win, app_info))
        meta_window_set_icon_geometry (win, rect);
    }
  g_list_free (all);
}

void
my_dock_update_icon_geometries (MetaDisplay  *display,
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
        my_dock_set_geometry_for_app_id (display, app_id, &rect);

      app_info = g_object_get_data (G_OBJECT (child), "app-info");
      if (app_info)
        my_dock_set_geometry_for_app_info (display, app_info, &rect);
    }
}

static gboolean
my_dock_update_indicators_cb (gpointer user_data)
{
  MyDockIndicatorCtx *ctx   = user_data;
  ClutterActor       *child;

  for (child = clutter_actor_get_first_child (ctx->container);
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
          running = my_dock_check_by_id (ctx->display, app_id);
      }

      /* Then try GDesktopAppInfo (generic launchers) */
      if (!running)
        {
          GDesktopAppInfo *info = g_object_get_data (G_OBJECT (child), "app-info");
          if (info)
            running = my_dock_check_by_info (ctx->display, info);
        }

      clutter_actor_set_opacity (indicator, running ? 255 : 0);
    }

  my_dock_update_icon_geometries (ctx->display, ctx->container);

  return G_SOURCE_CONTINUE;
}

static gboolean
my_dock_window_matches_app_id (MetaWindow *window,
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
my_dock_get_app_windows (MetaDisplay *display,
                         const char  *app_id)
{
  GList *all_windows;
  GList *l;
  GList *matches = NULL;

  all_windows = meta_display_list_all_windows (display);
  for (l = all_windows; l != NULL; l = l->next)
    {
      MetaWindow *window = l->data;

      if (my_dock_window_matches_app_id (window, app_id))
        matches = g_list_prepend (matches, window);
    }

  g_list_free (all_windows);
  return g_list_reverse (matches);
}

static void
my_dock_handle_app_click (MetaDisplay   *display,
                          MetaContext   *context,
                          const char    *app_id,
                          MyDockLaunchFn launch)
{
  g_autoptr (GList) windows = NULL;
  GList *l;
  gboolean any_visible = FALSE;
  MetaWindow *focus_window;
  MetaWindow *target;
  gboolean app_is_focused;

  if (!display || !app_id || !launch)
    return;

  windows = my_dock_get_app_windows (display, app_id);
  if (!windows)
    {
      launch (context);
      return;
    }

  for (l = windows; l != NULL; l = l->next)
    {
      if (my_dock_window_is_visible (l->data))
        {
          any_visible = TRUE;
          break;
        }
    }

  focus_window = meta_display_get_focus_window (display);
  app_is_focused = my_dock_window_matches_app_id (focus_window, app_id);

  /* Frontmost → minimize. Otherwise raise existing. Never spawn a duplicate. */
  if (any_visible && app_is_focused)
    {
      for (l = windows; l != NULL; l = l->next)
        {
          MetaWindow *window = l->data;

          if (my_dock_window_is_visible (window) &&
              meta_window_can_minimize (window))
            meta_window_minimize (window);
        }
      return;
    }

  if (any_visible)
    {
      GList *visible = NULL;

      for (l = windows; l != NULL; l = l->next)
        {
          if (my_dock_window_is_visible (l->data))
            visible = g_list_prepend (visible, l->data);
        }

      target = my_dock_get_most_recent_window (visible);
      g_list_free (visible);
      if (target)
        meta_window_activate (target, clutter_get_current_event_time ());
      return;
    }

  target = my_dock_get_most_recent_window (windows);
  if (target)
    {
      meta_window_unminimize (target);
      meta_window_activate (target, clutter_get_current_event_time ());
    }
}

static gboolean
on_app_launcher_pressed (ClutterActor *actor,
                         ClutterEvent *event,
                         gpointer      user_data G_GNUC_UNUSED)
{
  MetaDisplay *display;
  MetaContext *context;
  const char *app_id;
  MyDockLaunchFn launch;
  guint button;

  button = clutter_event_get_button (event);
  display = g_object_get_data (G_OBJECT (actor), "launch-display");
  context = g_object_get_data (G_OBJECT (actor), "launch-context");
  app_id = g_object_get_data (G_OBJECT (actor), "app-id");
  launch = g_object_get_data (G_OBJECT (actor), "launch-fn");

  if (!display || !context || !app_id || !launch)
    return CLUTTER_EVENT_STOP;

  /* Middle-click always opens a new window. */
  if (button == CLUTTER_BUTTON_MIDDLE)
    {
      launch (context);
      return CLUTTER_EVENT_STOP;
    }

  if (button != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  my_dock_handle_app_click (display, context, app_id, launch);
  return CLUTTER_EVENT_STOP;
}

static void
my_dock_wire_app_launcher (ClutterActor  *button,
                           MetaDisplay   *display,
                           const char    *app_id,
                           MyDockLaunchFn launch)
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
  my_dock_attach_indicator (button, clutter_actor_get_width (button));
}

ClutterActor *
my_dock_create_spot_launcher (ClutterActor *stage,
                              MetaDisplay  *display)
{
  ClutterActor *button;
  g_autoptr (ClutterContent) content = NULL;
  int logical = 48;
  int texture = my_aqua_icon_texture_size (display, logical);

  button = clutter_actor_new ();
  clutter_actor_set_size (button, (gfloat) logical, (gfloat) logical);
  clutter_actor_set_reactive (button, TRUE);

  content = my_dock_themed_icon_content (stage,
                                         display,
                                         spot_dock_icon_names,
                                         logical);
  if (!content)
    content = my_aqua_spot_icon_content (stage, display, logical);
  if (!content)
    content = my_aqua_dock_icon_content (stage, texture, 0.22f, 0.48f, 0.92f);
  if (content)
    my_aqua_actor_set_scaled_content (button,
                                      g_steal_pointer (&content),
                                      logical,
                                      logical,
                                      texture,
                                      texture);

  my_dock_wire_app_launcher (button, display, "org.ooze.Spot",
                             my_dock_launch_spot);
  return button;
}

static gboolean
my_dock_window_matches_app (MetaWindow       *window,
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
my_dock_window_is_visible (MetaWindow *window)
{
  return meta_window_showing_on_its_workspace (window) &&
         !meta_window_is_hidden (window);
}

static MetaWindow *
my_dock_get_most_recent_window (GList *windows)
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
my_dock_texture_from_pixbuf (ClutterActor *actor,
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
    g_warning ("MyDock: failed to upload icon texture: %s",
               error ? error->message : "unknown error");

  return texture;
}

static GdkPixbuf *
my_dock_load_png_pixbuf (const char  *path,
                         int          size,
                         GError     **error)
{
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  GdkPixbuf *scaled;
  FILE *fp;
  png_structp png;
  png_infop info;
  png_byte header[8];
  png_uint_32 width;
  png_uint_32 height;
  png_bytep *rows = NULL;
  guchar *pixels;
  int rowstride;
  png_uint_32 png_rowbytes;
  int y;

  fp = fopen (path, "rb");
  if (!fp)
    {
      g_set_error (error,
                   G_FILE_ERROR,
                   g_file_error_from_errno (errno),
                   "Failed to open %s: %s",
                   path,
                   g_strerror (errno));
      return NULL;
    }

  if (fread (header, 1, 8, fp) != 8 || png_sig_cmp (header, 0, 8))
    {
      fclose (fp);
      g_set_error_literal (error,
                           GDK_PIXBUF_ERROR,
                           GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
                           "Not a PNG file");
      return NULL;
    }

  png = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  info = png_create_info_struct (png);
  if (!png || !info)
    {
      fclose (fp);
      png_destroy_read_struct (&png, &info, NULL);
      g_set_error_literal (error,
                           GDK_PIXBUF_ERROR,
                           GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY,
                           "Failed to allocate PNG decoder");
      return NULL;
    }

  if (setjmp (png_jmpbuf (png)))
    {
      g_free (rows);
      png_destroy_read_struct (&png, &info, NULL);
      fclose (fp);
      g_set_error_literal (error,
                           GDK_PIXBUF_ERROR,
                           GDK_PIXBUF_ERROR_CORRUPT_IMAGE,
                           "Failed to decode PNG");
      return NULL;
    }

  png_init_io (png, fp);
  png_set_sig_bytes (png, 8);
  png_read_info (png, info);

  width = png_get_image_width (png, info);
  height = png_get_image_height (png, info);

  if (png_get_bit_depth (png, info) == 16)
    png_set_strip_16 (png);
  if (png_get_color_type (png, info) == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb (png);
  if (png_get_color_type (png, info) == PNG_COLOR_TYPE_GRAY &&
      png_get_bit_depth (png, info) < 8)
    png_set_expand_gray_1_2_4_to_8 (png);
  if (png_get_valid (png, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha (png);
  if (png_get_color_type (png, info) == PNG_COLOR_TYPE_RGB ||
      png_get_color_type (png, info) == PNG_COLOR_TYPE_GRAY ||
      png_get_color_type (png, info) == PNG_COLOR_TYPE_PALETTE)
    png_set_filler (png, 0xff, PNG_FILLER_AFTER);
  if (png_get_color_type (png, info) == PNG_COLOR_TYPE_GRAY ||
      png_get_color_type (png, info) == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb (png);

  png_read_update_info (png, info);
  png_rowbytes = png_get_rowbytes (png, info);

  rows = g_new0 (png_bytep, height);
  for (y = 0; y < (int) height; y++)
    rows[y] = g_new (png_byte, png_rowbytes);

  png_read_image (png, rows);
  png_read_end (png, NULL);
  fclose (fp);
  png_destroy_read_struct (&png, &info, NULL);

  pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                           TRUE,
                           8,
                           (int) width,
                           (int) height);
  pixels = gdk_pixbuf_get_pixels (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);

  for (y = 0; y < (int) height; y++)
    {
      memcpy (pixels + y * rowstride, rows[y], png_rowbytes);
      g_free (rows[y]);
    }
  g_free (rows);

  if ((int) width != size || (int) height != size)
    {
      scaled = gdk_pixbuf_scale_simple (pixbuf, size, size, GDK_INTERP_BILINEAR);
      if (!scaled)
        {
          g_set_error_literal (error,
                               GDK_PIXBUF_ERROR,
                               GDK_PIXBUF_ERROR_FAILED,
                               "Failed to scale PNG");
          return NULL;
        }
      return scaled;
    }

  return g_steal_pointer (&pixbuf);
}

static GdkPixbuf *
my_dock_try_load_icon_file (const char *path,
                            int         size)
{
  g_autoptr (GError) error = NULL;

  if (g_str_has_suffix (path, ".png"))
    return my_dock_load_png_pixbuf (path, size, &error);

  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    return NULL;

  return gdk_pixbuf_new_from_file_at_scale (path,
                                            size,
                                            size,
                                            TRUE,
                                            &error);
}

static GdkPixbuf *
my_dock_try_icon_in_dir (const char *dir,
                         const char *icon_name,
                         int         size)
{
  static const char *extensions[] = { ".png", ".svg", ".xpm", NULL };
  gsize e;

  for (e = 0; extensions[e] != NULL; e++)
    {
      g_autofree char *path =
        g_strdup_printf ("%s/%s%s", dir, icon_name, extensions[e]);
      GdkPixbuf *pixbuf = my_dock_try_load_icon_file (path, size);

      if (pixbuf)
        return pixbuf;
    }

  return NULL;
}

static char *
my_dock_get_icon_theme (void)
{
  g_autoptr (GSettings) settings = NULL;

  settings = g_settings_new ("org.gnome.desktop.interface");
  return g_settings_get_string (settings, "icon-theme");
}

static GdkPixbuf *
my_dock_try_theme_paths (const char *icon_base,
                         const char *theme,
                         const char *icon_name,
                         int         size)
{
  static const char *contexts[] = {
    "actions",
    "apps",
    "devices",
    "places",
    "mimes",
    "mimetypes",
    "categories",
    "status",
    "emblems",
    NULL,
  };
  static const int sizes[] = { 256, 128, 96, 64, 48, 32, 24, 16, 0 };
  gsize c;
  gsize s;

  if (!theme || theme[0] == '\0')
    return NULL;

  /* Icon theme layout: theme/context/size/icon (elementary, Adwaita). */
  for (c = 0; contexts[c] != NULL; c++)
    {
      g_autofree char *dir = NULL;
      GdkPixbuf *pixbuf;

      dir = g_strdup_printf ("%s/%s/%s/%d",
                             icon_base,
                             theme,
                             contexts[c],
                             size);
      pixbuf = my_dock_try_icon_in_dir (dir, icon_name, size);
      if (pixbuf)
        return pixbuf;
    }

  for (s = 0; sizes[s] != 0; s++)
    {
      for (c = 0; contexts[c] != NULL; c++)
        {
          g_autofree char *dir = NULL;
          GdkPixbuf *pixbuf;
          int found_w;

          if (sizes[s] == size)
            continue;

          dir = g_strdup_printf ("%s/%s/%s/%d",
                                 icon_base,
                                 theme,
                                 contexts[c],
                                 sizes[s]);
          pixbuf = my_dock_try_icon_in_dir (dir, icon_name, sizes[s]);
          if (!pixbuf)
            continue;

          found_w = gdk_pixbuf_get_width (pixbuf);
          if (found_w > size)
            {
              GdkPixbuf *scaled;

              scaled = gdk_pixbuf_scale_simple (pixbuf,
                                                size,
                                                size,
                                                GDK_INTERP_HYPER);
              g_object_unref (pixbuf);
              return scaled;
            }

          return pixbuf;
        }
    }

  /* Legacy hicolor layout: theme/sizexsize/context/icon. */
  for (c = 0; contexts[c] != NULL; c++)
    {
      g_autofree char *dir = NULL;
      GdkPixbuf *pixbuf;

      dir = g_strdup_printf ("%s/%s/%dx%d/%s",
                             icon_base,
                             theme,
                             size,
                             size,
                             contexts[c]);
      pixbuf = my_dock_try_icon_in_dir (dir, icon_name, size);
      if (pixbuf)
        return pixbuf;
    }

  for (s = 0; sizes[s] != 0; s++)
    {
      for (c = 0; contexts[c] != NULL; c++)
        {
          g_autofree char *dir = NULL;
          GdkPixbuf *pixbuf;
          int found_w;

          if (sizes[s] == size)
            continue;

          dir = g_strdup_printf ("%s/%s/%dx%d/%s",
                                 icon_base,
                                 theme,
                                 sizes[s],
                                 sizes[s],
                                 contexts[c]);
          pixbuf = my_dock_try_icon_in_dir (dir, icon_name, sizes[s]);
          if (!pixbuf)
            continue;

          found_w = gdk_pixbuf_get_width (pixbuf);
          if (found_w > size)
            {
              GdkPixbuf *scaled;

              scaled = gdk_pixbuf_scale_simple (pixbuf,
                                                size,
                                                size,
                                                GDK_INTERP_HYPER);
              g_object_unref (pixbuf);
              return scaled;
            }

          return pixbuf;
        }
    }

  for (c = 0; contexts[c] != NULL; c++)
    {
      g_autoptr (GdkPixbuf) pixbuf = NULL;
      g_autofree char *dir = NULL;

      dir = g_strdup_printf ("%s/%s/scalable/%s", icon_base, theme, contexts[c]);
      pixbuf = my_dock_try_icon_in_dir (dir, icon_name, size);
      if (pixbuf)
        return g_steal_pointer (&pixbuf);

      dir = g_strdup_printf ("%s/%s/symbolic/%s", icon_base, theme, contexts[c]);
      pixbuf = my_dock_try_icon_in_dir (dir, icon_name, size);
      if (pixbuf)
        return g_steal_pointer (&pixbuf);
    }

  return NULL;
}

static GdkPixbuf *
my_dock_load_themed_icon (const char *icon_name,
                          int         size)
{
  g_autofree char *user_theme = my_dock_get_icon_theme ();
  g_autofree char *local_icons = my_icons_get_icons_dir ();
  const char *alias;
  const char *icon_bases[] = {
    local_icons,
    "/usr/share/icons",
    NULL,
  };
  const char *themes[] = {
    user_theme,
    OOZE_ICON_THEME,
    "elementary",
    "Adwaita",
    "Yaru",
    "hicolor",
    NULL,
  };
  gsize b;
  gsize t;
  GdkPixbuf *pixbuf;

  if (!icon_name || icon_name[0] == '\0')
    return NULL;

  for (b = 0; icon_bases[b] != NULL; b++)
    {
      if (!icon_bases[b] || icon_bases[b][0] == '\0')
        continue;
      if (!g_file_test (icon_bases[b], G_FILE_TEST_IS_DIR))
        continue;

      for (t = 0; themes[t] != NULL; t++)
        {
          pixbuf = my_dock_try_theme_paths (icon_bases[b],
                                            themes[t],
                                            icon_name,
                                            size);
          if (pixbuf)
            return pixbuf;
        }
    }

  alias = my_dock_icon_alias (icon_name);
  if (alias && g_strcmp0 (alias, icon_name) != 0)
    return my_dock_load_themed_icon (alias, size);

  return NULL;
}

ClutterContent *
my_dock_themed_icon_content (ClutterActor       *ref_actor,
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

  load_size = my_aqua_icon_texture_size (display, logical_size);

  for (i = 0; icon_names[i] != NULL; i++)
    {
      pixbuf = my_dock_load_themed_icon (icon_names[i], load_size);
      if (pixbuf)
        break;
    }

  if (!pixbuf)
    return NULL;

  texture = my_dock_texture_from_pixbuf (ref_actor, pixbuf);
  if (!texture)
    return NULL;

  return clutter_texture_content_new_from_texture (texture, NULL);
}

static void
my_dock_layout_launchers (MyDock *dock,
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

MyDock *
my_dock_new (MetaContext     *context,
             MetaDisplay     *display,
             MetaCompositor  *compositor)
{
  MyDock *dock;
  ClutterActor *stage;
  ClutterActor *window_group;
  ClutterActor *launchers;
  CoglColor dock_color;
  g_autoptr (MetaBackground) background = NULL;
  ClutterContent *content;
  MetaBackgroundContent *background_content;

  dock = g_new0 (MyDock, 1);
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

  my_dock_populate_container (dock->context, dock->display, stage, launchers);

  clutter_actor_show (dock->actor);

  return dock;
}

void
my_dock_resize (MyDock      *dock,
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

  my_dock_layout_launchers (dock, (gfloat) width);
}

ClutterActor *
my_dock_get_actor (MyDock *dock)
{
  return dock ? dock->actor : NULL;
}

void
my_dock_populate_container (MetaContext   *context G_GNUC_UNUSED,
                            MetaDisplay   *display,
                            ClutterActor  *stage,
                            ClutterActor  *container)
{
  MyDockIndicatorCtx *indicator_ctx;

  {
    ClutterActor *spot_launcher;
    ClutterActor *command_launcher;
    ClutterActor *ear_launcher;
    ClutterActor *king_launcher;
    ClutterActor *pak_launcher;
    ClutterActor *trash_launcher;

    spot_launcher = my_dock_create_spot_launcher (stage, display);
    clutter_actor_set_y_align (spot_launcher, CLUTTER_ACTOR_ALIGN_CENTER);
    clutter_actor_add_child (container, spot_launcher);
    clutter_actor_show (spot_launcher);

    command_launcher = my_dock_create_command_launcher (stage, display);
    clutter_actor_set_y_align (command_launcher, CLUTTER_ACTOR_ALIGN_CENTER);
    clutter_actor_add_child (container, command_launcher);
    clutter_actor_show (command_launcher);

    ear_launcher = my_dock_create_ear_launcher (stage, display);
    clutter_actor_set_y_align (ear_launcher, CLUTTER_ACTOR_ALIGN_CENTER);
    clutter_actor_add_child (container, ear_launcher);
    clutter_actor_show (ear_launcher);

    king_launcher = my_dock_create_king_launcher (stage, display);
    clutter_actor_set_y_align (king_launcher, CLUTTER_ACTOR_ALIGN_CENTER);
    clutter_actor_add_child (container, king_launcher);
    clutter_actor_show (king_launcher);

    pak_launcher = my_dock_create_pak_launcher (stage, display);
    clutter_actor_set_y_align (pak_launcher, CLUTTER_ACTOR_ALIGN_CENTER);
    clutter_actor_add_child (container, pak_launcher);
    clutter_actor_show (pak_launcher);

    trash_launcher = my_dock_create_trash_launcher (stage, display);
    clutter_actor_set_y_align (trash_launcher, CLUTTER_ACTOR_ALIGN_CENTER);
    clutter_actor_add_child (container, trash_launcher);
    clutter_actor_show (trash_launcher);
  }

  /* Kick off the running-app indicator timer (600 ms). */
  indicator_ctx            = g_new0 (MyDockIndicatorCtx, 1);
  indicator_ctx->display   = display;
  indicator_ctx->container = container;
  g_timeout_add (600, my_dock_update_indicators_cb, indicator_ctx);
}

void
my_dock_free (MyDock *dock)
{
  if (!dock)
    return;

  g_clear_pointer (&dock->actor, clutter_actor_destroy);
  g_clear_object (&dock->context);
  g_free (dock);
}
