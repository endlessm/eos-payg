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
#include <glib/gstdio.h>
#include <libgsystemservice/service.h>
#include <libeos-payg/service.h>
#include <signal.h>
#include <systemd/sd-daemon.h>
#include <linux/reboot.h>
#include <sys/reboot.h>
#include <glob.h>
#include <fcntl.h>
#include <errno.h>

#define FATAL_SIGNAL_EXIT_CODE 254
#define WATCHDOG_FAILURE_EXIT_CODE 253

int watchdog_fd = -1;

/* Ping the watchdog periodically as long as eos-paygd is running. */
static gboolean
ping_watchdog (gpointer user_data)
{
  g_assert (watchdog_fd >= 0);

  /* Write a byte to the watchdog device. We need the loop to deal with EINTR */
  while (TRUE)
    {
      int bytes_written = write (watchdog_fd, "\0", 1);
      if (bytes_written == -1)
        {
          int saved_errno = errno;
          if (saved_errno == EINTR)
            continue;

          g_warning ("%s: Error writing to /dev/watchdog: %s", G_STRFUNC,
                     g_strerror (saved_errno));
        }
      else if (bytes_written == 0)
        g_warning ("%s: write() reported writing zero bytes", G_STRFUNC);

      break;
    }

  return G_SOURCE_CONTINUE;
}

/* Force a poweroff in situations where we are not able to enforce PAYG */
static gboolean
sync_and_poweroff (gpointer user_data)
{
  g_warning ("bcsn: Forcing poweroff now!");
  sync ();
  reboot (LINUX_REBOOT_CMD_POWER_OFF);
  return FALSE;
}

static void
allow_writing_to_boot_partition (gboolean allow_write)
{
  glob_t globbuf = { 0 };
  int ret = glob ("/dev/mmcblk*boot0", 0, NULL, &globbuf);
  switch (ret)
    {
      case 0: /* success */
        g_debug ("%s: glob matched", G_STRFUNC);
        for (gsize i = globbuf.gl_offs; i < globbuf.gl_pathc; i++)
          {
            g_autofree char *mmcblkx = g_path_get_basename (globbuf.gl_pathv[i]);
            g_autofree char *force_ro_path = g_build_filename ("/sys/block", mmcblkx, "force_ro", NULL);
            uint32_t flags = O_SYNC | O_CLOEXEC | O_WRONLY;
            int fd = open (force_ro_path, flags);
            if (fd == -1)
              g_warning ("%s: Error opening %s: %m", G_STRFUNC, force_ro_path);
            else
              {
                g_autoptr(GError) error = NULL;

                while (TRUE)
                  {
                    int bytes_written = write (fd, allow_write ? "0" : "1", 1);
                    if (bytes_written == -1)
                      {
                        int saved_errno = errno;
                        if (saved_errno == EINTR)
                          continue;

                        g_warning ("%s: Error writing to %s: %s", G_STRFUNC,
                                   force_ro_path, g_strerror (saved_errno));
                      }
                    else if (bytes_written == 0)
                      g_warning ("%s: write() reported writing zero bytes", G_STRFUNC);

                    break;
                  }

                if (!g_close (fd, &error))
                  g_warning ("%s: failed to close file descriptor: %s", G_STRFUNC, error->message);
              }
          }
        break;

      case GLOB_NOMATCH: /* benign failure */
        g_debug ("%s: no matches for glob", G_STRFUNC);
        break;

      default: /* actual, unexpected failure */
        g_warning ("%s: glob() failed with code %i", G_STRFUNC, ret);
        break;
    }
  globfree (&globbuf);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EpgService) service = NULL;
  g_autoptr(GFile) state_dir = NULL;
  int ret, sd_notify_ret, system_ret;
  gboolean backward_compat_mode = FALSE;
  guint timeout_id, watchdog_id;

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

  /* Allow writes to /dev/mmcblk?boot0. This requires writing to a special file
   * as documented in
   * https://github.com/torvalds/linux/blob/master/Documentation/driver-api/mmc/mmc-dev-parts.rst
   */
  allow_writing_to_boot_partition (TRUE);

  /* Do some partial initialization before the root pivot. See
   * https://phabricator.endlessm.com/T27054 */
  service = epg_service_new ();
  epg_service_secure_init_sync (service, NULL);

  if (!backward_compat_mode)
    {
      /* Open and start pinging the custom watchdog timer ("endlessdog") which
       * will ask for a shut down after not being pinged for 30 seconds, and
       * force a shutdown after 60 seconds. This means that if eos-paygd is
       * somehow killed or crashes, PAYG will not go unenforced. Ping it every
       * 10 seconds so we have a margin of error in case another high priority
       * task is happening on the main loop. And use O_CLOEXEC in case it's
       * somehow possible to execve() this process after the root pivot. We
       * ping the watchdog even if PAYG is not active (e.g. it's not yet
       * provisioned or has been paid off) to prevent any other process from
       * accidentally or maliciously using the watchdog timer.
       */
      watchdog_fd = open ("/dev/watchdog", O_WRONLY | O_CLOEXEC);
      if (watchdog_fd == -1)
        {
          g_warning ("eos-paygd could not open /dev/watchdog: %m");
          return WATCHDOG_FAILURE_EXIT_CODE; /* Early return */
        }
      watchdog_id = g_timeout_add_seconds_full (G_PRIORITY_HIGH, 10, ping_watchdog, NULL, NULL);
      g_assert (watchdog_id > 0);

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

  /* Technically this existence check is racy but no other process should be
   * accessing this path
   */
  state_dir = g_file_new_for_path ("/var/lib/eos-payg");
  if (g_file_query_exists (state_dir, NULL))
    {
      /* Change the ownership of the state directory recursively (systems
       * provisioned before 3.7.0 have it owned by eos-paygd). This isn't strictly
       * necessary since root can write to it either way, but we don't want some
       * other user owning it if the UID is recycled.
       */
      system_ret = system ("/bin/chown -R root:root /var/lib/eos-payg");
      if (system_ret == -1 || !WIFEXITED (system_ret) || WEXITSTATUS (system_ret) != 0)
        g_warning ("chown of /var/lib/eos-payg failed");
    }

  /* Set up a D-Bus service and run until we are killed. */
  gss_service_run (GSS_SERVICE (service), argc, argv, &error);

  if (error != NULL)
    {
      if (g_error_matches (error, EPG_SERVICE_ERROR, EPG_SERVICE_ERROR_NO_PROVIDER))
        {
          /* This could mean the PAYG data has been erased; force a poweroff
           * after 10 minutes. See https://phabricator.endlessm.com/T27581
           */
          g_printerr ("%s: %s\n", argv[0], error->message);
          timeout_id = g_timeout_add_seconds (10 * 60, sync_and_poweroff, NULL);
          while (TRUE)
            g_main_context_iteration (NULL, TRUE);
        }
      else if (g_error_matches (error, GSS_SERVICE_ERROR, GSS_SERVICE_ERROR_SIGNALLED))
        {
          raise (gss_service_get_exit_signal (GSS_SERVICE (service)));
          ret = FATAL_SIGNAL_EXIT_CODE; /* should not be reached, just in case the signal is caught */
        }
      else
        {
          g_printerr ("%s: %s\n", argv[0], error->message);
          ret = error->code;
          g_assert (ret != 0);
        }
    }
  else
    ret = 0;

  allow_writing_to_boot_partition (FALSE);
  return ret;
}
