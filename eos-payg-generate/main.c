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

#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <libeos-payg-codes/codes.h>
#include <locale.h>


/* Exit statuses. */
typedef enum
{
  /* Success. */
  EXIT_OK = 0,
  /* Error parsing command line options. */
  EXIT_INVALID_OPTIONS = 1,
  /* Command failed. */
  EXIT_FAILED = 2,
} ExitStatus;

/* Main function stuff. */
static const struct
  {
    EpcPeriod period;
    const gchar *period_str;
    const gchar *description;
  }
periods[] =
  {
    { EPC_PERIOD_5_SECONDS, "5s", N_("5 seconds") },
    { EPC_PERIOD_1_MINUTE, "1m", N_("1 minute") },
    { EPC_PERIOD_5_MINUTES, "5m", N_("5 minutes") },
    { EPC_PERIOD_1_HOUR, "1h", N_("1 hour") },
    { EPC_PERIOD_1_DAY, "1d", N_("1 day") },
    { EPC_PERIOD_2_DAYS, "2d", N_("2 days") },
    { EPC_PERIOD_3_DAYS, "3d", N_("3 days") },
    { EPC_PERIOD_4_DAYS, "4d", N_("4 days") },
    { EPC_PERIOD_5_DAYS, "5d", N_("5 days") },
    { EPC_PERIOD_6_DAYS, "6d", N_("6 days") },
    { EPC_PERIOD_7_DAYS, "7d", N_("7 days") },
    { EPC_PERIOD_8_DAYS, "8d", N_("8 days") },
    { EPC_PERIOD_9_DAYS, "9d", N_("9 days") },
    { EPC_PERIOD_10_DAYS, "10d", N_("10 days") },
    { EPC_PERIOD_11_DAYS, "11d", N_("11 days") },
    { EPC_PERIOD_12_DAYS, "12d", N_("12 days") },
    { EPC_PERIOD_13_DAYS, "13d", N_("13 days") },
    { EPC_PERIOD_14_DAYS, "14d", N_("14 days") },
    { EPC_PERIOD_30_DAYS, "30d", N_("30 days") },
    { EPC_PERIOD_60_DAYS, "60d", N_("60 days") },
    { EPC_PERIOD_90_DAYS, "90d", N_("90 days") },
    { EPC_PERIOD_120_DAYS, "120d", N_("120 days") },
    { EPC_PERIOD_365_DAYS, "365d", N_("365 days") },
    { EPC_PERIOD_INFINITE, "infinite", N_("Infinite") },
  };
G_STATIC_ASSERT (G_N_ELEMENTS (periods) == EPC_N_PERIODS);

