/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2020 Endless OS Foundation LLC
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

#include <util.h>
#include <linux/rtc.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

static int rtc_fd = -1;
static gboolean queued = FALSE;
static gboolean warned = FALSE;

/* Sets the hardware clock to the system clock time
 * Note: this assumes the hardware clock is in UTC, which
 * should always be the case for standalone EOS systems.
 */
static gboolean
payg_hwclock_update (gpointer unused)
{
  int err;
  time_t now_sec;
  struct tm now_tm;

  /* We make a trivial effort to prevent doing a stack of
   * time updates on the same main loop iteration, but
   * we don't try very hard because it doesn't matter
   * too much.
   */
  queued = FALSE;

  time (&now_sec);
  gmtime_r (&now_sec, &now_tm);

  /* The RTC docs indicate the third param should be
   * struct rtc_time, however hwclock-rtc.c in util-linux
   * uses a struct tm. This works because the structs
   * are identical.
   */
  err = ioctl (rtc_fd, RTC_SET_TIME, &now_tm);
  if (err != 0 && !warned)
    {
      warned = TRUE;
      g_warning ("Failed to update hardware clock: %s", g_strerror (errno));
    }
  else
      g_debug ("Updated RTC time to %s", asctime (&now_tm));
  return FALSE;
}

/**
 * payg_hwclock_queue_update:
 *
 * Schedule the system clock to be written to the hardware
 * clock on the next main loop iteration.
 */
void
payg_hwclock_queue_update (void)
{
  /* If we failed to init the clock, we're still running for
   * 20 minutes until forced shutdown - so we must check for
   * fd validity, but we don't need to log anything else.
   */
  if (rtc_fd == -1)
    return;

  if (!queued)
    {
      queued = TRUE;
      g_idle_add (payg_hwclock_update, NULL);
    }
}

static gboolean
source_hwclock_update (gpointer unused)
{
  payg_hwclock_queue_update ();
  return TRUE;
}

/* payg_hwclock_init:
 *
 * Initialize hardware clock subsystem.
 * This must be called before the root pivot.
 *
 * Returns: %TRUE on success, %FAIL otherwise
 */
gboolean
payg_hwclock_init (void)
{
  time_t sys_secs, rtc_secs;
  struct tm rtc_tm;
  int err;

  rtc_fd = open ("/dev/rtc", O_RDWR);
  if (rtc_fd == -1)
    {
      g_warning ("Failed to open RTC device: %s", g_strerror (errno));
      return FALSE;
    }

  /* If the system time and RTC time aren't roughly similar
   * then systemd has bumped the time to be newer than its
   * NEWS file was at build time.
   *
   * This almost certainly means the RTC is broken or has
   * been reset by battery removal, so fail out.
   */
  err = ioctl (rtc_fd, RTC_RD_TIME, &rtc_tm);
  if (err != 0)
    {
      g_warning ("Failed to read RTC: %s", g_strerror (errno));
      return FALSE;
    }
  rtc_secs = timegm (&rtc_tm);
  time (&sys_secs);
  g_debug ("RTC time:        %s", asctime (&rtc_tm));
  g_debug ("system UTC time: %s", asctime (gmtime (&sys_secs)));
  g_debug ("RTC secs:        %ld\n", rtc_secs);
  g_debug ("system UTC secs: %ld\n", sys_secs);
  /* Knock off a few binary digits to be safe, this will
   * drop a few days of precision. If the clock was reset
   * by battery removal, it will shift years.
   */
  rtc_secs >>= 19;
  sys_secs >>= 19;
  if (rtc_secs < sys_secs)
    {
      g_warning ("RTC out of sync with system clock at boot");
      return FALSE;
    }

  /* Set up a timer to update the hwclock every 659 seconds,
   * just like ntp would on a normal system.
   */
  g_timeout_add_seconds (659, source_hwclock_update, NULL);

  return TRUE;
}
