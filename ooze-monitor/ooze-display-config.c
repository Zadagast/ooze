#include "ooze-display-config.h"

#include <gio/gio.h>

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
#define STATE_FORMAT     "(u" MONITORS_FORMAT LMS_FORMAT "a{sv})"

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

void
ooze_display_state_free (OozeDisplayState *state)
{
  if (!state)
    return;

  g_free (state->connector);
  g_free (state->display_name);
  g_free (state->current_mode_id);

  if (state->modes)
    g_ptr_array_free (state->modes, TRUE);

  g_free (state);
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
ooze_display_config_is_nest_dummy (const OozeDisplayState *state)
{
  return state && state->connector &&
         g_str_has_prefix (state->connector, "Meta-");
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
ooze_display_find_monitor (GVariant   *monitors_variant,
                           const char *connector,
                           char      **display_name_out,
                           GPtrArray **modes_out,
                           char      **current_mode_out)
{
  GVariantIter monitors_iter;
  GVariant *monitor_tuple;

  g_clear_pointer (display_name_out, g_free);
  g_clear_pointer (modes_out, g_ptr_array_unref);
  g_clear_pointer (current_mode_out, g_free);

  if (!monitors_variant || !connector)
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

      g_variant_get (monitor_tuple, "((&s&s&s&s)@" MODES_FORMAT "@a{sv})",
                     &mon_connector, &vendor, &product, &serial,
                     &modes_v, &props);

      if (g_strcmp0 (mon_connector, connector) != 0)
        {
          g_variant_unref (monitor_tuple);
          continue;
        }

      /* parse display-name from monitor props */
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
                  g_free (*display_name_out);
                  *display_name_out = g_variant_dup_string (val, NULL);
                }
            }
        }

      if (!*display_name_out || !**display_name_out)
        {
          g_free (*display_name_out);
          *display_name_out = (product && *product)
            ? g_strdup (product)
            : g_strdup (mon_connector);
        }

      if (modes_out)
        {
          guint i;

          *modes_out = ooze_display_parse_modes (modes_v);

          for (i = 0; i < (*modes_out)->len; i++)
            {
              OozeDisplayMode *mode = (*modes_out)->pdata[i];

              if (mode->is_current)
                {
                  *current_mode_out = g_strdup (mode->id);
                  break;
                }
            }
        }

      g_variant_unref (monitor_tuple);
      return TRUE;
    }

  return FALSE;
}

gboolean
ooze_display_config_load (OozeDisplayState **state_out,
                          GError           **error)
{
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GVariant) monitors = NULL;
  g_autoptr (GVariant) logical_monitors = NULL;
  g_autoptr (GVariant) global_props = NULL;
  OozeDisplayState *state;
  GVariantIter logical_iter;
  GVariant *logical_tuple;
  gboolean found = FALSE;

  g_return_val_if_fail (state_out != NULL, FALSE);
  *state_out = NULL;

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

  state = g_new0 (OozeDisplayState, 1);
  state->layout_mode = 1; /* logical by default */
  state->modes = g_ptr_array_new_with_free_func ((GDestroyNotify) ooze_display_mode_free);

  g_variant_get (reply, "(u@" MONITORS_FORMAT "@" LMS_FORMAT "@a{sv})",
                 &state->serial,
                 &monitors,
                 &logical_monitors,
                 &global_props);

  /* Parse global properties: layout-mode, supports-changing-layout-mode, global-scale-required */
  if (global_props)
    {
      GVariantIter piter;
      const char *key;
      GVariant *val;

      g_variant_iter_init (&piter, global_props);
      while (g_variant_iter_loop (&piter, "{&sv}", &key, &val))
        {
          if (g_strcmp0 (key, "layout-mode") == 0)
            state->layout_mode = g_variant_get_uint32 (val);
          else if (g_strcmp0 (key, "supports-changing-layout-mode") == 0)
            state->supports_changing_layout_mode = g_variant_get_boolean (val);
          else if (g_strcmp0 (key, "global-scale-required") == 0)
            state->global_scale_required = g_variant_get_boolean (val);
        }
    }

  /* Walk logical monitors; pick primary, or first if none primary */
  g_variant_iter_init (&logical_iter, logical_monitors);
  while ((logical_tuple = g_variant_iter_next_value (&logical_iter)))
    {
      g_autoptr (GVariant) lm_monitors = NULL;
      GVariantIter lm_mon_iter;
      GVariant *lm_mon_tuple;
      gboolean primary = FALSE;

      g_variant_get (logical_tuple, "(iidub@a(ssss)@a{sv})",
                     &state->layout_x,
                     &state->layout_y,
                     &state->scale,
                     &state->transform,
                     &primary,
                     &lm_monitors,
                     NULL);

      if (!primary && found)
        {
          g_variant_unref (logical_tuple);
          continue;
        }

      state->primary = primary;

      g_variant_iter_init (&lm_mon_iter, lm_monitors);
      if ((lm_mon_tuple = g_variant_iter_next_value (&lm_mon_iter)))
        {
          const char *connector = NULL;

          g_variant_get (lm_mon_tuple, "(&s&s&s&s)", &connector, NULL, NULL, NULL);
          g_free (state->connector);
          state->connector = g_strdup (connector);
          g_variant_unref (lm_mon_tuple);
        }

      found = TRUE;
      g_variant_unref (logical_tuple);
      if (state->primary)
        break;
    }

  if (!found || !state->connector)
    {
      ooze_display_state_free (state);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No logical monitor found");
      return FALSE;
    }

  if (!ooze_display_find_monitor (monitors,
                                  state->connector,
                                  &state->display_name,
                                  &state->modes,
                                  &state->current_mode_id))
    {
      state->display_name = g_strdup (state->connector);
    }

  if (!state->current_mode_id && state->modes->len > 0)
    {
      guint i;

      for (i = 0; i < state->modes->len; i++)
        {
          OozeDisplayMode *mode = state->modes->pdata[i];

          if (mode->is_current || mode->is_preferred)
            {
              state->current_mode_id = g_strdup (mode->id);
              break;
            }
        }
      if (!state->current_mode_id)
        state->current_mode_id = g_strdup (((OozeDisplayMode *) state->modes->pdata[0])->id);
    }

  *state_out = state;
  return TRUE;
}

