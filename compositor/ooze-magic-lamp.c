/*
 * Mesh-warping magic-lamp minimize/unminimize.
 *
 * Deformation math adapted from Mauro Pepe's Compiz-alike Magic Lamp
 * GNOME Shell extension (GPL-3.0):
 *   https://github.com/hermes83/compiz-alike-magic-lamp-effect
 */

#include "ooze-magic-lamp.h"

#include <math.h>
#include <meta/display.h>
#include <meta/window.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAGIC_LAMP_EFFECT_NAME "ooze-magic-lamp"
#define MAGIC_LAMP_DURATION_MS 500
#define MAGIC_LAMP_X_TILES     15
#define MAGIC_LAMP_Y_TILES     20
#define MAGIC_LAMP_SPLIT       0.3
#define MAGIC_LAMP_EPSILON     40.0f

typedef enum {
  OOZE_LAMP_SIDE_BOTTOM = 0,
  OOZE_LAMP_SIDE_TOP,
  OOZE_LAMP_SIDE_LEFT,
  OOZE_LAMP_SIDE_RIGHT,
} OozeLampSide;

G_DECLARE_FINAL_TYPE (OozeMagicLampEffect,
                      ooze_magic_lamp_effect,
                      OOZE,
                      MAGIC_LAMP_EFFECT,
                      ClutterDeformEffect)

struct _OozeMagicLampEffect
{
  ClutterDeformEffect parent_instance;

  MetaPlugin *plugin;
  gboolean unminimize;
  OozeMagicLampDoneFunc done;
  gpointer user_data;

  ClutterTimeline *timeline;
  gulong new_frame_id;
  gulong completed_id;
  guint watchdog_id;

  gboolean initialized;
  gboolean finishing;

  gfloat monitor_x;
  gfloat monitor_y;
  gfloat monitor_w;
  gfloat monitor_h;

  gfloat window_x;
  gfloat window_y;
  gfloat window_w;
  gfloat window_h;

  gfloat icon_x;
  gfloat icon_y;
  gfloat icon_w;
  gfloat icon_h;

  OozeLampSide icon_side;
  gdouble progress;
  gdouble k;
  gdouble j;
  gdouble split;
};

G_DEFINE_TYPE (OozeMagicLampEffect, ooze_magic_lamp_effect, CLUTTER_TYPE_DEFORM_EFFECT)

static void
ooze_magic_lamp_effect_finish (OozeMagicLampEffect *self,
                             gboolean           notify)
{
  ClutterActor *actor;
  MetaWindowActor *window_actor;
  MetaPlugin *plugin;
  gboolean unminimize;
  OozeMagicLampDoneFunc done;
  gpointer user_data;

  if (self->finishing)
    return;
  self->finishing = TRUE;

  if (self->watchdog_id)
    {
      g_source_remove (self->watchdog_id);
      self->watchdog_id = 0;
    }

  if (self->timeline)
    {
      g_clear_signal_handler (&self->new_frame_id, self->timeline);
      g_clear_signal_handler (&self->completed_id, self->timeline);
      clutter_timeline_stop (self->timeline);
      g_clear_object (&self->timeline);
    }

  actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  window_actor = actor ? META_WINDOW_ACTOR (actor) : NULL;
  plugin = self->plugin;
  unminimize = self->unminimize;
  done = self->done;
  user_data = self->user_data;
  /* Clear before notify so dispose cannot double-complete. */
  self->done = NULL;
  self->user_data = NULL;

  /* Notify before remove_effect — removing may dispose this object. */
  if (notify && done)
    done (plugin, window_actor, unminimize, user_data);

  if (actor)
    clutter_actor_remove_effect (actor, CLUTTER_EFFECT (self));
}

static gboolean
ooze_magic_lamp_effect_watchdog (gpointer user_data)
{
  OozeMagicLampEffect *self = user_data;

  self->watchdog_id = 0;
  g_warning ("Ooze magic-lamp: animation watchdog fired — forcing complete");
  ooze_magic_lamp_effect_finish (self, TRUE);
  return G_SOURCE_REMOVE;
}

