/* gstyle-palette.c
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

#define G_LOG_DOMAIN "gstyle-palette"

#include <glib/gi18n.h>
#include <string.h>

#include "gstyle-private.h"
#include "gstyle-palette.h"

#define XML_TO_CHAR(s)  ((char *) (s))
#define CHAR_TO_XML(s)  ((unsigned char *) (s))

#define GSTYLE_PALETTE_ID_CHARSET "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_"

struct _GstylePalette
{
  GObject     parent_instance;

  GPtrArray  *colors;
  GHashTable *color_names;
  gchar      *id;
  gchar      *name;
  GFile      *file;
};

static void gstyle_palette_list_model_iface_init (GListModelInterface *iface);

G_DEFINE_QUARK (gstyle_palette_error, gstyle_palette_error)

G_DEFINE_TYPE_WITH_CODE (GstylePalette, gstyle_palette, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, gstyle_palette_list_model_iface_init));

enum {
  PROP_0,
  PROP_ID,
  PROP_NAME,
  PROP_FILE,
  PROP_COLORS,
  PROP_LEN,
  N_PROPS
};

static inline gchar *
strdup_and_xmlfree (xmlChar *xml_str)
{
  gchar *res = NULL;

  if (xml_str != NULL)
    {
      res = g_strdup ((char *)xml_str);
      xmlFree(xml_str);
    }

  return res;
}

static void
gstyle_palette_error_cb (void                    *arg,
                         const char              *msg,
                         xmlParserSeverities      severity,
                         xmlTextReaderLocatorPtr  locator)
{
  g_warning ("Parse error at line %i:\n%s\n",
             xmlTextReaderLocatorLineNumber (locator),
             msg);
}

static GParamSpec *properties [N_PROPS];

static gboolean
gstyle_palette_xml_get_header (xmlTextReaderPtr   reader,
                               gchar            **id,
                               gchar            **name)
{
  g_assert (reader != NULL);
  g_assert (id != NULL);
  g_assert (name != NULL);

  *id = *name = NULL;
  if (xmlTextReaderRead(reader) == 1 &&
      xmlTextReaderNodeType (reader) == XML_READER_TYPE_ELEMENT &&
      !g_strcmp0 (XML_TO_CHAR (xmlTextReaderConstName (reader)), "palette") &&
      xmlTextReaderDepth (reader) == 0)
    {
      *id = strdup_and_xmlfree (xmlTextReaderGetAttribute (reader, CHAR_TO_XML ("id")));
      *name = strdup_and_xmlfree (xmlTextReaderGetAttribute (reader, CHAR_TO_XML ("_name")));
      if (gstyle_str_empty0 (*id) || gstyle_utf8_is_spaces (*id))
        {
          g_warning ("Palette '%s'has an empty or NULL id\n", *name);
          return FALSE;
        }

      if (gstyle_utf8_is_spaces (*name))
        g_clear_pointer (name, g_free);
    }

  return (*id != NULL);
}

static GstyleColor *
gstyle_palette_xml_get_color (xmlTextReaderPtr reader)
{
  GstyleColor *color = NULL;
  g_autofree gchar *name;
  g_autofree gchar *value;

  g_assert (reader != NULL);

  if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_ELEMENT &&
      !g_strcmp0 (XML_TO_CHAR (xmlTextReaderConstName (reader)), "color") &&
      xmlTextReaderDepth (reader) == 1)
    {
      name = strdup_and_xmlfree (xmlTextReaderGetAttribute (reader, CHAR_TO_XML ("name")));
      if (gstyle_utf8_is_spaces (name))
        g_clear_pointer (&name, g_free);

      value = strdup_and_xmlfree (xmlTextReaderGetAttribute (reader, CHAR_TO_XML ("value")));
      if (!gstyle_str_empty0 (value))
        color = gstyle_color_new_from_string (name, value);
    }

  return color;
}

/**
 * gstyle_palette_get_colors:
 * @self: a #GstylePalette
 *
 *  Return an array of colors contained in the palette.
 *
 * Returns: (transfer none) (element-type GstyleColor): a #GPtrArray of #GstyleColor.
 */
