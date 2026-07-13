#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
  char    *id;
  int      width;
  int      height;
  double   refresh;
  gboolean is_current;
  gboolean is_preferred;
  double   preferred_scale;
  GArray  *supported_scales; /* array of double */
} OozeDisplayMode;

typedef struct
{
  guint     serial;
  char     *connector;
  char     *display_name;
  int       layout_x;
  int       layout_y;
  double    scale;
  guint     transform;
  gboolean  primary;
  char     *current_mode_id;
  GPtrArray *modes; /* OozeDisplayMode* */

  /* From GetCurrentState global properties */
  guint    layout_mode;                   /* 1=logical, 2=physical */
  gboolean supports_changing_layout_mode;
  gboolean global_scale_required;
} OozeDisplayState;

gboolean ooze_display_config_allowed (void);

gboolean ooze_display_config_is_nest_dummy (const OozeDisplayState *state);

gboolean ooze_display_config_load (OozeDisplayState **state_out,
                                   GError           **error);

void ooze_display_state_free (OozeDisplayState *state);

gboolean ooze_display_config_apply (const OozeDisplayState *state,
                                    const char             *mode_id,
                                    double                  scale,
                                    guint                   transform,
                                    GError                 **error);

G_END_DECLS
