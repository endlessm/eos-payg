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
#include <libeos-payg/efi.h>

static gboolean payg_legacy_mode = FALSE;

/* Force a poweroff in situations where we are not able to enforce PAYG. This
 * is intended to be used as a GSourceFunc, e.g. with g_timeout_add_seconds()
 *
 * If user_data != NULL, it must be a pointer to the source ID to be cleared.
 */
gboolean
payg_system_poweroff (gpointer user_data)
{
  int ret;

  if (user_data != NULL)
    {
      guint *source_id = (guint *) user_data;
      *source_id = 0;
    }

  g_message ("Requesting an orderly system shutdown");
  ret = system ("systemctl poweroff");
  g_debug ("systemctl returned %d", ret);

  /* We have requested an orderly system shutdown. If that request failed or if
   * a shutdown was already in progress 'systemctl' will return a failure
   * status the same way, so we can't easily distinguish between these two
   * scenarios. Let's ignore systemctl exit status and let the daemon
   * terminate. If an orderly system shutdown does not happen after we exit the
   * watchdog will take care of powering off the machine eventually.
   */
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
  g_autofree unsigned char *debug_efivar = NULL;
  int data_size;
  EpgDebugFlags debug_flags = 0;

  if (!eospayg_efi_var_exists ("debug"))
    return debug_flags;

  /* For now we only look at the first byte but let's allow it to be bigger so
   * it can be extended in the future.
   */
  debug_efivar = eospayg_efi_var_read ("debug", &data_size);
  if (!debug_efivar)
    {
      g_warning ("Failed to read EOSPAYG_debug");
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
  gboolean secboot;

  if (debug_flags & EPG_DEBUG_SECURE_BOOT_OFF &&
      debug_flags & EPG_DEBUG_SECURE_BOOT_ON)
    g_warning ("Both EPG_DEBUG_SECURE_BOOT_OFF and EPG_DEBUG_SECURE_BOOT_ON are set");

  if (debug_flags & EPG_DEBUG_SECURE_BOOT_OFF)
    return FALSE;

  if (debug_flags & EPG_DEBUG_SECURE_BOOT_ON)
    return TRUE;

  secboot = eospayg_efi_secureboot_active ();
  if (!secboot)
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
    g_warning ("Both EPG_DEBUG_EOSPAYG_ACTIVE_OFF and EPG_DEBUG_EOSPAYG_ACTIVE_ON are set");

  if (debug_flags & EPG_DEBUG_EOSPAYG_ACTIVE_OFF)
    return FALSE;

  if (debug_flags & EPG_DEBUG_EOSPAYG_ACTIVE_ON)
    return TRUE;

  return eospayg_efi_var_exists ("active");
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

/**
 * payg_get_legacy_mode:
 *
 * If eospaygd is running in Phase 2 mode, returns %TRUE, otherwise returns
 * %FALSE
 */
gboolean payg_get_legacy_mode (void)
{
  return payg_legacy_mode;
}

/**
 * payg_internal_set_legacy_mode:
 *
 * To be called by eospaygd to initialize the value returned by
 * payg_get_legacy_mode. Should be called once, only on Phase 2
 * systems (where eos-paygd is run from the primary filesystem
 * not the initramfs).
 */
void payg_internal_set_legacy_mode (void)
{
  payg_legacy_mode = TRUE;
}
