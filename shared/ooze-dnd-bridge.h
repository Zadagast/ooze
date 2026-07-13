#pragma once

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

/* Session-scoped pending file drag for Ooze surfaces that cannot see
 * Wayland drop payloads (compositor desktop / dock). Spot writes on
 * GtkDragSource begin; the shell may take() on MetaDnd leave. */

void     ooze_dnd_bridge_set_paths (const char * const *paths,
                                    gsize               n_paths,
                                    gboolean            prefer_move);

void     ooze_dnd_bridge_set_files (GFile **files,
                                    gsize   n_files,
                                    gboolean prefer_move);

/* Returns TRUE and steals ownership of the path list into *paths_out
 * (NULL-terminated, free with g_strfreev). Clears the bridge. */
gboolean ooze_dnd_bridge_take (char    ***paths_out,
                               gboolean  *prefer_move_out);

void     ooze_dnd_bridge_clear (void);

gboolean ooze_dnd_bridge_has_pending (void);

/* Compositor writes the current desktop/dock drop target while MetaDnd
 * tracks the pointer. Spot reads this on drag-end when no GTK target
 * accepted the drop. */
void     ooze_dnd_bridge_set_hover_dir (const char *dir_or_null);
char    *ooze_dnd_bridge_get_hover_dir (void);

/* take() pending paths and copy/move into dest_dir. Returns TRUE if any
 * transfer was attempted. Same-volume defaults to move when prefer_move
 * from the bridge is TRUE or when files share a filesystem id. */
gboolean ooze_dnd_bridge_drop_into (const char *dest_dir);

G_END_DECLS
