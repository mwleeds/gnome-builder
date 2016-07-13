/* gstyle-utils.h
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

#ifndef GSTYLE_UTILS_H
#define GSTYLE_UTILS_H

#include <glib.h>
#include <cairo.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

gboolean              gstyle_str_empty0                  (const gchar     *str);
gboolean              gstyle_utf8_is_spaces              (const gchar     *str);
void                  draw_cairo_round_box               (cairo_t         *cr,
                                                          GdkRectangle     rect,
                                                          gint             tl_radius,
                                                          gint             tr_radius,
                                                          gint             bl_radius,
                                                          gint             br_radius);
void                  gstyle_utils_get_rect_resized_box  (GdkRectangle     src_rect,
                                                          GdkRectangle    *dst_rect,
                                                          GtkBorder       *offset);
cairo_pattern_t      *gstyle_utils_get_checkered_pattern (void);
void                  gstyle_utils_get_contrasted_rgba   (GdkRGBA          rgba,
                                                          GdkRGBA         *dst_rgba);

static inline guint32
pack_rgba24 (GdkRGBA *rgba)
{
  guint red, green, blue, alpha;
  guint32 result;

  alpha = CLAMP (rgba->alpha * 255, 0, 255);
  red = CLAMP (rgba->red * 255, 0, 255);
  green = CLAMP (rgba->green * 255, 0, 255);
  blue = CLAMP (rgba->blue * 255, 0, 255);
  result = (alpha << 24) | (red << 16) | (green << 8) | blue;

  return result;
}

static inline void
unpack_rgba24 (guint32  val,
               GdkRGBA *rgba)
{
  rgba->blue = (val & 0xFF) / 255.0;
  val >>= 8;
  rgba->green = (val & 0xFF) / 255.0;
  val >>= 8;
  rgba->red = (val & 0xFF) / 255.0;
  val >>= 8;
  rgba->alpha = (val & 0xFF) / 255.0;
}

static inline gboolean
gstyle_utils_cmp_border (GtkBorder border1,
                         GtkBorder border2)
{
  if (border1.left != border2.left ||
      border1.right != border2.right ||
      border1.top != border2.top ||
      border1.bottom != border2.bottom)
    return FALSE;
  else
    return TRUE;
}

#define gstyle_clear_weak_pointer(ptr) \
  (*(ptr) ? (g_object_remove_weak_pointer((GObject*)*(ptr), (gpointer*)ptr),*(ptr)=NULL,1) : 0)

#define gstyle_set_weak_pointer(ptr,obj) \
  ((obj!=*(ptr))?(gstyle_clear_weak_pointer(ptr),*(ptr)=obj,((obj)?g_object_add_weak_pointer((GObject*)obj,(gpointer*)ptr),NULL:NULL),1):0)

G_END_DECLS

#endif /* GSTYLE_UTILS_H */
