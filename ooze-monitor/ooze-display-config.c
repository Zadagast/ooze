#include "ooze-display-config.h"

#include <gio/gio.h>
#include <string.h>

#define DC_BUS   "org.gnome.Mutter.DisplayConfig"
#define DC_PATH  "/org/gnome/Mutter/DisplayConfig"
#define DC_IFACE "org.gnome.Mutter.DisplayConfig"

/* mode tuple format from GetCurrentState */
#define MODE_FORMAT      "(siiddada{sv})"
#define MODES_FORMAT     "a" MODE_FORMAT
#define MONITOR_FORMAT   "((ssss)" MODES_FORMAT "a{sv})"
#define MONITORS_FORMAT  "a" MONITOR_FORMAT
#define LM_FORMAT        "(iiduba(ssss)a{sv})"
#define LMS_FORMAT       "a" LM_FORMAT

static void
ooze_display_mode_free (OozeDisplayMode *mode)
{
  if (!mode)
    return;
  g_free (mode->id);
  if (mode->supported_scales)
    g_array_unref (mode->supported_scales);
  g_free (mode);
}

static OozeDisplayMode *
ooze_display_mode_copy (const OozeDisplayMode *src)
{
  OozeDisplayMode *mode;
  guint i;

  if (!src)
    return NULL;

  mode = g_new0 (OozeDisplayMode, 1);
  mode->id = g_strdup (src->id);
  mode->width = src->width;
  mode->height = src->height;
  mode->refresh = src->refresh;
  mode->is_current = src->is_current;
  mode->is_preferred = src->is_preferred;
  mode->preferred_scale = src->preferred_scale;
  mode->supported_scales = g_array_new (FALSE, FALSE, sizeof (double));
  if (src->supported_scales)
    {
      for (i = 0; i < src->supported_scales->len; i++)
        {
          double s = g_array_index (src->supported_scales, double, i);

          g_array_append_val (mode->supported_scales, s);
        }
    }
  return mode;
}

static void
ooze_display_monitor_free (OozeDisplayMonitor *monitor)
{
  if (!monitor)
    return;

  g_free (monitor->connector);
  g_free (monitor->display_name);
  g_free (monitor->current_mode_id);
  if (monitor->modes)
    g_ptr_array_free (monitor->modes, TRUE);
  g_free (monitor);
}

static OozeDisplayMonitor *
ooze_display_monitor_copy (const OozeDisplayMonitor *src)
{
  OozeDisplayMonitor *monitor;
  guint i;

  if (!src)
    return NULL;

  monitor = g_new0 (OozeDisplayMonitor, 1);
  monitor->connector = g_strdup (src->connector);
  monitor->display_name = g_strdup (src->display_name);
  monitor->layout_x = src->layout_x;
  monitor->layout_y = src->layout_y;
  monitor->scale = src->scale;
  monitor->transform = src->transform;
  monitor->primary = src->primary;
  monitor->current_mode_id = g_strdup (src->current_mode_id);
  monitor->modes = g_ptr_array_new_with_free_func ((GDestroyNotify) ooze_display_mode_free);
  if (src->modes)
    {
      for (i = 0; i < src->modes->len; i++)
        g_ptr_array_add (monitor->modes,
                         ooze_display_mode_copy (src->modes->pdata[i]));
    }
  return monitor;
}

void
ooze_display_config_free (OozeDisplayConfig *config)
{
  if (!config)
    return;

  if (config->monitors)
    g_ptr_array_free (config->monitors, TRUE);
  g_free (config);
}

OozeDisplayConfig *
ooze_display_config_copy (const OozeDisplayConfig *config)
{
  OozeDisplayConfig *copy;
  guint i;

  if (!config)
    return NULL;

  copy = g_new0 (OozeDisplayConfig, 1);
  copy->serial = config->serial;
  copy->layout_mode = config->layout_mode;
  copy->supports_changing_layout_mode = config->supports_changing_layout_mode;
  copy->global_scale_required = config->global_scale_required;
  copy->monitors = g_ptr_array_new_with_free_func ((GDestroyNotify) ooze_display_monitor_free);
  if (config->monitors)
    {
      for (i = 0; i < config->monitors->len; i++)
        g_ptr_array_add (copy->monitors,
                         ooze_display_monitor_copy (config->monitors->pdata[i]));
    }
  return copy;
}

void
ooze_display_config_set_primary (OozeDisplayConfig *config,
                                 guint              index)
{
  guint i;

  g_return_if_fail (config != NULL);
  g_return_if_fail (config->monitors != NULL);
  g_return_if_fail (index < config->monitors->len);

  for (i = 0; i < config->monitors->len; i++)
    {
      OozeDisplayMonitor *m = config->monitors->pdata[i];

      m->primary = (i == index);
    }
}

