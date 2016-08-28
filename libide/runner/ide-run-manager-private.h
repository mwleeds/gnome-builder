/* ide-run-manager-private.h
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

#ifndef IDE_RUN_MANAGER_PRIVATE_H
#define IDE_RUN_MANAGER_PRIVATE_H

#include "ide-run-manager.h"

G_BEGIN_DECLS

typedef struct
{
  gchar          *id;
  gchar          *title;
  gchar          *icon_name;
  gchar          *accel;
  gint            priority;
  IdeRunHandler   handler;
  gpointer        handler_data;
  GDestroyNotify  handler_data_destroy;
} IdeRunHandlerInfo;

const GList *_ide_run_manager_get_handlers (IdeRunManager *self);

G_END_DECLS

#endif /* IDE_RUN_MANAGER_PRIVATE_H */