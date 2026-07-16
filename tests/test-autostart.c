#include "ooze-autostart.h"

#include <glib.h>

static GKeyFile *
make_entry (const char *extra)
{
  GKeyFile *keyfile = g_key_file_new ();
  g_autofree char *data =
    g_strdup_printf ("[Desktop Entry]\n"
                     "Type=Application\n"
                     "Name=Test\n"
                     "Exec=true\n"
                     "%s",
                     extra ? extra : "");

  g_assert_true (g_key_file_load_from_data (keyfile, data, -1,
                                            G_KEY_FILE_NONE, NULL));
  return keyfile;
}

static void
test_plain_runs (void)
{
  g_autoptr (GKeyFile) entry = make_entry (NULL);

  g_assert_true (ooze_autostart_entry_should_run (entry, "Ooze"));
}

static void
test_hidden_skipped (void)
{
  g_autoptr (GKeyFile) entry = make_entry ("Hidden=true\n");

  g_assert_false (ooze_autostart_entry_should_run (entry, "Ooze"));
}

static void
test_gnome_autostart_disabled_skipped (void)
{
  g_autoptr (GKeyFile) entry =
    make_entry ("X-GNOME-Autostart-enabled=false\n");

  g_assert_false (ooze_autostart_entry_should_run (entry, "Ooze"));
}

static void
test_gnome_autostart_enabled_runs (void)
{
  g_autoptr (GKeyFile) entry =
    make_entry ("X-GNOME-Autostart-enabled=true\n");

  g_assert_true (ooze_autostart_entry_should_run (entry, "Ooze"));
}

static void
test_only_show_in_match (void)
{
  g_autoptr (GKeyFile) entry = make_entry ("OnlyShowIn=Ooze;GNOME;\n");

  g_assert_true (ooze_autostart_entry_should_run (entry, "Ooze"));
}

static void
test_only_show_in_no_match (void)
{
  g_autoptr (GKeyFile) entry = make_entry ("OnlyShowIn=GNOME;KDE;\n");

  g_assert_false (ooze_autostart_entry_should_run (entry, "Ooze"));
}

static void
test_not_show_in_excludes (void)
{
  g_autoptr (GKeyFile) entry = make_entry ("NotShowIn=Ooze;\n");

  g_assert_false (ooze_autostart_entry_should_run (entry, "Ooze"));
}

static void
test_not_show_in_other_runs (void)
{
  g_autoptr (GKeyFile) entry = make_entry ("NotShowIn=KDE;\n");

  g_assert_true (ooze_autostart_entry_should_run (entry, "Ooze"));
}

static void
test_try_exec_missing_skipped (void)
{
  g_autoptr (GKeyFile) entry =
    make_entry ("TryExec=ooze-definitely-not-a-real-binary-xyz\n");

  g_assert_false (ooze_autostart_entry_should_run (entry, "Ooze"));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/autostart/plain-runs", test_plain_runs);
  g_test_add_func ("/autostart/hidden-skipped", test_hidden_skipped);
  g_test_add_func ("/autostart/gnome-autostart-disabled",
                   test_gnome_autostart_disabled_skipped);
  g_test_add_func ("/autostart/gnome-autostart-enabled",
                   test_gnome_autostart_enabled_runs);
  g_test_add_func ("/autostart/only-show-in-match", test_only_show_in_match);
  g_test_add_func ("/autostart/only-show-in-no-match",
                   test_only_show_in_no_match);
  g_test_add_func ("/autostart/not-show-in-excludes",
                   test_not_show_in_excludes);
  g_test_add_func ("/autostart/not-show-in-other-runs",
                   test_not_show_in_other_runs);
  g_test_add_func ("/autostart/try-exec-missing",
                   test_try_exec_missing_skipped);

  return g_test_run ();
}