GPtrArray *
gstyle_palette_get_colors (GstylePalette  *self)
{
  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), NULL);

  return self->colors;
}

/**
 * gstyle_palette_get_color_at_index:
 * @self: a #GstylePalette
 * @index: position of the #GstyleColor to get
 *
 *  Return the #gstyleColor at @index position
 * or %NULL if the index is out of bounds.
 *
 * Returns: (transfer none) (nullable): a #GstyleColor.
 */
const GstyleColor *
gstyle_palette_get_color_at_index (GstylePalette  *self,
                                   guint           index)
{
  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), NULL);
  g_return_val_if_fail (index < self->colors->len, NULL);

  return g_ptr_array_index (self->colors, index);
}

/* TODO: add an unnamed category ? */
static gboolean
add_color_to_names_sets (GstylePalette *self,
                         GstyleColor   *color)
{
  const gchar *name;
  GPtrArray *set;

  g_assert (GSTYLE_IS_PALETTE (self));
  g_assert (GSTYLE_IS_COLOR (color));

  name = gstyle_color_get_name (color);
  if (gstyle_str_empty0 (name))
    return FALSE;

  set = g_hash_table_lookup (self->color_names, name);
  if (set == NULL)
    {
      set = g_ptr_array_new ();
      g_hash_table_insert (self->color_names, (gpointer)name, set);
    }

  g_ptr_array_add (set, color);
  return TRUE;
}

static gboolean
remove_color_to_names_sets (GstylePalette *self,
                            GstyleColor   *color)
{
  const gchar *name;
  GPtrArray *set;
  gboolean ret = FALSE;

  g_assert (GSTYLE_IS_PALETTE (self));
  g_assert (GSTYLE_IS_COLOR (color));

  name = gstyle_color_get_name (color);
  if (gstyle_str_empty0 (name))
    return FALSE;

  set = g_hash_table_lookup (self->color_names, name);
  if (set == NULL)
    return FALSE;

  ret = g_ptr_array_remove (set, color);
  if (set->len == 0)
    {
      g_ptr_array_unref (set);
      g_hash_table_remove (self->color_names, name);
    }

  return ret;
}

/**
 * gstyle_palette_add_at_index:
 * @self: a #GstylePalette
 * @color: A #GstyleColor
 * @position: Position to insert the new color, from 0 to gstyle_palette_get_len() -1,
 *   or -1 to append it
 * @error: (nullable): a #GError location or %NULL
 *
 * Add a #GstyleColor to the palette.
 *
 * Returns: %TRUE on succes, %FALSE otherwise.
 */
gboolean
gstyle_palette_add_at_index (GstylePalette  *self,
                             GstyleColor    *color,
                             gint            position,
                             GError        **error)
{
  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), FALSE);
  g_return_val_if_fail (GSTYLE_IS_COLOR (color), FALSE);

  /* If we are just after the last position, we in fact do an append */
  if (position == self->colors->len)
    position = -1;

  if (position == -1 ||
      (position == 0 && self->colors->len == 0) ||
      (0 <= position && position < self->colors->len))
    {
      g_object_ref (color);
      g_ptr_array_insert (self->colors, position, color);
      add_color_to_names_sets (self, color);

      position = (position == -1) ? self->colors->len - 1 : position;
      g_list_model_items_changed (G_LIST_MODEL (self), position, 0, 1);

      return TRUE;
    }
  else
    {
      g_warning ("Color inserted in palette '%s' at out-of-bounds position %i in (0, %i)\n",
                 gstyle_palette_get_name (self),
                 position, self->colors->len - 1);

      return FALSE;
     }
}

