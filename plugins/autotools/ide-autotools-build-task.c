/* ide-autotools-build-task.c
 *
 * Copyright (C) 2015 Christian Hergert <christian@hergert.me>
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

#define G_LOG_DOMAIN "ide-autotools-build-task"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <fcntl.h>
#include <glib/gi18n.h>
#include <ide.h>
#include <stdlib.h>
#include <unistd.h>

#include "ide-autotools-build-task.h"

struct _IdeAutotoolsBuildTask
{
  IdeObject         parent;
  IdeConfiguration *configuration;
  IdeBuildResult   *build_result;
};

typedef struct
{
  gchar                 *directory_path;
  gchar                 *project_path;
  gchar                 *parallel;
  gchar                **configure_argv;
  gchar                **make_targets;
  IdeRuntime            *runtime;
  guint                  require_autogen : 1;
  guint                  require_configure : 1;
  guint                  bootstrap_only : 1;
} BuildWorkerState;

typedef gboolean (*WorkStep) (GTask                 *task,
                              IdeAutotoolsBuildTask *self,
                              BuildWorkerState      *state,
                              GCancellable          *cancellable);

G_DEFINE_TYPE (IdeAutotoolsBuildTask, ide_autotools_build_task, IDE_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_CONFIGURATION,
  PROP_BUILD_RESULT,
  LAST_PROP
};

static IdeSubprocess *log_and_spawn     (IdeAutotoolsBuildTask  *self,
                                         IdeSubprocessLauncher  *launcher,
                                         GCancellable           *cancellable,
                                         GError                **error,
                                         const gchar            *argv0,
                                         ...) G_GNUC_NULL_TERMINATED;
static gboolean       step_mkdirs       (GTask                  *task,
                                         IdeAutotoolsBuildTask  *self,
                                         BuildWorkerState       *state,
                                         GCancellable           *cancellable);
static gboolean       step_autogen      (GTask                  *task,
                                         IdeAutotoolsBuildTask  *self,
                                         BuildWorkerState       *state,
                                         GCancellable           *cancellable);
static gboolean       step_configure    (GTask                  *task,
                                         IdeAutotoolsBuildTask  *self,
                                         BuildWorkerState       *state,
                                         GCancellable           *cancellable);
static gboolean       step_make_all     (GTask                  *task,
                                         IdeAutotoolsBuildTask  *self,
                                         BuildWorkerState       *state,
                                         GCancellable           *cancellable);
static void           apply_environment (IdeAutotoolsBuildTask  *self,
                                         IdeSubprocessLauncher  *launcher);

static GParamSpec *properties [LAST_PROP];
static WorkStep workSteps [] = {
  step_mkdirs,
  step_autogen,
  step_configure,
  step_make_all,
  NULL
};

/**
 * ide_autotools_build_task_get_configuration:
 * @self: An #IdeAutotoolsBuildTask
 *
 * Gets the configuration to use for the build.
 */
IdeConfiguration *
ide_autotools_build_task_get_configuration (IdeAutotoolsBuildTask *self)
{
  g_return_val_if_fail (IDE_IS_AUTOTOOLS_BUILD_TASK (self), NULL);

  return self->configuration;
}

static void
ide_autotools_build_task_set_configuration (IdeAutotoolsBuildTask *self,
                                            IdeConfiguration      *configuration)
{
  g_assert (IDE_IS_AUTOTOOLS_BUILD_TASK (self));
  g_assert (IDE_IS_CONFIGURATION (configuration));

  if (g_set_object (&self->configuration, configuration))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CONFIGURATION]);
}

/**
 * ide_autotools_build_task_get_build_result:
 * @self: An #IdeAutotoolsBuildTask
 *
 * Gets the #IdeBuildResult instance being used.
 */
IdeBuildResult *
ide_autotools_build_task_get_build_result (IdeAutotoolsBuildTask *self)
{
  g_return_val_if_fail (IDE_IS_AUTOTOOLS_BUILD_TASK (self), NULL);

  return self->build_result;
}

static void
ide_autotools_build_task_set_build_result (IdeAutotoolsBuildTask *self,
                                           IdeBuildResult        *build_result)
{
  g_assert (IDE_IS_AUTOTOOLS_BUILD_TASK (self));
  g_assert (IDE_IS_BUILD_RESULT (build_result));

  if (g_set_object (&self->build_result, build_result))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_BUILD_RESULT]);
}

