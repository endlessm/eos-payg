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
#include <glib-unix.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <libeos-payg/manager-interface.h>
#include <libeos-payg/manager-service.h>


static void epg_manager_service_dispose      (GObject      *object);
static void epg_manager_service_get_property (GObject      *object,
                                              guint         property_id,
                                              GValue       *value,
                                              GParamSpec   *pspec);
static void epg_manager_service_set_property (GObject      *object,
                                              guint         property_id,
                                              const GValue *value,
                                              GParamSpec   *pspec);

static gchar **epg_manager_service_entry_enumerate (GDBusConnection *connection,
                                                    const gchar     *sender,
                                                    const gchar     *object_path,
                                                    gpointer         user_data);
static GDBusInterfaceInfo **epg_manager_service_entry_introspect (GDBusConnection *connection,
                                                                  const gchar     *sender,
                                                                  const gchar     *object_path,
                                                                  const gchar     *node,
                                                                  gpointer         user_data);
static const GDBusInterfaceVTable *epg_manager_service_entry_dispatch (GDBusConnection *connection,
                                                                       const gchar     *sender,
                                                                       const gchar     *object_path,
                                                                       const gchar     *interface_name,
                                                                       const gchar     *node,
                                                                       gpointer        *out_user_data,
                                                                       gpointer         user_data);

static void epg_manager_service_manager_method_call (GDBusConnection       *connection,
                                                     const gchar           *sender,
                                                     const gchar           *object_path,
                                                     const gchar           *interface_name,
                                                     const gchar           *method_name,
                                                     GVariant              *parameters,
                                                     GDBusMethodInvocation *invocation,
                                                     gpointer               user_data);

static void epg_manager_service_manager_properties_get     (EpgManagerService     *self,
                                                            GDBusConnection       *connection,
                                                            const gchar           *sender,
                                                            GVariant              *parameters,
                                                            GDBusMethodInvocation *invocation);
static void epg_manager_service_manager_properties_set     (EpgManagerService     *self,
                                                            GDBusConnection       *connection,
                                                            const gchar           *sender,
                                                            GVariant              *parameters,
                                                            GDBusMethodInvocation *invocation);
static void epg_manager_service_manager_properties_get_all (EpgManagerService     *self,
                                                            GDBusConnection       *connection,
                                                            const gchar           *sender,
                                                            GVariant              *parameters,
                                                            GDBusMethodInvocation *invocation);
static void epg_manager_service_manager_add_code           (EpgManagerService     *self,
                                                            GDBusConnection       *connection,
                                                            const gchar           *sender,
                                                            GVariant              *parameters,
                                                            GDBusMethodInvocation *invocation);
static void epg_manager_service_manager_clear_code         (EpgManagerService     *self,
                                                            GDBusConnection       *connection,
                                                            const gchar           *sender,
                                                            GVariant              *parameters,
                                                            GDBusMethodInvocation *invocation);

static GVariant *epg_manager_service_manager_get_expiry_time (EpgManagerService     *self,
                                                              GDBusConnection       *connection,
                                                              const gchar           *sender,
                                                              const gchar           *interface_name,
                                                              const gchar           *property_name,
                                                              GDBusMethodInvocation *invocation);
static GVariant *epg_manager_service_manager_get_enabled     (EpgManagerService     *self,
                                                              GDBusConnection       *connection,
                                                              const gchar           *sender,
                                                              const gchar           *interface_name,
                                                              const gchar           *property_name,
                                                              GDBusMethodInvocation *invocation);

static void expired_cb (EpgManager *manager,
                        gpointer    user_data);
static void notify_cb  (GObject    *obj,
                        GParamSpec *pspec,
                        gpointer    user_data);

static const GDBusErrorEntry manager_error_map[] =
  {
    { EPG_MANAGER_ERROR_INVALID_CODE,
      "com.endlessm.Payg1.Error.InvalidCode" },
    { EPG_MANAGER_ERROR_CODE_ALREADY_USED,
      "com.endlessm.Payg1.Error.CodeAlreadyUsed" },
    { EPG_MANAGER_ERROR_TOO_MANY_ATTEMPTS,
      "com.endlessm.Payg1.Error.TooManyAttempts" },
    { EPG_MANAGER_ERROR_DISABLED,
      "com.endlessm.Payg1.Error.Disabled" },
  };
G_STATIC_ASSERT (G_N_ELEMENTS (manager_error_map) == EPG_MANAGER_N_ERRORS);
G_STATIC_ASSERT (G_N_ELEMENTS (manager_error_map) == G_N_ELEMENTS (manager_errors));

