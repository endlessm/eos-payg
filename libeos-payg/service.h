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
#include <libgsystemservice/service.h>

G_BEGIN_DECLS

/* The security level is used to ensure that a system can't be "downgraded"
 * to a version with a known security hole. Any time a release is made that
 * fixes a security issue, this must be increased. It must never decrease.
 */
#define EPG_SECURITY_LEVEL 4

/**
 * EpgServiceError:
 * @EPG_SERVICE_ERROR_NO_PROVIDER: No PAYG provider was found to be enabled,
 *                                 despite PAYG being active.
 *
 * Error codes returned by #EpgService
 */
typedef enum {
  EPG_SERVICE_ERROR_NO_PROVIDER,
  EPG_SERVICE_ERROR_LAST = EPG_SERVICE_ERROR_NO_PROVIDER, /*< skip >*/
} EpgServiceError;

#define EPG_SERVICE_ERROR (epg_service_error_quark ())
GQuark epg_service_error_quark (void);

#define EPG_TYPE_SERVICE epg_service_get_type ()
G_DECLARE_FINAL_TYPE (EpgService, epg_service, EPG, SERVICE, GssService)

EpgService *epg_service_new (void);

void        epg_service_secure_init_sync (EpgService   *self,
                                          GCancellable *cancellable);

G_END_DECLS
