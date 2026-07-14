#include "ooze-panel.h"
#include "ooze-plugin-priv.h"
#include "ooze-shell-menu.h"
#include "ooze-aqua-draw.h"
#include "ooze-aqua-menu.h"
#include "ooze-global-menu.h"
#include "ooze-theme.h"
#include "ooze-dock-shell.h"
#include "ooze-stall.h"

#include "../common/aqua-chrome.h"
#include "../common/ooze-font.h"

#include <meta/compositor.h>
#include <meta/display.h>
#include <meta/meta-context.h>
#include <clutter/clutter.h>
#include <string.h>

#include <time.h>

#define PANEL_HEIGHT        26.0f
#define OOZE_BUTTON_MARGIN   4.0f
#define OOZE_BUTTON_PAD_Y    2.0f
#define MENU_ITEM_GAP       18.0f
#define MENU_BAR_HIT_HEIGHT PANEL_HEIGHT
#define PANEL_REBUILD_DEBOUNCE_MS 40

/* ── Text rendering helpers ──────────────────────────────────────────────── */

static gfloat
panel_vertical_center (gfloat bar_height, gfloat item_height)
{
  return (bar_height - item_height) / 2.0f;
}

static void
panel_make_click_target (ClutterActor *actor, gfloat width, gfloat height)
{
  clutter_actor_set_reactive (actor, TRUE);
  clutter_actor_set_size (actor, width, height);
}

static ClutterActor *
panel_create_text_label (ClutterActor *ref_actor,
                         const char   *font_desc,
                         const char   *text,
                         gfloat        r,
                         gfloat        g,
                         gfloat        b)
{
  ClutterActor *actor;
  g_autoptr (ClutterContent) content = NULL;
  int width = 1;
  int height = 1;

  content = ooze_aqua_text_content (ref_actor, font_desc, text, r, g, b,
                                  &width, &height);
  actor = clutter_actor_new ();
  if (!content)
    {
      clutter_actor_set_size (actor, 1.0f, 1.0f);
      return actor;
    }

  ooze_aqua_actor_set_content (actor, g_steal_pointer (&content), width, height);
  return actor;
}

static void
panel_set_text_label (ClutterActor *actor,
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

/* ── Forward declarations ────────────────────────────────────────────────── */

static void panel_ensure_menu_bar   (OozePlugin *plugin);
static void panel_cancel_pending    (OozePlugin *plugin);
static void panel_show_bar_menu_ex  (OozePlugin     *plugin,
                                     gsize         menu_index,
                                     ClutterActor *anchor,
                                     gboolean      user_toggle);
static void panel_show_bar_menu     (OozePlugin     *plugin,
                                     gsize         menu_index,
                                     ClutterActor *anchor);
static gboolean panel_retry_pending_menu (gpointer user_data);
static gboolean panel_rebuild_idle  (gpointer user_data);

/* ── Panel child helpers ─────────────────────────────────────────────────── */

static gboolean
panel_child_is_fixed_chrome (OozePlugin *plugin, ClutterActor *child)
{
  return child == plugin->menu_icon ||
         child == plugin->clock_label ||
         child == plugin->tray_box;
}

/* ── Menu label utilities ────────────────────────────────────────────────── */

static ClutterActor *
panel_menu_label_for_top (OozePlugin *plugin, gsize top_index)
{
  guint i;

  for (i = 0; i < plugin->n_menu_bar_labels; i++)
    {
      ClutterActor *label = plugin->menu_bar_labels[i];
      gsize top;

      if (!label)
        continue;
      top = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (label),
                                                 "ooze-menu-top"));
      if (top == top_index)
        return label;
    }

  return NULL;
}

static void
panel_clear_menu_bar_labels (OozePlugin *plugin)
{
  guint i;

  for (i = 0; i < plugin->n_menu_bar_labels; i++)
    {
      if (plugin->menu_bar_labels[i])
        {
          clutter_actor_destroy (plugin->menu_bar_labels[i]);
          plugin->menu_bar_labels[i] = NULL;
        }
    }
  plugin->n_menu_bar_labels = 0;
}

