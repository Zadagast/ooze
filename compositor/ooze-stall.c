#include "ooze-stall.h"

#include <stdlib.h>

struct _OozeStallScope
{
  const char *tag;
  gint64      start_us;
};

static gint ooze_stall_threshold_ms = -1;

void
ooze_stall_init (void)
{
  const char *env;

  if (ooze_stall_threshold_ms >= 0)
    return;

  env = g_getenv ("OOZE_STALL_MS");
  if (env && env[0] != '\0')
    {
      int v = atoi (env);

      ooze_stall_threshold_ms = v < 0 ? 0 : v;
    }
  else
    {
      /* Always on with a modest default — stalls that freeze launches are
       * exactly what we need to see in nest and native sessions. */
      ooze_stall_threshold_ms = OOZE_STALL_DEFAULT_MS;
    }
}

OozeStallScope *
ooze_stall_begin (const char *tag)
{
  OozeStallScope *scope;

  ooze_stall_init ();
  scope = g_new0 (OozeStallScope, 1);
  scope->tag = tag && tag[0] ? tag : "unknown";
  scope->start_us = g_get_monotonic_time ();
  return scope;
}

void
ooze_stall_end (OozeStallScope *scope)
{
  gint64 elapsed_us;
  guint elapsed_ms;

  if (!scope)
    return;

  if (ooze_stall_threshold_ms > 0)
    {
      elapsed_us = g_get_monotonic_time () - scope->start_us;
      elapsed_ms = (guint) (elapsed_us / 1000);
      if (elapsed_ms >= (guint) ooze_stall_threshold_ms)
        g_warning ("OozeStall: %s took %u ms", scope->tag, elapsed_ms);
    }

  g_free (scope);
}
