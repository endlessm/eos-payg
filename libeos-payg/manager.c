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
#include <libeos-payg-codes/codes.h>


/* These errors do go over the bus, and are registered in manager-service.c. */
G_DEFINE_QUARK (EpgManagerError, epg_manager_error)

static void epg_manager_constructed  (GObject *object);
static void epg_manager_dispose      (GObject *object);

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
 * entered and verified to extend the expiry time.
 *
 * Its state is stored in files in #EpgManager:state-directory. Any integers
 * stored in those files are in host endianness.
 *
 * Since: 0.1.0
 */
struct _EpgManager
{
  GObject parent;

  GArray *used_counters;  /* (element-type EpcCounter) (owned) */
  guint64 expiry_time;  /* UNIX timestamp */
  gboolean enabled;
  GBytes *key_bytes;  /* (owned) */

  GFile *state_directory;  /* (owned) */

  GMainContext *context;  /* (owned) */
  GSource *expiry;  /* (owned) (nullable) */
};

typedef enum
{
  PROP_EXPIRY_TIME = 1,
  PROP_ENABLED,
  PROP_KEY_BYTES,
  PROP_STATE_DIRECTORY,
} EpgManagerProperty;

G_DEFINE_TYPE (EpgManager, epg_manager, G_TYPE_OBJECT)