static ClutterActor *
panel_create_menu_bar_item (ClutterActor *ref_actor,
                            const char   *font_desc,
                            const char   *text,
                            gfloat        r,
                            gfloat        g,
                            gfloat        b)
{
  ClutterActor *bin;
  ClutterActor *label;
  gfloat label_w;
  gfloat label_h;

  label = panel_create_text_label (ref_actor, font_desc, text, r, g, b);
  label_w = clutter_actor_get_width (label);
  label_h = clutter_actor_get_height (label);

  bin = clutter_actor_new ();
  panel_make_click_target (bin, label_w, MENU_BAR_HIT_HEIGHT);
  clutter_actor_add_child (bin, label);
  clutter_actor_set_position (label,
                              0.0f,
                              panel_vertical_center (MENU_BAR_HIT_HEIGHT, label_h));
  clutter_actor_show (label);

  return bin;
}

/* ── Menu bar input handlers ─────────────────────────────────────────────── */

static gboolean
on_menu_bar_pressed (ClutterActor *actor,
                     ClutterEvent *event,
                     OozePlugin    *plugin)
{
  gsize menu_index;

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  for (menu_index = 0; menu_index < plugin->n_menu_bar_labels; menu_index++)
    {
      if (plugin->menu_bar_labels[menu_index] == actor)
        {
          gsize top = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (actor),
                                                           "ooze-menu-top"));
          panel_show_bar_menu (plugin, top, actor);
          return CLUTTER_EVENT_STOP;
        }
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
on_ooze_button_pressed (ClutterActor *actor,
                        ClutterEvent *event,
                        OozePlugin    *plugin)
{
  g_autofree char *logout_label = NULL;
  gboolean         dark;
  OozeAquaMenuEntry entries[] = {
    { NULL,            OOZE_MENU_OOZE_APPEARANCE, TRUE  },
    { NULL,            0,                       FALSE },
    { "About Ooze...", OOZE_MENU_HELP_ABOUT,      TRUE  },
    { NULL,            0,                       FALSE },
    { "Lock Screen",   OOZE_MENU_OOZE_LOCK,       TRUE  },
    { "Restart...",    OOZE_MENU_OOZE_RESTART,    TRUE  },
    { "Shut Down...",  OOZE_MENU_OOZE_SHUTDOWN,   TRUE  },
    { NULL,            0,                       FALSE },
    { NULL,            OOZE_MENU_OOZE_LOGOUT,     TRUE  },
  };

  if (clutter_event_get_button (event) != CLUTTER_BUTTON_PRIMARY)
    return CLUTTER_EVENT_PROPAGATE;

  panel_ensure_menu_bar (plugin);

  dark = ooze_theme_is_dark (ooze_theme_get_default ());
  entries[0].label = dark ? "Switch to Light Mode" : "Switch to Dark Mode";

  logout_label = g_strdup_printf ("Log Out %s...", g_get_user_name ());
  entries[8].label = logout_label;

  ooze_aqua_menu_toggle_for_anchor (plugin->menu_popup,
                                  actor,
                                  entries,
                                  G_N_ELEMENTS (entries));
  return CLUTTER_EVENT_STOP;
}

/* ── Menu bar state ──────────────────────────────────────────────────────── */

static void
panel_ensure_menu_bar (OozePlugin *plugin)
{
  if (!plugin->menu_bar_needs_rebuild)
    return;

  if (plugin->menu_popup && ooze_aqua_menu_is_open (plugin->menu_popup))
    return;

  plugin->menu_bar_needs_rebuild = FALSE;
  if (plugin->menubar_rebuild_idle)
    {
      g_source_remove (plugin->menubar_rebuild_idle);
      plugin->menubar_rebuild_idle = 0;
    }

  ooze_panel_rebuild_menu_bar (plugin);
}

