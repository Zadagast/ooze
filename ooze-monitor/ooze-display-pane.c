#include "ooze-display-pane.h"
#include "ooze-display-config.h"

#include "ooze-scroll.h"
#include "ooze-surface.h"

#include <adwaita.h>
#include <math.h>
#include <stdio.h>

/* Resolution group: a unique (width, height) pair and the indices into
 * state->modes that share it. */
typedef struct
{
  int    width;
  int    height;
  GArray *mode_indices; /* guint indices into state->modes */
} ResGroup;

static void
res_group_free (ResGroup *g)
{
  if (!g)
    return;
  if (g->mode_indices)
    g_array_unref (g->mode_indices);
  g_free (g);
}

struct _OozeDisplayPane
{
  GtkBox parent_instance;

  GtkWidget *status;
  GtkWidget *display_label;
  GtkWidget *resolution;
  GtkWidget *refresh_rate;
  GtkWidget *scale;
  GtkWidget *orientation;
  GtkWidget *apply_button;

  OozeDisplayState *state;
  gboolean          applying;
  gboolean          reload_pending;
  gboolean          filling;   /* suppress re-entrant notify::selected */
  guint             changed_signal;

  /* Index maps derived from state, rebuilt on every reload */
  GPtrArray *res_groups;     /* ResGroup*, ordered by resolution area desc */
  GArray    *refresh_indices; /* guint mode indices for current res group */
  GArray    *scale_values;    /* double — supported scales for current mode */
};

G_DEFINE_FINAL_TYPE (OozeDisplayPane, ooze_display_pane, GTK_TYPE_BOX)

static const struct
{
  guint       transform;
  const char *label;
} orientation_options[] = {
  { 0, "Landscape" },
  { 1, "Portrait (90\xc2\xb0)" },
  { 2, "Landscape (flipped)" },
  { 3, "Portrait (270\xc2\xb0)" },
};

/* ── helpers ──────────────────────────────────────────────────────────── */

static void
ooze_display_pane_set_status (OozeDisplayPane *self,
                              const char      *text)
{
  gtk_label_set_text (GTK_LABEL (self->status), text ? text : "");
}

static const char *
ooze_display_pane_nest_hint (OozeDisplayPane *self)
{
  if (!self->state || !ooze_display_config_is_nest_dummy (self->state))
    return NULL;

  if (self->state->modes->len <= 1)
    return "Nest devkit: only one resolution available. "
           "Restart with OOZE_DISPLAY_MODES=1280x720:1920x1080 "
           "./run-devkit.sh to add more.";

  return "Nest devkit: enable Emulate monitor modes in the "
         "devkit viewer \xe2\x98\xb0 menu for resolution changes to resize the window.";
}

/* ── sort comparator for resolution groups ────────────────────────────── */

static int
res_group_compare_desc (gconstpointer a,
                        gconstpointer b)
{
  const ResGroup *ra = *(const ResGroup * const *) a;
  const ResGroup *rb = *(const ResGroup * const *) b;
  int pa = ra->width * ra->height;
  int pb = rb->width * rb->height;

  /* descending: larger area first */
  return (pa < pb) - (pa > pb);
}

/* ── index map builders ───────────────────────────────────────────────── */

static void
ooze_display_pane_build_res_groups (OozeDisplayPane *self)
{
  guint i;

  if (self->res_groups)
    g_ptr_array_free (self->res_groups, TRUE);
  self->res_groups = g_ptr_array_new_with_free_func ((GDestroyNotify) res_group_free);

  if (!self->state)
    return;

  for (i = 0; i < self->state->modes->len; i++)
    {
      OozeDisplayMode *mode = self->state->modes->pdata[i];
      gboolean found = FALSE;
      guint g;

      for (g = 0; g < self->res_groups->len; g++)
        {
          ResGroup *rg = self->res_groups->pdata[g];

          if (rg->width == mode->width && rg->height == mode->height)
            {
              g_array_append_val (rg->mode_indices, i);
              found = TRUE;
              break;
            }
        }

      if (!found)
        {
          ResGroup *rg = g_new0 (ResGroup, 1);

          rg->width = mode->width;
          rg->height = mode->height;
          rg->mode_indices = g_array_new (FALSE, FALSE, sizeof (guint));
          g_array_append_val (rg->mode_indices, i);
          g_ptr_array_add (self->res_groups, rg);
        }
    }

  /* Sort descending by pixel count (largest first) */
  g_ptr_array_sort (self->res_groups, res_group_compare_desc);
}

