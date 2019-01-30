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
#include <libeos-payg/fake-clock.h>
#include <libeos-payg/errors.h>
#include <libeos-payg/manager.h>
#include <libeos-payg-codes/codes.h>
#include <locale.h>

static const char KEY[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

typedef struct _Fixture {
  gchar *tmp_path;
  GFile *tmp_dir;

  gchar *clock_time_path;
  gchar *expiry_seconds_path;
  gchar *expiry_time_path; /* for backwards compat only */
  gchar *used_codes_path;

  GBytes *key;
  gchar *key_path;
  GFile *key_file;

  EpcCounter next_counter;

  EpgProvider *provider;
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

  fixture->clock_time_path = g_build_filename (fixture->tmp_path, "clock-time", NULL);
  fixture->expiry_seconds_path = g_build_filename (fixture->tmp_path, "expiry-seconds", NULL);
  fixture->expiry_time_path = g_build_filename (fixture->tmp_path, "expiry-time", NULL);
  fixture->used_codes_path = g_build_filename (fixture->tmp_path, "used-codes", NULL);

  fixture->key = g_bytes_new_static (KEY, sizeof (KEY) - 1);
  fixture->next_counter = EPC_MINCOUNTER;

  /* This would not normally live in the same directory, but the code doesn't care. */
  fixture->key_path = g_build_filename (fixture->tmp_path, "key", NULL);
  g_file_set_contents (fixture->key_path, KEY, -1, &error);
  g_assert_no_error (error);
  fixture->key_file = g_file_new_for_path (fixture->key_path);
  g_assert_nonnull (fixture->key_file);
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
async_cb (GObject      *source,
          GAsyncResult *result,
          gpointer      data)
{
  GAsyncResult **result_out = data;

  g_assert_null (*result_out);
  *result_out = g_object_ref (result);
}

static gboolean
shutdown (Fixture *fixture,
          GError **error)
{
  gboolean ret;
  g_autoptr(GAsyncResult) result = NULL;

  if (fixture->provider == NULL)
    return TRUE;

  epg_provider_shutdown_async (fixture->provider, NULL, async_cb, &result);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  ret = epg_provider_shutdown_finish (fixture->provider, result, error);
  g_clear_object (&fixture->provider);
  return ret;
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
  gboolean ret;
  g_autoptr(GError) local_error = NULL;

  ret = shutdown (fixture, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);

  g_clear_pointer (&fixture->clock_time_path, remove_and_free_path);
  g_clear_pointer (&fixture->expiry_seconds_path, remove_and_free_path);
  g_clear_pointer (&fixture->expiry_time_path, remove_and_free_path);
  g_clear_pointer (&fixture->used_codes_path, remove_and_free_path);
  g_clear_pointer (&fixture->key_path, remove_and_free_path);
  g_clear_pointer (&fixture->tmp_path, remove_and_free_path);

  g_clear_object (&fixture->tmp_dir);
  g_clear_pointer (&fixture->key, g_bytes_unref);
}

static void
manager_new_failable (Fixture *fixture,
                      gboolean enabled,
                      GError **error)
{
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(EpgFakeClock) clock = NULL;
  g_assert_null (fixture->provider);

  clock = epg_fake_clock_new (-1, -1);
  epg_manager_new (enabled, fixture->key_file, fixture->tmp_dir,
                   EPG_CLOCK (clock), NULL, async_cb, &result);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  fixture->provider = epg_manager_new_finish (result, error);
}

static void
manager_new (Fixture *fixture)
{
  g_autoptr(GError) error = NULL;

  manager_new_failable (fixture, TRUE, &error);
  g_assert_no_error (error);
  g_assert_nonnull (fixture->provider);
}

typedef enum {
    TEST_MANAGER_DISABLED_REMOVE_KEY = 1 << 0,
    TEST_MANAGER_DISABLED_SET_ENABLED_PROPERTY = 1 << 1,
} TestManagerDisabledFlags;

/* test_manager_disabled:
 * @data: GUINT_TO_POINTER(TestManagerDisabledFlags)
 *
 * Tests the behaviour of EpgManager when the key is missing or the enabled
 * construct-time property is set to FALSE.
 */
static void
test_manager_disabled (Fixture *fixture,
                       gconstpointer data)
{
  guint test_flags = GPOINTER_TO_UINT (data);
  gboolean initially_enabled = !!(test_flags & TEST_MANAGER_DISABLED_SET_ENABLED_PROPERTY);
  gboolean ret;

  if (test_flags & TEST_MANAGER_DISABLED_REMOVE_KEY)
    remove_path (fixture->key_path);

  g_autoptr(GError) error = NULL;

  manager_new_failable (fixture, initially_enabled, &error);
  g_assert_no_error (error);
  g_assert_nonnull (fixture->provider);

  g_assert_false (epg_provider_get_enabled (fixture->provider));
  g_assert_cmpuint (0, ==, epg_provider_get_expiry_time (fixture->provider));
  g_assert_cmpuint (0, ==, epg_provider_get_rate_limit_end_time (fixture->provider));

  ret = epg_provider_add_code (fixture->provider, "00000000", &error);
  g_assert_error (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_DISABLED);
  g_assert_false (ret);
  g_clear_error (&error);

  ret = epg_provider_clear_code (fixture->provider, &error);
  g_assert_error (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_DISABLED);
  g_assert_false (ret);
  g_clear_error (&error);
}

/* test_manager_load_empty:
 *
 * Tests the default behaviour when the key is present but the state directory
 * is empty.
 */
static void
test_manager_load_empty (Fixture *fixture,
                         gconstpointer data)
{
  guint64 expiry, current_time;
  EpgClock *clock;

  manager_new (fixture);
  clock = epg_provider_get_clock (fixture->provider);
  current_time = epg_clock_get_time (clock);

  /* Default expiry time is "now". */
  expiry = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpuint (expiry, <=, current_time);
}

static GRegex *
get_code_format (Fixture *fixture)
{
  manager_new (fixture);

  const gchar *code_format = epg_provider_get_code_format (fixture->provider);
  g_autoptr(GRegex) regex = NULL;
  g_autoptr(GError) local_error = NULL;

  regex = g_regex_new (code_format,
                       G_REGEX_DOLLAR_ENDONLY,
                       G_REGEX_MATCH_PARTIAL,
                       &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (regex);

  return g_steal_pointer (&regex);
}

/* test_manager_code_format_matches:
 * @data: const gchar *
 *
 * Tests that the :code-format regexp matches @data in its entirety.
 */
static void
test_manager_code_format_matches (Fixture *fixture,
                                  gconstpointer data)
{
  /* Hi LISP enthusiasts */
  const gchar *code = data;
  g_autoptr(GRegex) regex = get_code_format (fixture);
  g_assert_true (g_regex_match (regex, code, 0, NULL));
}

/* test_manager_code_format_matches_partial:
 * @data: const gchar *
 *
 * Tests that the :code-format regexp partially matches @data.
 */
static void
test_manager_code_format_matches_partial (Fixture *fixture,
                                          gconstpointer data)
{
  const gchar *code = data;
  g_autoptr(GRegex) regex = get_code_format (fixture);
  gboolean ret;
  g_autoptr(GMatchInfo) match_info = NULL;

  ret = g_regex_match (regex, code, 0, &match_info);
  g_assert_false (ret);
  g_assert_true (g_match_info_is_partial_match (match_info));
}

/* test_manager_code_format_rejects:
 * @data: const gchar *
 *
 * Tests that the :code-format regexp doesn't match @data, fully or partially.
 */
static void
test_manager_code_format_rejects (Fixture *fixture,
                                  gconstpointer data)
{
  const gchar *code = data;
  g_autoptr(GRegex) regex = get_code_format (fixture);
  gboolean ret;
  g_autoptr(GMatchInfo) match_info = NULL;

  ret = g_regex_match (regex, code, 0, &match_info);
  g_assert_false (ret);
  g_assert_false (g_match_info_is_partial_match (match_info));
}

static void
write_valid_clock_time_file (Fixture *fixture)
{
  guint64 current_time = g_get_real_time () / G_USEC_PER_SEC;
  gboolean ret;
  g_autoptr(GError) error = NULL;

  ret = g_file_set_contents (fixture->clock_time_path,
                             (char *)&current_time,
                             sizeof (current_time),
                             &error);
  g_assert_no_error (error);
  g_assert_true (ret);
}

/* test_manager_load_error_malformed:
 * @data: offset within Fixture of the path to a state file, which will be
 * initialized to an invalid value
 *
 * Tests the behaviour when a state file contains invalid data, by populating
 * one with invalid data before constructing the EpgManager.
 */
static void
test_manager_load_error_malformed (Fixture *fixture,
                                   gconstpointer data)
{
  gsize path_offset = GPOINTER_TO_SIZE (data);
  const char *path_p = G_STRUCT_MEMBER (const char *, fixture, path_offset);
  gboolean ret;
  g_autoptr(GError) error = NULL;

  /* Set contents of state file to something illegal. It happens that a single
   * byte is illegal for all state files.
   */
  ret = g_file_set_contents (path_p, "X", 1, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  /* expiry-seconds is a special case because it is only read if clock-time exists */
  if (g_strcmp0 (path_p, fixture->expiry_seconds_path) == 0)
    write_valid_clock_time_file (fixture);

  manager_new_failable (fixture, TRUE, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  if (g_strstr_len (error->message, -1, path_p) == NULL)
    g_error ("Error message '%s' does not contain state file path '%s'",
             error->message, path_p);
  g_assert_null (fixture->provider);
  g_clear_error (&error);

  /* The offending state file should have been cleaned up so subsequent
   * attempts work:
   */
  manager_new_failable (fixture, TRUE, &error);
  g_assert_no_error (error);
  g_assert_nonnull (fixture->provider);
}

/* test_manager_load_error_unreadable:
 * @data: offset within Fixture of the path to a state file, which will be
 * populated with something which cannot be read (namely a directory)
 *
 * Tests the behaviour when a state file cannot be read.
 */
static void
test_manager_load_error_unreadable (Fixture *fixture,
                                    gconstpointer data)
{
  gsize path_offset = GPOINTER_TO_SIZE (data);
  const char *path_p = G_STRUCT_MEMBER (const char *, fixture, path_offset);
  g_autoptr(GError) error = NULL;

  /* Create a directory where the state file should be. On sensible platforms
   * like Linux, you can't open() and read() a directory.
   */
  if (g_mkdir (path_p, 0755) != 0)
    g_error ("Couldn't create directory at '%s': %s",
             path_p, g_strerror (errno));

  manager_new_failable (fixture, TRUE, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY);
  if (g_strstr_len (error->message, -1, path_p) == NULL)
    g_error ("Error message '%s' does not contain state file path '%s'",
             error->message, path_p);
  g_assert_null (fixture->provider);
  g_clear_error (&error);

  /* FIXME: clean away the offending illegible 'file'. */
#if 0
  provider = manager_new_failable (fixture, TRUE, &error);
  g_assert_no_error (error);
  g_assert_nonnull (fixture->provider);
  test_manager_load_empty (fixture, NULL);
#endif
}

/* test_manager_save_error:
 * @data: if non-zero, try to apply a code
 *
 * Tests what happens when the manager can't write back its state to disk.
 * This is made to happen by removing the state directory behind its back.
 * We test both the case where writing state on shutdown fails, and when
 * writing state after applying a code.
 */
static void
test_manager_save_error (Fixture *fixture,
                         gconstpointer data)
{
  gboolean apply_code = GPOINTER_TO_INT (data);
  manager_new (fixture);
  g_autoptr(GError) error = NULL;
  gboolean ret;

  /* Sabotage any future attempts to save state. */
  remove_path (fixture->key_path);
  remove_path (fixture->clock_time_path);
  remove_path (fixture->expiry_seconds_path);
  remove_path (fixture->tmp_path);

  if (apply_code)
    {
      g_autofree gchar *code_str = get_next_code (fixture);

      /* There's an internal call to epg_manager_save_state_async() here which
       * will later fail. Arguably this should make applying the code fail as
       * well.
       */
      g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                             "*save_state failed:*");
      ret = epg_provider_add_code (fixture->provider, code_str, &error);
      g_assert_no_error (error);
      g_assert_true (ret);
    }

  g_autoptr(GAsyncResult) result = NULL;

  ret = shutdown (fixture, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ret);
  g_test_assert_expected_messages ();
}

/* test_manager_extend_expiry:
 *
 * Tests that applying a code extends the expiry time, regardless of whether
 * the current time is before or after the current expiry time.
 */
static void
test_manager_extend_expiry (Fixture *fixture,
                            gconstpointer data)
{
  manager_new (fixture);
  g_autofree gchar *code_str = NULL;
  g_autoptr(GError) error = NULL;
  guint64 expiry_before_code, now, expiry_after_code;
  gboolean ret;
  EpgFakeClock *clock;

  /* First test with the current time after the expiration time */
  clock = (EpgFakeClock *)epg_provider_get_clock (fixture->provider);
  expiry_before_code = epg_provider_get_expiry_time (fixture->provider);
  now = expiry_before_code + 5;
  epg_fake_clock_set_time (clock, now);
  code_str = get_next_code (fixture);
  ret = epg_provider_add_code (fixture->provider, code_str, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  expiry_after_code = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpint (now + 5, ==, expiry_after_code);

  /* Now test with the current time before the expiration time */
  expiry_before_code = expiry_after_code;
  now = epg_clock_get_time (EPG_CLOCK (clock));
  g_assert_cmpint (now, <, expiry_before_code);
  code_str = get_next_code (fixture);
  ret = epg_provider_add_code (fixture->provider, code_str, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  expiry_after_code = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpint (expiry_before_code + 5, ==, expiry_after_code);
}

/* test_manager_add_save_reload:
 *
 * Tests that applying a code (to extend the expiry time), tearing down the
 * EpgManager, and loading it up again restores the same expiry time.
 */
static void
test_manager_add_save_reload (Fixture *fixture,
                              gconstpointer data)
{
  manager_new (fixture);
  g_autofree gchar *code_str = NULL;
  g_autoptr(GError) error = NULL;
  guint64 expiry_before_code, expiry_after_code, expiry_after_reload;
  gboolean ret;

  expiry_before_code = epg_provider_get_expiry_time (fixture->provider);

  code_str = get_next_code (fixture);

  ret = epg_provider_add_code (fixture->provider, code_str, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  expiry_after_code = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpint (expiry_before_code + 5, ==, expiry_after_code);

  /* Adding the code kicks off an async save, but we can't be sure it will have
   * finished before we re-load the files. Force the issue here.
   */
  ret = shutdown (fixture, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  manager_new (fixture);
  expiry_after_reload = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpuint (expiry_before_code + 5, ==, expiry_after_reload);
}

/* test_manager_add_infinite_code:
 *
 * Tests that applying an infinite code sets the expiry time to infinitely far
 * in the future, and that this survives both a restart and adding a
 * non-infinite code afterwards.
 */
static void
test_manager_add_infinite_code (Fixture      *fixture,
                                gconstpointer data)
{
  manager_new (fixture);
  EpcCode code;
  g_autofree gchar *code_str = NULL;
  g_autoptr(GError) error = NULL;
  guint64 expiry_before_code, expiry_after_code, expiry_after_reload;
  gboolean ret;

  expiry_before_code = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpuint (G_MAXUINT64, !=, expiry_before_code);

  /* Apply an infinite code. The expiry time should be updated to be infinitely
   * far away. (TODO: or, set Enabled=False in this case?)
   */
  code = epc_calculate_code (EPC_PERIOD_INFINITE, EPC_MINCOUNTER, fixture->key, &error);
  g_assert_no_error (error);
  code_str = epc_format_code (code);

  ret = epg_provider_add_code (fixture->provider, code_str, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  expiry_after_code = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpint (G_MAXUINT64, ==, expiry_after_code);

  /* Apply a 5-second code. It should have no effect: the expiry time should be
   * clamped to infinitely far away.
   */
  g_clear_pointer (&code_str, g_free);
  code_str = get_next_code (fixture);
  ret = epg_provider_add_code (fixture->provider, code_str, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  expiry_after_code = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpint (G_MAXUINT64, ==, expiry_after_code);

  /* Tear down and restart. */
  ret = shutdown (fixture, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  manager_new (fixture);
  expiry_after_reload = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpuint (G_MAXUINT64, ==, expiry_after_reload);
}

/* test_manager_error_malformed:
 *
 * Tests that entering a totally malformed code does not extend the expiry
 * time.
 */
static void
test_manager_error_malformed (Fixture *fixture,
                              gconstpointer data)
{
  manager_new (fixture);
  g_autoptr(GError) error = NULL;
  guint64 expiry, expiry_after_code;
  gboolean ret;

  expiry = epg_provider_get_expiry_time (fixture->provider);

  /* Just use an obviously-wrong code. The nuances are tested in libeos-payg-codes. */
  ret = epg_provider_add_code (fixture->provider, "abcdefgh", &error);
  g_assert_error (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_INVALID_CODE);
  g_assert_false (ret);

  expiry_after_code = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpint (expiry, ==, expiry_after_code);
}

/* test_manager_error_reused:
 *
 * Tests that entering a code twice causes it to be rejected the second time,
 * without extending the expiry time.
 */
static void
test_manager_error_reused (Fixture *fixture,
                           gconstpointer data)
{
  manager_new (fixture);
  g_autofree gchar *code_str = get_next_code (fixture);
  g_autoptr(GError) error = NULL;
  guint64 expiry_before_code, expiry_after_code;
  gboolean ret;
  EpgFakeClock *clock;

  expiry_before_code = epg_provider_get_expiry_time (fixture->provider);

  ret = epg_provider_add_code (fixture->provider, code_str, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  expiry_after_code = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpint (expiry_before_code + 5, ==, expiry_after_code);

  /* Test that the code is rejected before the last application expires */
  ret = epg_provider_add_code (fixture->provider, code_str, &error);
  g_assert_error (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_CODE_ALREADY_USED);
  g_assert_false (ret);

  g_assert_cmpint (expiry_after_code, ==, epg_provider_get_expiry_time (fixture->provider));

  /* Test that the code is rejected after the last application expired */
  clock = (EpgFakeClock *)epg_provider_get_clock (fixture->provider);
  epg_fake_clock_set_time (clock, expiry_after_code + 5);

  g_clear_error (&error);
  ret = epg_provider_add_code (fixture->provider, code_str, &error);
  g_assert_error (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_CODE_ALREADY_USED);
  g_assert_false (ret);

  g_assert_cmpint (expiry_after_code, ==, epg_provider_get_expiry_time (fixture->provider));
}

/* test_manager_error_rate_limit:
 *
 * Tests that entering a large number of valid codes in quick succession is
 * accepted, but entering a large number of invalid codes in quick succession
 * causes entering any code (valid or invalid) to be rate-limited for a period
 * of time.
 */
static void
test_manager_error_rate_limit (Fixture *fixture,
                               gconstpointer data)
{
  manager_new (fixture);

  g_autoptr(GError) error = NULL;
  g_autofree gchar *code_str = NULL;
  /* Avoid hardcoding the actual limit, but surely it should be less than this */
  int attempts_remaining = 100;
  guint64 expiry, now, expiry_after_code, rate_limit_end_time;
  gboolean ret;
  EpgFakeClock *clock;

  expiry = epg_provider_get_expiry_time (fixture->provider);

  /* Should be able to add many new, valid codes in quick succession */
  while (fixture->next_counter < EPC_MINCOUNTER + 64)
    {
      g_clear_pointer (&code_str, g_free);
      code_str = get_next_code (fixture);
      ret = epg_provider_add_code (fixture->provider, code_str, &error);
      g_assert_no_error (error);
      g_assert_true (ret);
    }

  expiry_after_code = epg_provider_get_expiry_time (fixture->provider);
  g_assert_cmpuint (expiry + (64 * 5), ==, expiry_after_code);

  /* Trying to reuse a code should fail for a while with EPG_MANAGER_ERROR_CODE_ALREADY_USED ... */
  do
    {
      g_clear_error (&error);
      ret = epg_provider_add_code (fixture->provider, code_str, &error);
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
  ret = epg_provider_add_code (fixture->provider, code_str, &error);
  g_assert_error (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_TOO_MANY_ATTEMPTS);
  g_assert_false (ret);

  clock = (EpgFakeClock *)epg_provider_get_clock (fixture->provider);
  now = epg_clock_get_time (EPG_CLOCK (clock));
  rate_limit_end_time = epg_provider_get_rate_limit_end_time (fixture->provider);
  /* Some time in the future */
  g_assert_cmpuint (rate_limit_end_time, >, now);
  /* And certainly longer than the n lots of 5-second codes we added */
  g_assert_cmpuint (rate_limit_end_time, >, expiry_after_code);

  now = rate_limit_end_time + 1;
  epg_fake_clock_set_time (clock, now);
  g_clear_error (&error);
  ret = epg_provider_add_code (fixture->provider, code_str, &error);
  g_assert_no_error (error);
  g_assert_true (ret);

  g_assert_cmpuint (now + 5, ==, epg_provider_get_expiry_time (fixture->provider));
}

static void
add_many (const char  *path_base,
          void       (*func) (Fixture *, gconstpointer),
          const char **data)
{
  const char **datum;

  for (datum = data; *datum != NULL; datum++)
    {
      g_autofree char *escaped = g_uri_escape_string (*datum, NULL, TRUE);
      g_autofree char *path = g_strconcat (path_base,
                                           "/",
                                           escaped[0] != 0 ? escaped : "empty",
                                           NULL);
      g_test_add (path, Fixture, *datum, setup, func, teardown);
    }
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  const gpointer clock_time_offset =
     GSIZE_TO_POINTER (G_STRUCT_OFFSET (Fixture, clock_time_path));
  const gpointer expiry_seconds_offset =
     GSIZE_TO_POINTER (G_STRUCT_OFFSET (Fixture, expiry_seconds_path));
  const gpointer expiry_time_offset =
     GSIZE_TO_POINTER (G_STRUCT_OFFSET (Fixture, expiry_time_path));
  const gpointer used_codes_offset =
     GSIZE_TO_POINTER (G_STRUCT_OFFSET (Fixture, used_codes_path));

#define T(path, func, data) \
  g_test_add (path, Fixture, data, setup, func, teardown)
  T ("/manager/disabled/key-present", test_manager_disabled,
     GUINT_TO_POINTER (0));
  T ("/manager/disabled/key-missing", test_manager_disabled,
     GUINT_TO_POINTER (TEST_MANAGER_DISABLED_REMOVE_KEY));
  T ("/manager/enabled/key-missing", test_manager_disabled,
     GUINT_TO_POINTER (TEST_MANAGER_DISABLED_REMOVE_KEY |
                       TEST_MANAGER_DISABLED_SET_ENABLED_PROPERTY));
  T ("/manager/load-empty", test_manager_load_empty, NULL);

  const char *full_matches[] = { "00000000", "12345678", NULL };
  add_many ("/manager/code-format/matches",
            test_manager_code_format_matches,
            full_matches);

  const char *partial_matches[] = { "1", "12", "1234567", NULL };
  add_many ("/manager/code-format/partial",
            test_manager_code_format_matches_partial,
            partial_matches);

  const char *non_matches[] = {
      "\n", "a", "1a", "123\n", "a123",
      /* '6' in various non-ASCII forms */
      "६", "೬", "𝟨", "六",
      /* Sadly, the empty string is never a partial match for any regular
       * expression.
       */
      "",
      NULL
  };
  add_many ("/manager/code-format/rejects",
            test_manager_code_format_rejects,
            non_matches);
  T ("/manager/load-error/malformed/clock-time", test_manager_load_error_malformed, clock_time_offset);
  T ("/manager/load-error/malformed/expiry-seconds", test_manager_load_error_malformed, expiry_seconds_offset);
  T ("/manager/load-error/malformed/expiry-time", test_manager_load_error_malformed, expiry_time_offset);
  T ("/manager/load-error/malformed/used-codes", test_manager_load_error_malformed, used_codes_offset);
  T ("/manager/load-error/unreadable/expiry-time", test_manager_load_error_unreadable, expiry_time_offset);
  T ("/manager/load-error/unreadable/used-codes", test_manager_load_error_unreadable, used_codes_offset);
  T ("/manager/save-error/no-codes-applied", test_manager_save_error, GINT_TO_POINTER (FALSE));
  T ("/manager/save-error/codes-applied", test_manager_save_error, GINT_TO_POINTER (TRUE));
  T ("/manager/extend-expiry", test_manager_extend_expiry, NULL);
  T ("/manager/add-save-reload", test_manager_add_save_reload, NULL);
  T ("/manager/add-infinite", test_manager_add_infinite_code, NULL);
  T ("/manager/error/malformed", test_manager_error_malformed, NULL);
  T ("/manager/error/reused", test_manager_error_reused, NULL);
  T ("/manager/error/rate-limit", test_manager_error_rate_limit, NULL);
#undef T

  return g_test_run ();
}
