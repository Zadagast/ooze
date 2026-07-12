#include "ooze-sound-pane.h"
#include "ooze-ear-pw.h"

#include "ooze-scroll.h"
#include "ooze-surface.h"

#include <adwaita.h>
#include <math.h>

struct _OozeSoundPane
{
  GtkBox parent_instance;

  GtkWidget *stack;
  GtkWidget *output_list;
  GtkWidget *input_list;
  GtkWidget *apps_list;
  GtkWidget *output_volume;
  GtkWidget *output_mute;
  GtkWidget *input_volume;
  GtkWidget *input_mute;
  GtkWidget *status;

  OozeEarPw *pw;
  gboolean   rebuilding;
  guint      drag_freeze;
  guint      pending_rebuild_id;

  uint32_t   selected_output_id;
  uint32_t   selected_input_id;
};

G_DEFINE_FINAL_TYPE (OozeSoundPane, ooze_sound_pane, GTK_TYPE_BOX)

static float
linear_to_ui (float linear)
{
  if (linear <= 0.f)
    return 0.f;
  return cbrtf (linear);
}

static float
ui_to_linear (float ui)
{
  if (ui <= 0.f)
    return 0.f;
  return ui * ui * ui;
}

static void
clear_list (GtkWidget *list)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (list)) != NULL)
    gtk_list_box_remove (GTK_LIST_BOX (list), child);
}

static void sound_rebuild (OozeSoundPane *self);

static gboolean
sound_rebuild_idle (gpointer user_data)
{
  OozeSoundPane *self = user_data;

  self->pending_rebuild_id = 0;
  if (self->drag_freeze == 0)
    sound_rebuild (self);
  return G_SOURCE_REMOVE;
}

static void
sound_request_rebuild (OozeSoundPane *self)
{
  if (self->drag_freeze > 0)
    {
      if (self->pending_rebuild_id == 0)
        self->pending_rebuild_id = g_idle_add (sound_rebuild_idle, self);
      return;
    }

  sound_rebuild (self);
}

static void
on_scale_pressed (GtkGestureClick *gesture G_GNUC_UNUSED,
                  gint             n_press G_GNUC_UNUSED,
                  gdouble          x G_GNUC_UNUSED,
                  gdouble          y G_GNUC_UNUSED,
                  OozeSoundPane   *self)
{
  self->drag_freeze++;
}

static void
on_scale_released (GtkGestureClick *gesture G_GNUC_UNUSED,
                   gint             n_press G_GNUC_UNUSED,
                   gdouble          x G_GNUC_UNUSED,
                   gdouble          y G_GNUC_UNUSED,
                   OozeSoundPane   *self)
{
  if (self->drag_freeze > 0)
    self->drag_freeze--;
  if (self->drag_freeze == 0)
    sound_request_rebuild (self);
}

static void
attach_drag_freeze (GtkWidget *scale, OozeSoundPane *self)
{
  GtkGesture *press = gtk_gesture_click_new ();

  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (press), GDK_BUTTON_PRIMARY);
  g_signal_connect (press, "pressed", G_CALLBACK (on_scale_pressed), self);
  g_signal_connect (press, "released", G_CALLBACK (on_scale_released), self);
  gtk_widget_add_controller (scale, GTK_EVENT_CONTROLLER (press));
}

static void
on_device_volume_changed (GtkRange *range, OozeSoundPane *self)
{
  uint32_t id;

  if (self->rebuilding || !self->pw)
    return;

  id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (range), "node-id"));
  if (id == 0)
    return;
  ooze_ear_pw_set_volume (self->pw, id, ui_to_linear ((float) gtk_range_get_value (range)));
}

static void
on_device_mute_toggled (GtkCheckButton *btn, OozeSoundPane *self)
{
  uint32_t id;

  if (self->rebuilding || !self->pw)
    return;

  id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (btn), "node-id"));
  if (id == 0)
    return;
  ooze_ear_pw_set_mute (self->pw, id, gtk_check_button_get_active (btn));
}

