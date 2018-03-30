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
#include <gio/gio.h>

G_BEGIN_DECLS

/*
 * Declaration of the com.endlessm.Payg1 D-Bus interface.
 *
 * FIXME: Ideally, there would be a gdbus-codegen mode to generate just this
 * interface info, because writing it out in C is horrific.
 */

static const GDBusArgInfo manager_interface_add_code_arg_code =
{
  -1,  /* ref count */
  (gchar *) "code",
  (gchar *) "s",
  NULL
};

static const GDBusArgInfo *manager_interface_add_code_in_args[] =
{
  &manager_interface_add_code_arg_code,
  NULL,
};
static const GDBusMethodInfo manager_interface_add_code =
{
  -1,  /* ref count */
  (gchar *) "AddCode",
  (GDBusArgInfo **) manager_interface_add_code_in_args,
  NULL,  /* out args */
  NULL,  /* annotations */
};

static const GDBusMethodInfo manager_interface_clear_code =
{
  -1,  /* ref count */
  (gchar *) "ClearCode",
  NULL,  /* in args */
  NULL,  /* out args */
  NULL,  /* annotations */
};

static const GDBusMethodInfo *manager_interface_methods[] =
{
  &manager_interface_add_code,
  &manager_interface_clear_code,
  NULL,
};

static const GDBusSignalInfo manager_interface_expired =
{
  -1,  /* ref count */
  (gchar *) "Expired",
  NULL,  /* args */
  NULL,  /* annotations */
};

static const GDBusSignalInfo *manager_interface_signals[] =
{
  &manager_interface_expired,
  NULL,
};

static const GDBusPropertyInfo manager_interface_expiry_time =
{
  -1,  /* ref count */
  (gchar *) "ExpiryTime",
  (gchar *) "t",
  G_DBUS_PROPERTY_INFO_FLAGS_READABLE,
  NULL,  /* annotations */
};

static const GDBusPropertyInfo manager_interface_enabled =
{
  -1,  /* ref count */
  (gchar *) "Enabled",
  (gchar *) "b",
  G_DBUS_PROPERTY_INFO_FLAGS_READABLE,
  NULL,  /* annotations */
};

static const GDBusPropertyInfo manager_interface_rate_limit_end_time =
{
  -1,  /* ref count */
  (gchar *) "RateLimitEndTime",
  (gchar *) "t",
  G_DBUS_PROPERTY_INFO_FLAGS_READABLE,
  NULL,  /* annotations */
};

static const GDBusPropertyInfo *manager_interface_properties[] =
{
  &manager_interface_expiry_time,
  &manager_interface_enabled,
  &manager_interface_rate_limit_end_time,
  NULL,
};

static const GDBusInterfaceInfo manager_interface =
{
  -1,  /* ref count */
  (gchar *) "com.endlessm.Payg1",
  (GDBusMethodInfo **) manager_interface_methods,
  (GDBusSignalInfo **) manager_interface_signals,
  (GDBusPropertyInfo **) manager_interface_properties,
  NULL,  /* no annotations */
};

static const gchar *manager_errors[] =
{
  "com.endlessm.Payg1.Error.InvalidCode",
  "com.endlessm.Payg1.Error.CodeAlreadyUsed",
  "com.endlessm.Payg1.Error.TooManyAttempts",
  "com.endlessm.Payg1.Error.Disabled",
};

G_END_DECLS
