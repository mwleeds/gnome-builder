/* test-ide-subprocess-launcher.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ide.h>

static void
test_basic (void)
{
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  g_autoptr(IdeSubprocess) process = NULL;
  g_autoptr(GError) error = NULL;

  launcher = ide_subprocess_launcher_new (0);
  g_assert (launcher != NULL);

  ide_subprocess_launcher_push_argv (launcher, "true");

  process = ide_subprocess_launcher_spawn (launcher, NULL, &error);
  g_assert (process != NULL);
  g_assert (error == NULL);
  g_assert_cmpint (ide_subprocess_wait_check (process, NULL, &error), !=, 0);
}

static int
check_args (IdeSubprocessLauncher *launcher,
            gchar *argv0,
            ...)
{
  va_list args;
  const gchar * const * actual_argv;
  guint num_args;
  gchar *item;

  g_assert (IDE_IS_SUBPROCESS_LAUNCHER (launcher));

  actual_argv = ide_subprocess_launcher_get_argv (launcher);

  if (actual_argv == NULL && argv0 == NULL)
    return 1;
  else if (actual_argv == NULL || argv0 == NULL)
    return 0;

  num_args = 0;
  if (g_strcmp0 (argv0, actual_argv[num_args++]) != 0)
    return 0;

  va_start (args, argv0);
  while (NULL != (item = va_arg (args, gchar *)))
    {
      const gchar *next_arg = NULL;
      next_arg = actual_argv[num_args++];
      if (g_strcmp0 (next_arg, item) != 0)
        return 0;
    }
  va_end (args);

  if (actual_argv[num_args] == NULL)
    return 1;
  else
    return 0;
}

static void
test_argv_manipulation (void)
{
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;

  launcher = ide_subprocess_launcher_new (0);
  g_assert (launcher != NULL);

  ide_subprocess_launcher_push_argv (launcher, "echo");
  ide_subprocess_launcher_push_argv (launcher, "world");
  ide_subprocess_launcher_insert_argv (launcher, 1, "hello");
  g_assert_cmpint (check_args (launcher, "echo", "hello", "world", NULL), !=, 0);

  ide_subprocess_launcher_replace_argv (launcher, 2, "universe");
  g_assert_cmpint (check_args (launcher, "echo", "hello", "universe", NULL), !=, 0);

  g_assert_cmpstr (ide_subprocess_launcher_pop_argv (launcher), ==, "universe");
  g_assert_cmpint (check_args (launcher, "echo", "hello", NULL), !=, 0);
}

gint
main (gint   argc,
      gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/Ide/SubprocessLauncher/basic", test_basic);
  g_test_add_func ("/Ide/SubprocessLauncher/argv-manipulation", test_argv_manipulation);
  return g_test_run ();
}