static void
ide_autotools_build_task_finalize (GObject *object)
{
  IdeAutotoolsBuildTask *self = (IdeAutotoolsBuildTask *)object;

  g_clear_object (&self->configuration);

  G_OBJECT_CLASS (ide_autotools_build_task_parent_class)->finalize (object);
}

static void
ide_autotools_build_task_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  IdeAutotoolsBuildTask *self = IDE_AUTOTOOLS_BUILD_TASK (object);

  switch (prop_id)
    {
    case PROP_CONFIGURATION:
      g_value_set_object (value, ide_autotools_build_task_get_configuration (self));
      break;

    case PROP_BUILD_RESULT:
      g_value_set_object (value, ide_autotools_build_task_get_build_result (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_autotools_build_task_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  IdeAutotoolsBuildTask *self = IDE_AUTOTOOLS_BUILD_TASK (object);

  switch (prop_id)
    {
    case PROP_CONFIGURATION:
      ide_autotools_build_task_set_configuration (self, g_value_get_object (value));
      break;

    case PROP_BUILD_RESULT:
      ide_autotools_build_task_set_build_result (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_autotools_build_task_class_init (IdeAutotoolsBuildTaskClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ide_autotools_build_task_finalize;
  object_class->get_property = ide_autotools_build_task_get_property;
  object_class->set_property = ide_autotools_build_task_set_property;

  properties [PROP_CONFIGURATION] =
    g_param_spec_object ("configuration",
                        "Configuration",
                        "The configuration for this build.",
                        IDE_TYPE_CONFIGURATION,
                        (G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS));

  properties [PROP_BUILD_RESULT] =
    g_param_spec_object ("build-result",
                        "Build Result",
                        "The IdeBuildResult instance for this build.",
                        IDE_TYPE_BUILD_RESULT,
                        (G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, LAST_PROP, properties);
}

static void
ide_autotools_build_task_init (IdeAutotoolsBuildTask *self)
{
}

static BuildWorkerState *
worker_state_new (IdeAutotoolsBuildTask *self,
                  IdeRuntime            *runtime,
                  gchar                 *directory_path,
                  gchar                 *project_path,
                  gchar                **configure_argv,
                  gchar                **make_targets,
                  guint                  require_autogen,
                  guint                  require_configure,
                  guint                  bootstrap_only,
                  GError                **error)
{
  BuildWorkerState *state;
  gint val32;

  g_return_val_if_fail (IDE_IS_AUTOTOOLS_BUILD_TASK (self), NULL);
  g_return_val_if_fail (IDE_IS_CONFIGURATION (self->configuration), NULL);

  state = g_slice_new0 (BuildWorkerState);
  state->runtime = g_object_ref (runtime);
  state->directory_path = g_strdup (directory_path);
  state->project_path = g_strdup (project_path);
  state->configure_argv = g_strdupv (configure_argv);
  state->make_targets = g_strdupv (make_targets);
  state->require_autogen = require_autogen;
  state->require_configure = require_configure;
  state->bootstrap_only = bootstrap_only;

  val32 = ide_configuration_get_parallelism (self->configuration);

  if (val32 == -1)
    state->parallel = g_strdup_printf ("-j%u", g_get_num_processors () + 1);
  else if (val32 == 0)
    state->parallel = g_strdup_printf ("-j%u", g_get_num_processors ());
  else
    state->parallel = g_strdup_printf ("-j%u", val32);

  return state;
}

static void
worker_state_free (void *data)
{
  BuildWorkerState *state = data;

  g_free (state->directory_path);
  g_free (state->project_path);
  g_free (state->parallel);
  g_strfreev (state->configure_argv);
  g_strfreev (state->make_targets);
  g_clear_object (&state->runtime);
  g_slice_free (BuildWorkerState, state);
}

static void
ide_autotools_build_task_execute_worker (GTask        *task,
                                         gpointer      source_object,
                                         gpointer      task_data,
                                         GCancellable *cancellable)
{
  IdeAutotoolsBuildTask *self = source_object;
  BuildWorkerState *state = task_data;

  g_return_if_fail (G_IS_TASK (task));
  g_return_if_fail (IDE_IS_AUTOTOOLS_BUILD_TASK (self));
  g_return_if_fail (state);
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  for (guint i = 0; workSteps [i]; i++)
    {
      if (g_cancellable_is_cancelled (cancellable) ||
          !workSteps [i] (task, self, state, cancellable))
        return;
    }

  g_task_return_boolean (task, TRUE);
}

void
ide_autotools_build_task_execute_async (IdeAutotoolsBuildTask *self,
                                        IdeRuntime            *runtime,
                                        gchar                 *directory_path,
                                        gchar                 *project_path,
                                        gchar                **configure_argv,
                                        gchar                **make_targets,
                                        guint                  require_autogen,
                                        guint                  require_configure,
                                        guint                  bootstrap_only,
                                        GCancellable          *cancellable,
                                        GAsyncReadyCallback    callback,
                                        gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;
  GError *error = NULL;
  BuildWorkerState *state;

  IDE_ENTRY;

  g_return_if_fail (IDE_IS_AUTOTOOLS_BUILD_TASK (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (callback != NULL);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, ide_autotools_build_task_execute_async);

  state = worker_state_new (self,
                            runtime,
                            directory_path,
                            project_path,
                            configure_argv,
                            make_targets,
                            require_autogen,
                            require_configure,
                            bootstrap_only,
                            &error);

  if (state == NULL)
    {
      g_task_return_error (task, error);
      IDE_EXIT;
    }

  g_task_set_task_data (task, state, worker_state_free);

  g_task_run_in_thread (task, ide_autotools_build_task_execute_worker);

  IDE_EXIT;
}

gboolean
ide_autotools_build_task_execute_finish (IdeAutotoolsBuildTask  *self,
                                         GAsyncResult           *result,
                                         GError                **error)
{
  GTask *task = (GTask *)result;
  gboolean ret;

  IDE_ENTRY;

  g_return_val_if_fail (IDE_IS_AUTOTOOLS_BUILD_TASK (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (task), FALSE);

  ret = g_task_propagate_boolean (task, error);

  /* Mark the task as failed */
  if (ret == FALSE)
    ide_build_result_set_failed (self->build_result, TRUE);

  ide_build_result_set_running (self->build_result, FALSE);

  IDE_RETURN (ret);
}

static gboolean
log_in_main (gpointer data)
{
  struct {
    IdeBuildResult *result;
    gchar *message;
  } *pair = data;

  ide_build_result_log_stdout (pair->result, "%s", pair->message);

  g_free (pair->message);
  g_object_unref (pair->result);
  g_slice_free1 (sizeof *pair, pair);

  return G_SOURCE_REMOVE;
}

static IdeSubprocess *
log_and_spawn (IdeAutotoolsBuildTask  *self,
               IdeSubprocessLauncher  *launcher,
               GCancellable           *cancellable,
               GError                **error,
               const gchar            *argv0,
               ...)
{
  g_autoptr(GError) local_error = NULL;
  IdeSubprocess *ret;
  struct {
    IdeBuildResult *result;
    gchar *message;
  } *pair;
  GString *log;
  gchar *item;
  va_list args;
  gint popcnt = 0;

  g_assert (IDE_IS_AUTOTOOLS_BUILD_TASK (self));
  g_assert (IDE_IS_SUBPROCESS_LAUNCHER (launcher));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  log = g_string_new (argv0);
  ide_subprocess_launcher_push_argv (launcher, argv0);

  va_start (args, argv0);
  while (NULL != (item = va_arg (args, gchar *)))
    {
      ide_subprocess_launcher_push_argv (launcher, item);
      g_string_append_printf (log, " '%s'", item);
      popcnt++;
    }
  va_end (args);

  pair = g_slice_alloc (sizeof *pair);
  pair->result = g_object_ref (self->build_result);
  pair->message = g_string_free (log, FALSE);
  g_timeout_add (0, log_in_main, pair);

  ret = ide_subprocess_launcher_spawn_sync (launcher, cancellable, &local_error);

  if (ret == NULL)
    {
      ide_build_result_log_stderr (self->build_result, "%s %s",
                                   _("Build Failed: "),
                                   local_error->message);
      g_propagate_error (error, g_steal_pointer (&local_error));
    }

  /* pop make args */
  for (; popcnt; popcnt--)
    g_free (ide_subprocess_launcher_pop_argv (launcher));

  /* pop "make" */
  g_free (ide_subprocess_launcher_pop_argv (launcher));

  return ret;
}

static gboolean
step_mkdirs (GTask                 *task,
             IdeAutotoolsBuildTask *self,
             BuildWorkerState      *state,
             GCancellable          *cancellable)
{
  g_assert (G_IS_TASK (task));
  g_assert (IDE_IS_AUTOTOOLS_BUILD_TASK (self));
  g_assert (state);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (!g_file_test (state->directory_path, G_FILE_TEST_EXISTS))
    {
      if (g_mkdir_with_parents (state->directory_path, 0750) != 0)
        {
          g_task_return_new_error (task,
                                   G_IO_ERROR,
                                   G_IO_ERROR_FAILED,
                                   _("Failed to create build directory."));
          return FALSE;
        }
    }
  else if (!g_file_test (state->directory_path, G_FILE_TEST_IS_DIR))
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_DIRECTORY,
                               _("'%s' is not a directory."),
                               state->directory_path);
      return FALSE;
    }

  return TRUE;
}

static gboolean
step_autogen (GTask                 *task,
              IdeAutotoolsBuildTask *self,
              BuildWorkerState      *state,
              GCancellable          *cancellable)
{
  g_autofree gchar *autogen_sh_path = NULL;
  g_autofree gchar *configure_path = NULL;
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  g_autoptr(IdeSubprocess) process = NULL;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (IDE_IS_AUTOTOOLS_BUILD_TASK (self));
  g_assert (state);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  configure_path = g_build_filename (state->project_path, "configure", NULL);

  if (!state->require_autogen)
    {
      if (g_file_test (configure_path, G_FILE_TEST_IS_REGULAR))
        return TRUE;
    }

  autogen_sh_path = g_build_filename (state->project_path, "autogen.sh", NULL);
  if (!g_file_test (autogen_sh_path, G_FILE_TEST_EXISTS))
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               _("autogen.sh is missing from project directory (%s)."),
                               state->project_path);
      return FALSE;
    }

  if (!g_file_test (autogen_sh_path, G_FILE_TEST_IS_EXECUTABLE))
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               _("autogen.sh is not executable."));
      return FALSE;
    }

  ide_build_result_set_mode (self->build_result, _("Running autogen…"));

  if (NULL == (launcher = ide_runtime_create_launcher (state->runtime, &error)))
    {
      g_task_return_error (task, error);
      return FALSE;
    }

  ide_subprocess_launcher_set_cwd (launcher, state->project_path);
  ide_subprocess_launcher_setenv (launcher, "LANG", "C", TRUE);
  ide_subprocess_launcher_setenv (launcher, "NOCONFIGURE", "1", TRUE);
  apply_environment (self, launcher);

  process = log_and_spawn (self, launcher, cancellable, &error, autogen_sh_path, NULL);

  if (!process)
    {
      g_task_return_error (task, error);
      return FALSE;
    }

  ide_build_result_log_subprocess (self->build_result, process);

  if (!ide_subprocess_wait_check (process, cancellable, &error))
    {
      g_task_return_error (task, error);
      return FALSE;
    }

  if (!g_file_test (configure_path, G_FILE_TEST_IS_EXECUTABLE))
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_FAILED,
                               _("autogen.sh failed to create configure (%s)"),
                               configure_path);
      return FALSE;
    }

  return TRUE;
}

