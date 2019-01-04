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

#include <libeos-payg/real-clock.h>
#include <libeos-payg/boottime-source.h>

#define MSEC_PER_SEC 1000

struct _EpgRealClock
{
  GObject parent_instance;
};

static void clock_iface_init (EpgClockInterface *iface,
                              gpointer           iface_data);

G_DEFINE_TYPE_WITH_CODE (EpgRealClock, epg_real_clock, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (EPG_TYPE_CLOCK, clock_iface_init))

static gint64
epg_real_clock_get_wallclock_time (EpgClock *clock)
{
  EpgRealClock *self = (EpgRealClock *)clock;

  g_return_val_if_fail (EPG_IS_REAL_CLOCK (self), -1);

  return g_get_real_time () / G_USEC_PER_SEC;
}

static gint64
epg_real_clock_get_time (EpgClock *clock)
{
  EpgRealClock *self = (EpgRealClock *)clock;

  g_return_val_if_fail (EPG_IS_REAL_CLOCK (self), -1);

  return epg_get_boottime () / G_USEC_PER_SEC;
}

static GSource *
epg_real_clock_source_new_seconds (EpgClock  *clock,
                                   guint      interval,
                                   GError   **error)
{
  EpgRealClock *self = (EpgRealClock *)clock;
  guint interval_clamped;

  g_return_val_if_fail (EPG_IS_REAL_CLOCK (self), NULL);

  interval_clamped = MIN (interval, G_MAXUINT / MSEC_PER_SEC);
  return epg_boottime_source_new (interval_clamped * MSEC_PER_SEC, error);
}

static void
clock_iface_init (EpgClockInterface *iface,
                  gpointer           iface_data)
{
  iface->get_wallclock_time = epg_real_clock_get_wallclock_time;
  iface->get_time = epg_real_clock_get_time;
  iface->source_new_seconds = epg_real_clock_source_new_seconds;
}

static void
epg_real_clock_class_init (EpgRealClockClass *klass)
{
}

static void
epg_real_clock_init (EpgRealClock *self)
{
}

EpgRealClock *
epg_real_clock_new (void)
{
    return g_object_new (EPG_TYPE_REAL_CLOCK, NULL);
}
