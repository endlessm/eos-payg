/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 * Copyright © 2016 Kinvolk GmbH
 * Copyright © 2017, 2018 Endless Mobile, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *  - Vivek Dasmohapatra <vivek@etla.org>
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 *  - Philip Withnall <withnall@endlessm.com>
 */

#pragma once

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define HLP_TYPE_CONFIG_FILE hlp_config_file_get_type ()
G_DECLARE_FINAL_TYPE (HlpConfigFile, hlp_config_file, HLP, CONFIG_FILE, GObject)

HlpConfigFile *hlp_config_file_new (const gchar * const *key_file_paths,
                                    GResource           *default_resource,
                                    const gchar         *default_path);

guint hlp_config_file_get_uint (HlpConfigFile  *self,
                                const gchar    *group_name,
                                const gchar    *key_name,
                                guint           min_value,
                                guint           max_value,
                                GError        **error);
gboolean hlp_config_file_get_boolean (HlpConfigFile  *self,
                                      const gchar    *group_name,
                                      const gchar    *key_name,
                                      GError        **error);
gchar *hlp_config_file_get_string (HlpConfigFile  *self,
                                   const gchar    *group_name,
                                   const gchar    *key_name,
                                   GError        **error);
gchar **hlp_config_file_get_strv (HlpConfigFile  *self,
                                  const gchar    *group_name,
                                  const gchar    *key_name,
                                  gsize          *n_elements_out,
                                  GError        **error);

gchar **hlp_config_file_get_groups (HlpConfigFile  *self,
                                    gsize          *n_groups_out,
                                    GError        **error);

G_END_DECLS