static void
on_stream_mute_toggled (GtkToggleButton *btn, OozeSoundPane *self)
{
  uint32_t id;

  if (self->rebuilding || !self->pw)
    return;

  id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (btn), "node-id"));
  ooze_ear_pw_set_mute (self->pw, id, gtk_toggle_button_get_active (btn));
}

static void
on_stream_volume_changed (GtkRange *range, OozeSoundPane *self)
{
  uint32_t id;

  if (self->rebuilding || !self->pw)
    return;

  id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (range), "node-id"));
  ooze_ear_pw_set_volume (self->pw, id, ui_to_linear ((float) gtk_range_get_value (range)));
}

static void
on_target_changed (GtkDropDown *drop, GParamSpec *pspec G_GNUC_UNUSED,
                   OozeSoundPane *self)
{
  uint32_t stream_id;
  guint selected;
  GListModel *model;
  GtkStringObject *item;
  const char *id_str;
  uint32_t target_id = 0;

  if (self->rebuilding || !self->pw)
    return;

  stream_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (drop), "node-id"));
  selected = gtk_drop_down_get_selected (drop);
  model = gtk_drop_down_get_model (drop);
  if (!model || selected == GTK_INVALID_LIST_POSITION)
    return;

  item = g_list_model_get_item (model, selected);
  if (!item)
    return;

  id_str = g_object_get_data (G_OBJECT (item), "target-id");
  if (id_str)
    target_id = (uint32_t) g_ascii_strtoull (id_str, NULL, 10);
  g_object_unref (item);

  ooze_ear_pw_set_target (self->pw, stream_id, target_id);
}

static gboolean
target_matches (const OozeEarNodeInfo *stream, const OozeEarNodeInfo *device)
{
  if (!stream->target || !stream->target[0] || g_strcmp0 (stream->target, "-1") == 0)
    return FALSE;
  if (device->object_serial && g_strcmp0 (stream->target, device->object_serial) == 0)
    return TRUE;
  if (device->node_name && g_strcmp0 (stream->target, device->node_name) == 0)
    return TRUE;
  {
    g_autofree char *id_str = g_strdup_printf ("%u", device->id);
    if (g_strcmp0 (stream->target, id_str) == 0)
      return TRUE;
  }
  return FALSE;
}

static GtkWidget *
make_target_dropdown (OozeSoundPane         *self,
                      const OozeEarNodeInfo *stream,
                      GPtrArray             *devices)
{
  GtkStringList *strings;
  GtkWidget *drop;
  guint i;
  guint selected = 0;
  GListModel *model;

  strings = gtk_string_list_new (NULL);
  gtk_string_list_append (strings, "Default device");

  for (i = 0; i < devices->len; i++)
    {
      OozeEarNodeInfo *dev = g_ptr_array_index (devices, i);
      g_autofree char *label = NULL;

      if (dev->is_default)
        label = g_strdup_printf ("%s (default)", dev->name ? dev->name : "Device");
      else
        label = g_strdup (dev->name ? dev->name : "Device");

      gtk_string_list_append (strings, label);
      if (target_matches (stream, dev))
        selected = i + 1;
    }

  drop = gtk_drop_down_new (G_LIST_MODEL (strings), NULL);
  gtk_drop_down_set_selected (GTK_DROP_DOWN (drop), selected);
  gtk_widget_set_size_request (drop, 180, -1);
  g_object_set_data (G_OBJECT (drop), "node-id", GUINT_TO_POINTER (stream->id));

  model = G_LIST_MODEL (strings);
  {
    GtkStringObject *item0 = g_list_model_get_item (model, 0);
    if (item0)
      {
        g_object_set_data_full (G_OBJECT (item0), "target-id", g_strdup ("0"), g_free);
        g_object_unref (item0);
      }
  }
  for (i = 0; i < devices->len; i++)
    {
      OozeEarNodeInfo *dev = g_ptr_array_index (devices, i);
      GtkStringObject *item = g_list_model_get_item (model, i + 1);
      if (item)
        {
          g_object_set_data_full (G_OBJECT (item), "target-id",
                                  g_strdup_printf ("%u", dev->id), g_free);
          g_object_unref (item);
        }
    }

  g_signal_connect (drop, "notify::selected", G_CALLBACK (on_target_changed), self);
  return drop;
}

