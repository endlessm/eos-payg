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
#include <gio/gio.h>
#include <libeos-payg/multi-task.h>
#include <locale.h>

static void
async_cb (GObject      *source_object,
          GAsyncResult *result,
          gpointer      user_data)
{
  GAsyncResult **result_out = user_data;

  *result_out = g_object_ref (result);
}

static void
wait (GAsyncResult **result_out)
{
  while (*result_out == NULL)
    g_main_context_iteration (NULL, TRUE);
}

/* Test that the last boolean passed to epg_multi_task_return_boolean() is the
 * one returned.
 */
static void
test_returns_last_boolean (void)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GTask) task = g_task_new (NULL, NULL, async_cb, &result);

  epg_multi_task_attach (task, 2);
  epg_multi_task_return_boolean (task, FALSE);
  epg_multi_task_return_boolean (task, TRUE);

  wait (&result);
  g_assert_true (G_IS_TASK (result));
  g_assert_true (G_TASK (result) == task);

  g_autoptr(GError) local_error = NULL;
  gboolean ret = g_task_propagate_boolean (task, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
}

static void
store_true (gpointer data)
{
  gboolean *v = data;
  *v = TRUE;
}

/* Test that the last pointer passed to epg_multi_task_return_pointer() is the
 * one returned, and that the previous ones are freed.
 */
static void
test_returns_last_pointer (void)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GTask) task = g_task_new (NULL, NULL, async_cb, &result);

  gboolean one = FALSE, two = FALSE;

  epg_multi_task_attach (task, 2);
  epg_multi_task_return_pointer (task, &one, store_true);
  epg_multi_task_return_pointer (task, &two, store_true);

  wait (&result);
  g_assert_true (G_IS_TASK (result));
  g_assert_true (G_TASK (result) == task);

  g_autoptr(GError) local_error = NULL;
  gpointer ret = g_task_propagate_pointer (task, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret == &two);
  g_assert_true (one);
  g_assert_false (two);
}

/* Tests that the first error is propagated, even if the final return is a
 * boolean.
 */
static void
test_returns_error_before_boolean (void)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GTask) task = g_task_new (NULL, NULL, async_cb, &result);

  g_autoptr(GError) one = g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "oh no");
  g_autoptr(GError) two = g_error_new (G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY, "oh no");

  epg_multi_task_attach (task, 3);
  epg_multi_task_return_error (task, "one", g_error_copy (one));
  epg_multi_task_return_error (task, "two", g_error_copy (two));
  epg_multi_task_return_boolean (task, TRUE);

  wait (&result);
  g_assert_true (G_IS_TASK (result));
  g_assert_true (G_TASK (result) == task);

  g_autoptr(GError) local_error = NULL;
  gboolean ret = g_task_propagate_boolean (task, &local_error);
  g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_assert_false (ret);
}

/* Tests that the first error is propagated, even if the final return is a
 * pointer.
 */
static void
test_returns_error_before_pointer (void)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GTask) task = g_task_new (NULL, NULL, async_cb, &result);

  g_autoptr(GError) one = g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "oh no");
  g_autoptr(GError) two = g_error_new (G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY, "oh no");

  epg_multi_task_attach (task, 3);
  epg_multi_task_return_error (task, "one", g_error_copy (one));
  epg_multi_task_return_error (task, "two", g_error_copy (two));
  epg_multi_task_return_pointer (task, g_strdup ("three"), g_free);

  wait (&result);
  g_assert_true (G_IS_TASK (result));
  g_assert_true (G_TASK (result) == task);

  g_autoptr(GError) local_error = NULL;
  gpointer ret = g_task_propagate_pointer (task, &local_error);
  g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_assert_null (ret);
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/multi-task/returns-last-boolean", test_returns_last_boolean);
  g_test_add_func ("/multi-task/returns-last-pointer", test_returns_last_pointer);
  g_test_add_func ("/multi-task/returns-error-before-boolean", test_returns_error_before_boolean);
  g_test_add_func ("/multi-task/returns-error-before-pointer", test_returns_error_before_pointer);

  return g_test_run ();
}
