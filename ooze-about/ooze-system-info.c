#include "ooze-system-info.h"

#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifdef HAVE_LIBGTOP
#include <glibtop/sysinfo.h>
#include <glibtop/mem.h>
#endif

#ifdef HAVE_UDISKS2
#include <udisks/udisks.h>
#endif

static char *
hostname1_prop (GDBusProxy *proxy, const char *name)
{
  g_autoptr (GVariant) v = NULL;
  const char *s;

  if (!proxy)
    return NULL;

  v = g_dbus_proxy_get_cached_property (proxy, name);
  if (!v)
    return NULL;

  s = g_variant_get_string (v, NULL);
  if (!s || !*s)
    return NULL;

  return g_strdup (s);
}

static GDBusProxy *
hostname1_proxy (void)
{
  g_autoptr (GError) error = NULL;
  GDBusProxy *proxy;

  proxy = g_dbus_proxy_new_for_bus_sync (
      G_BUS_TYPE_SYSTEM,
      G_DBUS_PROXY_FLAGS_NONE,
      NULL,
      "org.freedesktop.hostname1",
      "/org/freedesktop/hostname1",
      "org.freedesktop.hostname1",
      NULL,
      &error);
  if (!proxy)
    g_debug ("Ooze About: hostname1 unavailable: %s",
             error ? error->message : "unknown");
  return proxy;
}

static char *
format_bytes (guint64 bytes)
{
  return g_format_size_full (bytes, G_FORMAT_SIZE_IEC_UNITS);
}

static char *
read_meminfo_total (void)
{
  FILE *fp;
  char line[256];
  guint64 kib = 0;

  fp = fopen ("/proc/meminfo", "r");
  if (!fp)
    return NULL;

  while (fgets (line, sizeof line, fp))
    {
      if (sscanf (line, "MemTotal: %" G_GUINT64_FORMAT " kB", &kib) == 1)
        break;
    }
  fclose (fp);

  if (kib == 0)
    return NULL;

  return format_bytes (kib * 1024);
}

static char *
read_cpu_model (void)
{
  FILE *fp;
  char line[512];
  char *model = NULL;

  fp = fopen ("/proc/cpuinfo", "r");
  if (!fp)
    return NULL;

  while (fgets (line, sizeof line, fp))
    {
      if (g_str_has_prefix (line, "model name") ||
          g_str_has_prefix (line, "Hardware") ||
          g_str_has_prefix (line, "cpu model"))
        {
          char *colon = strchr (line, ':');
          if (colon)
            {
              model = g_strdup (g_strstrip (colon + 1));
              break;
            }
        }
    }
  fclose (fp);
  return model;
}

static char *
root_disk_capacity (void)
{
  struct statvfs st;

  if (statvfs ("/", &st) != 0)
    return NULL;

  return format_bytes ((guint64) st.f_blocks * (guint64) st.f_frsize);
}

#ifdef HAVE_LIBGTOP
static char *
gtop_processor (void)
{
  const glibtop_sysinfo *sys;
  guint i;

  sys = glibtop_get_sysinfo ();
  if (!sys || sys->ncpu < 1)
    return NULL;

  for (i = 0; i < sys->ncpu; i++)
    {
      const char *model = g_hash_table_lookup (sys->cpuinfo[i].values,
                                               "model name");
      if (!model)
        model = g_hash_table_lookup (sys->cpuinfo[i].values, "cpu model");
      if (model && *model)
        return g_strdup_printf ("%s × %u", model, sys->ncpu);
    }

  return g_strdup_printf ("%u CPUs", sys->ncpu);
}

static char *
gtop_memory (void)
{
  glibtop_mem mem;

  memset (&mem, 0, sizeof mem);
  glibtop_get_mem (&mem);
  if (mem.total == 0)
    return NULL;

  return format_bytes (mem.total);
}
#endif