/* ── forward declarations ─────────────────────────────────────────────── */

static void ooze_display_pane_fill_scale (OozeDisplayPane *self);
static void ooze_display_pane_fill_refresh_rate (OozeDisplayPane *self,
                                                 guint            res_idx);
static void ooze_display_pane_reload (OozeDisplayPane *self);

/* ── clear ────────────────────────────────────────────────────────────── */

static void
ooze_display_pane_clear_controls (OozeDisplayPane *self)
{
  const char *none[] = { "—", NULL };
  GtkStringList *empty;

  self->filling = TRUE;

  empty = gtk_string_list_new (none);
  gtk_drop_down_set_model (GTK_DROP_DOWN (self->resolution), G_LIST_MODEL (empty));
  gtk_drop_down_set_model (GTK_DROP_DOWN (self->refresh_rate), G_LIST_MODEL (empty));
  gtk_drop_down_set_model (GTK_DROP_DOWN (self->scale), G_LIST_MODEL (empty));
  gtk_drop_down_set_model (GTK_DROP_DOWN (self->orientation), G_LIST_MODEL (empty));
  g_object_unref (empty);

  gtk_widget_set_sensitive (self->resolution, FALSE);
  gtk_widget_set_sensitive (self->refresh_rate, FALSE);
  gtk_widget_set_sensitive (self->scale, FALSE);
  gtk_widget_set_sensitive (self->orientation, FALSE);
  gtk_widget_set_sensitive (self->apply_button, FALSE);
  gtk_label_set_text (GTK_LABEL (self->display_label), "No display");

  self->filling = FALSE;
}

/* ── scale fill ───────────────────────────────────────────────────────── */

static void
ooze_display_pane_fill_scale (OozeDisplayPane *self)
{
  GtkStringList *list;
  guint ref_idx;
  guint i;
  guint select = 0;
  OozeDisplayMode *mode = NULL;

  if (!self->state || !self->refresh_indices || self->refresh_indices->len == 0)
    return;

  ref_idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->refresh_rate));
  if (ref_idx >= self->refresh_indices->len)
    ref_idx = 0;

  {
    guint mode_idx = g_array_index (self->refresh_indices, guint, ref_idx);

    if (mode_idx < self->state->modes->len)
      mode = self->state->modes->pdata[mode_idx];
  }

  if (self->scale_values)
    g_array_unref (self->scale_values);
  self->scale_values = g_array_new (FALSE, FALSE, sizeof (double));

  if (!mode || !mode->supported_scales || mode->supported_scales->len == 0)
    {
      /* Fallback: expose only 1× if no scale info */
      double one = 1.0;

      g_array_append_val (self->scale_values, one);
    }
  else
    {
      /* Copy supported scales (already sorted ascending by mutter) */
      for (i = 0; i < mode->supported_scales->len; i++)
        {
          double s = g_array_index (mode->supported_scales, double, i);

          g_array_append_val (self->scale_values, s);
        }
    }

  {
    const char **items = g_new0 (const char *, self->scale_values->len + 1);
    char **strs = g_new0 (char *, self->scale_values->len);
    double best_diff = G_MAXDOUBLE;
    double target = self->state->scale;

    for (i = 0; i < self->scale_values->len; i++)
      {
        double s = g_array_index (self->scale_values, double, i);
        double diff = fabs (s - target);

        strs[i] = g_strdup_printf ("%.0f%%", s * 100.0);
        items[i] = strs[i];

        if (diff < best_diff)
          {
            best_diff = diff;
            select = i;
          }
      }

    list = gtk_string_list_new (items);

    for (i = 0; i < self->scale_values->len; i++)
      g_free (strs[i]);
    g_free (strs);
    g_free (items);
  }

  self->filling = TRUE;
  gtk_drop_down_set_model (GTK_DROP_DOWN (self->scale), G_LIST_MODEL (list));
  gtk_drop_down_set_selected (GTK_DROP_DOWN (self->scale), select);
  gtk_widget_set_sensitive (self->scale, self->scale_values->len > 1);
  g_object_unref (list);
  self->filling = FALSE;
}

