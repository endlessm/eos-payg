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
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libeos-payg/provider.h>

G_DEFINE_INTERFACE (EpgProvider, epg_provider, G_TYPE_ASYNC_INITABLE)

static void
epg_provider_default_init (EpgProviderInterface *iface)
{
  GParamSpec *pspec;

  /**
   * EpgProvider:expiry-time:
   *
   * UNIX timestamp when the current pay as you go credit will expire, in seconds
   * since the epoch. At this point, it is expected that clients of this service
   * will lock the computer until a new code is entered. Use
   * epg_provider_add_code() to add a new code and extend the expiry time.
   *
   * If #EpgProvider:enabled is %FALSE, this will always be zero.
   *
   * Since: 0.2.0
   */
  pspec =
      g_param_spec_uint64 ("expiry-time", "Expiry Time",
                           "UNIX timestamp when the current pay as you go code "
                           "will expire, in seconds since the epoch.",
                           0, G_MAXUINT64, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_interface_install_property (iface, pspec);

  /**
   * EpgProvider:enabled:
   *
   * Whether pay as you go support is enabled on the system. If this is %FALSE,
   * the #EpgProvider:expiry-time can be ignored, #EpgProvider::expired will
   * never be emitted, and all methods will return %EPG_MANAGER_ERROR_DISABLED.
   *
   * Since: 0.2.0
   */
  pspec =
      g_param_spec_boolean ("enabled", "Enabled",
                            "Whether pay as you go support is enabled on the system.",
                            FALSE,
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_interface_install_property (iface, pspec);

  /**
   * EpgProvider:rate-limit-end-time:
   *
   * UNIX timestamp when the rate limit on adding codes will end, in seconds
   * since the epoch. At this point, a new call to epg_provider_add_code() will
   * not immediately result in an %EPG_MANAGER_ERROR_TOO_MANY_ATTEMPTS error.
   *
   * If #EpgProvider:enabled is %FALSE, this will always be zero.
   *
   * Since: 0.2.0
   */
  pspec =
      g_param_spec_uint64 ("rate-limit-end-time", "Rate Limit End Time",
                           "UNIX timestamp when the rate limit on adding codes "
                           "will end, in seconds since the epoch.",
                           0, G_MAXUINT64, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_interface_install_property (iface, pspec);

  /**
   * EpgProvider::expired:
   * @self: a #EpgProvider
   *
   * Emitted when the #EpgProvider:expiry-time is reached, and the current pay as
   * you go code expires. It is expected that when this is emitted, clients of
   * this service will lock the computer until a new code is entered.
   *
   * This will never be emitted when #EpgProvider:enabled is %FALSE.
   *
   * Since: 0.2.0
   */
  g_signal_new ("expired", G_TYPE_FROM_INTERFACE (iface),
                G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                G_TYPE_NONE, 0);
}

/**
 * epg_provider_add_code:
 * @self: an #EpgProvider
 * @code_str: code to verify and add
 * @now_secs: the current time, in seconds since the UNIX epoch, as returned by
 *    (g_get_real_time() / G_USEC_PER_SEC); this is parameterised to allow for
 *    easy testing
 * @error: return location for a #GError
 *
 * Verify and add the given @code_str. This checks that @code_str is valid, and
 * has not been used already. If so, it will add the time period given in the
 * @code_str to #EpgProvider:expiry-time (or to @now_secs if
 * #EpgProvider:expiry-time is in the past). If @code_str fails verification or
 * cannot be added, an error will be returned.
 *
 * Calls to this function may be rate limited: if too many attempts are made within
 * a given time period, %EPG_MANAGER_ERROR_TOO_MANY_ATTEMPTS will be returned
 * until that period expires. The rate limiting history is reset on a successful
 * verification of a code.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.2.0
 */
gboolean
epg_provider_add_code   (EpgProvider  *self,
                         const gchar  *code_str,
                         guint64       now_secs,
                         GError      **error)
{
  g_return_val_if_fail (EPG_IS_PROVIDER (self), FALSE);

  EpgProviderInterface *iface = EPG_PROVIDER_GET_IFACE (self);

  g_assert (iface->add_code != NULL);

  return iface->add_code (self, code_str, now_secs, error);
}

/**
 * epg_provider_clear_code:
 * @self: an #EpgProvider
 * @error: return location for a #GError
 *
 * Clear the current pay as you go code, reset #EpgProvider:expiry-time to zero,
 * and cause #EpgProvider::expired to be emitted instantly. This is typically
 * intended to be used for testing.
 *
 * If pay as you go is disabled, %EPG_MANAGER_ERROR_DISABLED will be returned.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.2.0
 */
gboolean
epg_provider_clear_code (EpgProvider  *self,
                         GError      **error)
{
  g_return_val_if_fail (EPG_IS_PROVIDER (self), FALSE);

  EpgProviderInterface *iface = EPG_PROVIDER_GET_IFACE (self);

  g_assert (iface->clear_code != NULL);

  return iface->clear_code (self, error);
}

/**
 * epg_provider_save_state_async:
 * @self: an #EpgProvider
 * @cancellable: a #GCancellable, or %NULL
 * @callback: function to call once the async operation is complete
 * @user_data: data to pass to @callback
 *
 * Save the state for the #EpgProvider.
 *
 * Since: 0.2.0
 */
void
epg_provider_save_state_async  (EpgProvider         *self,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  g_return_if_fail (EPG_IS_PROVIDER (self));

  EpgProviderInterface *iface = EPG_PROVIDER_GET_IFACE (self);

  g_assert (iface->save_state_async != NULL);

  return iface->save_state_async (self, cancellable, callback, user_data);
}

/**
 * epg_provider_save_state_finish:
 * @self: an #EpgProvider
 * @result: asynchronous operation result
 * @error: return location for an error, or %NULL
 *
 * Finish an asynchronous save operation started with
 * epg_provider_save_state_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.2.0
 */
gboolean
epg_provider_save_state_finish (EpgProvider   *self,
                                GAsyncResult  *result,
                                GError       **error)
{
  g_return_val_if_fail (EPG_IS_PROVIDER (self), FALSE);

  EpgProviderInterface *iface = EPG_PROVIDER_GET_IFACE (self);

  g_assert (iface->save_state_finish != NULL);

  return iface->save_state_finish (self, result, error);
}

/**
 * epg_provider_get_expiry_time:
 * @self: a #EpgProvider
 *
 * Get the value of #EpgProvider:expiry-time.
 *
 * Returns: the UNIX timestamp when the current pay as you go top up will
 *    expire, in seconds since the UNIX epoch
 * Since: 0.2.0
 */
guint64
epg_provider_get_expiry_time (EpgProvider *self)
{
  g_return_val_if_fail (EPG_IS_PROVIDER (self), 0);

  EpgProviderInterface *iface = EPG_PROVIDER_GET_IFACE (self);

  g_assert (iface->get_expiry_time != NULL);

  return iface->get_expiry_time (self);
}

/**
 * epg_provider_get_enabled:
 * @self: a #EpgProvider
 *
 * Get the value of #EpgProvider:enabled.
 *
 * Returns: (transfer none): %TRUE if pay as you go is enabled, %FALSE otherwise
 * Since: 0.2.0
 */
gboolean
epg_provider_get_enabled (EpgProvider *self)
{
  g_return_val_if_fail (EPG_IS_PROVIDER (self), FALSE);

  EpgProviderInterface *iface = EPG_PROVIDER_GET_IFACE (self);

  g_assert (iface->get_enabled != NULL);

  return iface->get_enabled (self);
}

/**
 * epg_provider_get_rate_limit_end_time:
 * @self: a #EpgProvider
 *
 * Get the value of #EpgProvider:rate-limit-end-time.
 *
 * Returns: the UNIX timestamp when the current rate limit on calling
 *    epg_provider_add_code() will reset, in seconds since the UNIX epoch
 * Since: 0.2.0
 */
guint64
epg_provider_get_rate_limit_end_time (EpgProvider *self)
{
  g_return_val_if_fail (EPG_IS_PROVIDER (self), 0);

  EpgProviderInterface *iface = EPG_PROVIDER_GET_IFACE (self);

  g_assert (iface->get_rate_limit_end_time != NULL);

  return iface->get_rate_limit_end_time (self);
}