/**
 * gstyle_palette_add:
 * @self: a #GstylePalette
 * @color: A #GstyleColor
 * @error: (nullable): a #GError location or %NULL
 *
 * Add a #GstyleColor to the palette.
 *
 * Returns: %TRUE on succes, %FALSE otherwise.
 */
gboolean
gstyle_palette_add (GstylePalette  *self,
                    GstyleColor    *color,
                    GError        **error)
{
  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), FALSE);
  g_return_val_if_fail (GSTYLE_IS_COLOR (color), FALSE);

  return gstyle_palette_add_at_index (self, color, -1, error);
}

/**
 * gstyle_palette_remove_at_index:
 * @self: a #GstylePalette
 * @position: A position to remove the #GstyleColor from
 *
 * Try to remove the #GstyleColor at @position from the palette
 *
 * Returns: %TRUE on succes, %FALSE otherwise.
 */
gboolean
gstyle_palette_remove_at_index (GstylePalette  *self,
                                gint            position)
{
  GstyleColor *color;

  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), FALSE);

  if (0 <= position && position < self->colors->len)
    {
      color = GSTYLE_COLOR (g_ptr_array_index (self->colors, position));
      remove_color_to_names_sets (self, color);
      g_ptr_array_remove_index (self->colors, position);
      g_list_model_items_changed (G_LIST_MODEL (self), position, 1, 0);

      return TRUE;
    }
  else
    {
      g_warning ("Trying to remove a Color in palette '%s' at out-of-bounds position %i in (0, %i)\n",
                 gstyle_palette_get_name (self),
                 position, self->colors->len - 1);

      return FALSE;
     }
}

/**
 * gstyle_palette_remove:
 * @self: a #GstylePalette
 * @color: A #GstyleColor
 *
 * Try to remove a #GstyleColor from the palette.
 *
 * Returns: %TRUE on succes, %FALSE otherwise.
 */
gboolean
gstyle_palette_remove (GstylePalette  *self,
                       GstyleColor    *color)
{
  GPtrArray *array;

  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), FALSE);
  g_return_val_if_fail (GSTYLE_IS_COLOR (self), FALSE);

  array = self->colors;
  for (gint i = 0; i < array->len; ++i)
    {
      if (array->pdata[i] == color)
        {
          remove_color_to_names_sets (self, color);
          g_ptr_array_remove_index (array, i);
          g_list_model_items_changed (G_LIST_MODEL (self), i, 1, 0);

          return TRUE;
        }
    }

  return FALSE;
}

/**
 * gstyle_palette_lookup:
 * @self: a #GstylePalette
 * @name: A #GstyleColor name
 *
 * Search for one or several #GstyleColor named @name in the palette.
 *
 * Returns: (transfer none) (nullable) (element-type GstyleColor): a #GstyleColor pointer array.
 */
GPtrArray *
gstyle_palette_lookup (GstylePalette  *self,
                       const gchar    *name)
{
  GPtrArray *set;

  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), NULL);
  g_return_val_if_fail (!gstyle_str_empty0 (name), NULL);

  set = g_hash_table_lookup (self->color_names, name);

  return set;
}

/**
 * gstyle_palette_get_index:
 * @self: a #GstylePalette
 * @color: A #GstyleColor
 *
 * Search for a #GstyleColor in the palette and
 * return its index or -1 if not found.
 *
 * Returns: An index in the palette or -1.
 */
gint
gstyle_palette_get_index (GstylePalette  *self,
                          GstyleColor    *color)
{
  guint len;

  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), -1);
  g_return_val_if_fail (GSTYLE_COLOR (color), -1);

  len = self->colors->len;
  for (gint i = 0; i < len; i++)
    if (color == g_ptr_array_index (self->colors, i))
      return i;

  return -1;
}

