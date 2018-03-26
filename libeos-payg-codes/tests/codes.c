/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2018 Endless Mobile, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include <glib.h>
#include <libeos-payg-codes/codes.h>
#include <locale.h>
#include <string.h>


/* Copied from the implementation. If these values change, the tests may need
 * to be reworked. */
#define COUNTER_WIDTH_BITS 8
#define PERIOD_WIDTH_BITS 5
#define SIGN_WIDTH_BITS 13
#define CODE_VALUE_WIDTH_BITS (COUNTER_WIDTH_BITS + PERIOD_WIDTH_BITS + SIGN_WIDTH_BITS)
#define CODE_STR_WIDTH_DIGITS 8
#define KEY_LENGTH_BYTES_MINIMUM 64


/* Test epc_period_validate() works correctly for valid and invalid periods. */
static void
test_codes_period_validation (void)
{
  g_autoptr(GError) local_error = NULL;

  /* Valid values. */
  g_assert_true (epc_period_validate (EPC_PERIOD_5_SECONDS, &local_error));
  g_assert_no_error (local_error);

  g_assert_true (epc_period_validate (EPC_PERIOD_INFINITE, &local_error));
  g_assert_no_error (local_error);

  /* This value is currently unassigned. */
  g_assert_false (epc_period_validate (30, &local_error));
  g_assert_error (local_error, EPC_CODE_ERROR, EPC_CODE_ERROR_INVALID_PERIOD);
  g_clear_error (&local_error);

  /* This value is too big. */
  g_assert_false (epc_period_validate (32, &local_error));
  g_assert_error (local_error, EPC_CODE_ERROR, EPC_CODE_ERROR_INVALID_PERIOD);
  g_clear_error (&local_error);
}

/* Test epc_code_validate() works correctly for valid and invalid codes. */
static void
test_codes_code_validation (void)
{
  g_autoptr(GError) local_error = NULL;

  /* Valid values. */
  g_assert_true (epc_code_validate (0, &local_error));
  g_assert_no_error (local_error);

  g_assert_true (epc_code_validate ((1 << CODE_VALUE_WIDTH_BITS) - 1, &local_error));
  g_assert_no_error (local_error);

  /* This value is too big. */
  g_assert_false (epc_code_validate (1 << CODE_VALUE_WIDTH_BITS, &local_error));
  g_assert_error (local_error, EPC_CODE_ERROR, EPC_CODE_ERROR_INVALID_CODE);
  g_clear_error (&local_error);
}

/* Test round-trip calls between epc_calculate_code() and epc_verify_code(),
 * using only valid sets of inputs. The generated codes are compared against
 * known-good values to make sure they don’t change in future. */
static void
test_codes_calculate_round_trip (void)
{
  const gchar *key1_data =
      "hello this has to be at least 64 bytes long so I am going to keep on typing.";
  g_autoptr(GBytes) key1 = g_bytes_new_static (key1_data, strlen (key1_data));
  const struct
    {
      EpcPeriod period;
      EpcCounter counter;
      GBytes *key;
      EpcCode expected_code;
    }
  vectors[] =
    {
      { EPC_PERIOD_5_SECONDS, 0, key1, 6996 },
      { EPC_PERIOD_5_SECONDS, 1, key1, 13963 },
      { EPC_PERIOD_5_SECONDS, 2, key1, 23105 },
      { EPC_PERIOD_5_SECONDS, 3, key1, 32552 },
      { EPC_PERIOD_5_SECONDS, 4, key1, 36698 },
      { EPC_PERIOD_5_SECONDS, 5, key1, 45721 },
      { EPC_PERIOD_5_SECONDS, 6, key1, 50472 },
      { EPC_PERIOD_5_SECONDS, 7, key1, 63462 },
      { EPC_PERIOD_1_MINUTE, 100, key1, 2919004 },
      { EPC_PERIOD_5_MINUTES, 51, key1, 4614445 },
      { EPC_PERIOD_1_HOUR, 12, key1, 6395742 },
      { EPC_PERIOD_1_DAY, 13, key1, 8495508 },
      { EPC_PERIOD_2_DAYS, 46, key1, 10866382 },
      { EPC_PERIOD_3_DAYS, 31, key1, 12838372 },
      { EPC_PERIOD_4_DAYS, 0, key1, 14684372 },
      { EPC_PERIOD_5_DAYS, 8, key1, 16848988 },
      { EPC_PERIOD_6_DAYS, 65, key1, 19411925 },
      { EPC_PERIOD_7_DAYS, 250, key1, 23027316 },
      { EPC_PERIOD_8_DAYS, 46, key1, 23453556 },
      { EPC_PERIOD_9_DAYS, 2, key1, 25186550 },
      { EPC_PERIOD_10_DAYS, 89, key1, 27992206 },
      { EPC_PERIOD_11_DAYS, 34, key1, 29645509 },
      { EPC_PERIOD_12_DAYS, 46, key1, 31840181 },
      { EPC_PERIOD_13_DAYS, 76, key1, 34178837 },
      { EPC_PERIOD_14_DAYS, 66, key1, 36195098 },
      { EPC_PERIOD_30_DAYS, 70, key1, 38323642 },
      { EPC_PERIOD_60_DAYS, 64, key1, 40373693 },
      { EPC_PERIOD_90_DAYS, 95, key1, 42722623 },
      { EPC_PERIOD_120_DAYS, 43, key1, 44396753 },
      { EPC_PERIOD_365_DAYS, 76, key1, 46761597 },
      { EPC_PERIOD_INFINITE, 32, key1, 65277943 },
    };

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GError) local_error = NULL;

      g_test_message ("Vector %" G_GSIZE_FORMAT ": period: %u, counter: %u, key: %p",
                      i, (guint) vectors[i].period,
                      (guint) vectors[i].counter, vectors[i].key);

      EpcCode actual_code = epc_calculate_code (vectors[i].period,
                                                vectors[i].counter,
                                                vectors[i].key,
                                                &local_error);

      g_assert_cmpuint (actual_code, ==, vectors[i].expected_code);
      g_assert_no_error (local_error);

      EpcPeriod actual_period;
      EpcCounter actual_counter;

      gboolean is_valid = epc_verify_code (actual_code, vectors[i].key,
                                           &actual_period, &actual_counter,
                                           &local_error);
      g_assert_no_error (local_error);
      g_assert_true (is_valid);

      g_assert_cmpuint (actual_period, ==, vectors[i].period);
      g_assert_cmpuint (actual_counter, ==, vectors[i].counter);
    }
}

