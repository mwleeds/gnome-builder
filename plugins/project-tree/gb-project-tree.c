/* gb-project-tree.c
 *
 * Copyright (C) 2015 Christian Hergert <christian@hergert.me>
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

#define G_LOG_DOMAIN "project-tree"

#include <glib/gi18n.h>
#include <ide.h>

#include "gb-project-file.h"
#include "gb-project-tree.h"
#include "gb-project-tree-actions.h"
#include "gb-project-tree-builder.h"
#include "gb-project-tree-private.h"

G_DEFINE_TYPE (GbProjectTree, gb_project_tree, IDE_TYPE_TREE)

enum {
  PROP_0,
  PROP_SHOW_IGNORED_FILES,
  LAST_PROP
};

static GParamSpec *properties [LAST_PROP];

GtkWidget *
gb_project_tree_new (void)
{
  return g_object_new (GB_TYPE_PROJECT_TREE, NULL);
}

IdeContext *
gb_project_tree_get_context (GbProjectTree *self)
{
  IdeTreeNode *root;
  GObject *item;

  g_return_val_if_fail (GB_IS_PROJECT_TREE (self), NULL);

  if ((root = ide_tree_get_root (IDE_TREE (self))) &&
      (item = ide_tree_node_get_item (root)) &&
      IDE_IS_CONTEXT (item))
    return IDE_CONTEXT (item);

  return NULL;
}

static void
gb_project_tree_project_file_renamed (GbProjectTree *self,
                                      GFile         *src_file,
                                      GFile         *dst_file,
                                      IdeProject    *project)
{
  IDE_ENTRY;

  g_assert (GB_IS_PROJECT_TREE (self));
  g_assert (G_IS_FILE (src_file));
  g_assert (G_IS_FILE (dst_file));
  g_assert (IDE_IS_PROJECT (project));

  ide_tree_rebuild (IDE_TREE (self));
  gb_project_tree_reveal (self, dst_file);

  IDE_EXIT;
}

static gboolean
compare_to_file (gconstpointer a,
                 gconstpointer b)
{
  GFile *file = (GFile *)a;
  GObject *item = (GObject *)b;

  /*
   * Our key (the GFile) is always @a.
   * The potential match (maybe a GbProjectFile) is @b.
   * @b may also be NULL.
   */

  g_assert (G_IS_FILE (file));
  g_assert (!item || G_IS_OBJECT (item));

  if (GB_IS_PROJECT_FILE (item))
    return g_file_equal (file, gb_project_file_get_file (GB_PROJECT_FILE (item)));

  return FALSE;
}

static void
gb_project_tree_project_file_trashed (GbProjectTree *self,
                                      GFile         *file,
                                      IdeProject    *project)
{
  IdeTreeNode *node;

  IDE_ENTRY;

  g_assert (GB_IS_PROJECT_TREE (self));
  g_assert (G_IS_FILE (file));
  g_assert (IDE_IS_PROJECT (project));

  node = ide_tree_find_custom (IDE_TREE (self), compare_to_file, file);

  if (node != NULL)
    {
      IdeTreeNode *parent = ide_tree_node_get_parent (node);

      ide_tree_node_invalidate (parent);
      ide_tree_node_expand (parent, TRUE);
      ide_tree_node_select (parent);
    }

  IDE_EXIT;
}

static void
gb_project_tree_vcs_changed (GbProjectTree *self,
                             IdeVcs        *vcs)
{
  g_autoptr(GFile) file = NULL;
  IdeTreeNode *node;
  GObject *item;

  g_assert (GB_IS_PROJECT_TREE (self));
  g_assert (IDE_IS_VCS (vcs));

  if (NULL != (node = ide_tree_get_selected (IDE_TREE (self))) &&
      NULL != (item = ide_tree_node_get_item (node)) &&
      GB_IS_PROJECT_FILE (item))
    {
      if (NULL != (file = gb_project_file_get_file (GB_PROJECT_FILE (item))))
        g_object_ref (file);
    }


  ide_tree_rebuild (IDE_TREE (self));

  if (file != NULL)
    gb_project_tree_reveal (self, file);
}

void
gb_project_tree_set_context (GbProjectTree *self,
                             IdeContext    *context)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  IdeTreeNode *root;
  IdeProject *project;
  IdeVcs *vcs;

  g_return_if_fail (GB_IS_PROJECT_TREE (self));
  g_return_if_fail (IDE_IS_CONTEXT (context));

  vcs = ide_context_get_vcs (context);

  g_signal_connect_object (vcs,
                           "changed",
                           G_CALLBACK (gb_project_tree_vcs_changed),
                           self,
                           G_CONNECT_SWAPPED);

  project = ide_context_get_project (context);

  g_signal_connect_object (project,
                           "file-renamed",
                           G_CALLBACK (gb_project_tree_project_file_renamed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (project,
                           "file-trashed",
                           G_CALLBACK (gb_project_tree_project_file_trashed),
                           self,
                           G_CONNECT_SWAPPED);

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (self));

  root = ide_tree_node_new ();
  ide_tree_node_set_item (root, G_OBJECT (context));
  ide_tree_set_root (IDE_TREE (self), root);

  /*
   * If we only have one toplevel item (underneath root), expand it.
   */
  if ((gtk_tree_model_iter_n_children (model, NULL) == 1) &&
      gtk_tree_model_get_iter_first (model, &iter))
    {
      g_autoptr(IdeTreeNode) node = NULL;

      gtk_tree_model_get (model, &iter, 0, &node, -1);
      if (node != NULL)
        ide_tree_node_expand (node, FALSE);
    }
}


