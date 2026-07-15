#pragma once

#include "ooze-plugin-priv.h"

G_BEGIN_DECLS

typedef enum
{
  OOZE_SESSION_ACTION_LOGOUT,
  OOZE_SESSION_ACTION_RESTART,
  OOZE_SESSION_ACTION_SHUTDOWN,
  OOZE_SESSION_ACTION_SUSPEND,
} OozeSessionAction;

/*
 * Present the compositor-drawn end-session dialog for @action: an Aqua
 * overlay with a 60s auto-confirm countdown and a Cancel button, like
 * GNOME's. Confirming logs out (graceful window close + clean compositor
 * exit) or routes Restart/Shut Down/Suspend through logind.
 */
void ooze_session_dialog_present (OozePlugin        *plugin,
                                  OozeSessionAction  action);

/* Tear down any open dialog (called during compositor shutdown). */
void ooze_session_dialog_dismiss (void);

G_END_DECLS