/* Convert a string-form period into an #EpcPeriod. */
static gboolean
parse_period (const gchar  *period_str,
              EpcPeriod    *out_period,
              GError      **error)
{
  g_return_val_if_fail (out_period != NULL, FALSE);

  for (gsize i = 0; i < G_N_ELEMENTS (periods); i++)
    {
      if (g_str_equal (period_str, periods[i].period_str))
        {
          *out_period = periods[i].period;
          return TRUE;
        }
    }

  g_set_error (error, EPC_CODE_ERROR, EPC_CODE_ERROR_INVALID_PERIOD,
               _("Invalid period ‘%s’."), period_str);
  return FALSE;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GError) local_error = NULL;

  /* Localisation */
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  /* Handle command line parameters. */
  gboolean quiet = FALSE;
  gboolean list_periods = FALSE;
  g_auto(GStrv) args = NULL;

  const GOptionEntry entries[] =
    {
      { "quiet", 'q', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &quiet,
        N_("Only print error messages"), NULL },
      { "list-periods", 'l', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &list_periods,
        N_("List the available periods"), NULL },
      { G_OPTION_REMAINING, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING_ARRAY,
        &args, NULL, NULL },
      { NULL, },
    };

  g_autoptr(GOptionContext) context = NULL;
  context = g_option_context_new (_("KEY-FILENAME PERIOD [COUNTER]"));
  g_option_context_set_summary (context, _("Generate one or more pay as you go codes"));
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);

  if (!g_option_context_parse (context, &argc, &argv, &local_error))
    {
      g_autofree gchar *message = NULL;
      message = g_strdup_printf (_("Option parsing failed: %s"),
                                 local_error->message);
      g_printerr ("%s: %s\n", argv[0], message);

      return EXIT_INVALID_OPTIONS;
    }

  /* Early bail out if the user asked to list the available periods. */
  if (list_periods)
    {
      if (!quiet)
        g_print ("%s\n", _("Available periods:"));

      for (gsize i = 0; i < G_N_ELEMENTS (periods); i++)
        {
          if (!quiet)
            g_print (" • %s — %s\n",
                     periods[i].period_str, _(periods[i].description));
          else
            g_print ("%s\n", periods[i].period_str);
        }

      return EXIT_OK;
    }

  if (args == NULL || args[0] == NULL || args[1] == NULL)
    {
      g_autofree gchar *message = NULL;
      message = g_strdup_printf (_("Option parsing failed: %s"),
                                 _("A KEY-FILENAME and PERIOD are required"));
      g_printerr ("%s: %s\n", argv[0], message);

      return EXIT_INVALID_OPTIONS;
    }
  if (args[2] != NULL && args[3] != NULL)
    {
      g_autofree gchar *message = NULL;
      message = g_strdup_printf (_("Option parsing failed: %s"),
                                 _("Too many arguments provided"));
      g_printerr ("%s: %s\n", argv[0], message);

      return EXIT_INVALID_OPTIONS;
    }

  const gchar *key_filename = args[0];
  const gchar *period_str = args[1];
  const gchar *counter_str = args[2];
  g_autoptr(GFile) key_file = g_file_new_for_commandline_arg (key_filename);

  /* Parse the period. */
  EpcPeriod period;

  if (!parse_period (period_str, &period, &local_error))
    {
      g_printerr ("%s: %s\n", argv[0], local_error->message);

      return EXIT_INVALID_OPTIONS;
    }

  /* Load the key. It should be local, so doing it synchronously is OK. */
  g_autofree gchar *key_data = NULL;  /* should be guint8 were it not for strict aliasing */
  gsize key_len = 0;

  if (!g_file_load_contents (key_file, NULL, &key_data, &key_len,
                             NULL, &local_error))
    {
      g_printerr ("%s: %s\n", argv[0], local_error->message);

      return EXIT_INVALID_OPTIONS;
    }

  g_autoptr(GBytes) key_bytes = g_bytes_new_take (g_steal_pointer (&key_data),
                                                  key_len);

  /* Work out how many codes we’re generating. */
  EpcCounter min_counter, max_counter;
  guint64 parsed_counter;

  if (counter_str == NULL)
    {
      min_counter = 0;
      max_counter = EPC_MAXCOUNTER;
    }
  else if (g_ascii_string_to_unsigned (counter_str, 10, 0, EPC_MAXCOUNTER,
                                       &parsed_counter, &local_error))
    {
      min_counter = parsed_counter;
      max_counter = parsed_counter;
    }
  else
    {
      g_printerr ("%s: %s\n", argv[0], local_error->message);

      return EXIT_INVALID_OPTIONS;
    }

  /* Generate codes [min_counter, max_counter]. */
  for (guint64 counter = min_counter; counter <= max_counter; counter++)
    {
      /* Generate the code. */
      EpcCode code = epc_calculate_code (period, counter, key_bytes, &local_error);

      if (local_error != NULL)
        {
          g_printerr ("%s: %s\n", argv[0], local_error->message);

          return EXIT_FAILED;
        }

      /* Format and output. */
      g_autofree gchar *code_str = epc_format_code (code);
      g_print ("%s\n", code_str);
    }

  return EXIT_OK;
}
