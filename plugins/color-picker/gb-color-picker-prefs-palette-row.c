/* gb-color-picker-prefs-palette-row.c
 *
 * Copyright (C) 2016 Sebastien Lafargue <slafargue@gnome.org>
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

#include <gdk/gdk.h>

#include <ide.h>

#include "gb-color-picker-prefs-palette-row.h"

struct _GbColorPickerPrefsPaletteRow
{
  IdePreferencesBin  parent_instance;

  GtkEntry          *palette_name;
  GtkButton         *button;
  GtkImage          *image;
  GtkWidget         *event_box;
  gchar             *palette_id;
  gchar             *backup_name;

  gulong             handler;

  gchar             *key;
  GVariant          *target;
  GSettings         *settings;

  guint              updating : 1;
  guint              is_editing : 1;
};

G_DEFINE_TYPE (GbColorPickerPrefsPaletteRow, gb_color_picker_prefs_palette_row, IDE_TYPE_PREFERENCES_BIN)

enum {
  PROP_0,
  PROP_KEY,
  PROP_IS_EDITING,
  PROP_TARGET,
  PROP_PALETTE_NAME,
  N_PROPS
};

enum {
  ACTIVATED,
  CLOSED,
  EDIT,
  NAME_CHANGED,
  LAST_SIGNAL
};

static GParamSpec *properties [N_PROPS];
static guint signals [LAST_SIGNAL];

static void
gb_color_picker_prefs_palette_row_changed (GbColorPickerPrefsPaletteRow *self,
                                           const gchar                  *key,
                                           GSettings                    *settings)
{
  g_autoptr (GVariant) value = NULL;
  gboolean active;

  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_ROW (self));
  g_assert (key != NULL);
  g_assert (G_IS_SETTINGS (settings));

  if (self->target == NULL)
    {
      gtk_widget_set_visible (GTK_WIDGET (self->image), FALSE);
      return;
    }

  if (self->updating == TRUE)
    return;

  value = g_settings_get_value (settings, key);
  if (g_variant_is_of_type (value, g_variant_get_type (self->target)))
    {
      active = (g_variant_equal (value, self->target));
      gtk_widget_set_visible (GTK_WIDGET (self->image), active);
    }
  else
    g_warning ("Value and target must be of the same type");
}

static void
gb_color_picker_prefs_palette_row_activate (GbColorPickerPrefsPaletteRow *self)
{
  g_autoptr (GVariant) value = NULL;

  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_ROW (self));
  g_assert (self->target != NULL);

  if (!gtk_widget_get_sensitive (GTK_WIDGET (self)) || self->settings == NULL || self->updating)
    return;

  value = g_settings_get_value (self->settings, self->key);
  if (g_variant_is_of_type (value, g_variant_get_type (self->target)))
    {
      if (!g_variant_equal (value, self->target))
        {
          self->updating = TRUE;
          g_settings_set_value (self->settings, self->key, self->target);
          gtk_widget_set_visible (GTK_WIDGET (self->image), TRUE);
          self->updating = FALSE;
        }
    }
  else
    g_warning ("Value and target must be of the same type");
}

static void
gb_color_picker_prefs_palette_row_edit (GbColorPickerPrefsPaletteRow *self)
{
  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_ROW (self));

  gtk_entry_grab_focus_without_selecting (self->palette_name);
  gtk_editable_set_position (GTK_EDITABLE (self->palette_name), -1);
}

static void
gb_color_picker_prefs_palette_row_set_edit (GbColorPickerPrefsPaletteRow *self,
                                            gboolean                      is_editing)
{
  GtkWidget *parent;

  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_ROW (self));

  if (is_editing)
    g_signal_emit_by_name (self, "edit");
  else
    {
      parent = gtk_widget_get_parent (GTK_WIDGET (self));
      gtk_widget_grab_focus (parent);
    }

  self->is_editing = is_editing;
}

static void
gb_color_picker_prefs_palette_row_connect (IdePreferencesBin *bin,
                                           GSettings         *settings)
{
  GbColorPickerPrefsPaletteRow *self = (GbColorPickerPrefsPaletteRow *)bin;
  g_autofree gchar *signal_detail = NULL;

  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_ROW (self));
  g_assert (G_IS_SETTINGS (settings));

  signal_detail = g_strdup_printf ("changed::%s", self->key);
  self->settings = g_object_ref (settings);
  self->handler =
    g_signal_connect_object (settings,
                             signal_detail,
                             G_CALLBACK (gb_color_picker_prefs_palette_row_changed),
                             self,
                             G_CONNECT_SWAPPED);

  gb_color_picker_prefs_palette_row_changed (self, self->key, settings);
}

static void
gb_color_picker_prefs_palette_row_disconnect (IdePreferencesBin *bin,
                                              GSettings         *settings)
{
  GbColorPickerPrefsPaletteRow *self = (GbColorPickerPrefsPaletteRow *)bin;

  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_ROW (self));
  g_assert (G_IS_SETTINGS (settings));

  g_signal_handler_disconnect (settings, self->handler);
  self->handler = 0;
  g_clear_object (&self->settings);
}

static void
gb_color_picker_prefs_list_row_button_clicked_cb (GbColorPickerPrefsPaletteRow *self,
                                                  GtkButton                    *button)
{
  const gchar *id;

  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_ROW (self));
  g_assert (GTK_IS_BUTTON (button));

  id = g_variant_get_string (self->target, NULL);
  g_signal_emit_by_name (self, "closed", id);
}

static gboolean
palette_name_activate_cb (GbColorPickerPrefsPaletteRow *self,
                          GtkEntry                     *palette_name)
{
  GtkWidget *parent;
  const gchar *id;

  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_ROW (self));
  g_assert (GTK_IS_ENTRY (palette_name));

  g_clear_pointer (&self->backup_name, g_free);
  self->backup_name = g_strdup (gtk_entry_get_text (self->palette_name));
  id = g_variant_get_string (self->target, NULL);
  g_signal_emit_by_name (self, "name-changed",
                         id,
                         self->backup_name );

  parent = gtk_widget_get_parent (GTK_WIDGET (self));
  g_assert (GTK_IS_LIST_BOX_ROW (parent));
  gtk_widget_grab_focus (GTK_WIDGET (parent));

  return GDK_EVENT_STOP;
}

static gboolean
event_box_button_pressed_cb (GbColorPickerPrefsPaletteRow *self,
                             GdkEventButton               *event,
                             GtkEventBox                  *event_box)
{
  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_ROW (self));
  g_assert (event != NULL);
  g_assert (GTK_IS_EVENT_BOX (event_box));

  if (event->type == GDK_BUTTON_PRESS && event->button == GDK_BUTTON_SECONDARY)
    {
      g_signal_emit_by_name (self, "edit");
      return GDK_EVENT_STOP;
    }

  return GDK_EVENT_PROPAGATE;
}

static void
palette_name_has_focus_cb (GbColorPickerPrefsPaletteRow *self,
                           GParamSpec                   *pspec,
                           GtkEntry                     *palette_name)
{
  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_ROW (self));
  g_assert (pspec != NULL);
  g_assert (GTK_IS_ENTRY (palette_name));

  self->is_editing = gtk_widget_has_focus (GTK_WIDGET (self->palette_name));
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_IS_EDITING]);

  if (!self->is_editing)
    {
      if (g_strcmp0 (self->backup_name, gtk_entry_get_text (self->palette_name)) != 0)
        {
          gtk_entry_set_text (self->palette_name, self->backup_name);
          g_clear_pointer (&self->backup_name, g_free);
        }
    }
  else
    {
      g_clear_pointer (&self->backup_name, g_free);
      self->backup_name = g_strdup (gtk_entry_get_text (self->palette_name));
    }
}

static void
gb_color_picker_prefs_palette_row_set_palette_name (GbColorPickerPrefsPaletteRow *self,
                                                    const gchar                  *new_text)
{
  const gchar *text;

  g_assert (GB_IS_COLOR_PICKER_PREFS_PALETTE_ROW (self));

  if (ide_str_empty0 (new_text))
    {
      gtk_entry_set_text (self->palette_name, "No name");
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_PALETTE_NAME]);

      return;
    }

  text = gtk_entry_get_text (self->palette_name);
  if (g_strcmp0 (text, new_text) != 0)
    {
      gtk_entry_set_text (self->palette_name, new_text);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_PALETTE_NAME]);
    }
}

GbColorPickerPrefsPaletteRow *
gb_color_picker_prefs_palette_row_new (void)
{
  return g_object_new (GB_TYPE_COLOR_PICKER_PREFS_PALETTE_ROW, NULL);
}

static void
gb_color_picker_prefs_palette_row_finalize (GObject *object)
{
  GbColorPickerPrefsPaletteRow *self = (GbColorPickerPrefsPaletteRow *)object;

  if (self->settings != NULL)
    gb_color_picker_prefs_palette_row_disconnect (IDE_PREFERENCES_BIN (self), self->settings);

  g_clear_pointer (&self->key, g_free);
  g_clear_pointer (&self->target, g_variant_unref);
  g_clear_pointer (&self->palette_id, g_free);
  g_clear_pointer (&self->backup_name, g_free);

  G_OBJECT_CLASS (gb_color_picker_prefs_palette_row_parent_class)->finalize (object);
}

static void
gb_color_picker_prefs_palette_row_get_property (GObject    *object,
                                                guint       prop_id,
                                                GValue     *value,
                                                GParamSpec *pspec)
{
  GbColorPickerPrefsPaletteRow *self = GB_COLOR_PICKER_PREFS_PALETTE_ROW (object);

  switch (prop_id)
    {
    case PROP_KEY:
      g_value_set_string (value, self->key);
      break;

    case PROP_IS_EDITING:
      g_value_set_boolean (value, self->is_editing);
      break;

    case PROP_TARGET:
      g_value_set_variant (value, self->target);
      break;

    case PROP_PALETTE_NAME:
      g_value_set_string (value, gtk_entry_get_text (self->palette_name));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gb_color_picker_prefs_palette_row_set_property (GObject      *object,
                                                guint         prop_id,
                                                const GValue *value,
                                                GParamSpec   *pspec)
{
  GbColorPickerPrefsPaletteRow *self = GB_COLOR_PICKER_PREFS_PALETTE_ROW (object);

  switch (prop_id)
    {
    case PROP_KEY:
      self->key = g_value_dup_string (value);
      break;

    case PROP_IS_EDITING:
      gb_color_picker_prefs_palette_row_set_edit (self, g_value_get_boolean (value));
      break;

    case PROP_TARGET:
      self->target = g_value_dup_variant (value);
      break;

    case PROP_PALETTE_NAME:
      gb_color_picker_prefs_palette_row_set_palette_name (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gb_color_picker_prefs_palette_row_class_init (GbColorPickerPrefsPaletteRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  IdePreferencesBinClass *bin_class = IDE_PREFERENCES_BIN_CLASS (klass);

  object_class->finalize = gb_color_picker_prefs_palette_row_finalize;
  object_class->get_property = gb_color_picker_prefs_palette_row_get_property;
  object_class->set_property = gb_color_picker_prefs_palette_row_set_property;

  bin_class->connect = gb_color_picker_prefs_palette_row_connect;
  bin_class->disconnect = gb_color_picker_prefs_palette_row_disconnect;

  properties [PROP_IS_EDITING] =
    g_param_spec_boolean ("is-editing",
                          "is-editing",
                          "Whether the row is currently in edit mode or not",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TARGET] =
    g_param_spec_variant ("target",
                          "Target",
                          "Target",
                          G_VARIANT_TYPE_STRING,
                          NULL,
                          (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_KEY] =
    g_param_spec_string ("key",
                         "Key",
                         "Key",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_PALETTE_NAME] =
    g_param_spec_string ("palette-name",
                         "Palette name",
                         "Palette name",
                         NULL,
                         (G_PARAM_READWRITE |G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  signals [ACTIVATED] =
    g_signal_new_class_handler ("activated",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                G_CALLBACK (gb_color_picker_prefs_palette_row_activate),
                                NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals [CLOSED] =
    g_signal_new ("closed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_POINTER);

  signals [NAME_CHANGED] =
    g_signal_new ("name-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  2,
                  G_TYPE_POINTER,
                  G_TYPE_POINTER);

  signals [EDIT] =
    g_signal_new_class_handler ("edit",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_CALLBACK (gb_color_picker_prefs_palette_row_edit),
                                NULL, NULL, NULL, G_TYPE_NONE, 0);

  widget_class->activate_signal = signals [ACTIVATED];

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/builder/plugins/color-picker-plugin/gtk/color-picker-palette-row.ui");
  gtk_widget_class_bind_template_child (widget_class, GbColorPickerPrefsPaletteRow, image);
  gtk_widget_class_bind_template_child (widget_class, GbColorPickerPrefsPaletteRow, event_box);
  gtk_widget_class_bind_template_child (widget_class, GbColorPickerPrefsPaletteRow, palette_name);
  gtk_widget_class_bind_template_child (widget_class, GbColorPickerPrefsPaletteRow, button);
}

static void
gb_color_picker_prefs_palette_row_init (GbColorPickerPrefsPaletteRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_widget_add_events (self->event_box, GDK_KEY_PRESS_MASK);

  g_signal_connect_swapped (self->event_box, "button-press-event",
                            G_CALLBACK (event_box_button_pressed_cb),
                            self);

  g_signal_connect_swapped (self->palette_name, "activate",
                            G_CALLBACK (palette_name_activate_cb),
                            self);

  g_signal_connect_swapped (self->palette_name, "notify::has-focus",
                            G_CALLBACK (palette_name_has_focus_cb),
                            self);

  g_signal_connect_swapped (self->button, "clicked", G_CALLBACK (gb_color_picker_prefs_list_row_button_clicked_cb), self);
}