/* Test that calling epc_calculate_code() on some invalid period/counter/key
 * combinations results in an error. */
static void
test_codes_calculate_error (void)
{
  const gchar *key1_data =
      "hello this has to be at least 64 bytes long so I am going to keep on typing.";
  g_autoptr(GBytes) key1 = g_bytes_new_static (key1_data, strlen (key1_data));
  g_autoptr(GBytes) invalid_key = g_bytes_new_static ("", 0);
  const struct
    {
      EpcPeriod period;
      EpcCounter counter;
      GBytes *key;
    }
  vectors[] =
    {
      { 30  /* invalid */, 1, key1 },
      { EPC_PERIOD_5_SECONDS, 0, invalid_key },
    };

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GError) local_error = NULL;

      g_test_message ("Vector %" G_GSIZE_FORMAT ": period: %u, counter: %u, key: %p",
                      i, (guint) vectors[i].period,
                      (guint) vectors[i].counter, vectors[i].key);

      EpcCode actual_code = epc_calculate_code (vectors[i].period,
                                                vectors[i].counter,
                                                vectors[i].key,
                                                &local_error);

      g_assert_nonnull (local_error);
      g_assert_cmpuint (actual_code, ==, 0);

      g_clear_error (&local_error);
    }
}

/* Test that calling epc_verify_code() on some invalid codes results in an
 * error. */
static void
test_codes_verify_error (void)
{
  const gchar *key1_data = "hello this has to be at least 64 bytes long so I am going to keep on typing.";
  g_autoptr(GBytes) key1 = g_bytes_new_static (key1_data, strlen (key1_data));
  g_autoptr(GBytes) invalid_key = g_bytes_new_static ("", 0);
  const struct
    {
      EpcCode code;
      GBytes *key;
    }
  vectors[] =
    {
      { 1 << CODE_VALUE_WIDTH_BITS  /* too big */, key1 },
      { 15, invalid_key },
      { 30 << (COUNTER_WIDTH_BITS + SIGN_WIDTH_BITS)  /* invalid period */, key1 },
      { EPC_PERIOD_1_DAY << (COUNTER_WIDTH_BITS + SIGN_WIDTH_BITS)  /* valid period; invalid signature */, key1 },
    };

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GError) local_error = NULL;

      g_test_message ("Code: %u, key: %p", vectors[i].code, vectors[i].key);

      g_assert_false (epc_verify_code (vectors[i].code, vectors[i].key,
                                       NULL, NULL, &local_error));
      g_assert_nonnull (local_error);
    }
}

/* Test that formatting codes can be round-tripped and parsed again. */
static void
test_codes_format_round_trip (void)
{
  const struct
    {
      EpcCode code;
      const gchar *code_str;
    }
  vectors[] =
    {
      { 0, "00000000" },
      { 123, "00000123" },
      { 12345678, "12345678" },
    };

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GError) local_error = NULL;

      g_test_message ("Vector %" G_GSIZE_FORMAT ": %u, %s",
                      i, vectors[i].code, vectors[i].code_str);

      g_autofree gchar *actual_code_str = epc_format_code (vectors[i].code);
      g_assert_cmpstr (actual_code_str, ==, vectors[i].code_str);

      EpcCode actual_code;
      gboolean parse_success = epc_parse_code (actual_code_str, &actual_code, &local_error);
      g_assert_cmpuint (actual_code, ==, vectors[i].code);
      g_assert_no_error (local_error);
      g_assert_true (parse_success);
    }
}

/* Test that calling epc_parse_code() on some invalid strings results in an
 * error. */
static void
test_codes_parse_error (void)
{
  const gchar *vectors[] =
    {
      "",
      "some words",
      "1234567",
      "123456789",
      "abcdefgh",
      "99999999",
    };

  for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++)
    {
      g_autoptr(GError) local_error = NULL;

      g_test_message ("Code: %s", vectors[i]);

      EpcCode code;
      gboolean success = epc_parse_code (vectors[i], &code, &local_error);
      g_assert_error (local_error, EPC_CODE_ERROR, EPC_CODE_ERROR_INVALID_CODE);
      g_assert_false (success);
    }
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/codes/period-validation", test_codes_period_validation);
  g_test_add_func ("/codes/code-validation", test_codes_code_validation);
  g_test_add_func ("/codes/calculate/round-trip", test_codes_calculate_round_trip);
  g_test_add_func ("/codes/calculate/error", test_codes_calculate_error);
  g_test_add_func ("/codes/verify/error", test_codes_verify_error);
  g_test_add_func ("/codes/format/round-trip", test_codes_format_round_trip);
  g_test_add_func ("/codes/parse/error", test_codes_parse_error);

  return g_test_run ();
}
