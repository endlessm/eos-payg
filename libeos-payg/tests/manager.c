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
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <libeos-payg/errors.h>
#include <libeos-payg/manager.h>
#include <libeos-payg-codes/codes.h>
#include <locale.h>

static const char KEY[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

typedef struct _Fixture {
  gchar *tmp_path;
  GFile *tmp_dir;

  gchar *expiry_time_path;
  gchar *used_codes_path;

  GBytes *key;
  EpcCounter next_counter;
} Fixture;

static void
setup (Fixture *fixture,
       gconstpointer data)
{
  g_autoptr(GError) error = NULL;

  fixture->tmp_path = g_dir_make_tmp ("libeos-payg-tests-manager-XXXXXX", &error);
  g_assert_no_error (error);
  g_assert_nonnull (fixture->tmp_path);

  fixture->tmp_dir = g_file_new_for_path (fixture->tmp_path);

  fixture->expiry_time_path = g_build_filename (fixture->tmp_path, "expiry-time", NULL);
  fixture->used_codes_path = g_build_filename (fixture->tmp_path, "used-codes", NULL);

  fixture->key = g_bytes_new_static (KEY, sizeof (KEY) - 1);
  fixture->next_counter = EPC_MINCOUNTER;
}

static gchar *
get_next_code (Fixture *fixture)
{
  EpcCode code;
  g_autoptr(GError) error = NULL;

  code = epc_calculate_code (EPC_PERIOD_5_SECONDS, fixture->next_counter++, fixture->key, &error);
  g_assert_no_error (error);
  return epc_format_code (code);
}

static void
remove_path (gchar *path)
{
  if (g_remove (path) != 0)
    {
      int errsv = errno;

      if (errsv != ENOENT)
        g_error ("Couldn't remove %s: %s", path, g_strerror (errsv));
    }
}

static void
remove_and_free_path (gchar *path)
{
  remove_path (path);
  g_free (path);
}

static void
teardown (Fixture *fixture,
          gconstpointer data)
{
  g_clear_pointer (&fixture->expiry_time_path, remove_and_free_path);
  g_clear_pointer (&fixture->used_codes_path, remove_and_free_path);

  if (fixture->tmp_path != NULL)
    {
      /* The manager internally performs asynchronous saves in a GTask thread.
       * There's currently no way to wait for them to finish. If it happens that
       * one of the files gets recreated after being removed above but before
       * we get here, removing the directory will fail. Ignore the error.
       */
      (void) g_remove (fixture->tmp_path);
      g_clear_pointer (&fixture->tmp_path, g_free);
    }

  g_clear_object (&fixture->tmp_dir);
  g_clear_pointer (&fixture->key, g_bytes_unref);
}

static void
async_cb (GObject      *source,
          GAsyncResult *result,
          gpointer      data)
{
  GAsyncResult **result_out = data;

  g_assert_null (*result_out);
  *result_out = g_object_ref (result);
}

static EpgManager *
manager_new_failable (Fixture *fixture,
                      GError **error)
{
  g_autoptr(GAsyncResult) result = NULL;

  epg_manager_new (TRUE, fixture->key, fixture->tmp_dir,
                   NULL, async_cb, &result);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  return epg_manager_new_finish (result, error);
}

static EpgManager *
manager_new (Fixture *fixture)
{
  g_autoptr(EpgManager) manager = NULL;
  g_autoptr(GError) error = NULL;

  manager = manager_new_failable (fixture, &error);
  g_assert_no_error (error);
  g_assert_nonnull (manager);

  return g_steal_pointer (&manager);
}

static void
save_state (EpgManager *manager)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GError) error = NULL;
  gboolean ret;

  epg_manager_save_state_async (manager, NULL, async_cb, &result);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  ret = epg_manager_save_state_finish (manager, result, &error);
  g_assert_no_error (error);
  g_assert_true (ret);
}

static void
test_manager_load_empty (Fixture *fixture,
                         gconstpointer data)
{
  guint64 start, expiry, end;

  start = g_get_real_time () / G_USEC_PER_SEC;
  g_autoptr(EpgManager) manager = manager_new (fixture);
  end = g_get_real_time () / G_USEC_PER_SEC;

  /* Default expiry time is "now". */
  expiry = epg_manager_get_expiry_time (manager);
  g_assert_cmpuint (start, <=, expiry);
  g_assert_cmpuint (expiry, <=, end);
}

