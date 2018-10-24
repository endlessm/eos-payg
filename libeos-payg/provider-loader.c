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

#include <libpeas/peas.h>
#include <libeos-payg/multi-task.h>
#include <libeos-payg/provider-loader.h>
#include <libeos-payg/errors.h>

static void epg_provider_loader_set_property (GObject      *object,
                                              guint         property_id,
                                              const GValue *value,
                                              GParamSpec   *pspec);
static void epg_provider_loader_constructed (GObject *object);
static void epg_provider_loader_dispose (GObject *object);
static void epg_provider_loader_finalize (GObject *object);

/**
 * EpgProviderLoader:
 *
 * Loads plugins which implement the #EpgProvider interface, using libpeas.
 *
 * Since: 0.2.0
 */
struct _EpgProviderLoader
{
  GObject parent;

  gchar *module_dir;  /* (owned) */
  PeasEngine *engine;  /* (owned) */
};

typedef enum
{
  PROP_MODULE_DIR = 1,
} EpgProviderLoaderProperty;

G_DEFINE_TYPE (EpgProviderLoader, epg_provider_loader, G_TYPE_OBJECT)

static void
epg_provider_loader_class_init (EpgProviderLoaderClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_MODULE_DIR + 1] = { NULL, };

  object_class->set_property = epg_provider_loader_set_property;
  object_class->constructed = epg_provider_loader_constructed;
  object_class->dispose = epg_provider_loader_dispose;
  object_class->finalize = epg_provider_loader_finalize;

  /**
   * EpgProviderLoader:module-dir:
   *
   * The path to the directory where plugins implementing EpgProvider can be
   * found. If %NULL or not specified, a compile-time default path is used.
   *
   * Since: 0.2.0
   */
  props[PROP_MODULE_DIR] =
      g_param_spec_string ("module-dir", "Module Dir",
                           "Directory holding EpgProvider plugins",
                           NULL,
                           G_PARAM_WRITABLE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
epg_provider_loader_init (EpgProviderLoader *self)
{
}

static void
epg_provider_loader_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EpgProviderLoader *self = EPG_PROVIDER_LOADER (object);

  switch ((EpgProviderLoaderProperty) property_id)
    {
    case PROP_MODULE_DIR:
      /* Construct only. */
      g_assert (self->module_dir == NULL);
      self->module_dir = g_value_dup_string (value);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
epg_provider_loader_constructed (GObject *object)
{
  EpgProviderLoader *self = EPG_PROVIDER_LOADER (object);

  G_OBJECT_CLASS (epg_provider_loader_parent_class)->constructed (object);

  if (self->module_dir == NULL)
    self->module_dir = g_strdup (g_getenv ("EPG_MODULE_DIR"));

  if (self->module_dir == NULL || self->module_dir[0] == '\0')
    self->module_dir = g_strdup (PLUGINSDIR);

  self->engine = peas_engine_new ();
  peas_engine_add_search_path (self->engine, self->module_dir, NULL);
}

static void
epg_provider_loader_dispose (GObject *object)
{
  EpgProviderLoader *self = EPG_PROVIDER_LOADER (object);

  g_clear_object (&self->engine);

  G_OBJECT_CLASS (epg_provider_loader_parent_class)->dispose (object);
}

static void
epg_provider_loader_finalize (GObject *object)
{
  EpgProviderLoader *self = EPG_PROVIDER_LOADER (object);

  g_clear_pointer (&self->module_dir, g_free);

  G_OBJECT_CLASS (epg_provider_loader_parent_class)->finalize (object);
}

static void try_load (EpgProviderLoader *self,
                      PeasPluginInfo    *plugin_info,
                      GTask             *task);
static void provider_init_cb (GObject      *source_object,
                              GAsyncResult *result,
                              gpointer      user_data);
static void maybe_return (GTask       *task,
                          EpgProvider *provider);

/**
 * epg_provider_loader_load_async:
 * @self: a %EpgProviderLoader
 *
 * Scans installed plugins, and for each one that implements EpgProvider,
 * attempts to construct and initialize it. When @callback is called, use
 * epg_provider_loader_load_finish() to retrieve a (possibly empty) list of
 * providers that were successfully initialized.
 *
 * Since: 0.2.0
 */
void
epg_provider_loader_load_async (EpgProviderLoader   *self,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  g_return_if_fail (EPG_IS_PROVIDER_LOADER (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_provider_loader_load_async);
  g_task_set_task_data (task,
                        g_ptr_array_new_with_free_func (g_object_unref),
                        (GDestroyNotify) g_ptr_array_unref);
  epg_multi_task_attach (task, 1);

  const GList *plugins = peas_engine_get_plugin_list (self->engine);
  for (; plugins != NULL; plugins = plugins->next)
    try_load (self, PEAS_PLUGIN_INFO (plugins->data), task);

  maybe_return (task, NULL);
}

static void
try_load (EpgProviderLoader *self,
          PeasPluginInfo    *plugin_info,
          GTask             *task)
{
  const gchar *name = peas_plugin_info_get_name (plugin_info);
  g_autoptr(PeasExtension) extension = NULL;

  if (!peas_engine_load_plugin (self->engine, plugin_info))
    {
      g_autoptr(GError) local_error = NULL;
      peas_plugin_info_is_available (plugin_info, &local_error);
      g_warning ("Failed to load plugin %s: %s",
                 name,
                 local_error != NULL ? local_error->message : "(unknown reason)");
      return;
    }

  if (!peas_engine_provides_extension (self->engine,
                                       plugin_info,
                                       EPG_TYPE_PROVIDER))
    return;

  extension = peas_engine_create_extension (self->engine,
                                            plugin_info,
                                            EPG_TYPE_PROVIDER,
                                            NULL);
  if (extension == NULL)
    {
      /* Should not happen: we just checked that plugin_info can do this. */
      g_warning ("Failed to create EpgProvider from %s", name);
      return;
    }

  if (!G_IS_ASYNC_INITABLE (extension))
    {
      g_warning ("EpgProvider from %s is not GAsyncInitable", name);
      return;
    }

  epg_multi_task_increment (task);
  g_async_initable_init_async (G_ASYNC_INITABLE (g_steal_pointer (&extension)),
                               g_task_get_priority (task),
                               g_task_get_cancellable (task),
                               provider_init_cb,
                               g_object_ref (task));
}

static void
provider_init_cb (GObject      *source_object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  g_autoptr(EpgProvider) provider = EPG_PROVIDER (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) local_error = NULL;

  if (!g_async_initable_init_finish (G_ASYNC_INITABLE (provider),
                                     result,
                                     &local_error))
    {
      g_warning ("Failed to initialize %s: %s",
                 G_OBJECT_TYPE_NAME (provider),
                 local_error->message);
      maybe_return (task, NULL);
    }
  else
    {
      maybe_return (task, g_steal_pointer (&provider));
    }
}

static void
maybe_return (GTask       *task,
              EpgProvider *provider)
{
  GPtrArray *providers = g_task_get_task_data (task);

  if (provider != NULL)
    g_ptr_array_add (providers, g_steal_pointer (&provider));

  /* We always return success, even if we found >= 1 plugin but all failed to
   * load/initialize for some reason. The policy in this case is the same as
   * the policy when >= 1 plugin is found but none are enabled (namely, load
   * the built-in one), and lives in EpgService.
   */
  epg_multi_task_return_pointer (task,
                                 g_ptr_array_ref (providers),
                                 (GDestroyNotify) g_ptr_array_unref);
}

/**
 * epg_provider_loader_load_finish:
 *
 * Completes a call to epg_provider_loader_load_async().
 *
 * Returns: (transfer full) (element-type EpgProvider): a possibly-empty list
 *  of initialized #EpgProvider objects, in arbitrary order; or %NULL with
 *  @error set on error.
 */
GPtrArray *
epg_provider_loader_load_finish (EpgProviderLoader   *self,
                                 GAsyncResult        *result,
                                 GError             **error)
{
  g_return_val_if_fail (EPG_IS_PROVIDER_LOADER (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static void shutdown_one_cb (GObject      *source_object,
                             GAsyncResult *result,
                             gpointer      user_data);

static void
shutdown_providers_async (EpgProviderLoader   *self,
                          GPtrArray           *providers,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_provider_loader_get_first_enabled_async);
  epg_multi_task_attach (task, providers->len + 1);

  guint i = 0;
  for (i = 0; i < providers->len; i++)
    {
      EpgProvider *provider = EPG_PROVIDER (g_ptr_array_index (providers, i));
      epg_provider_shutdown_async (provider,
                                   g_task_get_cancellable (task),
                                   shutdown_one_cb,
                                   g_object_ref (task));
    }

  epg_multi_task_return_boolean (task, TRUE);
}

static void
shutdown_one_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  EpgProvider *provider = EPG_PROVIDER (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) local_error = NULL;

  if (!epg_provider_shutdown_finish (provider, result, &local_error))
    {
      g_warning ("%s: failed to shut down %s: %s",
                 G_STRFUNC, G_OBJECT_TYPE_NAME (provider),
                 local_error->message);
    }

  epg_multi_task_return_boolean (task, TRUE);
}

static gboolean
shutdown_providers_finish (EpgProviderLoader  *self,
                           GAsyncResult       *result,
                           GError            **error)
{
  g_return_val_if_fail (EPG_IS_PROVIDER_LOADER (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void get_first_enabled_load_cb (GObject      *source_object,
                                       GAsyncResult *result,
                                       gpointer      user_data);
static void get_first_enabled_shutdown_cb (GObject      *source_object,
                                           GAsyncResult *result,
                                           gpointer      user_data);

/**
 * epg_provider_loader_get_first_enabled_async:
 *
 * Looks for an installed plugin which implements EpgProvider and returns %TRUE
 * from epg_provider_get_enabled(). If more than one installed plugin is
 * enabled, an arbitrary one is chosen. When @callback is called, use
 * epg_provider_loader_get_first_enabled_finish() to retrieve the first enabled
 * provider, if any.
 *
 * Since: 0.2.0
 */
void
epg_provider_loader_get_first_enabled_async  (EpgProviderLoader   *self,
                                              GCancellable        *cancellable,
                                              GAsyncReadyCallback  callback,
                                              gpointer             user_data)
{
  g_return_if_fail (EPG_IS_PROVIDER_LOADER (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_provider_loader_get_first_enabled_async);

  epg_provider_loader_load_async (self, cancellable,
                                  get_first_enabled_load_cb,
                                  g_steal_pointer (&task));
}

static void
get_first_enabled_load_cb (GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  EpgProviderLoader *self = EPG_PROVIDER_LOADER (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GPtrArray) providers = NULL;
  g_autoptr(GError) local_error = NULL;

  providers = epg_provider_loader_load_finish (self, result, &local_error);
  if (providers == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  g_autoptr(EpgProvider) provider = NULL;
  guint i;

  /* Find the first enabled external provider, and use that. */
  for (i = 0; i < providers->len; i++)
    {
      provider = g_object_ref (EPG_PROVIDER (g_ptr_array_index (providers, i)));
      if (epg_provider_get_enabled (provider))
        {
          g_debug ("%s: Found enabled external provider %s",
                   G_STRFUNC, G_OBJECT_TYPE_NAME (provider));
          g_ptr_array_remove_index_fast (providers, i);
          break;
        }
      else
        {
          g_debug ("%s: external provider %s is not enabled",
                   G_STRFUNC, G_OBJECT_TYPE_NAME (provider));
          g_clear_object (&provider);
        }
    }

  if (provider != NULL)
    g_task_set_task_data (task, g_steal_pointer (&provider), g_object_unref);

  /* Shut down all providers except the first enabled one (if any). */
  GCancellable *cancellable = g_task_get_cancellable (task);
  shutdown_providers_async (self, providers, cancellable,
                            get_first_enabled_shutdown_cb,
                            g_steal_pointer (&task));
}

static void
get_first_enabled_shutdown_cb (GObject      *source_object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  EpgProviderLoader *self = EPG_PROVIDER_LOADER (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  EpgProvider *provider = EPG_PROVIDER (g_task_get_task_data (task));
  g_autoptr(GError) local_error = NULL;

  if (!shutdown_providers_finish (self, result, &local_error))
    g_warning ("%s: failed to shut down unused providers: %s",
               G_STRFUNC,
               local_error->message);

  if (provider == NULL)
    g_task_return_pointer (task, NULL, NULL);
  else
    g_task_return_pointer (task, g_object_ref (provider), g_object_unref);
}

/**
 * epg_provider_loader_get_first_enabled_finish:
 *
 * Completes a call to epg_provider_loader_get_first_enabled_async().
 *
 * Returns: (transfer full) (nullable): the first enabled #EpgProvider plugin,
 *  or %NULL (with @error unset) if none are enabled, or %NULL with @error set
 *  on error.
 */
EpgProvider *
epg_provider_loader_get_first_enabled_finish (EpgProviderLoader  *self,
                                              GAsyncResult       *result,
                                              GError            **error)
{
  g_return_val_if_fail (EPG_IS_PROVIDER_LOADER (self), NULL);
  g_return_val_if_fail (G_IS_TASK (result), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * epg_provider_loader:
 * @module_dir: path to directory holding provider plugins; see
 *  EpgProviderLoader:module-dir.
 *
 * Create a new #EpgProviderLoader.
 *
 * Returns: (transfer full): a new #EpgProviderLoader
 * Since: 0.2.0
 */
EpgProviderLoader *
epg_provider_loader_new (const gchar *module_dir)
{
  return g_object_new (EPG_TYPE_PROVIDER_LOADER,
                       "module-dir", module_dir,
                       NULL);
}