static void
update_device_controls (OozeSoundPane         *self,
                        const OozeEarNodeInfo *info,
                        gboolean               is_output)
{
  GtkWidget *scale = is_output ? self->output_volume : self->input_volume;
  GtkWidget *mute = is_output ? self->output_mute : self->input_mute;

  g_object_set_data (G_OBJECT (scale), "node-id", GUINT_TO_POINTER (info->id));
  g_object_set_data (G_OBJECT (mute), "node-id", GUINT_TO_POINTER (info->id));
  gtk_range_set_value (GTK_RANGE (scale), linear_to_ui (info->volume));
  gtk_check_button_set_active (GTK_CHECK_BUTTON (mute), info->mute);
  gtk_widget_set_sensitive (scale, TRUE);
  gtk_widget_set_sensitive (mute, TRUE);
}

static void
clear_device_controls (OozeSoundPane *self, gboolean is_output)
{
  GtkWidget *scale = is_output ? self->output_volume : self->input_volume;
  GtkWidget *mute = is_output ? self->output_mute : self->input_mute;

  g_object_set_data (G_OBJECT (scale), "node-id", GUINT_TO_POINTER (0));
  g_object_set_data (G_OBJECT (mute), "node-id", GUINT_TO_POINTER (0));
  gtk_range_set_value (GTK_RANGE (scale), 0.0);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (mute), FALSE);
  gtk_widget_set_sensitive (scale, FALSE);
  gtk_widget_set_sensitive (mute, FALSE);
}

static GtkWidget *
make_device_row (const OozeEarNodeInfo *info)
{
  GtkWidget *row;
  GtkWidget *box;
  GtkWidget *title;
  GtkWidget *badge = NULL;

  row = gtk_list_box_row_new ();
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), TRUE);
  g_object_set_data (G_OBJECT (row), "node-id", GUINT_TO_POINTER (info->id));

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_top (box, 10);
  gtk_widget_set_margin_bottom (box, 10);
  gtk_widget_set_margin_start (box, 12);
  gtk_widget_set_margin_end (box, 12);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);

  title = gtk_label_new (info->name ? info->name : "Audio");
  gtk_label_set_xalign (GTK_LABEL (title), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand (title, TRUE);
  gtk_box_append (GTK_BOX (box), title);

  if (info->is_default)
    {
      badge = gtk_label_new ("Default");
      gtk_widget_add_css_class (badge, "dim-label");
      gtk_box_append (GTK_BOX (box), badge);
    }

  return row;
}

static GtkWidget *
make_stream_row (OozeSoundPane         *self,
                 const OozeEarNodeInfo *info,
                 GPtrArray             *route_devices)
{
  GtkWidget *row;
  GtkWidget *outer;
  GtkWidget *top;
  GtkWidget *labels;
  GtkWidget *title;
  GtkWidget *mute;
  GtkWidget *scale;
  GtkWidget *route;

  row = gtk_list_box_row_new ();
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
  g_object_set_data (G_OBJECT (row), "node-id", GUINT_TO_POINTER (info->id));

  outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top (outer, 8);
  gtk_widget_set_margin_bottom (outer, 8);
  gtk_widget_set_margin_start (outer, 12);
  gtk_widget_set_margin_end (outer, 12);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), outer);

  top = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_box_append (GTK_BOX (outer), top);

  labels = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_hexpand (labels, TRUE);
  gtk_box_append (GTK_BOX (top), labels);

  title = gtk_label_new (info->name ? info->name : "Stream");
  gtk_label_set_xalign (GTK_LABEL (title), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_END);
  gtk_widget_add_css_class (title, "heading");
  gtk_box_append (GTK_BOX (labels), title);

  if (info->detail && info->detail[0])
    {
      GtkWidget *sub = gtk_label_new (info->detail);
      gtk_label_set_xalign (GTK_LABEL (sub), 0.0);
      gtk_label_set_ellipsize (GTK_LABEL (sub), PANGO_ELLIPSIZE_END);
      gtk_widget_add_css_class (sub, "dim-label");
      gtk_box_append (GTK_BOX (labels), sub);
    }

  if (route_devices)
    {
      route = make_target_dropdown (self, info, route_devices);
      gtk_box_append (GTK_BOX (top), route);
    }

  mute = gtk_toggle_button_new_with_label ("Mute");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mute), info->mute);
  g_object_set_data (G_OBJECT (mute), "node-id", GUINT_TO_POINTER (info->id));
  g_signal_connect (mute, "toggled", G_CALLBACK (on_stream_mute_toggled), self);
  gtk_box_append (GTK_BOX (top), mute);

  scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
  gtk_range_set_value (GTK_RANGE (scale), linear_to_ui (info->volume));
  gtk_widget_set_hexpand (scale, TRUE);
  gtk_scale_set_draw_value (GTK_SCALE (scale), FALSE);
  g_object_set_data (G_OBJECT (scale), "node-id", GUINT_TO_POINTER (info->id));
  g_signal_connect (scale, "value-changed", G_CALLBACK (on_stream_volume_changed), self);
  attach_drag_freeze (scale, self);
  gtk_box_append (GTK_BOX (outer), scale);

  return row;
}