#ifdef HAVE_UDISKS2
static char *
udisks_disk_capacity (void)
{
  g_autoptr (UDisksClient) client = NULL;
  g_autoptr (GError) error = NULL;
  GList *objects;
  GList *l;
  guint64 best = 0;

  client = udisks_client_new_sync (NULL, &error);
  if (!client)
    {
      g_debug ("Ooze About: UDisks2 unavailable: %s",
               error ? error->message : "unknown");
      return NULL;
    }

  objects = g_dbus_object_manager_get_objects (
      udisks_client_get_object_manager (client));
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *obj = UDISKS_OBJECT (l->data);
      UDisksBlock *block = udisks_object_peek_block (obj);
      UDisksDrive *drive;
      guint64 size;

      if (!block)
        continue;

      drive = udisks_client_get_drive_for_block (client, block);
      if (!drive)
        continue;

      if (udisks_drive_get_optical (drive) ||
          udisks_drive_get_media_removable (drive))
        {
          g_object_unref (drive);
          continue;
        }

      size = udisks_drive_get_size (drive);
      if (size > best)
        best = size;
      g_object_unref (drive);
    }
  g_list_free_full (objects, g_object_unref);

  if (best == 0)
    return NULL;

  return format_bytes (best);
}
#endif

OozeSystemInfo *
ooze_system_info_gather (void)
{
  OozeSystemInfo *info;
  g_autoptr (GDBusProxy) host = NULL;
  g_autofree char *vendor = NULL;
  g_autofree char *model = NULL;
  const char *pretty_os;

  info = g_new0 (OozeSystemInfo, 1);
  host = hostname1_proxy ();

  info->device_name = hostname1_prop (host, "PrettyHostname");
  if (!info->device_name || !*info->device_name)
    {
      g_free (info->device_name);
      info->device_name = g_strdup (g_get_host_name ());
    }

  vendor = hostname1_prop (host, "HardwareVendor");
  model = hostname1_prop (host, "HardwareModel");
  if (vendor && model)
    info->hardware_model = g_strdup_printf ("%s %s", vendor, model);
  else if (model)
    info->hardware_model = g_strdup (model);
  else if (vendor)
    info->hardware_model = g_strdup (vendor);
  else
    info->hardware_model = g_strdup ("Unknown");

  info->operating_system = hostname1_prop (host, "OperatingSystemPrettyName");
  if (!info->operating_system)
    {
      pretty_os = g_get_os_info (G_OS_INFO_KEY_PRETTY_NAME);
      info->operating_system = g_strdup (pretty_os ? pretty_os : "Unknown");
    }

  info->kernel = hostname1_prop (host, "KernelRelease");
  if (!info->kernel)
    {
      struct utsname u;
      if (uname (&u) == 0)
        info->kernel = g_strdup (u.release);
      else
        info->kernel = g_strdup ("Unknown");
    }

#ifdef HAVE_LIBGTOP
  info->processor = gtop_processor ();
  info->memory = gtop_memory ();
#endif

  if (!info->processor)
    info->processor = read_cpu_model ();
  if (!info->processor)
    info->processor = g_strdup ("Unknown");

  if (!info->memory)
    info->memory = read_meminfo_total ();
  if (!info->memory)
    info->memory = g_strdup ("Unknown");

#ifdef HAVE_UDISKS2
  info->disk_capacity = udisks_disk_capacity ();
#endif
  if (!info->disk_capacity)
    info->disk_capacity = root_disk_capacity ();
  if (!info->disk_capacity)
    info->disk_capacity = g_strdup ("Unknown");

  return info;
}

void
ooze_system_info_free (OozeSystemInfo *info)
{
  if (!info)
    return;

  g_free (info->device_name);
  g_free (info->hardware_model);
  g_free (info->operating_system);
  g_free (info->processor);
  g_free (info->memory);
  g_free (info->disk_capacity);
  g_free (info->kernel);
  g_free (info);
}