gboolean
ooze_display_config_apply (const OozeDisplayState *state,
                           const char             *mode_id,
                           double                  scale,
                           guint                   transform,
                           GError                **error)
{
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GVariant) reply = NULL;
  guint method;

  g_return_val_if_fail (state != NULL, FALSE);
  g_return_val_if_fail (mode_id != NULL && *mode_id, FALSE);

  proxy = ooze_display_config_proxy (error);
  if (!proxy)
    return FALSE;

  /* Try persistent (2) first, fall back to temporary (1) — same as GNOME Settings does
   * (it only does persistent, but we fall back in case policy blocks persistence). */
  for (method = 2; method >= 1; method--)
    {
      GVariantBuilder mon_builder;
      GVariantBuilder lm_builder;
      GVariantBuilder props_builder;
      g_autoptr (GVariant) params = NULL;
      g_autoptr (GError) call_error = NULL;

      /* Per-monitor: connector + mode id + empty props */
      g_variant_builder_init (&mon_builder, G_VARIANT_TYPE ("a(ssa{sv})"));
      g_variant_builder_open (&mon_builder, G_VARIANT_TYPE ("(ssa{sv})"));
      g_variant_builder_add (&mon_builder, "s", state->connector);
      g_variant_builder_add (&mon_builder, "s", mode_id);
      g_variant_builder_open (&mon_builder, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_close (&mon_builder);
      g_variant_builder_close (&mon_builder);

      /* Logical monitor */
      g_variant_builder_init (&lm_builder, G_VARIANT_TYPE ("a(iiduba(ssa{sv}))"));
      g_variant_builder_open (&lm_builder, G_VARIANT_TYPE ("(iiduba(ssa{sv}))"));
      g_variant_builder_add (&lm_builder, "i", state->layout_x);
      g_variant_builder_add (&lm_builder, "i", state->layout_y);
      g_variant_builder_add (&lm_builder, "d", scale);
      g_variant_builder_add (&lm_builder, "u", transform);
      g_variant_builder_add (&lm_builder, "b", state->primary);
      g_variant_builder_add_value (&lm_builder,
                                   g_variant_builder_end (&mon_builder));
      g_variant_builder_close (&lm_builder);

      /* Global properties — always include layout-mode (echoed from GetCurrentState) */
      g_variant_builder_init (&props_builder, G_VARIANT_TYPE ("a{sv}"));
      g_variant_builder_add (&props_builder, "{sv}",
                             "layout-mode",
                             g_variant_new_uint32 (state->layout_mode));

      params = g_variant_new ("(uu@a(iiduba(ssa{sv}))@a{sv})",
                              state->serial,
                              (guint) method,
                              g_variant_builder_end (&lm_builder),
                              g_variant_builder_end (&props_builder));

      reply = g_dbus_proxy_call_sync (proxy,
                                      "ApplyMonitorsConfig",
                                      params,
                                      G_DBUS_CALL_FLAGS_NONE,
                                      5000,
                                      NULL,
                                      &call_error);
      if (reply)
        return TRUE;

      if (method == 1 && call_error)
        g_propagate_error (error, g_steal_pointer (&call_error));
    }

  return FALSE;
}