static gboolean
step_configure (GTask                 *task,
                IdeAutotoolsBuildTask *self,
                BuildWorkerState      *state,
                GCancellable          *cancellable)
{
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  g_autoptr(IdeSubprocess) process = NULL;
  g_autofree gchar *makefile_path = NULL;
  g_autofree gchar *config_log = NULL;
  GError *error = NULL;

  g_assert (G_IS_TASK (task));
  g_assert (IDE_IS_AUTOTOOLS_BUILD_TASK (self));
  g_assert (state);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (!state->require_configure)
    {
      /*
       * Skip configure if we already have a makefile.
       */
      makefile_path = g_build_filename (state->directory_path, "Makefile", NULL);
      if (g_file_test (makefile_path, G_FILE_TEST_EXISTS))
        return TRUE;
    }

  ide_build_result_set_mode (self->build_result, _("Running configure…"));

  if (NULL == (launcher = ide_runtime_create_launcher (state->runtime, &error)))
    return FALSE;

  ide_subprocess_launcher_set_flags (launcher,
                                     (G_SUBPROCESS_FLAGS_STDERR_PIPE |
                                      G_SUBPROCESS_FLAGS_STDOUT_PIPE));
  ide_subprocess_launcher_set_cwd (launcher, state->directory_path);
  ide_subprocess_launcher_setenv (launcher, "LANG", "C", TRUE);
  apply_environment (self, launcher);

  config_log = g_strjoinv (" ", state->configure_argv);
  ide_build_result_log_stdout (self->build_result, "%s", config_log);
  ide_subprocess_launcher_push_args (launcher, (const gchar * const *)state->configure_argv);

  if (NULL == (process = ide_subprocess_launcher_spawn_sync (launcher, cancellable, &error)))
    {
      g_task_return_error (task, error);
      return FALSE;
    }

  ide_build_result_log_subprocess (self->build_result, process);

  if (!ide_subprocess_wait_check (process, cancellable, &error))
    {
      g_task_return_error (task, error);
      return FALSE;
    }

  if (state->bootstrap_only)
    {
      g_task_return_boolean (task, TRUE);
      return FALSE;
    }

  return TRUE;
}