/**
 * EpgManagerService:
 *
 * An implementation of a D-Bus interface to expose the pay as you go manager
 * on the bus. This will expose all the necessary objects on the bus for peers
 * to interact with them, and hooks them up to internal state management using
 * #EpgManagerService:manager.
 *
 * Since: 0.1.0
 */
struct _EpgManagerService
{
  GObject parent;

  GDBusConnection *connection;  /* (owned) */
  gchar *object_path;  /* (owned) */
  guint entry_subtree_id;

  /* Used to cancel any pending operations when the object is unregistered. */
  GCancellable *cancellable;  /* (owned) */

  /* Actual implementation of the manager. */
  EpgManager *manager;  /* (owned) */
};

typedef enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_MANAGER,
} EpgManagerServiceProperty;

G_DEFINE_TYPE (EpgManagerService, epg_manager_service, G_TYPE_OBJECT)

static void
epg_manager_service_class_init (EpgManagerServiceClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_MANAGER + 1] = { NULL, };

  object_class->dispose = epg_manager_service_dispose;
  object_class->get_property = epg_manager_service_get_property;
  object_class->set_property = epg_manager_service_set_property;

  /**
   * EpgManagerService:connection:
   *
   * D-Bus connection to export objects on.
   *
   * Since: 0.1.0
   */
  props[PROP_CONNECTION] =
      g_param_spec_object ("connection", "Connection",
                           "D-Bus connection to export objects on.",
                           G_TYPE_DBUS_CONNECTION,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * EpgManagerService:object-path:
   *
   * Object path to root all exported objects at. If this does not end in a
   * slash, one will be added.
   *
   * Since: 0.1.0
   */
  props[PROP_OBJECT_PATH] =
      g_param_spec_string ("object-path", "Object Path",
                           "Object path to root all exported objects at.",
                           "/",
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  /**
   * EpgManagerService:manager:
   *
   * Code verification manager which implements the core of these D-Bus APIs.
   *
   * Since: 0.1.0
   */
  props[PROP_MANAGER] =
      g_param_spec_object ("manager", "Manager",
                           "Code verification manager which implements the "
                           "core of these D-Bus APIs.",
                           EPG_TYPE_MANAGER,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);

  /* Error domain registration for D-Bus. We do this here, rather than in a
   * #GOnce section in epg_manager_error_quark(), to avoid spreading the D-Bus
   * code outside this file. */
  for (gsize i = 0; i < G_N_ELEMENTS (manager_error_map); i++)
    g_dbus_error_register_error (EPG_MANAGER_ERROR,
                                 manager_error_map[i].error_code,
                                 manager_error_map[i].dbus_error_name);
}

static void
epg_manager_service_init (EpgManagerService *self)
{
  self->cancellable = g_cancellable_new ();
}

static void
epg_manager_service_dispose (GObject *object)
{
  EpgManagerService *self = EPG_MANAGER_SERVICE (object);

  g_assert (self->entry_subtree_id == 0);

  g_clear_object (&self->connection);
  g_clear_pointer (&self->object_path, g_free);
  g_clear_object (&self->cancellable);

  if (self->manager != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->manager, notify_cb, self);
      g_signal_handlers_disconnect_by_func (self->manager, expired_cb, self);
    }

  g_clear_object (&self->manager);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (epg_manager_service_parent_class)->dispose (object);
}

static void
epg_manager_service_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EpgManagerService *self = EPG_MANAGER_SERVICE (object);

  switch ((EpgManagerServiceProperty) property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, self->connection);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, self->object_path);
      break;
    case PROP_MANAGER:
      g_value_set_object (value, self->manager);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
