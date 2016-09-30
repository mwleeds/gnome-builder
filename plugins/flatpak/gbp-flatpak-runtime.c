/* gb-flatpak-runtime.c
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

#include "gbp-flatpak-runtime.h"

struct _GbpFlatpakRuntime
{
  IdeRuntime parent_instance;

  gchar *sdk;
  gchar *platform;
  gchar *branch;
};

G_DEFINE_TYPE (GbpFlatpakRuntime, gbp_flatpak_runtime, IDE_TYPE_RUNTIME)

enum {
  PROP_0,
  PROP_BRANCH,
  PROP_PLATFORM,
  PROP_SDK,
  LAST_PROP
};

static GParamSpec *properties [LAST_PROP];

static gchar *
get_build_directory (GbpFlatpakRuntime *self)
{
  IdeContext *context;
  IdeProject *project;

  g_assert (GBP_IS_FLATPAK_RUNTIME (self));

  context = ide_object_get_context (IDE_OBJECT (self));
  project = ide_context_get_project (context);

  return g_build_filename (g_get_user_cache_dir (),
                           "gnome-builder",
                           "builds",
                           ide_project_get_id (project),
                           "flatpak",
                           ide_runtime_get_id (IDE_RUNTIME (self)),
                           NULL);
}

static gboolean
gbp_flatpak_runtime_contains_program_in_path (IdeRuntime   *runtime,
                                              const gchar  *program,
                                              GCancellable *cancellable)
{
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  g_autoptr(IdeSubprocess) subprocess = NULL;

  g_assert (IDE_IS_RUNTIME (runtime));
  g_assert (program != NULL);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  launcher = ide_runtime_create_launcher (runtime, 0);

  ide_subprocess_launcher_push_argv (launcher, "which");
  ide_subprocess_launcher_push_argv (launcher, program);

  subprocess = ide_subprocess_launcher_spawn_sync (launcher, cancellable, NULL);

  return (subprocess != NULL) && ide_subprocess_wait_check (subprocess, cancellable, NULL);
}

static void
gbp_flatpak_runtime_prebuild_worker (GTask        *task,
                                     gpointer      source_object,
                                     gpointer      task_data,
                                     GCancellable *cancellable)
{
  GbpFlatpakRuntime *self = source_object;
  g_autofree gchar *build_path = NULL;
  g_autoptr(GFile) build_dir = NULL;
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  GError *error = NULL;
  GPtrArray *args;
  IdeContext *context;
  IdeConfigurationManager *config_manager;
  IdeConfiguration *configuration;
  gchar *manifest_path;

  g_assert (G_IS_TASK (task));
  g_assert (GBP_IS_FLATPAK_RUNTIME (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  build_path = get_build_directory (self);
  build_dir = g_file_new_for_path (build_path);

  if (!g_file_query_exists (build_dir, cancellable))
    {
      if (!g_file_make_directory_with_parents (build_dir, cancellable, &error))
        {
          g_task_return_error (task, error);
          return;
        }
    }

  context = ide_object_get_context (IDE_OBJECT (self));
  config_manager = ide_context_get_configuration_manager (context);
  configuration = ide_configuration_manager_get_current (config_manager);
  manifest_path = g_file_get_path (ide_configuration_get_flatpak_manifest (configuration));

  // is this the right test?
  if (!ide_str_empty0 (manifest_path))
    {
      /* We will run flatpak-builder on the manifest, so there's no need to run
       * flatpak build-init. Instead just make sure there's a local flatpak repo
       * we can use to export the build.
       */
       g_autofree gchar *flatpak_repo_path = NULL;
       g_autoptr(GFile) flatpak_repo_dir = NULL;
       g_autoptr(IdeSubprocessLauncher) ide_launcher = NULL;
       g_autoptr(IdeSubprocessLauncher) ide_launcher2 = NULL;
       g_autoptr(IdeSubprocess) process = NULL;
       g_autoptr(IdeSubprocess) process2 = NULL;

       flatpak_repo_path = g_build_filename (g_get_user_cache_dir (),
                                             "gnome-builder",
                                             "flatpak-repo",
                                             NULL);
       flatpak_repo_dir = g_file_new_for_path (flatpak_repo_path);
       if (!g_file_query_exists (flatpak_repo_dir, cancellable))
         {
            if (!g_file_make_directory_with_parents (flatpak_repo_dir, cancellable, &error))
              {
                g_task_return_error (task, error);
                return;
              }
          }

      ide_launcher = ide_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
      ide_subprocess_launcher_push_argv (ide_launcher, "flatpak");
      ide_subprocess_launcher_push_argv (ide_launcher, "remote-add");
      ide_subprocess_launcher_push_argv (ide_launcher, "--user");
      ide_subprocess_launcher_push_argv (ide_launcher, "--no-gpg-verify");
      ide_subprocess_launcher_push_argv (ide_launcher, "--if-not-exists");
      ide_subprocess_launcher_push_argv (ide_launcher, "gnome-builder-builds");
      ide_subprocess_launcher_push_argv (ide_launcher, flatpak_repo_path);
      process = ide_subprocess_launcher_spawn_sync (ide_launcher, cancellable, &error);

      if (!process || !ide_subprocess_wait_check (process, cancellable, &error))
        {
          g_task_return_error (task, error);
          return;
        }

      ide_configuration_set_flatpak_repo_dir (configuration, flatpak_repo_dir);
      ide_configuration_set_flatpak_repo_name (configuration, "gnome-builder-builds");

      //TODO override runtime in manifest
      ide_launcher2 = ide_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
      ide_subprocess_launcher_push_argv (ide_launcher2, "flatpak-builder");
      ide_subprocess_launcher_push_argv (ide_launcher2, "-v");
      ide_subprocess_launcher_push_argv (ide_launcher2, "--ccache");
      ide_subprocess_launcher_push_argv (ide_launcher2, "--force-clean");
      //TODO guess module name
      ide_subprocess_launcher_push_argv (ide_launcher2, "--stop-at=my-gnome-app");
      ide_subprocess_launcher_push_argv (ide_launcher2, g_strdup_printf ("--repo=%s", flatpak_repo_path));
      ide_subprocess_launcher_push_argv (ide_launcher2, "--subject=\"Development build\"");
      ide_subprocess_launcher_push_argv (ide_launcher2, build_path);
      ide_subprocess_launcher_push_argv (ide_launcher2, manifest_path);
      process2 = ide_subprocess_launcher_spawn_sync (ide_launcher2, cancellable, &error);

      if (!process2 || !ide_subprocess_wait_check (process2, cancellable, &error))
        {
          g_task_return_error (task, error);
          return;
        }

      g_task_return_boolean (task, TRUE);
      return;
    }

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  args = g_ptr_array_new ();
  g_ptr_array_add (args, "flatpak");
  g_ptr_array_add (args, "build-init");
  g_ptr_array_add (args, build_path);
  /* XXX: Fake name, probably okay, but can be proper once we get
   * IdeConfiguration in place.
   */
  g_ptr_array_add (args, "org.gnome.Builder.FlatpakApp.Build");
  g_ptr_array_add (args, self->sdk);
  g_ptr_array_add (args, self->platform);
  g_ptr_array_add (args, self->branch);
  g_ptr_array_add (args, NULL);

