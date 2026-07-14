#pragma once

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

/*
 * Resolve the session appearance from the canonical GSettings key.
 * "default" and unknown values mean no preference, which is light here.
 */
gboolean ooze_color_scheme_is_dark (GSettings *settings);

G_END_DECLS
