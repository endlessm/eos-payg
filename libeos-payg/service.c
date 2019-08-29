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
#include <libeos-payg/provider-loader.h>
#include <libeos-payg/resources.h>
#include <libeos-payg/service.h>
#include <libeos-payg/clock-jump-source.h>
#include <libeos-payg-codes/codes.h>
#include <libgsystemservice/config-file.h>
#include <locale.h>
#include <efivar.h>


/* Paths to the various places the config file could be loaded from. */
#define ETC_CONFIG_FILE_PATH SYSCONFDIR "/eos-payg/eos-payg.conf"
#define USR_LOCAL_SHARE_CONFIG_FILE_PATH PREFIX "/local/share/eos-payg/eos-payg.conf"
#define USR_SHARE_CONFIG_FILE_PATH DATADIR "/eos-payg/eos-payg.conf"

/* The GUID for the EOSPAYG_active EFI variable */
#define EOSPAYG_ACTIVE_GUID EFI_GUID(0xd89c3871, 0xae0c, 0x4fc5, 0xa409, 0xdc, 0x71, 0x7a, 0xee, 0x61, 0xe7)

static const GDBusErrorEntry epg_service_error_entries[] = {
  { EPG_SERVICE_ERROR_NO_PROVIDER, "com.endlessm.Payg1.Error.NoProvider" },
};

/* Ensure that every error code has an associated D-Bus error name */
G_STATIC_ASSERT (G_N_ELEMENTS (epg_service_error_entries) == EPG_SERVICE_ERROR_LAST + 1);

GQuark
epg_service_error_quark (void)
{
  static volatile gsize quark_volatile = 0;
  g_dbus_error_register_error_domain ("epg-service-error-quark",
                                      &quark_volatile,
                                      epg_service_error_entries,
                                      G_N_ELEMENTS (epg_service_error_entries));
  return (GQuark) quark_volatile;
}

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

  /* If @provider is non-%NULL, a non-zero ID for a handler of
   * provider::notify::enabled.
   */
  gulong notify_enabled_id;

  /* If %TRUE, there is an outstanding call to gss_service_hold() without a
   * matching call to gss_service_release().
   */
  gboolean holding;

  /* A GSource to detect discontinous clock changes, and the pre-jump clock
   * values so the delta can be calculated */
  GSource *source; /* (owned) */
  gint64 clock_realtime_secs_v0;
  gint64 clock_boottime_secs_v0;

  /* Whether the EOSPAYG_active EFI variable is set */
  gboolean eospayg_active_efivar;
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

  if (self->notify_enabled_id != 0)
    {
      g_assert (self->provider != NULL);
      g_signal_handler_disconnect (self->provider, self->notify_enabled_id);
      self->notify_enabled_id = 0;
    }

  g_clear_object (&self->manager_service);
  g_clear_object (&self->provider);

  if (self->source != NULL)
    {
      g_source_destroy (self->source);
      g_source_unref (self->source);
      self->source = NULL;
    }

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

static void provider_get_first_enabled_cb (GObject      *source_object,
                                           GAsyncResult *result,
                                           gpointer      user_data);
static void manager_new_cb (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data);
static void epg_service_startup_complete (EpgService  *self,
                                          GTask       *task);
static void provider_notify_enabled_cb (GObject    *object,
                                        GParamSpec *param_spec,
                                        gpointer    user_data);

static void
async_result_cb (GObject      *obj,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GAsyncResult **result_out = user_data;

  *result_out = g_object_ref (result);
}

static time_t
get_clock_seconds (int clockid)
{
  struct timespec ts;
  if (G_UNLIKELY (clock_gettime (clockid, &ts) != 0))
    g_error ("clock_gettime() failed for clockid %d: %s", clockid, g_strerror (errno));
  return ts.tv_sec;
}

static gboolean
clock_jump_cb (gpointer user_data)
{
  EpgService *self = EPG_SERVICE (user_data);
  gint64 clock_realtime_secs_v1;
  gint64 clock_boottime_secs_v1;
  gint64 clock_jump_delta;

  clock_realtime_secs_v1 = get_clock_seconds (CLOCK_REALTIME);
  clock_boottime_secs_v1 = get_clock_seconds (CLOCK_BOOTTIME);

  clock_jump_delta = ((clock_realtime_secs_v1 - self->clock_realtime_secs_v0) -
                      (clock_boottime_secs_v1 - self->clock_boottime_secs_v0));
  if (clock_jump_delta != 0)
    {
      g_debug ("Detected system clock jump of %" G_GINT64_FORMAT " seconds", clock_jump_delta);
      epg_provider_wallclock_time_changed (self->provider, clock_jump_delta);
    }

  self->clock_realtime_secs_v0 = clock_realtime_secs_v1;
  self->clock_boottime_secs_v0 = clock_boottime_secs_v1;

  return G_SOURCE_CONTINUE;
}

