/* ide-omni-bar.c
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

#define G_LOG_DOMAIN "ide-omni-bar"

#include <egg-signal-group.h>

#include "ide-context.h"
#include "ide-debug.h"

#include "buildsystem/ide-build-result.h"
#include "projects/ide-project.h"
#include "util/ide-gtk.h"
#include "vcs/ide-vcs.h"
#include "workbench/ide-omni-bar.h"

struct _IdeOmniBar
{
  GtkBox          parent_instance;

  EggSignalGroup *build_result_signals;

  GtkLabel       *branch_label;
  GtkLabel       *project_label;
  GtkLabel       *build_result_mode_label;
  GtkImage       *build_result_diagnostics_image;
  GtkButton      *build_button;
  GtkImage       *build_button_image;
  GtkLabel       *config_name_label;
  GtkStack       *message_stack;
};

G_DEFINE_TYPE (IdeOmniBar, ide_omni_bar, GTK_TYPE_BOX)

static void
ide_omni_bar_update (IdeOmniBar *self)
{
  g_autofree gchar *branch_name = NULL;
  const gchar *project_name = NULL;
  IdeContext *context;

  g_assert (IDE_IS_OMNI_BAR (self));

  context = ide_widget_get_context (GTK_WIDGET (self));

  if (IDE_IS_CONTEXT (context))
    {
      IdeProject *project;
      IdeVcs *vcs;

      project = ide_context_get_project (context);
      project_name = ide_project_get_name (project);

      vcs = ide_context_get_vcs (context);
      branch_name = ide_vcs_get_branch_name (vcs);
    }

  gtk_label_set_label (self->project_label, project_name);
  gtk_label_set_label (self->branch_label, branch_name);
}

static void
ide_omni_bar_context_set (GtkWidget  *widget,
                          IdeContext *context)
{
  IdeOmniBar *self = (IdeOmniBar *)widget;

  IDE_ENTRY;

  g_assert (IDE_IS_OMNI_BAR (self));
  g_assert (!context || IDE_IS_CONTEXT (context));

  ide_omni_bar_update (self);

  if (context != NULL)
    {
      IdeVcs *vcs = ide_context_get_vcs (context);
      IdeConfigurationManager *configs = ide_context_get_configuration_manager (context);

      g_signal_connect_object (vcs,
                               "changed",
                               G_CALLBACK (ide_omni_bar_update),
                               self,
                               G_CONNECT_SWAPPED);

      g_object_bind_property (configs, "current-display-name",
                              self->config_name_label, "label",
                              G_BINDING_SYNC_CREATE);
    }

  IDE_EXIT;
}

static void
ide_omni_bar_build_result_notify_mode (IdeOmniBar     *self,
                                       GParamSpec     *pspec,
                                       IdeBuildResult *result)
{
  g_autofree gchar *mode = NULL;

  g_assert (IDE_IS_OMNI_BAR (self));
  g_assert (pspec != NULL);
  g_assert (IDE_IS_BUILD_RESULT (result));

  mode = ide_build_result_get_mode (result);

  gtk_label_set_label (self->build_result_mode_label, mode);
}

static void
ide_omni_bar_build_result_notify_running (IdeOmniBar     *self,
                                          GParamSpec     *pspec,
                                          IdeBuildResult *result)
{
  g_assert (IDE_IS_OMNI_BAR (self));
  g_assert (pspec != NULL);
  g_assert (IDE_IS_BUILD_RESULT (result));

  if (ide_build_result_get_running (result))
    {
      g_object_set (self->build_button_image,
                    "icon-name", "process-stop-symbolic",
                    NULL);
      g_object_set (self->build_button,
                    "action-name", "build-tools.cancel-build",
                    NULL);

      gtk_stack_set_visible_child (self->message_stack,
                                   GTK_WIDGET (self->build_result_mode_label));
    }
  else
    {
      g_object_set (self->build_button_image,
                    "icon-name", "system-run-symbolic",
                    NULL);
      g_object_set (self->build_button,
                    "action-name", "build-tools.build",
                    NULL);
    }
}

static void
ide_omni_bar_build_result_diagnostic (IdeOmniBar     *self,
                                      IdeDiagnostic  *diagnostic,
                                      IdeBuildResult *result)
{
  g_assert (IDE_IS_OMNI_BAR (self));
  g_assert (diagnostic != NULL);
  g_assert (IDE_IS_BUILD_RESULT (result));

  gtk_widget_show (GTK_WIDGET (self->build_result_diagnostics_image));
}

static void
ide_omni_bar_finalize (GObject *object)
{
  IdeOmniBar *self = (IdeOmniBar *)object;

  g_clear_object (&self->build_result_signals);

  G_OBJECT_CLASS (ide_omni_bar_parent_class)->finalize (object);
}

static void
ide_omni_bar_class_init (IdeOmniBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = ide_omni_bar_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/builder/ui/ide-omni-bar.ui");
  gtk_widget_class_set_css_name (widget_class, "omnibar");
  gtk_widget_class_bind_template_child (widget_class, IdeOmniBar, branch_label);
  gtk_widget_class_bind_template_child (widget_class, IdeOmniBar, build_button);
  gtk_widget_class_bind_template_child (widget_class, IdeOmniBar, build_button_image);
  gtk_widget_class_bind_template_child (widget_class, IdeOmniBar, build_result_diagnostics_image);
  gtk_widget_class_bind_template_child (widget_class, IdeOmniBar, build_result_mode_label);
  gtk_widget_class_bind_template_child (widget_class, IdeOmniBar, config_name_label);
  gtk_widget_class_bind_template_child (widget_class, IdeOmniBar, message_stack);
  gtk_widget_class_bind_template_child (widget_class, IdeOmniBar, project_label);
}

static void
ide_omni_bar_init (IdeOmniBar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->build_result_signals = egg_signal_group_new (IDE_TYPE_BUILD_RESULT);

  egg_signal_group_connect_object (self->build_result_signals,
                                   "notify::mode",
                                   G_CALLBACK (ide_omni_bar_build_result_notify_mode),
                                   self,
                                   G_CONNECT_SWAPPED);
  egg_signal_group_connect_object (self->build_result_signals,
                                   "notify::running",
                                   G_CALLBACK (ide_omni_bar_build_result_notify_running),
                                   self,
                                   G_CONNECT_SWAPPED);
  egg_signal_group_connect_object (self->build_result_signals,
                                   "diagnostic",
                                   G_CALLBACK (ide_omni_bar_build_result_diagnostic),
                                   self,
                                   G_CONNECT_SWAPPED);

  ide_widget_set_context_handler (self, ide_omni_bar_context_set);
}

GtkWidget *
ide_omni_bar_new (void)
{
  return g_object_new (IDE_TYPE_OMNI_BAR, NULL);
}

/**
 * ide_omni_bar_get_build_result:
 *
 * Gets the current build result that is being visualized in the omni bar.
 *
 * Returns: (nullable) (transfer none): An #IdeBuildResult or %NULL.
 */
IdeBuildResult *
ide_omni_bar_get_build_result (IdeOmniBar *self)
{
  g_return_val_if_fail (IDE_IS_OMNI_BAR (self), NULL);

  return egg_signal_group_get_target (self->build_result_signals);
}

void
ide_omni_bar_set_build_result (IdeOmniBar     *self,
                               IdeBuildResult *build_result)
{
  g_return_if_fail (IDE_IS_OMNI_BAR (self));
  g_return_if_fail (!build_result || IDE_IS_BUILD_RESULT (build_result));

  gtk_widget_hide (GTK_WIDGET (self->build_result_diagnostics_image));
  egg_signal_group_set_target (self->build_result_signals, build_result);
}