/* We skip commented lines and not prefixed by prefix if not NULL */
static gchar *
read_gpl_line (GDataInputStream  *stream,
               GError           **error,
               const gchar       *prefix)
{
  gchar *line;

  g_assert (G_IS_INPUT_STREAM (stream));

  while ((line = g_data_input_stream_read_line_utf8 (stream, NULL, NULL, error)))
    {
      g_strchug (line);
      if (*line == '#' || (prefix != NULL && !g_str_has_prefix (line, prefix)))
        g_free (line);
      else
        break;
    }

  return line;
}

static gboolean
read_gpl_header (GDataInputStream   *stream,
                 gchar             **palette_name,
                 gint               *line_count,
                 GError            **error)
{
  gchar *line;

  g_assert (G_IS_INPUT_STREAM (stream));
  g_assert (palette_name != NULL);
  g_assert (line_count != NULL);

  if ((line = read_gpl_line (stream, error, "GIMP Palette")))
    {
      g_free (line);
      ++(*line_count);
      line = read_gpl_line (stream, error, "Name:");
      if (line != NULL && g_str_has_prefix (line, "Name:"))
        {
          *palette_name = g_strdup (g_strstrip (&line[5]));
          if (gstyle_utf8_is_spaces (*palette_name))
            g_clear_pointer (palette_name, g_free);

          g_free (line);

          return TRUE;
        }
    }

  g_set_error (error, GSTYLE_PALETTE_ERROR, GSTYLE_PALETTE_ERROR_PARSE,
               _("failed to parse line %i\n"), *line_count);

  return FALSE;
}

static gboolean
read_gpl_color_line (GDataInputStream  *stream,
                     GdkRGBA           *rgba,
                     gchar            **name,
                     gint              *line_count,
                     GError           **error)
{
  gchar *line;
  gchar *cursor;
  gchar *end;
  gint value;

  g_assert (G_IS_INPUT_STREAM (stream));
  g_assert (rgba != NULL);
  g_assert (name != NULL);
  g_assert (line_count != NULL);

  ++(*line_count);
  while ((line = read_gpl_line (stream, error, NULL)))
    if (*line >= '0' && *line <= '9')
      break;
    else
    {
      g_free (line);
      ++(*line_count);
    }

  if (line == NULL)
    return FALSE;

  cursor = line;

  rgba->alpha = 1.0;
  value = strtol (cursor, &end, 10);
  if (end != cursor && value >= 0 && value <= 255)
    {
      rgba->red = value / 255.0;
      cursor = end;
      value = strtol (cursor, &end, 10);
      if (end != cursor && value >= 0 && value <= 255)
        {
          rgba->green = value / 255.0;
          cursor = end;
          value = strtol (cursor, &end, 10);
          if (end != cursor && value >= 0 && value <= 255)
            {
              rgba->blue = value / 255.0;
              cursor = end;
              cursor = g_strstrip (cursor);
              if (*cursor != '\0')
                *name = g_strdup (cursor);
              else
                *name = NULL;

              g_free (line);
              return TRUE;
            }
        }
    }

  g_set_error (error, GSTYLE_PALETTE_ERROR, GSTYLE_PALETTE_ERROR_PARSE,
               _("failed to parse line %i\n"), *line_count);

  g_free (line);
  return FALSE;
}

