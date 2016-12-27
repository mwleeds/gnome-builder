/* ide-build-configuration-view.h
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

#ifndef IDE_BUILD_CONFIGURATION_VIEW_H
#define IDE_BUILD_CONFIGURATION_VIEW_H

#include <ide.h>

#include "egg-column-layout.h"

G_BEGIN_DECLS

#define IDE_TYPE_BUILD_CONFIGURATION_VIEW (ide_build_configuration_view_get_type())

G_DECLARE_DERIVABLE_TYPE (IdeBuildConfigurationView, ide_build_configuration_view, IDE, BUILD_CONFIGURATION_VIEW, EggColumnLayout)

struct _IdeBuildConfigurationViewClass
{
  EggColumnLayoutClass parent;

  void   (*connect)    (IdeBuildConfigurationView *self,
                        IdeConfiguration          *configuration);

  void   (*disconnect) (IdeBuildConfigurationView *self,
                        IdeConfiguration          *configuration);

  gpointer _reserved3;
  gpointer _reserved4;
  gpointer _reserved5;
  gpointer _reserved6;
  gpointer _reserved7;
  gpointer _reserved8;
  gpointer _reserved9;
  gpointer _reserved10;
  gpointer _reserved11;
  gpointer _reserved12;
  gpointer _reserved13;
  gpointer _reserved14;
  gpointer _reserved15;
  gpointer _reserved16;
};

IdeConfiguration *ide_build_configuration_view_get_configuration (IdeBuildConfigurationView *self);
void              ide_build_configuration_view_set_configuration (IdeBuildConfigurationView *self,
                                                                  IdeConfiguration          *configuration);

G_END_DECLS

#endif /* IDE_BUILD_CONFIGURATION_VIEW_H */