static GtkWidget *
make_section_header (const char *text)
{
  GtkWidget *row;
  GtkWidget *label;

  row = gtk_list_box_row_new ();
  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
  gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (row), FALSE);
  gtk_widget_set_sensitive (row, FALSE);

  label = gtk_label_new (text);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_add_css_class (label, "heading");
  gtk_widget_set_margin_top (label, 8);
  gtk_widget_set_margin_bottom (label, 4);
  gtk_widget_set_margin_start (label, 12);
  gtk_widget_set_margin_end (label, 12);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), label);
  return row;
}

static int
compare_nodes (gconstpointer a, gconstpointer b)
{
  const OozeEarNodeInfo *na = *(const OozeEarNodeInfo * const *) a;
  const OozeEarNodeInfo *nb = *(const OozeEarNodeInfo * const *) b;

  if (na->is_default != nb->is_default)
    return na->is_default ? -1 : 1;
  return g_ascii_strcasecmp (na->name ? na->name : "",
                             nb->name ? nb->name : "");
}

static void
select_device_row (GtkWidget *list, uint32_t id)
{
  GtkWidget *child;

  for (child = gtk_widget_get_first_child (list);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      uint32_t row_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (child), "node-id"));
      if (row_id == id)
        {
          gtk_list_box_select_row (GTK_LIST_BOX (list), GTK_LIST_BOX_ROW (child));
          return;
        }
    }
}

static void
on_device_row_selected (GtkListBox    *box,
                        GtkListBoxRow *row,
                        OozeSoundPane *self)
{
  uint32_t id;
  gboolean is_output;

  if (self->rebuilding || !row || !self->pw)
    return;

  id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (row), "node-id"));
  if (id == 0)
    return;

  is_output = (GTK_WIDGET (box) == self->output_list);
  if (is_output)
    {
      if (id == self->selected_output_id)
        return;
      self->selected_output_id = id;
    }
  else
    {
      if (id == self->selected_input_id)
        return;
      self->selected_input_id = id;
    }

  ooze_ear_pw_set_default (self->pw, id);
}