/* ── refresh rate fill ────────────────────────────────────────────────── */

static void
ooze_display_pane_fill_refresh_rate (OozeDisplayPane *self,
                                     guint            res_idx)
{
  ResGroup *rg;
  GPtrArray *labels;
  GtkStringList *list;
  guint i;
  guint select = 0;
  const char **items;

  if (!self->state || !self->res_groups ||
      res_idx >= self->res_groups->len)
    return;

  rg = self->res_groups->pdata[res_idx];

  if (self->refresh_indices)
    g_array_unref (self->refresh_indices);
  self->refresh_indices = g_array_new (FALSE, FALSE, sizeof (guint));

  labels = g_ptr_array_new_with_free_func (g_free);

  for (i = 0; i < rg->mode_indices->len; i++)
    {
      guint mode_idx = g_array_index (rg->mode_indices, guint, i);
      OozeDisplayMode *mode = self->state->modes->pdata[mode_idx];
      char *label;

      /* e.g. "60 Hz" or "59.94 Hz" */
      if (fabs (mode->refresh - round (mode->refresh)) < 0.01)
        label = g_strdup_printf ("%.0f Hz", mode->refresh);
      else
        label = g_strdup_printf ("%.2f Hz", mode->refresh);

      g_ptr_array_add (labels, label);
      g_array_append_val (self->refresh_indices, mode_idx);

      if (g_strcmp0 (mode->id, self->state->current_mode_id) == 0)
        select = i;
    }

  items = g_new0 (const char *, labels->len + 1);
  for (i = 0; i < labels->len; i++)
    items[i] = labels->pdata[i];

  list = gtk_string_list_new (items);
  g_free (items);
  g_ptr_array_free (labels, TRUE);

  self->filling = TRUE;
  gtk_drop_down_set_model (GTK_DROP_DOWN (self->refresh_rate), G_LIST_MODEL (list));
  gtk_drop_down_set_selected (GTK_DROP_DOWN (self->refresh_rate), select);
  gtk_widget_set_sensitive (self->refresh_rate, rg->mode_indices->len > 1);
  g_object_unref (list);
  self->filling = FALSE;

  ooze_display_pane_fill_scale (self);
}

/* ── resolution fill ──────────────────────────────────────────────────── */

static void
ooze_display_pane_fill_resolution (OozeDisplayPane *self)
{
  GPtrArray *labels;
  GtkStringList *list;
  guint i;
  guint select = 0;
  const char **items;

  if (!self->state || !self->res_groups)
    return;

  labels = g_ptr_array_new_with_free_func (g_free);

  for (i = 0; i < self->res_groups->len; i++)
    {
      ResGroup *rg = self->res_groups->pdata[i];
      char *label = g_strdup_printf ("%d \xc3\x97 %d", rg->width, rg->height);

      g_ptr_array_add (labels, label);

      /* Is the current mode in this group? */
      if (self->state->current_mode_id)
        {
          guint j;

          for (j = 0; j < rg->mode_indices->len; j++)
            {
              guint mode_idx = g_array_index (rg->mode_indices, guint, j);
              OozeDisplayMode *mode = self->state->modes->pdata[mode_idx];

              if (g_strcmp0 (mode->id, self->state->current_mode_id) == 0)
                {
                  select = i;
                  break;
                }
            }
        }
    }

  if (labels->len == 0)
    g_ptr_array_add (labels, g_strdup ("\xe2\x80\x94"));

  items = g_new0 (const char *, labels->len + 1);
  for (i = 0; i < labels->len; i++)
    items[i] = labels->pdata[i];

  list = gtk_string_list_new (items);
  g_free (items);
  g_ptr_array_free (labels, TRUE);

  self->filling = TRUE;
  gtk_drop_down_set_model (GTK_DROP_DOWN (self->resolution), G_LIST_MODEL (list));
  gtk_drop_down_set_selected (GTK_DROP_DOWN (self->resolution), select);
  gtk_widget_set_sensitive (self->resolution, self->res_groups->len > 1);
  g_object_unref (list);
  self->filling = FALSE;

  ooze_display_pane_fill_refresh_rate (self, select);
}

