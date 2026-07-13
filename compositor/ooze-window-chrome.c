#include "ooze-window-chrome.h"
#include "ooze-aqua-draw.h"
#include "ooze-theme.h"
#include "ooze-window.h"

#include "../common/aqua-chrome.h"
#include "../common/ooze-font.h"

#include <meta/compositor.h>
#include <meta/display.h>
#include <meta/meta-window-actor.h>
#include <meta/window.h>
#include <mtk/mtk.h>

#include <glib.h>

/* ── Private chrome state ─────────────────────────────────────────────────── */

typedef struct
{
  ClutterActor *titlebar;
  ClutterActor *title_label;
  int           last_titlebar_width;
  char         *last_title;
  gulong        position_changed_id;
} OozeWindowChrome;

static void
ooze_window_chrome_free (gpointer data)
{
  OozeWindowChrome *chrome = data;

  /* title_label is a child of titlebar; destroying titlebar destroys it too. */
  chrome->title_label = NULL;
  g_clear_pointer (&chrome->titlebar, clutter_actor_destroy);
  g_clear_pointer (&chrome->last_title, g_free);
  g_free (chrome);
}

static gfloat
chrome_vertical_center (gfloat bar_height, gfloat item_height)
{
  return (bar_height - item_height) / 2.0f;
}

static void
chrome_set_text_label (ClutterActor *actor,
                       const char   *font_desc,
                       const char   *text,
                       gfloat        r,
                       gfloat        g,
                       gfloat        b)
{
  g_autoptr (ClutterContent) content = NULL;
  int width = 1;
  int height = 1;

  content = ooze_aqua_text_content (actor, font_desc, text, r, g, b,
                                    &width, &height);
  if (!content)
    return;

  ooze_aqua_actor_set_content (actor, g_steal_pointer (&content), width, height);
}

static gboolean
chrome_window_is_minimized (MetaWindow *window)
{
  gboolean minimized = FALSE;

  if (!window)
    return FALSE;
  g_object_get (window, "minimized", &minimized, NULL);
  return minimized;
}

static gboolean
chrome_sync_idle (gpointer user_data)
{
  MetaWindowActor *actor = META_WINDOW_ACTOR (user_data);

  g_object_set_data (G_OBJECT (actor), "ooze-chrome-sync-id", NULL);
  ooze_window_chrome_sync (actor);
  return G_SOURCE_REMOVE;
}

void
ooze_window_chrome_schedule_sync (MetaWindowActor *actor)
{
  guint id;

  if (g_object_get_data (G_OBJECT (actor), "ooze-chrome-sync-id"))
    return;

  g_object_ref (actor);
  id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                        chrome_sync_idle,
                        actor,
                        g_object_unref);
  g_object_set_data (G_OBJECT (actor),
                     "ooze-chrome-sync-id",
                     GUINT_TO_POINTER (id));
}

void
ooze_window_chrome_cancel_sync (MetaWindowActor *actor)
{
  gpointer sync_id;

  sync_id = g_object_get_data (G_OBJECT (actor), "ooze-chrome-sync-id");
  if (sync_id)
    {
      g_source_remove (GPOINTER_TO_UINT (sync_id));
      g_object_set_data (G_OBJECT (actor), "ooze-chrome-sync-id", NULL);
    }
}