epg_manager_service_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EpgManagerService *self = EPG_MANAGER_SERVICE (object);

  switch ((EpgManagerServiceProperty) property_id)
    {
    case PROP_CONNECTION:
      /* Construct only. */
      g_assert (self->connection == NULL);
      self->connection = g_value_dup_object (value);
      break;
    case PROP_OBJECT_PATH:
      /* Construct only. */
      g_assert (self->object_path == NULL);
      g_assert (g_variant_is_object_path (g_value_get_string (value)));
      self->object_path = g_value_dup_string (value);
      break;
    case PROP_MANAGER:
      /* Construct only. */
      g_assert (self->manager == NULL);
      self->manager = g_value_dup_object (value);
      g_signal_connect (self->manager, "expired", (GCallback) expired_cb, self);
      g_signal_connect (self->manager, "notify", (GCallback) notify_cb, self);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
expired_cb (EpgManager *manager,
            gpointer    user_data)
{
  EpgManagerService *self = EPG_MANAGER_SERVICE (user_data);
  g_autoptr(GError) local_error = NULL;

  if (!g_dbus_connection_emit_signal (self->connection,
                                      NULL,  /* broadcast */
                                      self->object_path,
                                      "com.endlessm.Payg1",
                                      "Expired",
                                      NULL,  /* no parameters */
                                      &local_error))
    g_warning ("Failed to emit com.endlessm.Payg1.Expired signal: %s",
               local_error->message);
}

/**
 * epg_manager_service_register:
 * @self: a #EpgManagerService
 * @error: return location for a #GError
 *
 * Register the service objects on D-Bus using the connection details given in
 * #EpgManagerService:connection and #EpgManagerService:object-path.
 *
 * Use epg_manager_service_unregister() to unregister them. Calls to these two
 * functions must be well paired.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
epg_manager_service_register (EpgManagerService  *self,
                              GError            **error)
{
  g_return_val_if_fail (EPG_IS_MANAGER_SERVICE (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  GDBusSubtreeVTable subtree_vtable =
    {
      epg_manager_service_entry_enumerate,
      epg_manager_service_entry_introspect,
      epg_manager_service_entry_dispatch,
    };

  guint id = g_dbus_connection_register_subtree (self->connection,
                                                 self->object_path,
                                                 &subtree_vtable,
                                                 G_DBUS_SUBTREE_FLAGS_NONE,
                                                 g_object_ref (self),
                                                 g_object_unref,
                                                 error);

  if (id == 0)
    return FALSE;

  self->entry_subtree_id = id;

  return TRUE;
}

/**
 * epg_manager_service_unregister:
 * @self: a #EpgManagerService
 *
 * Unregister objects from D-Bus which were previously registered using
 * epg_manager_service_register(). Calls to these two functions must be well
 * paired.
 *
 * Since: 0.1.0
 */
void
epg_manager_service_unregister (EpgManagerService *self)
{
  g_return_if_fail (EPG_IS_MANAGER_SERVICE (self));

  g_dbus_connection_unregister_subtree (self->connection,
                                        self->entry_subtree_id);
  self->entry_subtree_id = 0;
}

static gchar **
epg_manager_service_entry_enumerate (GDBusConnection *connection,
                                     const gchar     *sender,
                                     const gchar     *object_path,
                                     gpointer         user_data)
{
  /* Don’t implement any permissions checks here, as they should be specific to
   * the APIs being called and objects being accessed. */

  /* Output an empty list of paths: we only have a root object. */
  g_autoptr(GPtrArray) paths = NULL;  /* (element-type utf8) */
  paths = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (paths, NULL);  /* terminator */

  return (gchar **) g_ptr_array_free (g_steal_pointer (&paths), FALSE);
}

static GDBusInterfaceInfo **
epg_manager_service_entry_introspect (GDBusConnection *connection,
                                      const gchar     *sender,
                                      const gchar     *object_path,
                                      const gchar     *node,
                                      gpointer         user_data)
{
  g_autofree GDBusInterfaceInfo **interfaces = NULL;

  /* Don’t implement any permissions checks here, as they should be specific to
   * the APIs being called and objects being accessed. */

  if (node == NULL)
    {
      /* The root node implements the manager only. */
      interfaces = g_new0 (GDBusInterfaceInfo *, 2);
      interfaces[0] = (GDBusInterfaceInfo *) &manager_interface;
      interfaces[1] = NULL;
    }

  return g_steal_pointer (&interfaces);
}

static const GDBusInterfaceVTable *
epg_manager_service_entry_dispatch (GDBusConnection *connection,
                                    const gchar     *sender,
                                    const gchar     *object_path,
                                    const gchar     *interface_name,
                                    const gchar     *node,
                                    gpointer        *out_user_data,
                                    gpointer         user_data)
{
  static const GDBusInterfaceVTable manager_interface_vtable =
    {
      epg_manager_service_manager_method_call,
      NULL,  /* handled in epg_manager_service_manager_method_call() */
      NULL,  /* handled in epg_manager_service_manager_method_call() */
    };

  /* Don’t implement any permissions checks here, as they should be specific to
   * the APIs being called and objects being accessed. */

  /* Manager is implemented on the root of the tree. */
  if (node == NULL &&
      g_str_equal (interface_name, "com.endlessm.Payg1"))
    {
      *out_user_data = user_data;
      return &manager_interface_vtable;
    }

  /* Currently no other interfaces or objects implemented. */
  return NULL;
}

static gboolean
validate_dbus_interface_name (GDBusMethodInvocation *invocation,
                              const gchar           *interface_name)
{
  if (!g_dbus_is_interface_name (interface_name))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_UNKNOWN_INTERFACE,
                                             _("Invalid interface name ‘%s’."),
                                             interface_name);
      return FALSE;
    }

  return TRUE;
}