const OozeDisplayMode *
ooze_display_monitor_current_mode (const OozeDisplayMonitor *monitor)
{
  guint i;

  if (!monitor || !monitor->modes)
    return NULL;

  if (monitor->current_mode_id)
    {
      for (i = 0; i < monitor->modes->len; i++)
        {
          OozeDisplayMode *mode = monitor->modes->pdata[i];

          if (g_strcmp0 (mode->id, monitor->current_mode_id) == 0)
            return mode;
        }
    }

  for (i = 0; i < monitor->modes->len; i++)
    {
      OozeDisplayMode *mode = monitor->modes->pdata[i];

      if (mode->is_current)
        return mode;
    }

  if (monitor->modes->len > 0)
    return monitor->modes->pdata[0];

  return NULL;
}

gboolean
ooze_display_monitor_is_nest_dummy (const OozeDisplayMonitor *monitor)
{
  return monitor && monitor->connector &&
         g_str_has_prefix (monitor->connector, "Meta-");
}

static GDBusProxy *
ooze_display_config_proxy (GError **error)
{
  return g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                        G_DBUS_PROXY_FLAGS_NONE,
                                        NULL,
                                        DC_BUS,
                                        DC_PATH,
                                        DC_IFACE,
                                        NULL,
                                        error);
}

gboolean
ooze_display_config_allowed (void)
{
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) value = NULL;

  proxy = ooze_display_config_proxy (&error);
  if (!proxy)
    return FALSE;

  value = g_dbus_proxy_get_cached_property (proxy, "ApplyMonitorsConfigAllowed");
  if (!value)
    return TRUE;

  return g_variant_get_boolean (value);
}

static OozeDisplayMode *
ooze_display_mode_parse (GVariant *mode_tuple)
{
  OozeDisplayMode *mode = g_new0 (OozeDisplayMode, 1);
  const char *id = NULL;
  g_autoptr (GVariant) props = NULL;
  g_autoptr (GVariantIter) scales_iter = NULL;
  double d;

  mode->supported_scales = g_array_new (FALSE, FALSE, sizeof (double));

  g_variant_get (mode_tuple, "(&siidd@ad@a{sv})",
                 &id,
                 &mode->width,
                 &mode->height,
                 &mode->refresh,
                 &mode->preferred_scale,
                 &scales_iter,
                 &props);

  while (g_variant_iter_next (scales_iter, "d", &d))
    g_array_append_val (mode->supported_scales, d);

  mode->id = g_strdup (id);

  if (props)
    {
      GVariantIter piter;
      const char *key;
      GVariant *val;

      g_variant_iter_init (&piter, props);
      while (g_variant_iter_loop (&piter, "{&sv}", &key, &val))
        {
          if (g_strcmp0 (key, "is-current") == 0)
            mode->is_current = g_variant_get_boolean (val);
          else if (g_strcmp0 (key, "is-preferred") == 0)
            mode->is_preferred = g_variant_get_boolean (val);
        }
    }

  return mode;
}

static GPtrArray *
ooze_display_parse_modes (GVariant *modes_variant)
{
  GPtrArray *modes = g_ptr_array_new_with_free_func ((GDestroyNotify) ooze_display_mode_free);
  GVariantIter modes_iter;
  GVariant *mode_tuple;

  if (!modes_variant)
    return modes;

  g_variant_iter_init (&modes_iter, modes_variant);
  while ((mode_tuple = g_variant_iter_next_value (&modes_iter)))
    {
      g_ptr_array_add (modes, ooze_display_mode_parse (mode_tuple));
      g_variant_unref (mode_tuple);
    }

  return modes;
}