static void
panel_cancel_pending (OozePlugin *plugin)
{
  plugin->pending_menu_open = FALSE;
  plugin->pending_menu_retries = 0;
  if (plugin->pending_menu_idle)
    {
      g_source_remove (plugin->pending_menu_idle);
      plugin->pending_menu_idle = 0;
    }
  if (plugin->global_menu)
    ooze_global_menu_discard_pending (plugin->global_menu);
}

static gboolean
panel_rebuild_idle (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  plugin->menubar_rebuild_idle = 0;

  if (plugin->menu_popup && ooze_aqua_menu_is_open (plugin->menu_popup))
    {
      plugin->menu_bar_needs_rebuild = TRUE;
      return G_SOURCE_REMOVE;
    }

  plugin->menu_bar_needs_rebuild = FALSE;
  ooze_panel_rebuild_menu_bar (plugin);
  return G_SOURCE_REMOVE;
}

/* ── Show bar menu (app vs shell stubs) ──────────────────────────────────── */

static void
panel_show_bar_menu (OozePlugin *plugin, gsize menu_index, ClutterActor *anchor)
{
  panel_show_bar_menu_ex (plugin, menu_index, anchor, TRUE);
}

static void
panel_show_bar_menu_ex (OozePlugin     *plugin,
                        gsize         menu_index,
                        ClutterActor *anchor,
                        gboolean      user_toggle)
{
  MetaWindow *focus;
  gboolean has_focus;
  OozeAquaMenuEntry *app_entries = NULL;
  gsize n_app_entries = 0;
  gboolean try_app;
  ClutterActor *by_top;

  if (user_toggle)
    plugin->force_shell_menu = FALSE;

  panel_ensure_menu_bar (plugin);

  by_top = panel_menu_label_for_top (plugin, menu_index);
  if (by_top)
    anchor = by_top;

  if (plugin->global_menu)
    ooze_global_menu_prepare_for_open (plugin->global_menu);

  try_app = plugin->global_menu &&
            ooze_global_menu_has_app_menu (plugin->global_menu) &&
            !plugin->force_shell_menu;

  if (try_app)
    {
      if (ooze_global_menu_fill_entries (plugin->global_menu,
                                       (guint) menu_index,
                                       &app_entries,
                                       &n_app_entries) &&
          n_app_entries > 0)
        {
          plugin->menu_bar_from_app = TRUE;
          panel_cancel_pending (plugin);
          if (user_toggle)
            ooze_aqua_menu_toggle_for_anchor (plugin->menu_popup, anchor,
                                            app_entries, n_app_entries);
          else
            ooze_aqua_menu_show_for_anchor (plugin->menu_popup, anchor,
                                          app_entries, n_app_entries);
          return;
        }

      plugin->pending_menu_index = menu_index;
      plugin->pending_menu_open = TRUE;
      if (plugin->pending_menu_idle == 0)
        plugin->pending_menu_idle =
          g_timeout_add (50, panel_retry_pending_menu, plugin);
      return;
    }

  panel_cancel_pending (plugin);
  plugin->force_shell_menu = FALSE;

  if (plugin->global_menu &&
      !ooze_global_menu_wants_shell_stubs (plugin->global_menu))
    return;

  focus = meta_display_get_focus_window (
    meta_plugin_get_display (META_PLUGIN (plugin)));
  has_focus = focus != NULL;

#define PANEL_SHOW_SHELL(entries_arr)                                          \
  G_STMT_START {                                                               \
    if (user_toggle)                                                           \
      ooze_aqua_menu_toggle_for_anchor (plugin->menu_popup, anchor,             \
                                      (entries_arr),                          \
                                      G_N_ELEMENTS (entries_arr));            \
    else                                                                       \
      ooze_aqua_menu_show_for_anchor (plugin->menu_popup, anchor,               \
                                    (entries_arr),                            \
                                    G_N_ELEMENTS (entries_arr));              \
  } G_STMT_END

  switch (menu_index)
    {
    case 0:
      {
        OozeAquaMenuEntry entries[] = {
          { "New Finder Window", OOZE_MENU_FILE_NEW_SPOT, TRUE },
          { "Close Window", OOZE_MENU_FILE_CLOSE, has_focus },
        };
        PANEL_SHOW_SHELL (entries);
      }
      break;

    case 1:
      {
        OozeAquaMenuEntry entries[] = {
          { "Undo",       OOZE_MENU_EDIT_UNDO,       FALSE },
          { "Cut",        OOZE_MENU_EDIT_CUT,        FALSE },
          { "Copy",       OOZE_MENU_EDIT_COPY,       FALSE },
          { "Paste",      OOZE_MENU_EDIT_PASTE,      FALSE },
          { NULL, 0, FALSE },
          { "Select All", OOZE_MENU_EDIT_SELECT_ALL, FALSE },
        };
        PANEL_SHOW_SHELL (entries);
      }
      break;

    case 2:
      {
        OozeAquaMenuEntry entries[] = {
          { "as Icons",   OOZE_MENU_VIEW_ICONS,   FALSE },
          { "as List",    OOZE_MENU_VIEW_LIST,    FALSE },
          { "as Columns", OOZE_MENU_VIEW_COLUMNS, FALSE },
        };
        PANEL_SHOW_SHELL (entries);
      }
      break;

    case 3:
      {
        OozeAquaMenuEntry entries[] = {
          { "Computer",
            OOZE_MENU_GO_COMPUTER, TRUE },
          { "Home",
            OOZE_MENU_GO_HOME, TRUE },
          { "Desktop",
            OOZE_MENU_GO_DESKTOP,
            g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP) != NULL },
          { "Documents",
            OOZE_MENU_GO_DOCUMENTS,
            g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS) != NULL },
          { "Downloads",
            OOZE_MENU_GO_DOWNLOADS,
            g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD) != NULL },
          { NULL, 0, FALSE },
          { "Applications", OOZE_MENU_GO_APPLICATIONS, TRUE },
        };
        PANEL_SHOW_SHELL (entries);
      }
      break;

    case 4:
      {
        MetaDisplay *display;
        GList *windows;
        GList *l;
        g_autoptr (GArray) entries = g_array_new (FALSE, FALSE,
                                                  sizeof (OozeAquaMenuEntry));
        OozeAquaMenuEntry row;
        int focus_index = OOZE_MENU_WINDOW_FOCUS_BASE;
        gboolean added_window = FALSE;

        row = (OozeAquaMenuEntry){ "Minimize", OOZE_MENU_WINDOW_MINIMIZE, has_focus };
        g_array_append_val (entries, row);
        row = (OozeAquaMenuEntry){ "Zoom", OOZE_MENU_WINDOW_ZOOM, has_focus };
        g_array_append_val (entries, row);
        row = (OozeAquaMenuEntry){ "Bring All to Front", OOZE_MENU_WINDOW_BRING_ALL, TRUE };
        g_array_append_val (entries, row);

        display = meta_plugin_get_display (META_PLUGIN (plugin));
        windows = meta_display_list_all_windows (display);
        for (l = windows; l != NULL; l = l->next)
          {
            MetaWindow *w = l->data;
            const char *title;

            if (meta_window_get_window_type (w) != META_WINDOW_NORMAL)
              continue;

            if (!added_window)
              {
                row = (OozeAquaMenuEntry){ NULL, 0, FALSE };
                g_array_append_val (entries, row);
                added_window = TRUE;
              }

            title = meta_window_get_title (w);
            if (!title || title[0] == '\0')
              title = "Untitled";

            row = (OozeAquaMenuEntry){ title, focus_index++, TRUE };
            g_array_append_val (entries, row);
          }
        g_list_free (windows);

        if (user_toggle)
          ooze_aqua_menu_toggle_for_anchor (plugin->menu_popup, anchor,
                                          (OozeAquaMenuEntry *) entries->data,
                                          entries->len);
        else
          ooze_aqua_menu_show_for_anchor (plugin->menu_popup, anchor,
                                        (OozeAquaMenuEntry *) entries->data,
                                        entries->len);
      }
      break;

    case 5:
      {
        OozeAquaMenuEntry entries[] = {
          { "About Ooze", OOZE_MENU_HELP_ABOUT, TRUE },
        };
        PANEL_SHOW_SHELL (entries);
      }
      break;

    default:
      break;
    }

