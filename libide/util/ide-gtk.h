/* ide-gtk.h
 *
 * Copyright (C) 2015 Christian Hergert <chergert@redhat.com>
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

#ifndef IDE_GTK_H
#define IDE_GTK_H

#include <gtk/gtk.h>

#include "ide-context.h"

#include "workbench/ide-workbench.h"

G_BEGIN_DECLS

typedef void (*IdeWidgetContextHandler) (GtkWidget  *widget,
                                         IdeContext *context);

gboolean      ide_widget_action              (GtkWidget               *widget,
                                              const gchar             *group,
                                              const gchar             *name,
                                              GVariant                *param);
gboolean      ide_widget_action_with_string  (GtkWidget               *widget,
                                              const gchar             *group,
                                              const gchar             *name,
                                              const gchar             *param);
void          ide_widget_set_context_handler (gpointer                 widget,
                                              IdeWidgetContextHandler  handler);
void          ide_widget_hide_with_fade      (GtkWidget               *widget);
void          ide_widget_show_with_fade      (GtkWidget               *widget);
void          ide_widget_add_style_class     (GtkWidget               *widget,
                                              const gchar             *class_name);
IdeWorkbench *ide_widget_get_workbench       (GtkWidget               *widget);
gpointer      ide_widget_find_child_typed    (GtkWidget               *widget,
                                              GType                    type);
void          ide_gtk_text_buffer_remove_tag (GtkTextBuffer           *buffer,
                                              GtkTextTag              *tag,
                                              const GtkTextIter       *start,
                                              const GtkTextIter       *end,
                                              gboolean                 minimal_damage);

G_END_DECLS

#endif /* IDE_GTK_H */
