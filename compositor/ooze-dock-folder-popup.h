#pragma once

#include <clutter/clutter.h>
#include <meta/meta-context.h>

G_BEGIN_DECLS

/*
 * OozeDockFolderPopup — mac-style grid "fan-out" for a docked folder.
 *
 * Clicking a folder item on the dock opens a themed grid of that folder's
 * contents (icon over label), scrolling when it overflows. Clicking an entry
 * opens it; clicking outside dismisses the popup.
 */
typedef struct _OozeDockFolderPopup OozeDockFolderPopup;

/* Called when the user activates a directory entry (open it in the file
 * manager). Regular files are opened with their default handler internally. */
typedef void (*OozeDockFolderOpenDirFunc) (const char *path,
                                           gpointer    user_data);

OozeDockFolderPopup *ooze_dock_folder_popup_new (MetaContext *context,
                                                 ClutterActor *stage);

void ooze_dock_folder_popup_set_open_dir_func (OozeDockFolderPopup       *popup,
                                               OozeDockFolderOpenDirFunc  func,
                                               gpointer                   user_data);

/* Open (or, if already open for this anchor, close) the popup for PATH,
 * anchored above ANCHOR. */
void ooze_dock_folder_popup_toggle (OozeDockFolderPopup *popup,
                                    ClutterActor        *anchor,
                                    const char          *path);

void ooze_dock_folder_popup_close (OozeDockFolderPopup *popup);

gboolean ooze_dock_folder_popup_is_open (OozeDockFolderPopup *popup);

void ooze_dock_folder_popup_destroy (OozeDockFolderPopup *popup);

G_END_DECLS
