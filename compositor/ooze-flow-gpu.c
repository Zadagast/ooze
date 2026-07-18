#include "ooze-flow-gpu.h"

#include <cogl/cogl.h>

struct _OozeFlowGpu
{
  GObject parent_instance;
  CoglPipeline *pipeline;
  int phase_location;
  int aspect_location;
  int color_location;
  int dark_location;
};

static void ooze_flow_gpu_content_init (ClutterContentInterface *iface);

G_DEFINE_TYPE_WITH_CODE (OozeFlowGpu, ooze_flow_gpu, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT,
                                                ooze_flow_gpu_content_init))

static const char flow_gpu_declarations[] =
  "uniform float ooze_flow_phase;\n"
  "uniform float ooze_flow_aspect;\n"
  "uniform vec3 ooze_flow_color;\n"
  "uniform float ooze_flow_dark;\n"
  "float ooze_flow_smoothstep (float edge0, float edge1, float value)\n"
  "{\n"
  "  float t = clamp ((value - edge0) / (edge1 - edge0), 0.0, 1.0);\n"
  "  return t * t * (3.0 - 2.0 * t);\n"
  "}\n"
  "vec2 ooze_flow_blob (vec4 blob, float xscale, float yscale, float phase)\n"
  "{\n"
  "  float angle = phase * blob.z + blob.w;\n"
  "  return blob.xy + vec2 (\n"
  "    xscale * sin (angle) + xscale * 0.35 * sin (angle * 0.47 + blob.w),\n"
  "    yscale * cos (angle * 0.83) + yscale * 0.30 * cos (angle * 0.39 + blob.w));\n"
  "}\n";

