/* gbp-create-project-widget.h
 *
 * Copyright (C) 2016 Christian Hergert <christian@hergert.me>
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

#ifndef GBP_CREATE_PROJECT_WIDGET_H
#define GBP_CREATE_PROJECT_WIDGET_H

#include <gtk/gtk.h>
#include <ide.h>

G_BEGIN_DECLS

#define GBP_TYPE_CREATE_PROJECT_WIDGET (gbp_create_project_widget_get_type())

G_DECLARE_FINAL_TYPE (GbpCreateProjectWidget, gbp_create_project_widget, GBP, CREATE_PROJECT_WIDGET, GtkBin)

void gbp_create_project_widget_create_async (GbpCreateProjectWidget *self,
                                             GCancellable           *cancellable,
                                             GAsyncReadyCallback     callback,
                                             gpointer                user_data);
gboolean gbp_create_project_widget_create_finish (GbpCreateProjectWidget *self,
                                                  GAsyncResult           *result,
                                                  GError                **error);

G_END_DECLS

#endif /* GBP_CREATE_PROJECT_WIDGET_H */