static void
test_manager_load_error_malformed (Fixture *fixture,
                                   gconstpointer data)
{
  gsize path_offset = GPOINTER_TO_SIZE (data);
  const char *path_p = G_STRUCT_MEMBER (const char *, fixture, path_offset);
  gboolean ret;
  g_autoptr(EpgManager) manager = NULL;
  g_autoptr(GError) error = NULL;

  /* Set contents of state file to something illegal. It happens that a single
   * byte is illegal for all state files.
   */
  ret = g_file_set_contents (path_p, "X", 1, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  manager = manager_new_failable (fixture, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  if (g_strstr_len (error->message, -1, path_p) == NULL)
    g_error ("Error message '%s' does not contain state file path '%s'",
             error->message, path_p);
  g_assert_null (manager);
  g_clear_error (&error);

  /* The offending state file should have been cleaned up so subsequent
   * attempts work:
   */
  manager = manager_new_failable (fixture, &error);
  g_assert_no_error (error);
  g_assert_nonnull (manager);
}

static void
test_manager_load_error_unreadable (Fixture *fixture,
                                    gconstpointer data)
{
  gsize path_offset = GPOINTER_TO_SIZE (data);
  const char *path_p = G_STRUCT_MEMBER (const char *, fixture, path_offset);
  g_autoptr(EpgManager) manager = NULL;
  g_autoptr(GError) error = NULL;

  /* Create a directory where the state file should be. On sensible platforms
   * like Linux, you can't open() and read() a directory.
   */
  if (g_mkdir (path_p, 0755) != 0)
    g_error ("Couldn't create directory at '%s': %s",
             path_p, g_strerror (errno));

  manager = manager_new_failable (fixture, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY);
  if (g_strstr_len (error->message, -1, path_p) == NULL)
    g_error ("Error message '%s' does not contain state file path '%s'",
             error->message, path_p);
  g_assert_null (manager);
  g_clear_error (&error);

  /* Ideally, we would attempt to clean away the offending illegible 'file',
   * but that is not currently the case.
   */
#if 0
  manager = manager_new_failable (fixture, &error);
  g_assert_no_error (error);
  g_assert_nonnull (manager);
  test_manager_load_empty (fixture, NULL);
#endif
}

static void
test_manager_save_error (Fixture *fixture,
                         gconstpointer data)
{
  gboolean apply_code = GPOINTER_TO_INT (data);
  g_autoptr(EpgManager) manager = manager_new (fixture);
  g_autoptr(GError) error = NULL;
  gboolean ret;

  /* Sabotage any future attempts to save state. */
  remove_path (fixture->tmp_path);

  if (apply_code)
    {
      guint64 now = epg_manager_get_expiry_time (manager);
      g_autofree gchar *code_str = get_next_code (fixture);

      /* There's an internal call to epg_manager_save_state_async() here which
       * will later fail. Arguably this should make applying the code fail as
       * well.
       */
      ret = epg_manager_add_code (manager, code_str, now, &error);
      g_assert_no_error (error);
      g_assert_true (ret);
    }

  g_autoptr(GAsyncResult) result = NULL;

  epg_manager_save_state_async (manager, NULL, async_cb, &result);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  ret = epg_manager_save_state_finish (manager, result, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ret);
}

static void
test_manager_add_save_reload (Fixture *fixture,
                              gconstpointer data)
{
  g_autoptr(EpgManager) manager = manager_new (fixture);
  g_autofree gchar *code_str = NULL;
  g_autoptr(GError) error = NULL;
  guint64 expiry_before_code, now, expiry_after_code, expiry_after_reload;
  gboolean ret;

  expiry_before_code = epg_manager_get_expiry_time (manager);

  code_str = get_next_code (fixture);

  now = expiry_before_code + 5;
  ret = epg_manager_add_code (manager, code_str, now, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  expiry_after_code = epg_manager_get_expiry_time (manager);
  g_assert_cmpint (now + 5, ==, expiry_after_code);

  /* Adding the code kicks off an async save, but we can't be sure it will have
   * finished before we re-load the files. Force the issue here. Note that
   * there's actually no guarantee that this call will finish after the internal
   * save, but it doesn't matter because the content written will be the same.
   */
  save_state (manager);

  g_clear_object (&manager);
  manager = manager_new (fixture);
  expiry_after_reload = epg_manager_get_expiry_time (manager);
  g_assert_cmpuint (now + 5, ==, expiry_after_reload);
}

static void
test_manager_error_malformed (Fixture *fixture,
                              gconstpointer data)
{
  g_autoptr(EpgManager) manager = manager_new (fixture);
  g_autoptr(GError) error = NULL;
  guint64 expiry, now, expiry_after_code;
  gboolean ret;

  expiry = epg_manager_get_expiry_time (manager);
  now = expiry + 5;

  /* Just use an obviously-wrong code. The nuances are tested in libeos-payg-codes. */
  ret = epg_manager_add_code (manager, "abcdefgh", now, &error);
  g_assert_error (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_INVALID_CODE);
  g_assert_false (ret);

  expiry_after_code = epg_manager_get_expiry_time (manager);
  g_assert_cmpint (expiry, ==, expiry_after_code);
}

static void
test_manager_error_reused (Fixture *fixture,
                           gconstpointer data)
{
  g_autoptr(EpgManager) manager = manager_new (fixture);
  g_autofree gchar *code_str = get_next_code (fixture);
  g_autoptr(GError) error = NULL;
  guint64 expiry_before_code, now, expiry_after_code;
  gboolean ret;

  expiry_before_code = epg_manager_get_expiry_time (manager);

  now = expiry_before_code + 5;
  ret = epg_manager_add_code (manager, code_str, now, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  expiry_after_code = epg_manager_get_expiry_time (manager);
  g_assert_cmpint (now + 5, ==, expiry_after_code);

  ret = epg_manager_add_code (manager, code_str, now + 10, &error);
  g_assert_error (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_CODE_ALREADY_USED);
  g_assert_false (ret);

  g_assert_cmpint (expiry_after_code, ==, epg_manager_get_expiry_time (manager));
}

static void
test_manager_error_rate_limit (Fixture *fixture,
                               gconstpointer data)
{
  g_autoptr(EpgManager) manager = manager_new (fixture);
  g_autoptr(GError) error = NULL;
  g_autofree gchar *code_str = NULL;
  /* Avoid hardcoding the actual limit, but surely it should be less than this */
  int attempts_remaining = 100;
  guint64 expiry, now, expiry_after_code, rate_limit_end_time;
  gboolean ret;

  expiry = epg_manager_get_expiry_time (manager);
  now = expiry + 5;

  /* Should be able to add many new, valid codes in quick succession */
  while (fixture->next_counter < EPC_MINCOUNTER + 64)
    {
      g_clear_pointer (&code_str, g_free);
      code_str = get_next_code (fixture);
      ret = epg_manager_add_code (manager, code_str, now, &error);
      g_assert_no_error (error);
      g_assert_true (ret);
    }

  expiry_after_code = epg_manager_get_expiry_time (manager);
  g_assert_cmpuint (now + (64 * 5), ==, expiry_after_code);

  /* Trying to reuse a code should fail for a while with EPG_MANAGER_ERROR_CODE_ALREADY_USED ... */
  do
    {
      g_clear_error (&error);
      ret = epg_manager_add_code (manager, code_str, now, &error);
      g_assert_false (ret);
  }
  while (g_error_matches (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_CODE_ALREADY_USED) &&
         --attempts_remaining > 0 /* Prevent an infinite loop if rate-limiting doesn't work */);

  g_assert_error (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_TOO_MANY_ATTEMPTS);

  /* Generate a new valid code */
  g_clear_pointer (&code_str, g_free);
  code_str = get_next_code (fixture);

  /* The code is valid, but trying to use it should still fail because we're now rate-limited */
  g_clear_error (&error);
  ret = epg_manager_add_code (manager, code_str, now, &error);
  g_assert_error (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_TOO_MANY_ATTEMPTS);
  g_assert_false (ret);

  rate_limit_end_time = epg_manager_get_rate_limit_end_time (manager);
  /* Some time in the future */
  g_assert_cmpuint (rate_limit_end_time, >, now);
  /* And certainly longer than the n lots of 5-second codes we added */
  g_assert_cmpuint (rate_limit_end_time, >, expiry_after_code);

  now = rate_limit_end_time + 1;
  g_clear_error (&error);
  ret = epg_manager_add_code (manager, code_str, now, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  g_assert_cmpuint (now + 5, ==, epg_manager_get_expiry_time (manager));
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  const gpointer expiry_time_offset =
     GSIZE_TO_POINTER (G_STRUCT_OFFSET (Fixture, expiry_time_path));
  const gpointer used_codes_offset =
     GSIZE_TO_POINTER (G_STRUCT_OFFSET (Fixture, used_codes_path));

#define T(path, func, data) \
  g_test_add (path, Fixture, data, setup, func, teardown)
  T ("/manager/load-empty", test_manager_load_empty, NULL);
  T ("/manager/load-error/malformed/expiry-time", test_manager_load_error_malformed, expiry_time_offset);
  T ("/manager/load-error/malformed/used-codes", test_manager_load_error_malformed, used_codes_offset);
  T ("/manager/load-error/unreadable/expiry-time", test_manager_load_error_unreadable, expiry_time_offset);
  T ("/manager/load-error/unreadable/used-codes", test_manager_load_error_unreadable, used_codes_offset);
  T ("/manager/save-error/no-codes-applied", test_manager_save_error, GINT_TO_POINTER (FALSE));
  T ("/manager/save-error/codes-applied", test_manager_save_error, GINT_TO_POINTER (TRUE));
  T ("/manager/add-save-reload", test_manager_add_save_reload, NULL);
  T ("/manager/error/malformed", test_manager_error_malformed, NULL);
  T ("/manager/error/reused", test_manager_error_reused, NULL);
  T ("/manager/error/rate-limit", test_manager_error_rate_limit, NULL);
#undef T

  return g_test_run ();
}