static void
ooze_magic_lamp_effect_on_completed (ClutterTimeline   *timeline G_GNUC_UNUSED,
                                   OozeMagicLampEffect *self)
{
  ooze_magic_lamp_effect_finish (self, TRUE);
}

static void
ooze_magic_lamp_effect_on_new_frame (ClutterTimeline   *timeline,
                                   gint               msecs G_GNUC_UNUSED,
                                   OozeMagicLampEffect *self)
{
  ClutterActor *actor;
  ClutterActor *parent;

  self->progress = clutter_timeline_get_progress (timeline);

  if (self->unminimize)
    {
      /* Reverse of minimize: j shrinks first, then k. */
      self->k = 1.0 - (self->progress > (1.0 - self->split)
                       ? (self->progress - (1.0 - self->split)) / self->split
                       : 0.0);
      self->j = 1.0 - (self->progress <= (1.0 - self->split)
                       ? self->progress / (1.0 - self->split)
                       : 1.0);
    }
  else
    {
      /* Phase 1 (k): stretch toward dock. Phase 2 (j): collapse into icon. */
      self->k = self->progress <= self->split
                ? self->progress / self->split
                : 1.0;
      self->j = self->progress > self->split
                ? (self->progress - self->split) / (1.0 - self->split)
                : 0.0;
    }

  actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (self));
  if (actor)
    {
      parent = clutter_actor_get_parent (actor);
      if (parent)
        clutter_actor_queue_redraw (parent);
    }

  clutter_deform_effect_invalidate (CLUTTER_DEFORM_EFFECT (self));
}

static void
ooze_magic_lamp_effect_init_geometry (OozeMagicLampEffect *self,
                                    ClutterActor      *actor)
{
  MetaWindowActor *window_actor = META_WINDOW_ACTOR (actor);
  MetaWindow *window;
  MetaDisplay *display;
  int screen_w = 1280, screen_h = 800;

  window = meta_window_actor_get_meta_window (window_actor);
  display = window ? meta_window_get_display (window) : NULL;
  if (display)
    meta_display_get_size (display, &screen_w, &screen_h);

  self->monitor_x = 0.f;
  self->monitor_y = 0.f;
  self->monitor_w = (gfloat) screen_w;
  self->monitor_h = (gfloat) screen_h;

  self->window_x = clutter_actor_get_x (actor) - self->monitor_x;
  self->window_y = clutter_actor_get_y (actor) - self->monitor_y;
  self->window_w = clutter_actor_get_width (actor);
  self->window_h = clutter_actor_get_height (actor);
  if (self->window_w < 1.f)
    self->window_w = 1.f;
  if (self->window_h < 1.f)
    self->window_h = 1.f;

  self->icon_x -= self->monitor_x;
  self->icon_y -= self->monitor_y;

  if (self->icon_w < 1.f && self->icon_h < 1.f &&
      self->icon_x <= 0.f && self->icon_y <= 0.f)
    {
      self->icon_x = self->monitor_w / 2.f;
      self->icon_y = self->monitor_h;
      self->icon_w = 0.f;
      self->icon_h = 0.f;
    }

  /* Prefer bottom dock (Aqua). Icons slightly above the edge still count —
   * otherwise a raised shelf falls through to TOP and minimizes upward. */
  if (self->icon_y + self->icon_h >= self->monitor_h - MAGIC_LAMP_EPSILON ||
      self->icon_y + self->icon_h * 0.5f >= self->monitor_h * 0.55f)
    {
      self->icon_side = OOZE_LAMP_SIDE_BOTTOM;
      self->icon_y = self->monitor_h;
      self->icon_h = 0.f;
    }
  else if (self->icon_x <= MAGIC_LAMP_EPSILON)
    {
      self->icon_side = OOZE_LAMP_SIDE_LEFT;
      self->icon_x = 0.f;
      self->icon_w = 0.f;
    }
  else if (self->icon_x + self->icon_w >= self->monitor_w - MAGIC_LAMP_EPSILON)
    {
      self->icon_side = OOZE_LAMP_SIDE_RIGHT;
      self->icon_x = self->monitor_w;
      self->icon_w = 0.f;
    }
  else if (self->icon_y <= MAGIC_LAMP_EPSILON)
    {
      self->icon_side = OOZE_LAMP_SIDE_TOP;
      self->icon_y = 0.f;
      self->icon_h = 0.f;
    }
  else
    {
      /* Default Aqua: suck toward the bottom shelf. */
      self->icon_side = OOZE_LAMP_SIDE_BOTTOM;
      self->icon_y = self->monitor_h;
      self->icon_h = 0.f;
    }
}

