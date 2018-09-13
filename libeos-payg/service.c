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
#include <glib-object.h>
#include <glib-unix.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libeos-payg/manager.h>
#include <libeos-payg/manager-service.h>
#include <libeos-payg/resources.h>
#include <libeos-payg/service.h>
#include <libeos-payg-codes/codes.h>
#include <libgsystemservice/config-file.h>
#include <locale.h>


/* Paths to the various places the config file could be loaded from. */
#define ETC_CONFIG_FILE_PATH SYSCONFDIR "/eos-payg/eos-payg.conf"
#define USR_LOCAL_SHARE_CONFIG_FILE_PATH PREFIX "/local/share/eos-payg/eos-payg.conf"
#define USR_SHARE_CONFIG_FILE_PATH DATADIR "/eos-payg/eos-payg.conf"


static void epg_service_dispose (GObject *object);

static GOptionEntry *epg_service_get_main_option_entries (GssService *service);

static void epg_service_startup_async (GssService          *service,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data);
static void epg_service_startup_finish (GssService    *service,
                                        GAsyncResult  *result,
                                        GError       **error);
static void epg_service_shutdown (GssService *service);

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
  GssService parent;

  EpgProvider *provider;  /* (owned) */
  EpgManagerService *manager_service;  /* (owned) */

  /* This is normally %NULL, and is only non-%NULL when overridden from the
   * command line: */
  gchar *config_file_path;  /* (type filename) (owned) (nullable) */
};

G_DEFINE_TYPE (EpgService, epg_service, GSS_TYPE_SERVICE)

static void
epg_service_class_init (EpgServiceClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GssServiceClass *service_class = (GssServiceClass *) klass;

  object_class->dispose = epg_service_dispose;

  service_class->get_main_option_entries = epg_service_get_main_option_entries;
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
  g_clear_object (&self->provider);
  g_clear_pointer (&self->config_file_path, g_free);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (epg_service_parent_class)->dispose (object);
}

static GOptionEntry *
epg_service_get_main_option_entries (GssService *service)
{
  EpgService *self = EPG_SERVICE (service);

  g_autofree GOptionEntry *entries = g_new0 (GOptionEntry, 1 + 1 /* NULL terminator */);

  entries[0].long_name = "config-file";
  entries[0].short_name = 'c';
  entries[0].flags = G_OPTION_FLAG_NONE;
  entries[0].arg = G_OPTION_ARG_FILENAME;
  entries[0].arg_data = &self->config_file_path;
  entries[0].description = N_("Configuration file to use (default: " ETC_CONFIG_FILE_PATH ")");
  entries[0].arg_description = N_("PATH");

  return g_steal_pointer (&entries);
}

static void manager_new_cb (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data);

static void
epg_service_startup_async (GssService          *service,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  EpgService *self = EPG_SERVICE (service);
  g_autoptr(GError) local_error = NULL;

  g_autoptr(GTask) task = g_task_new (service, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_service_startup_async);

  /* Load the configuration file. */
  const gchar * const default_paths[] =
    {
      ETC_CONFIG_FILE_PATH,
      USR_LOCAL_SHARE_CONFIG_FILE_PATH,
      USR_SHARE_CONFIG_FILE_PATH,
      NULL,
    };
  const gchar * const override_paths[] =
    {
      self->config_file_path,
      USR_SHARE_CONFIG_FILE_PATH,
      NULL,
    };

  g_autoptr(GssConfigFile) config_file = NULL;
  config_file = gss_config_file_new ((self->config_file_path != NULL) ? override_paths : default_paths,
                                     epg_get_resource (),
                                     "/com/endlessm/Payg1/config/eos-payg.conf");

  /* Is pay as you go enabled? */
  gboolean enabled = gss_config_file_get_boolean (config_file,
                                                  "PAYG", "Enabled",
                                                  &local_error);

  if (local_error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  g_autoptr(GFile) key_file = g_file_new_for_path (PREFIX "/local/share/eos-payg/key");
  g_autoptr(GFile) state_directory = g_file_new_for_path (LOCALSTATEDIR "/lib/eos-payg");

  epg_manager_new (enabled, key_file, state_directory,
                   cancellable,
                   manager_new_cb, g_steal_pointer (&task));
}

static void
manager_new_cb (GObject      *source_object,
                GAsyncResult *result,
                gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  EpgService *self = EPG_SERVICE (g_task_get_source_object (task));
  g_autoptr(GError) local_error = NULL;

  self->provider = epg_manager_new_finish (result, &local_error);
  if (self->provider == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  /* Create our D-Bus service. */
  GDBusConnection *connection = gss_service_get_dbus_connection (GSS_SERVICE (self));

  self->manager_service = epg_manager_service_new (connection,
                                                   "/com/endlessm/Payg1",
                                                   self->provider);

  if (!epg_manager_service_register (self->manager_service, &local_error))
    g_task_return_error (task, g_steal_pointer (&local_error));
  else
    g_task_return_boolean (task, TRUE);
}

static void
epg_service_startup_finish (GssService    *service,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_task_propagate_boolean (G_TASK (result), error);
}

static void
async_result_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GAsyncResult **result_out = user_data;
  *result_out = g_object_ref (result);
}

static void
epg_service_shutdown (GssService *service)
{
  EpgService *self = EPG_SERVICE (service);
  g_autoptr(GError) local_error = NULL;

  /* Save the provider’s state. */
  g_autoptr(GAsyncResult) result = NULL;
  epg_provider_save_state_async (self->provider, NULL, async_result_cb, &result);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  if (!epg_provider_save_state_finish (self->provider, result, &local_error))
    {
      g_warning ("Error saving state: %s", local_error->message);
      g_clear_error (&local_error);
    }

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
