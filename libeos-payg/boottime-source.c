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
#include <gio/gio.h>
#include <libeos-payg/boottime-source.h>

typedef struct {
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

  if (callback == NULL)
    {
      g_warning ("Boottime source dispatched without callback. "
                 "You must call g_source_set_callback().");
      return G_SOURCE_REMOVE;
    }

  /* Must read from the FD to reset its ready state. */
  if (read (self->fd, &n_expirations, sizeof (n_expirations)) < 0)
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
  g_autoptr(GError) local_error = NULL;

  if (!g_close (self->fd, &local_error))
    g_warning ("Failed to close timerfd: %s", local_error->message);
  self->fd = -1;
}

static const GSourceFuncs epg_boottime_source_funcs = {
  .check = epg_boottime_source_check,
  .dispatch = epg_boottime_source_dispatch,
  .finalize = epg_boottime_source_finalize,
};

/**
 * epg_boottime_source_new:
 * @interval_ms: the timeout interval, in ms
 * @error: return location for a #GError, or %NULL
 *
 * Like g_timeout_source_new(), but uses `CLOCK_BOOTTIME` to account for time
 * when the system is suspended.
 *
 * If @interval_ms is set to zero, the #GSource will be ready the next time
 * it's checked.
 *
 * @error will be set to a #GIOError if, for example, the process runs out
 * of file descriptors.
 *
 * Returns: (transfer full): a new `CLOCK_BOOTTIME` #GSource, or %NULL with
 *   @error set
 * Since: 0.2.1
 */
GSource *
epg_boottime_source_new (guint    interval_ms,
                         GError **error)
{
  g_autoptr(GSource) source = NULL;
  EpgBoottimeSource *self = NULL;
  int fd;
  struct itimerspec its = {
    .it_interval = {
      .tv_sec = interval_ms / 1000,
      .tv_nsec = (interval_ms % 1000) * (1000 * 1000),
    }
  };

  its.it_value = its.it_interval;

  /* Set the GError if timerfd_create() fails because it could be e.g. ENFILE
   * which we should handle gracefully */
  fd = timerfd_create (CLOCK_BOOTTIME, TFD_CLOEXEC | TFD_NONBLOCK);
  if (G_UNLIKELY (fd < 0))
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                   "timerfd_create (CLOCK_BOOTTIME, TFD_CLOEXEC | TFD_NONBLOCK) failed: %s",
                   g_strerror (errno));
      return NULL;
    }

  /* But use g_error() if timerfd_settime() fails which likely would indicate
   * programmer error */
  /* FIXME: If this is upstreamed to GLib, handle EINVAL gracefully */
  if (G_UNLIKELY (timerfd_settime (fd,
                                   0,
                                   &its,
                                   NULL /* old_value */) < 0))
    g_error ("timerfd_settime() failed: %s",
             g_strerror (errno));

  source = g_source_new ((GSourceFuncs *)&epg_boottime_source_funcs,
                         sizeof (EpgBoottimeSource));
  self = (EpgBoottimeSource *) source;
  self->fd = fd;
  self->tag = g_source_add_unix_fd (source, fd,
                                    G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL);

  return g_steal_pointer (&source);
}

/**
 * epg_get_boottime:
 *
 * Like g_get_monotonic_time(), but also includes any time the system is
 * suspended. Uses CLOCK_BOOTTIME, hence the name, but is not guaranteed to be
 * the time since boot.
 *
 * Returns: the time since some unspecified starting point, in microseconds
 * Since: 0.2.1
 */
gint64
epg_get_boottime (void)
{
  struct timespec ts;
  gint result;

  result = clock_gettime (CLOCK_BOOTTIME, &ts);

  if (G_UNLIKELY (result != 0))
    g_error ("clock_gettime (CLOCK_BOOTTIME) failed: %s",
             g_strerror (errno));

  return (((gint64) ts.tv_sec) * 1000000) + (ts.tv_nsec / 1000);
}

