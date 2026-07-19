#pragma once

#if !defined (OOZEKIT_BUNDLED) && defined (__has_include)
#  if __has_include (<oozekit/ooze-application.h>)
#    define OOZEKIT_HAVE_INSTALLED_NAMESPACE 1
#  endif
#endif

#ifdef OOZEKIT_HAVE_INSTALLED_NAMESPACE
#include <oozekit/ooze-application.h>
#include <oozekit/ooze-init.h>
#include <oozekit/ooze-application-window.h>
#include <oozekit/ooze-about.h>
#include <oozekit/ooze-feedback.h>
#include <oozekit/ooze-action-bar.h>
#include <oozekit/ooze-button.h>
#include <oozekit/ooze-draw.h>
#include <oozekit/ooze-icons.h>
#include <oozekit/ooze-palette.h>
#include <oozekit/ooze-pinline.h>
#include <oozekit/ooze-popover.h>
#include <oozekit/ooze-scroll.h>
#include <oozekit/ooze-grid-menu.h>
#include <oozekit/ooze-surface.h>
#include <oozekit/ooze-theme.h>
#include <oozekit/ooze-toolbar.h>
#include <oozekit/ooze-window-actions.h>
#include <oozekit/ooze-gel.h>
#include <oozekit/ooze-header-bar.h>
#include <oozekit/ooze-traffic-lights.h>
#else
#include "ooze-application.h"
#include "ooze-init.h"
#include "ooze-application-window.h"
#include "ooze-about.h"
#include "ooze-feedback.h"
#include "ooze-action-bar.h"
#include "ooze-button.h"
#include "ooze-draw.h"
#include "ooze-icons.h"
#include "ooze-palette.h"
#include "ooze-pinline.h"
#include "ooze-popover.h"
#include "ooze-scroll.h"
#include "ooze-grid-menu.h"
#include "ooze-surface.h"
#include "ooze-theme.h"
#include "ooze-toolbar.h"
#include "ooze-window-actions.h"
#include "ooze-gel.h"
#include "ooze-header-bar.h"
#include "ooze-traffic-lights.h"
#endif