/* ── orientation fill ─────────────────────────────────────────────────── */

static void
ooze_display_pane_fill_orientation (OozeDisplayPane *self)
{
  const char *items[G_N_ELEMENTS (orientation_options) + 1];
  GtkStringList *list;
  guint i;
  guint select = 0;

  for (i = 0; i < G_N_ELEMENTS (orientation_options); i++)
    {
      items[i] = orientation_options[i].label;
      if (orientation_options[i].transform == self->state->transform)
        select = i;
    }
  items[G_N_ELEMENTS (orientation_options)] = NULL;

  list = gtk_string_list_new (items);

  self->filling = TRUE;
  gtk_drop_down_set_model (GTK_DROP_DOWN (self->orientation), G_LIST_MODEL (list));
  gtk_drop_down_set_selected (GTK_DROP_DOWN (self->orientation), select);
  g_object_unref (list);
  self->filling = FALSE;
}

/* ── idle reload ──────────────────────────────────────────────────────── */

static gboolean
ooze_display_pane_reload_idle (gpointer user_data)
{
  OozeDisplayPane *self = OOZE_DISPLAY_PANE (user_data);

  self->reload_pending = FALSE;
  ooze_display_pane_reload (self);
  return G_SOURCE_REMOVE;
}

static void
ooze_display_pane_schedule_reload (OozeDisplayPane *self)
{
  if (self->reload_pending)
    return;

  self->reload_pending = TRUE;
  g_idle_add (ooze_display_pane_reload_idle, self);
}

/* ── reload ───────────────────────────────────────────────────────────── */

static void
ooze_display_pane_reload (OozeDisplayPane *self)
{
  g_autoptr (GError) error = NULL;
  const char *hint;

  ooze_display_state_free (self->state);
  self->state = NULL;

  if (!ooze_display_config_allowed ())
    {
      ooze_display_pane_clear_controls (self);
      ooze_display_pane_set_status (self,
                                    "Display changes are disabled by policy.");
      return;
    }

  if (!ooze_display_config_load (&self->state, &error))
    {
      ooze_display_pane_clear_controls (self);
      ooze_display_pane_set_status (self,
                                    error ? error->message :
                                    "Could not read display configuration.");
      return;
    }

  gtk_label_set_text (GTK_LABEL (self->display_label),
                      self->state->display_name && *self->state->display_name
                        ? self->state->display_name
                        : self->state->connector);

  ooze_display_pane_build_res_groups (self);
  ooze_display_pane_fill_resolution (self);
  ooze_display_pane_fill_orientation (self);

  gtk_widget_set_sensitive (self->orientation, TRUE);
  gtk_widget_set_sensitive (self->apply_button, TRUE);

  ooze_display_pane_set_status (self, NULL);

  hint = ooze_display_pane_nest_hint (self);
  if (hint)
    ooze_display_pane_set_status (self, hint);
}

/* ── selection readers ────────────────────────────────────────────────── */

static const char *
ooze_display_pane_selected_mode_id (OozeDisplayPane *self)
{
  guint ref_idx;
  guint mode_idx;

  if (!self->state || !self->refresh_indices ||
      self->refresh_indices->len == 0)
    return NULL;

  ref_idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->refresh_rate));
  if (ref_idx >= self->refresh_indices->len)
    ref_idx = 0;

  mode_idx = g_array_index (self->refresh_indices, guint, ref_idx);
  if (mode_idx >= self->state->modes->len)
    return NULL;

  return ((OozeDisplayMode *) self->state->modes->pdata[mode_idx])->id;
}

static double
ooze_display_pane_selected_scale (OozeDisplayPane *self)
{
  guint idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->scale));

  if (!self->scale_values || idx >= self->scale_values->len)
    return 1.0;

  return g_array_index (self->scale_values, double, idx);
}

