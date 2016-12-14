/* ide-editor-spell-navigator.c
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

 * This code is a modification of:
 * https://git.gnome.org/browse/gspell/tree/gspell/gspell-navigator-text-view.c
 */

#include <glib/gi18n.h>
#include "sourceview/ide-source-view.h"

#include "ide-editor-spell-navigator.h"
#include "ide-editor-spell-utils.h"

struct _IdeEditorSpellNavigator
{
  GObject        parent_instance;

  GtkTextView   *view;
  GtkTextBuffer *buffer;
  GHashTable    *words_count;
  GtkTextMark   *start_boundary;
  GtkTextMark   *end_boundary;
  GtkTextMark   *word_start;
  GtkTextMark   *word_end;
};

static void gspell_navigator_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_EXTENDED (IdeEditorSpellNavigator, ide_editor_spell_navigator, G_TYPE_INITIALLY_UNOWNED, 0,
                        G_IMPLEMENT_INTERFACE (GSPELL_TYPE_NAVIGATOR, gspell_navigator_iface_init))

enum {
  PROP_0,
  PROP_VIEW,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/* TODO: do it async */

/* Always process start and end by init_boudaries before */
static GHashTable *
ide_editor_spell_navigator_count_words (IdeEditorSpellNavigator *self,
                                        GtkTextIter             *start,
                                        GtkTextIter             *end)
{
  GHashTable *table;
  GtkTextTag *no_spell_check_tag;
  GtkTextIter word_start;
  GtkTextIter word_end;
  guint count;
  gchar *word;

  g_assert (IDE_IS_EDITOR_SPELL_NAVIGATOR (self));
  g_assert (start != NULL);
  g_assert (end != NULL);

  word_start = word_end = *start;
  no_spell_check_tag = ide_editor_spell_utils_get_no_spell_check_tag (self->buffer);

  table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  while (TRUE)
    {
      if (!ide_editor_spell_utils_text_iter_starts_word (&word_start))
        {
          GtkTextIter iter;

          iter = word_start;
          ide_editor_spell_utils_text_iter_forward_word_end (&word_start);
          if (gtk_text_iter_equal (&iter, &word_start))
            break;

          ide_editor_spell_utils_text_iter_backward_word_start (&word_start);
        }

      if (!ide_editor_spell_utils_skip_no_spell_check (no_spell_check_tag, &word_start, end))
        break;

      word_end = word_start;
      ide_editor_spell_utils_text_iter_forward_word_end (&word_end);
      if (gtk_text_iter_compare (&word_end, end) >= 0)
        break;

      word = gtk_text_buffer_get_text (self->buffer, &word_start, &word_end, FALSE);
      if ((count = GPOINTER_TO_UINT (g_hash_table_lookup (table, word))))
        count++;
      else
        count = 1;

      g_hash_table_insert (table, word, GUINT_TO_POINTER (count));

      word_start = word_end;
    }

  return table;
}

guint
ide_editor_spell_navigator_get_count (IdeEditorSpellNavigator *self,
                                      const gchar             *word)
{
  g_assert (IDE_IS_EDITOR_SPELL_NAVIGATOR (self));

  if (ide_str_empty0 (word))
    return 0;
  else
    return GPOINTER_TO_UINT (g_hash_table_lookup (self->words_count, word));
}

GspellNavigator *
ide_editor_spell_navigator_new (GtkTextView *view)
{
  return g_object_new (IDE_TYPE_EDITOR_SPELL_NAVIGATOR,
                       "view", view,
                       NULL);
}

static void
delete_mark (GtkTextBuffer *buffer,
             GtkTextMark   *mark)
{
  if (mark != NULL)
    gtk_text_buffer_delete_mark (buffer, mark);
}

static void
ide_editor_spell_navigator_dispose (GObject *object)
{
  IdeEditorSpellNavigator *self = (IdeEditorSpellNavigator *)object;

  g_clear_object (&self->view);
  g_hash_table_unref (self->words_count);

  if (self->buffer != NULL)
    {
      delete_mark (self->buffer, self->start_boundary);
      delete_mark (self->buffer, self->end_boundary);
      delete_mark (self->buffer, self->word_start);
      delete_mark (self->buffer, self->word_end);

      g_object_unref (self->buffer);
      self->buffer = NULL;
    }

  G_OBJECT_CLASS (ide_editor_spell_navigator_parent_class)->dispose (object);
}

/* static void */
/* ide_editor_spell_navigator_finalize (GObject *object) */
/* { */
/*   IdeEditorSpellNavigator *self = (IdeEditorSpellNavigator *)object; */

/*   G_OBJECT_CLASS (ide_editor_spell_navigator_parent_class)->finalize (object); */
/* } */

static void
init_boundaries (IdeEditorSpellNavigator *self)
{
  GtkTextIter start;
  GtkTextIter end;

  g_assert (IDE_IS_EDITOR_SPELL_NAVIGATOR (self));
  g_assert (self->start_boundary == NULL);
  g_assert (self->end_boundary == NULL);

  if (!gtk_text_buffer_get_selection_bounds (self->buffer, &start, &end))
    gtk_text_buffer_get_bounds (self->buffer, &start, &end);

  if (ide_editor_spell_utils_text_iter_inside_word (&start) &&
      !ide_editor_spell_utils_text_iter_starts_word (&start))
    ide_editor_spell_utils_text_iter_backward_word_start (&start);

  if (ide_editor_spell_utils_text_iter_inside_word (&end))
    ide_editor_spell_utils_text_iter_forward_word_end (&end);

  self->start_boundary = gtk_text_buffer_create_mark (self->buffer, NULL, &start, TRUE);
  self->end_boundary = gtk_text_buffer_create_mark (self->buffer, NULL, &end, FALSE);
}

static void
set_view (IdeEditorSpellNavigator *self,
          GtkTextView             *view)
{
  GtkTextIter start;
  GtkTextIter end;

  g_assert (IDE_IS_EDITOR_SPELL_NAVIGATOR (self));
  g_assert (self->view == NULL);
  g_assert (self->buffer == NULL);

  self->view = g_object_ref (view);
  self->buffer = g_object_ref (gtk_text_view_get_buffer (view));

  init_boundaries (self);

  gtk_text_buffer_get_iter_at_mark (self->buffer, &start, self->start_boundary);
  gtk_text_buffer_get_iter_at_mark (self->buffer, &end, self->end_boundary);
  self->words_count = ide_editor_spell_navigator_count_words (self, &start, &end);

  g_object_notify (G_OBJECT (self), "view");
}

static void
ide_editor_spell_navigator_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  IdeEditorSpellNavigator *self = IDE_EDITOR_SPELL_NAVIGATOR (object);

