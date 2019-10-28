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

#include "config.h"

#include <libeos-payg/util.h>
#include <glib.h>
#include <gio/gio.h>
#include <linux/reboot.h>
#include <sys/reboot.h>
#include <efivar.h>

#define EFI_GLOBAL_VARIABLE_GUID EFI_GUID(0x8be4df61, 0x93ca, 0x11d2, 0xaa0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c)

/* Force a poweroff in situations where we are not able to enforce PAYG. This
 * is intended to be used as a GSourceFunc, e.g. with g_timeout_add_seconds()
 */
gboolean
payg_sync_and_poweroff (gpointer user_data)
{
  g_warning ("bcsn: Forcing poweroff now!");
  sync ();
  reboot (LINUX_REBOOT_CMD_POWER_OFF);
  return G_SOURCE_REMOVE;
}

/**
 * EpgDebugFlags:
 *
 * Flags set by the EFI variable EOSPAYG_debug which affect the behavior of
 * eos-paygd. For an explanation of each, see
 * https://phabricator.endlessm.com/w/software/pay-as-you-go/
 *
 * If adding to this enum, update _read_eospayg_debug() and if necessary extend
 * it to work with more than one byte.
 */
typedef enum {
  EPG_DEBUG_SET_G_MESSAGES_DEBUG_ALL = 1 << 0,
  EPG_DEBUG_SECURE_BOOT_OFF = 1 << 1,
  EPG_DEBUG_SECURE_BOOT_ON = 1 << 2,
  EPG_DEBUG_EOSPAYG_ACTIVE_OFF = 1 << 3,
  EPG_DEBUG_EOSPAYG_ACTIVE_ON = 1 << 4,
  EPG_DEBUG_DONT_USE_WATCHDOG = 1 << 5,
  EPG_DEBUG_DONT_USE_LSM = 1 << 6,
  EPG_DEBUG_SKIP_SECURITYLEVEL_CHECK = 1 << 7,
} EpgDebugFlags;

static EpgDebugFlags
_read_eospayg_debug (void)
{
  uint8_t *debug_efivar;
  size_t data_size;
  uint32_t attributes;
  int ret;
  EpgDebugFlags debug_flags = 0;

  if (efi_get_variable_exists (EOSPAYG_GUID, "EOSPAYG_debug") < 0)
    return debug_flags;

  /* For now we only look at the first byte but let's allow it to be bigger so
   * it can be extended in the future.
   */
  ret = efi_get_variable (EOSPAYG_GUID, "EOSPAYG_debug", &debug_efivar, &data_size, &attributes);
  if (ret < 0 || !debug_efivar || data_size < 1)
    {
      g_printerr ("%s: Failed to read EOSPAYG_debug\n", G_STRFUNC);
      return debug_flags;
    }

  g_debug ("%s: EOSPAYG_debug is set to %u", G_STRFUNC, debug_efivar[0]);

  /* The C standard doesn't guarantee the underlying type of an enum, so let's
   * convert manually to be safe.
   */
  if (debug_efivar[0] & EPG_DEBUG_SET_G_MESSAGES_DEBUG_ALL)
    debug_flags |= EPG_DEBUG_SET_G_MESSAGES_DEBUG_ALL;
  if (debug_efivar[0] & EPG_DEBUG_SECURE_BOOT_OFF)
    debug_flags |= EPG_DEBUG_SECURE_BOOT_OFF;
  if (debug_efivar[0] & EPG_DEBUG_SECURE_BOOT_ON)
    debug_flags |= EPG_DEBUG_SECURE_BOOT_ON;
  if (debug_efivar[0] & EPG_DEBUG_EOSPAYG_ACTIVE_OFF)
    debug_flags |= EPG_DEBUG_EOSPAYG_ACTIVE_OFF;
  if (debug_efivar[0] & EPG_DEBUG_EOSPAYG_ACTIVE_ON)
    debug_flags |= EPG_DEBUG_EOSPAYG_ACTIVE_ON;
  if (debug_efivar[0] & EPG_DEBUG_DONT_USE_WATCHDOG)
    debug_flags |= EPG_DEBUG_DONT_USE_WATCHDOG;
  if (debug_efivar[0] & EPG_DEBUG_DONT_USE_LSM)
    debug_flags |= EPG_DEBUG_DONT_USE_LSM;
  if (debug_efivar[0] & EPG_DEBUG_SKIP_SECURITYLEVEL_CHECK)
    debug_flags |= EPG_DEBUG_SKIP_SECURITYLEVEL_CHECK;

  return debug_flags;
}

/**
 * payg_set_debug_env_vars:
 *
 * If the EOSPAYG_debug EFI variable has the bit set for
 * EPG_DEBUG_SET_G_MESSAGES_DEBUG_ALL, this function sets the environment
 * variable "G_MESSAGES_DEBUG" to the value "all" (unless G_MESSAGES_DEBUG is
 * already set).
 *
 * This may be extended in the future to include other environment variables.
 */
void
payg_set_debug_env_vars (void)
{
  EpgDebugFlags debug_flags = _read_eospayg_debug ();

  if (debug_flags & EPG_DEBUG_SET_G_MESSAGES_DEBUG_ALL)
    g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);
}