#undef PANEL_SHOW_SHELL
}

/* ── Pending menu retry ──────────────────────────────────────────────────── */

static gboolean
panel_retry_pending_menu (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);
  gsize index;

  plugin->pending_menu_idle = 0;

  if (!plugin->pending_menu_open)
    return G_SOURCE_REMOVE;

  if (plugin->menu_popup && ooze_aqua_menu_is_open (plugin->menu_popup))
    {
      panel_cancel_pending (plugin);
      return G_SOURCE_REMOVE;
    }

  panel_ensure_menu_bar (plugin);

  index = plugin->pending_menu_index;
  {
    ClutterActor *anchor = panel_menu_label_for_top (plugin, index);

    if (!anchor)
      {
        panel_cancel_pending (plugin);
        return G_SOURCE_REMOVE;
      }

    if (plugin->pending_menu_retries++ > 40)
      {
        if (plugin->global_menu &&
            (ooze_global_menu_has_dbusmenu (plugin->global_menu) ||
             !ooze_global_menu_wants_shell_stubs (plugin->global_menu)))
          {
            g_warning ("Ooze: app submenu still empty after retries");
            panel_cancel_pending (plugin);
            return G_SOURCE_REMOVE;
          }

        plugin->force_shell_menu = TRUE;
        plugin->pending_menu_open = FALSE;
        panel_show_bar_menu_ex (plugin, index, anchor, FALSE);
        panel_cancel_pending (plugin);
        return G_SOURCE_REMOVE;
      }

    panel_show_bar_menu_ex (plugin, index, anchor, FALSE);
  }
  return G_SOURCE_REMOVE;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

