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

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <libgsystemservice/service.h>
#include <libeos-payg/service.h>
#include <signal.h>
#include <systemd/sd-daemon.h>
#include <linux/reboot.h>
#include <sys/reboot.h>

/* Force a poweroff in situations where we are not able to enforce PAYG */
static gboolean
sync_and_poweroff (gpointer user_data)
{
    g_warning ("bcsn: Forcing poweroff now!");
    sync ();
    reboot (LINUX_REBOOT_CMD_POWER_OFF);
    return FALSE;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EpgService) service = NULL;
  int sd_notify_ret;
  gboolean backward_compat_mode = FALSE;
  guint timeout_id;

  /* If eos-paygd is running from the initramfs, change the process name so
   * that it survives the pivot to the final root filesystem. This is an
   * (ab)use of the functionality systemd has to support storage daemons needed
   * for mounting/unmounting the root filesystem. See
   * https://phabricator.endlessm.com/T27037
   */
  if (access ("/etc/initrd-release", F_OK) >= 0)
    argv[0][0] = '@';
  else
    {
      /* To support existing deployments which use grub and can't be OTA
       * updated to Phase 4 PAYG security, we have to support running from the
       * root filesystem and in that case not use any features that are
       * unsupported on those grub systems, like state data encryption.
       * https://phabricator.endlessm.com/T27524 */
      g_debug ("eos-paygd running from root filesystem, entering backward compat mode");
      backward_compat_mode = TRUE;
    }

  /* Do some partial initialization before the root pivot. See
   * https://phabricator.endlessm.com/T27054 */
  service = epg_service_new ();
  epg_service_secure_init_sync (service, NULL);

  if (!backward_compat_mode)
    {
      /* Let systemd know it's okay to proceed to pivot to the final root */
      sd_notify_ret = sd_notify (0, "READY=1");
      if (sd_notify_ret < 0)
        g_warning ("sd_notify() failed with code %d", -sd_notify_ret);
      else if (sd_notify_ret == 0)
        g_warning ("sd_notify() failed due to unset $NOTIFY_SOCKET");

      /* Wait for the pivot to the final root so we can connect to the D-Bus
       * daemon, but timeout after 10 minutes; otherwise we could be fooled into
       * waiting forever. If the timeout were shorter it would make debugging the
       * initramfs difficult. */
      timeout_id = g_timeout_add_seconds (10 * 60, sync_and_poweroff, NULL);
      while (access ("/etc/initrd-release", F_OK) >= 0)
        g_usleep (G_USEC_PER_SEC / 5);
      g_source_remove (timeout_id);

      /* Wait up to 20 minutes for the system D-Bus daemon to be started. A
       * shorter timeout would mean risking putting systems in an infinite boot
       * loop; in the past we've had long running operations like migrations of
       * flatpaks occur during a reboot. */
      timeout_id = g_timeout_add_seconds (20 * 60, sync_and_poweroff, NULL);
      while (TRUE)
        {
          g_autoptr(GDBusConnection) bus_connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
          if (bus_connection == NULL)
            {
              g_debug ("Error connecting to system bus, will retry: %s", error->message);
              g_clear_error (&error);
              g_usleep (G_USEC_PER_SEC);
            }
          else
            break;
        }
      g_source_remove (timeout_id);
    }

  /* Set up a D-Bus service and run until we are killed. */
  gss_service_run (GSS_SERVICE (service), argc, argv, &error);

  if (error != NULL)
    {
      int code;

      if (g_error_matches (error,
                           GSS_SERVICE_ERROR, GSS_SERVICE_ERROR_SIGNALLED))
        raise (gss_service_get_exit_signal (GSS_SERVICE (service)));

      g_printerr ("%s: %s\n", argv[0], error->message);
      code = error->code;
      g_assert (code != 0);

      return code;
    }

  return 0;
}
