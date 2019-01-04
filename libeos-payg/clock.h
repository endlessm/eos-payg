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

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define EPG_TYPE_CLOCK (epg_clock_get_type ())
G_DECLARE_INTERFACE (EpgClock, epg_clock, EPG, CLOCK, GObject)

struct _EpgClockInterface
{
  GTypeInterface parent;

  gint64     (*get_wallclock_time) (EpgClock *self);
  gint64     (*get_time) (EpgClock *self);
  GSource   *(*source_new_seconds) (EpgClock *self,
                                    guint     interval,
                                    GError  **error);
};

gint64 epg_clock_get_wallclock_time (EpgClock *self);

gint64 epg_clock_get_time (EpgClock *self);

GSource *epg_clock_source_new_seconds (EpgClock  *self,
                                       guint      interval,
                                       GError   **error);

G_END_DECLS