static void
sound_rebuild (OozeSoundPane *self)
{
  g_autoptr (GPtrArray) nodes = NULL;
  g_autoptr (GPtrArray) sinks = NULL;
  g_autoptr (GPtrArray) sources = NULL;
  guint i;
  guint counts[4] = { 0, 0, 0, 0 };
  const OozeEarNodeInfo *sel_out = NULL;
  const OozeEarNodeInfo *sel_in = NULL;
  gboolean have_playback = FALSE;
  gboolean have_record = FALSE;

  if (!self->pw || self->drag_freeze > 0)
    return;

  self->rebuilding = TRUE;

  clear_list (self->output_list);
  clear_list (self->input_list);
  clear_list (self->apps_list);

  nodes = ooze_ear_pw_snapshot (self->pw);
  g_ptr_array_sort (nodes, compare_nodes);

  sinks = g_ptr_array_new ();
  sources = g_ptr_array_new ();
  for (i = 0; i < nodes->len; i++)
    {
      OozeEarNodeInfo *info = g_ptr_array_index (nodes, i);
      if (info->kind == OOZE_EAR_NODE_SINK)
        g_ptr_array_add (sinks, info);
      else if (info->kind == OOZE_EAR_NODE_SOURCE)
        g_ptr_array_add (sources, info);
    }

  for (i = 0; i < sinks->len; i++)
    {
      OozeEarNodeInfo *info = g_ptr_array_index (sinks, i);
      gtk_list_box_append (GTK_LIST_BOX (self->output_list), make_device_row (info));
      counts[OOZE_EAR_NODE_SINK]++;
      if (info->is_default || info->id == self->selected_output_id)
        sel_out = info;
      if (!sel_out && i == 0)
        sel_out = info;
    }

  for (i = 0; i < sources->len; i++)
    {
      OozeEarNodeInfo *info = g_ptr_array_index (sources, i);
      gtk_list_box_append (GTK_LIST_BOX (self->input_list), make_device_row (info));
      counts[OOZE_EAR_NODE_SOURCE]++;
      if (info->is_default || info->id == self->selected_input_id)
        sel_in = info;
      if (!sel_in && i == 0)
        sel_in = info;
    }

  /* Prefer explicit defaults for selection highlight. */
  for (i = 0; i < sinks->len; i++)
    {
      OozeEarNodeInfo *info = g_ptr_array_index (sinks, i);
      if (info->is_default)
        {
          sel_out = info;
          break;
        }
    }
  for (i = 0; i < sources->len; i++)
    {
      OozeEarNodeInfo *info = g_ptr_array_index (sources, i);
      if (info->is_default)
        {
          sel_in = info;
          break;
        }
    }

  for (i = 0; i < nodes->len; i++)
    {
      OozeEarNodeInfo *info = g_ptr_array_index (nodes, i);
      if (info->kind == OOZE_EAR_NODE_PLAYBACK)
        {
          if (!have_playback)
            {
              gtk_list_box_append (GTK_LIST_BOX (self->apps_list),
                                   make_section_header ("Playback"));
              have_playback = TRUE;
            }
          gtk_list_box_append (GTK_LIST_BOX (self->apps_list),
                               make_stream_row (self, info, sinks));
          counts[OOZE_EAR_NODE_PLAYBACK]++;
        }
    }
  for (i = 0; i < nodes->len; i++)
    {
      OozeEarNodeInfo *info = g_ptr_array_index (nodes, i);
      if (info->kind == OOZE_EAR_NODE_RECORD)
        {
          if (!have_record)
            {
              gtk_list_box_append (GTK_LIST_BOX (self->apps_list),
                                   make_section_header ("Recording"));
              have_record = TRUE;
            }
          gtk_list_box_append (GTK_LIST_BOX (self->apps_list),
                               make_stream_row (self, info, sources));
          counts[OOZE_EAR_NODE_RECORD]++;
        }
    }

  if (sel_out)
    {
      self->selected_output_id = sel_out->id;
      select_device_row (self->output_list, sel_out->id);
      update_device_controls (self, sel_out, TRUE);
    }
  else
    {
      self->selected_output_id = 0;
      clear_device_controls (self, TRUE);
    }

  if (sel_in)
    {
      self->selected_input_id = sel_in->id;
      select_device_row (self->input_list, sel_in->id);
      update_device_controls (self, sel_in, FALSE);
    }
  else
    {
      self->selected_input_id = 0;
      clear_device_controls (self, FALSE);
    }

  {
    g_autofree char *msg = g_strdup_printf (
        "Playback %u · Recording %u · Outputs %u · Inputs %u",
        counts[OOZE_EAR_NODE_PLAYBACK],
        counts[OOZE_EAR_NODE_RECORD],
        counts[OOZE_EAR_NODE_SINK],
        counts[OOZE_EAR_NODE_SOURCE]);
    gtk_label_set_text (GTK_LABEL (self->status), msg);
  }

  self->rebuilding = FALSE;
}

