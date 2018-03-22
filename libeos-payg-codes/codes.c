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
#include <glib/gi18n-lib.h>
#include <libeos-payg-codes/codes.h>
#include <math.h>
#include <string.h>


/*
 * Codes are 8-digit strings which can be interpreted as unsigned integers
 * mod 10^8. Any string representation of a code has to be exactly 8 characters
 * long to be valid (so must be zero-prefixed if needed).
 *
 * Each code needs to contain a time period (which would set the computer’s
 * expiry time to now + the period), with authenticity and integrity
 * properties. In theory, this can be achieved by signing a time period with a
 * private key belonging to Endless (generation), and then verifying the
 * signature with a public key (verification). However, the key and block
 * lengths are too long for this to be practical for such a short plaintext.
 * Symmetric ciphers are similarly unsuitable.
 *
 * Instead, the message has a truncated signature appended, and verification is
 * performed by extracting the message (period and uniqueness counter) from the
 * code and re-generating a new signature for them, then comparing that to the
 * inputted signature. This requires use of a shared key.
 *
 * The algorithm we use is based closely on HOTP, using HMAC-SHA-1. We sign a
 * message P ∥ C which contains the code’s period, and a counter value for
 * uniqueness. The counter allows a verifying computer to reject that code
 * again in future.
 *
 * Our implementation differs from the HOTP standard in the definition of the
 * Truncate() function, and the calculation of the Code-Value. They are both
 * adjusted to be shorter, to fit within a 26 bit limit for codes.
 *
 * Let:
 *  - K be a secret key shared between the generation and verification sides
 *  - C be a counter (8 bits)
 *  - P be the time period (5 bits)
 *  - HMAC(K, m) be the HMAC function
 *  - Truncate be a function that selects 13 bits from the result of the HMAC in a defined manner
 * Then Sign(K, C, P) is defined as:
 *  - Sign(K, C, P) = Truncate(HMAC(K, P ∥ C)) & 0x7FFFFFFF
 * and the code displayed to the user (half of which is the message, half of which is the truncated signature calculated using Sign) is defined as:
 *  - Code-Value = (P ∥ C ∥ Sign(K, C, P)) mod 10^8
 * and it’s formatted base-10 using normal digits.
 */
#define COUNTER_WIDTH_BITS 8
#define PERIOD_WIDTH_BITS 5
#define SIGN_WIDTH_BITS 13
#define CODE_VALUE_WIDTH_BITS (COUNTER_WIDTH_BITS + PERIOD_WIDTH_BITS + SIGN_WIDTH_BITS)
#define CODE_STR_WIDTH_DIGITS 8

/* The key is used with the HMAC() function, which always adjusts it to be the
 * same as the block size of the hash function in use (in this case, SHA-1).
 * If the key is too short, it is padded; if it’s too long, it’s hashed. We want
 * to avoid padding, so set the minimum size to be the block size of the hash
 * function. */
#define KEY_LENGTH_BYTES_MINIMUM 64

G_DEFINE_QUARK (EpcCodeError, epc_code_error)

/**
 * epc_period_validate:
 * @period: possibly an #EpcPeriod
 * @error: return location for a #GError
 *
 * Validate @period to work out whether it’s a valid #EpcPeriod. If not,
 * %EPC_CODE_ERROR_INVALID_PERIOD will be returned.
 *
 * Returns: %TRUE if @period is valid, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
epc_period_validate (EpcPeriod   period,
                     GError    **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Handle the validation this way, so we can catch invalid uses of holes in
   * the code-space without hard-coding all the period values here numerically.
   * This makes use of -Wswitch-enum. */
  switch (period)
    {
    case EPC_PERIOD_5_SECONDS:
    case EPC_PERIOD_1_MINUTE:
    case EPC_PERIOD_5_MINUTES:
    case EPC_PERIOD_1_HOUR:
    case EPC_PERIOD_1_DAY:
    case EPC_PERIOD_2_DAYS:
    case EPC_PERIOD_3_DAYS:
    case EPC_PERIOD_4_DAYS:
    case EPC_PERIOD_5_DAYS:
    case EPC_PERIOD_6_DAYS:
    case EPC_PERIOD_7_DAYS:
    case EPC_PERIOD_8_DAYS:
    case EPC_PERIOD_9_DAYS:
    case EPC_PERIOD_10_DAYS:
    case EPC_PERIOD_11_DAYS:
    case EPC_PERIOD_12_DAYS:
    case EPC_PERIOD_13_DAYS:
    case EPC_PERIOD_14_DAYS:
    case EPC_PERIOD_30_DAYS:
    case EPC_PERIOD_60_DAYS:
    case EPC_PERIOD_90_DAYS:
    case EPC_PERIOD_120_DAYS:
    case EPC_PERIOD_365_DAYS:
    case EPC_PERIOD_INFINITE:
      g_assert (period < (1 << PERIOD_WIDTH_BITS));
      return TRUE;
    default:
      g_set_error (error, EPC_CODE_ERROR, EPC_CODE_ERROR_INVALID_PERIOD,
                   _("Unknown period %u."), (guint) period);
      return FALSE;
    }
}