static void
ooze_magic_lamp_effect_set_actor (ClutterActorMeta *meta,
                                ClutterActor     *actor)
{
  OozeMagicLampEffect *self = OOZE_MAGIC_LAMP_EFFECT (meta);
  guint duration;
  gdouble area_factor;

  CLUTTER_ACTOR_META_CLASS (ooze_magic_lamp_effect_parent_class)->set_actor (meta, actor);

  if (!actor || self->initialized)
    return;

  self->initialized = TRUE;
  ooze_magic_lamp_effect_init_geometry (self, actor);

  clutter_deform_effect_set_n_tiles (CLUTTER_DEFORM_EFFECT (self),
                                     MAGIC_LAMP_X_TILES,
                                     MAGIC_LAMP_Y_TILES);

  area_factor = (self->monitor_w * self->monitor_h) /
                (self->window_w * self->window_h);
  duration = MAGIC_LAMP_DURATION_MS + (guint) CLAMP (area_factor, 0.0, 200.0);

  self->timeline = clutter_timeline_new_for_actor (actor, duration);
  self->new_frame_id =
    g_signal_connect (self->timeline, "new-frame",
                      G_CALLBACK (ooze_magic_lamp_effect_on_new_frame), self);
  self->completed_id =
    g_signal_connect (self->timeline, "completed",
                      G_CALLBACK (ooze_magic_lamp_effect_on_completed), self);
  clutter_timeline_start (self->timeline);

  /* If the timeline stalls (blocked main loop / actor quirks), still
   * release Mutter's minimize/unminimize wait. */
  self->watchdog_id =
    g_timeout_add (duration + 750, ooze_magic_lamp_effect_watchdog, self);
}

static gboolean
ooze_magic_lamp_effect_modify_paint_volume (ClutterEffect      *effect G_GNUC_UNUSED,
                                          ClutterPaintVolume *volume G_GNUC_UNUSED)
{
  /* Full-stage redraw while warping — volume would clip the stretched mesh. */
  return FALSE;
}

