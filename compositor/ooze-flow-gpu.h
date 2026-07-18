#pragma once

#include <clutter/clutter.h>

G_BEGIN_DECLS

#define OOZE_TYPE_FLOW_GPU (ooze_flow_gpu_get_type ())
G_DECLARE_FINAL_TYPE (OozeFlowGpu, ooze_flow_gpu, OOZE, FLOW_GPU, GObject)

OozeFlowGpu *ooze_flow_gpu_new (void);

ClutterContent *ooze_flow_gpu_get_content (OozeFlowGpu *flow);

void ooze_flow_gpu_set_phase (OozeFlowGpu *flow,
                              gdouble       phase);

void ooze_flow_gpu_set_size (OozeFlowGpu *flow,
                             int           width,
                             int           height);

void ooze_flow_gpu_set_color (OozeFlowGpu *flow,
                              gdouble       red,
                              gdouble       green,
                              gdouble       blue,
                              gboolean      dark);

void ooze_flow_gpu_free (OozeFlowGpu *flow);

G_END_DECLS