static GstylePalette *
gstyle_palette_new_from_gpl (GFile         *file,
                             GCancellable  *cancellable,
                             GError       **error)
{
  g_autoptr(GDataInputStream) data_stream = NULL;
  g_autoptr (GInputStream) stream = NULL;
  GstylePalette *palette = NULL;
  g_autofree gchar *palette_name = NULL;
  g_autofree gchar *id = NULL;
  GstyleColor *color;
  gchar *color_name;
  GdkRGBA rgba;
  GError *tmp_error = NULL;
  gint line_count = 1;
  gboolean has_colors = FALSE;

  g_assert (G_IS_FILE (file));

  if ((stream = G_INPUT_STREAM (g_file_read (file, cancellable, &tmp_error))))
    {
      data_stream = g_data_input_stream_new (stream);
      if (read_gpl_header (data_stream, &palette_name, &line_count, &tmp_error))
        {
          id = g_strcanon (g_strdup (palette_name), GSTYLE_PALETTE_ID_CHARSET, '_');
          palette = g_object_new (GSTYLE_TYPE_PALETTE,
                                 "id", id,
                                 "name", palette_name,
                                 "file", file,
                                 NULL);

          while (read_gpl_color_line (data_stream, &rgba, &color_name, &line_count, &tmp_error))
            {
              has_colors = TRUE;
              color = gstyle_color_new_from_rgba (color_name, GSTYLE_COLOR_KIND_RGB_HEX6, &rgba);
              gstyle_palette_add (palette, color, &tmp_error);
              g_object_unref (color);
              g_free (color_name);
              if (tmp_error != NULL)
                break;
            }
        }
    }

  if (tmp_error == NULL && !has_colors)
    {
      g_autofree gchar *uri = g_file_get_uri (file);

      g_set_error (&tmp_error, GSTYLE_PALETTE_ERROR, GSTYLE_PALETTE_ERROR_EMPTY,
                   _("%s : palette is empty\n"), uri);
    }

  if (tmp_error != NULL)
    {
      g_clear_object (&palette);
      g_propagate_error (error, tmp_error);
    }

  return palette;
}

static int
gstyle_palette_io_read_cb (void *user_data,
                           char *buffer,
                           int   len)
{
  g_assert (G_IS_INPUT_STREAM(user_data));
  g_assert (buffer != NULL);

  return g_input_stream_read ((GInputStream *)user_data, buffer, len, NULL, NULL);
}

static int
gstyle_palette_io_close_cb (void *user_data)
{
  g_assert (G_IS_INPUT_STREAM(user_data));

  return g_input_stream_close ((GInputStream *)user_data, NULL, NULL) ? 0 : -1;
}

static GstylePalette *
gstyle_palette_new_from_xml (GFile         *file,
                             GCancellable  *cancellable,
                             GError       **error)
{
  g_autoptr(GInputStream) stream = NULL;
  g_autofree gchar *uri = NULL;
  GstylePalette *palette = NULL;
  xmlTextReaderPtr reader;
  GError *tmp_error = NULL;
  gboolean has_colors = FALSE;
  gint ret = -1;

  g_assert (G_IS_FILE (file));

  uri = g_file_get_uri (file);

  if (!(stream = G_INPUT_STREAM (g_file_read (file, cancellable, &tmp_error))))
    goto finish;

  reader = xmlReaderForIO (gstyle_palette_io_read_cb,
                           gstyle_palette_io_close_cb,
                           stream,
                           uri,
                           NULL,
                           XML_PARSE_RECOVER | XML_PARSE_NOBLANKS | XML_PARSE_COMPACT);

  if (reader != NULL)
    {
      GstyleColor *color;
      g_autofree gchar *id = NULL;
      g_autofree gchar *name = NULL;

      xmlTextReaderSetErrorHandler (reader, gstyle_palette_error_cb, NULL);

      if (xmlTextReaderRead(reader) &&
          gstyle_palette_xml_get_header (reader, &id, &name))
        {
          palette = g_object_new (GSTYLE_TYPE_PALETTE,
                                  "id", id,
                                  "name", name,
                                  "file", file,
                                  NULL);

          ret = xmlTextReaderRead(reader);
          while (ret == 1)
            {
              if (xmlTextReaderNodeType (reader) == XML_READER_TYPE_END_ELEMENT)
                {
                  ret = 0;
                  break;
                }

              /* TODO: better naming */
              color = gstyle_palette_xml_get_color (reader);
              if (color == NULL)
                {
                  ret = -1;
                  break;
                }

              gstyle_palette_add (palette, color, &tmp_error);
              g_object_unref (color);
              has_colors = TRUE;

              ret = xmlTextReaderRead(reader);
            }
        }

      if (ret != 0 || !has_colors)
        {
          g_clear_object (&palette);
          g_set_error (&tmp_error, GSTYLE_PALETTE_ERROR, GSTYLE_PALETTE_ERROR_PARSE,
                       _("%s : failed to parse\n"), uri);
        }

      xmlTextReaderClose(reader);
      xmlFreeTextReader(reader);
    }
  else
    g_set_error (&tmp_error, GSTYLE_PALETTE_ERROR, GSTYLE_PALETTE_ERROR_FILE,
                 _("Unable to open %s\n"), uri);

finish:

  if (tmp_error)
    g_propagate_error (error, tmp_error);

  return palette;
}

