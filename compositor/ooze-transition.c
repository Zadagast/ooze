#include "ooze-transition.h"
#include "ooze-theme.h"

#include <meta/compositor.h>
#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>

/*
 * Opaque color fade — never clutter_stage_paint_to_content(). A full-stage
 * snapshot blocks the main thread long enough that dock/desktop launches
 * appear frozen after Light↔Dark.
 */
#define SCREEN_TRANSITION_DELAY_MS    60
#define SCREEN_TRANSITION_DURATION_MS 420

typedef struct
{
  ClutterActor *overlay;
  guint         cleanup_id;
} OozeScreenTransition;

static OozeScreenTransition *active_transition;

static void
ooze_screen_transition_cleanup (OozeScreenTransition *st)
{
  if (!st)
    return;

  if (st->cleanup_id)
    {
      g_source_remove (st->cleanup_id);
      st->cleanup_id = 0;
    }

  if (st->overlay)
    {
      clutter_actor_remove_all_transitions (st->overlay);
      clutter_actor_destroy (st->overlay);
      st->overlay = NULL;
    }

  if (active_transition == st)
    active_transition = NULL;

  g_free (st);
}

static gboolean
ooze_screen_transition_on_finished (gpointer user_data)
{
  OozeScreenTransition *st = user_data;

  st->cleanup_id = 0;
  ooze_screen_transition_cleanup (st);
  return G_SOURCE_REMOVE;
}

static void
ooze_screen_transition_start_fade (OozeScreenTransition *st)
{
  clutter_actor_save_easing_state (st->overlay);
  clutter_actor_set_easing_mode (st->overlay, CLUTTER_EASE_IN_OUT_CUBIC);
  clutter_actor_set_easing_delay (st->overlay, SCREEN_TRANSITION_DELAY_MS);
  clutter_actor_set_easing_duration (st->overlay, SCREEN_TRANSITION_DURATION_MS);
  clutter_actor_set_opacity (st->overlay, 0);
  clutter_actor_restore_easing_state (st->overlay);

  st->cleanup_id =
    g_timeout_add (SCREEN_TRANSITION_DELAY_MS + SCREEN_TRANSITION_DURATION_MS + 50,
                   ooze_screen_transition_on_finished,
                   st);
}

void
ooze_screen_transition_run (MetaPlugin *plugin)
{
  MetaDisplay *display;
  MetaCompositor *compositor;
  MetaContext *context;
  MetaBackend *backend;
  ClutterActor *stage_actor;
  ClutterActor *overlay;
  OozeScreenTransition *st;
  const OozeAquaPalette *palette;
  CoglColor color;
  gfloat width;
  gfloat height;

  g_return_if_fail (META_IS_PLUGIN (plugin));

  if (active_transition)
    ooze_screen_transition_cleanup (active_transition);

  display = meta_plugin_get_display (plugin);
  if (!display)
    return;

  compositor = meta_display_get_compositor (display);
  stage_actor = compositor
    ? CLUTTER_ACTOR (meta_compositor_get_stage (compositor))
    : NULL;

  if (!stage_actor)
    {
      context = meta_display_get_context (display);
      backend = context ? meta_context_get_backend (context) : NULL;
      stage_actor = backend
        ? CLUTTER_ACTOR (meta_backend_get_stage (backend))
        : NULL;
    }

  if (!CLUTTER_IS_STAGE (stage_actor))
    return;

  width = clutter_actor_get_width (stage_actor);
  height = clutter_actor_get_height (stage_actor);
  if (width < 1.0f || height < 1.0f)
    return;

  /* will_change fires before palette flips — cover with the outgoing look. */
  palette = ooze_theme_get_palette (NULL);
  cogl_color_init_from_4f (&color,
                           (float) palette->wallpaper_mid_r,
                           (float) palette->wallpaper_mid_g,
                           (float) palette->wallpaper_mid_b,
                           1.0f);

  overlay = clutter_actor_new ();
  clutter_actor_set_size (overlay, width, height);
  clutter_actor_set_position (overlay, 0.0f, 0.0f);
  clutter_actor_set_reactive (overlay, FALSE);
  clutter_actor_set_background_color (overlay, &color);
  clutter_actor_set_opacity (overlay, 255);

  clutter_actor_add_child (stage_actor, overlay);
  clutter_actor_set_child_above_sibling (stage_actor, overlay, NULL);

  st = g_new0 (OozeScreenTransition, 1);
  st->overlay = overlay;
  active_transition = st;

  /* Theme refresh runs next under this overlay. */
  ooze_screen_transition_start_fade (st);
}
