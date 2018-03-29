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

G_BEGIN_DECLS

/**
 * EpgManagerError:
 * @EPG_MANAGER_ERROR_INVALID_CODE: The given code was invalid, such as having
 *    an invalid signature or time period.
 * @EPG_MANAGER_ERROR_CODE_ALREADY_USED: The given code has already been used.
 * @EPG_MANAGER_ERROR_TOO_MANY_ATTEMPTS: Too many attempts to verify a code
 *    in recent history.
 * @EPG_MANAGER_ERROR_DISABLED: Pay as you go is disabled.
 *
 * Errors which can be returned by #EpgManager.
 *
 * Since: 0.1.0
 */
typedef enum
{
  EPG_MANAGER_ERROR_INVALID_CODE = 0,
  EPG_MANAGER_ERROR_CODE_ALREADY_USED,
  EPG_MANAGER_ERROR_TOO_MANY_ATTEMPTS,
  EPG_MANAGER_ERROR_DISABLED,
} EpgManagerError;
#define EPG_MANAGER_N_ERRORS (EPG_MANAGER_ERROR_DISABLED + 1)

GQuark epg_manager_error_quark (void);
#define EPG_MANAGER_ERROR epg_manager_error_quark ()

#define EPG_TYPE_MANAGER epg_manager_get_type ()
G_DECLARE_FINAL_TYPE (EpgManager, epg_manager, EPG, MANAGER, GObject)

EpgManager *epg_manager_new (gboolean  enabled,
                             GBytes   *key_bytes,
                             GFile    *state_directory);

gboolean    epg_manager_add_code   (EpgManager   *self,
                                    const gchar  *code_str,
                                    guint64       now_secs,
                                    GError      **error);
gboolean    epg_manager_clear_code (EpgManager   *self,
                                    GError      **error);

void        epg_manager_load_state_async  (EpgManager           *self,
                                           GCancellable         *cancellable,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data);
gboolean    epg_manager_load_state_finish (EpgManager           *self,
                                           GAsyncResult         *result,
                                           GError              **error);

void        epg_manager_save_state_async  (EpgManager           *self,
                                           GCancellable         *cancellable,
                                           GAsyncReadyCallback   callback,
                                           gpointer              user_data);
gboolean    epg_manager_save_state_finish (EpgManager           *self,
                                           GAsyncResult         *result,
                                           GError              **error);

guint64     epg_manager_get_expiry_time     (EpgManager *self);
gboolean    epg_manager_get_enabled         (EpgManager *self);
GBytes     *epg_manager_get_key_bytes       (EpgManager *self);
GFile      *epg_manager_get_state_directory (EpgManager *self);
guint64     epg_manager_get_rate_limit_end_time (EpgManager *self);

G_END_DECLS