/**
 * gstyle_palette_new_from_file:
 * @file: a #GFile
 * @cancellable: A #GCancellable
 * @error: (nullable): a #GError location or %NULL
 *
 * Load a palette from an .xml or .gpl file.
 *
 * Returns: A #GstylePalette.
 */
GstylePalette *
gstyle_palette_new_from_file (GFile         *file,
                              GCancellable  *cancellable,
                              GError       **error)
{
  GstylePalette *palette = NULL;
  g_autofree gchar *uri = NULL;
  GError *tmp_error = NULL;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  uri = g_file_get_uri (file);
  if (g_str_has_suffix (uri, "xml"))
    palette = gstyle_palette_new_from_xml (file, cancellable, &tmp_error);
  else if (g_str_has_suffix (uri, "gpl"))
    palette = gstyle_palette_new_from_gpl (file, cancellable, &tmp_error);
  else
    g_set_error (&tmp_error, GSTYLE_PALETTE_ERROR, GSTYLE_PALETTE_ERROR_FORMAT,
                 _("%s : This file format is not supported\n"), uri);

  /* TODO: check for duplicated color names */

  if (tmp_error)
    g_propagate_error (error, tmp_error);

  return palette;
}

/**
 * gstyle_palette_save_to_xml:
 * @self: a #GstylePalette
 * @file: a #GFile
 * @error: (nullable): a #GError location or %NULL
 *
 * Save a palette to Gnome-Builder .xml palette format.
 *
 * Returns: %TRUE if succesfull, %FALSE otherwise.
 */
