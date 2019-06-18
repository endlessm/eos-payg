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
 * All rights reserved.
 */

#include <libeos-payg/tests/plugins/test-provider.h>

static void epg_test_provider_async_initable_iface_init (gpointer g_iface,
                                                         gpointer iface_data);
static void epg_test_provider_provider_iface_init (gpointer g_iface,
                                                   gpointer iface_data);

static void epg_test_provider_get_property (GObject      *object,
                                            guint         property_id,
                                            GValue        *value,
                                            GParamSpec   *pspec);
static void epg_test_provider_set_property (GObject      *object,
                                            guint         property_id,
                                            const GValue *value,
                                            GParamSpec   *pspec);
static void epg_test_provider_constructed  (GObject      *object);

static void epg_test_provider_init_async  (GAsyncInitable      *initable,
                                           int                  priority,
                                           GCancellable        *cancellable,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data);
static gboolean epg_test_provider_init_finish (GAsyncInitable  *initable,
                                               GAsyncResult    *result,
                                               GError         **error);

static gboolean    epg_test_provider_add_code   (EpgProvider  *provider,
                                                 const gchar  *code_str,
                                                 gint64       *time_added,
                                                 GError      **error);
static gboolean    epg_test_provider_clear_code (EpgProvider   *provider,
                                                 GError      **error);

static void        epg_test_provider_shutdown_async  (EpgProvider           *provider,
                                                      GCancellable         *cancellable,
                                                      GAsyncReadyCallback   callback,
                                                      gpointer              user_data);
static gboolean    epg_test_provider_shutdown_finish (EpgProvider           *provider,
                                                      GAsyncResult         *result,
                                                      GError              **error);

static guint64     epg_test_provider_get_expiry_time     (EpgProvider *provider);
static gboolean    epg_test_provider_get_enabled         (EpgProvider *provider);
static guint64     epg_test_provider_get_rate_limit_end_time (EpgProvider *provider);
static EpgClock *  epg_test_provider_get_clock (EpgProvider *provider);

typedef struct
{
  gboolean enabled;
  EpgClock *clock; /* (owned) */
} EpgTestProviderPrivate;

typedef enum
{
  PROP_EXPIRY_TIME = 1,
  PROP_ENABLED,
  PROP_RATE_LIMIT_END_TIME,
  PROP_CODE_FORMAT,
  PROP_CODE_FORMAT_PREFIX,
  PROP_CODE_FORMAT_SUFFIX,
  PROP_CLOCK,
} EpgTestProviderProperty;

G_DEFINE_TYPE_WITH_CODE (EpgTestProvider, epg_test_provider, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (EpgTestProvider);
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                                epg_test_provider_async_initable_iface_init);
                         G_IMPLEMENT_INTERFACE (EPG_TYPE_PROVIDER,
                                                epg_test_provider_provider_iface_init);
                         )

static void
epg_test_provider_class_init (EpgTestProviderClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->get_property = epg_test_provider_get_property;
  object_class->set_property = epg_test_provider_set_property;
  object_class->constructed = epg_test_provider_constructed;

  g_object_class_override_property (object_class, PROP_EXPIRY_TIME, "expiry-time");
  g_object_class_override_property (object_class, PROP_ENABLED, "enabled");
  g_object_class_override_property (object_class, PROP_RATE_LIMIT_END_TIME, "rate-limit-end-time");
  g_object_class_override_property (object_class, PROP_CODE_FORMAT, "code-format");
  g_object_class_override_property (object_class, PROP_CODE_FORMAT_PREFIX, "code-format-prefix");
  g_object_class_override_property (object_class, PROP_CODE_FORMAT_SUFFIX, "code-format-suffix");
  g_object_class_override_property (object_class, PROP_CLOCK, "clock");
}

static void
epg_test_provider_async_initable_iface_init (gpointer g_iface,
                                             gpointer iface_data)
{
  GAsyncInitableIface *iface = g_iface;

  iface->init_async = epg_test_provider_init_async;
  iface->init_finish = epg_test_provider_init_finish;
}

static void
epg_test_provider_provider_iface_init (gpointer g_iface,
                                       gpointer iface_data)
{
  EpgProviderInterface *iface = g_iface;

  iface->add_code = epg_test_provider_add_code;
  iface->clear_code = epg_test_provider_clear_code;
  iface->shutdown_async = epg_test_provider_shutdown_async;
  iface->shutdown_finish = epg_test_provider_shutdown_finish;
  iface->get_expiry_time = epg_test_provider_get_expiry_time;
  iface->get_enabled = epg_test_provider_get_enabled;
  iface->get_rate_limit_end_time = epg_test_provider_get_rate_limit_end_time;
  iface->get_clock = epg_test_provider_get_clock;
  iface->code_format = "";
  iface->code_format_prefix = "";
  iface->code_format_suffix = "";
}

static void
epg_test_provider_init (EpgTestProvider *self)
{
}