static void
epg_manager_class_init (EpgManagerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_STATE_DIRECTORY + 1] = { NULL, };

  object_class->constructed = epg_manager_constructed;
  object_class->dispose = epg_manager_dispose;
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
                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * EpgManager:key-bytes:
   *
   * Shared key to verify codes with. These must be the bytes of the key, with
   * no surrounding whitespace or padding. This must be the same key used to
   * generate the codes being verified.
   *
   * Keys must be at least 64 bytes in length, or they will be rejected.
   *
   * Since: 0.1.0
   */
  props[PROP_KEY_BYTES] =
      g_param_spec_boxed ("key-bytes", "Key Bytes",
                          "Shared key to verify codes with.",
                          G_TYPE_BYTES,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * EpgManager:state-directory:
   *
   * Directory to store and load state from. This will typically be something
   * like `/var/lib/eos-payg`, apart from in unit tests. It is used by
   * epg_manager_save_state_async() and epg_manager_load_state_async().
   *
   * Since: 0.1.0
   */
  props[PROP_STATE_DIRECTORY] =
      g_param_spec_object ("state-directory", "State Directory",
                           "Directory to store and load state from.",
                           G_TYPE_FILE,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

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
  /* @used_counters is populated when epg_manager_load_state_async() is called. */
  self->used_counters = g_array_new (FALSE, FALSE, sizeof (EpcCounter));
  self->context = g_main_context_ref_thread_default ();
}

/* Clear the expiry #GSource timer, if it hasn’t been already cleared. */
static void
clear_expiry_timer (EpgManager *self)
{
  if (self->expiry != NULL)
    {
      g_source_destroy (self->expiry);
      g_source_unref (self->expiry);
      self->expiry = NULL;
    }
}

static void
epg_manager_constructed (GObject *object)
{
  EpgManager *self = EPG_MANAGER (object);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (epg_manager_parent_class)->constructed (object);

  /* Ensure all our construct-time properties have been set. */
  g_assert (self->key_bytes != NULL);
  g_assert (self->state_directory != NULL);
}

static void
epg_manager_dispose (GObject *object)
{
  EpgManager *self = EPG_MANAGER (object);

  clear_expiry_timer (self);

  g_clear_pointer (&self->used_counters, g_array_unref);
  g_clear_pointer (&self->key_bytes, g_bytes_unref);
  g_clear_object (&self->state_directory);
  g_clear_pointer (&self->context, g_main_context_unref);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (epg_manager_parent_class)->dispose (object);
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
    case PROP_KEY_BYTES:
      g_value_set_boxed (value, epg_manager_get_key_bytes (self));
      break;
    case PROP_STATE_DIRECTORY:
      g_value_set_object (value, epg_manager_get_state_directory (self));
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
  EpgManager *self = EPG_MANAGER (object);

  switch ((EpgManagerProperty) property_id)
    {
    case PROP_EXPIRY_TIME:
      /* Read only. */
      g_assert_not_reached ();
      break;
    case PROP_ENABLED:
      /* Construct only. */
      self->enabled = g_value_get_boolean (value);
      break;
    case PROP_KEY_BYTES:
      /* Construct only. */
      g_assert (self->key_bytes == NULL);
      self->key_bytes = g_value_dup_boxed (value);
      break;
    case PROP_STATE_DIRECTORY:
      /* Construct only. */
      g_assert (self->state_directory == NULL);
      self->state_directory = g_value_dup_object (value);
      break;
    default:
      g_assert_not_reached ();
    }
}

/**
 * epg_manager_new:
 * @enabled: whether PAYG is enabled; if not, the #EpgManager will return
 *    %EPG_MANAGER_ERROR_DISABLED for all operations
 * @key_bytes: shared key to verify codes with; see #EpgManager:key-bytes
 * @state_directory: (transfer none): directory to load/store state in; see
 *    #EpgManager:state-directory
 *
 * Create a new #EpgManager instance, which will load its previous state from
 * disk when epg_manager_load_state_async() is called.
 *
 * Returns: (transfer full): a new #EpgManager
 * Since: 0.1.0
 */
EpgManager *
epg_manager_new (gboolean  enabled,
                 GBytes   *key_bytes,
                 GFile    *state_directory)
{
  g_return_val_if_fail (key_bytes != NULL, NULL);
  g_return_val_if_fail (G_IS_FILE (state_directory), NULL);

  return g_object_new (EPG_TYPE_MANAGER,
                       "enabled", enabled,
                       "key-bytes", key_bytes,
                       "state-directory", state_directory,
                       NULL);
}

/* Check whether PAYG is enabled. If not, set an error. */
static gboolean
check_enabled (EpgManager  *self,
               GError     **error)
{
  if (!self->enabled)
    {
      g_set_error_literal (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_DISABLED,
                           _("Pay as you go is disabled on this computer."));
      return FALSE;
    }

  return TRUE;
}

/* Check that @counter has not been used yet. If it has, return
 * %EPG_MANAGER_ERROR_CODE_ALREADY_USED; otherwise, return %TRUE. */
static gboolean
check_is_counter_unused (EpgManager  *self,
                         EpcCounter   counter,
                         GError     **error)
{
  /* FIXME: Since the counter list could be long (up to 256 entries), we could
   * use a binary search to speed things up here. The list is guaranteed to be
   * sorted. */
  for (gsize i = 0; i < self->used_counters->len; i++)
    {
      EpcCounter used_counter = g_array_index (self->used_counters, EpcCounter, i);

      if (counter == used_counter)
        {
          g_set_error_literal (error, EPG_MANAGER_ERROR,
                               EPG_MANAGER_ERROR_CODE_ALREADY_USED,
                               _("This pay as you go code has already been used."));
          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
expired_cb (gpointer user_data)
{
  EpgManager *self = EPG_MANAGER (user_data);

  g_signal_emit_by_name (self, "expired");

  return G_SOURCE_REMOVE;
}

/* Set the #EpgManager:expiry-time to `MIN (G_MAXUINT64, @now + @span)` and set
 * the #GSource expiry timer to the new expiry time. */
static void
set_expiry_time (EpgManager *self,
                 guint64     now,
                 guint64     span)
{
  /* FIXME: Our use of g_get_real_time() means PAYG can be avoided by changing
   * the system clock, but there’s no way round that. g_get_monotonic_time() can
   * use a different epoch across reboots. */
  guint64 old_expiry_time = self->expiry_time;

  /* Clamp to the end of time instead of overflowing. */
  self->expiry_time = (now <= G_MAXUINT64 - span) ? now + span : G_MAXUINT64;

  /* Set the expiry timer. g_timeout_source_new_seconds() takes a #guint, and
   * @span is a #guint64. However, the maximum span is 365 days, which is
   * representable in 32 bits. We don’t set a timer for infinite periods. */
  clear_expiry_timer (self);

  if (self->expiry_time != G_MAXUINT64)
    {
      g_assert (span <= (guint64) G_MAXUINT * G_USEC_PER_SEC);
      self->expiry = g_timeout_source_new_seconds (span / G_USEC_PER_SEC);
      g_source_set_callback (self->expiry, expired_cb, self, NULL);
      g_source_attach (self->expiry, self->context);
    }

  if (old_expiry_time != self->expiry_time)
    g_object_notify (G_OBJECT (self), "expiry-time");
}

/* Set the #EpgManager:expiry-time to be the current time (@now), plus the given
 * @period. If @period is %EPC_PERIOD_INFINITE, or if the expiry time would
 * overflow, set the expiry time as high as possible.
 *
 * @now will typically be the return value of g_get_real_time(). It is
 * parameterised to allow easy testing. */
static void
extend_expiry_time (EpgManager *self,
                    guint64     now,
                    EpcPeriod   period)
{
  guint64 span;

  switch (period)
    {
    case EPC_PERIOD_5_SECONDS:
      span = 5 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_1_MINUTE:
      span = 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_5_MINUTES:
      span = 5 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_1_HOUR:
      span = 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_1_DAY:
      span = 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_2_DAYS:
      span = 2 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_3_DAYS:
      span = 3 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_4_DAYS:
      span = 4 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_5_DAYS:
      span = 5 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_6_DAYS:
      span = 6 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_7_DAYS:
      span = 7 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_8_DAYS:
      span = 8 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_9_DAYS:
      span = 9 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_10_DAYS:
      span = 10 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_11_DAYS:
      span = 11 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_12_DAYS:
      span = 12 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_13_DAYS:
      span = 13 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_14_DAYS:
      span = 14 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_30_DAYS:
      span = 30 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_60_DAYS:
      span = 60 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_90_DAYS:
      span = 90 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_120_DAYS:
      span = 120 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_365_DAYS:
      span = 365 * 24 * 60 * 60 * (guint64) G_USEC_PER_SEC;
      break;
    case EPC_PERIOD_INFINITE:
      span = G_MAXUINT64;
      break;
    default:
      g_assert_not_reached ();
    }

  set_expiry_time (self, now, span);
}

static gint
used_counters_sort_cb (gconstpointer a,
                       gconstpointer b)
{
  const EpcCounter *counter_a = a;
  const EpcCounter *counter_b = b;

  return *counter_a - *counter_b;
}

/**
 * epg_manager_add_code:
 * @self: an #EpgManager
 * @code_str: code to verify and add, in a form suitable for parsing with
 *    epc_parse_code()
 * @now: the current time, as returned by g_get_real_time(); this is
 *    parameterised to allow for easy testing
 * @error: return location for a #GError
 *
 * Verify and add the given @code_str. This checks that @code_str is valid, and
 * has not been used already. If so, it will add the time period given in the
 * @code_str to #EpgManager:expiry-time. If @code_str fails verification or
 * cannot be added, an error will be returned.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
epg_manager_add_code (EpgManager   *self,
                      const gchar  *code_str,
                      guint64       now,
                      GError      **error)
{
  g_return_val_if_fail (EPG_IS_MANAGER (self), FALSE);
  g_return_val_if_fail (code_str != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!check_enabled (self, error))
    return FALSE;

  /* FIXME: We need to rate limit attempts here. */

  /* Convert from a string to #EpcCode. */
  EpcCode code;
  if (!epc_parse_code (code_str, &code, error))
    return FALSE;

  /* Parse and verify the code. */
  EpcPeriod period;
  EpcCounter counter;

  if (!epc_verify_code (code, self->key_bytes, &period, &counter, error))
    return FALSE;

  /* Check the counter hasn’t been used before. */
  if (!check_is_counter_unused (self, counter, error))
    return FALSE;

  /* Mark the counter as used. Typically, the sort should be a no-op, as we
   * expect (but don’t require) that counters should be used in order. */
  g_array_append_val (self->used_counters, counter);
  g_array_sort (self->used_counters, used_counters_sort_cb);

  /* Extend the expiry time. */
  extend_expiry_time (self, now, period);

  return TRUE;
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

  if (!check_enabled (self, NULL))
    return;

  if (self->expiry_time != 0)
    {
      self->expiry_time = 0;
      g_object_notify (G_OBJECT (self), "expiry-time");
    }

  clear_expiry_timer (self);
}

/* Get the path of the state file containing the expiry time. */
static GFile *
get_expiry_time_file (EpgManager *self)
{
  return g_file_get_child (self->state_directory, "expiry-time");
}

/* Get the path of the state file containing the set of used counters. */
static GFile *
get_used_counters_file (EpgManager *self)
{
  return g_file_get_child (self->state_directory, "used-counters");
}

static void file_load_cb        (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);
static void file_load_delete_cb (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);

/**
 * epg_manager_load_state_async:
 * @self: an #EpgManager
 * @cancellable: a #GCancellable, or %NULL
 * @callback: function to call once the async operation is complete
 * @user_data: data to pass to @callback
 *
 * Load the state for the #EpgManager from the #EpgManager:state-directory, and
 * overwrite any state currently in memory.
 *
 * Since: 0.1.0
 */
void
epg_manager_load_state_async (EpgManager          *self,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_return_if_fail (EPG_IS_MANAGER (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_manager_load_state_async);
  g_task_set_task_data (task, GUINT_TO_POINTER (3), NULL);

  /* Load the expiry time. */
  g_autoptr(GFile) expiry_time_file = get_expiry_time_file (self);

  g_file_load_contents_async (expiry_time_file, cancellable,
                              file_load_cb, g_object_ref (task));

  /* And the used counters. */
  g_autoptr(GFile) used_counters_file = get_used_counters_file (self);

  g_file_load_contents_async (used_counters_file, cancellable,
                              file_load_cb, g_object_ref (task));

  /* Decrement the pending operation count. */
  guint operation_count = GPOINTER_TO_UINT (g_task_get_task_data (task));
  g_task_set_task_data (task, GUINT_TO_POINTER (--operation_count), NULL);
}

static void
file_load_cb (GObject      *source_object,
              GAsyncResult *result,
              gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  GCancellable *cancellable = g_task_get_cancellable (task);
  EpgManager *self = g_task_get_source_object (task);
  g_autoptr(GError) local_error = NULL;

  guint64 now = g_get_real_time ();

  /* Decrement the pending operation count. */
  guint operation_count = GPOINTER_TO_UINT (g_task_get_task_data (task));
  g_task_set_task_data (task, GUINT_TO_POINTER (--operation_count), NULL);

  /* Handle any error. The first error returned from an operation is propagated;
   * subsequent errors are logged and ignored.
   *
   * If the file is not found, we continue below, but with @data set to %NULL
   * and @data_len set to zero. */
  g_autofree gchar *data = NULL;  /* actually guint8 if it weren’t for strict aliasing */
  gsize data_len = 0;

  if (!g_file_load_contents_finish (file, result, &data, &data_len, NULL, &local_error) &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
      if (g_task_had_error (task))
        g_debug ("%s: Error: %s", G_STRFUNC, local_error->message);
      else
        g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  /* Update the manager’s state. */
  g_autoptr(GFile) expiry_time_file = get_expiry_time_file (self);
  g_autoptr(GFile) used_counters_file = get_used_counters_file (self);

  if (g_file_equal (file, expiry_time_file))
    {
      if (data_len == 0)
        {
          /* No expiry time is set. Expire immediately. */
          set_expiry_time (self, now, 0);
        }
      else
        {
          union
            {
              guint64 u64;
              const gchar u8[8];
            } expiry_time;
          G_STATIC_ASSERT (sizeof (expiry_time.u8) == sizeof (self->expiry_time));

          /* Check the file is the right size. If not, delete it so that we
           * don’t error next time we start. */
          if (data_len != sizeof (expiry_time.u8))
            {
              /* Increment the pending operation count. */
              g_task_set_task_data (task, GUINT_TO_POINTER (++operation_count), NULL);

              g_file_delete_async (file, G_PRIORITY_DEFAULT, cancellable,
                                   file_load_delete_cb, g_object_ref (task));
              return;
            }

          /* Set the expiry time. No other validation is needed on the loaded
           * number, as the entire range of the type is valid. */
          memcpy (expiry_time.u8, data, sizeof (expiry_time.u8));
          set_expiry_time (self, now,
                           (expiry_time.u64 > now) ? expiry_time.u64 - now : 0);
        }
    }
  else if (g_file_equal (file, used_counters_file))
    {
      if (data_len == 0)
        {
          /* No used counters have been stored. Clear previous state anyway. */
          g_array_set_size (self->used_counters, 0);
        }
      else
        {
          /* Load the stored used counters. No additional validation is needed,
           * since each counter is only one byte, and the whole range of the
           * type is valid. */
          G_STATIC_ASSERT (sizeof (EpcCounter) == 1);

          g_array_set_size (self->used_counters, 0);
          g_array_append_vals (self->used_counters,
                               data, data_len / sizeof (EpcCounter));
          g_array_sort (self->used_counters, used_counters_sort_cb);
        }
    }
  else
    {
      g_assert_not_reached ();
    }

  if (operation_count == 0)
    g_task_return_boolean (task, TRUE);
}

static void
file_load_delete_cb (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) local_error = NULL;

  /* Decrement the pending operation count. */
  guint operation_count = GPOINTER_TO_UINT (g_task_get_task_data (task));
  g_task_set_task_data (task, GUINT_TO_POINTER (--operation_count), NULL);

  /* Log any error, but don’t propagate it since we’re already returning an
   * error due to the file being the wrong length. */
  if (!g_file_delete_finish (file, result, &local_error) &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    g_debug ("%s: Error: %s", G_STRFUNC, local_error->message);

  /* Another operation may have finished and returned an error while we were
   * deleting. */
  g_autofree gchar *file_path = g_file_get_path (file);

  if (g_task_had_error (task))
    g_warning (_("State file ‘%s’ was the wrong length."), file_path);
  else
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             _("State file ‘%s’ was the wrong length."),
                             file_path);
}

/**
 * epg_manager_load_state_finish:
 * @self: an #EpgManager
 * @result: asynchronous operation result
 * @error: return location for an error, or %NULL
 *
 * Finish an asynchronous load operation started with
 * epg_manager_load_state_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
epg_manager_load_state_finish (EpgManager    *self,
                               GAsyncResult  *result,
                               GError       **error)
{
  g_return_val_if_fail (EPG_IS_MANAGER (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result, epg_manager_load_state_async), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void file_replace_cb     (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);
static void file_save_delete_cb (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);

/**
 * epg_manager_save_state_async:
 * @self: an #EpgManager
 * @cancellable: a #GCancellable, or %NULL
 * @callback: function to call once the async operation is complete
 * @user_data: data to pass to @callback
 *
 * Save the state for the #EpgManager to the #EpgManager:state-directory.
 *
 * Since: 0.1.0
 */
void
epg_manager_save_state_async (EpgManager          *self,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_return_if_fail (EPG_IS_MANAGER (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_manager_save_state_async);
  g_task_set_task_data (task, GUINT_TO_POINTER (3), NULL);

  /* Save the expiry time. */
  g_autoptr(GFile) expiry_time_file = get_expiry_time_file (self);

  union
    {
      guint64 u64;
      const guint8 u8[8];
    } expiry_time;
  G_STATIC_ASSERT (sizeof (expiry_time.u8) == sizeof (self->expiry_time));
  expiry_time.u64 = self->expiry_time;

  g_autoptr(GBytes) expiry_time_bytes = g_bytes_new (expiry_time.u8,
                                                     sizeof (expiry_time.u8));

  g_file_replace_contents_bytes_async (expiry_time_file,
                                       expiry_time_bytes,
                                       NULL,  /* ETag */
                                       FALSE,  /* no backup */
                                       G_FILE_CREATE_PRIVATE,
                                       cancellable,
                                       file_replace_cb,
                                       g_object_ref (task));

  /* And the used counters, if there are any. Otherwise delete the file. */
  g_autoptr(GFile) used_counters_file = get_used_counters_file (self);

  if (self->used_counters->len > 0)
    {
      g_autoptr(GBytes) used_counters_bytes = g_bytes_new (self->used_counters->data,
                                                           self->used_counters->len);

      g_file_replace_contents_bytes_async (used_counters_file,
                                           used_counters_bytes,
                                           NULL,  /* ETag */
                                           FALSE,  /* no backup */
                                           G_FILE_CREATE_PRIVATE,
                                           cancellable,
                                           file_replace_cb,
                                           g_object_ref (task));
    }
  else
    {
      g_file_delete_async (used_counters_file, G_PRIORITY_DEFAULT,
                           cancellable, file_save_delete_cb, g_object_ref (task));
    }

  /* Decrement the pending operation count. */
  guint operation_count = GPOINTER_TO_UINT (g_task_get_task_data (task));
  g_task_set_task_data (task, GUINT_TO_POINTER (--operation_count), NULL);
}

/* FIXME: Factor out the handling of parallel operations and add to GLib. */
static void
file_replace_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) local_error = NULL;

  /* Decrement the pending operation count. */
  guint operation_count = GPOINTER_TO_UINT (g_task_get_task_data (task));
  g_task_set_task_data (task, GUINT_TO_POINTER (--operation_count), NULL);

  /* Handle any error. The first error returned from an operation is propagated;
   * subsequent errors are logged and ignored. */
  if (!g_file_replace_contents_finish (file, result, NULL, &local_error))
    {
      if (g_task_had_error (task))
        g_debug ("%s: Error: %s", G_STRFUNC, local_error->message);
      else
        g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  if (operation_count == 0)
    g_task_return_boolean (task, TRUE);
}

static void
file_save_delete_cb (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) local_error = NULL;

  /* Decrement the pending operation count. */
  guint operation_count = GPOINTER_TO_UINT (g_task_get_task_data (task));
  g_task_set_task_data (task, GUINT_TO_POINTER (--operation_count), NULL);

  /* Handle any error. The first error returned from an operation is propagated;
   * subsequent errors are logged and ignored. */
  if (!g_file_delete_finish (file, result, &local_error) &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
      if (g_task_had_error (task))
        g_debug ("%s: Error: %s", G_STRFUNC, local_error->message);
      else
        g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  if (operation_count == 0)
    g_task_return_boolean (task, TRUE);
}

/**
 * epg_manager_save_state_finish:
 * @self: an #EpgManager
 * @result: asynchronous operation result
 * @error: return location for an error, or %NULL
 *
 * Finish an asynchronous save operation started with
 * epg_manager_save_state_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
gboolean
epg_manager_save_state_finish (EpgManager    *self,
                               GAsyncResult  *result,
                               GError       **error)
{
  g_return_val_if_fail (EPG_IS_MANAGER (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result, epg_manager_save_state_async), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
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

  return self->expiry_time;
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

  return self->enabled;
}

/**
 * epg_manager_get_key_bytes:
 * @self: a #EpgManager
 *
 * Get the value of #EpgManager:key-bytes.
 *
 * Returns: (transfer none): bytes of the shared key
 * Since: 0.1.0
 */
GBytes *
epg_manager_get_key_bytes (EpgManager *self)
{
  g_return_val_if_fail (EPG_IS_MANAGER (self), NULL);

  return self->key_bytes;
}

/**
 * epg_manager_get_state_directory:
 * @self: a #EpgManager
 *
 * Get the value of #EpgManager:state-directory.
 *
 * Returns: (transfer none): directory containing the manager’s stored state
 * Since: 0.1.0
 */
GFile *
epg_manager_get_state_directory (EpgManager *self)
{
  g_return_val_if_fail (EPG_IS_MANAGER (self), NULL);

  return self->state_directory;
}