static void
gb_project_tree_notify_selection (GbProjectTree *self)
{
  g_assert (GB_IS_PROJECT_TREE (self));

  gb_project_tree_actions_update (self);
}

static void
gb_project_tree_finalize (GObject *object)
{
  GbProjectTree *self = (GbProjectTree *)object;

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (gb_project_tree_parent_class)->finalize (object);
}

static void
gb_project_tree_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  GbProjectTree *self = GB_PROJECT_TREE(object);

  switch (prop_id)
    {
    case PROP_SHOW_IGNORED_FILES:
      g_value_set_boolean (value, gb_project_tree_get_show_ignored_files (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gb_project_tree_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  GbProjectTree *self = GB_PROJECT_TREE(object);

  switch (prop_id)
    {
    case PROP_SHOW_IGNORED_FILES:
      gb_project_tree_set_show_ignored_files (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gb_project_tree_class_init (GbProjectTreeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gb_project_tree_finalize;
  object_class->get_property = gb_project_tree_get_property;
  object_class->set_property = gb_project_tree_set_property;

  properties [PROP_SHOW_IGNORED_FILES] =
    g_param_spec_boolean ("show-ignored-files",
                          "Show Ignored Files",
                          "If files ignored by the VCS should be displayed.",
                         FALSE,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, LAST_PROP, properties);
}

static void
gb_project_tree_init (GbProjectTree *self)
{
  GtkStyleContext *style_context;
  IdeTreeBuilder *builder;
  GMenu *menu;

  style_context = gtk_widget_get_style_context (GTK_WIDGET (self));
  gtk_style_context_add_class (style_context, "project-tree");

  self->settings = g_settings_new ("org.gnome.builder.project-tree");

  g_settings_bind (self->settings, "show-icons",
                   self, "show-icons",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "show-ignored-files",
                   self, "show-ignored-files",
                   G_SETTINGS_BIND_DEFAULT);

  builder = gb_project_tree_builder_new ();
  ide_tree_add_builder (IDE_TREE (self), builder);

  g_signal_connect (self,
                    "notify::selection",
                    G_CALLBACK (gb_project_tree_notify_selection),
                    NULL);

  gb_project_tree_actions_init (self);

  menu = ide_application_get_menu_by_id (IDE_APPLICATION_DEFAULT, "gb-project-tree-popup-menu");
  ide_tree_set_context_menu (IDE_TREE (self), G_MENU_MODEL (menu));
}

gboolean
gb_project_tree_get_show_ignored_files (GbProjectTree *self)
{
  g_return_val_if_fail (GB_IS_PROJECT_TREE (self), FALSE);

  return self->show_ignored_files;
}

void
gb_project_tree_set_show_ignored_files (GbProjectTree *self,
                                        gboolean       show_ignored_files)
{
  g_return_if_fail (GB_IS_PROJECT_TREE (self));

  show_ignored_files = !!show_ignored_files;

  if (show_ignored_files != self->show_ignored_files)
    {
      self->show_ignored_files = show_ignored_files;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SHOW_IGNORED_FILES]);
      ide_tree_rebuild (IDE_TREE (self));
    }
}

static gboolean
find_child_node (IdeTree     *tree,
                 IdeTreeNode *node,
                 IdeTreeNode *child,
                 gpointer    user_data)
{
  const gchar *name = user_data;
  GObject *item;

  g_assert (IDE_IS_TREE (tree));
  g_assert (IDE_IS_TREE_NODE (node));
  g_assert (IDE_IS_TREE_NODE (child));

  item = ide_tree_node_get_item (child);

  if (GB_IS_PROJECT_FILE (item))
    {
      const gchar *item_name;

      item_name = gb_project_file_get_display_name (GB_PROJECT_FILE (item));

      return ide_str_equal0 (item_name, name);
    }

  return FALSE;
}

static gboolean
find_files_node (IdeTree     *tree,
                 IdeTreeNode *node,
                 IdeTreeNode *child,
                 gpointer    user_data)
{
  GObject *item;

  g_assert (IDE_IS_TREE (tree));
  g_assert (IDE_IS_TREE_NODE (node));
  g_assert (IDE_IS_TREE_NODE (child));

  item = ide_tree_node_get_item (child);

  return GB_IS_PROJECT_FILE (item);
}

void
gb_project_tree_reveal (GbProjectTree *self,
                        GFile         *file)
{
  g_autofree gchar *relpath = NULL;
  g_auto(GStrv) parts = NULL;
  IdeContext *context;
  IdeTreeNode *node;
  IdeVcs *vcs;
  GFile *workdir;
  guint i;

  g_return_if_fail (GB_IS_PROJECT_TREE (self));
  g_return_if_fail (G_IS_FILE (file));

  context = gb_project_tree_get_context (self);
  g_assert (IDE_IS_CONTEXT (context));

  if (context == NULL)
    return;

  vcs = ide_context_get_vcs (context);
  workdir = ide_vcs_get_working_directory (vcs);
  relpath = g_file_get_relative_path (workdir, file);

  if (relpath == NULL)
    return;

  node = ide_tree_find_child_node (IDE_TREE (self), NULL, find_files_node, NULL);
  if (node == NULL)
    return;

  parts = g_strsplit (relpath, G_DIR_SEPARATOR_S, 0);

  for (i = 0; parts [i]; i++)
    {
      node = ide_tree_find_child_node (IDE_TREE (self), node, find_child_node, parts [i]);
      if (node == NULL)
        return;
    }

  ide_tree_expand_to_node (IDE_TREE (self), node);
  ide_tree_scroll_to_node (IDE_TREE (self), node);
  ide_tree_node_select (node);
}
