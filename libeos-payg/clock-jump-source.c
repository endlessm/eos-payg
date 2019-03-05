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

#include <errno.h>
#include <inttypes.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <limits.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libeos-payg/clock-jump-source.h>

/* This file is based on boottime-source.c */

typedef struct {
  GSource parent;

  int fd;
  gpointer tag;
} EpgClockJumpSource;

static gboolean
epg_clock_jump_source_check (GSource *source)
{
  EpgClockJumpSource *self = (EpgClockJumpSource *) source;

  return g_source_query_unix_fd (source, self->tag) != 0;
}

static gboolean
epg_clock_jump_source_dispatch (GSource    *source,
                                GSourceFunc callback,
                                gpointer    user_data)
{
  EpgClockJumpSource *self = (EpgClockJumpSource *) source;
  uint64_t n_expirations = 0;

  if (callback == NULL)
    {
      g_warning ("ClockJump source dispatched without callback. "
                 "You must call g_source_set_callback().");
      return G_SOURCE_REMOVE;
    }

  /* Must read from the FD to reset its ready state. */
  if (read (self->fd, &n_expirations, sizeof (n_expirations)) == 0 || errno != ECANCELED)
    {
      g_warning ("read() unexpectedly did not return ECANCELED for cancel-on-set timerfd: %s",
                 errno == 0 ? "No error" : g_strerror (errno));
      return G_SOURCE_REMOVE;
    }

  return callback (user_data);
}

static void
epg_clock_jump_source_finalize (GSource *source)
{
  EpgClockJumpSource *self = (EpgClockJumpSource *) source;
  g_autoptr(GError) local_error = NULL;

  if (!g_close (self->fd, &local_error))
    g_warning ("Failed to close timerfd: %s", local_error->message);
  self->fd = -1;
}

static const GSourceFuncs epg_clock_jump_source_funcs = {
  .check = epg_clock_jump_source_check,
  .dispatch = epg_clock_jump_source_dispatch,
  .finalize = epg_clock_jump_source_finalize,
};

/**
 * epg_clock_jump_source_new:
 * @error: return location for a #GError, or %NULL
 *
 * This creates a #GSource that will dispatch when the system clock jumps
 * (changes discontinously) which can happen for example when the user sets it
 * or NTP adjusts it. This is accomplished using the TFD_TIMER_CANCEL_ON_SET
 * flag supported by timerfd_settime().
 *
 * There is an unlikely race here: if the clock jumps while this function is
 * executing but before the timerfd is set up, that jump will be missed.
 *
 * @error will be set to a #GIOError if, for example, the process runs out
 * of file descriptors.
 *
 * Returns: (transfer full): a new #GSource, or %NULL with @error set
 * Since: 0.2.2
 */
GSource *
epg_clock_jump_source_new (GError **error)
{
  g_autoptr(GSource) source = NULL;
  EpgClockJumpSource *self = NULL;
  int fd;
  /* Unfortunately this will break in the year 2038 on platforms that define
   * time_t to be 4 bytes. Hopefully we're not still supporting e.g. the EC-100
   * at that point. */
  struct itimerspec its = {
    .it_interval = {
      .tv_sec = TIME_MAX,
      .tv_nsec = 0,
    }
  };

  its.it_value = its.it_interval;

  /* Set the GError if timerfd_create() fails because it could be e.g. ENFILE
   * which we should handle gracefully */
  fd = timerfd_create (CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
  if (G_UNLIKELY (fd < 0))
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "timerfd_create (CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK) failed: %s",
                   g_strerror (errno));
      return NULL;
    }

  /* But use g_error() if timerfd_settime() fails which likely would indicate
   * programmer error. We should never get EINVAL here on Endless OS. */
  if (G_UNLIKELY (timerfd_settime (fd,
                                   TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET,
                                   &its,
                                   NULL /* old_value */) < 0))
    g_error ("timerfd_settime() failed: %s", g_strerror (errno));

  source = g_source_new ((GSourceFuncs *)&epg_clock_jump_source_funcs,
                         sizeof (EpgClockJumpSource));
  self = (EpgClockJumpSource *) source;
  self->fd = fd;
  self->tag = g_source_add_unix_fd (source, fd,
                                    G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL);

  return g_steal_pointer (&source);
}
