/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2019 Endless Mobile, Inc.
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

G_BEGIN_DECLS

/* The GUID for the EOSPAYG EFI variables */
#define EOSPAYG_GUID EFI_GUID(0xd89c3871, 0xae0c, 0x4fc5, 0xa409, 0xdc, 0x71, 0x7a, 0xee, 0x61, 0xe7)

gboolean payg_sync_and_poweroff (gpointer user_data);
void payg_set_debug_env_vars (void);
gboolean payg_get_secure_boot_enabled (void);
gboolean payg_get_eospayg_active_set (void);
gboolean payg_should_use_watchdog (void);
gboolean payg_should_use_lsm (void);
gboolean payg_should_check_securitylevel (void);
gboolean payg_get_legacy_mode (void);
void payg_internal_set_legacy_mode (void);

G_END_DECLS