typedef void (*ManagerMethodCallFunc) (EpgManagerService     *self,
                                       GDBusConnection       *connection,
                                       const gchar           *sender,
                                       GVariant              *parameters,
                                       GDBusMethodInvocation *invocation);

static const struct
  {
    const gchar *interface_name;
    const gchar *method_name;
    ManagerMethodCallFunc func;
  }
manager_methods[] =
  {
    /* Handle properties. */
    { "org.freedesktop.DBus.Properties", "Get",
      epg_manager_service_manager_properties_get },
    { "org.freedesktop.DBus.Properties", "Set",
      epg_manager_service_manager_properties_set },
    { "org.freedesktop.DBus.Properties", "GetAll",
      epg_manager_service_manager_properties_get_all },

    /* Manager methods. */
    { "com.endlessm.Payg1", "AddCode",
      epg_manager_service_manager_add_code },
    { "com.endlessm.Payg1", "ClearCode",
      epg_manager_service_manager_clear_code },
  };

G_STATIC_ASSERT (G_N_ELEMENTS (manager_methods) ==
                 G_N_ELEMENTS (manager_interface_methods) +
                 -1  /* NULL terminator */ +
                 3  /* o.fdo.DBus.Properties */);

static void
epg_manager_service_manager_method_call (GDBusConnection       *connection,
                                         const gchar           *sender,
                                         const gchar           *object_path,
                                         const gchar           *interface_name,
                                         const gchar           *method_name,
                                         GVariant              *parameters,
                                         GDBusMethodInvocation *invocation,
                                         gpointer               user_data)
{
  EpgManagerService *self = EPG_MANAGER_SERVICE (user_data);

  /* FIXME: Add permissions checks? This is the right place to add them.
   * Currently, we rely on D-Bus policy allowing/preventing access from
   * appropriate peers. */

  /* Remove the service prefix from the path. */
  g_assert (g_str_equal (object_path, self->object_path));

  /* Work out which method to call. */
  for (gsize i = 0; i < G_N_ELEMENTS (manager_methods); i++)
    {
      if (g_str_equal (manager_methods[i].interface_name, interface_name) &&
          g_str_equal (manager_methods[i].method_name, method_name))
        {
          manager_methods[i].func (self, connection, sender,
                                   parameters, invocation);
          return;
        }
    }

  /* Make sure we actually called a method implementation. GIO guarantees that
   * this function is only called with methods we’ve declared in the interface
   * info, so this should never fail. */
  g_assert_not_reached ();
}

typedef GVariant *(*ManagerPropertyGetFunc) (EpgManagerService     *self,
                                             GDBusConnection       *connection,
                                             const gchar           *sender,
                                             const gchar           *interface_name,
                                             const gchar           *property_name,
                                             GDBusMethodInvocation *invocation);
typedef void      (*ManagerPropertySetFunc) (EpgManagerService     *self,
                                             GDBusConnection       *connection,
                                             const gchar           *sender,
                                             const gchar           *interface_name,
                                             const gchar           *property_name,
                                             GVariant              *value,
                                             GDBusMethodInvocation *invocation);

static const struct
  {
    const gchar *interface_name;
    const gchar *property_name;
    const gchar *object_property_name;
    ManagerPropertyGetFunc get_func;
    ManagerPropertySetFunc set_func;
  }
manager_properties[] =
  {
    /* Manager properties. */
    { "com.endlessm.Payg1", "ExpiryTime", "expiry-time",
      epg_manager_service_manager_get_expiry_time, NULL  /* read-only */ },
    { "com.endlessm.Payg1", "Enabled", "enabled",
      epg_manager_service_manager_get_enabled, NULL  /* read-only */ },
  };

G_STATIC_ASSERT (G_N_ELEMENTS (manager_properties) ==
                 G_N_ELEMENTS (manager_interface_properties) +
                 -1  /* NULL terminator */);