/**
 * payg_get_secure_boot_enabled:
 *
 * If the EOSPAYG_debug EFI variable has the bit set for
 * EPG_DEBUG_SECURE_BOOT_OFF or EPG_DEBUG_SECURE_BOOT_ON, this will return
 * %FALSE or %TRUE respectively, regardless of whether secure boot is actually
 * enabled. Otherwise, the SecureBoot EFI variable is checked to determine if
 * the current boot is secure.
 *
 * Returns: %TRUE if eos-paygd should act as if Secure Boot is enabled, and
 *   %FALSE otherwise
 */
gboolean
payg_get_secure_boot_enabled (void)
{
  EpgDebugFlags debug_flags = _read_eospayg_debug ();
  uint8_t *secboot = NULL;
  size_t data_size = 0;
  uint32_t attributes;
  int ret;

  if (debug_flags & EPG_DEBUG_SECURE_BOOT_OFF &&
      debug_flags & EPG_DEBUG_SECURE_BOOT_ON)
    g_printerr ("%s: Both EPG_DEBUG_SECURE_BOOT_OFF and EPG_DEBUG_SECURE_BOOT_ON are set\n", G_STRFUNC);

  if (debug_flags & EPG_DEBUG_SECURE_BOOT_OFF)
    return FALSE;

  if (debug_flags & EPG_DEBUG_SECURE_BOOT_ON)
    return TRUE;

  ret = efi_get_variable (EFI_GLOBAL_VARIABLE_GUID, "SecureBoot", &secboot, &data_size, &attributes);
  if (ret < 0 || !secboot || data_size != 1)
    {
      g_debug ("Failed to read SecureBoot EFI variable, treating as SB off");
      return FALSE;
    }
  if (*secboot == 0)
    {
      g_debug ("SecureBoot EFI variable indicates the current boot is not secure");
      return FALSE;
    }

  return TRUE;
}

/**
 * payg_get_eospayg_active_set:
 *
 * If the EOSPAYG_debug EFI variable has the bit set for
 * EPG_DEBUG_EOSPAYG_ACTIVE_OFF or EPG_DEBUG_EOSPAYG_ACTIVE_ON, this will
 * return %FALSE or %TRUE respectively, regardless of whether the EFI variable
 * EOSPAYG_active is actually set. Otherwise, return whether EOSPAYG_active
 * is set.
 *
 * Returns: %TRUE if eos-paygd should act as if EOSPAYG_active is set, and
 *   %FALSE otherwise
 */
gboolean
payg_get_eospayg_active_set (void)
{
  EpgDebugFlags debug_flags = _read_eospayg_debug ();

  if (debug_flags & EPG_DEBUG_EOSPAYG_ACTIVE_OFF &&
      debug_flags & EPG_DEBUG_EOSPAYG_ACTIVE_ON)
    g_printerr ("%s: Both EPG_DEBUG_EOSPAYG_ACTIVE_OFF and EPG_DEBUG_EOSPAYG_ACTIVE_ON are set\n", G_STRFUNC);

  if (debug_flags & EPG_DEBUG_EOSPAYG_ACTIVE_OFF)
    return FALSE;

  if (debug_flags & EPG_DEBUG_EOSPAYG_ACTIVE_ON)
    return TRUE;

  return efi_get_variable_exists (EOSPAYG_GUID, "EOSPAYG_active") == 0;
}

/**
 * payg_should_use_watchdog:
 *
 * If the EOSPAYG_debug EFI variable has the bit set for
 * EPG_DEBUG_DONT_USE_WATCHDOG, returns %FALSE. Otherwise, returns %TRUE.
 *
 * Returns: %TRUE if eos-paygd should use the watchdog "endlessdog", and %FALSE
 *   otherwise
 */
gboolean
payg_should_use_watchdog (void)
{
  EpgDebugFlags debug_flags = _read_eospayg_debug ();

  if (debug_flags & EPG_DEBUG_DONT_USE_WATCHDOG)
    return FALSE;

  return TRUE;
}

/**
 * payg_should_use_lsm:
 *
 * If the EOSPAYG_debug EFI variable has the bit set for
 * EPG_DEBUG_DONT_USE_LSM, returns %FALSE. Otherwise, returns %TRUE.
 *
 * Returns: %TRUE if eos-paygd should use the custom LSM, and %FALSE
 *   otherwise
 */
gboolean
payg_should_use_lsm (void)
{
  EpgDebugFlags debug_flags = _read_eospayg_debug ();

  if (debug_flags & EPG_DEBUG_DONT_USE_LSM)
    return FALSE;

  return TRUE;
}

/**
 * payg_should_check_securitylevel:
 *
 * If the EOSPAYG_debug EFI variable has the bit set for
 * EPG_DEBUG_SKIP_SECURITYLEVEL_CHECK, returns %FALSE. Otherwise, returns
 * %TRUE.
 *
 * Returns: %TRUE if eos-paygd should check EOSPAYG_securitylevel against its
 *   compiled security level, and %FALSE if the check should be skipped
 */
gboolean
payg_should_check_securitylevel (void)
{
  EpgDebugFlags debug_flags = _read_eospayg_debug ();

  if (debug_flags & EPG_DEBUG_SKIP_SECURITYLEVEL_CHECK)
    return FALSE;

  return TRUE;
}