static gboolean
panel_menu_bar_labels_unchanged (OozePlugin  *plugin,
                                 gboolean     from_app,
                                 const char **texts,
                                 guint        n_texts)
{
  guint i;

  if (plugin->n_menu_bar_labels != n_texts)
    return FALSE;
  if (plugin->menu_bar_from_app != from_app)
    return FALSE;

  for (i = 0; i < n_texts; i++)
    {
      const char *cur;

      if (!plugin->menu_bar_labels[i])
        return FALSE;
      cur = g_object_get_data (G_OBJECT (plugin->menu_bar_labels[i]),
                               "ooze-menu-label-text");
      if (g_strcmp0 (cur, texts[i]) != 0)
        return FALSE;
    }

  return TRUE;
}

void
ooze_panel_rebuild_menu_bar (OozePlugin *plugin)
{
  g_autoptr (OozeStallScope) stall = NULL;
  const OozeAquaPalette *palette;
  guint n;
  guint i;
  gboolean from_app;
  const char *texts[OOZE_GLOBAL_MENU_MAX_TOP];
  guint n_texts = 0;

  if (!plugin->panel)
    return;

  stall = ooze_stall_begin ("panel-rebuild");

  palette = ooze_theme_get_palette (NULL);
  from_app = plugin->global_menu &&
             ooze_global_menu_has_app_menu (plugin->global_menu);
  if (from_app)
    n = ooze_global_menu_get_n_top (plugin->global_menu);
  else if (plugin->global_menu &&
           ooze_global_menu_get_x11_launch_hint (plugin->global_menu))
    n = 1;
  else if (plugin->global_menu &&
           ooze_global_menu_wants_shell_stubs (plugin->global_menu))
    n = (guint) ooze_shell_menu_bar_n_items;
  else
    n = 0;
  if (n > OOZE_GLOBAL_MENU_MAX_TOP)
    n = OOZE_GLOBAL_MENU_MAX_TOP;

  for (i = 0; i < n; i++)
    {
      const char *text;
      const char *hint;

      if (from_app)
        {
          text = ooze_global_menu_get_top_label (plugin->global_menu, i);
          if (!text || !*text)
            continue;
        }
      else if ((hint = ooze_global_menu_get_x11_launch_hint (plugin->global_menu)) != NULL)
        text = hint;
      else
        text = ooze_shell_menu_bar_items[i];

      texts[n_texts++] = text;
    }

  if (panel_menu_bar_labels_unchanged (plugin, from_app, texts, n_texts))
    {
      /* Same tops — keep widgets, just re-lay out if needed. */
      ooze_panel_layout_labels (plugin);
      return;
    }

  panel_clear_menu_bar_labels (plugin);
  plugin->menu_bar_from_app = from_app;

  for (i = 0; i < n_texts; i++)
    {
      ClutterActor *label;
      const char *text = texts[i];

      label = panel_create_menu_bar_item (plugin->panel,
                                          OOZE_UI_FONT,
                                          text,
                                          (gfloat) palette->menu_text_r,
                                          (gfloat) palette->menu_text_g,
                                          (gfloat) palette->menu_text_b);
      g_object_set_data (G_OBJECT (label), "ooze-menu-top",
                         GSIZE_TO_POINTER ((gsize) i));
      g_object_set_data_full (G_OBJECT (label), "ooze-menu-label-text",
                              g_strdup (text), g_free);
      if (from_app || !ooze_global_menu_get_x11_launch_hint (plugin->global_menu))
        {
          g_signal_connect (label, "button-press-event",
                            G_CALLBACK (on_menu_bar_pressed), plugin);
        }

      if (plugin->clock_label)
        clutter_actor_insert_child_below (plugin->panel, label, plugin->clock_label);
      else
        clutter_actor_add_child (plugin->panel, label);

      plugin->menu_bar_labels[plugin->n_menu_bar_labels++] = label;
    }

  ooze_panel_layout_labels (plugin);
}

