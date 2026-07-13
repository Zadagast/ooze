#include "ooze-display-pane.h"
#include "ooze-display-config.h"

#include "ooze-scroll.h"
#include "ooze-surface.h"

#include <adwaita.h>
#include <math.h>
#include <stdio.h>

#define LAYOUT_SNAP_PX     32
#define CONFIRM_SECONDS    20
#define LAYOUT_PAD         16.0

/* Resolution group: a unique (width, height) pair and indices into monitor->modes. */
typedef struct
{
  int    width;
  int    height;
  GArray *mode_indices; /* guint */
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
  GtkWidget *layout_area;
  GtkWidget *primary_button;
  GtkWidget *resolution;
  GtkWidget *refresh_rate;
  GtkWidget *scale;
  GtkWidget *orientation;
  GtkWidget *apply_button;

  OozeDisplayConfig *config;
  OozeDisplayConfig *revert_config; /* snapshot before temporary apply */
  guint              selected;
  gboolean           applying;
  gboolean           reload_pending;
  gboolean           filling;
  guint              changed_signal;

  /* Layout drag */
  gboolean  dragging;
  guint     drag_index;
  double    drag_grab_x; /* layout coords at press relative to monitor origin */
  double    drag_grab_y;
  double    view_scale;
  double    view_ox;
  double    view_oy;

  /* Confirm dialog */
  GtkWidget *confirm_dialog;
  GtkWidget *confirm_label;
  guint      confirm_timeout_id;
  int        confirm_remaining;

  GPtrArray *res_groups;
  GArray    *refresh_indices;
  GArray    *scale_values;
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

static void ooze_display_pane_fill_scale (OozeDisplayPane *self);
static void ooze_display_pane_fill_refresh_rate (OozeDisplayPane *self,
                                                 guint            res_idx);
static void ooze_display_pane_reload (OozeDisplayPane *self);
static void ooze_display_pane_queue_layout_draw (OozeDisplayPane *self);
static void ooze_display_pane_fill_selected (OozeDisplayPane *self);
static void ooze_display_pane_cancel_confirm (OozeDisplayPane *self,
                                              gboolean         revert);
static OozeDisplayMonitor *ooze_display_pane_selected_monitor (OozeDisplayPane *self);

/* ── helpers ──────────────────────────────────────────────────────────── */

static void
ooze_display_pane_set_status (OozeDisplayPane *self,
                              const char      *text)
{
  gtk_label_set_text (GTK_LABEL (self->status), text ? text : "");
}

static OozeDisplayMonitor *
ooze_display_pane_selected_monitor (OozeDisplayPane *self)
{
  if (!self->config || !self->config->monitors ||
      self->selected >= self->config->monitors->len)
    return NULL;

  return self->config->monitors->pdata[self->selected];
}

static const char *
ooze_display_pane_nest_hint (OozeDisplayPane *self)
{
  OozeDisplayMonitor *m = ooze_display_pane_selected_monitor (self);

  if (!m || !ooze_display_monitor_is_nest_dummy (m))
    return NULL;

  if (!m->modes || m->modes->len <= 1)
    return "Nest devkit: only one resolution available. "
           "Restart with OOZE_DISPLAY_MODES=1600x900:1920x1080 "
           "./run-devkit.sh to add more (prefer ≥1600px wide for side-tile).";

  return "Nest devkit: enable Emulate monitor modes in the "
         "devkit viewer \xe2\x98\xb0 menu for resolution changes to resize the window.";
}

static void
ooze_display_monitor_logical_size (const OozeDisplayMonitor *m,
                                   int                      *w_out,
                                   int                      *h_out)
{
  const OozeDisplayMode *mode = ooze_display_monitor_current_mode (m);
  int w = 1920;
  int h = 1080;
  double scale = (m && m->scale > 0.01) ? m->scale : 1.0;

  if (mode)
    {
      w = mode->width;
      h = mode->height;
    }

  /* Portrait transforms swap axes in logical layout space */
  if (m && (m->transform == 1 || m->transform == 3))
    {
      int tmp = w;

      w = h;
      h = tmp;
    }

  *w_out = MAX (1, (int) round ((double) w / scale));
  *h_out = MAX (1, (int) round ((double) h / scale));
}

static int
res_group_compare_desc (gconstpointer a,
                        gconstpointer b)
{
  const ResGroup *ra = *(const ResGroup * const *) a;
  const ResGroup *rb = *(const ResGroup * const *) b;
  int pa = ra->width * ra->height;
  int pb = rb->width * rb->height;

  return (pa < pb) - (pa > pb);
}

static void
ooze_display_pane_build_res_groups (OozeDisplayPane    *self,
                                    OozeDisplayMonitor *monitor)
{
  guint i;

  if (self->res_groups)
    g_ptr_array_free (self->res_groups, TRUE);
  self->res_groups = g_ptr_array_new_with_free_func ((GDestroyNotify) res_group_free);

  if (!monitor || !monitor->modes)
    return;

  for (i = 0; i < monitor->modes->len; i++)
    {
      OozeDisplayMode *mode = monitor->modes->pdata[i];
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

  g_ptr_array_sort (self->res_groups, res_group_compare_desc);
}

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
  gtk_widget_set_sensitive (self->primary_button, FALSE);
  gtk_label_set_text (GTK_LABEL (self->display_label), "No display");

  self->filling = FALSE;
  ooze_display_pane_queue_layout_draw (self);
}

/* ── scale / refresh / resolution / orientation (selected monitor) ───── */

static void
ooze_display_pane_fill_scale (OozeDisplayPane *self)
{
  OozeDisplayMonitor *monitor = ooze_display_pane_selected_monitor (self);
  GtkStringList *list;
  guint ref_idx;
  guint i;
  guint select = 0;
  OozeDisplayMode *mode = NULL;

  if (!monitor || !self->refresh_indices || self->refresh_indices->len == 0)
    return;

  ref_idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->refresh_rate));
  if (ref_idx >= self->refresh_indices->len)
    ref_idx = 0;

  {
    guint mode_idx = g_array_index (self->refresh_indices, guint, ref_idx);

    if (mode_idx < monitor->modes->len)
      mode = monitor->modes->pdata[mode_idx];
  }

  if (self->scale_values)
    g_array_unref (self->scale_values);
  self->scale_values = g_array_new (FALSE, FALSE, sizeof (double));

  if (!mode || !mode->supported_scales || mode->supported_scales->len == 0)
    {
      double one = 1.0;

      g_array_append_val (self->scale_values, one);
    }
  else
    {
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
    double target = monitor->scale;

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
  gtk_widget_set_sensitive (self->scale,
                            self->scale_values->len > 1 &&
                            !self->config->global_scale_required);
  if (self->config->global_scale_required)
    gtk_widget_set_sensitive (self->scale, self->scale_values->len > 1);
  g_object_unref (list);
  self->filling = FALSE;
}