static gboolean
ooze_display_fill_monitor_modes (GVariant            *monitors_variant,
                                 OozeDisplayMonitor  *monitor)
{
  GVariantIter monitors_iter;
  GVariant *monitor_tuple;

  g_return_val_if_fail (monitor != NULL, FALSE);
  g_return_val_if_fail (monitor->connector != NULL, FALSE);

  if (!monitors_variant)
    return FALSE;

  g_variant_iter_init (&monitors_iter, monitors_variant);
  while ((monitor_tuple = g_variant_iter_next_value (&monitors_iter)))
    {
      const char *mon_connector = NULL;
      const char *vendor = NULL;
      const char *product = NULL;
      const char *serial = NULL;
      g_autoptr (GVariant) modes_v = NULL;
      g_autoptr (GVariant) props = NULL;
      guint i;

      g_variant_get (monitor_tuple, "((&s&s&s&s)@" MODES_FORMAT "@a{sv})",
                     &mon_connector, &vendor, &product, &serial,
                     &modes_v, &props);

      if (g_strcmp0 (mon_connector, monitor->connector) != 0)
        {
          g_variant_unref (monitor_tuple);
          continue;
        }

      if (props)
        {
          GVariantIter piter;
          const char *key;
          GVariant *val;

          g_variant_iter_init (&piter, props);
          while (g_variant_iter_loop (&piter, "{&sv}", &key, &val))
            {
              if (g_strcmp0 (key, "display-name") == 0)
                {
                  g_free (monitor->display_name);
                  monitor->display_name = g_variant_dup_string (val, NULL);
                }
            }
        }

      if (!monitor->display_name || !*monitor->display_name)
        {
          g_free (monitor->display_name);
          monitor->display_name = (product && *product)
            ? g_strdup (product)
            : g_strdup (mon_connector);
        }

      if (monitor->modes)
        g_ptr_array_free (monitor->modes, TRUE);
      monitor->modes = ooze_display_parse_modes (modes_v);

      g_clear_pointer (&monitor->current_mode_id, g_free);
      for (i = 0; i < monitor->modes->len; i++)
        {
          OozeDisplayMode *mode = monitor->modes->pdata[i];

          if (mode->is_current)
            {
              monitor->current_mode_id = g_strdup (mode->id);
              break;
            }
        }

      g_variant_unref (monitor_tuple);
      return TRUE;
    }

  return FALSE;
}

gboolean
ooze_display_config_load (OozeDisplayConfig **config_out,
                          GError            **error)
{
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GVariant) monitors = NULL;
  g_autoptr (GVariant) logical_monitors = NULL;
  g_autoptr (GVariant) global_props = NULL;
  OozeDisplayConfig *config;
  GVariantIter logical_iter;
  GVariant *logical_tuple;

  g_return_val_if_fail (config_out != NULL, FALSE);
  *config_out = NULL;

  proxy = ooze_display_config_proxy (error);
  if (!proxy)
    return FALSE;

  reply = g_dbus_proxy_call_sync (proxy,
                                  "GetCurrentState",
                                  NULL,
                                  G_DBUS_CALL_FLAGS_NONE,
                                  5000,
                                  NULL,
                                  error);
  if (!reply)
    return FALSE;

  config = g_new0 (OozeDisplayConfig, 1);
  config->layout_mode = 1; /* logical by default */
  config->monitors = g_ptr_array_new_with_free_func ((GDestroyNotify) ooze_display_monitor_free);

  g_variant_get (reply, "(u@" MONITORS_FORMAT "@" LMS_FORMAT "@a{sv})",
                 &config->serial,
                 &monitors,
                 &logical_monitors,
                 &global_props);

  if (global_props)
    {
      GVariantIter piter;
      const char *key;
      GVariant *val;

      g_variant_iter_init (&piter, global_props);
      while (g_variant_iter_loop (&piter, "{&sv}", &key, &val))
        {
          if (g_strcmp0 (key, "layout-mode") == 0)
            config->layout_mode = g_variant_get_uint32 (val);
          else if (g_strcmp0 (key, "supports-changing-layout-mode") == 0)
            config->supports_changing_layout_mode = g_variant_get_boolean (val);
          else if (g_strcmp0 (key, "global-scale-required") == 0)
            config->global_scale_required = g_variant_get_boolean (val);
        }
    }

  g_variant_iter_init (&logical_iter, logical_monitors);
  while ((logical_tuple = g_variant_iter_next_value (&logical_iter)))
    {
      OozeDisplayMonitor *monitor = g_new0 (OozeDisplayMonitor, 1);
      g_autoptr (GVariant) lm_monitors = NULL;
      GVariantIter lm_mon_iter;
      GVariant *lm_mon_tuple;
      gboolean primary = FALSE;

      g_variant_get (logical_tuple, "(iidub@a(ssss)@a{sv})",
                     &monitor->layout_x,
                     &monitor->layout_y,
                     &monitor->scale,
                     &monitor->transform,
                     &primary,
                     &lm_monitors,
                     NULL);
      monitor->primary = primary;
      monitor->modes = g_ptr_array_new_with_free_func ((GDestroyNotify) ooze_display_mode_free);

      g_variant_iter_init (&lm_mon_iter, lm_monitors);
      if ((lm_mon_tuple = g_variant_iter_next_value (&lm_mon_iter)))
        {
          const char *connector = NULL;

          g_variant_get (lm_mon_tuple, "(&s&s&s&s)", &connector, NULL, NULL, NULL);
          monitor->connector = g_strdup (connector);
          g_variant_unref (lm_mon_tuple);
        }

      if (!monitor->connector)
        {
          ooze_display_monitor_free (monitor);
          g_variant_unref (logical_tuple);
          continue;
        }

      if (!ooze_display_fill_monitor_modes (monitors, monitor))
        monitor->display_name = g_strdup (monitor->connector);

      if (!monitor->current_mode_id && monitor->modes->len > 0)
        {
          guint i;

          for (i = 0; i < monitor->modes->len; i++)
            {
              OozeDisplayMode *mode = monitor->modes->pdata[i];

              if (mode->is_current || mode->is_preferred)
                {
                  monitor->current_mode_id = g_strdup (mode->id);
                  break;
                }
            }
          if (!monitor->current_mode_id)
            {
              OozeDisplayMode *first = monitor->modes->pdata[0];

              monitor->current_mode_id = g_strdup (first->id);
            }
        }

      g_ptr_array_add (config->monitors, monitor);
      g_variant_unref (logical_tuple);
    }

  if (config->monitors->len == 0)
    {
      ooze_display_config_free (config);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No logical monitor found");
      return FALSE;
    }

  *config_out = config;
  return TRUE;
}

