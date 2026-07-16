#include "ooze-tray.h"

#include "ooze-aqua-draw.h"
#include "ooze-aqua-menu.h"
#include "ooze-dbusmenu.h"
#include "ooze-plugin-priv.h"
#include "ooze-sni.h"
#include "ooze-stall.h"
#include "../shared/ooze-icon-lookup.h"
#include "ooze-theme.h"

#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>

#define OOZE_TRAY_ICON_SIZE   20.0f
#define OOZE_TRAY_ICON_GAP     6.0f
#define OOZE_TRAY_RIGHT_PAD   10.0f

typedef struct
{
  OozePlugin  *plugin;
  OozeSniItem *item;
  ClutterActor *actor;
} OozeTrayIcon;

typedef struct
{
  OozePlugin *plugin;
  OozeSniItem *item;
  OozeDbusmenu *menu;
  OozeDbusmenuItem *flat;
  gsize n_flat;
} OozeTrayMenuCtx;

static void
ooze_tray_icon_free (gpointer data)
{
  OozeTrayIcon *icon = data;

  if (!icon)
    return;
  if (icon->actor)
    clutter_actor_destroy (icon->actor);
  g_free (icon);
}

static OozeTrayIcon *
ooze_tray_find_icon (OozePlugin *plugin, OozeSniItem *item)
{
  guint i;

  if (!plugin->tray_icons)
    return NULL;

  for (i = 0; i < plugin->tray_icons->len; i++)
    {
      OozeTrayIcon *icon = plugin->tray_icons->pdata[i];
      if (icon->item == item)
        return icon;
    }
  return NULL;
}

static void
ooze_tray_update_icon_content (OozeTrayIcon *icon)
{
  GdkPixbuf *pb;
  g_autoptr (GdkPixbuf) fallback_pb = NULL;
  g_autoptr (ClutterContent) content = NULL;
  CoglColor transparent;

  if (!icon || !icon->actor)
    return;

  /* Ubuntu's AppIndicator host and KDE's SNI host never draw a background or
   * border behind a tray icon (St.Icon "system-status-icon", padding:0). Keep
   * the actor fully transparent so app-provided icons render flat and clean. */
  cogl_color_init_from_4f (&transparent, 0.0f, 0.0f, 0.0f, 0.0f);
  clutter_actor_set_background_color (icon->actor, &transparent);

  pb = ooze_sni_item_icon_pixbuf (icon->item);
  if (!pb)
    {
      /* No resolvable icon: show a themed fallback glyph like GNOME's
       * fallbackIconName, not a coloured placeholder box. */
      fallback_pb = ooze_icon_lookup_load ("image-missing",
                                           (int) (OOZE_TRAY_ICON_SIZE * 2.0f));
      pb = fallback_pb;
    }

  if (pb)
    content = ooze_aqua_content_from_pixbuf (icon->actor, pb);

  g_message ("OOZE_TRAY_DEBUG: content name='%s' sni_pb=%p fallback_pb=%p content=%p",
             ooze_sni_item_icon_name (icon->item),
             (void *) ooze_sni_item_icon_pixbuf (icon->item),
             (void *) fallback_pb,
             (void *) content);

  clutter_actor_set_content (icon->actor, content);
  if (content)
    clutter_actor_set_content_gravity (icon->actor,
                                       CLUTTER_CONTENT_GRAVITY_RESIZE_ASPECT);
}

static void
ooze_tray_menu_action (gpointer user_data, int action_id)
{
  OozeTrayMenuCtx *ctx = user_data;
  gsize i;

  if (!ctx || !ctx->menu)
    return;

  for (i = 0; i < ctx->n_flat; i++)
    {
      if (ctx->flat[i].id == action_id)
        {
          ooze_dbusmenu_click (ctx->menu, action_id);
          break;
        }
    }
}

static void
ooze_tray_menu_ctx_free (OozeTrayMenuCtx *ctx)
{
  if (!ctx)
    return;
  ooze_dbusmenu_items_free (ctx->flat, ctx->n_flat);
  ooze_dbusmenu_free (ctx->menu);
  g_free (ctx);
}

