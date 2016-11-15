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

#define G_LOG_DOMAIN "gbp-flatpak-runtime"

#include "gbp-flatpak-runtime.h"
#include "gbp-flatpak-subprocess-launcher.h"

struct _GbpFlatpakRuntime
{
  IdeRuntime parent_instance;

  gchar *sdk;
  gchar *platform;
  gchar *branch;
  gchar *primary_module;
  gchar *app_id;
  GFile *manifest;
};

G_DEFINE_TYPE (GbpFlatpakRuntime, gbp_flatpak_runtime, IDE_TYPE_RUNTIME)

enum {
  PROP_0,
  PROP_BRANCH,
  PROP_PLATFORM,
  PROP_SDK,
  PROP_PRIMARY_MODULE,
  PROP_APP_ID,
  PROP_MANIFEST,
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

  subprocess = ide_subprocess_launcher_spawn (launcher, cancellable, NULL);

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
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  g_autoptr(IdeSubprocess) subprocess = NULL;
  g_autoptr(GFile) parent = NULL;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (GBP_IS_FLATPAK_RUNTIME (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  build_path = get_build_directory (self);
  build_dir = g_file_new_for_path (build_path);

  if (g_file_query_exists (build_dir, cancellable))
    {
      g_task_return_boolean (task, TRUE);
      return;
    }

  parent = g_file_get_parent (build_dir);

  if (!g_file_query_exists (parent, cancellable))
    {
      if (!g_file_make_directory_with_parents (parent, cancellable, &error))
        {
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }
    }

  launcher = IDE_RUNTIME_CLASS (gbp_flatpak_runtime_parent_class)->create_launcher (IDE_RUNTIME (self), &error);

  if (launcher == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  ide_subprocess_launcher_set_run_on_host (launcher, TRUE);
  ide_subprocess_launcher_set_clear_env (launcher, FALSE);

  ide_subprocess_launcher_push_argv (launcher, "flatpak");
  ide_subprocess_launcher_push_argv (launcher, "build-init");
  ide_subprocess_launcher_push_argv (launcher, build_path);
  /* XXX: Fake name, probably okay, but can be proper once we get
   * IdeConfiguration in place.
   */
  ide_subprocess_launcher_push_argv (launcher, "org.gnome.Builder.FlatpakApp.Build");
  ide_subprocess_launcher_push_argv (launcher, self->sdk);
  ide_subprocess_launcher_push_argv (launcher, self->platform);
  ide_subprocess_launcher_push_argv (launcher, self->branch);

  subprocess = ide_subprocess_launcher_spawn (launcher, cancellable, &error);

  if (subprocess == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (!ide_subprocess_wait_check (subprocess, cancellable, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
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

static IdeSubprocessLauncher *
gbp_flatpak_runtime_create_launcher (IdeRuntime  *runtime,
                                     GError     **error)
{
  IdeSubprocessLauncher *ret;
  GbpFlatpakRuntime *self = (GbpFlatpakRuntime *)runtime;

  g_return_val_if_fail (GBP_IS_FLATPAK_RUNTIME (self), NULL);

  ret = gbp_flatpak_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);

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
  GbpFlatpakRuntime* self = (GbpFlatpakRuntime *)runtime;

  g_assert (GBP_IS_FLATPAK_RUNTIME (self));
  g_assert (IDE_IS_CONFIGURATION (configuration));

  if (!ide_str_empty0 (self->app_id))
    ide_configuration_set_app_id (configuration, self->app_id);
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

    case PROP_PRIMARY_MODULE:
      g_value_set_string (value, self->primary_module);
      break;

    case PROP_APP_ID:
      g_value_set_string (value, self->app_id);
      break;

    case PROP_MANIFEST:
      g_value_set_object (value, self->manifest);
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

    case PROP_PRIMARY_MODULE:
      self->primary_module = g_value_dup_string (value);
      break;

    case PROP_APP_ID:
      self->app_id = g_value_dup_string (value);
      break;

    case PROP_MANIFEST:
      self->manifest = g_value_dup_object (value);
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
  g_clear_pointer (&self->primary_module, g_free);
  g_clear_pointer (&self->app_id, g_free);
  g_clear_object (&self->manifest);

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

  properties [PROP_PRIMARY_MODULE] =
    g_param_spec_string ("primary-module",
                         "Primary module",
                         "Primary module",
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT |
                          G_PARAM_STATIC_STRINGS));

  properties [PROP_APP_ID] =
    g_param_spec_string ("app-id",
                         "App ID",
                         "App ID",
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT |
                          G_PARAM_STATIC_STRINGS));

  properties [PROP_MANIFEST] =
    g_param_spec_object ("manifest",
                         "Manifest",
                         "Manifest file for use with flatpak-builder",
                         G_TYPE_FILE,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, LAST_PROP, properties);
}

static void
gbp_flatpak_runtime_init (GbpFlatpakRuntime *self)
{
}