static void
epg_manager_service_manager_properties_get (EpgManagerService     *self,
                                            GDBusConnection       *connection,
                                            const gchar           *sender,
                                            GVariant              *parameters,
                                            GDBusMethodInvocation *invocation)
{
  const gchar *interface_name, *property_name;
  g_variant_get (parameters, "(&s&s)", &interface_name, &property_name);

  /* D-Bus property names can be anything. */
  if (!validate_dbus_interface_name (invocation, interface_name))
    return;

  /* Work out which property to get. */
  for (gsize i = 0; i < G_N_ELEMENTS (manager_properties); i++)
    {
      if (g_str_equal (manager_properties[i].interface_name, interface_name) &&
          g_str_equal (manager_properties[i].property_name, property_name))
        {
          g_autoptr(GVariant) value = NULL;
          value = manager_properties[i].get_func (self, connection, sender,
                                                  interface_name, property_name,
                                                  invocation);
          g_variant_ref_sink (value);
          g_dbus_method_invocation_return_value (invocation,
                                                 g_variant_new ("(v)", value));
          return;
        }
    }

  g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                         G_DBUS_ERROR_UNKNOWN_PROPERTY,
                                         _("Unknown property ‘%s.%s’."),
                                         interface_name, property_name);
}

static void
epg_manager_service_manager_properties_set (EpgManagerService     *self,
                                            GDBusConnection       *connection,
                                            const gchar           *sender,
                                            GVariant              *parameters,
                                            GDBusMethodInvocation *invocation)
{
  const gchar *interface_name, *property_name;
  g_autoptr(GVariant) value = NULL;
  g_variant_get (parameters, "(&s&sv)", &interface_name, &property_name, &value);

  /* D-Bus property names can be anything. */
  if (!validate_dbus_interface_name (invocation, interface_name))
    return;

  /* Work out which property to set. */
  for (gsize i = 0; i < G_N_ELEMENTS (manager_properties); i++)
    {
      if (g_str_equal (manager_properties[i].interface_name, interface_name) &&
          g_str_equal (manager_properties[i].property_name, property_name))
        {
          if (manager_properties[i].set_func != NULL)
            manager_properties[i].set_func (self, connection, sender,
                                            interface_name, property_name,
                                            value, invocation);
          else
            g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                                   G_DBUS_ERROR_PROPERTY_READ_ONLY,
                                                   _("Property ‘%s.%s’ is read-only."),
                                                   interface_name, property_name);
          return;
        }
    }

  g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                         G_DBUS_ERROR_UNKNOWN_PROPERTY,
                                         _("Unknown property ‘%s.%s’."),
                                         interface_name, property_name);
}

static void
epg_manager_service_manager_properties_get_all (EpgManagerService     *self,
                                                GDBusConnection       *connection,
                                                const gchar           *sender,
                                                GVariant              *parameters,
                                                GDBusMethodInvocation *invocation)
{
  const gchar *interface_name;
  g_variant_get (parameters, "(&s)", &interface_name);

  if (!validate_dbus_interface_name (invocation, interface_name))
    return;

  /* Try the interface. */
  if (!g_str_equal (interface_name, "com.endlessm.Payg1"))
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_UNKNOWN_INTERFACE,
                                             _("Unknown interface ‘%s’."),
                                             interface_name);
      return;
    }

  /* Return all the properties from that interface. */
  g_auto(GVariantDict) dict = G_VARIANT_DICT_INIT (NULL);

  for (gsize i = 0; i < G_N_ELEMENTS (manager_properties); i++)
    {
      const gchar *property_name = manager_properties[i].property_name;

      if (!g_str_equal (manager_properties[i].interface_name, interface_name))
        continue;

      g_autoptr(GVariant) value = NULL;
      value = manager_properties[i].get_func (self, connection, sender,
                                              interface_name, property_name,
                                              invocation);
      g_variant_dict_insert_value (&dict, property_name, g_steal_pointer (&value));
    }

  g_autoptr(GVariant) dict_variant = g_variant_dict_end (&dict);
  g_variant_ref_sink (dict_variant);
  g_dbus_method_invocation_return_value (invocation,
                                         g_variant_new ("(@a{sv})", dict_variant));
}