static gboolean
step_make_all  (GTask                 *task,
                IdeAutotoolsBuildTask *self,
                BuildWorkerState      *state,
                GCancellable          *cancellable)
{
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  g_autoptr(IdeSubprocess) process = NULL;
  const gchar * const *targets;
  const gchar *make = NULL;
  gchar *default_targets[] = { "all", NULL };
  GError *error = NULL;
  guint i;

  g_assert (G_IS_TASK (task));
  g_assert (IDE_IS_AUTOTOOLS_BUILD_TASK (self));
  g_assert (state);
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  if (NULL == (launcher = ide_runtime_create_launcher (state->runtime, &error)))
    {
      g_task_return_error (task, error);
      return FALSE;
    }

  ide_subprocess_launcher_set_flags (launcher,
                                     (G_SUBPROCESS_FLAGS_STDERR_PIPE |
                                      G_SUBPROCESS_FLAGS_STDOUT_PIPE));
  ide_subprocess_launcher_set_cwd (launcher, state->directory_path);
  ide_subprocess_launcher_setenv (launcher, "LANG", "C", TRUE);
  apply_environment (self, launcher);

  /*
   * Try to locate GNU make within the runtime.
   */
  if (ide_runtime_contains_program_in_path (state->runtime, "gmake", cancellable))
    make = "gmake";
  else if (ide_runtime_contains_program_in_path (state->runtime, "make", cancellable))
    make = "make";
  else
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_FOUND,
                               "Failed to locate make.");
      return FALSE;
    }

  if (!g_strv_length (state->make_targets))
    targets = (const gchar * const *)default_targets;
  else
    targets = (const gchar * const *)state->make_targets;

  for (i = 0; targets [i]; i++)
    {
      const gchar *target = targets [i];

      if (ide_str_equal0 (target, "clean"))
        ide_build_result_set_mode (self->build_result, _("Cleaning…"));
      else
        ide_build_result_set_mode (self->build_result, _("Building…"));

      process = log_and_spawn (self, launcher, cancellable, &error,
                               make, target, state->parallel, NULL);

      if (!process)
        {
          g_task_return_error (task, error);
          return FALSE;
        }

      ide_build_result_log_subprocess (self->build_result, process);

      if (!ide_subprocess_wait_check (process, cancellable, &error))
        {
          g_task_return_error (task, error);
          return FALSE;
        }
    }

  return TRUE;
}

static void
apply_environment (IdeAutotoolsBuildTask *self,
                   IdeSubprocessLauncher *launcher)
{
  IdeEnvironment *environment;

  g_assert (IDE_IS_AUTOTOOLS_BUILD_TASK (self));
  g_assert (IDE_IS_SUBPROCESS_LAUNCHER (launcher));

  environment = ide_configuration_get_environment (self->configuration);
  ide_subprocess_launcher_overlay_environment (launcher, environment);
}
