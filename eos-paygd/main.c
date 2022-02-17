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
#include <libeos-payg/util.h>
#include <signal.h>
#include <systemd/sd-daemon.h>
#include <glob.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <libeos-payg/efi.h>

#define TIMEOUT_POWEROFF_ON_ERROR_MINUTES 20

static int watchdog_fd = -1;

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

static int
payg_relative_sd_notify (const gchar *path,
                         const char *state)
{
  struct sockaddr_un sockaddr = {
    .sun_family = AF_UNIX,
  };
  struct iovec iovec = {
    .iov_base = (char *)state,
    .iov_len = strlen(state),
  };
  struct msghdr msghdr = {
    .msg_iov = &iovec,
    .msg_iovlen = 1,
    .msg_name = &sockaddr,
  };
  int fd;
  int l, r = 1, salen;

  unsetenv("NOTIFY_SOCKET");

  l = strlen(path);
  memcpy(&sockaddr.sun_path, path, l + 1);
  salen = offsetof(struct sockaddr_un, sun_path) + l + 1;

  fd = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -errno;

  msghdr.msg_namelen = salen;
  if (sendmsg(fd, &msghdr, MSG_NOSIGNAL) < 0)
    r = -errno;

  close(fd);

  return r;
}

/* Check that our securitylevel never goes backwards. The daemon's
 * compiled in security level must always be equal to or higher than
 * what's stored in the efi variable, or it indicates a "downgrade"
 * to a version of endless with known PAYG bugs.
 */
static gboolean
test_and_update_securitylevel (void)
{
  g_autofree char *level = NULL;
  int data_size = 0;
  gboolean ret;

  level = eospayg_efi_var_read ("securitylevel", &data_size);
  if (!level || data_size != 1)
    {
      g_warning ("Failed to read security level");
      return FALSE;
    }

  /* We've detected an attempt to boot an eos from before the last
   * security level increase - we assume this is being done to
   * exploit old holes.
   */
  if (level[0] > EPG_SECURITY_LEVEL)
    {
      g_warning ("Security level violation");
      return FALSE;
    }

  /* The daemon's security level is higher than the system's, increase
   * the system level so there's no going back to an older version.
   */
  if (level[0] < EPG_SECURITY_LEVEL)
    {
      g_debug ("Security level changed this boot.");

      /* If we exceed 255 security level bumps during the lifetime of this
       * project we probably need to consider alternate career paths.
       */
      level[0] = EPG_SECURITY_LEVEL;
      ret = eospayg_efi_var_overwrite ("securitylevel", level, data_size);

      /* There's nothing a user should be able to do to cause this to fail,
       * so we'll let this "impossible" situation slide with a warning, and
       * attempt to correct it on next boot.
       */
      if (!ret)
        g_warning ("Failed to update security level.");
    }

  return TRUE;
}

static gboolean print_level = FALSE;
static gboolean skip_sb_check = FALSE;