static void
ooze_magic_lamp_effect_deform_vertex (ClutterDeformEffect  *effect,
                                    gfloat                width,
                                    gfloat                height,
                                    ClutterTextureVertex *v)
{
  OozeMagicLampEffect *self = OOZE_MAGIC_LAMP_EFFECT (effect);
  gfloat prop_x, prop_y;
  gfloat x = 0.f, y = 0.f;
  gfloat offset_x = 0.f, offset_y = 0.f;
  gfloat effect_x = 0.f, effect_y = 0.f;
  gfloat expand_h, full_h, h;
  gfloat expand_w, full_w, w;
  gdouble k = self->k;
  gdouble j = self->j;

  if (!self->initialized)
    return;

  prop_x = width / self->window_w;
  prop_y = height / self->window_h;

  switch (self->icon_side)
    {
    case OOZE_LAMP_SIDE_BOTTOM:
      expand_h = (self->monitor_h - self->icon_h - self->window_y - self->window_h);
      full_h = (self->monitor_h - self->icon_h - self->window_y) - expand_h * (1.f - (gfloat) k);
      h = full_h - (gfloat) j * full_h;

      y = v->ty * h;
      x = v->tx * self->icon_w +
          v->tx * (self->window_w - self->icon_w) * (1.f - (gfloat) j) * (1.f - v->ty) +
          v->tx * (self->window_w - self->icon_w) * (1.f - (gfloat) k) * v->ty;

      offset_x = (self->icon_x - self->window_x) * (y / MAX (full_h, 1.f)) * (gfloat) k +
                 (self->icon_x - self->window_x) * (gfloat) j;
      offset_y = self->monitor_h - self->icon_h - self->window_y - h - expand_h * (1.f - (gfloat) k);

      effect_x = sinf (((h - y) / MAX (full_h, 1.f)) * 2.f * (gfloat) M_PI + (gfloat) M_PI) *
                 (self->window_x + self->window_w * v->tx -
                  (self->icon_x + self->icon_w * v->tx)) / 7.f * (gfloat) k;
      break;

    case OOZE_LAMP_SIDE_TOP:
      h = self->window_h - self->icon_h + self->window_y * (gfloat) k;

      y = (h - (gfloat) j * h) * v->ty;
      x = v->tx * self->window_w * (y + (h - y) * (1.f - (gfloat) k)) / MAX (h, 1.f) +
          v->tx * self->icon_w * (h - y) / MAX (h, 1.f);

      offset_x = (self->icon_x - self->window_x) * ((h - y) / MAX (h, 1.f)) * (gfloat) k;
      offset_y = self->icon_h - self->window_y * (gfloat) k;

      effect_x = sinf ((0.5f - (h - y) / MAX (h, 1.f)) * 2.f * (gfloat) M_PI) *
                 (self->window_x + self->window_w * v->tx -
                  (self->icon_x + self->icon_w * v->tx)) / 7.f * (gfloat) k;
      break;

    case OOZE_LAMP_SIDE_LEFT:
      w = self->window_w - self->icon_w + self->window_x * (gfloat) k;

      x = (w - (gfloat) j * w) * v->tx;
      y = v->ty * self->window_h * (x + (w - x) * (1.f - (gfloat) k)) / MAX (w, 1.f) +
          v->ty * self->icon_h * (w - x) / MAX (w, 1.f);

      offset_x = self->icon_w - self->window_x * (gfloat) k;
      offset_y = (self->icon_y - self->window_y) * ((w - x) / MAX (w, 1.f)) * (gfloat) k;

      effect_y = sinf ((0.5f - (w - x) / MAX (w, 1.f)) * 2.f * (gfloat) M_PI) *
                 (self->window_y + self->window_h * v->ty -
                  (self->icon_y + self->icon_h * v->ty)) / 7.f * (gfloat) k;
      break;

    case OOZE_LAMP_SIDE_RIGHT:
      expand_w = (self->monitor_w - self->icon_w - self->window_x - self->window_w);
      full_w = (self->monitor_w - self->icon_w - self->window_x) - expand_w * (1.f - (gfloat) k);
      w = full_w - (gfloat) j * full_w;

      x = v->tx * w;
      y = v->ty * self->icon_h +
          v->ty * (self->window_h - self->icon_h) * (1.f - (gfloat) j) * (1.f - v->tx) +
          v->ty * (self->window_h - self->icon_h) * (1.f - (gfloat) k) * v->tx;

      offset_y = (self->icon_y - self->window_y) * (x / MAX (full_w, 1.f)) * (gfloat) k +
                 (self->icon_y - self->window_y) * (gfloat) j;
      offset_x = self->monitor_w - self->icon_w - self->window_x - w - expand_w * (1.f - (gfloat) k);

      effect_y = sinf (((w - x) / MAX (full_w, 1.f)) * 2.f * (gfloat) M_PI + (gfloat) M_PI) *
                 (self->window_y + self->window_h * v->ty -
                  (self->icon_y + self->icon_h * v->ty)) / 7.f * (gfloat) k;
      break;
    }

  v->x = (x + offset_x + effect_x) * prop_x;
  v->y = (y + offset_y + effect_y) * prop_y;
}

static void
ooze_magic_lamp_effect_dispose (GObject *object)
{
  OozeMagicLampEffect *self = OOZE_MAGIC_LAMP_EFFECT (object);

  /* If animation was torn down without completing, still notify Mutter. */
  ooze_magic_lamp_effect_finish (self, TRUE);

  G_OBJECT_CLASS (ooze_magic_lamp_effect_parent_class)->dispose (object);
}