gboolean
ooze_display_config_apply (const OozeDisplayConfig *config,
                           guint                    method,
                           GError                 **error)
{
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GVariant) reply = NULL;
  GVariantBuilder lm_builder;
  GVariantBuilder props_builder;
  g_autoptr (GVariant) params = NULL;
  guint i;

  g_return_val_if_fail (config != NULL, FALSE);
  g_return_val_if_fail (config->monitors != NULL, FALSE);
  g_return_val_if_fail (config->monitors->len > 0, FALSE);
  g_return_val_if_fail (method == OOZE_DISPLAY_APPLY_TEMPORARY ||
                        method == OOZE_DISPLAY_APPLY_PERSISTENT, FALSE);

  for (i = 0; i < config->monitors->len; i++)
    {
      OozeDisplayMonitor *m = config->monitors->pdata[i];

      if (!m->connector || !m->current_mode_id || !*m->current_mode_id)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Monitor %u is missing connector or mode", i);
          return FALSE;
        }
    }

  proxy = ooze_display_config_proxy (error);
  if (!proxy)
    return FALSE;

  g_variant_builder_init (&lm_builder, G_VARIANT_TYPE ("a(iiduba(ssa{sv}))"));

  for (i = 0; i < config->monitors->len; i++)
    {
      OozeDisplayMonitor *m = config->monitors->pdata[i];
      GVariantBuilder mon_builder;

      g_variant_builder_init (&mon_builder, G_VARIANT_TYPE ("a(ssa{sv})"));
      g_variant_builder_open (&mon_builder, G_VARIANT_TYPE ("(ssa{sv})"));
      g_variant_builder_add (&mon_builder, "s", m->connector);
      g_variant_builder_add (&mon_builder, "s", m->current_mode_id);
      g_variant_builder_open (&mon_builder, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_close (&mon_builder);
      g_variant_builder_close (&mon_builder);

      g_variant_builder_open (&lm_builder, G_VARIANT_TYPE ("(iiduba(ssa{sv}))"));
      g_variant_builder_add (&lm_builder, "i", m->layout_x);
      g_variant_builder_add (&lm_builder, "i", m->layout_y);
      g_variant_builder_add (&lm_builder, "d", m->scale);
      g_variant_builder_add (&lm_builder, "u", m->transform);
      g_variant_builder_add (&lm_builder, "b", m->primary);
      g_variant_builder_add_value (&lm_builder, g_variant_builder_end (&mon_builder));
      g_variant_builder_close (&lm_builder);
    }

  g_variant_builder_init (&props_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&props_builder, "{sv}",
                         "layout-mode",
                         g_variant_new_uint32 (config->layout_mode));

  params = g_variant_new ("(uu@a(iiduba(ssa{sv}))@a{sv})",
                          config->serial,
                          method,
                          g_variant_builder_end (&lm_builder),
                          g_variant_builder_end (&props_builder));

  reply = g_dbus_proxy_call_sync (proxy,
                                  "ApplyMonitorsConfig",
                                  params,
                                  G_DBUS_CALL_FLAGS_NONE,
                                  5000,
                                  NULL,
                                  error);
  return reply != NULL;
}