static GOptionEntry opts[] =
{
  { "seclevel", 's', 0, G_OPTION_ARG_NONE, &print_level, "Print security level and exit", NULL },
  { "skip-sb-check", 0, 0, G_OPTION_ARG_NONE, &skip_sb_check, "Enforce PAYG even if Secure Boot is off", NULL },
  { NULL }
};

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GError) error = NULL;
  g_autoptr(EpgService) service = NULL;
  g_autoptr(GFile) state_dir = NULL;
  int ret = EXIT_SUCCESS, sd_notify_ret, system_ret, exit_signal = 0;
  int lsm_fd;
  guint timeout_id = 0, watchdog_id = 0;
  const gchar *sd_socket_env = NULL;
  g_autofree char *sd_socket_dir = NULL;
  g_autofree char *sd_socket_name = NULL;
  GOptionContext *context;
  gboolean enforcing_mode = TRUE;

  context = g_option_context_new ("- Pay As You Go enforcement daemon");
  g_option_context_add_main_entries (context, opts, GETTEXT_PACKAGE);
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_warning ("Failed to parse options: %s", error->message);
      return EXIT_FAILURE;
    }
  if (print_level)
    {
      /* This is intentionally user-hostile, with no trailing newline.
       * That makes it easier for the provisioning tool to consume the
       * raw value.
       */
      printf ("%c", EPG_SECURITY_LEVEL);
      exit (0);
    }

  /* If eos-paygd is running from the initramfs, change the process name so
   * that it survives the pivot to the final root filesystem. This is an
   * (ab)use of the functionality systemd has to support storage daemons needed
   * for mounting/unmounting the root filesystem. See
   * https://phabricator.endlessm.com/T27037
   */
  if (access ("/etc/initrd-release", F_OK) >= 0)
    {
      argv[0][0] = '@';

      g_debug ("eos-paygd running from initramfs");

      if (!eospayg_efi_init (0))
        {
          g_warning ("Unable to access EFI variables, shutting down in %d minutes",
                     TIMEOUT_POWEROFF_ON_ERROR_MINUTES);
          g_timeout_add_seconds (TIMEOUT_POWEROFF_ON_ERROR_MINUTES * 60,
                                 payg_system_poweroff, NULL);
        }

      payg_set_debug_env_vars ();

      /* Don't enforce PAYG if the current boot is not secure. This likely
       * means the machine is being unlocked for debugging purposes, or has
       * been paid off. A command line flag can be used to skip this check.
       * Note that we can't simply exit; systemd expects us to send READY=1
       * so it knows to proceed with the root pivot.
       */
      if (!skip_sb_check && !payg_get_secure_boot_enabled ())
        {
          g_debug ("Secure Boot is not enabled; not enforcing PAYG");
          enforcing_mode = FALSE;
        }

      /* Also don't enforce PAYG if EOSPAYG_active is not set; this could mean
       * the machine has been paid off or unlocked for another reason.
       */
      if (!payg_get_eospayg_active_set ())
        {
          g_debug ("EOSPAYG_active is not set; not enforcing PAYG");
          enforcing_mode = FALSE;
        }

      /* If we fail the securitylevel test we still want to complete
       * booting and have a chance at doing a system update to recover
       * from our currently broken state, but the shutdown is
       * inevitable.
       */
      if (enforcing_mode && payg_should_check_securitylevel () &&
          !test_and_update_securitylevel ())
        {
          g_warning ("Security level regressed, shutting down in %d minutes",
                     TIMEOUT_POWEROFF_ON_ERROR_MINUTES);
          g_timeout_add_seconds (TIMEOUT_POWEROFF_ON_ERROR_MINUTES * 60,
                                 payg_system_poweroff, NULL);
        }

      /* Setup RTC updater before the root pivot */
      if (enforcing_mode && !payg_hwclock_init ())
        {
          g_warning ("RTC failure, shutting down in %d minutes",
                     TIMEOUT_POWEROFF_ON_ERROR_MINUTES);
          g_timeout_add_seconds (TIMEOUT_POWEROFF_ON_ERROR_MINUTES * 60,
                                 payg_system_poweroff, NULL);
        }
    }
  else
    {
      /* To support existing deployments which use grub and can't be OTA
       * updated to Phase 4 PAYG security, we have to support running from the
       * root filesystem and in that case not use any features that are
       * unsupported on those grub systems, like state data encryption.
       * https://phabricator.endlessm.com/T27524 */
      g_debug ("eos-paygd running from root filesystem, entering backward compat mode");
      payg_internal_set_legacy_mode();
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

  g_debug ("epg_service_secure_init_sync() completed");

  if (!payg_get_legacy_mode ())
    {
      if (enforcing_mode && payg_should_use_watchdog ())
        {
          /* Open and start pinging the custom watchdog timer ("endlessdog") which
           * will ask for a shut down after not being pinged for 19 minutes, and
           * force a shutdown after 20 minutes. This means that if eos-paygd is
           * somehow killed or crashes, PAYG will not go unenforced. Ping it every
           * 60 seconds so we have a margin of error in case another high priority
           * task is happening on the main loop. And use O_CLOEXEC in case it's
           * somehow possible to execve() this process after the root pivot. We
           * ping the watchdog even if PAYG is not active (e.g. it's not yet
           * provisioned or has been paid off) to prevent any other process from
           * accidentally or maliciously using the watchdog timer.
           */
          g_debug ("Opening /dev/watchdog");
          watchdog_fd = open ("/dev/watchdog", O_WRONLY | O_CLOEXEC);
          if (watchdog_fd == -1)
            {
              g_warning ("eos-paygd could not open /dev/watchdog: %m");
              return EXIT_FAILURE;
            }
          watchdog_id = g_timeout_add_seconds_full (G_PRIORITY_HIGH, 60, ping_watchdog, NULL, NULL);
          g_assert (watchdog_id > 0);
        }

      if (enforcing_mode && payg_should_use_lsm ())
        {
          /* Activate the LSM which will protect this process from various
           * signals, protect it from being ptraced, remove it from /proc, and
           * give it privileged access to EFI variables. We don't ever want to
           * close the fd (we'd lose the protection), but use O_CLOEXEC anyway
           * since we don't expect an execve() to happen.
           */
          g_debug ("Opening /sys/kernel/security/endlesspayg/paygd_pid");
          lsm_fd = open("/sys/kernel/security/endlesspayg/paygd_pid", O_RDONLY | O_CLOEXEC);
          if (lsm_fd == -1)
            {
              g_warning ("eos-paygd could not open /sys/kernel/security/endlesspayg/paygd_pid: %m");
              return EXIT_FAILURE;
            }
          else
            {
              /* Forget the fd since we don't even want to close it. */
              lsm_fd = -1;
            }
        }

      /* Here be dragons:
       * We're currently in the initramfs root directory, and systemd is putting
       * all the useful bits of the system into /sysroot in preparation for the
       * root pivot. After the root pivot, it will delete everything in the
       * initramfs root, which will leave us in limbo forever.  We need to
       * chroot() out of this into sysroot.
       *
       * However, once we do that, we no longer have access to the socket we
       * need to write to to inform systemd we're ready for it to do the root
       * pivot.
       *
       * What we do is chdir() into the directory $NOTIFY_SOCKET resides in,
       * chroot() into /sysroot, then use our own variant of sd_notify() that
       * doesn't refuse to use relative paths to notify systemd we're ready.
       *
       * After that, we can chdir() into our new / and begin trying to connect
       * to the dbus socket. */

      sd_socket_env = g_getenv("NOTIFY_SOCKET");
      if (!sd_socket_env)
        {
          /* If we can't notify systemd that we're ready to move on, it will
           * timeout in 90 seconds and shutdown, might as well just exit. */
          g_warning ("NOTIFY_SOCKET not set");
          return EXIT_FAILURE;
        }
      sd_socket_dir = g_path_get_dirname (sd_socket_env);
      sd_socket_name = g_path_get_basename (sd_socket_env);
      if (!sd_socket_dir || !sd_socket_name)
        {
          g_warning ("NOTIFY_SOCKET not in valid format");
          return EXIT_FAILURE;
        }
      if (chdir (sd_socket_dir))
        g_warning ("Unable to change working dir to systemd socket dir (%s): %m", sd_socket_dir);
      if (chroot ("/sysroot"))
        g_warning ("Unable to switch root to run-time root directory (/sysroot): %m");

      sd_notify_ret = payg_relative_sd_notify (sd_socket_name, "READY=1");
      if (sd_notify_ret < 0)
        {
          g_warning ("payg_relative_sd_notify() failed with code %d", -sd_notify_ret);
          return EXIT_FAILURE;
        }
      if (chdir ("/"))
        g_warning ("Unable to change working dir to root of run-time root directory: %m");

      /* Wait up to 20 minutes for the system D-Bus daemon to be started. A
       * shorter timeout would mean risking putting systems in an infinite boot
       * loop; in the past we've had long running operations like migrations of
       * flatpaks occur during a reboot. */
      g_debug ("Attempting to connect to D-Bus daemon");
      timeout_id = g_timeout_add_seconds (TIMEOUT_POWEROFF_ON_ERROR_MINUTES * 60,
                                          payg_system_poweroff, NULL);
      while (TRUE)
        {
          g_autoptr(GDBusConnection) bus_connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
          if (bus_connection == NULL)
            {
              g_debug ("Error connecting to system bus, will retry: %s", error->message);
              g_clear_error (&error);

              /* Keep pinging the watchdog if we have it, but don't block in
               * case we don't have it. We also need to iterate the main
               * context for payg_system_poweroff() to have a chance to be
               * triggered.
               */
              g_main_context_iteration (NULL, FALSE);

              g_usleep (G_USEC_PER_SEC);
            }
          else
            break;
        }
      g_source_remove (timeout_id);
      timeout_id = 0;

      /* Now that we've connected to the dbus socket we know that / is populated.
       * We were able to connect to dbus with an absolute path, but to be certain
       * that relative paths work in the future, let's chdir into / one last time
       * to be sure we're actually there and not "under" a mount point. */
      if (chdir ("/"))
        g_warning ("Unable to re-establish root directory: %m");
      else
        g_debug ("Pivoted to final root filesystem");

      /* Tell our efi code that we're post root pivot so it stops us from doing
       * anything unsafe
       */
      eospayg_efi_root_pivot ();
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
  else
    {
      /* Use /bin/mkdir instead of mkdir() to ensure the mode is unaffected by
       * the process's umask.
       */
      system_ret = system ("/bin/mkdir --mode=700 /var/lib/eos-payg");
      if (system_ret == -1 || !WIFEXITED (system_ret) || WEXITSTATUS (system_ret) != 0)
        g_warning ("mkdir of /var/lib/eos-payg failed");
    }

  if (enforcing_mode)
    {
      /* Set up a D-Bus service and run until we are killed. */
      g_debug ("Starting EpgService to enforce PAYG");
      gss_service_run (GSS_SERVICE (service), argc, argv, &error);
    }
  else
    g_message ("Not enforcing PAYG for this boot");

  if (error != NULL)
    {
      if (g_error_matches (error, GSS_SERVICE_ERROR, GSS_SERVICE_ERROR_SIGNALLED))
        {
          /* The service received SIGTERM or SIGINT */
          timeout_id = g_idle_add (payg_system_poweroff, &timeout_id);
          exit_signal = gss_service_get_exit_signal (GSS_SERVICE (service));
        }
      else
        {
          /* This could mean the PAYG data has been erased or the service lost
           * ownership of the bus name, the latter most likely because the
           * D-Bus server was terminated or restarted.*/
          g_warning ("Daemon exited, shutting down in %d minutes: %s",
                     TIMEOUT_POWEROFF_ON_ERROR_MINUTES, error->message);
          timeout_id = g_timeout_add_seconds (TIMEOUT_POWEROFF_ON_ERROR_MINUTES * 60,
                                              payg_system_poweroff, NULL);
          ret = EXIT_FAILURE;
        }
    }
  else
    {
      g_debug ("EpgService exited successfully or did not run");
    }

  allow_writing_to_boot_partition (FALSE);

  /* If payg_system_poweroff is scheduled, spin the mainloop until it runs.
   * timeout_id will be cleared by payg_system_poweroff.
   */
  while (timeout_id)
    g_main_context_iteration (NULL, TRUE);

  if (exit_signal != 0)
    /* If the service exited due to a signal we should not exit with an error
     * status, as this is likely systemd's SIGTERM when stopping the service.
     * Let's just re-raise the signal so the unit ends up with a clean
     * termination status.
     */
    raise (exit_signal);

  if (ret == EXIT_SUCCESS && watchdog_id > 0)
    {
      g_message ("Entering watchdog-ping-only mode");
      while (TRUE)
        g_main_context_iteration (NULL, TRUE);
    }

  return ret;
}
