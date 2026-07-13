#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * Main-thread stall observability for the compositor.
 * Start a scope around potentially heavy work; if the scope runs longer
 * than the threshold, emit: g_warning ("OozeStall: <tag> took %u ms", ...).
 * No auto-kill — diagnostics only.
 */

typedef struct _OozeStallScope OozeStallScope;

/* Default threshold (ms). Override with OOZE_STALL_MS (0 disables warnings). */
#define OOZE_STALL_DEFAULT_MS 50

void            ooze_stall_init (void);
OozeStallScope *ooze_stall_begin (const char *tag);
void            ooze_stall_end (OozeStallScope *scope);

/* Convenience: begin/end around a block via g_autoptr cleanup. */
G_DEFINE_AUTOPTR_CLEANUP_FUNC (OozeStallScope, ooze_stall_end)

G_END_DECLS
