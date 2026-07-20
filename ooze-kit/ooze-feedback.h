#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Show a short saved/confirmation message in an Ooze overlay.
 */
void ooze_feedback_show (GtkWindow   *parent,
                         const char  *message);

G_END_DECLS
