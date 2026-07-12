#include "my-screen-transition.h"

#include <meta/compositor.h>
#include <meta/display.h>
#include <meta/meta-backend.h>
#include <meta/meta-context.h>
#include <mtk/mtk.h>

/* Slower than GNOME Shell — hold briefly so apps restyle, then a soft fade. */
#define SCREEN_TRANSITION_DELAY_MS    180
#define SCREEN_TRANSITION_DURATION_MS 1100

typedef struct
{
  ClutterActor *overlay;
  guint         cleanup_id;
} MyScreenTransition;

static MyScreenTransition *active_transition;

static void
my_screen_transition_cleanup (MyScreenTransition *st)
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
my_screen_transition_on_finished (gpointer user_data)
{
  MyScreenTransition *st = user_data;

  st->cleanup_id = 0;
  my_screen_transition_cleanup (st);
  return G_SOURCE_REMOVE;
}

static void
my_screen_transition_start_fade (MyScreenTransition *st)
{
  clutter_actor_save_easing_state (st->overlay);
  clutter_actor_set_easing_mode (st->overlay, CLUTTER_EASE_IN_OUT_CUBIC);
  clutter_actor_set_easing_delay (st->overlay, SCREEN_TRANSITION_DELAY_MS);
  clutter_actor_set_easing_duration (st->overlay, SCREEN_TRANSITION_DURATION_MS);
  clutter_actor_set_opacity (st->overlay, 0);
  clutter_actor_restore_easing_state (st->overlay);

  st->cleanup_id =
    g_timeout_add (SCREEN_TRANSITION_DELAY_MS + SCREEN_TRANSITION_DURATION_MS + 50,
                   my_screen_transition_on_finished,
                   st);
}

void
my_screen_transition_run (MetaPlugin *plugin)
{
  MetaDisplay *display;
  MetaCompositor *compositor;
  MetaContext *context;
  MetaBackend *backend;
  ClutterActor *stage_actor;
  ClutterStage *stage;
  ClutterContent *content = NULL;
  ClutterActor *overlay;
  MyScreenTransition *st;
  MtkRectangle rect;
  gfloat width;
  gfloat height;
  float scale;
  g_autoptr (GError) error = NULL;

  g_return_if_fail (META_IS_PLUGIN (plugin));

  if (active_transition)
    my_screen_transition_cleanup (active_transition);

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

  stage = CLUTTER_STAGE (stage_actor);
  width = clutter_actor_get_width (stage_actor);
  height = clutter_actor_get_height (stage_actor);
  if (width < 1.0f || height < 1.0f)
    return;

  scale = clutter_actor_get_resource_scale (stage_actor);
  if (scale < 1.0f)
    scale = 1.0f;

  rect = MTK_RECTANGLE_INIT (0, 0, (int) width, (int) height);
  content = clutter_stage_paint_to_content (stage,
                                            &rect,
                                            scale,
                                            clutter_actor_get_color_state (stage_actor),
                                            CLUTTER_PAINT_FLAG_NO_CURSORS,
                                            &error);
  if (!content)
    {
      g_warning ("Ooze: screen transition snapshot failed: %s",
                 error ? error->message : "unknown");
      return;
    }

  overlay = clutter_actor_new ();
  clutter_actor_set_size (overlay, width, height);
  clutter_actor_set_position (overlay, 0.0f, 0.0f);
  clutter_actor_set_reactive (overlay, FALSE);
  clutter_actor_set_content (overlay, content);
  clutter_actor_set_opacity (overlay, 255);
  g_object_unref (content);

  clutter_actor_add_child (stage_actor, overlay);
  clutter_actor_set_child_above_sibling (stage_actor, overlay, NULL);

  st = g_new0 (MyScreenTransition, 1);
  st->overlay = overlay;
  active_transition = st;

  /* Theme refresh runs next under this overlay; delay covers app restyle. */
  my_screen_transition_start_fade (st);
}