static void
epg_test_provider_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EpgTestProvider *self = EPG_TEST_PROVIDER (object);
  EpgProvider *provider = EPG_PROVIDER (self);

  switch ((EpgTestProviderProperty) property_id)
    {
    case PROP_EXPIRY_TIME:
      g_value_set_uint64 (value, epg_provider_get_expiry_time (provider));
      break;
    case PROP_ENABLED:
      g_value_set_boolean (value, epg_provider_get_enabled (provider));
      break;
    case PROP_RATE_LIMIT_END_TIME:
      g_value_set_uint64 (value, epg_provider_get_rate_limit_end_time (provider));
      break;
    case PROP_CODE_FORMAT:
      g_value_set_static_string (value, epg_provider_get_code_format (provider));
      break;
    case PROP_CODE_FORMAT_PREFIX:
      g_value_set_static_string (value, epg_provider_get_code_format_prefix (provider));
      break;
    case PROP_CODE_FORMAT_SUFFIX:
      g_value_set_static_string (value, epg_provider_get_code_format_suffix (provider));
      break;
    case PROP_CLOCK:
      g_value_set_object (value, epg_provider_get_clock (provider));
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
epg_test_provider_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  EpgTestProvider *self = EPG_TEST_PROVIDER (object);
  EpgTestProviderPrivate *priv = epg_test_provider_get_instance_private (self);

  switch ((EpgTestProviderProperty) property_id)
    {
    case PROP_EXPIRY_TIME:
    case PROP_RATE_LIMIT_END_TIME:
    case PROP_CODE_FORMAT:
      /* Read only. */
      g_assert_not_reached ();
      break;
    case PROP_CODE_FORMAT_PREFIX:
      /* Read only. */
      g_assert_not_reached ();
      break;
    case PROP_CODE_FORMAT_SUFFIX:
      /* Read only. */
      g_assert_not_reached ();
      break;
    case PROP_ENABLED:
      /* Construct only. */
      priv->enabled = g_value_get_boolean (value);
      break;
    case PROP_CLOCK:
      /* Construct only. */
      g_assert (priv->clock == NULL);
      priv->clock = g_value_dup_object (value);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
epg_test_provider_constructed (GObject *object)
{
  EpgTestProvider *self = EPG_TEST_PROVIDER (object);
  EpgTestProviderPrivate *priv = epg_test_provider_get_instance_private (self);

  G_OBJECT_CLASS (epg_test_provider_parent_class)->constructed (object);

  const char *type_name = G_OBJECT_TYPE_NAME (self);
  const char *type_name_env = g_getenv (type_name);

  g_debug ("%s=%s", type_name, type_name_env ?: "");
  priv->enabled = 0 == g_strcmp0 (type_name_env, "enabled");
}

static gboolean
epg_test_provider_add_code (EpgProvider  *provider,
                            const gchar  *code_str,
                            gint64       *time_added,
                            GError      **error)
{
  return TRUE;
}

gboolean
epg_test_provider_clear_code (EpgProvider  *provider,
                              GError     **error)
{
  return TRUE;
}

static void
epg_test_provider_init_async (GAsyncInitable      *initable,
                              int                  priority,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  EpgTestProvider *self = EPG_TEST_PROVIDER (initable);

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_test_provider_init_async);
  g_task_set_priority (task, priority);

  g_task_return_boolean (task, TRUE);
}

/*
 * epg_test_provider_init_finish:
 * @initable: an #EpgTestProvider
 * @result: asynchronous operation result
 * @error: return location for an error, or %NULL
 *
 * Finish an asynchronous load operation started with
 * epg_test_provider_init_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
static gboolean
epg_test_provider_init_finish (GAsyncInitable  *initable,
                               GAsyncResult    *result,
                               GError         **error)
{
  g_return_val_if_fail (EPG_IS_TEST_PROVIDER (initable), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, initable), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result, epg_test_provider_init_async), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
epg_test_provider_shutdown_async (EpgProvider         *provider,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  EpgTestProvider *self = EPG_TEST_PROVIDER (provider);

  g_return_if_fail (EPG_IS_TEST_PROVIDER (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_test_provider_shutdown_async);

  g_task_return_boolean (task, TRUE);
}

gboolean
epg_test_provider_shutdown_finish (EpgProvider   *provider,
                                   GAsyncResult  *result,
                                   GError       **error)
{
  EpgTestProvider *self = EPG_TEST_PROVIDER (provider);

  g_return_val_if_fail (EPG_IS_TEST_PROVIDER (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result, epg_test_provider_shutdown_async), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

guint64
epg_test_provider_get_expiry_time (EpgProvider *provider)
{
  EpgTestProvider *self = EPG_TEST_PROVIDER (provider);

  g_return_val_if_fail (EPG_IS_TEST_PROVIDER (self), 0);

  return 0;
}

gboolean
epg_test_provider_get_enabled (EpgProvider *provider)
{
  EpgTestProvider *self = EPG_TEST_PROVIDER (provider);
  EpgTestProviderPrivate *priv = epg_test_provider_get_instance_private (self);

  g_return_val_if_fail (EPG_IS_TEST_PROVIDER (self), FALSE);

  return priv->enabled;
}

static guint64
epg_test_provider_get_rate_limit_end_time (EpgProvider *provider)
{
  EpgTestProvider *self = EPG_TEST_PROVIDER (provider);

  g_return_val_if_fail (EPG_IS_TEST_PROVIDER (self), 0);

  return 0;
}

static EpgClock *
epg_test_provider_get_clock (EpgProvider *provider)
{
  EpgTestProvider *self = EPG_TEST_PROVIDER (provider);

  g_return_val_if_fail (EPG_IS_TEST_PROVIDER (self), NULL);

  EpgTestProviderPrivate *priv = epg_test_provider_get_instance_private (self);

  return priv->clock;
}
