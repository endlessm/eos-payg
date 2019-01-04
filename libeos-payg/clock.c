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

#include <clock.h>
#include <glib.h>

G_DEFINE_INTERFACE (EpgClock, epg_clock, G_TYPE_OBJECT)

static void
epg_clock_default_init (EpgClockInterface *iface)
{
}

/**
 * epg_clock_get_wallclock_time:
 * @self: an #EpgClock
 *
 * Like g_get_real_time(), this returns the wall clock time, but the
 * implementation determines what type of clock to use and seconds are used
 * instead of microseconds.
 *
 * Returns: the number of seconds since January 1, 1970 UTC
 * Since: 0.2.1
 */
gint64
epg_clock_get_wallclock_time (EpgClock *self)
{
  EpgClockInterface *iface;

  g_return_val_if_fail (EPG_IS_CLOCK (self), -1);

  iface = EPG_CLOCK_GET_IFACE (self);
  g_assert (iface->get_wallclock_time != NULL);
  return iface->get_wallclock_time (self);
}

/**
 * epg_clock_get_time:
 * @self: an #EpgClock
 *
 * This returns the time based on the same clock used for
 * epg_clock_source_new_seconds()
 *
 * Returns: the time since some unspecified starting point, in seconds
 * Since: 0.2.1
 */
gint64
epg_clock_get_time (EpgClock *self)
{
  EpgClockInterface *iface;

  g_return_val_if_fail (EPG_IS_CLOCK (self), -1);

  iface = EPG_CLOCK_GET_IFACE (self);
  g_assert (iface->get_time != NULL);
  return iface->get_time (self);
}

/**
 * epg_clock_source_new_seconds:
 * @self: an #EpgClock
 * @interval: the timeout interval in seconds
 * @error: return location for a #GError, or %NULL
 *
 * Like g_timeout_source_new_seconds(), but the implementation determines what
 * type of clock to use. If @interval is set to zero, the source will be ready
 * the next time it's checked.
 *
 * Returns: (transfer full): the newly-created timeout source, or %NULL with
 *   @error set
 * Since: 0.2.1
 */
GSource *
epg_clock_source_new_seconds (EpgClock  *self,
                              guint      interval,
                              GError   **error)
{
  EpgClockInterface *iface;

  g_return_val_if_fail (EPG_IS_CLOCK (self), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  iface = EPG_CLOCK_GET_IFACE (self);
  g_assert (iface->source_new_seconds != NULL);
  return iface->source_new_seconds (self, interval, error);
}
