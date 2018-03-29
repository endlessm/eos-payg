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
                                    guint64       now,
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

G_END_DECLS
