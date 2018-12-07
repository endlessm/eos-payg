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

/**
 * EpgProvisioningError:
 * @EPG_PROVISIONING_ERROR_ALREADY_PROVISIONED: The device is already
 *    provisioned
 * @EPG_PROVISIONING_ERROR_NOT_FOUND: The requested PAYG provider was not found
 * @EPG_PROVISIONING_ERROR_NOT_SUPPORTED: The requested PAYG provider does not
 *    support provisioning
 *
 * Errors which can be returned during provisioning.
 *
 * Since: X.Y.Z
 */
typedef enum
{
  EPG_PROVISIONING_ERROR_ALREADY_PROVISIONED = 0,
  EPG_PROVISIONING_ERROR_NOT_FOUND,
  EPG_PROVISIONING_ERROR_NOT_SUPPORTED,
} EpgProvisioningError;
#define EPG_PROVISIONING_N_ERRORS (EPG_PROVISIONING_ERROR_NOT_SUPPORTED + 1)

GQuark epg_provisioning_error_quark (void);
#define EPG_PROVISIONING_ERROR epg_provisioning_error_quark ()

G_END_DECLS