static gboolean
ooze_tray_open_menu_idle (gpointer user_data)
{
  OozeTrayIcon *icon = user_data;
  OozePlugin *plugin;
  OozeTrayMenuCtx *ctx;
  g_autoptr (OozeStallScope) stall = NULL;
  g_autoptr (GDBusConnection) session = NULL;
  g_autoptr (GError) error = NULL;
  const char *menu_path;
  const char *bus_name;
  OozeAquaMenuEntry *entries = NULL;
  gsize n = 0;
  gsize i;
  gsize out = 0;

  if (!icon || !icon->plugin || !icon->item)
    return G_SOURCE_REMOVE;

  plugin = icon->plugin;
  if (plugin->shutting_down)
    return G_SOURCE_REMOVE;
  menu_path = ooze_sni_item_menu_path (icon->item);
  bus_name = ooze_sni_item_bus_name (icon->item);
  g_message ("OOZE_TRAY_DEBUG: open_menu_idle bus=%s path=%s", bus_name, menu_path);
  if (!menu_path || !menu_path[0] || !bus_name)
    {
      g_message ("OOZE_TRAY_DEBUG: no menu path -> Activate");
      ooze_sni_item_activate (icon->item, 0, 0);
      return G_SOURCE_REMOVE;
    }

  stall = ooze_stall_begin ("tray-dbusmenu");
  session = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (!session)
    {
      g_warning ("Ooze tray: session bus unavailable: %s", error->message);
      return G_SOURCE_REMOVE;
    }

  ctx = g_new0 (OozeTrayMenuCtx, 1);
  ctx->plugin = plugin;
  ctx->item = icon->item;
  ctx->menu = ooze_dbusmenu_new (session, bus_name, menu_path);
  if (!ctx->menu ||
      !ooze_dbusmenu_get_top_items (ctx->menu, &ctx->flat, &ctx->n_flat) ||
      ctx->n_flat == 0)
    {
      g_message ("OOZE_TRAY_DEBUG: dbusmenu empty (menu=%p n_flat=%zu) -> Activate",
                 (void *) ctx->menu, ctx->menu ? ctx->n_flat : 0);
      ooze_tray_menu_ctx_free (ctx);
      ooze_sni_item_activate (icon->item, 0, 0);
      return G_SOURCE_REMOVE;
    }

  n = ctx->n_flat;
  entries = g_new0 (OozeAquaMenuEntry, n);
  for (i = 0; i < n; i++)
    {
      if (ctx->flat[i].separator || !ctx->flat[i].visible)
        continue;
      if (!ctx->flat[i].label || !ctx->flat[i].label[0])
        continue;
      entries[out].label = ctx->flat[i].label;
      entries[out].action_id = ctx->flat[i].id;
      entries[out].sensitive = ctx->flat[i].enabled;
      out++;
    }

  if (out == 0)
    {
      g_free (entries);
      ooze_tray_menu_ctx_free (ctx);
      ooze_sni_item_activate (icon->item, 0, 0);
      return G_SOURCE_REMOVE;
    }

  if (plugin->tray_menu_ctx)
    ooze_tray_menu_ctx_free (plugin->tray_menu_ctx);
  plugin->tray_menu_ctx = ctx;

  if (!plugin->tray_popup)
    {
      MetaDisplay *display = meta_plugin_get_display (META_PLUGIN (plugin));
      MetaBackend *backend =
        meta_context_get_backend (meta_display_get_context (display));
      ClutterActor *stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));

      plugin->tray_popup = ooze_aqua_menu_new (plugin->context,
                                               stage,
                                               ooze_tray_menu_action,
                                               ctx);
    }
  else
    {
      /* Rebuild so callback user_data points at the latest ctx. */
      MetaDisplay *display = meta_plugin_get_display (META_PLUGIN (plugin));
      MetaBackend *backend =
        meta_context_get_backend (meta_display_get_context (display));
      ClutterActor *stage = CLUTTER_ACTOR (meta_backend_get_stage (backend));

      ooze_aqua_menu_destroy (plugin->tray_popup);
      plugin->tray_popup = ooze_aqua_menu_new (plugin->context,
                                               stage,
                                               ooze_tray_menu_action,
                                               ctx);
    }

  g_message ("OOZE_TRAY_DEBUG: showing tray menu with %zu entries", out);
  ooze_aqua_menu_show_for_anchor (plugin->tray_popup,
                                  icon->actor,
                                  entries,
                                  out);
  g_free (entries);
  return G_SOURCE_REMOVE;
}

