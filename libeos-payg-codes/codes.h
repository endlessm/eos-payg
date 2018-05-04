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

#include <glib.h>

G_BEGIN_DECLS

/**
 * EpcCodeError:
 * @EPC_CODE_ERROR_INVALID_PERIOD: An #EpcPeriod was invalid, using a value
 *    outside the enumerated set.
 * @EPC_CODE_ERROR_INVALID_KEY: A shared key was invalid by being too short.
 * @EPC_CODE_ERROR_INVALID_CODE: A code (in integer or string form) was invalid
 *    by being outside the permitted code space.
 * @EPC_CODE_ERROR_INVALID_SIGNATURE: When verifying a code, the signature
 *    did not match the message.
 *
 * Errors which can be returned by the code generation and verification
 * functions.
 *
 * Since: 0.1.0
 */
typedef enum
{
  EPC_CODE_ERROR_INVALID_PERIOD = 0,
  EPC_CODE_ERROR_INVALID_KEY,
  EPC_CODE_ERROR_INVALID_CODE,
  EPC_CODE_ERROR_INVALID_SIGNATURE,
} EpcCodeError;
#define EPC_CODE_N_ERRORS (EPC_CODE_ERROR_INVALID_CODE + 1)

GQuark epc_code_error_quark (void);
#define EPC_CODE_ERROR epc_code_error_quark ()

/**
 * EpcPeriod:
 *
 * The different available time periods for a code to encode. The time period
 * indicates how much more time the code will permit the user to use their
 * computer for.
 *
 * The small time periods are intended for testing purposes only, and are not
 * intended to be used in production.
 *
 * More time periods may be added to this set in future.
 *
 * Since: 0.1.0
 */
typedef enum
{
  EPC_PERIOD_5_SECONDS = 0,
  EPC_PERIOD_1_MINUTE = 1,
  EPC_PERIOD_5_MINUTES = 2,
  EPC_PERIOD_1_HOUR = 3,
  EPC_PERIOD_1_DAY = 4,
  EPC_PERIOD_2_DAYS = 5,
  EPC_PERIOD_3_DAYS = 6,
  EPC_PERIOD_4_DAYS = 7,
  EPC_PERIOD_5_DAYS = 8,
  EPC_PERIOD_6_DAYS = 9,
  EPC_PERIOD_7_DAYS = 10,
  EPC_PERIOD_8_DAYS = 11,
  EPC_PERIOD_9_DAYS = 12,
  EPC_PERIOD_10_DAYS = 13,
  EPC_PERIOD_11_DAYS = 14,
  EPC_PERIOD_12_DAYS = 15,
  EPC_PERIOD_13_DAYS = 16,
  EPC_PERIOD_14_DAYS = 17,
  EPC_PERIOD_30_DAYS = 18,
  EPC_PERIOD_60_DAYS = 19,
  EPC_PERIOD_90_DAYS = 20,
  EPC_PERIOD_120_DAYS = 21,
  EPC_PERIOD_365_DAYS = 22,
  EPC_PERIOD_30_MINUTES = 23,
  EPC_PERIOD_8_HOURS = 24,
  /* add additional periods here, and update %EPC_N_PERIODS */
  EPC_PERIOD_INFINITE = 31,
  /* This must use 5 bits exactly, so no values above 31 are allowed */
} EpcPeriod;

/**
 * EPC_N_PERIODS:
 *
 * The number of periods defined in #EpcPeriod.
 *
 * Since: 0.1.0
 */
#define EPC_N_PERIODS 26

gboolean epc_period_validate (EpcPeriod   period,
                              GError    **error);

/**
 * EpcCounter:
 *
 * A type representing a counter value used to generate different codes for the
 * same #EpcPeriod and shared key.
 *
 * The full 8-bit code space can be used.
 *
 * Since: 0.1.0
 */
typedef guint8 EpcCounter;

/**
 * EPC_MINCOUNTER:
 *
 * Minimum valid value of an #EpcCounter variable (inclusive).
 *
 * Since: 0.1.0
 */
#define EPC_MINCOUNTER 0

/**
 * EPC_MAXCOUNTER:
 *
 * Maximum valid value of an #EpcCounter variable (inclusive).
 *
 * Since: 0.1.0
 */
#define EPC_MAXCOUNTER G_MAXUINT8

/**
 * EPC_KEY_MINIMUM_LENGTH_BYTES:
 *
 * Minimum valid length of a shared key, in bytes. Shorter keys will be
 * rejected; longer keys will be hashed down to this length.
 *
 * Since: 0.1.0
 */
#define EPC_KEY_MINIMUM_LENGTH_BYTES 64

/**
 * EpcCode:
 *
 * An integer representation of a code, as calculated by epc_calculate_code().
 *
 * Only the least significant 26 bits of the code space can be used; any
 * #EpcCode with more significant bits set is invalid. This can be checked using
 * epc_code_validate().
 *
 * Since: 0.1.0
 */
typedef guint32 EpcCode;

gboolean epc_code_validate  (EpcCode       code,
                             GError      **error);

EpcCode  epc_calculate_code (EpcPeriod     period,
                             EpcCounter    counter,
                             GBytes       *key,
                             GError      **error);
gboolean epc_verify_code    (EpcCode       code,
                             GBytes       *key,
                             EpcPeriod    *period_out,
                             EpcCounter   *counter_out,
                             GError      **error);

gchar    *epc_format_code   (EpcCode       code);
gboolean  epc_parse_code    (const gchar  *code_str,
                             EpcCode      *code_out,
                             GError      **error);

G_END_DECLS