static void
on_pw_changed (gpointer user_data)
{
  sound_request_rebuild (OOZE_SOUND_PANE (user_data));
}

static GtkWidget *
make_volume_strip (OozeSoundPane *self,
                   const char    *label_text,
                   GtkWidget    **scale_out,
                   GtkWidget    **mute_out)
{
  GtkWidget *strip;
  GtkWidget *label;
  GtkWidget *low;
  GtkWidget *high;
  GtkWidget *scale;
  GtkWidget *mute;

  strip = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top (strip, 8);
  gtk_widget_set_margin_bottom (strip, 8);
  gtk_widget_set_margin_start (strip, 14);
  gtk_widget_set_margin_end (strip, 14);

  label = gtk_label_new (label_text);
  gtk_widget_set_size_request (label, 110, -1);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_box_append (GTK_BOX (strip), label);

  low = gtk_image_new_from_icon_name ("audio-volume-low-symbolic");
  gtk_box_append (GTK_BOX (strip), low);

  scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.01);
  gtk_widget_set_hexpand (scale, TRUE);
  gtk_scale_set_draw_value (GTK_SCALE (scale), FALSE);
  g_signal_connect (scale, "value-changed", G_CALLBACK (on_device_volume_changed), self);
  attach_drag_freeze (scale, self);
  gtk_box_append (GTK_BOX (strip), scale);

  high = gtk_image_new_from_icon_name ("audio-volume-high-symbolic");
  gtk_box_append (GTK_BOX (strip), high);

  mute = gtk_check_button_new_with_label ("Mute");
  g_signal_connect (mute, "toggled", G_CALLBACK (on_device_mute_toggled), self);
  gtk_box_append (GTK_BOX (strip), mute);

  *scale_out = scale;
  *mute_out = mute;
  return strip;
}

static GtkWidget *
make_device_page (OozeSoundPane *self,
                  const char    *heading,
                  GtkWidget    **list_out,
                  GtkWidget     *volume_strip)
{
  GtkWidget *page;
  GtkWidget *frame;
  GtkWidget *scrolled;
  GtkWidget *list;
  GtkWidget *label;

  page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_margin_top (page, 12);
  gtk_widget_set_margin_start (page, 16);
  gtk_widget_set_margin_end (page, 16);

  label = gtk_label_new (heading);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_set_margin_bottom (label, 8);
  gtk_box_append (GTK_BOX (page), label);

  frame = gtk_frame_new (NULL);
  gtk_widget_set_vexpand (frame, TRUE);
  gtk_widget_set_hexpand (frame, TRUE);

  scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand (scrolled, TRUE);

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_SINGLE);
  gtk_widget_add_css_class (list, "boxed-list");
  g_signal_connect (list, "row-selected", G_CALLBACK (on_device_row_selected), self);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list);
  gtk_frame_set_child (GTK_FRAME (frame), scrolled);
  gtk_box_append (GTK_BOX (page), frame);

  gtk_box_append (GTK_BOX (page), volume_strip);

  *list_out = list;
  return page;
}

static GtkWidget *
make_apps_page (OozeSoundPane *self)
{
  GtkWidget *page;
  GtkWidget *frame;
  GtkWidget *scrolled;
  GtkWidget *label;

  page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_margin_top (page, 12);
  gtk_widget_set_margin_start (page, 16);
  gtk_widget_set_margin_end (page, 16);
  gtk_widget_set_margin_bottom (page, 12);

  label = gtk_label_new ("Application volume and device routing");
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_set_margin_bottom (label, 8);
  gtk_box_append (GTK_BOX (page), label);

  frame = gtk_frame_new (NULL);
  gtk_widget_set_vexpand (frame, TRUE);

  scrolled = ooze_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand (scrolled, TRUE);

  self->apps_list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->apps_list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class (self->apps_list, "boxed-list");
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), self->apps_list);
  gtk_frame_set_child (GTK_FRAME (frame), scrolled);
  gtk_box_append (GTK_BOX (page), frame);

  return page;
}

