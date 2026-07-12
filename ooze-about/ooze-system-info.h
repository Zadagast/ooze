#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
  char *device_name;
  char *hardware_model;
  char *operating_system;
  char *processor;
  char *memory;
  char *disk_capacity;
  char *kernel;
} OozeSystemInfo;

OozeSystemInfo *ooze_system_info_gather (void);
void            ooze_system_info_free   (OozeSystemInfo *info);

G_END_DECLS
