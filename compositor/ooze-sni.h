#pragma once

#include "ooze-plugin.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

typedef struct _OozeSniItem OozeSniItem;
typedef struct _OozeSniWatcher OozeSniWatcher;

typedef void (*OozeSniItemChangedFn) (OozeSniItem *item, gpointer user_data);
typedef void (*OozeSniItemRemovedFn) (OozeSniItem *item, gpointer user_data);

const char *ooze_sni_item_bus_name (OozeSniItem *item);
const char *ooze_sni_item_object_path (OozeSniItem *item);
const char *ooze_sni_item_id (OozeSniItem *item);
const char *ooze_sni_item_title (OozeSniItem *item);
const char *ooze_sni_item_icon_name (OozeSniItem *item);
const char *ooze_sni_item_menu_path (OozeSniItem *item);
gboolean    ooze_sni_item_is_menu (OozeSniItem *item);
GdkPixbuf  *ooze_sni_item_icon_pixbuf (OozeSniItem *item); /* may be NULL; owned by item */

/* Re-resolve IconName (theme / symbolic tint). No-op for pixmap-only items. */
void ooze_sni_item_reresolve_icon (OozeSniItem *item);

void ooze_sni_item_activate (OozeSniItem *item, int x, int y);
void ooze_sni_item_secondary_activate (OozeSniItem *item, int x, int y);
void ooze_sni_item_context_menu (OozeSniItem *item, int x, int y);

OozeSniWatcher *ooze_sni_watcher_new (OozeSniItemChangedFn added_or_changed,
                                      OozeSniItemRemovedFn removed,
                                      gpointer             user_data);
void            ooze_sni_watcher_free (OozeSniWatcher *watcher);

G_END_DECLS