/* Validate @key to ensure it’s long enough to provide sufficient entropy.
 * Returns %EPC_CODE_ERROR_INVALID_KEY if not. */
static gboolean
validate_key (GBytes  *key,
              GError **error)
{
  if (g_bytes_get_size (key) < KEY_LENGTH_BYTES_MINIMUM)
    {
      g_set_error (error, EPC_CODE_ERROR, EPC_CODE_ERROR_INVALID_KEY,
                   _("Key is too short; minimum length %u bytes."),
                   (guint) KEY_LENGTH_BYTES_MINIMUM);
      return FALSE;
    }

  return TRUE;
}

/**
 * epc_code_validate:
 * @code: possibly an #EpcCode
 * @error: return location for a #GError
 *
 * Validate @code to work out whether it has the right structure for an
 * #EpcCode. If not, %EPC_CODE_ERROR_INVALID_CODE will be returned.
 *
 * Note that this only validates the structure (length and maximum value) of
 * @code. It does not check the HMAC matches the message; use
 * epc_verify_code() for that.
 *
 * Returns: %TRUE if @code is valid, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
epc_code_validate (EpcCode   code,
                   GError  **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if ((code >> CODE_VALUE_WIDTH_BITS) == 0 &&
       code < pow (10, CODE_STR_WIDTH_DIGITS))
    return TRUE;

  g_autofree gchar *code_str = g_strdup_printf ("%08u", code);
  g_set_error (error, EPC_CODE_ERROR, EPC_CODE_ERROR_INVALID_CODE,
               _("Invalid code %s."), code_str);

  return FALSE;
}

/**
 * epc_calculate_code:
 * @period: period to encode in the code
 * @counter: counter to add uniqueness to the code
 * @key: shared key
 * @error: return location for a #GError
 *
 * Calculate the code for @period and @counter using the given shared @key. This
 * is the way to generate new codes.
 *
 * Use epc_verify_code() to verify codes generated with this function.
 *
 * If @period is invalid, %EPC_CODE_ERROR_INVALID_PERIOD will be returned. If
 * @key is invalid, %EPC_CODE_ERROR_INVALID_KEY will be returned.
 *
 * Returns: the calculated code
 * Since: 0.1.0
 */
EpcCode
epc_calculate_code (EpcPeriod    period,
                    EpcCounter   counter,
                    GBytes      *key,
                    GError     **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

  if (!epc_period_validate (period, error))
    return 0;
  if (!validate_key (key, error))
    return 0;

  g_assert ((period >> PERIOD_WIDTH_BITS) == 0);
  g_assert ((counter >> COUNTER_WIDTH_BITS) == 0);

  /* Calculate the HMAC. */
  gsize key_len;
  const gchar *key_data = g_bytes_get_data (key, &key_len);
  g_autoptr(GHmac) hmac_state = g_hmac_new (G_CHECKSUM_SHA1,
                                            (guchar *) key_data, key_len);
  g_hmac_update (hmac_state, (guint8 *) &period, 1);
  g_hmac_update (hmac_state, (guint8 *) &counter, 1);

  guint8 hmac_data[20] = { 0, };
  gsize hmac_len = G_N_ELEMENTS (hmac_data);
  g_hmac_get_digest (hmac_state, hmac_data, &hmac_len);
  g_assert (hmac_len == 20);

  /* Truncate down to SIGN_WIDTH_BITS bits. */
  const guint16 sign_mask = (1 << SIGN_WIDTH_BITS) - 1;
  guint16 sign_result = ((((guint16) hmac_data[18]) << 8) | hmac_data[19]) & sign_mask;

  g_assert ((sign_result >> SIGN_WIDTH_BITS) == 0);

  /* Build the full 26-bit code to return. */
  guint32 code_value = (((guint32) period) << (COUNTER_WIDTH_BITS + SIGN_WIDTH_BITS)) |
                       (((guint32) counter) << SIGN_WIDTH_BITS) |
                       ((guint32) sign_result);

  g_assert (epc_code_validate (code_value, NULL));

  return code_value;
}