static void
ooze_magic_lamp_effect_class_init (OozeMagicLampEffectClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorMetaClass *meta_class = CLUTTER_ACTOR_META_CLASS (klass);
  ClutterEffectClass *effect_class = CLUTTER_EFFECT_CLASS (klass);
  ClutterDeformEffectClass *deform_class = CLUTTER_DEFORM_EFFECT_CLASS (klass);

  object_class->dispose = ooze_magic_lamp_effect_dispose;
  meta_class->set_actor = ooze_magic_lamp_effect_set_actor;
  effect_class->modify_paint_volume = ooze_magic_lamp_effect_modify_paint_volume;
  deform_class->deform_vertex = ooze_magic_lamp_effect_deform_vertex;
}

static void
ooze_magic_lamp_effect_init (OozeMagicLampEffect *self)
{
  self->split = MAGIC_LAMP_SPLIT;
  self->k = 0.0;
  self->j = 0.0;
}

static OozeMagicLampEffect *
ooze_magic_lamp_effect_new (MetaPlugin         *plugin,
                          gboolean            unminimize,
                          gfloat              icon_x,
                          gfloat              icon_y,
                          gfloat              icon_w,
                          gfloat              icon_h,
                          OozeMagicLampDoneFunc done,
                          gpointer            user_data)
{
  OozeMagicLampEffect *self;

  self = g_object_new (ooze_magic_lamp_effect_get_type (), NULL);
  self->plugin = plugin;
  self->unminimize = unminimize;
  self->done = done;
  self->user_data = user_data;
  self->icon_x = icon_x;
  self->icon_y = icon_y;
  self->icon_w = MAX (0.f, icon_w);
  self->icon_h = MAX (0.f, icon_h);

  if (unminimize)
    {
      self->k = 1.0;
      self->j = 1.0;
    }

  return self;
}

void
ooze_magic_lamp_cancel (MetaWindowActor *actor)
{
  ClutterEffect *effect;

  if (!actor)
    return;

  effect = clutter_actor_get_effect (CLUTTER_ACTOR (actor), MAGIC_LAMP_EFFECT_NAME);
  if (effect)
    ooze_magic_lamp_effect_finish (OOZE_MAGIC_LAMP_EFFECT (effect), TRUE);
}

void
ooze_magic_lamp_run (MetaPlugin         *plugin,
                   MetaWindowActor    *actor,
                   gboolean            unminimize,
                   gfloat              icon_x,
                   gfloat              icon_y,
                   gfloat              icon_w,
                   gfloat              icon_h,
                   OozeMagicLampDoneFunc done,
                   gpointer            user_data)
{
  OozeMagicLampEffect *effect;
  ClutterEffect *existing;

  if (!actor || meta_window_actor_is_destroyed (actor))
    {
      if (done)
        done (plugin, actor, unminimize, user_data);
      return;
    }

  existing = clutter_actor_get_effect (CLUTTER_ACTOR (actor), MAGIC_LAMP_EFFECT_NAME);
  if (existing)
    {
      OozeMagicLampEffect *old = OOZE_MAGIC_LAMP_EFFECT (existing);

      /*
       * Reversing direction must complete the in-flight op first, or Mutter
       * never gets minimize_completed and the window stays stuck (Spot
       * "won't go away"). Same-direction restarts can abandon quietly —
       * the new effect owns the completion.
       */
      ooze_magic_lamp_effect_finish (old, old->unminimize != unminimize);
    }

  if (unminimize)
    clutter_actor_show (CLUTTER_ACTOR (actor));

  effect = ooze_magic_lamp_effect_new (plugin, unminimize,
                                     icon_x, icon_y, icon_w, icon_h,
                                     done, user_data);
  clutter_actor_add_effect_with_name (CLUTTER_ACTOR (actor),
                                      MAGIC_LAMP_EFFECT_NAME,
                                      CLUTTER_EFFECT (effect));
}