static void
notify_cb (GObject    *obj,
           GParamSpec *pspec,
           gpointer    user_data)
{
  EpgManagerService *self = EPG_MANAGER_SERVICE (user_data);
  g_autoptr(GError) local_error = NULL;

  /* Find the property being notified. */
  gsize i;
  for (i = 0; i < G_N_ELEMENTS (manager_properties); i++)
    {
      const gchar *object_property_name = manager_properties[i].object_property_name;

      if (g_str_equal (g_param_spec_get_name (pspec), object_property_name))
        break;
    }

  if (i == G_N_ELEMENTS (manager_properties))
    {
      g_debug ("%s: Couldn’t find D-Bus property matching EpgManager:%s; ignoring.",
               G_STRFUNC, g_param_spec_get_name (pspec));
      return;
    }

  /* Emit PropertiesChanged for it. */
  g_autoptr(GVariant) value = NULL;
  value = manager_properties[i].get_func (self, self->connection, NULL,
                                          manager_properties[i].interface_name,
                                          manager_properties[i].property_name,
                                          NULL);
  g_variant_ref_sink (value);

  g_auto(GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("(sa{sv}as)"));
  g_variant_builder_add (&builder, "s", manager_properties[i].interface_name);

  g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder, "{sv}",
                         manager_properties[i].property_name, value);
  g_variant_builder_close (&builder);

  g_variant_builder_open (&builder, G_VARIANT_TYPE ("as"));
  /* no invalidated properties */
  g_variant_builder_close (&builder);

  g_autoptr(GVariant) parameters = g_variant_builder_end (&builder);

  if (!g_dbus_connection_emit_signal (self->connection,
                                      NULL,  /* broadcast */
                                      self->object_path,
                                      "org.freedesktop.DBus.Properties",
                                      "PropertiesChanged",
                                      g_steal_pointer (&parameters),
                                      &local_error))
    g_warning ("Failed to emit org.freedesktop.DBus.Properties.Properties signal: %s",
               local_error->message);
}

static GVariant *
epg_manager_service_manager_get_expiry_time (EpgManagerService     *self,
                                             GDBusConnection       *connection,
                                             const gchar           *sender,
                                             const gchar           *interface_name,
                                             const gchar           *property_name,
                                             GDBusMethodInvocation *invocation)
{
  return g_variant_new_uint64 (epg_manager_get_expiry_time (self->manager));
}

static GVariant *
epg_manager_service_manager_get_enabled (EpgManagerService     *self,
                                         GDBusConnection       *connection,
                                         const gchar           *sender,
                                         const gchar           *interface_name,
                                         const gchar           *property_name,
                                         GDBusMethodInvocation *invocation)
{
  return g_variant_new_boolean (epg_manager_get_enabled (self->manager));
}

static void
epg_manager_service_manager_add_code (EpgManagerService     *self,
                                      GDBusConnection       *connection,
                                      const gchar           *sender,
                                      GVariant              *parameters,
                                      GDBusMethodInvocation *invocation)
{
  g_autoptr(GError) local_error = NULL;

  const gchar *code_str;
  g_variant_get (parameters, "(&s)", &code_str);

  guint64 now_secs = g_get_real_time () / G_USEC_PER_SEC;
  epg_manager_add_code (self->manager, code_str, now_secs, &local_error);

  if (local_error != NULL)
    g_dbus_method_invocation_return_gerror (invocation, local_error);
  else
    g_dbus_method_invocation_return_value (invocation, NULL);
}

static void
epg_manager_service_manager_clear_code (EpgManagerService     *self,
                                        GDBusConnection       *connection,
                                        const gchar           *sender,
                                        GVariant              *parameters,
                                        GDBusMethodInvocation *invocation)
{
  g_autoptr(GError) local_error = NULL;

  epg_manager_clear_code (self->manager, &local_error);

  if (local_error != NULL)
    g_dbus_method_invocation_return_gerror (invocation, local_error);
  else
    g_dbus_method_invocation_return_value (invocation, NULL);
}

/**
 * epg_manager_service_new:
 * @connection: (transfer none): D-Bus connection to export objects on
 * @object_path: root path to export objects below; must be a valid D-Bus object
 *    path
 * @manager: (transfer none): manager implementing the core of code verification
 *
 * Create a new #EpgManagerService instance which is set up to run as a
 * service.
 *
 * Returns: (transfer full): a new #EpgManagerService
 * Since: 0.1.0
 */
EpgManagerService *
epg_manager_service_new (GDBusConnection *connection,
                         const gchar     *object_path,
                         EpgManager      *manager)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  g_return_val_if_fail (g_variant_is_object_path (object_path), NULL);
  g_return_val_if_fail (EPG_IS_MANAGER (manager), NULL);

  return g_object_new (EPG_TYPE_MANAGER_SERVICE,
                       "connection", connection,
                       "object-path", object_path,
                       "manager", manager,
                       NULL);
}
