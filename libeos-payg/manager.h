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
#include <libeos-payg/provider.h>
#include <libeos-payg/clock.h>

G_BEGIN_DECLS

#define EPG_TYPE_MANAGER epg_manager_get_type ()
G_DECLARE_FINAL_TYPE (EpgManager, epg_manager, EPG, MANAGER, GObject)

void         epg_manager_new        (gboolean             enabled,
                                     GFile               *key_file,
                                     GFile               *account_id_file,
                                     GFile               *state_directory,
                                     EpgClock            *clock,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data);
EpgProvider *epg_manager_new_finish (GAsyncResult  *result,
                                     GError       **error);

GFile      *epg_manager_get_key_file        (EpgManager *self);
GFile      *epg_manager_get_state_directory (EpgManager *self);

G_END_DECLS