void
ooze_panel_recolor_menu_bar (OozePlugin *plugin)
{
  const OozeAquaPalette *palette;
  guint i;
  gboolean missing_text = FALSE;

  if (!plugin->panel)
    return;

  palette = ooze_theme_get_palette (NULL);

  for (i = 0; i < plugin->n_menu_bar_labels; i++)
    {
      ClutterActor *bin = plugin->menu_bar_labels[i];
      ClutterActor *label;
      const char *text;
      gfloat label_w;
      gfloat label_h;

      if (!bin)
        continue;

      text = g_object_get_data (G_OBJECT (bin), "ooze-menu-label-text");
      label = clutter_actor_get_first_child (bin);
      if (!text || !*text || !label)
        {
          missing_text = TRUE;
          break;
        }

      panel_set_text_label (label,
                            OOZE_UI_FONT,
                            text,
                            (gfloat) palette->menu_text_r,
                            (gfloat) palette->menu_text_g,
                            (gfloat) palette->menu_text_b);
      label_w = clutter_actor_get_width (label);
      label_h = clutter_actor_get_height (label);
      panel_make_click_target (bin, label_w, MENU_BAR_HIT_HEIGHT);
      clutter_actor_set_position (label,
                                  0.0f,
                                  panel_vertical_center (MENU_BAR_HIT_HEIGHT,
                                                         label_h));
    }

  if (missing_text)
    {
      ooze_panel_rebuild_menu_bar (plugin);
      return;
    }

  ooze_panel_update_clock (plugin);
  ooze_panel_layout_labels (plugin);
}