static const char flow_gpu_fragment[] = {
  "vec2 uv = cogl_tex_coord0_in.st;\n"
  "float aspect = ooze_flow_aspect;\n"
  "float field = 0.0;\n"
  "float specular = 0.0;\n"
  "vec4 blobs[14];\n"
  "blobs[0] = vec4 (0.16, 0.25, 0.72, 0.10);\n"
  "blobs[1] = vec4 (0.30, 0.20, 0.51, 1.80);\n"
  "blobs[2] = vec4 (0.47, 0.25, 0.63, 3.25);\n"
  "blobs[3] = vec4 (0.64, 0.19, 0.43, 4.40);\n"
  "blobs[4] = vec4 (0.82, 0.28, 0.57, 5.10);\n"
  "blobs[5] = vec4 (0.22, 0.52, 0.46, 2.55);\n"
  "blobs[6] = vec4 (0.40, 0.66, 0.68, 0.90);\n"
  "blobs[7] = vec4 (0.58, 0.50, 0.37, 5.70);\n"
  "blobs[8] = vec4 (0.77, 0.49, 0.59, 2.10);\n"
  "blobs[9] = vec4 (0.90, 0.68, 0.78, 3.90);\n"
  "blobs[10] = vec4 (0.12, 0.80, 0.48, 1.30);\n"
  "blobs[11] = vec4 (0.34, 0.88, 0.66, 4.80);\n"
  "blobs[12] = vec4 (0.63, 0.82, 0.54, 0.35);\n"
  "blobs[13] = vec4 (0.84, 0.88, 0.41, 2.90);\n"
  "float radii[14];\n"
  "float xscales[14];\n"
  "float yscales[14];\n"
  "radii[0] = 0.14; radii[1] = 0.11; radii[2] = 0.15; radii[3] = 0.10;\n"
  "radii[4] = 0.13; radii[5] = 0.12; radii[6] = 0.16; radii[7] = 0.10;\n"
  "radii[8] = 0.14; radii[9] = 0.11; radii[10] = 0.10; radii[11] = 0.13;\n"
  "radii[12] = 0.11; radii[13] = 0.13;\n"
  "xscales[0] = 0.14; xscales[1] = 0.11; xscales[2] = 0.15; xscales[3] = 0.10;\n"
  "xscales[4] = 0.13; xscales[5] = 0.12; xscales[6] = 0.15; xscales[7] = 0.10;\n"
  "xscales[8] = 0.13; xscales[9] = 0.11; xscales[10] = 0.10; xscales[11] = 0.12;\n"
  "xscales[12] = 0.11; xscales[13] = 0.12;\n"
  "yscales[0] = 0.11; yscales[1] = 0.15; yscales[2] = 0.10; yscales[3] = 0.13;\n"
  "yscales[4] = 0.12; yscales[5] = 0.10; yscales[6] = 0.14; yscales[7] = 0.12;\n"
  "yscales[8] = 0.11; yscales[9] = 0.14; yscales[10] = 0.12; yscales[11] = 0.10;\n"
  "yscales[12] = 0.13; yscales[13] = 0.10;\n"
  "for (int i = 0; i < 14; i++)\n"
  "{\n"
  "  vec2 center = ooze_flow_blob (blobs[i], xscales[i], yscales[i], ooze_flow_phase);\n"
  "  float radius = radii[i];\n"
  "  vec2 delta = (uv - center) * vec2 (aspect, 1.0);\n"
  "  float distance = dot (delta, delta) / (radius * radius);\n"
  "  field += 0.20 / (distance + 0.045);\n"
  "  vec2 highlight = center - vec2 (radius * 0.34, radius * 0.38);\n"
  "  vec2 highlight_delta = (uv - highlight) * vec2 (aspect, 1.0);\n"
  "  specular += exp (-dot (highlight_delta, highlight_delta) /\n"
  "                   (radius * radius * 0.11));\n"
  "}\n"
  "float alpha = ooze_flow_smoothstep (0.48, 1.45, field);\n"
  "float rim = ooze_flow_smoothstep (0.38, 0.82, field) -\n"
  "            ooze_flow_smoothstep (1.05, 1.70, field);\n"
  "float glass = clamp (ooze_flow_smoothstep (0.55, 1.35, field) * 0.35 +\n"
  "                     rim * 0.55, 0.0, 0.78);\n"
  "float lens = clamp (field * 0.035, 0.0, 0.16);\n"
  "vec2 lens_uv = (uv - vec2 (0.5)) * (1.0 - lens) + vec2 (0.5);\n"
  "float lens_sheen = exp (-dot (lens_uv - uv, lens_uv - uv) * 420.0);\n"
  "alpha *= mix (0.48, 0.38, ooze_flow_dark);\n"
  "specular = clamp (specular * 0.34 + lens_sheen * 0.18, 0.0, 0.58);\n"
  "vec3 color = ooze_flow_color + vec3 (glass * 0.16, glass * 0.18, glass * 0.24);\n"
  "color += specular * vec3 (1.0, 1.10, 1.30);\n"
  "cogl_color_out = vec4 (color * alpha, alpha);\n"
};

static gboolean
ooze_flow_gpu_get_preferred_size (ClutterContent *content G_GNUC_UNUSED,
                                   gfloat         *width G_GNUC_UNUSED,
                                   gfloat         *height G_GNUC_UNUSED)
{
  return FALSE;
}

static void
ooze_flow_gpu_paint_content (ClutterContent      *content,
                             ClutterActor        *actor,
                             ClutterPaintNode    *node,
                             ClutterPaintContext *paint_context G_GNUC_UNUSED)
{
  OozeFlowGpu *flow = OOZE_FLOW_GPU (content);
  ClutterActorBox box;
  ClutterPaintNode *pipeline_node;

  box.x1 = 0.0f;
  box.y1 = 0.0f;
  box.x2 = clutter_actor_get_width (actor);
  box.y2 = clutter_actor_get_height (actor);
  pipeline_node = clutter_pipeline_node_new (flow->pipeline);
  clutter_paint_node_add_rectangle (pipeline_node, &box);
  clutter_paint_node_add_child (node, pipeline_node);
  clutter_paint_node_unref (pipeline_node);
}

static void
ooze_flow_gpu_content_init (ClutterContentInterface *iface)
{
  iface->get_preferred_size = ooze_flow_gpu_get_preferred_size;
  iface->paint_content = ooze_flow_gpu_paint_content;
}

