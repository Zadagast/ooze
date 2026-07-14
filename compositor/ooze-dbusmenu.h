#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _OozeDbusmenu OozeDbusmenu;

typedef struct
{
  int   id;
  char *label;
  gboolean enabled;
  gboolean visible;
  gboolean separator;
  gboolean has_children;
} OozeDbusmenuItem;

/* Fired on the main loop whenever cached layout data changes. */
typedef void (*OozeDbusmenuChangedFn) (gpointer user_data);

OozeDbusmenu *ooze_dbusmenu_new (GDBusConnection *session,
                             const char      *bus_name,
                             const char      *object_path);

void ooze_dbusmenu_free (OozeDbusmenu *menu);

void ooze_dbusmenu_set_changed_callback (OozeDbusmenu          *menu,
                                       OozeDbusmenuChangedFn  callback,
                                       gpointer             user_data);

/* Subscribe to layout signals and request the root layout asynchronously.
 * The changed callback fires when data arrives. */
void ooze_dbusmenu_start (OozeDbusmenu *menu);

/* Root children (= panel top labels) from the cache. Returns FALSE and
 * requests an async fetch when not cached yet. Caller frees the copy with
 * ooze_dbusmenu_items_free. */
gboolean ooze_dbusmenu_get_top_items (OozeDbusmenu       *menu,
                                    OozeDbusmenuItem  **items_out,
                                    gsize            *n_out);

/* Children of a top item (one panel dropdown) from the cache. Returns FALSE
 * and requests an async fetch when not cached yet. */
gboolean ooze_dbusmenu_get_children (OozeDbusmenu       *menu,
                                   int               parent_id,
                                   OozeDbusmenuItem  **items_out,
                                   gsize            *n_out);

void ooze_dbusmenu_items_free (OozeDbusmenuItem *items,
                             gsize           n);

/* Fire-and-forget events; replies are ignored. */
void ooze_dbusmenu_click (OozeDbusmenu *menu,
                        int         id);

/* Tell the exporter a top-level menu was opened (lazy populate). */
void ooze_dbusmenu_opened (OozeDbusmenu *menu,
                         int         id);

G_END_DECLS
