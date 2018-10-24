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
#include <libeos-payg/errors.h>
#include <libeos-payg/provider-loader.h>
#include <locale.h>

enum {
  ENABLE_PROVIDER_ONE = 1 << 0,
  ENABLE_PROVIDER_TWO = 1 << 1,
};

typedef struct
{
  const char *suffix;
  int flags;
} TestPermutation;

static TestPermutation matrix[] =
{
  { "neither", 0 },
  { "one", ENABLE_PROVIDER_ONE },
  { "two", ENABLE_PROVIDER_TWO },
  { "both", ENABLE_PROVIDER_ONE | ENABLE_PROVIDER_TWO },
};

typedef struct {
} Fixture;

static void
setup (Fixture *fixture,
       gconstpointer test_data)
{
  const TestPermutation *p = test_data;

  g_setenv ("EpgTestProviderOne", p->flags & ENABLE_PROVIDER_ONE ? "enabled" : "disabled", TRUE);
  g_setenv ("EpgTestProviderTwo", p->flags & ENABLE_PROVIDER_TWO ? "enabled" : "disabled", TRUE);
}

static void
teardown (Fixture       *fixture,
          gconstpointer  test_data)
{
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

/* Tests loading the two defined test providers, asserting that they are
 * enabled or disabled as determined by the flags in the test data.
 */
static void
test_load (Fixture       *fixture,
           gconstpointer  test_data)
{
  const TestPermutation *p = test_data;
  g_autofree gchar *plugins_dir = g_test_build_filename (G_TEST_BUILT,
                                                         "plugins",
                                                         NULL);
  g_autoptr(EpgProviderLoader) loader = epg_provider_loader_new (plugins_dir);
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(GPtrArray) providers = NULL;
  g_autoptr(GError) local_error = NULL;
  guint i;

  epg_provider_loader_load_async (loader, NULL, async_cb, &result);

  while (result == NULL)
    g_main_context_iteration (NULL, FALSE);

  providers = epg_provider_loader_load_finish (loader, result, &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (providers);

  g_assert_cmpuint (providers->len, ==, 2);
  for (i = 0; i < providers->len; i++)
    {
      EpgProvider *provider = g_ptr_array_index (providers, i);
      gboolean enabled = epg_provider_get_enabled (provider);
      const char *name = G_OBJECT_TYPE_NAME (provider);

      if (!g_strcmp0 (name, "EpgTestProviderOne"))
        {
          g_assert_cmpint (!!(p->flags & ENABLE_PROVIDER_ONE), ==, enabled);
        }
      else
        {
          g_assert_cmpstr (name, ==, "EpgTestProviderTwo");
          g_assert_cmpint (!!(p->flags & ENABLE_PROVIDER_TWO), ==, enabled);
        }
    }
}

/* Tests loading the first enabled provider. If the test flags show that
 * neither should be enabled, asserts that the call cleanly returns NULL;
 * if it shows that both should be enabled, asserts only that it doesn't fail.
 * Otherwise, asserts that the correct one is returned.
 */
static void
test_get_first_enabled (Fixture       *fixture,
                         gconstpointer  test_data)
{
  const TestPermutation *p = test_data;
  g_autofree gchar *plugins_dir = g_test_build_filename (G_TEST_BUILT,
                                                         "plugins",
                                                         NULL);
  g_autoptr(EpgProviderLoader) loader = epg_provider_loader_new (plugins_dir);
  g_autoptr(GAsyncResult) result = NULL;
  g_autoptr(EpgProvider) provider = NULL;
  g_autoptr(GError) local_error = NULL;

  epg_provider_loader_get_first_enabled_async (loader, NULL, async_cb, &result);

  while (result == NULL)
    g_main_context_iteration (NULL, FALSE);

  provider = epg_provider_loader_get_first_enabled_finish (loader, result, &local_error);
  g_assert_no_error (local_error);

  if (p->flags == 0)
    {
      /* It's not an error if none are enabled */
      g_assert_null (provider);
    }
  else
    {
      g_assert_nonnull (provider);
      g_assert_true (epg_provider_get_enabled (provider));

      const char *name = G_OBJECT_TYPE_NAME (provider);

      if (p->flags == ENABLE_PROVIDER_ONE)
        g_assert_cmpstr (name, ==, "EpgTestProviderOne");
      else if (p->flags == ENABLE_PROVIDER_TWO)
        g_assert_cmpstr (name, ==, "EpgTestProviderTwo");
      else
        g_assert_cmpint (p->flags, ==, ENABLE_PROVIDER_ONE | ENABLE_PROVIDER_TWO);
    }
}

int
main (int    argc,
      char **argv)
{
  setlocale (LC_ALL, "");
  g_test_init (&argc, &argv, NULL);

  gsize i;

  for (i = 0; i < G_N_ELEMENTS (matrix); i++)
    {
      TestPermutation *p = &matrix[i];
      gchar *path;

      path = g_strconcat ("/provider-loader/load/", p->suffix, NULL);
      g_test_add (path, Fixture, p, setup, test_load, teardown);
      g_free (path);

      path = g_strconcat ("/provider-loader/get-first-enabled/", p->suffix, NULL);
      g_test_add (path, Fixture, p, setup, test_get_first_enabled, teardown);
      g_free (path);
    }

  return g_test_run ();
}