void
ooze_panel_layout_labels (OozePlugin *plugin)
{
  ClutterActor *child;
  gfloat menu_x;

  if (!plugin->panel)
    return;

  if (plugin->menu_icon)
    menu_x = OOZE_BUTTON_MARGIN +
              clutter_actor_get_width (plugin->menu_icon) + 6.0f;
  else
    menu_x = OOZE_BUTTON_MARGIN;

  for (child = clutter_actor_get_first_child (plugin->panel);
       child != NULL;
       child = clutter_actor_get_next_sibling (child))
    {
      if (panel_child_is_fixed_chrome (plugin, child))
        continue;

      clutter_actor_set_position (child, menu_x, 0.0f);
      menu_x += clutter_actor_get_width (child) + MENU_ITEM_GAP;
    }
}

void
ooze_panel_refresh_ooze_button (OozePlugin *plugin)
{
  g_autoptr (ClutterContent) content = NULL;
  int width = 1;
  int height = 1;

  if (!plugin->menu_icon)
    return;

  content = ooze_aqua_ooze_button_content (plugin->panel, &width, &height);
  if (!content)
    return;

  ooze_aqua_actor_set_content (plugin->menu_icon,
                             g_steal_pointer (&content),
                             width,
                             height);
  clutter_actor_set_size (plugin->menu_icon, (gfloat) width, (gfloat) height);
  clutter_actor_set_content_gravity (plugin->menu_icon,
                                     CLUTTER_CONTENT_GRAVITY_CENTER);
  clutter_actor_set_position (plugin->menu_icon,
                              OOZE_BUTTON_MARGIN,
                              OOZE_BUTTON_PAD_Y);
}

void
ooze_panel_refresh_texture (OozePlugin *plugin, int width)
{
  g_autoptr (ClutterContent) content = NULL;

  if (!plugin->panel || width < 1)
    return;

  if (width == plugin->last_panel_width)
    return;

  content = ooze_aqua_pinstripe_content (plugin->panel, width, (int) PANEL_HEIGHT);
  if (content)
    ooze_aqua_actor_set_content (plugin->panel,
                               g_steal_pointer (&content),
                               width,
                               (int) PANEL_HEIGHT);

  plugin->last_panel_width = width;
}

void
ooze_panel_update_clock (OozePlugin *plugin)
{
  time_t now;
  struct tm *tm_local;
  char buffer[64];

  if (!plugin->clock_label)
    return;

  now = time (NULL);
  tm_local = localtime (&now);
  if (!tm_local)
    return;

  strftime (buffer, sizeof buffer, "%a %b %e  %l:%M %p", tm_local);
  {
    const OozeAquaPalette *palette = ooze_theme_get_palette (NULL);

    panel_set_text_label (plugin->clock_label,
                          OOZE_UI_FONT,
                          buffer,
                          (gfloat) palette->menu_text_r,
                          (gfloat) palette->menu_text_g,
                          (gfloat) palette->menu_text_b);
  }
}

static gboolean
on_clock_tick (gpointer user_data)
{
  ooze_panel_update_clock (OOZE_PLUGIN (user_data));
  return G_SOURCE_CONTINUE;
}

void
ooze_panel_schedule_rebuild (OozePlugin *plugin)
{
  if (plugin->menubar_rebuild_idle != 0)
    g_source_remove (plugin->menubar_rebuild_idle);

  plugin->menubar_rebuild_idle =
    g_timeout_add (PANEL_REBUILD_DEBOUNCE_MS, panel_rebuild_idle, plugin);
}

void
ooze_panel_on_global_menu_changed (gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);
  guint pending_top = 0;
  gboolean retry;

  retry = FALSE;
  if (plugin->global_menu &&
      ooze_global_menu_take_pending_top (plugin->global_menu, &pending_top))
    {
      plugin->pending_menu_index = pending_top;
      retry = TRUE;
    }
  else if (plugin->pending_menu_open)
    {
      retry = TRUE;
    }

  if (plugin->menu_popup && ooze_aqua_menu_is_open (plugin->menu_popup))
    {
      plugin->menu_bar_needs_rebuild = TRUE;
      if (retry)
        panel_cancel_pending (plugin);
      return;
    }

  ooze_panel_schedule_rebuild (plugin);

  if (!retry)
    {
      panel_cancel_pending (plugin);
      return;
    }

  plugin->pending_menu_open = TRUE;
  if (plugin->pending_menu_idle == 0)
    plugin->pending_menu_idle =
      g_timeout_add (50, panel_retry_pending_menu, plugin);
}