static void
_init_clock_jump_detection (EpgService *self)
{
  g_autoptr(GError) local_error = NULL;

  g_assert (self->provider != NULL);

  self->clock_realtime_secs_v0 = get_clock_seconds (CLOCK_REALTIME);
  self->clock_boottime_secs_v0 = get_clock_seconds (CLOCK_BOOTTIME);

  self->source = epg_clock_jump_source_new (&local_error);
  if (self->source == NULL)
    {
      g_warning ("Error creating EpgClockJumpSource: %s", local_error->message);
    }
  else
    {
      g_source_set_callback (self->source, clock_jump_cb, self, NULL);
      g_source_attach (self->source, NULL);
    }
}

static void
epg_service_set_provider (EpgService *self,
                          EpgProvider *provider) /* transfer full */
{
  g_assert (EPG_IS_SERVICE (self));
  g_assert (EPG_IS_PROVIDER (provider));

  self->provider = provider;
  _init_clock_jump_detection (self);
}

/**
 * epg_service_secure_init_sync:
 * @self: an #EpgService
 * @cancellable: a #GCancellable
 *
 * This function must be executed before gss_service_run(), and does any
 * initialization which requires a secure environment (such as a signed
 * initramfs). For example, this loads .so files of #EpgProvider
 * implementations and reads the system clock so we can inform providers of any
 * discontinuous changes to the clock.
 *
 * In contrast to this, gss_service_run() may be executed in the context of the
 * primary root filesystem, which is not trusted since it may be modified by
 * the user.
 *
 * For backwards compatibility, epg_service_secure_init_sync() can also be
 * executed in the context of the primary root filesystem. In that case it will
 * still initialize providers, but without the same security guarantees.
 */
void
epg_service_secure_init_sync (EpgService   *self,
                              GCancellable *cancellable)
{
  g_autoptr(EpgProviderLoader) loader = NULL;
  g_autoptr(GAsyncResult) load_result = NULL;
  g_autoptr(EpgProvider) provider = NULL;
  g_autoptr(GSource) clock_jump_source = NULL;
  g_autoptr(GError) local_error = NULL;

  g_return_if_fail (EPG_IS_SERVICE (self));

  /* Read EFI variable(s) before the root pivot */
  self->eospayg_active_efivar = (efi_get_variable_exists (EOSPAYG_ACTIVE_GUID, "EOSPAYG_active") == 0);

  /* Look for enabled PAYG providers */
  loader = epg_provider_loader_new (NULL);
  epg_provider_loader_get_first_enabled_async (loader, cancellable,
                                               async_result_cb,
                                               &load_result);

  while (load_result == NULL)
    g_main_context_iteration (NULL, TRUE);

  provider = epg_provider_loader_get_first_enabled_finish (loader, load_result, &local_error);
  if (local_error != NULL)
    {
      g_warning ("%s: Failed to load external providers: %s",
                 G_STRFUNC, local_error->message);
      /* This isn't a fatal error; we will just fall back to using #EpgManager
       * in epg_service_startup_async() */
      return;
    }

  /* We may not find any providers because PAYG is not enabled, or because
   * file-backed state is being used, which will only be found after the root
   * pivot.
   */
  if (provider == NULL)
    g_debug ("%s: No enabled providers found pre-root-pivot");
  else
    epg_service_set_provider (self, g_steal_pointer (&provider));
}

static void
epg_service_startup_async (GssService          *service,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  EpgService *self = EPG_SERVICE (service);
  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_service_startup_async);

  /* Check if a provider was found in epg_service_secure_init_sync() */
  if (self->provider != NULL)
    {
      epg_service_startup_complete (self, task);
      return;
    }

  /* Some deployments are using an external provider (Angaza) with state stored
   * on the main filesystem, which would not have been found in
   * epg_service_secure_init_sync(). So we need to try
   * epg_provider_loader_get_first_enabled_async() again here. */
  g_autoptr(EpgProviderLoader) loader = epg_provider_loader_new (NULL);
  epg_provider_loader_get_first_enabled_async (loader, cancellable,
                                               provider_get_first_enabled_cb,
                                               g_steal_pointer (&task));
}

