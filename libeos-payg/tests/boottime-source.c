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
#include <libeos-payg/boottime-source.h>
#include <locale.h>

/* 60 FPS should be enough for anyone. */
#define INTERVAL_MS 16
#define ITERATIONS 30
#define USEC_PER_MSEC 1000

static gboolean
timeout_cb (gpointer data)
{
  guint *called = (guint *) data;
  *called += 1;
  return G_SOURCE_CONTINUE;
}

/* Test the timeout firing once */
static void
test_once (void)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GSource) source = NULL;
  guint called = 0;
  gint64 start = epg_get_boottime ();
  gint64 end;

  source = epg_boottime_source_new (INTERVAL_MS, &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (source);

  g_source_set_callback (source, timeout_cb, &called, NULL);
  g_source_attach (source, NULL);
  while (called == 0)
    g_main_context_iteration (NULL, TRUE);

  g_source_destroy (source);
  g_assert_cmpuint (called, ==, 1);

  end = epg_get_boottime ();
  g_assert_cmpint (end, >, start);
  g_assert_cmpint (end - start, >, INTERVAL_MS * USEC_PER_MSEC);
}

/* Test the timeout firing many times */
static void
test_many (void)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GSource) source = NULL;
  guint called = 0;
  gint64 start = epg_get_boottime ();
  gint64 end;

  source = epg_boottime_source_new (INTERVAL_MS, &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (source);

  g_source_set_callback (source, timeout_cb, &called, NULL);
  g_source_attach (source, NULL);
  while (called < ITERATIONS)
    g_main_context_iteration (NULL, TRUE);

  g_source_destroy (source);
  g_assert_cmpuint (called, ==, ITERATIONS);

  end = epg_get_boottime ();
  g_assert_cmpint (end, >, start);
  /* Catches the case where the callback fires continuously after the first
   * interval, as may happen if, hypothetically, you forget to read() from the
   * fd.
   */
  g_assert_cmpint (end - start, >, ITERATIONS * INTERVAL_MS * USEC_PER_MSEC);
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/boottime-timeout-source/once", test_once);
  g_test_add_func ("/boottime-timeout-source/many", test_many);

  return g_test_run ();
}
