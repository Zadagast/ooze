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
  char     *connector;
  char     *display_name;
  int       layout_x;
  int       layout_y;
  double    scale;
  guint     transform;
  gboolean  primary;
  char     *current_mode_id;
  GPtrArray *modes; /* OozeDisplayMode* */
} OozeDisplayMonitor;

typedef struct
{
  guint     serial;
  guint     layout_mode; /* 1=logical, 2=physical */
  gboolean  supports_changing_layout_mode;
  gboolean  global_scale_required;
  GPtrArray *monitors; /* OozeDisplayMonitor* */
} OozeDisplayConfig;

/* ApplyMonitorsConfig method — matches Mutter / GNOME Settings */
#define OOZE_DISPLAY_APPLY_TEMPORARY  1u
#define OOZE_DISPLAY_APPLY_PERSISTENT 2u

gboolean ooze_display_config_allowed (void);

gboolean ooze_display_monitor_is_nest_dummy (const OozeDisplayMonitor *monitor);

gboolean ooze_display_config_load (OozeDisplayConfig **config_out,
                                   GError            **error);

OozeDisplayConfig *ooze_display_config_copy (const OozeDisplayConfig *config);

void ooze_display_config_free (OozeDisplayConfig *config);

void ooze_display_config_set_primary (OozeDisplayConfig *config,
                                      guint              index);

const OozeDisplayMode *ooze_display_monitor_current_mode (const OozeDisplayMonitor *monitor);

/* Apply the full logical-monitor list. method is TEMPORARY or PERSISTENT. */
gboolean ooze_display_config_apply (const OozeDisplayConfig *config,
                                    guint                    method,
                                    GError                 **error);

G_END_DECLS