void
ooze_panel_setup (OozePlugin       *plugin,
                  MetaDisplay    *display,
                  MetaCompositor *compositor)
{
  ClutterActor *stage;
  g_autoptr (ClutterContent) ooze_content = NULL;
  int ooze_width = 1;
  int ooze_height = 1;
  const OozeAquaPalette *palette;

  if (plugin->panel)
    return;

  stage = CLUTTER_ACTOR (meta_compositor_get_stage (compositor));
  palette = ooze_theme_get_palette (NULL);

  plugin->panel = clutter_actor_new ();
  clutter_actor_set_reactive (plugin->panel, TRUE);

  ooze_content = ooze_aqua_ooze_button_content (plugin->panel,
                                              &ooze_width,
                                              &ooze_height);
  plugin->menu_icon = clutter_actor_new ();
  panel_make_click_target (plugin->menu_icon,
                           (gfloat) ooze_width,
                           (gfloat) ooze_height);
  if (ooze_content)
    ooze_aqua_actor_set_content (plugin->menu_icon,
                               g_steal_pointer (&ooze_content),
                               ooze_width,
                               ooze_height);
  clutter_actor_set_size (plugin->menu_icon,
                          (gfloat) ooze_width,
                          (gfloat) ooze_height);
  clutter_actor_set_content_gravity (plugin->menu_icon,
                                     CLUTTER_CONTENT_GRAVITY_CENTER);
  clutter_actor_set_position (plugin->menu_icon,
                              OOZE_BUTTON_MARGIN,
                              OOZE_BUTTON_PAD_Y);
  clutter_actor_add_child (plugin->panel, plugin->menu_icon);

  if (!plugin->menu_popup)
    {
      plugin->menu_popup = ooze_aqua_menu_new (plugin->context,
                                             stage,
                                             ooze_shell_menu_action,
                                             plugin);
    }

  g_signal_connect (plugin->menu_icon,
                    "button-press-event",
                    G_CALLBACK (on_ooze_button_pressed),
                    plugin);

  plugin->clock_label = panel_create_text_label (plugin->panel,
                                                 OOZE_UI_FONT,
                                                 "Sat Jan  1  12:00 PM",
                                                 (gfloat) palette->menu_text_r,
                                                 (gfloat) palette->menu_text_g,
                                                 (gfloat) palette->menu_text_b);
  clutter_actor_add_child (plugin->panel, plugin->clock_label);

  ooze_panel_rebuild_menu_bar (plugin);
  ooze_panel_update_clock (plugin);

  if (!plugin->clock_timer)
    plugin->clock_timer = g_timeout_add_seconds (30, on_clock_tick, plugin);

  clutter_actor_add_child (stage, plugin->panel);
  clutter_actor_show (plugin->panel);
}

void
ooze_panel_dispose (OozePlugin *plugin)
{
  if (plugin->clock_timer)
    {
      g_source_remove (plugin->clock_timer);
      plugin->clock_timer = 0;
    }
  if (plugin->pending_menu_idle)
    {
      g_source_remove (plugin->pending_menu_idle);
      plugin->pending_menu_idle = 0;
    }
  if (plugin->menubar_rebuild_idle)
    {
      g_source_remove (plugin->menubar_rebuild_idle);
      plugin->menubar_rebuild_idle = 0;
    }
  plugin->pending_menu_open = FALSE;

  g_clear_pointer (&plugin->panel, clutter_actor_destroy);
  plugin->menu_icon = NULL;
  plugin->clock_label = NULL;
  plugin->tray_box = NULL;
  memset (plugin->menu_bar_labels, 0, sizeof plugin->menu_bar_labels);
  plugin->n_menu_bar_labels = 0;
  plugin->menu_bar_from_app = FALSE;
}
