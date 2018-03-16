/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2018 Endless Mobile, Inc.
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
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <libeos-payg/manager.h>

G_BEGIN_DECLS

#define EPG_TYPE_MANAGER_SERVICE epg_manager_service_get_type ()
G_DECLARE_FINAL_TYPE (EpgManagerService, epg_manager_service, EPG,
                      MANAGER_SERVICE, GObject)

EpgManagerService *epg_manager_service_new (GDBusConnection *connection,
                                            const gchar     *object_path,
                                            EpgManager      *manager);

gboolean epg_manager_service_register   (EpgManagerService  *self,
                                         GError            **error);
void     epg_manager_service_unregister (EpgManagerService  *self);

G_END_DECLS