static void
ooze_flow_gpu_dispose (GObject *object)
{
  OozeFlowGpu *flow = OOZE_FLOW_GPU (object);

  g_clear_object (&flow->pipeline);
  G_OBJECT_CLASS (ooze_flow_gpu_parent_class)->dispose (object);
}

static void
ooze_flow_gpu_class_init (OozeFlowGpuClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ooze_flow_gpu_dispose;
}

static void
ooze_flow_gpu_init (OozeFlowGpu *flow G_GNUC_UNUSED)
{
}

OozeFlowGpu *
ooze_flow_gpu_new (void)
{
  ClutterBackend *backend;
  CoglContext *context;
  CoglSnippet *snippet;
  OozeFlowGpu *flow;
  g_autoptr (GError) blend_error = NULL;

  backend = clutter_get_default_backend ();
  context = backend ? clutter_backend_get_cogl_context (backend) : NULL;
  if (!context)
    return NULL;

  flow = g_object_new (ooze_flow_gpu_get_type (), NULL);
  flow->pipeline = cogl_pipeline_new (context);
  if (!flow->pipeline)
    {
      g_object_unref (flow);
      return NULL;
    }
  if (!cogl_pipeline_set_blend (
        flow->pipeline,
        "RGBA = ADD(SRC_COLOR, DST_COLOR*(1-SRC_COLOR[A]))",
        &blend_error))
    {
      g_warning ("Unable to enable Ooze Flow blending: %s",
                 blend_error->message);
      g_object_unref (flow);
      return NULL;
    }
  cogl_pipeline_set_layer_null_texture (flow->pipeline, 0);

  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT_GLOBALS,
                              flow_gpu_declarations,
                              NULL);
  cogl_pipeline_add_snippet (flow->pipeline, snippet);
  g_object_unref (snippet);

  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT, NULL, NULL);
  cogl_snippet_set_replace (snippet, flow_gpu_fragment);
  cogl_pipeline_add_snippet (flow->pipeline, snippet);
  g_object_unref (snippet);

  flow->phase_location =
    cogl_pipeline_get_uniform_location (flow->pipeline, "ooze_flow_phase");
  flow->aspect_location =
    cogl_pipeline_get_uniform_location (flow->pipeline, "ooze_flow_aspect");
  flow->color_location =
    cogl_pipeline_get_uniform_location (flow->pipeline, "ooze_flow_color");
  flow->dark_location =
    cogl_pipeline_get_uniform_location (flow->pipeline, "ooze_flow_dark");

  ooze_flow_gpu_set_phase (flow, 0.0);
  ooze_flow_gpu_set_size (flow, 1, 1);
  ooze_flow_gpu_set_color (flow, 0.09, 0.30, 0.68, FALSE);
  return flow;
}

ClutterContent *
ooze_flow_gpu_get_content (OozeFlowGpu *flow)
{
  return flow ? CLUTTER_CONTENT (flow) : NULL;
}

void
ooze_flow_gpu_set_phase (OozeFlowGpu *flow,
                         gdouble       phase)
{
  g_return_if_fail (flow != NULL);

  cogl_pipeline_set_uniform_1f (flow->pipeline,
                                flow->phase_location,
                                (float) phase);
}

void
ooze_flow_gpu_set_size (OozeFlowGpu *flow,
                        int           width,
                        int           height)
{
  g_return_if_fail (flow != NULL);

  cogl_pipeline_set_uniform_1f (flow->pipeline,
                                flow->aspect_location,
                                height > 0 ? (float) width / height : 1.0f);
}

void
ooze_flow_gpu_set_color (OozeFlowGpu *flow,
                         gdouble       red,
                         gdouble       green,
                         gdouble       blue,
                         gboolean      dark)
{
  float color[3] = { red, green, blue };

  g_return_if_fail (flow != NULL);

  cogl_pipeline_set_uniform_float (flow->pipeline,
                                   flow->color_location,
                                   3,
                                   1,
                                   color);
  cogl_pipeline_set_uniform_1f (flow->pipeline,
                                flow->dark_location,
                                dark ? 1.0f : 0.0f);
}

void
ooze_flow_gpu_free (OozeFlowGpu *flow)
{
  g_clear_object (&flow);
}
