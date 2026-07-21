#include "ooze-watch-transport.h"

#include "ooze-draw.h"
#include "ooze-palette.h"

#include <math.h>

#define DECK_HEIGHT      86
#define SCRUB_ROW_Y      10
#define SCRUB_ROW_H      20
#define BUTTON_ROW_CY    58
#define LCD_WIDTH        86
#define VOLUME_WIDTH     104
#define PLAY_RADIUS      22
#define JUMP_RADIUS      16
#define STEP_RADIUS      12
#define BUTTON_GAP       10

typedef enum
{
  HIT_NONE,
  HIT_STEP_BACK,
  HIT_JUMP_BACK,
  HIT_PLAY,
  HIT_JUMP_FWD,
  HIT_STEP_FWD,
  HIT_SCRUB,
  HIT_VOLUME,
} TransportHit;

struct _OozeWatchTransport
{
  GtkWidget parent_instance;

  double   time_pos;
  double   duration;
  double   volume;
  gboolean paused;
  gboolean has_media;

  TransportHit hover;
  TransportHit pressed;
  gboolean     scrubbing;
};

G_DEFINE_FINAL_TYPE (OozeWatchTransport, ooze_watch_transport, GTK_TYPE_WIDGET)

enum
{
  SIGNAL_PLAY_TOGGLED,
  SIGNAL_SEEK_FRAC,
  SIGNAL_VOLUME,
  SIGNAL_STEP,
  SIGNAL_JUMP,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ── Layout ───────────────────────────────────────────────────────────── */

typedef struct
{
  double cx[5];      /* centers of the five round buttons */
  double r[5];       /* their radii */
  double scrub_x, scrub_w;
  double vol_x, vol_w;
} DeckLayout;

static void
deck_layout (OozeWatchTransport *self, DeckLayout *l)
{
  int w = gtk_widget_get_width (GTK_WIDGET (self));
  double mid = w / 2.0;
  double r[5] = { STEP_RADIUS, JUMP_RADIUS, PLAY_RADIUS,
                  JUMP_RADIUS, STEP_RADIUS };

  l->r[0] = r[0]; l->r[1] = r[1]; l->r[2] = r[2];
  l->r[3] = r[3]; l->r[4] = r[4];
  l->cx[2] = mid;
  l->cx[1] = l->cx[2] - r[2] - BUTTON_GAP - r[1];
  l->cx[0] = l->cx[1] - r[1] - BUTTON_GAP - r[0];
  l->cx[3] = l->cx[2] + r[2] + BUTTON_GAP + r[3];
  l->cx[4] = l->cx[3] + r[3] + BUTTON_GAP + r[4];

  l->scrub_x = 12 + LCD_WIDTH + 10;
  l->scrub_w = MAX (w - l->scrub_x - 16, 40);

  l->vol_x = 16;
  l->vol_w = VOLUME_WIDTH;
}

/* ── Drawing helpers ──────────────────────────────────────────────────── */

static void
draw_brushed_metal (cairo_t *cr, int w, int h, const OozePalette *p)
{
  cairo_pattern_t *grad = cairo_pattern_create_linear (0, 0, 0, h);

  if (p->dark)
    {
      cairo_pattern_add_color_stop_rgb (grad, 0.00, 0.32, 0.32, 0.33);
      cairo_pattern_add_color_stop_rgb (grad, 0.10, 0.28, 0.28, 0.29);
      cairo_pattern_add_color_stop_rgb (grad, 0.90, 0.20, 0.20, 0.21);
      cairo_pattern_add_color_stop_rgb (grad, 1.00, 0.16, 0.16, 0.17);
    }
  else
    {
      cairo_pattern_add_color_stop_rgb (grad, 0.00, 0.87, 0.87, 0.88);
      cairo_pattern_add_color_stop_rgb (grad, 0.10, 0.82, 0.82, 0.83);
      cairo_pattern_add_color_stop_rgb (grad, 0.90, 0.72, 0.72, 0.73);
      cairo_pattern_add_color_stop_rgb (grad, 1.00, 0.64, 0.64, 0.65);
    }

  cairo_set_source (cr, grad);
  cairo_rectangle (cr, 0, 0, w, h);
  cairo_fill (cr);
  cairo_pattern_destroy (grad);

  /* fine horizontal brushing */
  cairo_set_line_width (cr, 1);
  for (int y = 2; y < h; y += 3)
    {
      cairo_set_source_rgba (cr, 1, 1, 1, p->dark ? 0.02 : 0.05);
      cairo_move_to (cr, 0, y + 0.5);
      cairo_line_to (cr, w, y + 0.5);
      cairo_stroke (cr);
    }

  /* top edge: light seam under the video screen */
  cairo_set_source_rgba (cr, 0, 0, 0, 0.45);
  cairo_move_to (cr, 0, 0.5);
  cairo_line_to (cr, w, 0.5);
  cairo_stroke (cr);
  cairo_set_source_rgba (cr, 1, 1, 1, p->dark ? 0.10 : 0.55);
  cairo_move_to (cr, 0, 1.5);
  cairo_line_to (cr, w, 1.5);
  cairo_stroke (cr);
}

static void
draw_inset_channel (cairo_t *cr, double x, double y, double w, double h,
                    const OozePalette *p)
{
  double r = h / 2.0;

  cairo_new_path (cr);
  cairo_arc (cr, x + r, y + r, r, G_PI / 2, 3 * G_PI / 2);
  cairo_arc (cr, x + w - r, y + r, r, 3 * G_PI / 2, G_PI / 2);
  cairo_close_path (cr);

  if (p->dark)
    cairo_set_source_rgb (cr, 0.10, 0.10, 0.11);
  else
    cairo_set_source_rgb (cr, 0.52, 0.52, 0.54);
  cairo_fill_preserve (cr);

  /* inner shadow at the top of the groove */
  {
    cairo_pattern_t *sh = cairo_pattern_create_linear (0, y, 0, y + h);
    cairo_pattern_add_color_stop_rgba (sh, 0, 0, 0, 0, 0.35);
    cairo_pattern_add_color_stop_rgba (sh, 0.6, 0, 0, 0, 0.0);
    cairo_set_source (cr, sh);
    cairo_fill_preserve (cr);
    cairo_pattern_destroy (sh);
  }

  cairo_set_line_width (cr, 1);
  cairo_set_source_rgba (cr, 1, 1, 1, p->dark ? 0.08 : 0.5);
  cairo_stroke (cr);
}

static void
draw_gel_ball (cairo_t *cr, double cx, double cy, double radius,
               gboolean hover, gboolean pressed, gboolean enabled,
               const OozePalette *p)
{
  cairo_pattern_t *grad;
  double lift = pressed ? 0.0 : (hover ? 0.06 : 0.0);
  double base = p->dark ? 0.30 : 0.78;

  if (!enabled)
    base = p->dark ? 0.24 : 0.70;
  if (pressed)
    base -= 0.10;
  base += lift;

  /* drop shadow */
  cairo_arc (cr, cx, cy + 1.5, radius, 0, 2 * G_PI);
  cairo_set_source_rgba (cr, 0, 0, 0, 0.30);
  cairo_fill (cr);

  /* ball body: vertical gel gradient */
  grad = cairo_pattern_create_linear (0, cy - radius, 0, cy + radius);
  cairo_pattern_add_color_stop_rgb (grad, 0.0,
                                    base + 0.16, base + 0.16, base + 0.17);
  cairo_pattern_add_color_stop_rgb (grad, 0.45, base, base, base + 0.01);
  cairo_pattern_add_color_stop_rgb (grad, 0.55,
                                    base - 0.08, base - 0.08, base - 0.07);
  cairo_pattern_add_color_stop_rgb (grad, 1.0,
                                    base + 0.05, base + 0.05, base + 0.06);
  cairo_arc (cr, cx, cy, radius, 0, 2 * G_PI);
  cairo_set_source (cr, grad);
  cairo_fill (cr);
  cairo_pattern_destroy (grad);

  /* rim */
  cairo_set_line_width (cr, 1);
  cairo_arc (cr, cx, cy, radius - 0.5, 0, 2 * G_PI);
  cairo_set_source_rgba (cr, 0, 0, 0, p->dark ? 0.55 : 0.35);
  cairo_stroke (cr);

  /* top gloss crescent */
  grad = cairo_pattern_create_linear (0, cy - radius, 0, cy);
  cairo_pattern_add_color_stop_rgba (grad, 0, 1, 1, 1, p->dark ? 0.35 : 0.85);
  cairo_pattern_add_color_stop_rgba (grad, 1, 1, 1, 1, 0.0);
  cairo_save (cr);
  cairo_translate (cr, cx, cy - radius * 0.45);
  cairo_scale (cr, radius * 0.72, radius * 0.42);
  cairo_arc (cr, 0, 0, 1, 0, 2 * G_PI);
  cairo_restore (cr);
  cairo_set_source (cr, grad);
  cairo_fill (cr);
  cairo_pattern_destroy (grad);
}

static void
glyph_color (cairo_t *cr, gboolean enabled, const OozePalette *p)
{
  if (p->dark)
    cairo_set_source_rgba (cr, 0.92, 0.92, 0.94, enabled ? 0.95 : 0.4);
  else
    cairo_set_source_rgba (cr, 0.17, 0.17, 0.19, enabled ? 0.95 : 0.4);
}

static void
draw_triangle (cairo_t *cr, double cx, double cy, double size, int dir)
{
  cairo_move_to (cr, cx - dir * size * 0.45, cy - size * 0.6);
  cairo_line_to (cr, cx + dir * size * 0.55, cy);
  cairo_line_to (cr, cx - dir * size * 0.45, cy + size * 0.6);
  cairo_close_path (cr);
  cairo_fill (cr);
}

static void
draw_glyph_play_pause (cairo_t *cr, double cx, double cy, double r,
                       gboolean paused)
{
  double s = r * 0.44;

  if (paused)
    draw_triangle (cr, cx + r * 0.06, cy, s * 1.35, 1);
  else
    {
      double bw = s * 0.5;
      cairo_rectangle (cr, cx - bw - 1.5, cy - s * 0.8, bw, s * 1.6);
      cairo_rectangle (cr, cx + 1.5, cy - s * 0.8, bw, s * 1.6);
      cairo_fill (cr);
    }
}

static void
draw_glyph_jump (cairo_t *cr, double cx, double cy, double r, int dir)
{
  double s = r * 0.42;

  draw_triangle (cr, cx - dir * s * 0.5, cy, s, dir);
  draw_triangle (cr, cx + dir * s * 0.55, cy, s, dir);
}

static void
draw_glyph_step (cairo_t *cr, double cx, double cy, double r, int dir)
{
  double s = r * 0.5;
  double bx = cx + dir * s * 0.75;

  draw_triangle (cr, cx - dir * s * 0.25, cy, s, dir);
  cairo_rectangle (cr, bx - 1, cy - s * 0.6, 2, s * 1.2);
  cairo_fill (cr);
}

static char *
format_timecode (double secs)
{
  int t = (int) floor (MAX (secs, 0));

  return g_strdup_printf ("%02d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
}

static void
transport_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  OozeWatchTransport *self = OOZE_WATCH_TRANSPORT (widget);
  const OozePalette *p = ooze_palette_current ();
  int w = gtk_widget_get_width (widget);
  int h = gtk_widget_get_height (widget);
  DeckLayout l;
  cairo_t *cr;

  if (w <= 0 || h <= 0)
    return;

  deck_layout (self, &l);
  cr = gtk_snapshot_append_cairo (snapshot,
                                  &GRAPHENE_RECT_INIT (0, 0, w, h));

  draw_brushed_metal (cr, w, h, p);

  /* ── LCD timecode ──────────────────────────────────────────────────── */
  {
    g_autofree char *tc = format_timecode (self->time_pos);
    cairo_text_extents_t ext;

    draw_inset_channel (cr, 12, SCRUB_ROW_Y, LCD_WIDTH, SCRUB_ROW_H, p);
    cairo_select_font_face (cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size (cr, 12);
    cairo_text_extents (cr, tc, &ext);
    if (p->dark)
      cairo_set_source_rgb (cr, 0.62, 0.86, 0.62);
    else
      cairo_set_source_rgb (cr, 0.82, 0.98, 0.82);
    cairo_move_to (cr, 12 + (LCD_WIDTH - ext.width) / 2,
                   SCRUB_ROW_Y + SCRUB_ROW_H / 2 + ext.height / 2 - 1);
    cairo_show_text (cr, tc);
  }

  /* ── Scrub channel ─────────────────────────────────────────────────── */
  {
    double frac = (self->duration > 0)
                    ? CLAMP (self->time_pos / self->duration, 0, 1)
                    : 0;
    double tx;

    draw_inset_channel (cr, l.scrub_x, SCRUB_ROW_Y, l.scrub_w,
                        SCRUB_ROW_H, p);

    if (self->has_media)
      {
        /* progress fill */
        double fw = (l.scrub_w - 8) * frac;

        if (fw > 1)
          {
            cairo_rectangle (cr, l.scrub_x + 4,
                             SCRUB_ROW_Y + 6, fw, SCRUB_ROW_H - 12);
            cairo_set_source_rgba (cr, p->accent_r, p->accent_g,
                                   p->accent_b, 0.85);
            cairo_fill (cr);
          }

        /* QuickTime triangle thumb riding the channel */
        tx = l.scrub_x + 4 + fw;
        cairo_move_to (cr, tx - 5, SCRUB_ROW_Y + SCRUB_ROW_H - 3);
        cairo_line_to (cr, tx + 5, SCRUB_ROW_Y + SCRUB_ROW_H - 3);
        cairo_line_to (cr, tx, SCRUB_ROW_Y + 3);
        cairo_close_path (cr);
        cairo_set_source_rgb (cr, p->dark ? 0.85 : 0.95,
                              p->dark ? 0.85 : 0.95,
                              p->dark ? 0.87 : 0.97);
        cairo_fill_preserve (cr);
        cairo_set_line_width (cr, 1);
        cairo_set_source_rgba (cr, 0, 0, 0, 0.5);
        cairo_stroke (cr);
      }
  }

  /* ── Volume: speaker + thumbwheel slider ───────────────────────────── */
  {
    double vy = BUTTON_ROW_CY;
    double vx = l.vol_x;

    glyph_color (cr, TRUE, p);
    /* small speaker */
    cairo_move_to (cr, vx, vy - 3);
    cairo_line_to (cr, vx + 4, vy - 3);
    cairo_line_to (cr, vx + 9, vy - 7);
    cairo_line_to (cr, vx + 9, vy + 7);
    cairo_line_to (cr, vx + 4, vy + 3);
    cairo_line_to (cr, vx, vy + 3);
    cairo_close_path (cr);
    cairo_fill (cr);

    draw_inset_channel (cr, vx + 14, vy - 4, l.vol_w, 8, p);

    /* volume knob */
    {
      double kx = vx + 14 + 4 + (l.vol_w - 8) * CLAMP (self->volume, 0, 1);

      draw_gel_ball (cr, kx, vy, 7,
                     self->hover == HIT_VOLUME,
                     self->pressed == HIT_VOLUME, TRUE, p);
    }
  }

  /* ── Round transport buttons (shrinking from the middle) ───────────── */
  {
    TransportHit hits[5] = { HIT_STEP_BACK, HIT_JUMP_BACK, HIT_PLAY,
                             HIT_JUMP_FWD, HIT_STEP_FWD };

    for (int i = 0; i < 5; i++)
      {
        gboolean enabled = self->has_media || hits[i] == HIT_PLAY;

        draw_gel_ball (cr, l.cx[i], BUTTON_ROW_CY, l.r[i],
                       self->hover == hits[i],
                       self->pressed == hits[i], enabled, p);
        glyph_color (cr, enabled, p);
        switch (hits[i])
          {
          case HIT_PLAY:
            draw_glyph_play_pause (cr, l.cx[i], BUTTON_ROW_CY, l.r[i],
                                   self->paused);
            break;
          case HIT_JUMP_BACK:
            draw_glyph_jump (cr, l.cx[i], BUTTON_ROW_CY, l.r[i], -1);
            break;
          case HIT_JUMP_FWD:
            draw_glyph_jump (cr, l.cx[i], BUTTON_ROW_CY, l.r[i], 1);
            break;
          case HIT_STEP_BACK:
            draw_glyph_step (cr, l.cx[i], BUTTON_ROW_CY, l.r[i], -1);
            break;
          case HIT_STEP_FWD:
            draw_glyph_step (cr, l.cx[i], BUTTON_ROW_CY, l.r[i], 1);
            break;
          default:
            break;
          }
      }
  }

  cairo_destroy (cr);
}

/* ── Input ────────────────────────────────────────────────────────────── */

static TransportHit
transport_hit (OozeWatchTransport *self, double x, double y)
{
  DeckLayout l;
  TransportHit hits[5] = { HIT_STEP_BACK, HIT_JUMP_BACK, HIT_PLAY,
                           HIT_JUMP_FWD, HIT_STEP_FWD };

  deck_layout (self, &l);

  for (int i = 0; i < 5; i++)
    {
      double dx = x - l.cx[i];
      double dy = y - BUTTON_ROW_CY;

      if (dx * dx + dy * dy <= (l.r[i] + 2) * (l.r[i] + 2))
        return hits[i];
    }

  if (y >= SCRUB_ROW_Y - 4 && y <= SCRUB_ROW_Y + SCRUB_ROW_H + 4 &&
      x >= l.scrub_x && x <= l.scrub_x + l.scrub_w)
    return HIT_SCRUB;

  if (y >= BUTTON_ROW_CY - 10 && y <= BUTTON_ROW_CY + 10 &&
      x >= l.vol_x + 14 - 4 && x <= l.vol_x + 14 + l.vol_w + 4)
    return HIT_VOLUME;

  return HIT_NONE;
}

static double
transport_scrub_frac (OozeWatchTransport *self, double x)
{
  DeckLayout l;

  deck_layout (self, &l);
  return CLAMP ((x - l.scrub_x - 4) / (l.scrub_w - 8), 0.0, 1.0);
}

static double
transport_volume_frac (OozeWatchTransport *self, double x)
{
  DeckLayout l;

  deck_layout (self, &l);
  return CLAMP ((x - l.vol_x - 14 - 4) / (l.vol_w - 8), 0.0, 1.0);
}

static void
on_press (GtkGestureClick *gesture G_GNUC_UNUSED, int n_press G_GNUC_UNUSED,
          double x, double y, gpointer user_data)
{
  OozeWatchTransport *self = user_data;

  self->pressed = transport_hit (self, x, y);

  if (self->pressed == HIT_SCRUB)
    {
      self->scrubbing = TRUE;
      g_signal_emit (self, signals[SIGNAL_SEEK_FRAC], 0,
                     transport_scrub_frac (self, x));
    }
  else if (self->pressed == HIT_VOLUME)
    {
      self->volume = transport_volume_frac (self, x);
      g_signal_emit (self, signals[SIGNAL_VOLUME], 0, self->volume);
    }

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
on_release (GtkGestureClick *gesture G_GNUC_UNUSED, int n_press G_GNUC_UNUSED,
            double x, double y, gpointer user_data)
{
  OozeWatchTransport *self = user_data;
  TransportHit hit = transport_hit (self, x, y);

  if (hit == self->pressed)
    switch (hit)
      {
      case HIT_PLAY:
        g_signal_emit (self, signals[SIGNAL_PLAY_TOGGLED], 0);
        break;
      case HIT_JUMP_BACK:
        g_signal_emit (self, signals[SIGNAL_JUMP], 0, -1);
        break;
      case HIT_JUMP_FWD:
        g_signal_emit (self, signals[SIGNAL_JUMP], 0, 1);
        break;
      case HIT_STEP_BACK:
        g_signal_emit (self, signals[SIGNAL_STEP], 0, -1);
        break;
      case HIT_STEP_FWD:
        g_signal_emit (self, signals[SIGNAL_STEP], 0, 1);
        break;
      default:
        break;
      }

  self->pressed = HIT_NONE;
  self->scrubbing = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
on_motion (GtkEventControllerMotion *ctl G_GNUC_UNUSED,
           double x, double y, gpointer user_data)
{
  OozeWatchTransport *self = user_data;
  TransportHit hit = transport_hit (self, x, y);

  if (self->scrubbing)
    g_signal_emit (self, signals[SIGNAL_SEEK_FRAC], 0,
                   transport_scrub_frac (self, x));
  else if (self->pressed == HIT_VOLUME)
    {
      self->volume = transport_volume_frac (self, x);
      g_signal_emit (self, signals[SIGNAL_VOLUME], 0, self->volume);
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }

  if (hit != self->hover)
    {
      self->hover = hit;
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
on_leave (GtkEventControllerMotion *ctl G_GNUC_UNUSED, gpointer user_data)
{
  OozeWatchTransport *self = user_data;

  if (self->hover != HIT_NONE)
    {
      self->hover = HIT_NONE;
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
transport_measure (GtkWidget *widget G_GNUC_UNUSED,
                   GtkOrientation orientation,
                   int for_size G_GNUC_UNUSED,
                   int *minimum, int *natural,
                   int *minimum_baseline, int *natural_baseline)
{
  if (orientation == GTK_ORIENTATION_VERTICAL)
    *minimum = *natural = DECK_HEIGHT;
  else
    *minimum = *natural = 420;
  *minimum_baseline = *natural_baseline = -1;
}

static void
ooze_watch_transport_class_init (OozeWatchTransportClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->snapshot = transport_snapshot;
  widget_class->measure = transport_measure;

  signals[SIGNAL_PLAY_TOGGLED] =
    g_signal_new ("play-toggled", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[SIGNAL_SEEK_FRAC] =
    g_signal_new ("seek-frac", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_DOUBLE);
  signals[SIGNAL_VOLUME] =
    g_signal_new ("volume", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_DOUBLE);
  signals[SIGNAL_STEP] =
    g_signal_new ("step", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_INT);
  signals[SIGNAL_JUMP] =
    g_signal_new ("jump", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
                  G_TYPE_NONE, 1, G_TYPE_INT);
}

static void
ooze_watch_transport_init (OozeWatchTransport *self)
{
  GtkGesture *click;
  GtkEventController *motion;

  self->volume = 1.0;
  self->paused = TRUE;

  gtk_widget_set_hexpand (GTK_WIDGET (self), TRUE);

  click = gtk_gesture_click_new ();
  g_signal_connect (click, "pressed", G_CALLBACK (on_press), self);
  g_signal_connect (click, "released", G_CALLBACK (on_release), self);
  gtk_widget_add_controller (GTK_WIDGET (self),
                             GTK_EVENT_CONTROLLER (click));

  motion = gtk_event_controller_motion_new ();
  g_signal_connect (motion, "motion", G_CALLBACK (on_motion), self);
  g_signal_connect (motion, "leave", G_CALLBACK (on_leave), self);
  gtk_widget_add_controller (GTK_WIDGET (self), motion);
}

GtkWidget *
ooze_watch_transport_new (void)
{
  return g_object_new (OOZE_WATCH_TYPE_TRANSPORT, NULL);
}

void
ooze_watch_transport_set_state (OozeWatchTransport *self,
                                double              time_pos,
                                double              duration,
                                gboolean            paused,
                                double              volume01,
                                gboolean            has_media)
{
  g_return_if_fail (OOZE_WATCH_IS_TRANSPORT (self));

  self->time_pos = time_pos;
  self->duration = duration;
  self->paused = paused;
  if (self->pressed != HIT_VOLUME)
    self->volume = volume01;
  self->has_media = has_media;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}
