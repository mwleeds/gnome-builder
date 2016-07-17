/* ide-runner-addin.h
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

#ifndef IDE_RUNNER_ADDIN_H
#define IDE_RUNNER_ADDIN_H

#include <gio/gio.h>

#include "ide-types.h"
#include "ide-runner.h"

G_BEGIN_DECLS

#define IDE_TYPE_RUNNER_ADDIN (ide_runner_addin_get_type())

G_DECLARE_INTERFACE (IdeRunnerAddin, ide_runner_addin, IDE, RUNNER_ADDIN, GObject)

struct _IdeRunnerAddinInterface
{
  GTypeInterface parent_interface;

  void     (*load)            (IdeRunnerAddin       *self,
                               IdeRunner            *runner);
  void     (*unload)          (IdeRunnerAddin       *self,
                               IdeRunner            *runner);
  void     (*prehook_async)   (IdeRunnerAddin       *self,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data);
  gboolean (*prehook_finish)  (IdeRunnerAddin       *self,
                               GAsyncResult         *result,
                               GError              **error);
  void     (*posthook_async)  (IdeRunnerAddin       *self,
                               GCancellable         *cancellable,
                               GAsyncReadyCallback   callback,
                               gpointer              user_data);
  gboolean (*posthook_finish) (IdeRunnerAddin       *self,
                               GAsyncResult         *result,
                               GError              **error);
};

void     ide_runner_addin_load            (IdeRunnerAddin       *self,
                                           IdeRunner            *runner);
void     ide_runner_addin_unload          (IdeRunnerAddin       *self,
                                           IdeRunner            *runner);
void     ide_runner_addin_prehook_async   (IdeRunnerAddin       *self,
                                           GCancellable         *cancellable,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data);
gboolean ide_runner_addin_prehook_finish  (IdeRunnerAddin       *self,
                                           GAsyncResult         *result,
                                           GError              **error);
void     ide_runner_addin_posthook_async  (IdeRunnerAddin       *self,
                                           GCancellable         *cancellable,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data);
gboolean ide_runner_addin_posthook_finish (IdeRunnerAddin       *self,
                                           GAsyncResult         *result,
                                           GError              **error);

G_END_DECLS

#endif /* IDE_RUNNER_ADDIN_H */
