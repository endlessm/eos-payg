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

#include <errno.h>
#include <inttypes.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libeos-payg/boottime-source.h>

typedef struct _EpgBoottimeSource {
  GSource parent;

  int fd;
  gpointer tag;
} EpgBoottimeSource;

static gboolean
epg_boottime_source_check (GSource *source)
{
  EpgBoottimeSource *self = (EpgBoottimeSource *) source;

  return g_source_query_unix_fd (source, self->tag) != 0;
}

static gboolean
epg_boottime_source_dispatch (GSource    *source,
                                      GSourceFunc callback,
                                      gpointer    user_data)
{
  EpgBoottimeSource *self = (EpgBoottimeSource *) source;
  uint64_t n_expirations = 0;

  if (!callback)
    {
      g_warning ("Boottime source dispatched without callback. "
                 "You must call g_source_set_callback().");
      return G_SOURCE_REMOVE;
    }

  /* Must read from the FD to reset its ready state. */
  if (read (self->fd, &n_expirations, sizeof n_expirations) < 0)
    {
      g_warning ("read() failed for timerfd: %s",
                 g_strerror (errno));
      return G_SOURCE_REMOVE;
    }

  return callback (user_data);
}

static void
epg_boottime_source_finalize (GSource *source)
{
  EpgBoottimeSource *self = (EpgBoottimeSource *) source;
  g_autoptr(GError) error = NULL;

  if (!g_close (self->fd, &error))
    g_warning ("failed to close timerfd: %s", error->message);
  self->fd = -1;
}

static GSourceFuncs epg_boottime_source_funcs = {
  .check = epg_boottime_source_check,
  .dispatch = epg_boottime_source_dispatch,
  .finalize = epg_boottime_source_finalize,
};

/**
 * epg_boottime_source_new():
 *
 * Like g_timeout_source_new(), but uses CLOCK_BOOTTIME to account for time
 * when the system is suspended.
 **/
GSource *
epg_boottime_source_new (guint interval_ms)
{
  GSource *source = NULL;
  EpgBoottimeSource *self = NULL;
  int fd;
  struct itimerspec its = {
    .it_interval = {
      .tv_sec = interval_ms / 1000,
      .tv_nsec = (interval_ms % 1000) * (1000 * 1000),
    }
  };

  its.it_value = its.it_interval;

  fd = timerfd_create (CLOCK_BOOTTIME, TFD_CLOEXEC);
  if G_UNLIKELY (fd < 0)
    g_error ("timerfd_create (CLOCK_BOOTTIME, TFD_CLOEXEC) failed: %s",
             g_strerror (errno));

  if G_UNLIKELY (timerfd_settime (fd,
                                  0 /*TFD_NONBLOCK */,
                                  &its,
                                  NULL /* old_value */) < 0)
    g_error ("timerfd_settime() failed: %s",
             g_strerror (errno));

  source = g_source_new (&epg_boottime_source_funcs,
                         sizeof (EpgBoottimeSource));
  self = (EpgBoottimeSource *) source;
  self->fd = fd;
  self->tag = g_source_add_unix_fd (source, fd,
                                    G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL);

  return source;
}

/**
 * epg_get_boottime:
 *
 * Like g_get_monotonic_time(), but also includes any time the system is
 * suspended. Uses CLOCK_BOOTTIME, hence the name, but is not guaranteed to be
 * the time since boot.
 *
 * Returns: the time since some unspecified starting point, in microseconds
 **/
gint64
epg_get_boottime (void)
{
  struct timespec ts;
  gint result;

  result = clock_gettime (CLOCK_BOOTTIME, &ts);

  if G_UNLIKELY (result != 0)
    g_error ("clock_gettime (CLOCK_BOOTTIME) failed: %s",
             g_strerror (errno));

  return (((gint64) ts.tv_sec) * 1000000) + (ts.tv_nsec / 1000);
}