static void
ooze_display_pane_fill_refresh_rate (OozeDisplayPane *self,
                                     guint            res_idx)
{
  OozeDisplayMonitor *monitor = ooze_display_pane_selected_monitor (self);
  ResGroup *rg;
  GPtrArray *labels;
  GtkStringList *list;
  guint i;
  guint select = 0;
  const char **items;

  if (!monitor || !self->res_groups || res_idx >= self->res_groups->len)
    return;

  rg = self->res_groups->pdata[res_idx];

  if (self->refresh_indices)
    g_array_unref (self->refresh_indices);
  self->refresh_indices = g_array_new (FALSE, FALSE, sizeof (guint));

  labels = g_ptr_array_new_with_free_func (g_free);

  for (i = 0; i < rg->mode_indices->len; i++)
    {
      guint mode_idx = g_array_index (rg->mode_indices, guint, i);
      OozeDisplayMode *mode = monitor->modes->pdata[mode_idx];
      char *label;

      if (fabs (mode->refresh - round (mode->refresh)) < 0.01)
        label = g_strdup_printf ("%.0f Hz", mode->refresh);
      else
        label = g_strdup_printf ("%.2f Hz", mode->refresh);

      g_ptr_array_add (labels, label);
      g_array_append_val (self->refresh_indices, mode_idx);

      if (g_strcmp0 (mode->id, monitor->current_mode_id) == 0)
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

static void
ooze_display_pane_fill_resolution (OozeDisplayPane *self)
{
  OozeDisplayMonitor *monitor = ooze_display_pane_selected_monitor (self);
  GPtrArray *labels;
  GtkStringList *list;
  guint i;
  guint select = 0;
  const char **items;

  if (!monitor || !self->res_groups)
    return;

  labels = g_ptr_array_new_with_free_func (g_free);

  for (i = 0; i < self->res_groups->len; i++)
    {
      ResGroup *rg = self->res_groups->pdata[i];
      char *label = g_strdup_printf ("%d \xc3\x97 %d", rg->width, rg->height);

      g_ptr_array_add (labels, label);

      if (monitor->current_mode_id)
        {
          guint j;

          for (j = 0; j < rg->mode_indices->len; j++)
            {
              guint mode_idx = g_array_index (rg->mode_indices, guint, j);
              OozeDisplayMode *mode = monitor->modes->pdata[mode_idx];

              if (g_strcmp0 (mode->id, monitor->current_mode_id) == 0)
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

static void
ooze_display_pane_fill_orientation (OozeDisplayPane *self)
{
  OozeDisplayMonitor *monitor = ooze_display_pane_selected_monitor (self);
  const char *items[G_N_ELEMENTS (orientation_options) + 1];
  GtkStringList *list;
  guint i;
  guint select = 0;

  if (!monitor)
    return;

  for (i = 0; i < G_N_ELEMENTS (orientation_options); i++)
    {
      items[i] = orientation_options[i].label;
      if (orientation_options[i].transform == monitor->transform)
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

static void
ooze_display_pane_fill_selected (OozeDisplayPane *self)
{
  OozeDisplayMonitor *monitor = ooze_display_pane_selected_monitor (self);
  const char *name;

  if (!monitor)
    {
      ooze_display_pane_clear_controls (self);
      return;
    }

  name = (monitor->display_name && *monitor->display_name)
           ? monitor->display_name
           : monitor->connector;
  gtk_label_set_text (GTK_LABEL (self->display_label), name);

  ooze_display_pane_build_res_groups (self, monitor);
  ooze_display_pane_fill_resolution (self);
  ooze_display_pane_fill_orientation (self);

  gtk_widget_set_sensitive (self->orientation, TRUE);
  gtk_widget_set_sensitive (self->apply_button, TRUE);
  gtk_widget_set_sensitive (self->primary_button, !monitor->primary);
  gtk_button_set_label (GTK_BUTTON (self->primary_button),
                        monitor->primary ? "Main Display" : "Set as Main Display");

  ooze_display_pane_queue_layout_draw (self);
}

/* ── layout canvas ────────────────────────────────────────────────────── */

static void
ooze_display_pane_layout_bounds (OozeDisplayPane *self,
                                 int             *min_x,
                                 int             *min_y,
                                 int             *max_x,
                                 int             *max_y)
{
  guint i;

  *min_x = *min_y = G_MAXINT;
  *max_x = *max_y = G_MININT;

  if (!self->config || !self->config->monitors)
    return;

  for (i = 0; i < self->config->monitors->len; i++)
    {
      OozeDisplayMonitor *m = self->config->monitors->pdata[i];
      int w, h;

      ooze_display_monitor_logical_size (m, &w, &h);
      *min_x = MIN (*min_x, m->layout_x);
      *min_y = MIN (*min_y, m->layout_y);
      *max_x = MAX (*max_x, m->layout_x + w);
      *max_y = MAX (*max_y, m->layout_y + h);
    }
}

static void
ooze_display_pane_update_view_transform (OozeDisplayPane *self,
                                         double           widget_w,
                                         double           widget_h)
{
  int min_x, min_y, max_x, max_y;
  double layout_w, layout_h;
  double sx, sy;

  ooze_display_pane_layout_bounds (self, &min_x, &min_y, &max_x, &max_y);
  layout_w = MAX (1.0, (double) (max_x - min_x));
  layout_h = MAX (1.0, (double) (max_y - min_y));

  sx = (widget_w - 2.0 * LAYOUT_PAD) / layout_w;
  sy = (widget_h - 2.0 * LAYOUT_PAD) / layout_h;
  self->view_scale = MIN (sx, sy);
  if (self->view_scale <= 0.0)
    self->view_scale = 0.05;

  self->view_ox = LAYOUT_PAD +
                  (widget_w - 2.0 * LAYOUT_PAD - layout_w * self->view_scale) * 0.5
                  - (double) min_x * self->view_scale;
  self->view_oy = LAYOUT_PAD +
                  (widget_h - 2.0 * LAYOUT_PAD - layout_h * self->view_scale) * 0.5
                  - (double) min_y * self->view_scale;
}

static void
ooze_display_pane_layout_to_widget (OozeDisplayPane *self,
                                    double           lx,
                                    double           ly,
                                    double          *wx,
                                    double          *wy)
{
  *wx = self->view_ox + lx * self->view_scale;
  *wy = self->view_oy + ly * self->view_scale;
}

static void
ooze_display_pane_widget_to_layout (OozeDisplayPane *self,
                                    double           wx,
                                    double           wy,
                                    double          *lx,
                                    double          *ly)
{
  *lx = (wx - self->view_ox) / self->view_scale;
  *ly = (wy - self->view_oy) / self->view_scale;
}

static int
ooze_display_pane_hit_test (OozeDisplayPane *self,
                            double           wx,
                            double           wy)
{
  guint i;

  if (!self->config || !self->config->monitors)
    return -1;

  /* Top-most / last drawn wins: iterate reverse */
  for (i = self->config->monitors->len; i > 0; i--)
    {
      OozeDisplayMonitor *m = self->config->monitors->pdata[i - 1];
      int w, h;
      double x0, y0, x1, y1;

      ooze_display_monitor_logical_size (m, &w, &h);
      ooze_display_pane_layout_to_widget (self, m->layout_x, m->layout_y, &x0, &y0);
      ooze_display_pane_layout_to_widget (self,
                                          m->layout_x + w,
                                          m->layout_y + h,
                                          &x1, &y1);
      if (wx >= x0 && wx <= x1 && wy >= y0 && wy <= y1)
        return (int) (i - 1);
    }

  return -1;
}

static void
ooze_display_pane_snap_monitor (OozeDisplayPane *self,
                                guint            index)
{
  OozeDisplayMonitor *m;
  int mw, mh;
  guint i;
  int best_dx = 0;
  int best_dy = 0;
  int best_abs_x = LAYOUT_SNAP_PX + 1;
  int best_abs_y = LAYOUT_SNAP_PX + 1;

  if (!self->config || index >= self->config->monitors->len)
    return;

  m = self->config->monitors->pdata[index];
  ooze_display_monitor_logical_size (m, &mw, &mh);

  for (i = 0; i < self->config->monitors->len; i++)
    {
      OozeDisplayMonitor *o;
      int ow, oh;
      int edges_x[4];
      int edges_y[4];
      int o_edges_x[4];
      int o_edges_y[4];
      int e, f;

      if (i == index)
        continue;

      o = self->config->monitors->pdata[i];
      ooze_display_monitor_logical_size (o, &ow, &oh);

      edges_x[0] = m->layout_x;
      edges_x[1] = m->layout_x + mw;
      edges_y[0] = m->layout_y;
      edges_y[1] = m->layout_y + mh;
      o_edges_x[0] = o->layout_x;
      o_edges_x[1] = o->layout_x + ow;
      o_edges_y[0] = o->layout_y;
      o_edges_y[1] = o->layout_y + oh;

      for (e = 0; e < 2; e++)
        for (f = 0; f < 2; f++)
          {
            int dx = o_edges_x[f] - edges_x[e];
            int dy = o_edges_y[f] - edges_y[e];

            if (ABS (dx) < best_abs_x)
              {
                best_abs_x = ABS (dx);
                best_dx = dx;
              }
            if (ABS (dy) < best_abs_y)
              {
                best_abs_y = ABS (dy);
                best_dy = dy;
              }
          }
    }

  if (best_abs_x <= LAYOUT_SNAP_PX)
    m->layout_x += best_dx;
  if (best_abs_y <= LAYOUT_SNAP_PX)
    m->layout_y += best_dy;
}

static void
ooze_display_pane_queue_layout_draw (OozeDisplayPane *self)
{
  if (self->layout_area)
    gtk_widget_queue_draw (self->layout_area);
}

static void
on_layout_draw (GtkDrawingArea *area,
                cairo_t        *cr,
                int             width,
                int             height,
                gpointer        user_data)
{
  OozeDisplayPane *self = OOZE_DISPLAY_PANE (user_data);
  guint i;

  cairo_set_source_rgb (cr, 0.92, 0.92, 0.93);
  cairo_paint (cr);

  if (!self->config || !self->config->monitors || self->config->monitors->len == 0)
    {
      cairo_set_source_rgb (cr, 0.45, 0.45, 0.48);
      cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size (cr, 13.0);
      cairo_move_to (cr, 16.0, height * 0.5);
      cairo_show_text (cr, "No displays");
      return;
    }

  ooze_display_pane_update_view_transform (self, width, height);

  for (i = 0; i < self->config->monitors->len; i++)
    {
      OozeDisplayMonitor *m = self->config->monitors->pdata[i];
      int w, h;
      double x0, y0, x1, y1;
      double rw, rh;
      const char *label;
      cairo_text_extents_t ext;
      gboolean selected = (i == self->selected);

      ooze_display_monitor_logical_size (m, &w, &h);
      ooze_display_pane_layout_to_widget (self, m->layout_x, m->layout_y, &x0, &y0);
      ooze_display_pane_layout_to_widget (self,
                                          m->layout_x + w,
                                          m->layout_y + h,
                                          &x1, &y1);
      rw = x1 - x0;
      rh = y1 - y0;

      if (selected)
        cairo_set_source_rgb (cr, 0.55, 0.72, 0.95);
      else
        cairo_set_source_rgb (cr, 0.78, 0.80, 0.84);
      cairo_rectangle (cr, x0, y0, rw, rh);
      cairo_fill_preserve (cr);

      if (selected)
        cairo_set_source_rgb (cr, 0.20, 0.45, 0.85);
      else
        cairo_set_source_rgb (cr, 0.40, 0.42, 0.46);
      cairo_set_line_width (cr, selected ? 2.5 : 1.5);
      cairo_stroke (cr);

      label = (m->display_name && *m->display_name) ? m->display_name : m->connector;
      cairo_set_source_rgb (cr, 0.15, 0.15, 0.18);
      cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                              m->primary ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size (cr, 12.0);
      cairo_text_extents (cr, label, &ext);
      cairo_move_to (cr,
                     x0 + (rw - ext.width) * 0.5 - ext.x_bearing,
                     y0 + rh * 0.5 - (ext.height * 0.5 + ext.y_bearing));
      cairo_show_text (cr, label);

      if (m->primary)
        {
          const char *star = "Main";

          cairo_set_font_size (cr, 10.0);
          cairo_text_extents (cr, star, &ext);
          cairo_set_source_rgb (cr, 0.25, 0.35, 0.55);
          cairo_move_to (cr,
                         x0 + (rw - ext.width) * 0.5 - ext.x_bearing,
                         y0 + rh * 0.5 + 14.0);
          cairo_show_text (cr, star);
        }
    }

  (void) area;
}

static void
ooze_display_pane_select_index (OozeDisplayPane *self,
                                guint            index)
{
  if (!self->config || index >= self->config->monitors->len)
    return;
  if (self->selected == index)
    {
      ooze_display_pane_queue_layout_draw (self);
      return;
    }

  self->selected = index;
  ooze_display_pane_fill_selected (self);
}

static void
on_layout_pressed (GtkGestureClick *gesture,
                   int              n_press G_GNUC_UNUSED,
                   double           x,
                   double           y,
                   gpointer         user_data)
{
  OozeDisplayPane *self = OOZE_DISPLAY_PANE (user_data);
  int hit;
  OozeDisplayMonitor *m;
  double lx, ly;

  if (self->confirm_dialog || self->applying)
    return;

  hit = ooze_display_pane_hit_test (self, x, y);
  if (hit < 0)
    return;

  ooze_display_pane_select_index (self, (guint) hit);
  m = ooze_display_pane_selected_monitor (self);
  if (!m)
    return;

  ooze_display_pane_widget_to_layout (self, x, y, &lx, &ly);
  self->dragging = TRUE;
  self->drag_index = (guint) hit;
  self->drag_grab_x = lx - m->layout_x;
  self->drag_grab_y = ly - m->layout_y;
  gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
on_layout_released (GtkGestureClick *gesture G_GNUC_UNUSED,
                    int              n_press G_GNUC_UNUSED,
                    double           x G_GNUC_UNUSED,
                    double           y G_GNUC_UNUSED,
                    gpointer         user_data)
{
  OozeDisplayPane *self = OOZE_DISPLAY_PANE (user_data);

  if (!self->dragging)
    return;

  ooze_display_pane_snap_monitor (self, self->drag_index);
  self->dragging = FALSE;
  ooze_display_pane_queue_layout_draw (self);
}

static void
on_layout_motion (GtkEventControllerMotion *controller G_GNUC_UNUSED,
                  double                    x,
                  double                    y,
                  gpointer                  user_data)
{
  OozeDisplayPane *self = OOZE_DISPLAY_PANE (user_data);
  OozeDisplayMonitor *m;
  double lx, ly;

  if (!self->dragging)
    return;

  m = self->config->monitors->pdata[self->drag_index];
  ooze_display_pane_widget_to_layout (self, x, y, &lx, &ly);
  m->layout_x = (int) round (lx - self->drag_grab_x);
  m->layout_y = (int) round (ly - self->drag_grab_y);
  ooze_display_pane_queue_layout_draw (self);
}

/* ── confirm / apply / revert ─────────────────────────────────────────── */

static void
ooze_display_pane_stop_confirm_timer (OozeDisplayPane *self)
{
  if (self->confirm_timeout_id)
    {
      g_source_remove (self->confirm_timeout_id);
      self->confirm_timeout_id = 0;
    }
}

static void
ooze_display_pane_update_confirm_label (OozeDisplayPane *self)
{
  g_autofree char *text = NULL;

  if (!self->confirm_label)
    return;

  text = g_strdup_printf (
    "Do you want to keep these display settings?\n"
    "Reverting in %d seconds\xe2\x80\xa6",
    self->confirm_remaining);
  gtk_label_set_text (GTK_LABEL (self->confirm_label), text);
}

static gboolean
ooze_display_pane_confirm_tick (gpointer user_data)
{
  OozeDisplayPane *self = OOZE_DISPLAY_PANE (user_data);

  self->confirm_remaining--;
  if (self->confirm_remaining <= 0)
    {
      self->confirm_timeout_id = 0;
      ooze_display_pane_cancel_confirm (self, TRUE);
      return G_SOURCE_REMOVE;
    }

  ooze_display_pane_update_confirm_label (self);
  return G_SOURCE_CONTINUE;
}

static void
ooze_display_pane_close_confirm_dialog (OozeDisplayPane *self)
{
  ooze_display_pane_stop_confirm_timer (self);
  if (self->confirm_dialog)
    {
      gtk_window_destroy (GTK_WINDOW (self->confirm_dialog));
      self->confirm_dialog = NULL;
      self->confirm_label = NULL;
    }
}

static void
ooze_display_pane_cancel_confirm (OozeDisplayPane *self,
                                  gboolean         revert)
{
  g_autoptr (GError) error = NULL;

  ooze_display_pane_close_confirm_dialog (self);

  if (revert && self->revert_config)
    {
      /* Refresh serial from live state, then apply old layout/modes */
      OozeDisplayConfig *live = NULL;

      if (ooze_display_config_load (&live, NULL) && live)
        {
          self->revert_config->serial = live->serial;
          ooze_display_config_free (live);
        }

      self->applying = TRUE;
      if (!ooze_display_config_apply (self->revert_config,
                                      OOZE_DISPLAY_APPLY_TEMPORARY,
                                      &error))
        {
          ooze_display_pane_set_status (self,
                                        error ? error->message :
                                        "Failed to revert display settings.");
        }
      self->applying = FALSE;
    }

  g_clear_pointer (&self->revert_config, ooze_display_config_free);
  ooze_display_pane_reload (self);
  if (revert)
    ooze_display_pane_set_status (self, "Display settings reverted.");
}

static void
on_confirm_keep (GtkButton       *btn G_GNUC_UNUSED,
                 OozeDisplayPane *self)
{
  g_autoptr (GError) error = NULL;
  OozeDisplayConfig *live = NULL;

  ooze_display_pane_stop_confirm_timer (self);

  /* Persist current on-screen config (reload serial, keep edits from last apply) */
  if (!ooze_display_config_load (&live, &error) || !live)
    {
      ooze_display_pane_set_status (self,
                                    error ? error->message :
                                    "Could not read display configuration.");
      ooze_display_pane_close_confirm_dialog (self);
      g_clear_pointer (&self->revert_config, ooze_display_config_free);
      return;
    }

  self->applying = TRUE;
  if (!ooze_display_config_apply (live, OOZE_DISPLAY_APPLY_PERSISTENT, &error))
    {
      /* Persistence may be blocked; temporary already active — treat as kept */
      if (error)
        ooze_display_pane_set_status (self, error->message);
    }
  else
    {
      ooze_display_pane_set_status (self, "Display settings kept.");
    }
  self->applying = FALSE;

  ooze_display_config_free (live);
  ooze_display_pane_close_confirm_dialog (self);
  g_clear_pointer (&self->revert_config, ooze_display_config_free);
  ooze_display_pane_reload (self);
}

static void
on_confirm_revert (GtkButton       *btn G_GNUC_UNUSED,
                   OozeDisplayPane *self)
{
  ooze_display_pane_cancel_confirm (self, TRUE);
}

static void
ooze_display_pane_show_confirm (OozeDisplayPane *self)
{
  GtkWidget *win;
  GtkWidget *box;
  GtkWidget *buttons;
  GtkWidget *keep;
  GtkWidget *revert;
  GtkRoot *root;

  ooze_display_pane_close_confirm_dialog (self);

  root = gtk_widget_get_root (GTK_WIDGET (self));
  win = gtk_window_new ();
  gtk_window_set_title (GTK_WINDOW (win), "Keep Display Settings?");
  gtk_window_set_modal (GTK_WINDOW (win), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (win), FALSE);
  if (GTK_IS_WINDOW (root))
    gtk_window_set_transient_for (GTK_WINDOW (win), GTK_WINDOW (root));

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_margin_start (box, 20);
  gtk_widget_set_margin_end (box, 20);
  gtk_widget_set_margin_top (box, 20);
  gtk_widget_set_margin_bottom (box, 20);
  gtk_window_set_child (GTK_WINDOW (win), box);

  self->confirm_label = gtk_label_new ("");
  gtk_label_set_wrap (GTK_LABEL (self->confirm_label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (self->confirm_label), 0.0f);
  gtk_box_append (GTK_BOX (box), self->confirm_label);

  buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign (buttons, GTK_ALIGN_END);
  gtk_box_append (GTK_BOX (box), buttons);

  revert = gtk_button_new_with_label ("Revert");
  g_signal_connect (revert, "clicked", G_CALLBACK (on_confirm_revert), self);
  gtk_box_append (GTK_BOX (buttons), revert);

  keep = gtk_button_new_with_label ("Keep Changes");
  gtk_widget_add_css_class (keep, "suggested-action");
  g_signal_connect (keep, "clicked", G_CALLBACK (on_confirm_keep), self);
  gtk_box_append (GTK_BOX (buttons), keep);

  self->confirm_dialog = win;
  self->confirm_remaining = CONFIRM_SECONDS;
  ooze_display_pane_update_confirm_label (self);
  self->confirm_timeout_id =
    g_timeout_add_seconds (1, ooze_display_pane_confirm_tick, self);

  gtk_window_present (GTK_WINDOW (win));
}

static void
ooze_display_pane_sync_selected_into_config (OozeDisplayPane *self)
{
  OozeDisplayMonitor *monitor = ooze_display_pane_selected_monitor (self);
  guint ref_idx;
  guint mode_idx;
  guint scale_idx;
  guint orient_idx;
  guint i;

  if (!monitor || !self->refresh_indices || self->refresh_indices->len == 0)
    return;

  ref_idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->refresh_rate));
  if (ref_idx >= self->refresh_indices->len)
    ref_idx = 0;
  mode_idx = g_array_index (self->refresh_indices, guint, ref_idx);
  if (mode_idx < monitor->modes->len)
    {
      OozeDisplayMode *mode = monitor->modes->pdata[mode_idx];

      g_free (monitor->current_mode_id);
      monitor->current_mode_id = g_strdup (mode->id);
    }

  scale_idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->scale));
  if (self->scale_values && scale_idx < self->scale_values->len)
    {
      double scale = g_array_index (self->scale_values, double, scale_idx);

      if (self->config->global_scale_required)
        {
          for (i = 0; i < self->config->monitors->len; i++)
            {
              OozeDisplayMonitor *m = self->config->monitors->pdata[i];

              m->scale = scale;
            }
        }
      else
        {
          monitor->scale = scale;
        }
    }

  orient_idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->orientation));
  if (orient_idx < G_N_ELEMENTS (orientation_options))
    monitor->transform = orientation_options[orient_idx].transform;
}

static void
ooze_display_pane_apply (OozeDisplayPane *self)
{
  g_autoptr (GError) error = NULL;
  OozeDisplayConfig *live = NULL;

  if (!self->config || self->applying || self->confirm_dialog)
    return;

  ooze_display_pane_sync_selected_into_config (self);

  /* Mutter expects exactly one primary logical monitor. */
  {
    gboolean any_primary = FALSE;
    guint i;

    for (i = 0; i < self->config->monitors->len; i++)
      {
        OozeDisplayMonitor *m = self->config->monitors->pdata[i];

        if (m->primary)
          {
            any_primary = TRUE;
            break;
          }
      }
    if (!any_primary)
      ooze_display_config_set_primary (self->config, self->selected);
  }

  /* Snapshot currently applied Mutter state for Revert / timeout. */
  g_clear_pointer (&self->revert_config, ooze_display_config_free);
  if (!ooze_display_config_load (&live, &error) || !live)
    {
      ooze_display_pane_set_status (self,
                                    error ? error->message :
                                    "Could not read display configuration.");
      return;
    }
  self->revert_config = live;
  self->config->serial = live->serial;

  self->applying = TRUE;
  ooze_display_pane_set_status (self, "Applying\xe2\x80\xa6");

  if (!ooze_display_config_apply (self->config,
                                  OOZE_DISPLAY_APPLY_TEMPORARY,
                                  &error))
    {
      ooze_display_pane_set_status (self,
                                    error ? error->message :
                                    "Failed to apply display settings.");
      g_clear_pointer (&self->revert_config, ooze_display_config_free);
      self->applying = FALSE;
      return;
    }

  self->applying = FALSE;
  ooze_display_pane_show_confirm (self);
}

/* ── reload ───────────────────────────────────────────────────────────── */

static gboolean
ooze_display_pane_reload_idle (gpointer user_data)
{
  OozeDisplayPane *self = OOZE_DISPLAY_PANE (user_data);

  self->reload_pending = FALSE;
  if (self->confirm_dialog)
    return G_SOURCE_REMOVE;
  ooze_display_pane_reload (self);
  return G_SOURCE_REMOVE;
}

static void
ooze_display_pane_schedule_reload (OozeDisplayPane *self)
{
  if (self->reload_pending || self->confirm_dialog)
    return;

  self->reload_pending = TRUE;
  g_idle_add (ooze_display_pane_reload_idle, self);
}

static void
ooze_display_pane_reload (OozeDisplayPane *self)
{
  g_autoptr (GError) error = NULL;
  const char *hint;
  char *keep_connector = NULL;
  guint i;

  if (self->confirm_dialog)
    return;

  {
    OozeDisplayMonitor *prev = ooze_display_pane_selected_monitor (self);

    if (prev && prev->connector)
      keep_connector = g_strdup (prev->connector);
  }

  ooze_display_config_free (self->config);
  self->config = NULL;

  if (!ooze_display_config_allowed ())
    {
      g_free (keep_connector);
      ooze_display_pane_clear_controls (self);
      ooze_display_pane_set_status (self,
                                    "Display changes are disabled by policy.");
      return;
    }

  if (!ooze_display_config_load (&self->config, &error))
    {
      g_free (keep_connector);
      ooze_display_pane_clear_controls (self);
      ooze_display_pane_set_status (self,
                                    error ? error->message :
                                    "Could not read display configuration.");
      return;
    }

  self->selected = 0;
  if (keep_connector)
    {
      for (i = 0; i < self->config->monitors->len; i++)
        {
          OozeDisplayMonitor *m = self->config->monitors->pdata[i];

          if (g_strcmp0 (m->connector, keep_connector) == 0)
            {
              self->selected = i;
              break;
            }
        }
      g_free (keep_connector);
    }
  else
    {
      for (i = 0; i < self->config->monitors->len; i++)
        {
          OozeDisplayMonitor *m = self->config->monitors->pdata[i];

          if (m->primary)
            {
              self->selected = i;
              break;
            }
        }
    }

  ooze_display_pane_fill_selected (self);
  ooze_display_pane_set_status (self, NULL);

  hint = ooze_display_pane_nest_hint (self);
  if (hint)
    ooze_display_pane_set_status (self, hint);
}

/* ── signal handlers ──────────────────────────────────────────────────── */

static void
on_resolution_changed (GtkDropDown     *dd G_GNUC_UNUSED,
                       GParamSpec      *pspec G_GNUC_UNUSED,
                       OozeDisplayPane *self)
{
  guint res_idx;

  if (self->filling || !self->config)
    return;

  res_idx = gtk_drop_down_get_selected (GTK_DROP_DOWN (self->resolution));
  ooze_display_pane_fill_refresh_rate (self, res_idx);
}

static void
on_refresh_changed (GtkDropDown     *dd G_GNUC_UNUSED,
                    GParamSpec      *pspec G_GNUC_UNUSED,
                    OozeDisplayPane *self)
{
  if (self->filling || !self->config)
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
on_primary_clicked (GtkButton       *btn G_GNUC_UNUSED,
                    OozeDisplayPane *self)
{
  if (!self->config)
    return;

  ooze_display_config_set_primary (self->config, self->selected);
  ooze_display_pane_fill_selected (self);
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

  if (self->applying || self->confirm_dialog)
    return;

  ooze_display_pane_schedule_reload (self);
}

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

  ooze_display_pane_close_confirm_dialog (self);

  if (self->changed_signal)
    {
      bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
      if (bus)
        g_dbus_connection_signal_unsubscribe (bus, self->changed_signal);
      self->changed_signal = 0;
    }

  g_clear_pointer (&self->config, ooze_display_config_free);
  g_clear_pointer (&self->revert_config, ooze_display_config_free);

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
  GtkWidget *layout_frame;
  GtkGesture *click;
  GtkEventController *motion;
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

  layout_frame = gtk_frame_new (NULL);
  gtk_widget_set_hexpand (layout_frame, TRUE);
  gtk_box_append (GTK_BOX (box), layout_frame);

  self->layout_area = gtk_drawing_area_new ();
  gtk_widget_set_size_request (self->layout_area, -1, 180);
  gtk_widget_set_hexpand (self->layout_area, TRUE);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (self->layout_area),
                                  on_layout_draw, self, NULL);
  gtk_frame_set_child (GTK_FRAME (layout_frame), self->layout_area);

  click = gtk_gesture_click_new ();
  g_signal_connect (click, "pressed", G_CALLBACK (on_layout_pressed), self);
  g_signal_connect (click, "released", G_CALLBACK (on_layout_released), self);
  gtk_widget_add_controller (self->layout_area, GTK_EVENT_CONTROLLER (click));

  motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "motion", G_CALLBACK (on_layout_motion), self);
  gtk_widget_add_controller (self->layout_area, motion);

  self->display_label = gtk_label_new ("");
  gtk_label_set_xalign (GTK_LABEL (self->display_label), 0.0f);
  gtk_widget_add_css_class (self->display_label, "title-4");
  gtk_box_append (GTK_BOX (box), self->display_label);

  self->primary_button = gtk_button_new_with_label ("Set as Main Display");
  gtk_widget_set_halign (self->primary_button, GTK_ALIGN_START);
  g_signal_connect (self->primary_button, "clicked",
                    G_CALLBACK (on_primary_clicked), self);
  gtk_box_append (GTK_BOX (box), self->primary_button);

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
