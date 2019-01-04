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

#include <libeos-payg/clock.h>

G_BEGIN_DECLS

#define EPG_TYPE_PROVIDER epg_provider_get_type ()
G_DECLARE_INTERFACE (EpgProvider, epg_provider, EPG, PROVIDER, GAsyncInitable)

struct _EpgProviderInterface
{
  GTypeInterface parent_iface;

  gboolean        (*add_code)   (EpgProvider  *self,
                                 const gchar  *code_str,
                                 GError      **error);
  gboolean        (*clear_code) (EpgProvider  *self,
                                 GError      **error);

  void            (*shutdown_async)  (EpgProvider         *self,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data);
  gboolean        (*shutdown_finish) (EpgProvider   *self,
                                      GAsyncResult  *result,
                                      GError       **error);

  guint64         (*get_expiry_time)         (EpgProvider *self);
  gboolean        (*get_enabled)             (EpgProvider *self);
  guint64         (*get_rate_limit_end_time) (EpgProvider *self);
  EpgClock       *(*get_clock)               (EpgProvider *self);

  const gchar *     code_format;
};

gboolean        epg_provider_add_code   (EpgProvider  *self,
                                         const gchar  *code_str,
                                         GError      **error);
gboolean        epg_provider_clear_code (EpgProvider  *self,
                                         GError      **error);

void            epg_provider_shutdown_async  (EpgProvider         *self,
                                              GCancellable        *cancellable,
                                              GAsyncReadyCallback  callback,
                                              gpointer             user_data);
gboolean        epg_provider_shutdown_finish (EpgProvider   *self,
                                              GAsyncResult  *result,
                                              GError       **error);

guint64         epg_provider_get_expiry_time         (EpgProvider *self);
gboolean        epg_provider_get_enabled             (EpgProvider *self);
guint64         epg_provider_get_rate_limit_end_time (EpgProvider *self);
const gchar *   epg_provider_get_code_format         (EpgProvider *self);
EpgClock *      epg_provider_get_clock               (EpgProvider *self);

G_END_DECLS
