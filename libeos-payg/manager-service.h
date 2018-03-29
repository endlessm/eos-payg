/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2018 Endless Mobile, Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License,
 * v. 2.0. If a copy of the MPL was not distributed with this file, You can
 * obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU Lesser General Public License Version 2.1 or later (the "LGPL"), in
 * which case the provisions of the LGPL are applicable instead of those above.
 * If you wish to allow use of your version of this file only under the terms
 * of the LGPL, and not to allow others to use your version of this file under
 * the terms of the MPL, indicate your decision by deleting the provisions
 * above and replace them with the notice and other provisions required by the
 * LGPL. If you do not delete the provisions above, a recipient may use your
 * version of this file under the terms of either the MPL or the LGPL.
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
