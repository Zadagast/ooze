#include "ooze-segment.h"
#include "ooze-icons.h"
#include "ooze-theme.h"

/*
 * OozeSegmentGroup — recessed track of connected toggle segments with a
 * caption underneath (classic Finder "View" control).
 */

struct _OozeSegmentGroup
{
  GtkBox     parent_instance;

  GtkWidget *track;      /* horizontal box holding the segments */
  GtkWidget *caption;
  GPtrArray *segments;   /* GtkButton*, unowned */
  int        active;
};

enum
{
  SIGNAL_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (OozeSegmentGroup, ooze_segment_group, GTK_TYPE_BOX)

static void
ooze_segment_group_ensure_css (void)
{
  static gboolean loaded = FALSE;
  GdkDisplay     *display;
  GtkCssProvider *p;

  if (loaded)
    return;

  display = gdk_display_get_default ();
  if (!display)
    return;

  p = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (p,
    /* Recessed Aqua track — a shallow well the segments sit in. */
    ".ooze-segment-track {"
    "  background: alpha(@window_fg_color, 0.07);"
    "  border: 1px solid alpha(@window_fg_color, 0.28);"
    "  border-radius: 6px;"
    "  padding: 1px;"
    "}"
    ".ooze-segment {"
    "  background: none;"
    "  border: none;"
    "  box-shadow: none;"
    "  outline: none;"
    "  border-radius: 4px;"
    "  min-height: 22px;"
    "  min-width: 34px;"
    "  padding: 1px 8px;"
    "  color: alpha(@window_fg_color, 0.85);"
    "}"
    ".ooze-segment:focus:not(:focus-visible) {"
    "  outline: none;"
    "}"
    ".ooze-segment:hover:not(.active) {"
    "  background: alpha(@window_fg_color, 0.08);"
    "}"
    /* Active segment: pressed-in Aqua gel. */
    ".ooze-segment.active {"
    "  background: linear-gradient(to bottom,"
    "      alpha(@accent_bg_color, 0.92),"
    "      alpha(@accent_bg_color, 0.72));"
    "  box-shadow: inset 0 1px 2px alpha(black, 0.35);"
    "  color: @accent_fg_color;"
    "}"
    /* Hairline between adjacent segments. */
    ".ooze-segment + .ooze-segment {"
    "  border-left: 1px solid alpha(@window_fg_color, 0.18);"
    "  border-top-left-radius: 0;"
    "  border-bottom-left-radius: 0;"
    "}"
    ".ooze-segment:not(:last-child) {"
    "  border-top-right-radius: 0;"
    "  border-bottom-right-radius: 0;"
    "}"
    /* Caption under the track, Finder style. */
    ".ooze-segment-caption {"
    "  font-size: 9pt;"
    "  margin-top: 4px;"
    "  color: @window_fg_color;"
    "}");
  gtk_style_context_add_provider_for_display (display,
    GTK_STYLE_PROVIDER (p),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (p);
  loaded = TRUE;
}

static void
on_segment_clicked (GtkButton        *button,
                    OozeSegmentGroup *self)
{
  guint i;

  for (i = 0; i < self->segments->len; i++)
    {
      if (g_ptr_array_index (self->segments, i) == (gpointer) button)
        {
          ooze_segment_group_set_active (GTK_WIDGET (self), (int) i);
          return;
        }
    }
}

static void
ooze_segment_group_dispose (GObject *object);

static void
ooze_segment_group_class_init (OozeSegmentGroupClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = ooze_segment_group_dispose;

  signals[SIGNAL_CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_INT);
}

static void
ooze_segment_group_dispose (GObject *object)
{
  OozeSegmentGroup *self = OOZE_SEGMENT_GROUP (object);

  g_clear_pointer (&self->segments, g_ptr_array_unref);

  G_OBJECT_CLASS (ooze_segment_group_parent_class)->dispose (object);
}

static void
ooze_segment_group_init (OozeSegmentGroup *self)
{
  ooze_segment_group_ensure_css ();

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self),
                                  GTK_ORIENTATION_VERTICAL);
  gtk_box_set_spacing (GTK_BOX (self), 0);
  gtk_widget_set_valign (GTK_WIDGET (self), GTK_ALIGN_CENTER);

  self->segments = g_ptr_array_new ();
  self->active = -1;

  self->track = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class (self->track, "ooze-segment-track");
  gtk_widget_set_halign (self->track, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (self), self->track);

  self->caption = gtk_label_new ("");
  gtk_widget_add_css_class (self->caption, "ooze-segment-caption");
  gtk_widget_set_halign (self->caption, GTK_ALIGN_CENTER);
  gtk_widget_set_visible (self->caption, FALSE);
  gtk_box_append (GTK_BOX (self), self->caption);
}

GtkWidget *
ooze_segment_group_new (const char *caption)
{
  OozeSegmentGroup *self = g_object_new (OOZE_TYPE_SEGMENT_GROUP, NULL);

  if (caption && caption[0])
    {
      gtk_label_set_text (GTK_LABEL (self->caption), caption);
      gtk_widget_set_visible (self->caption, TRUE);
    }

  return GTK_WIDGET (self);
}

int
ooze_segment_group_add (GtkWidget          *group,
                        const char * const *icon_names,
                        const char         *tooltip)
{
  OozeSegmentGroup *self = OOZE_SEGMENT_GROUP (group);
  GtkWidget        *button;
  GtkWidget        *image;

  button = gtk_button_new ();
  gtk_widget_add_css_class (button, "ooze-segment");
  gtk_button_set_has_frame (GTK_BUTTON (button), FALSE);
  if (tooltip && tooltip[0])
    gtk_widget_set_tooltip_text (button, tooltip);

  image = ooze_icon_image_new (icon_names, 16);
  gtk_button_set_child (GTK_BUTTON (button), image);

  g_signal_connect (button, "clicked",
                    G_CALLBACK (on_segment_clicked), self);

  gtk_box_append (GTK_BOX (self->track), button);
  g_ptr_array_add (self->segments, button);

  /* First segment becomes active by default. */
  if (self->active < 0)
    {
      self->active = 0;
      gtk_widget_add_css_class (button, "active");
    }

  return (int) self->segments->len - 1;
}

void
ooze_segment_group_set_active (GtkWidget *group,
                               int        index)
{
  OozeSegmentGroup *self = OOZE_SEGMENT_GROUP (group);
  guint             i;

  if (index < 0 || (guint) index >= self->segments->len)
    return;

  for (i = 0; i < self->segments->len; i++)
    {
      GtkWidget *seg = g_ptr_array_index (self->segments, i);

      if ((int) i == index)
        gtk_widget_add_css_class (seg, "active");
      else
        gtk_widget_remove_css_class (seg, "active");
    }

  if (self->active != index)
    {
      self->active = index;
      g_signal_emit (self, signals[SIGNAL_CHANGED], 0, index);
    }
}

int
ooze_segment_group_get_active (GtkWidget *group)
{
  return OOZE_SEGMENT_GROUP (group)->active;
}
