/* gb-color-picker-workbench-addin.h
 *
 * Copyright (C) 2016 sebastien lafargue <slafargue@gnome.org>
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

#ifndef GB_COLOR_PICKER_WORKBENCH_ADDIN_H
#define GB_COLOR_PICKER_WORKBENCH_ADDIN_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GB_TYPE_COLOR_PICKER_WORKBENCH_ADDIN (gb_color_picker_workbench_addin_get_type())

G_DECLARE_FINAL_TYPE (GbColorPickerWorkbenchAddin, gb_color_picker_workbench_addin, GB, COLOR_PICKER_WORKBENCH_ADDIN, GObject)

G_END_DECLS

#endif /* GB_COLOR_PICKER_WORKBENCH_ADDIN_H */