gboolean
gstyle_palette_save_to_xml (GstylePalette  *self,
                            GFile          *file,
                            GError        **error)
{
  g_autofree gchar *file_path = NULL;
  const gchar *id;
  const gchar *name;
  xmlDocPtr doc;
  xmlNodePtr root_node;
  xmlNodePtr palette_node;
  xmlNodePtr color_node;
  gint n_colors;
  gint succes;

  gchar *header = "Copyright (C) 2016 GNOME Builder Team at irc.gimp.net/#gnome-builder\n" \
                  "This program is free software: you can redistribute it and/or modify\n" \
                  "it under the terms of the GNU General Public License as published by\n" \
                  "the Free Software Foundation, either version 3 of the License, or\n"    \
                  "(at your option) any later version.\n\n"                                \
                  "This program is distributed in the hope that it will be useful,\n"      \
                  "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"       \
                  "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"        \
                  "GNU General Public License for more details.\n\n"                       \
                  "You should have received a copy of the GNU General Public License\n"    \
                  "along with this program.  If not, see <http://www.gnu.org/licenses/>\n";

  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), FALSE);
  g_return_val_if_fail (G_IS_FILE (file), FALSE);

  doc = xmlNewDoc(CHAR_TO_XML ("1.0"));
  root_node = xmlNewDocComment (doc, CHAR_TO_XML (header));
  xmlDocSetRootElement(doc, root_node);
  palette_node = xmlNewNode (NULL, CHAR_TO_XML ("palette"));
  xmlAddSibling (root_node, palette_node);

  id = gstyle_palette_get_id (self);
  name = gstyle_palette_get_name (self);
  xmlNewProp (palette_node, CHAR_TO_XML ("id"), CHAR_TO_XML (id));
  xmlNewProp (palette_node, CHAR_TO_XML ("_name"), CHAR_TO_XML (name));

  n_colors = gstyle_palette_get_len (self);
  for (gint i = 0; i < n_colors; ++i)
    {
      const GstyleColor *color;
      const gchar *color_name;
      g_autofree gchar *color_string = NULL;

      color = gstyle_palette_get_color_at_index (self, i);
      color_name = gstyle_color_get_name ((GstyleColor *)color);

      if (gstyle_color_get_kind ((GstyleColor *)color) == GSTYLE_COLOR_KIND_PREDEFINED)
        color_string = gstyle_color_to_string ((GstyleColor *)color, GSTYLE_COLOR_KIND_RGB_HEX6);
      else
        color_string = gstyle_color_to_string ((GstyleColor *)color, GSTYLE_COLOR_KIND_ORIGINAL);

      color_node = xmlNewChild (palette_node, NULL, CHAR_TO_XML ("color"), NULL);
      xmlNewProp (color_node, CHAR_TO_XML ("name"), CHAR_TO_XML (color_name));
      xmlNewProp (color_node, CHAR_TO_XML ("value"), CHAR_TO_XML (color_string));
    }

  file_path = g_file_get_path (file);
  succes = xmlSaveFormatFileEnc (file_path, doc, "UTF-8", 1);
  xmlFreeDoc(doc);

  if (succes == -1)
    {
      g_set_error (error, GSTYLE_PALETTE_ERROR, GSTYLE_PALETTE_ERROR_FILE,
                   _("Unable to save %s\n"), file_path);

      return FALSE;
    }
  else
    return TRUE;
}

/**
 * gstyle_palette_set_name:
 * @self: a #GstylePalette
 * @name: palette name
 *
 */
void
gstyle_palette_set_name (GstylePalette *self,
                         const gchar   *name)
{
  g_return_if_fail (GSTYLE_IS_PALETTE (self));

  if (g_strcmp0 (self->name, name) != 0)
    {
      g_free (self->name);
      self->name = g_strdup (name);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_NAME]);
    }
}

/**
 * gstyle_palette_set_id:
 * @self: a #GstylePalette
 * @id: palette id
 *
 */
void
gstyle_palette_set_id (GstylePalette *self,
                       const gchar   *id)
{
  gint64 num_id;

  g_return_if_fail (GSTYLE_IS_PALETTE (self));

  g_free (self->id);
  if (gstyle_str_empty0 (id))
    {
      num_id = g_get_real_time ();
      self->id = g_strdup_printf ("gb-cp-%lu", num_id);
    }
  else if (g_strcmp0 (self->id, id) != 0)
    self->id = g_strdup (id);
}

/**
 * gstyle_palette_get_name:
 * @self: a #GstylePalette
 *
 * Return the name of the palette.
 *
 * Returns: The palette's name.
 */
const gchar *
gstyle_palette_get_name (GstylePalette *self)
{
  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), NULL);

  return self->name;
}

/**
 * gstyle_palette_get_id:
 * @self: a #GstylePalette
 *
 * Return the palette id.
 *
 *
 * Returns: The palette id string.
 */
const gchar *
gstyle_palette_get_id (GstylePalette *self)
{
  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), NULL);

  return self->id;
}

/**
 * gstyle_palette_get_file:
 * @self: a #GstylePalette
 *
 * Return the #GFile used to create the palette.
 *
 *
 * Returns: (transfer full): a #GFile.
 */
GFile *
gstyle_palette_get_file (GstylePalette *self)
{
  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), NULL);

  return self->file;
}

/**
 * gstyle_palette_get_len:
 * @self: a #GstylePalette
 *
 * Return The number of colors in the palette.
 *
 *
 * Returns: Palette's length.
 */