  switch (prop_id)
    {
    case PROP_VIEW:
      set_view (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_editor_spell_navigator_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  IdeEditorSpellNavigator *self = IDE_EDITOR_SPELL_NAVIGATOR (object);

  switch (prop_id)
    {
    case PROP_VIEW:
      g_value_set_object (value, self->view);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_editor_spell_navigator_class_init (IdeEditorSpellNavigatorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ide_editor_spell_navigator_dispose;
  object_class->get_property = ide_editor_spell_navigator_get_property;
  object_class->set_property = ide_editor_spell_navigator_set_property;

  properties [PROP_VIEW] =
    g_param_spec_object ("view",
                        "View",
                        "the view",
                        GTK_TYPE_TEXT_VIEW,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
ide_editor_spell_navigator_init (IdeEditorSpellNavigator *self)
{
}

static void
select_misspelled_word (IdeEditorSpellNavigator *self)
{
  GtkTextIter word_start;
  GtkTextIter word_end;

  g_assert (IDE_IS_EDITOR_SPELL_NAVIGATOR (self));

  gtk_text_buffer_get_iter_at_mark (self->buffer, &word_start, self->word_start);
  gtk_text_buffer_get_iter_at_mark (self->buffer, &word_end, self->word_end);

  gtk_text_buffer_select_range (self->buffer, &word_start, &word_end);

  g_return_if_fail (gtk_text_view_get_buffer (self->view) == self->buffer);

  gtk_text_view_scroll_to_mark (self->view,
                                gtk_text_buffer_get_insert (self->buffer),
                                0.25,
                                FALSE,
                                0.0,
                                0.0);
}

static gboolean
ide_editor_spell_navigator_goto_next (GspellNavigator  *navigator,
                                      gchar           **word_p,
                                      GspellChecker   **spell_checker_p,
                                      GError          **error_p)
{
  IdeEditorSpellNavigator *self = (IdeEditorSpellNavigator *)navigator;
  GspellTextBuffer *gspell_buffer;
  GspellChecker *spell_checker;
  GtkTextIter word_start;
  GtkTextIter end;
  GtkTextTag *no_spell_check_tag;

  g_assert (IDE_IS_EDITOR_SPELL_NAVIGATOR (self));
  g_assert ((self->word_start == NULL && self->word_end == NULL) ||
            (self->word_start != NULL && self->word_end != NULL));

  gspell_buffer = gspell_text_buffer_get_from_gtk_text_buffer (self->buffer);
  spell_checker = gspell_text_buffer_get_spell_checker (gspell_buffer);

  if (spell_checker == NULL)
    return FALSE;

  if (gspell_checker_get_language (spell_checker) == NULL)
    {
      if (spell_checker_p != NULL)
        *spell_checker_p = g_object_ref (spell_checker);

      g_set_error (error_p,
                   GSPELL_CHECKER_ERROR,
                   GSPELL_CHECKER_ERROR_NO_LANGUAGE_SET,
                   "%s",
                   _("Spell checker error: no language set. "
                   "It’s maybe because no dictionaries are installed."));

      return FALSE;
    }

  gtk_text_buffer_get_iter_at_mark (self->buffer, &end, self->end_boundary);

  if (self->word_start == NULL)
    {
      GtkTextIter start;

      gtk_text_buffer_get_iter_at_mark (self->buffer, &start, self->start_boundary);

      self->word_start = gtk_text_buffer_create_mark (self->buffer, NULL, &start, TRUE);
      self->word_end = gtk_text_buffer_create_mark (self->buffer, NULL, &start, FALSE);

      word_start = start;
    }
  else
    {
      GtkTextIter word_end;

      gtk_text_buffer_get_iter_at_mark (self->buffer, &word_end, self->word_end);

      if (gtk_text_iter_compare (&end, &word_end) <= 0)
        return FALSE;

      word_start = word_end;
    }

  no_spell_check_tag = ide_editor_spell_utils_get_no_spell_check_tag (self->buffer);

  while (TRUE)
    {
      GtkTextIter word_end;
      g_autofree gchar *word = NULL;
      gboolean correctly_spelled;
      GError *error = NULL;

      if (!ide_editor_spell_utils_text_iter_starts_word (&word_start))
        {
          GtkTextIter iter;

          iter = word_start;
          ide_editor_spell_utils_text_iter_forward_word_end (&word_start);

          if (gtk_text_iter_equal (&iter, &word_start))
            return FALSE;

          ide_editor_spell_utils_text_iter_backward_word_start (&word_start);
        }

        if (!ide_editor_spell_utils_skip_no_spell_check (no_spell_check_tag, &word_start, &end))
          return FALSE;

        g_return_val_if_fail (ide_editor_spell_utils_text_iter_starts_word (&word_start), FALSE);

        word_end = word_start;
        ide_editor_spell_utils_text_iter_forward_word_end (&word_end);

        if (gtk_text_iter_compare (&end, &word_end) < 0)
          return FALSE;

        word = gtk_text_buffer_get_text (self->buffer, &word_start, &word_end, FALSE);
        correctly_spelled = gspell_checker_check_word (spell_checker, word, -1, &error);

        if (error != NULL)
          {
            g_propagate_error (error_p, error);
            return FALSE;
          }

        if (!correctly_spelled)
          {
            /* Found! */
            gtk_text_buffer_move_mark (self->buffer, self->word_start, &word_start);
            gtk_text_buffer_move_mark (self->buffer, self->word_end, &word_end);
            select_misspelled_word (self);

            if (spell_checker_p != NULL)
              *spell_checker_p = g_object_ref (spell_checker);

            if (word_p != NULL)
              *word_p = g_steal_pointer (&word);

            return TRUE;
         }

        word_start = word_end;
    }

  return FALSE;
}

static void
ide_editor_spell_navigator_change (GspellNavigator *navigator,
                                   const gchar     *word,
                                   const gchar     *change_to)
{
  IdeEditorSpellNavigator *self = (IdeEditorSpellNavigator *)navigator;
  GtkTextIter word_start;
  GtkTextIter word_end;
  g_autofree gchar *word_in_buffer = NULL;

  g_assert (IDE_IS_EDITOR_SPELL_NAVIGATOR (self));
  g_assert (GTK_IS_TEXT_MARK (self->word_start));
  g_assert (GTK_IS_TEXT_MARK (self->word_end));

  gtk_text_buffer_get_iter_at_mark (self->buffer, &word_start, self->word_start);
  gtk_text_buffer_get_iter_at_mark (self->buffer, &word_end, self->word_end);

  word_in_buffer = gtk_text_buffer_get_slice (self->buffer, &word_start, &word_end, TRUE);
  g_return_if_fail (word_in_buffer != NULL);
  g_return_if_fail (g_strcmp0 (word_in_buffer, word) == 0);

  gtk_text_buffer_begin_user_action (self->buffer);

  gtk_text_buffer_delete (self->buffer, &word_start, &word_end);
  gtk_text_buffer_insert (self->buffer, &word_start, change_to, -1);

  gtk_text_buffer_end_user_action (self->buffer);
}

static void
ide_editor_spell_navigator_change_all (GspellNavigator *navigator,
                                       const gchar     *word,
                                       const gchar     *change_to)
{
  IdeEditorSpellNavigator *self = (IdeEditorSpellNavigator *)navigator;
  GtkTextIter iter;

  g_assert (IDE_IS_EDITOR_SPELL_NAVIGATOR (self));
  g_assert (GTK_IS_TEXT_MARK (self->start_boundary));
  g_assert (GTK_IS_TEXT_MARK (self->end_boundary));

  gtk_text_buffer_get_iter_at_mark (self->buffer, &iter, self->start_boundary);
  gtk_text_buffer_begin_user_action (self->buffer);

  while (TRUE)
  {
    gboolean found;
    GtkTextIter match_start;
    GtkTextIter match_end;
    GtkTextIter limit;

    gtk_text_buffer_get_iter_at_mark (self->buffer, &limit, self->end_boundary);
    found = gtk_text_iter_forward_search (&iter,
                                          word,
                                          GTK_TEXT_SEARCH_VISIBLE_ONLY |
                                          GTK_TEXT_SEARCH_TEXT_ONLY,
                                          &match_start,
                                          &match_end,
                                          &limit);

    if (!found)
      break;

    if (ide_editor_spell_utils_text_iter_starts_word (&match_start) &&
        ide_editor_spell_utils_text_iter_ends_word (&match_end))
      {
        gtk_text_buffer_delete (self->buffer, &match_start, &match_end);
        gtk_text_buffer_insert (self->buffer, &match_end, change_to, -1);
      }

    iter = match_end;
  }

  gtk_text_buffer_end_user_action (self->buffer);
}

static void
gspell_navigator_iface_init (gpointer g_iface,
                             gpointer iface_data)
{
  GspellNavigatorInterface *iface = g_iface;

  iface->goto_next = ide_editor_spell_navigator_goto_next;
  iface->change = ide_editor_spell_navigator_change;
  iface->change_all = ide_editor_spell_navigator_change_all;
}
