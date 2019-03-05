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

#include <glib.h>
#include <libeos-payg/clock-jump-source.h>
#include <locale.h>
#include <unistd.h>

/* 60 FPS should be enough for anyone. */
#define ITERATIONS 30
#define USEC_PER_MSEC 1000

static gboolean
timeout_cb (gpointer data)
{
  guint *called = (guint *) data;
  *called += 1;
  return G_SOURCE_CONTINUE;
}

/* Test that the source callback is executed for forward and backward jumps of
 * the system clock. */
static void
test_forward_and_backward (void)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GSource) source = NULL;
  guint called = 0;
  struct timespec original_ts;
  struct timespec jump_ts;
  int ret;

  if (geteuid () != 0)
    {
      g_test_skip ("Setting the system clock requires root");
      return;
    }

  source = epg_clock_jump_source_new (&local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (source);

  g_source_set_callback (source, timeout_cb, &called, NULL);
  g_source_attach (source, NULL);

  ret = clock_gettime (CLOCK_REALTIME, &original_ts);
  g_assert_cmpint (ret, ==, 0);

  /* Set the clock back 10 seconds */
  jump_ts.tv_sec = original_ts.tv_sec - 10;
  jump_ts.tv_nsec = original_ts.tv_nsec;
  ret = clock_settime (CLOCK_REALTIME, &jump_ts);
  g_assert_cmpint (ret, ==, 0);

  while (called != 1)
    g_main_context_iteration (NULL, TRUE);

  /* Set the clock forward 10 seconds */
  jump_ts.tv_sec += 10;
  ret = clock_settime (CLOCK_REALTIME, &jump_ts);
  g_assert_cmpint (ret, ==, 0);

  while (called != 2)
    g_main_context_iteration (NULL, TRUE);

  g_source_destroy (source);
  g_assert_cmpuint (called, ==, 2);
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/clock-jump-source/forward-and-backward", test_forward_and_backward);

  return g_test_run ();
}