#ifdef IDE_ENABLE_TRACE
  {
    g_autofree gchar *str = NULL;
    str = g_strjoinv (" ", (gchar **)args->pdata);
    IDE_TRACE_MSG ("Launching '%s'", str);
  }
#endif

  subprocess = g_subprocess_launcher_spawnv (launcher,
                                             (const gchar * const *)args->pdata,
                                             &error);

  g_ptr_array_free (args, TRUE);

  if (!subprocess || !g_subprocess_wait_check (subprocess, cancellable, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
gbp_flatpak_runtime_prebuild_async (IdeRuntime          *runtime,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  GbpFlatpakRuntime *self = (GbpFlatpakRuntime *)runtime;
  g_autoptr(GTask) task = NULL;

  g_assert (GBP_IS_FLATPAK_RUNTIME (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_run_in_thread (task, gbp_flatpak_runtime_prebuild_worker);
}

static gboolean
gbp_flatpak_runtime_prebuild_finish (IdeRuntime    *runtime,
                                     GAsyncResult  *result,
                                     GError       **error)
{
  GbpFlatpakRuntime *self = (GbpFlatpakRuntime *)runtime;

  g_assert (GBP_IS_FLATPAK_RUNTIME (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gbp_flatpak_runtime_postinstall_worker (GTask        *task,
                                        gpointer      source_object,
                                        gpointer      task_data,
                                        GCancellable *cancellable)
{
  GbpFlatpakRuntime *self = source_object;
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) subprocess = NULL;
  GError *error = NULL;
  GPtrArray *args;
  IdeContext *context;
  IdeConfigurationManager *config_manager;
  IdeConfiguration *configuration;
  const gchar *repo_name;

  g_assert (G_IS_TASK (task));
  g_assert (GBP_IS_FLATPAK_RUNTIME (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  context = ide_object_get_context (IDE_OBJECT (self));
  config_manager = ide_context_get_configuration_manager (context);
  configuration = ide_configuration_manager_get_current (config_manager);

  repo_name = ide_configuration_get_flatpak_repo_name (configuration);

  //TODO this won't work if it's already installed
  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  args = g_ptr_array_new ();
  g_ptr_array_add (args, "flatpak");
  g_ptr_array_add (args, "install");
  g_ptr_array_add (args, "--user");
  g_ptr_array_add (args, "--app");
  g_ptr_array_add (args, g_strdup (repo_name));
  //TODO fix name
  g_ptr_array_add (args, "org.gnome.MyGnomeApp");
  g_ptr_array_add (args, NULL);

#ifdef IDE_ENABLE_TRACE
  {
    g_autofree gchar *str = NULL;
    str = g_strjoinv (" ", (gchar **)args->pdata);
    IDE_TRACE_MSG ("Launching '%s'", str);
  }
#endif

  subprocess = g_subprocess_launcher_spawnv (launcher,
                                             (const gchar * const *)args->pdata,
                                             &error);

  g_ptr_array_free (args, TRUE);

  if (!subprocess || !g_subprocess_wait_check (subprocess, cancellable, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
gbp_flatpak_runtime_postinstall_async (IdeRuntime          *runtime,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
  GbpFlatpakRuntime *self = (GbpFlatpakRuntime *)runtime;
  g_autoptr(GTask) task = NULL;

  g_assert (GBP_IS_FLATPAK_RUNTIME (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_run_in_thread (task, gbp_flatpak_runtime_postinstall_worker);
}

static gboolean
gbp_flatpak_runtime_postinstall_finish (IdeRuntime    *runtime,
                                        GAsyncResult  *result,
                                        GError       **error)
{
  GbpFlatpakRuntime *self = (GbpFlatpakRuntime *)runtime;

  g_assert (GBP_IS_FLATPAK_RUNTIME (self));
  g_assert (G_IS_TASK (result));

  return g_task_propagate_boolean (G_TASK (result), error);
}

static IdeSubprocessLauncher *
gbp_flatpak_runtime_create_launcher (IdeRuntime  *runtime,
                                     GError     **error)
{
  IdeSubprocessLauncher *ret;
  GbpFlatpakRuntime *self = (GbpFlatpakRuntime *)runtime;

  g_return_val_if_fail (GBP_IS_FLATPAK_RUNTIME (self), NULL);

  ret = IDE_RUNTIME_CLASS (gbp_flatpak_runtime_parent_class)->create_launcher (runtime, error);

  if (ret != NULL)
    {
      g_autofree gchar *build_path = get_build_directory (self);

      ide_subprocess_launcher_push_argv (ret, "flatpak");
      ide_subprocess_launcher_push_argv (ret, "build");
      ide_subprocess_launcher_push_argv (ret, build_path);

      ide_subprocess_launcher_set_run_on_host (ret, TRUE);
    }

  return ret;
}

static void
gbp_flatpak_runtime_prepare_configuration (IdeRuntime       *runtime,
                                           IdeConfiguration *configuration)
{
  g_assert (IDE_IS_RUNTIME (runtime));
  g_assert (IDE_IS_CONFIGURATION (configuration));

  ide_configuration_set_flatpak_manifest (configuration, g_file_new_for_path ("/home/mwleeds/Projects/my-gnome-app/org.gnome.MyGnomeApp.flatpak.json"));
  //TODO set flatpak manifest file in configuration properly
  //TODO set flatpak repo?

  ide_configuration_set_prefix (configuration, "/app");
}

static void
gbp_flatpak_runtime_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  GbpFlatpakRuntime *self = GBP_FLATPAK_RUNTIME(object);

  switch (prop_id)
    {
    case PROP_BRANCH:
      g_value_set_string (value, self->branch);
      break;

    case PROP_PLATFORM:
      g_value_set_string (value, self->platform);
      break;

    case PROP_SDK:
      g_value_set_string (value, self->sdk);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
gbp_flatpak_runtime_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GbpFlatpakRuntime *self = GBP_FLATPAK_RUNTIME(object);

  switch (prop_id)
    {
    case PROP_BRANCH:
      self->branch = g_value_dup_string (value);
      break;

    case PROP_PLATFORM:
      self->platform = g_value_dup_string (value);
      break;

    case PROP_SDK:
      self->sdk = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
gbp_flatpak_runtime_finalize (GObject *object)
{
  GbpFlatpakRuntime *self = (GbpFlatpakRuntime *)object;

  g_clear_pointer (&self->sdk, g_free);
  g_clear_pointer (&self->platform, g_free);
  g_clear_pointer (&self->branch, g_free);

  G_OBJECT_CLASS (gbp_flatpak_runtime_parent_class)->finalize (object);
}

static void
gbp_flatpak_runtime_class_init (GbpFlatpakRuntimeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  IdeRuntimeClass *runtime_class = IDE_RUNTIME_CLASS (klass);

  object_class->finalize = gbp_flatpak_runtime_finalize;
  object_class->get_property = gbp_flatpak_runtime_get_property;
  object_class->set_property = gbp_flatpak_runtime_set_property;

  runtime_class->prebuild_async = gbp_flatpak_runtime_prebuild_async;
  runtime_class->prebuild_finish = gbp_flatpak_runtime_prebuild_finish;
  runtime_class->postinstall_async = gbp_flatpak_runtime_postinstall_async;
  runtime_class->postinstall_finish = gbp_flatpak_runtime_postinstall_finish;
  runtime_class->create_launcher = gbp_flatpak_runtime_create_launcher;
  runtime_class->contains_program_in_path = gbp_flatpak_runtime_contains_program_in_path;
  runtime_class->prepare_configuration = gbp_flatpak_runtime_prepare_configuration;

  properties [PROP_BRANCH] =
    g_param_spec_string ("branch",
                         "Branch",
                         "Branch",
                         "master",
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT |
                          G_PARAM_STATIC_STRINGS));

  properties [PROP_PLATFORM] =
    g_param_spec_string ("platform",
                         "Platform",
                         "Platform",
                         "org.gnome.Platform",
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT |
                          G_PARAM_STATIC_STRINGS));

  properties [PROP_SDK] =
    g_param_spec_string ("sdk",
                         "Sdk",
                         "Sdk",
                         "org.gnome.Sdk",
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, LAST_PROP, properties);
}

static void
gbp_flatpak_runtime_init (GbpFlatpakRuntime *self)
{
}