guint
gstyle_palette_get_len (GstylePalette *self)
{
  g_return_val_if_fail (GSTYLE_IS_PALETTE (self), 0);

  return self->colors->len;
}

GstylePalette *
gstyle_palette_new (void)
{
  return g_object_new (GSTYLE_TYPE_PALETTE, NULL);
}

static void
gstyle_palette_finalize (GObject *object)
{
  GstylePalette *self = GSTYLE_PALETTE (object);

  g_ptr_array_unref (self->colors);
  g_hash_table_unref (self->color_names);

  g_free (self->name);
  g_free (self->id);
  g_clear_object (&self->file);

  G_OBJECT_CLASS (gstyle_palette_parent_class)->finalize (object);
}

static void
gstyle_palette_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  GstylePalette *self = GSTYLE_PALETTE (object);

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_string (value, self->id);
      break;

    case PROP_NAME:
      g_value_set_string (value, self->name);
      break;

    case PROP_FILE:
      g_value_set_object (value, self->file);
      break;

    case PROP_COLORS:
      g_value_set_object (value, self->colors);
      break;

    case PROP_LEN:
      g_value_set_uint (value, self->colors->len);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gstyle_palette_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  GstylePalette *self = GSTYLE_PALETTE (object);
  GFile *file;

  switch (prop_id)
    {
    case PROP_ID:
      gstyle_palette_set_id (self, g_value_get_string (value));
      break;

    case PROP_NAME:
      gstyle_palette_set_name (self, g_value_get_string (value));
      break;

    case PROP_FILE:
      file = g_value_get_object (value);
      self->file = file ? g_object_ref (file) : NULL;
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GType
gstyle_palette_list_model_get_item_type (GListModel *list)
{
  g_assert (GSTYLE_IS_PALETTE (list));

  return GSTYLE_TYPE_PALETTE;
}

static guint
gstyle_palette_list_model_get_n_items (GListModel *list)
{
  GstylePalette *self = (GstylePalette *)list;

  g_assert (GSTYLE_IS_PALETTE (self));

  return self->colors->len;
}

static gpointer
gstyle_palette_list_model_get_item (GListModel *list,
                                    guint       position)
{
  GstylePalette *self = (GstylePalette *)list;

  g_assert (GSTYLE_IS_PALETTE (self));

  if (position < self->colors->len)
    return g_object_ref (g_ptr_array_index (self->colors, position));
  else
    return NULL;
}

static void
gstyle_palette_list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = gstyle_palette_list_model_get_item_type;
  iface->get_n_items = gstyle_palette_list_model_get_n_items;
  iface->get_item = gstyle_palette_list_model_get_item;
}

static void
gstyle_palette_class_init (GstylePaletteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gstyle_palette_finalize;
  object_class->get_property = gstyle_palette_get_property;
  object_class->set_property = gstyle_palette_set_property;

  properties [PROP_ID] =
    g_param_spec_string ("id",
                         "Palette id",
                         "The id of the palette.",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  properties [PROP_NAME] =
    g_param_spec_string ("name",
                         "Palette name",
                         "The palette name.",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILE] =
    g_param_spec_object ("file",
                         "File",
                         "The uri, as a GFile, used to generate the palette.",
                         G_TYPE_FILE,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_COLORS] =
    g_param_spec_object ("colors",
                         "Colors",
                         "An array of colors contained in the palette.",
                         G_TYPE_FILE,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_LEN] =
    g_param_spec_uint ("len",
                       "Palette length",
                       "Palette length",
                       0,
                       G_MAXUINT,
                       0,
                       (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
gstyle_palette_init (GstylePalette *self)
{
  self->colors = g_ptr_array_new_with_free_func (g_object_unref);
  self->color_names = g_hash_table_new_full (g_str_hash,
                                             g_str_equal,
                                             NULL,
                                             (GDestroyNotify)g_ptr_array_unref);
}