static gboolean
ooze_tray_on_icon_pressed (ClutterActor *actor G_GNUC_UNUSED,
                           ClutterEvent *event,
                           gpointer      user_data)
{
  OozeTrayIcon *icon = user_data;
  guint button;

  g_message ("OOZE_TRAY_DEBUG: icon pressed, event type=%d", clutter_event_type (event));
  if (clutter_event_type (event) != CLUTTER_BUTTON_PRESS)
    return CLUTTER_EVENT_PROPAGATE;

  button = clutter_event_get_button (event);
  g_message ("OOZE_TRAY_DEBUG: button=%u menu_path=%s", button, ooze_sni_item_menu_path (icon->item));
  if (button == CLUTTER_BUTTON_PRIMARY)
    {
      /* Prefer the exported menu when available. Some indicators leave
       * ItemIsMenu false even though Activate is a no-op. */
      if (ooze_sni_item_menu_path (icon->item))
        {
          g_idle_add (ooze_tray_open_menu_idle, icon);
        }
      else
        ooze_sni_item_activate (icon->item, 0, 0);
      return CLUTTER_EVENT_STOP;
    }

  if (button == CLUTTER_BUTTON_SECONDARY)
    {
      if (ooze_sni_item_menu_path (icon->item))
        g_idle_add (ooze_tray_open_menu_idle, icon);
      else
        ooze_sni_item_context_menu (icon->item, 0, 0);
      return CLUTTER_EVENT_STOP;
    }

  return CLUTTER_EVENT_PROPAGATE;
}

static void
ooze_tray_add_or_update (OozePlugin *plugin, OozeSniItem *item)
{
  OozeTrayIcon *icon;

  icon = ooze_tray_find_icon (plugin, item);
  if (!icon)
    {
      icon = g_new0 (OozeTrayIcon, 1);
      icon->plugin = plugin;
      icon->item = item;
      icon->actor = clutter_actor_new ();
      clutter_actor_set_reactive (icon->actor, TRUE);
      clutter_actor_set_size (icon->actor, OOZE_TRAY_ICON_SIZE, OOZE_TRAY_ICON_SIZE);
      g_signal_connect (icon->actor, "button-press-event",
                        G_CALLBACK (ooze_tray_on_icon_pressed), icon);
      clutter_actor_add_child (plugin->tray_box, icon->actor);
      g_ptr_array_add (plugin->tray_icons, icon);
    }

  ooze_tray_update_icon_content (icon);
  ooze_tray_layout (plugin,
                    clutter_actor_get_width (plugin->panel),
                    clutter_actor_get_height (plugin->panel));
  {
    gfloat ax, ay, tx, ty;
    clutter_actor_get_transformed_position (icon->actor, &ax, &ay);
    clutter_actor_get_transformed_position (plugin->tray_box, &tx, &ty);
    g_message ("OOZE_TRAY_DEBUG: icon abs=(%.0f,%.0f) size=(%.0fx%.0f) reactive=%d tray_box abs=(%.0f,%.0f) panel_w=%.0f",
               ax, ay,
               clutter_actor_get_width (icon->actor),
               clutter_actor_get_height (icon->actor),
               clutter_actor_get_reactive (icon->actor),
               tx, ty,
               clutter_actor_get_width (plugin->panel));
  }
}

static void
ooze_tray_on_item_changed (OozeSniItem *item, gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);

  if (!plugin->shutting_down)
    ooze_tray_add_or_update (plugin, item);
}

static void
ooze_tray_on_item_removed (OozeSniItem *item, gpointer user_data)
{
  OozePlugin *plugin = OOZE_PLUGIN (user_data);
  guint i;

  if (plugin->shutting_down || !plugin->tray_icons)
    return;

  for (i = 0; i < plugin->tray_icons->len; i++)
    {
      OozeTrayIcon *icon = plugin->tray_icons->pdata[i];
      if (icon->item == item)
        {
          g_ptr_array_remove_index (plugin->tray_icons, i);
          ooze_tray_layout (plugin,
                            clutter_actor_get_width (plugin->panel),
                            clutter_actor_get_height (plugin->panel));
          return;
        }
    }
}

gfloat
ooze_tray_width (OozePlugin *plugin)
{
  guint n;

  if (!plugin || !plugin->tray_icons || plugin->tray_icons->len == 0)
    return 0.0f;

  n = plugin->tray_icons->len;
  return (gfloat) n * OOZE_TRAY_ICON_SIZE +
         (gfloat) (n > 0 ? n - 1 : 0) * OOZE_TRAY_ICON_GAP +
         OOZE_TRAY_RIGHT_PAD;
}

