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
#include <glib-object.h>
#include <glib-unix.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libeos-payg/manager-service.h>
#include <libeos-payg/service.h>
#include <locale.h>


static void epg_service_dispose (GObject *object);

static void epg_service_startup_async (HlpService          *service,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data);
static void epg_service_startup_finish (HlpService    *service,
                                        GAsyncResult  *result,
                                        GError       **error);
static void epg_service_shutdown (HlpService *service);

/**
 * EpgService:
 *
 * The core implementation of the pay as you go daemon, which exposes its D-Bus
 * API on the bus.
 *
 * Since: 0.1.0
 */
struct _EpgService
{
  HlpService parent;

  EpgManagerService *manager_service;  /* (owned) */
};

G_DEFINE_TYPE (EpgService, epg_service, HLP_TYPE_SERVICE)

static void
epg_service_class_init (EpgServiceClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  HlpServiceClass *service_class = (HlpServiceClass *) klass;

  object_class->dispose = epg_service_dispose;

  service_class->startup_async = epg_service_startup_async;
  service_class->startup_finish = epg_service_startup_finish;
  service_class->shutdown = epg_service_shutdown;
}

static void
epg_service_init (EpgService *self)
{
  /* Nothing to see here. */
}

static void
epg_service_dispose (GObject *object)
{
  EpgService *self = EPG_SERVICE (object);

  g_clear_object (&self->manager_service);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (epg_service_parent_class)->dispose (object);
}

static void
epg_service_startup_async (HlpService          *service,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  EpgService *self = EPG_SERVICE (service);
  g_autoptr(GError) local_error = NULL;

  g_autoptr (GTask) task = g_task_new (service, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_service_startup_async);

  GDBusConnection *connection = hlp_service_get_dbus_connection (service);

  g_autoptr(EpgManager) manager = epg_manager_new ();

  self->manager_service = epg_manager_service_new (connection,
                                                   "/com/endlessm/Payg1",
                                                   manager);

  if (!epg_manager_service_register (self->manager_service, &local_error))
    g_task_return_error (task, g_steal_pointer (&local_error));
  else
    g_task_return_boolean (task, TRUE);
}

static void
epg_service_startup_finish (HlpService    *service,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_task_propagate_boolean (G_TASK (result), error);
}

static void
epg_service_shutdown (HlpService *service)
{
  EpgService *self = EPG_SERVICE (service);

  epg_manager_service_unregister (self->manager_service);
}

/**
 * epg_service_new:
 *
 * Create a new #EpgService.
 *
 * Returns: (transfer full): a new #EpgService
 * Since: 0.1.0
 */
EpgService *
epg_service_new (void)
{
  return g_object_new (EPG_TYPE_SERVICE,
                       "bus-type", G_BUS_TYPE_SYSTEM,
                       "service-id", "com.endlessm.Payg1",
                       "inactivity-timeout", 0  /* no timeout */,
                       "translation-domain", GETTEXT_PACKAGE,
                       "parameter-string", _("— verify top-up codes and monitor time remaining"),
                       "summary", _("Verify inputted top-up codes and "
                                    "monitor the amount of time the computer "
                                    "has remaining before its lease next needs "
                                    "topping up."),
                       NULL);
}