/**
 * epc_verify_code:
 * @code: code to verify
 * @key: shared key
 * @period_out: (out) (optional): return location for the period from @code
 * @counter_out: (out) (optional): return location for the counter from @code
 * @error: return location for a #GError
 *
 * Verify that @code is correctly signed with the given shared @key, and
 * extract the #EpcPeriod and #EpcCounter which were used to generate the key.
 *
 * Use epc_calculate_code() to generate codes to verify with this function.
 *
 * Returns: %TRUE if @code is valid, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
epc_verify_code (EpcCode      code,
                 GBytes      *key,
                 EpcPeriod   *period_out,
                 EpcCounter  *counter_out,
                 GError     **error)
{
  g_autoptr(GError) local_error = NULL;

  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!epc_code_validate (code, error))
    return FALSE;
  if (!validate_key (key, error))
    return FALSE;

  /* Extract the period and counter. */
  const guint32 period_mask = (1 << PERIOD_WIDTH_BITS) - 1;
  EpcPeriod period = (code >> (COUNTER_WIDTH_BITS + SIGN_WIDTH_BITS)) & period_mask;

  const guint32 counter_mask = (1 << COUNTER_WIDTH_BITS) - 1;
  EpcCounter counter = (code >> SIGN_WIDTH_BITS) & counter_mask;

  /* Re-calculate the code for this @period, @counter and @key and compare it
   * to the input. */
  EpcCode check_code = epc_calculate_code (period, counter, key, &local_error);

  if (local_error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  /* Compare the codes. */
  if (check_code != code)
    {
      g_autofree gchar *code_str = epc_format_code (code);
      g_set_error (error, EPC_CODE_ERROR, EPC_CODE_ERROR_INVALID_SIGNATURE,
                   _("Invalid signature on code %s."), code_str);
      return FALSE;
    }

  /* Return what we parsed. */
  if (period_out != NULL)
    *period_out = period;
  if (counter_out != NULL)
    *counter_out = counter;

  return TRUE;
}

/**
 * epc_format_code:
 * @code: a valid code to format
 *
 * Format the given @code as a string, suitable for display to the user. The
 * return value from this function is guaranteed to be parsable by
 * epc_parse_code().
 *
 * Returns: (transfer full): string form of @code
 * Since: 0.1.0
 */
gchar *
epc_format_code (EpcCode code)
{
  g_return_val_if_fail (epc_code_validate (code, NULL), NULL);

  g_autofree gchar *code_str = g_strdup_printf ("%08u", code);
  g_assert (strlen (code_str) == CODE_STR_WIDTH_DIGITS);

  return g_steal_pointer (&code_str);
}

/**
 * epc_parse_code:
 * @code_str: a valid code to parse
 * @error: return location for a #GError
 *
 * Parse the given @code_str and return it in integer form. If the string is
 * not parsable as a code, or would result in an invalid code,
 * %EPC_CODE_ERROR_INVALID_CODE is returned.
 *
 * Returns: the parsed code
 * Since: 0.1.0
 */
EpcCode
epc_parse_code (const gchar  *code_str,
                GError      **error)
{
  g_return_val_if_fail (code_str != NULL, 0);
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

  guint64 code_value;

  if (strlen (code_str) != CODE_STR_WIDTH_DIGITS ||
      !g_ascii_string_to_unsigned (code_str,
                                   10,  /* base */
                                   0,  /* minimum */
                                   (1 << CODE_VALUE_WIDTH_BITS) - 1,  /* maximum */
                                   &code_value,
                                   NULL))
    {
      g_set_error_literal (error, EPC_CODE_ERROR, EPC_CODE_ERROR_INVALID_CODE,
                           _("Codes must be 8 digits long."));
      return 0;
    }

  if (!epc_code_validate (code_value, error))
    return 0;

  return (EpcCode) code_value;
}