static guint
ooze_display_pane_selected_transform (OozeDisplayPane *self)
{
  guint idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->orientation));

  if (idx < G_N_ELEMENTS (orientation_options))
    return orientation_options[idx].transform;
  return 0;
}

/* ── apply ────────────────────────────────────────────────────────────── */

static void
ooze_display_pane_apply (OozeDisplayPane *self)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *prev_mode = NULL;
  const char *mode_id;
  const char *hint;
  double prev_scale;
  double scale;
  guint prev_transform;
  guint transform;

  if (!self->state || self->applying)
    return;

  mode_id = ooze_display_pane_selected_mode_id (self);
  if (!mode_id)
    return;

  prev_mode = g_strdup (self->state->current_mode_id);
  prev_scale = self->state->scale;
  prev_transform = self->state->transform;
  scale = ooze_display_pane_selected_scale (self);
  transform = ooze_display_pane_selected_transform (self);

  self->applying = TRUE;
  ooze_display_pane_set_status (self, "Applying\xe2\x80\xa6");

  if (!ooze_display_config_apply (self->state,
                                  mode_id,
                                  scale,
                                  transform,
                                  &error))
    {
      ooze_display_pane_set_status (self,
                                    error ? error->message :
                                    "Failed to apply display settings.");
      self->applying = FALSE;
      return;
    }

  ooze_display_pane_reload (self);
  self->applying = FALSE;

  if (self->state &&
      g_strcmp0 (self->state->current_mode_id, prev_mode) == 0 &&
      fabs (self->state->scale - prev_scale) < 0.01 &&
      self->state->transform == prev_transform)
    {
      ooze_display_pane_set_status (self,
                                    "Apply succeeded but compositor state did not change. "
                                    "In devkit, enable Emulate monitor modes in the viewer \xe2\x98\xb0 menu.");
      return;
    }

  hint = ooze_display_pane_nest_hint (self);
  if (hint)
    {
      g_autofree char *msg = g_strdup_printf ("Display updated. %s", hint);

      ooze_display_pane_set_status (self, msg);
    }
  else
    {
      ooze_display_pane_set_status (self, "Display updated.");
    }
}

/* ── signal handlers ──────────────────────────────────────────────────── */

static void
on_resolution_changed (GtkDropDown     *dd G_GNUC_UNUSED,
                       GParamSpec      *pspec G_GNUC_UNUSED,
                       OozeDisplayPane *self)
{
  guint res_idx;

  if (self->filling || !self->state)
    return;

  res_idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->resolution));
  ooze_display_pane_fill_refresh_rate (self, res_idx);
}

static void
on_refresh_changed (GtkDropDown     *dd G_GNUC_UNUSED,
                    GParamSpec      *pspec G_GNUC_UNUSED,
                    OozeDisplayPane *self)
{
  if (self->filling || !self->state)
    return;

  ooze_display_pane_fill_scale (self);
}

static void
on_apply_clicked (GtkButton       *btn G_GNUC_UNUSED,
                  OozeDisplayPane *self)
{
  ooze_display_pane_apply (self);
}

static void
on_monitors_changed (GDBusConnection *connection G_GNUC_UNUSED,
                     const char      *sender G_GNUC_UNUSED,
                     const char      *path G_GNUC_UNUSED,
                     const char      *iface G_GNUC_UNUSED,
                     const char      *signal G_GNUC_UNUSED,
                     GVariant        *params G_GNUC_UNUSED,
                     gpointer         user_data)
{
  OozeDisplayPane *self = OOZE_DISPLAY_PANE (user_data);

  if (self->applying)
    return;

  ooze_display_pane_schedule_reload (self);
}

/* ── layout helpers ───────────────────────────────────────────────────── */

static GtkWidget *
make_row (const char *title,
          GtkWidget  *control)
{
  GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *label = gtk_label_new (title);

  gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_box_append (GTK_BOX (row), label);
  gtk_box_append (GTK_BOX (row), control);
  return row;
}

/* ── GObject lifecycle ────────────────────────────────────────────────── */

