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

#include <libeos-payg/fake-clock.h>
#include <libeos-payg/boottime-source.h>

#define MSEC_PER_SEC 1000

struct _EpgFakeClock
{
  GObject parent_instance;

  gint64 time_secs;
  gint64 wallclock_time_secs;
};

static void clock_iface_init (EpgClockInterface *iface,
                              gpointer           iface_data);

G_DEFINE_TYPE_WITH_CODE (EpgFakeClock, epg_fake_clock, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (EPG_TYPE_CLOCK, clock_iface_init))

static gint64
epg_fake_clock_get_wallclock_time (EpgClock *clock)
{
  EpgFakeClock *self = (EpgFakeClock *)clock;

  g_return_val_if_fail (EPG_IS_FAKE_CLOCK (self), -1);

  /* Initialize to a realistic value without introducing non-determinism */
  if (self->wallclock_time_secs == 0)
    self->wallclock_time_secs = 1231006505;

  return self->wallclock_time_secs;
}

/**
 * epg_fake_clock_set_wallclock_time:
 * @self: an #EpgFakeClock
 * @new_time: a positive value to set the clock to
 *
 * Set the time to @new_time so that value will be returned by
 * epg_clock_get_wallclock_time() until the next call to this function. This is
 * intended to be used by unit tests.
 *
 * Since: 0.2.1
 */
void
epg_fake_clock_set_wallclock_time (EpgFakeClock *self,
                                   gint64        new_time)
{
  g_return_if_fail (EPG_IS_FAKE_CLOCK (self));
  g_return_if_fail (new_time > 0);

  self->wallclock_time_secs = new_time;
}

static gint64
epg_fake_clock_get_time (EpgClock *clock)
{
  EpgFakeClock *self = (EpgFakeClock *)clock;

  g_return_val_if_fail (EPG_IS_FAKE_CLOCK (self), -1);

  /* Initialize to a realistic value without introducing non-determinism */
  if (self->time_secs == 0)
    self->time_secs = 424242;

  return self->time_secs;
}

/**
 * epg_fake_clock_set_time:
 * @self: an #EpgFakeClock
 * @new_time: a positive value to set the clock to
 *
 * Set the time to @new_time so that value will be returned by
 * epg_clock_get_time() until the next call to this function. This is intended
 * to be used by unit tests.
 *
 * Since: 0.2.1
 */
void
epg_fake_clock_set_time (EpgFakeClock *self,
                         gint64       new_time)
{
  g_return_if_fail (EPG_IS_FAKE_CLOCK (self));
  g_return_if_fail (new_time > 0);

  self->time_secs = new_time;
}

typedef struct {
  GSource parent;

  EpgFakeClock *clock; /* owned */
  gint64 ready_time_secs;
} EpgFakeSource;

static gboolean
epg_fake_source_check (GSource *source)
{
  EpgFakeSource *self = (EpgFakeSource *) source;

  return epg_clock_get_time (EPG_CLOCK (self->clock)) >= self->ready_time_secs;
}

static gboolean
epg_fake_source_dispatch (GSource    *source,
                          GSourceFunc callback,
                          gpointer    user_data)
{
  EpgFakeSource *self = (EpgFakeSource *) source;

  if (callback == NULL)
    {
      g_warning ("Fake source dispatched without callback. "
                 "You must call g_source_set_callback().");
      return G_SOURCE_REMOVE;
    }

  self->ready_time_secs = G_MAXINT64;

  return callback (user_data);
}

static void
epg_fake_source_finalize (GSource *source)
{
  EpgFakeSource *self = (EpgFakeSource *) source;

  g_clear_object (&self->clock);
}

static const GSourceFuncs epg_fake_source_funcs = {
  .check = epg_fake_source_check,
  .dispatch = epg_fake_source_dispatch,
  .finalize = epg_fake_source_finalize,
};

/**
 * epg_fake_clock_source_new_seconds:
 * @clock: an #EpgClock
 * @interval: the timeout interval in seconds
 * @error: a return location for #GError
 *
 * This creates a GSource which will be ready when @interval seconds have
 * passed according to epg_fake_clock_get_time().
 *
 * Returns: (transfer full) (nullable): the newly created #GSource
 */
static GSource *
epg_fake_clock_source_new_seconds (EpgClock  *clock,
                                   guint      interval,
                                   GError   **error)
{
  EpgFakeClock *self = (EpgFakeClock *)clock;
  g_autoptr(GSource) source = NULL;
  EpgFakeSource *fake_source = NULL;
  gint64 current_time = epg_clock_get_time (EPG_CLOCK (self));

  g_return_val_if_fail (EPG_IS_FAKE_CLOCK (self), NULL);
  g_return_val_if_fail (interval <= G_MAXINT64 - current_time, NULL);

  source = g_source_new ((GSourceFuncs *)&epg_fake_source_funcs,
                         sizeof (EpgFakeSource));
  fake_source = (EpgFakeSource *)source;
  fake_source->clock = g_object_ref (self);
  fake_source->ready_time_secs = current_time + interval;
  return g_steal_pointer (&source);
}

static void
clock_iface_init (EpgClockInterface *iface,
                  gpointer           iface_data)
{
  iface->get_wallclock_time = epg_fake_clock_get_wallclock_time;
  iface->get_time = epg_fake_clock_get_time;
  iface->source_new_seconds = epg_fake_clock_source_new_seconds;
}

static void
epg_fake_clock_class_init (EpgFakeClockClass *klass)
{
}

static void
epg_fake_clock_init (EpgFakeClock *self)
{
}

/**
 * epg_fake_clock_new:
 * @optional_time: A positive time value, or -1
 * @optional_wallclock_time: A positive wallclock time value, or -1
 *
 * EpgFakeClock is a clock that can be arbitrarily set forward or backward, and
 * is intended to be used for unit tests. It initializes with the values of
 * @optional_time and @optional_wallclock_time if set, and otherwise with
 * realistic, static values for both. The two different time values are roughly
 * analogous to time according to the `CLOCK_BOOTTIME` kernel clock, and time
 * according to the user-visible system clock.
 *
 * Once these values are initialized, they stand still and only
 * change when epg_fake_clock_set_time() or epg_fake_clock_set_wallclock_time()
 * is used.
 *
 * Returns: (transfer full): the newly-created #EpgFakeClock
 * Since: 0.2.1
 */
EpgFakeClock *
epg_fake_clock_new (gint64 optional_time,
                    gint64 optional_wallclock_time)
{
  EpgFakeClock *new_clock = g_object_new (EPG_TYPE_FAKE_CLOCK, NULL);

  if (optional_time > 0)
    epg_fake_clock_set_time (new_clock, optional_time);
  if (optional_wallclock_time > 0)
    epg_fake_clock_set_wallclock_time (new_clock, optional_wallclock_time);

  return new_clock;
}