void
ooze_tray_layout (OozePlugin *plugin, gfloat panel_width, gfloat panel_height)
{
  gfloat tray_w;
  gfloat x;
  gfloat y;
  guint i;

  if (!plugin || !plugin->tray_box)
    return;

  tray_w = ooze_tray_width (plugin);
  clutter_actor_set_size (plugin->tray_box, MAX (tray_w, 1.0f), panel_height);

  x = 0.0f;
  y = (panel_height - OOZE_TRAY_ICON_SIZE) / 2.0f;
  for (i = 0; plugin->tray_icons && i < plugin->tray_icons->len; i++)
    {
      OozeTrayIcon *icon = plugin->tray_icons->pdata[i];
      clutter_actor_set_position (icon->actor, x, y);
      clutter_actor_set_size (icon->actor, OOZE_TRAY_ICON_SIZE, OOZE_TRAY_ICON_SIZE);
      x += OOZE_TRAY_ICON_SIZE + OOZE_TRAY_ICON_GAP;
    }

  if (plugin->clock_label)
    {
      gfloat clock_w = clutter_actor_get_width (plugin->clock_label);
      clutter_actor_set_position (plugin->tray_box,
                                  panel_width - 12.0f - clock_w - tray_w,
                                  0.0f);
    }
  else
    {
      clutter_actor_set_position (plugin->tray_box,
                                  panel_width - 12.0f - tray_w,
                                  0.0f);
    }
}

static gboolean
ooze_tray_appearance_idle (gpointer user_data)
{
  OozePlugin *plugin = user_data;
  guint i;

  plugin->tray_appearance_idle = 0;
  if (plugin->shutting_down || !plugin->tray_icons)
    return G_SOURCE_REMOVE;

  for (i = 0; i < plugin->tray_icons->len; i++)
    {
      OozeTrayIcon *icon = plugin->tray_icons->pdata[i];
      if (!icon || !icon->item)
        continue;
      /* Named icons re-resolve (symbolic tint tracks palette). Pixmap-only: leave. */
      ooze_sni_item_reresolve_icon (icon->item);
      /* Placeholder actors repaint here — reresolve skips unloadable icons. */
      ooze_tray_update_icon_content (icon);
    }

  return G_SOURCE_REMOVE;
}

void
ooze_tray_refresh_appearance (OozePlugin *plugin)
{
  if (!plugin || !plugin->tray_icons || plugin->tray_icons->len == 0)
    return;

  if (plugin->tray_appearance_idle)
    return;

  plugin->tray_appearance_idle = g_idle_add (ooze_tray_appearance_idle, plugin);
}

void
ooze_tray_setup (OozePlugin *plugin)
{
  if (!plugin || !plugin->panel || plugin->tray_box)
    return;

  plugin->tray_box = clutter_actor_new ();
  clutter_actor_set_reactive (plugin->tray_box, FALSE);
  clutter_actor_add_child (plugin->panel, plugin->tray_box);

  plugin->tray_icons = g_ptr_array_new_with_free_func (ooze_tray_icon_free);
  plugin->tray_menu_ctx = NULL;
  plugin->tray_appearance_idle = 0;

  plugin->sni_watcher = ooze_sni_watcher_new (ooze_tray_on_item_changed,
                                              ooze_tray_on_item_removed,
                                              plugin);
}

void
ooze_tray_dispose (OozePlugin *plugin)
{
  if (!plugin)
    return;

  if (plugin->tray_appearance_idle)
    {
      g_source_remove (plugin->tray_appearance_idle);
      plugin->tray_appearance_idle = 0;
    }

  if (plugin->tray_menu_ctx)
    {
      ooze_tray_menu_ctx_free (plugin->tray_menu_ctx);
      plugin->tray_menu_ctx = NULL;
    }

  g_clear_pointer (&plugin->tray_popup, ooze_aqua_menu_destroy);
  g_clear_pointer (&plugin->sni_watcher, ooze_sni_watcher_free);
  g_clear_pointer (&plugin->tray_icons, g_ptr_array_unref);
  if (plugin->tray_box)
    {
      clutter_actor_destroy (plugin->tray_box);
      plugin->tray_box = NULL;
    }
}