static void
ooze_display_pane_dispose (GObject *object)
{
  OozeDisplayPane *self = OOZE_DISPLAY_PANE (object);
  g_autoptr (GDBusConnection) bus = NULL;

  if (self->changed_signal)
    {
      bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
      if (bus)
        g_dbus_connection_signal_unsubscribe (bus, self->changed_signal);
      self->changed_signal = 0;
    }

  ooze_display_state_free (self->state);
  self->state = NULL;

  if (self->res_groups)
    {
      g_ptr_array_free (self->res_groups, TRUE);
      self->res_groups = NULL;
    }
  if (self->refresh_indices)
    {
      g_array_unref (self->refresh_indices);
      self->refresh_indices = NULL;
    }
  if (self->scale_values)
    {
      g_array_unref (self->scale_values);
      self->scale_values = NULL;
    }

  G_OBJECT_CLASS (ooze_display_pane_parent_class)->dispose (object);
}

static void
ooze_display_pane_class_init (OozeDisplayPaneClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ooze_display_pane_dispose;
}

static void
ooze_display_pane_init (OozeDisplayPane *self)
{
  GtkWidget *scroll;
  GtkWidget *surface;
  GtkWidget *box;
  g_autoptr (GDBusConnection) bus = NULL;

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);

  scroll = ooze_scrolled_window_new ();
  gtk_widget_set_vexpand (scroll, TRUE);
  gtk_box_append (GTK_BOX (self), scroll);

  surface = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_start (surface, 16);
  gtk_widget_set_margin_end (surface, 16);
  gtk_widget_set_margin_top (surface, 16);
  gtk_widget_set_margin_bottom (surface, 16);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), surface);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
  gtk_box_append (GTK_BOX (surface), box);

  self->display_label = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (self->display_label), 0.0f);
  gtk_widget_add_css_class (self->display_label, "title-4");
  gtk_box_append (GTK_BOX (box), self->display_label);

  self->resolution = gtk_drop_down_new (NULL, NULL);
  g_signal_connect (self->resolution, "notify::selected",
                    G_CALLBACK (on_resolution_changed), self);
  gtk_box_append (GTK_BOX (box), make_row ("Resolution", self->resolution));

  self->refresh_rate = gtk_drop_down_new (NULL, NULL);
  g_signal_connect (self->refresh_rate, "notify::selected",
                    G_CALLBACK (on_refresh_changed), self);
  gtk_box_append (GTK_BOX (box), make_row ("Refresh Rate", self->refresh_rate));

  self->scale = gtk_drop_down_new (NULL, NULL);
  gtk_box_append (GTK_BOX (box), make_row ("Scale", self->scale));

  self->orientation = gtk_drop_down_new (NULL, NULL);
  gtk_box_append (GTK_BOX (box), make_row ("Orientation", self->orientation));

  self->apply_button = gtk_button_new_with_label ("Apply");
  gtk_widget_set_halign (self->apply_button, GTK_ALIGN_END);
  gtk_widget_add_css_class (self->apply_button, "suggested-action");
  g_signal_connect (self->apply_button, "clicked",
                    G_CALLBACK (on_apply_clicked), self);
  gtk_box_append (GTK_BOX (box), self->apply_button);

  self->status = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (self->status), 0.0f);
  gtk_label_set_wrap (GTK_LABEL (self->status), TRUE);
  gtk_widget_add_css_class (self->status, "dim-label");
  gtk_box_append (GTK_BOX (box), self->status);

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
  if (bus)
    {
      self->changed_signal =
        g_dbus_connection_signal_subscribe (bus,
                                            "org.gnome.Mutter.DisplayConfig",
                                            "org.gnome.Mutter.DisplayConfig",
                                            "MonitorsChanged",
                                            "/org/gnome/Mutter/DisplayConfig",
                                            NULL,
                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                            on_monitors_changed,
                                            self,
                                            NULL);
    }

  ooze_display_pane_reload (self);
}

GtkWidget *
ooze_display_pane_new (void)
{
  return g_object_new (OOZE_TYPE_DISPLAY_PANE, NULL);
}