void
ooze_window_chrome_sync (MetaWindowActor *actor)
{
  OozeWindowChrome *chrome;
  MetaWindow *window;
  MtkRectangle frame;
  g_autoptr (ClutterContent) titlebar_content = NULL;
  const OozeAquaPalette *palette;

  chrome = g_object_get_data (G_OBJECT (actor), "ooze-window-chrome");
  palette = ooze_theme_get_palette (NULL);

  if (meta_window_actor_is_destroyed (actor))
    return;

  window = meta_window_actor_get_meta_window (actor);
  if (!window)
    return;

  meta_window_get_frame_rect (window, &frame);

  if (frame.width <= 0 || frame.height <= 0)
    return;

  if (chrome && CLUTTER_IS_ACTOR (chrome->titlebar))
    {
      gboolean minimized;

      minimized = chrome_window_is_minimized (window);
      clutter_actor_set_position (chrome->titlebar,
                                  (gfloat) frame.x,
                                  (gfloat) frame.y);
      clutter_actor_set_size (chrome->titlebar,
                              (gfloat) frame.width,
                              AQUA_TITLEBAR_HEIGHT);

      if (frame.width != chrome->last_titlebar_width)
        {
          titlebar_content = ooze_aqua_pinstripe_content (chrome->titlebar,
                                                          frame.width,
                                                          (int) AQUA_TITLEBAR_HEIGHT);
          if (titlebar_content)
            ooze_aqua_actor_set_content (chrome->titlebar,
                                         g_steal_pointer (&titlebar_content),
                                         frame.width,
                                         (int) AQUA_TITLEBAR_HEIGHT);
          chrome->last_titlebar_width = frame.width;
        }

      if (minimized)
        clutter_actor_hide (chrome->titlebar);
      else
        clutter_actor_show (chrome->titlebar);
    }

  if (chrome && CLUTTER_IS_ACTOR (chrome->title_label))
    {
      gfloat label_width;
      const char *title = meta_window_get_title (window);

      if (!chrome->last_title || g_strcmp0 (chrome->last_title, title) != 0)
        {
          g_free (chrome->last_title);
          chrome->last_title = g_strdup (title);
          chrome_set_text_label (chrome->title_label,
                                 OOZE_UI_FONT,
                                 title,
                                 (gfloat) palette->title_text_r,
                                 (gfloat) palette->title_text_g,
                                 (gfloat) palette->title_text_b);
        }

      label_width = clutter_actor_get_width (chrome->title_label);
      clutter_actor_set_position (chrome->title_label,
                                  ((gfloat) frame.width - label_width) / 2.0f,
                                  chrome_vertical_center (AQUA_TITLEBAR_HEIGHT,
                                                          clutter_actor_get_height (chrome->title_label)));
    }

  ooze_window_sync (actor);
}

void
ooze_window_chrome_raise_ssd (MetaWindowActor *actor)
{
  OozeWindowChrome *chrome;
  ClutterActor *window_actor;
  ClutterActor *window_group;

  chrome = g_object_get_data (G_OBJECT (actor), "ooze-window-chrome");
  if (!chrome || !CLUTTER_IS_ACTOR (chrome->titlebar))
    return;

  window_actor = CLUTTER_ACTOR (actor);
  window_group = clutter_actor_get_parent (window_actor);
  if (!window_group)
    return;

  clutter_actor_set_child_above_sibling (window_group, chrome->titlebar, NULL);
}

void
ooze_window_chrome_remove (MetaWindowActor *actor)
{
  OozeWindowChrome *chrome;
  MetaWindow *window;

  chrome = g_object_get_data (G_OBJECT (actor), "ooze-window-chrome");
  if (!chrome)
    return;

  ooze_window_chrome_cancel_sync (actor);

  if (chrome->position_changed_id)
    {
      window = meta_window_actor_get_meta_window (actor);
      if (window)
        g_signal_handler_disconnect (window, chrome->position_changed_id);
      chrome->position_changed_id = 0;
    }

  g_object_set_data (G_OBJECT (actor), "ooze-window-chrome", NULL);
}

void
ooze_window_chrome_apply (MetaWindowActor *actor,
                          MetaPlugin      *plugin G_GNUC_UNUSED)
{
  MetaWindow *window;

  window = meta_window_actor_get_meta_window (actor);
  if (!window)
    return;

  if (g_object_get_data (G_OBJECT (actor), "ooze-window-chrome"))
    return;

  if (meta_window_get_window_type (window) != META_WINDOW_NORMAL)
    return;

  /*
   * Do not attach a Clutter SSD overlay to foreign windows.
   *
   * X11 / XWayland SSD clients get real Mutter MetaFrames (Cinnamon-style).
   * Wayland-native and CSD clients keep their own decorations; controls are
   * forced to the left via org.gnome.desktop.wm.preferences button-layout
   * and Gtk/DecorationLayout (see ooze_plugin_apply_button_layout /
   * ooze-xsettings).
   *
   * Ooze apps (org.ooze.*) already draw Ooze Gel themselves.
   */
}

void
ooze_window_chrome_set_visible (MetaWindowActor *actor, gboolean visible)
{
  OozeWindowChrome *chrome;

  chrome = g_object_get_data (G_OBJECT (actor), "ooze-window-chrome");
  if (!chrome || !CLUTTER_IS_ACTOR (chrome->titlebar))
    return;

  if (visible)
    clutter_actor_show (chrome->titlebar);
  else
    clutter_actor_hide (chrome->titlebar);
}

void
ooze_window_chrome_invalidate (MetaWindowActor *actor)
{
  OozeWindowChrome *chrome;

  chrome = g_object_get_data (G_OBJECT (actor), "ooze-window-chrome");
  if (!chrome)
    return;

  chrome->last_titlebar_width = 0;
  g_clear_pointer (&chrome->last_title, g_free);
  ooze_window_chrome_schedule_sync (actor);
}
