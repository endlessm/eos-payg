/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2018 Endless Mobile, Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License,
 * v. 2.0. If a copy of the MPL was not distributed with this file, You can
 * obtain two at https://mozilla.org/MPL/2.0/.
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
 * All rights reserved.
 */

#include <libpeas/peas.h>
#include <libeos-payg/tests/plugins/test-provider.h>

void peas_register_types (PeasObjectModule *module);

struct _EpgTestProviderTwo {
  EpgTestProvider parent;
};

#define EPG_TYPE_TEST_PROVIDER_TWO epg_test_provider_two_get_type ()
G_DECLARE_FINAL_TYPE (EpgTestProviderTwo, epg_test_provider_two, EPG, TEST_PROVIDER_TWO, EpgTestProvider)
G_DEFINE_TYPE (EpgTestProviderTwo, epg_test_provider_two, EPG_TYPE_TEST_PROVIDER);

static void
epg_test_provider_two_class_init (EpgTestProviderTwoClass *klass)
{
}

static void
epg_test_provider_two_init (EpgTestProviderTwo *self)
{
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
  peas_object_module_register_extension_type (module,
                                              EPG_TYPE_PROVIDER,
                                              EPG_TYPE_TEST_PROVIDER_TWO);
}