static void
provider_get_first_enabled_cb (GObject      *source_object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  EpgService *self = EPG_SERVICE (g_task_get_source_object (task));
  EpgProviderLoader *loader = EPG_PROVIDER_LOADER (source_object);
  g_autoptr(EpgProvider) provider = NULL;
  g_autoptr(GError) local_error = NULL;

  provider = epg_provider_loader_get_first_enabled_finish (loader, result, &local_error);
  if (provider != NULL)
    {
      epg_service_set_provider (self, g_steal_pointer (&provider));
      epg_service_startup_complete (self, task);
      return;
    }
  else if (local_error != NULL)
    {
      g_warning ("%s: Failed to load external providers: %s",
                 G_STRFUNC, local_error->message);
      g_clear_error (&local_error);
    }

  /* If the EFI variable EOSPAYG_active is set one of the external providers
   * should have been enabled, so error out otherwise. It would not be safe to
   * fall back to the unsecure #EpgManager but keep it for backward
   * compatibility with Phase 1 systems.
   */
  if (self->eospayg_active_efivar)
    {
      local_error = g_error_new (EPG_SERVICE_ERROR, EPG_SERVICE_ERROR_NO_PROVIDER,
                                 "No PAYG provider is enabled, despite PAYG being active");
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  /* No enabled provider plugin; fall back to #EpgManager. */
  g_debug ("%s: No enabled external providers", G_STRFUNC);

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

  GCancellable *cancellable = g_task_get_cancellable (task);
  epg_manager_new (enabled, NULL, NULL, NULL, cancellable,
                   manager_new_cb, g_steal_pointer (&task));
}

static void
manager_new_cb (GObject      *source_object,
                GAsyncResult *result,
                gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  EpgService *self = EPG_SERVICE (g_task_get_source_object (task));
  g_autoptr(EpgProvider) provider = NULL;
  g_autoptr(GError) local_error = NULL;

  provider = epg_manager_new_finish (result, &local_error);
  if (provider == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  if (!epg_provider_get_enabled (provider))
    {
      /* Neither Endless nor 3rd party PAYG has been provisioned */
      g_debug ("No PAYG providers are enabled, exiting");
      g_task_return_boolean (task, TRUE);
      gss_service_exit (GSS_SERVICE (self), NULL, 0);
    }
  else
    {
      epg_service_set_provider (self, g_steal_pointer (&provider));
      epg_service_startup_complete (self, task);
    }
}

/**
 * epg_service_startup_complete:
 * @self: an #EpgService
 * @task: (transfer none): an epg_service_startup_async() task
 *
 * Completes the startup process, registering @self->provider on the bus and
 * making @task return.
 */
static void
epg_service_startup_complete (EpgService  *self,
                              GTask       *task)
{
  g_autoptr(GError) local_error = NULL;

  g_assert (self->provider != NULL);

  /* Create our D-Bus service. */
  GDBusConnection *connection = gss_service_get_dbus_connection (GSS_SERVICE (self));

  self->manager_service = epg_manager_service_new (connection,
                                                   "/com/endlessm/Payg1",
                                                   self->provider);

  if (!epg_manager_service_register (self->manager_service, &local_error))
    g_task_return_error (task, g_steal_pointer (&local_error));
  else
    g_task_return_boolean (task, TRUE);

  self->notify_enabled_id = g_signal_connect (self->provider,
                                              "notify::enabled",
                                              G_CALLBACK (provider_notify_enabled_cb),
                                              self);
  provider_notify_enabled_cb (G_OBJECT (self->provider), NULL, self);
}

static void
epg_service_startup_finish (GssService    *service,
                            GAsyncResult  *result,
                            GError       **error)
{
  g_task_propagate_boolean (G_TASK (result), error);
}

static void
provider_notify_enabled_cb (GObject    *object,
                            GParamSpec *param_spec,
                            gpointer    user_data)
{
  EpgService *self = EPG_SERVICE (user_data);
  EpgProvider *provider = EPG_PROVIDER (object);
  gboolean enabled = epg_provider_get_enabled (provider);

  if (enabled && !self->holding)
    gss_service_hold (GSS_SERVICE (self));
  else if (!enabled && self->holding)
    gss_service_release (GSS_SERVICE (self));

  self->holding = enabled;
}

static void
epg_service_shutdown (GssService *service)
{
  EpgService *self = EPG_SERVICE (service);
  g_autoptr(GError) local_error = NULL;

  /* Save the provider’s state. */
  g_autoptr(GAsyncResult) result = NULL;
  epg_provider_shutdown_async (self->provider, NULL, async_result_cb, &result);

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  if (!epg_provider_shutdown_finish (self->provider, result, &local_error))
    {
      g_warning ("Error shutting down provider: %s", local_error->message);
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
                       "inactivity-timeout", 0,  /* never timeout */
                       "allow-root", TRUE, /* https://phabricator.endlessm.com/T27037 */
                       "translation-domain", GETTEXT_PACKAGE,
                       "parameter-string", _("— verify top-up codes and monitor time remaining"),
                       "summary", _("Verify inputted top-up codes and "
                                    "monitor the amount of time the computer "
                                    "has remaining before its lease next needs "
                                    "topping up."),
                       NULL);
}