static void
ooze_sound_pane_dispose (GObject *object)
{
  OozeSoundPane *self = OOZE_SOUND_PANE (object);

  if (self->pending_rebuild_id)
    {
      g_source_remove (self->pending_rebuild_id);
      self->pending_rebuild_id = 0;
    }
  g_clear_pointer (&self->pw, ooze_ear_pw_free);
  G_OBJECT_CLASS (ooze_sound_pane_parent_class)->dispose (object);
}

static void
ooze_sound_pane_class_init (OozeSoundPaneClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = ooze_sound_pane_dispose;
}

static void
ooze_sound_pane_init (OozeSoundPane *self)
{
  GtkWidget *toolbar;
  GtkWidget *switcher;
  GtkWidget *output_strip;
  GtkWidget *input_strip;
  GtkWidget *output_page;
  GtkWidget *input_page;
  GtkWidget *apps_page;
  GtkWidget *status_bar;

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
  gtk_widget_add_css_class (GTK_WIDGET (self), "ooze-sound-pane");

  toolbar = ooze_surface_new (OOZE_SURFACE_TOOLBAR, GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_margin_top (toolbar, 8);
  gtk_widget_set_margin_bottom (toolbar, 4);
  gtk_widget_set_margin_start (toolbar, 12);
  gtk_widget_set_margin_end (toolbar, 12);

  self->stack = gtk_stack_new ();
  gtk_stack_set_transition_type (GTK_STACK (self->stack),
                                 GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_widget_set_hexpand (self->stack, TRUE);
  gtk_widget_set_vexpand (self->stack, TRUE);

  output_strip = make_volume_strip (self, "Output volume:",
                                    &self->output_volume, &self->output_mute);
  input_strip = make_volume_strip (self, "Input volume:",
                                   &self->input_volume, &self->input_mute);

  output_page = make_device_page (self,
                                  "Choose a sound output device:",
                                  &self->output_list,
                                  output_strip);
  input_page = make_device_page (self,
                                 "Choose a sound input device:",
                                 &self->input_list,
                                 input_strip);
  apps_page = make_apps_page (self);

  gtk_stack_add_titled (GTK_STACK (self->stack), output_page, "output", "Output");
  gtk_stack_add_titled (GTK_STACK (self->stack), input_page, "input", "Input");
  gtk_stack_add_titled (GTK_STACK (self->stack), apps_page, "apps", "Applications");

  switcher = gtk_stack_switcher_new ();
  gtk_stack_switcher_set_stack (GTK_STACK_SWITCHER (switcher), GTK_STACK (self->stack));
  gtk_widget_set_halign (switcher, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (toolbar), switcher);

  status_bar = ooze_surface_new (OOZE_SURFACE_STATUSBAR, GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top (status_bar, 2);
  gtk_widget_set_margin_bottom (status_bar, 4);
  gtk_widget_set_margin_start (status_bar, 12);
  gtk_widget_set_margin_end (status_bar, 12);
  self->status = gtk_label_new ("Connecting to PipeWire…");
  gtk_label_set_xalign (GTK_LABEL (self->status), 0.0);
  gtk_widget_add_css_class (self->status, "dim-label");
  gtk_box_append (GTK_BOX (status_bar), self->status);

  gtk_box_append (GTK_BOX (self), toolbar);
  gtk_box_append (GTK_BOX (self), self->stack);
  gtk_box_append (GTK_BOX (self), status_bar);

  clear_device_controls (self, TRUE);
  clear_device_controls (self, FALSE);

  self->pw = ooze_ear_pw_new (on_pw_changed, self);
  if (!self->pw)
    gtk_label_set_text (GTK_LABEL (self->status),
                        "Could not connect to PipeWire. Is the daemon running?");
  else
    sound_rebuild (self);

  g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                           G_CALLBACK (gtk_widget_queue_draw), self,
                           G_CONNECT_SWAPPED);
}

GtkWidget *
ooze_sound_pane_new (void)
{
  return g_object_new (OOZE_TYPE_SOUND_PANE, NULL);
}
