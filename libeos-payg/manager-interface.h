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

static const GDBusPropertyInfo *manager_interface_properties[] =
{
  &manager_interface_expiry_time,
  &manager_interface_enabled,
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
