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
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libeos-payg/manager.h>


/* These errors do go over the bus, and are registered in manager-service.c. */
G_DEFINE_QUARK (EpgManagerError, epg_manager_error)

static void epg_manager_get_property (GObject      *object,
                                      guint         property_id,
                                      GValue        *value,
                                      GParamSpec   *pspec);
static void epg_manager_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec);

/**
 * EpgManager:
 *
 * A manager object which maintains the pay as you go state for the system
 * (mainly the expiry time of the current code), and allows new codes to be
 * entered and validated to extend the expiry time.
 *
 * Since: 0.1.0
 */
struct _EpgManager
{
  GObject parent;

  /* FIXME: Implement this all. */
};

typedef enum
{
  PROP_EXPIRY_TIME = 1,
  PROP_ENABLED,
} EpgManagerProperty;

G_DEFINE_TYPE (EpgManager, epg_manager, G_TYPE_OBJECT)

static void
epg_manager_class_init (EpgManagerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_ENABLED + 1] = { NULL, };

  object_class->get_property = epg_manager_get_property;
  object_class->set_property = epg_manager_set_property;

  /**
   * EpgManager:expiry-time:
   *
   * UNIX timestamp when the current pay as you go code will expire. At this
   * point, it is expected that clients of this service will lock the computer
   * until a new code is entered. Use epg_manager_add_code() to add a new code
   * and extend the expiry time.
   *
   * Since: 0.1.0
   */
  props[PROP_EXPIRY_TIME] =
      g_param_spec_uint64 ("expiry-time", "Expiry Time",
                           "UNIX timestamp when the current pay as you go code "
                           "will expire.",
                           0, G_MAXUINT64, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  /**
   * EpgManager:enabled:
   *
   * Whether pay as you go support is enabled on the system. If this is %FALSE,
   * the #EpgManager:expiry-time can be ignored, and #EpgManager::expired will
   * never be emitted. It is expected that this will be constant for a
   * particular system, only being modified at image configuration time.
   *
   * Since: 0.1.0
   */
  props[PROP_ENABLED] =
      g_param_spec_boolean ("enabled", "Enabled",
                            "Whether pay as you go support is enabled on the system.",
                            FALSE,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);

  /**
   * EpgManager::expired:
   * @self: a #EpgManager
   *
   * Emitted when the #EpgManager:expiry-time is reached, and the current pay as
   * you go code expires. It is expected that when this is emitted, clients of
   * this service will lock the computer until a new code is entered.
   *
   * Since: 0.1.0
   */
  g_signal_new ("expired", G_TYPE_FROM_CLASS (klass),
                G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                G_TYPE_NONE, 0);
}

static void
epg_manager_init (EpgManager *self)
{
  /* Nothing to see here. */
}

static void
epg_manager_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  EpgManager *self = EPG_MANAGER (object);

  switch ((EpgManagerProperty) property_id)
    {
    case PROP_EXPIRY_TIME:
      g_value_set_uint64 (value, epg_manager_get_expiry_time (self));
      break;
    case PROP_ENABLED:
      g_value_set_boolean (value, epg_manager_get_enabled (self));
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
epg_manager_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  switch ((EpgManagerProperty) property_id)
    {
    case PROP_EXPIRY_TIME:
    case PROP_ENABLED:
      /* Read only. */
      g_assert_not_reached ();
      break;
    default:
      g_assert_not_reached ();
    }
}

/**
 * epg_manager_new:
 *
 * Create a new #EpgManager instance, which will load its previous state from
 * disk.
 *
 * Returns: (transfer full): a new #EpgManager
 * Since: 0.1.0
 */
EpgManager *
epg_manager_new (void)
{
  return g_object_new (EPG_TYPE_MANAGER, NULL);
}

/**
 * epg_manager_add_code:
 * @self: an #EpgManager
 * @code: (transfer none): code to validate and add
 * @error: return location for a #GError
 *
 * Validate and add the given @code. This checks that @code is valid, and has
 * not been used already. If so, it will add the time period given in the @code
 * to #EpgManager:expiry-time. If @code fails validation or cannot be added, an
 * error will be returned.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
epg_manager_add_code (EpgManager  *self,
                      GBytes      *code,
                      GError     **error)
{
  g_return_val_if_fail (EPG_IS_MANAGER (self), FALSE);
  g_return_val_if_fail (code != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
               _("FIXME: Adding a code is not implemented yet."));
  return FALSE;
}

/**
 * epg_manager_clear_code:
 * @self: an #EpgManager
 *
 * Clear the current pay as you go code, reset #EpgManager:expiry-time to zero,
 * and cause #EpgManager::expired to be emitted instantly. This is typically
 * intended to be used for testing.
 *
 * Since: 0.1.0
 */
void
epg_manager_clear_code (EpgManager *self)
{
  g_return_if_fail (EPG_IS_MANAGER (self));

  /* FIXME: Implement this. */
}

/**
 * epg_manager_get_expiry_time:
 * @self: a #EpgManager
 *
 * Get the value of #EpgManager:expiry-time.
 *
 * Returns: (transfer none): the UNIX timestamp when the current pay as you go
 *    top up will expire
 * Since: 0.1.0
 */
guint64
epg_manager_get_expiry_time (EpgManager *self)
{
  g_return_val_if_fail (EPG_IS_MANAGER (self), 0);

  /* FIXME: Implement this. */
  return 0;
}

/**
 * epg_manager_get_enabled:
 * @self: a #EpgManager
 *
 * Get the value of #EpgManager:enabled.
 *
 * Returns: (transfer none): %TRUE if pay as you go is enabled, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
epg_manager_get_enabled (EpgManager *self)
{
  g_return_val_if_fail (EPG_IS_MANAGER (self), FALSE);

  /* FIXME: Implement this. */
  return FALSE;
}
