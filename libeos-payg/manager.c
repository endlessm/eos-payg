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
#include <libeos-payg/errors.h>
#include <libeos-payg/manager.h>
#include <libeos-payg/real-clock.h>
#include <libeos-payg/multi-task.h>
#include <libeos-payg-codes/codes.h>


static void epg_manager_async_initable_iface_init (gpointer g_iface,
                                                   gpointer iface_data);
static void epg_manager_provider_iface_init (gpointer g_iface,
                                             gpointer iface_data);

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

static void epg_manager_init_async  (GAsyncInitable      *initable,
                                     int                  priority,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data);
static gboolean epg_manager_init_finish (GAsyncInitable  *initable,
                                         GAsyncResult    *result,
                                         GError         **error);

static gboolean    epg_manager_add_code   (EpgProvider  *provider,
                                           const gchar  *code_str,
                                           GError      **error);
static gboolean    epg_manager_clear_code (EpgProvider  *provider,
                                           GError      **error);

static void        epg_manager_save_state_async  (EpgProvider          *provider,
                                                  GCancellable         *cancellable,
                                                  GAsyncReadyCallback   callback,
                                                  gpointer              user_data);
static gboolean    epg_manager_save_state_finish (EpgProvider          *provider,
                                                  GAsyncResult         *result,
                                                  GError              **error);

static void        epg_manager_shutdown_async  (EpgProvider          *provider,
                                                GCancellable         *cancellable,
                                                GAsyncReadyCallback   callback,
                                                gpointer              user_data);
static gboolean    epg_manager_shutdown_finish (EpgProvider          *provider,
                                                GAsyncResult         *result,
                                                GError              **error);

static void        internal_save_state_cb (GObject      *source_object,
                                           GAsyncResult *result,
                                           gpointer      user_data);
static void        shutdown_save_state_cb (GObject      *source_object,
                                           GAsyncResult *result,
                                           gpointer      user_data);

static guint64     epg_manager_get_expiry_time     (EpgProvider *provider);
static gboolean    epg_manager_get_enabled         (EpgProvider *provider);
static guint64     epg_manager_get_rate_limit_end_time (EpgProvider *provider);
static EpgClock *  epg_manager_get_clock (EpgProvider *provider);

/* Struct for storing the values in a used-codes file. The alignment and
 * size of this struct are file format ABI, and must be kept the same. */
typedef struct
{
  guint8 counter;
  guint8 period;
} UsedCode;

G_STATIC_ASSERT (sizeof (UsedCode) == 2);
G_STATIC_ASSERT (offsetof (UsedCode, counter) == 0);
G_STATIC_ASSERT (offsetof (UsedCode, period) == 1);

/* Limit calls to epg_manager_add_code() to 10 attempts every 30 minutes. These
 * values are not arbitrary, and are an inherent part of the security of the
 * codes in libeos-payg-codes against brute force attacks. By rate limiting at
 * this level, we can probabilistically say that brute force attacks will take
 * longer than the period of the code they would reveal, assuming codes have
 * an average period of 1 week. */
#define RATE_LIMITING_N_ATTEMPTS 10
#define RATE_LIMITING_TIME_PERIOD_SECS (30 * 60)

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
 * The `used-codes` state file stores pairs of #EpcCounter and #EpcPeriod,
 * rather than full #EpcCodes, to make it a bit harder for users to modify the
 * file to give themselves use of a code again. It also halves the storage
 * size requirements (although they are not a large concern). The file format
 * is a serialised array of `UsedCode` instances.
 *
 * Since: 0.1.0
 */
struct _EpgManager
{
  GObject parent;

  /* Used to cancel any pending asynchronous operations when we are disposed. */
  GCancellable *cancellable;  /* (owned) */

  GArray *used_codes;  /* (element-type UsedCode) (owned) */
  guint64 expiry_time_secs;  /* Timestamp in seconds based on CLOCK_BOOTTIME */
  gboolean enabled;
  GFile *key_file;  /* (owned) */
  GBytes *key_bytes;  /* (owned) */
  EpgClock *clock; /* (owned) */

  GFile *state_directory;  /* (owned) */

  GMainContext *context;  /* (owned) */
  GSource *expiry;  /* (owned) (nullable) */

  guint64 last_save_time_secs; /* wallclock timestamp of last state save */
  guint64 last_save_expiry_secs; /* seconds left to expiration at time of last state save */
  gboolean last_save_time_secs_set; /* whether last_save_time_secs has been set */
  gboolean last_save_expiry_secs_set; /* whether last_save_expiry_secs has been set */

  /* Rate limiting history. This is a FIFO queue of CLOCK_BOOTTIME timestamps
   * (in seconds) of recent epg_manager_add_code() attempts. See
   * check_rate_limiting(). */
  guint64 rate_limiting_history[RATE_LIMITING_N_ATTEMPTS];
  guint64 rate_limit_end_time_secs;

  /* Number of internal calls to epg_manager_save_state_async() in flight */
  guint64 pending_internal_save_state_calls;

  /* (owned) (nullable): epg_provider_shutdown_async() task to resume once
   * pending_internal_save_state_calls reaches 0
   */
  GTask *pending_shutdown;
};

typedef enum
{
  /* Local properties */
  PROP_KEY_FILE = 1,
  PROP_STATE_DIRECTORY,

  /* Properties inherited from EpgProvider */
  PROP_EXPIRY_TIME,
  PROP_ENABLED,
  PROP_RATE_LIMIT_END_TIME,
  PROP_CODE_FORMAT,
  PROP_CLOCK,
} EpgManagerProperty;

G_DEFINE_TYPE_WITH_CODE (EpgManager, epg_manager, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                                epg_manager_async_initable_iface_init);
                         G_IMPLEMENT_INTERFACE (EPG_TYPE_PROVIDER,
                                                epg_manager_provider_iface_init);
                         )

static void
epg_manager_class_init (EpgManagerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *props[PROP_STATE_DIRECTORY + 1] = { NULL, };

  object_class->constructed = epg_manager_constructed;
  object_class->dispose = epg_manager_dispose;
  object_class->get_property = epg_manager_get_property;
  object_class->set_property = epg_manager_set_property;

  g_object_class_override_property (object_class, PROP_EXPIRY_TIME, "expiry-time");
  g_object_class_override_property (object_class, PROP_ENABLED, "enabled");
  g_object_class_override_property (object_class, PROP_RATE_LIMIT_END_TIME, "rate-limit-end-time");
  g_object_class_override_property (object_class, PROP_CODE_FORMAT, "code-format");
  g_object_class_override_property (object_class, PROP_CLOCK, "clock");

  /**
   * EpgManager:key-file:
   *
   * File containing shared key to verify codes with, with
   * no surrounding whitespace or padding. This must be the same key used to
   * generate the codes being verified.
   *
   * Keys must be at least %EPC_KEY_MINIMUM_LENGTH_BYTES bytes in length, or
   * they will be rejected.
   *
   * A system-wide path is used if this property is not specified or is %NULL.
   * Only unit tests should need to override this path.
   *
   * Since: 0.2.0
   */
  props[PROP_KEY_FILE] =
      g_param_spec_object ("key-file", "Key File",
                           "File containing shared key to verify codes with.",
                           G_TYPE_FILE,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * EpgManager:state-directory:
   *
   * Directory to store and load state from. It is used by
   * epg_manager_new() and epg_manager_save_state_async().
   *
   * A system-wide path is used if this property is not specified or is %NULL.
   * Only unit tests should need to override this path.
   *
   * Since: 0.1.0
   */
  props[PROP_STATE_DIRECTORY] =
      g_param_spec_object ("state-directory", "State Directory",
                           "Directory to store and load state from.",
                           G_TYPE_FILE,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
epg_manager_async_initable_iface_init (gpointer g_iface,
                                       gpointer iface_data)
{
  GAsyncInitableIface *iface = g_iface;

  iface->init_async = epg_manager_init_async;
  iface->init_finish = epg_manager_init_finish;
}

static void
epg_manager_provider_iface_init (gpointer g_iface,
                                 gpointer iface_data)
{
  EpgProviderInterface *iface = g_iface;

  iface->add_code = epg_manager_add_code;
  iface->clear_code = epg_manager_clear_code;
  iface->shutdown_async = epg_manager_shutdown_async;
  iface->shutdown_finish = epg_manager_shutdown_finish;
  iface->get_expiry_time = epg_manager_get_expiry_time;
  iface->get_enabled = epg_manager_get_enabled;
  iface->get_rate_limit_end_time = epg_manager_get_rate_limit_end_time;
  iface->get_clock = epg_manager_get_clock;

  iface->code_format = "^[0-9]{8}$";
}

static void
epg_manager_init (EpgManager *self)
{
  /* @used_codes is populated when epg_manager_init_async() is called. */
  self->used_codes = g_array_new (FALSE, FALSE, sizeof (UsedCode));
  self->context = g_main_context_ref_thread_default ();
  self->cancellable = g_cancellable_new ();
  self->last_save_time_secs_set = FALSE;
  self->last_save_expiry_secs_set = FALSE;
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

  /* Set defaults for construct-time properties. */
  if (self->key_file == NULL)
    self->key_file = g_file_new_for_path (PREFIX "/local/share/eos-payg/key");

  if (self->state_directory == NULL)
    self->state_directory = g_file_new_for_path (LOCALSTATEDIR "/lib/eos-payg");

  if (self->clock == NULL)
    self->clock = EPG_CLOCK (epg_real_clock_new ());
}

static void
epg_manager_dispose (GObject *object)
{
  EpgManager *self = EPG_MANAGER (object);

  /* Cancel any outstanding save operations.
   * FIXME: This will never actually be reached, since a save_state_async()
   * holds a strong reference on the #EpgManager. */
  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  clear_expiry_timer (self);

  g_clear_pointer (&self->used_codes, g_array_unref);
  g_clear_pointer (&self->key_bytes, g_bytes_unref);
  g_clear_object (&self->key_file);
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
  EpgProvider *provider = EPG_PROVIDER (self);

  switch ((EpgManagerProperty) property_id)
    {
    case PROP_EXPIRY_TIME:
      g_value_set_uint64 (value, epg_provider_get_expiry_time (provider));
      break;
    case PROP_ENABLED:
      g_value_set_boolean (value, epg_provider_get_enabled (provider));
      break;
    case PROP_KEY_FILE:
      g_value_set_object (value, epg_manager_get_key_file (self));
      break;
    case PROP_STATE_DIRECTORY:
      g_value_set_object (value, epg_manager_get_state_directory (self));
      break;
    case PROP_RATE_LIMIT_END_TIME:
      g_value_set_uint64 (value, epg_provider_get_rate_limit_end_time (provider));
      break;
    case PROP_CODE_FORMAT:
      g_value_set_static_string (value, epg_provider_get_code_format (provider));
      break;
    case PROP_CLOCK:
      g_value_set_object (value, epg_provider_get_clock (provider));
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
    case PROP_RATE_LIMIT_END_TIME:
    case PROP_CODE_FORMAT:
      /* Read only. */
      g_assert_not_reached ();
      break;
    case PROP_ENABLED:
      /* Construct only. */
      self->enabled = g_value_get_boolean (value);
      break;
    case PROP_KEY_FILE:
      /* Construct only. */
      g_assert (self->key_file == NULL);
      self->key_file = g_value_dup_object (value);
      break;
    case PROP_STATE_DIRECTORY:
      /* Construct only. */
      g_assert (self->state_directory == NULL);
      self->state_directory = g_value_dup_object (value);
      break;
    case PROP_CLOCK:
      /* Construct only. */
      g_assert (self->clock == NULL);
      self->clock = g_value_dup_object (value);
      break;
    default:
      g_assert_not_reached ();
    }
}

/**
 * epg_manager_new:
 * @enabled: whether PAYG is enabled; if not, the #EpgManager will return
 *    %EPG_MANAGER_ERROR_DISABLED for all operations
 * @key_file: (transfer none) (optional): file containing shared key to verify codes with;
 *    see #EpgManager:key-file
 * @state_directory: (transfer none) (optional): directory to load/store state
 *    in, or %NULL to use the default directory; see #EpgManager:state-directory
 * @clock: (transfer none) (optional): an #EpgClock, or %NULL to use the default
 *    clock implementation
 * @cancellable: (nullable): a #GCancellable or %NULL
 * @callback: callback function to invoke when the #EpgManager is ready
 * @user_data: user data to pass to @callback
 *
 * Asynchronously creates a new #EpgManager instance, which will load its
 * previous state from disk.
 *
 * When the initialization is finished, @callback will be called. You can then
 * call epg_manager_new_finish() to get the #EpgManager and check for any
 * errors.
 *
 * Since: 0.2.0
 */
void
epg_manager_new (gboolean             enabled,
                 GFile               *key_file,
                 GFile               *state_directory,
                 EpgClock            *clock,
                 GCancellable        *cancellable,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
  g_return_if_fail (key_file == NULL || G_IS_FILE (key_file));
  g_return_if_fail (state_directory == NULL || G_IS_FILE (state_directory));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_async_initable_new_async (EPG_TYPE_MANAGER,
                              G_PRIORITY_DEFAULT,
                              cancellable,
                              callback,
                              user_data,
                              "enabled", enabled,
                              "key-file", key_file,
                              "state-directory", state_directory,
                              "clock", clock,
                              NULL);
}

/**
 * epg_manager_new_finish:
 * @result: a #GAsyncResult obtained from the #GAsyncReadyCallback passed to
 *    epg_manager_new()
 * @error: return location for error or %NULL
 *
 * Finishes creating an #EpgManager.
 *
 * Since: 0.2.0
 */
EpgProvider *
epg_manager_new_finish (GAsyncResult  *result,
                        GError       **error)
{
  g_autoptr(GObject) source_object;

  source_object = g_async_result_get_source_object (result);
  g_assert (source_object != NULL);

  return EPG_PROVIDER (g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
                                                    result,
                                                    error));
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

/* Check whether there is a limit on calls to the manager due to rate limiting
 * being in effect. If there have been too many calls to check_rate_limiting()
 * recently, this will update the rate limiting state to include this call, and
 * will return %EPG_MANAGER_ERROR_TOO_MANY_ATTEMPTS.
 * This will also update @rate_limit_end_time_secs.
 *
 * It is expected that if this method returns %TRUE, clear_rate_limiting() will
 * be called shortly afterwards; and hence #EpgManager:rate-limit-end-time is
 * only notified if this method returns %FALSE. */
static gboolean
check_rate_limiting (EpgManager  *self,
                     guint64      now_secs,
                     GError     **error)
{
  /* Count how many attempts there have been in the last
   * %RATE_LIMITING_TIME_PERIOD_SECS. If it’s over %RATE_LIMITING_N_ATTEMPTS,
   * reject the attempt. In any case, update the list of attempts. */
  gsize n_attempts_in_last_period = 0;

  for (gsize i = 0; i < G_N_ELEMENTS (self->rate_limiting_history); i++)
    {
      if (self->rate_limiting_history[i] >= now_secs - RATE_LIMITING_TIME_PERIOD_SECS)
        n_attempts_in_last_period++;
    }

  g_debug ("%s: Checking rate limiting: %" G_GSIZE_FORMAT " attempts in last "
           "%u seconds; limit is %u attempts",
           G_STRFUNC, n_attempts_in_last_period,
           (guint) RATE_LIMITING_TIME_PERIOD_SECS,
           (guint) RATE_LIMITING_N_ATTEMPTS);

  /* Update the history: shift the first N-1 elements of the array to indexes
   * 1..N, and push the new entry in at index 0. */
  memmove (self->rate_limiting_history + 1, self->rate_limiting_history,
           (G_N_ELEMENTS (self->rate_limiting_history) - 1) * sizeof (*self->rate_limiting_history));
  self->rate_limiting_history[0] = now_secs;

  /* Update the end time (when the user will first be able to try again). This
   * will be when the oldest attempt leaves the sliding time period window. If
   * there have not been enough attempts (ever) to trigger rate limiting, clamp
   * to zero. */
  guint64 oldest_attempt_secs = self->rate_limiting_history[G_N_ELEMENTS (self->rate_limiting_history) - 1];
  self->rate_limit_end_time_secs = (oldest_attempt_secs > 0) ? oldest_attempt_secs + RATE_LIMITING_TIME_PERIOD_SECS : 0;

  if (n_attempts_in_last_period >= RATE_LIMITING_N_ATTEMPTS)
    {
      if (self->enabled)
        g_object_notify (G_OBJECT (self), "rate-limit-end-time");

      g_set_error_literal (error, EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_TOO_MANY_ATTEMPTS,
                           _("Too many invalid codes entered recently; please wait"));
      return FALSE;
    }

  return TRUE;
}

/* Clear the rate limiting history, so that the next call to
 * check_rate_limiting() is guaranteed to not return an error. It will notify of
 * changes to #EpgManager:rate-limit-end-time. */
static void
clear_rate_limiting (EpgManager *self)
{
  memset (self->rate_limiting_history, 0, sizeof (self->rate_limiting_history));
  self->rate_limit_end_time_secs = 0;

  if (self->enabled)
    g_object_notify (G_OBJECT (self), "rate-limit-end-time");
}

/* Check that @counter/@period has not been used yet. If it has, return
 * %EPG_MANAGER_ERROR_CODE_ALREADY_USED; otherwise, return %TRUE. */
static gboolean
check_is_counter_unused (EpgManager  *self,
                         EpcPeriod    period,
                         EpcCounter   counter,
                         GError     **error)
{
  /* As per https://www.evanjones.ca/linear-search.html, there’s not much
   * point using anything more complex than linear search here. The counter
   * list is limited to 256 entries, and linear search is fast to around 100
   * entries, and still competitive above that.
   * The list is guaranteed to be sorted. */
  for (gsize i = 0; i < self->used_codes->len; i++)
    {
      const UsedCode *used_code = &g_array_index (self->used_codes, UsedCode, i);

      if (counter == used_code->counter &&
          period == used_code->period)
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
check_expired_cb (gpointer user_data)
{
  EpgManager *self = EPG_MANAGER (user_data);

  if (!self->enabled)
    return G_SOURCE_REMOVE;

  /* Expired yet? */
  guint64 now_secs = epg_clock_get_time (self->clock);

  if (self->expiry_time_secs <= now_secs)
    {
      g_signal_emit_by_name (self, "expired");
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

/* Set the #EpgManager:expiry-time to
 * `MIN (G_MAXUINT64, MAX (@now, #EpgManager:expiry-time) + @span)` and set
 * the #GSource expiry timer to the new expiry time. Everything is handled in
 * seconds. */
static void
set_expiry_time (EpgManager *self,
                 guint64     now_secs,
                 guint64     span_secs)
{
  /* Note that we're indirectly using CLOCK_BOOTTIME here and in the callback,
   * so that the clock includes time spent suspended and so that manual clock
   * adjustments have no effect */
  guint64 old_expiry_time_secs = self->expiry_time_secs;

  /* If the old PAYG code had expired, start from @now_secs; otherwise start
   * from the current expiry time (in the future). */
  guint64 base_secs = (now_secs < old_expiry_time_secs) ? old_expiry_time_secs : now_secs;

  /* Clamp to the end of time instead of overflowing. */
  self->expiry_time_secs = (base_secs <= G_MAXUINT64 - span_secs) ? base_secs + span_secs : G_MAXUINT64;

  /* Set the expiry timer. epg_clock_source_new_seconds() takes a #guint, and
   * @span_secs is a #guint64 so clamp to G_MAXUINT */
  clear_expiry_timer (self);

  if (self->expiry_time_secs != G_MAXUINT64)
    {
      g_autoptr(GError) local_error = NULL;

      g_assert (self->expiry_time_secs >= now_secs);
      guint expiry_span_clamped = MIN (self->expiry_time_secs - now_secs, G_MAXUINT);
      self->expiry = epg_clock_source_new_seconds (self->clock,
                                                   expiry_span_clamped,
                                                   &local_error);
      if (self->expiry == NULL)
        {
          g_warning ("%s: epg_clock_source_new_seconds() failed: %s", G_STRFUNC, local_error->message);
          /* We have no way to check expiration, so assume time is up */
          self->expiry_time_secs = now_secs;
          check_expired_cb (self);
        }
      else
        {
          g_source_set_callback (self->expiry, check_expired_cb, self, NULL);
          g_source_attach (self->expiry, self->context);
        }
    }

  if (old_expiry_time_secs != self->expiry_time_secs &&
      self->enabled)
    g_object_notify (G_OBJECT (self), "expiry-time");
}

/* Set the #EpgManager:expiry-time to be `MAX (@now, #EpgManager:expiry-time)`,
 * plus the given @period, guaranteeing to give the user @period extra time on
 * their computer. If @period is %EPC_PERIOD_INFINITE, or if the expiry time
 * would overflow, set the expiry time as high as possible. Everything is
 * handled in seconds.
 *
 * @now will typically be the current value of CLOCK_BOOTTIME. */
static void
extend_expiry_time (EpgManager *self,
                    guint64     now_secs,
                    EpcPeriod   period)
{
  guint64 span_secs;

  switch (period)
    {
    case EPC_PERIOD_5_SECONDS:
      span_secs = 5;
      break;
    case EPC_PERIOD_1_MINUTE:
      span_secs = 60;
      break;
    case EPC_PERIOD_5_MINUTES:
      span_secs = 5 * 60;
      break;
    case EPC_PERIOD_30_MINUTES:
      span_secs = 30 * 60;
      break;
    case EPC_PERIOD_1_HOUR:
      span_secs = 60 * 60;
      break;
    case EPC_PERIOD_8_HOURS:
      span_secs = 8 * 60 * 60;
      break;
    case EPC_PERIOD_1_DAY:
      span_secs = 24 * 60 * 60;
      break;
    case EPC_PERIOD_2_DAYS:
      span_secs = 2 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_3_DAYS:
      span_secs = 3 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_4_DAYS:
      span_secs = 4 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_5_DAYS:
      span_secs = 5 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_6_DAYS:
      span_secs = 6 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_7_DAYS:
      span_secs = 7 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_8_DAYS:
      span_secs = 8 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_9_DAYS:
      span_secs = 9 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_10_DAYS:
      span_secs = 10 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_11_DAYS:
      span_secs = 11 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_12_DAYS:
      span_secs = 12 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_13_DAYS:
      span_secs = 13 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_14_DAYS:
      span_secs = 14 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_30_DAYS:
      span_secs = 30 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_60_DAYS:
      span_secs = 60 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_90_DAYS:
      span_secs = 90 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_120_DAYS:
      span_secs = 120 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_365_DAYS:
      span_secs = 365 * 24 * 60 * 60;
      break;
    case EPC_PERIOD_INFINITE:
      span_secs = G_MAXUINT64;
      break;
    default:
      g_assert_not_reached ();
    }

  set_expiry_time (self, now_secs, span_secs);
}

static gint
used_codes_sort_cb (gconstpointer a,
                       gconstpointer b)
{
  const UsedCode *code_a = a;
  const UsedCode *code_b = b;

  if (code_a->counter != code_b->counter)
    return code_a->counter - code_b->counter;

  if (code_a->period != code_b->period)
    return code_a->period - code_b->period;

  return 0;
}

static gboolean
epg_manager_add_code (EpgProvider   *provider,
                      const gchar  *code_str,
                      GError      **error)
{
  EpgManager *self = EPG_MANAGER (provider);
  g_autoptr(GError) local_error = NULL;
  guint64 now_secs;

  g_return_val_if_fail (EPG_IS_MANAGER (provider), FALSE);
  g_return_val_if_fail (code_str != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!check_enabled (self, error))
    return FALSE;

  now_secs = epg_clock_get_time (self->clock);
  if (!check_rate_limiting (self, now_secs, error))
    return FALSE;

  if (g_cancellable_set_error_if_cancelled (self->cancellable, error))
    return FALSE;

  /* Convert from a string to #EpcCode. */
  EpcCode code;
  if (!epc_parse_code (code_str, &code, &local_error))
    {
      g_set_error_literal (error,
                           EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_INVALID_CODE,
                           local_error->message);
      return FALSE;
    }

  /* Parse and verify the code. */
  EpcPeriod period;
  EpcCounter counter;

  if (!epc_verify_code (code, self->key_bytes, &period, &counter, &local_error))
    {
      g_set_error_literal (error,
                           EPG_MANAGER_ERROR, EPG_MANAGER_ERROR_INVALID_CODE,
                           local_error->message);
      return FALSE;
    }

  /* Check the counter hasn’t been used before. */
  if (!check_is_counter_unused (self, period, counter, error))
    return FALSE;

  /* Mark the counter as used. Typically, the sort should be a no-op, as we
   * expect (but don’t require) that counters should be used in order. */
  UsedCode used_code = { counter, period };
  g_array_append_val (self->used_codes, used_code);
  g_array_sort (self->used_codes, used_codes_sort_cb);

  /* Extend the expiry time. */
  extend_expiry_time (self, now_secs, period);

  /* Reset the rate limiting history, since the code was successful. */
  clear_rate_limiting (self);

  /* Kick off an asynchronous save.
   *
   * FIXME: pass self->cancellable; see comment in
   * epg_manager_shutdown_async().
   */
  g_assert (self->pending_internal_save_state_calls < G_MAXUINT64);
  self->pending_internal_save_state_calls++;
  epg_manager_save_state_async (provider, NULL,
                                internal_save_state_cb, NULL);

  return TRUE;
}

gboolean
epg_manager_clear_code (EpgProvider  *provider,
                        GError     **error)
{
  EpgManager *self = EPG_MANAGER (provider);

  g_return_val_if_fail (EPG_IS_MANAGER (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!check_enabled (self, error))
    return FALSE;

  if (g_cancellable_set_error_if_cancelled (self->cancellable, error))
    return FALSE;

  if (self->expiry_time_secs != 0)
    {
      self->expiry_time_secs = 0;
      g_object_notify (G_OBJECT (self), "expiry-time");
    }

  clear_expiry_timer (self);

  /* Kick off an asynchronous save.
   *
   * FIXME: pass self->cancellable; see comment in
   * epg_manager_shutdown_async().
   */
  g_assert (self->pending_internal_save_state_calls < G_MAXUINT64);
  self->pending_internal_save_state_calls++;
  epg_manager_save_state_async (provider, NULL,
                                internal_save_state_cb, NULL);

  return TRUE;
}

static void
internal_save_state_cb (GObject      *source_object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  EpgManager *self = EPG_MANAGER (source_object);
  EpgProvider *provider = EPG_PROVIDER (self);
  g_autoptr(GError) local_error = NULL;

  if (!epg_manager_save_state_finish (provider, result, &local_error))
    g_warning ("save_state failed: %s", local_error->message);

  g_return_if_fail (self->pending_internal_save_state_calls > 0);
  self->pending_internal_save_state_calls--;

  if (self->pending_internal_save_state_calls == 0 &&
      self->pending_shutdown != NULL)
    {
      if (local_error != NULL)
        g_task_return_error (self->pending_shutdown, g_steal_pointer (&local_error));
      else
        g_task_return_boolean (self->pending_shutdown, TRUE);

      g_clear_object (&self->pending_shutdown);
    }
}

/* Get the path of the state file containing the wall clock time. */
static GFile *
get_wallclock_time_file (EpgManager *self)
{
  return g_file_get_child (self->state_directory, "clock-time");
}

/* Get the path of the state file containing the expiry seconds. */
static GFile *
get_expiry_seconds_file (EpgManager *self)
{
  return g_file_get_child (self->state_directory, "expiry-seconds");
}

/* Get the path of the state file containing the expiry time.
 * This file is deprecated but kept for now for backwards compat. */
static GFile *
get_expiry_time_file (EpgManager *self)
{
  return g_file_get_child (self->state_directory, "expiry-time");
}

/* Get the path of the state file containing the set of used codes. */
static GFile *
get_used_codes_file (EpgManager *self)
{
  return g_file_get_child (self->state_directory, "used-codes");
}

static void file_load_cb        (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);
static void file_load_delete_cb (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);

/*
 * epg_manager_init_async:
 * @self: an #EpgManager
 * @priority: priority
 * @cancellable: a #GCancellable, or %NULL
 * @callback: function to call once the async operation is complete
 * @user_data: data to pass to @callback
 *
 * Load the state for the #EpgManager from the #EpgManager:state-directory.
 */
static void
epg_manager_init_async (GAsyncInitable      *initable,
                        int                  priority,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  g_return_if_fail (EPG_IS_MANAGER (initable));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  EpgManager *self = EPG_MANAGER (initable);
  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);

  g_task_set_source_tag (task, epg_manager_init_async);
  g_task_set_priority (task, priority);
  epg_multi_task_attach (task, 4);

  /* Load the wall clock time of the last state save. */
  g_autoptr(GFile) wallclock_time_file = get_wallclock_time_file (self);

  g_file_load_contents_async (wallclock_time_file, cancellable,
                              file_load_cb, g_object_ref (task));

  /* And the used codes. */
  g_autoptr(GFile) used_codes_file = get_used_codes_file (self);

  g_file_load_contents_async (used_codes_file, cancellable,
                              file_load_cb, g_object_ref (task));

  /* And the key. */
  g_file_load_contents_async (self->key_file, cancellable,
                              file_load_cb, g_object_ref (task));

  /* Decrement the pending operation count. */
  epg_multi_task_return_boolean (task, TRUE);
}

/* Convert bytes into a guint64 assuming host endianness */
/* @bytes must have length 8 */
static guint64
get_guint64_from_bytes (const guint8 *bytes)
{
  g_assert (bytes != NULL);

  union
    {
      guint64 u64;
      gchar u8[8];
    } number_union;

  memcpy (number_union.u8, bytes, sizeof (number_union.u8));

  return number_union.u64;
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
  g_autoptr(GError) invalid_data_error = NULL;
  g_autofree gchar *file_path = g_file_get_path (file);

  invalid_data_error = g_error_new (G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                    _("State file ‘%s’ was the wrong length."),
                                    file_path);

  guint64 now_secs = epg_clock_get_time (self->clock);
  guint64 wallclock_now_secs = epg_clock_get_wallclock_time (self->clock);


  /* If the file is not found, we continue below, but with @data set to %NULL
   * and @data_len set to zero. */
  g_autofree gchar *data = NULL;  /* actually guint8 if it weren’t for strict aliasing */
  gsize data_len = 0;

  if (!g_file_load_contents_finish (file, result, &data, &data_len, NULL, &local_error) &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
      epg_multi_task_return_error (task, G_STRFUNC, g_steal_pointer (&local_error));
      return;
    }

  /* Update the manager’s state. */
  g_autoptr(GFile) wallclock_time_file = get_wallclock_time_file (self);
  g_autoptr(GFile) expiry_seconds_file = get_expiry_seconds_file (self);
  g_autoptr(GFile) expiry_time_file = get_expiry_time_file (self);
  g_autoptr(GFile) used_codes_file = get_used_codes_file (self);

  if (g_file_equal (file, wallclock_time_file))
    {
      epg_multi_task_increment (task);

      if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          /* Load the expiry time (deprecated but kept for backwards compat). */
          g_file_load_contents_async (expiry_time_file, cancellable,
                                      file_load_cb, g_object_ref (task));
        }
      else
        {
          /* Load the expiry seconds. */
          g_file_load_contents_async (expiry_seconds_file, cancellable,
                                      file_load_cb, g_object_ref (task));
        }

      /* If data_len == 0 the behavior depends on whether expiry_time_file or
       * expiry_seconds_file exist */
      if (data_len != 0)
        {
          /* Check the file is the right size. If not, delete it so that we
           * don’t error next time we start. */
          if (data_len != sizeof (self->last_save_time_secs))
            {
              epg_multi_task_increment (task);
              g_task_set_task_data (task, g_steal_pointer (&invalid_data_error), (GDestroyNotify)g_error_free);
              g_file_delete_async (file, G_PRIORITY_DEFAULT, cancellable,
                                   file_load_delete_cb, g_object_ref (task));
              return;
            }

          /* Just record the value; it will be used below */
          self->last_save_time_secs = get_guint64_from_bytes ((const guint8 *)data);
          self->last_save_time_secs_set = TRUE;
        }
    }
  else if (g_file_equal (file, expiry_time_file)) /* for backwards compat */
    {
      if (data_len == 0)
        {
          /* No expiry time is set. Expire immediately. */
          set_expiry_time (self, now_secs, 0);
        }
      else
        {
          guint64 wallclock_expiry_time_secs;

          /* Check the file is the right size. If not, delete it so that we
           * don’t error next time we start. */
          if (data_len != sizeof (wallclock_expiry_time_secs))
            {
              epg_multi_task_increment (task);
              g_task_set_task_data (task, g_steal_pointer (&invalid_data_error), (GDestroyNotify)g_error_free);
              g_file_delete_async (file, G_PRIORITY_DEFAULT, cancellable,
                                   file_load_delete_cb, g_object_ref (task));
              return;
            }

          /* Set the expiry time. No other validation is needed on the loaded
           * number, as the entire range of the type is valid.
           * The value stored on disk is in terms of wall clock time, but we
           * want self->expiry_time_secs to be in terms of CLOCK_BOOTTIME, so
           * we're migrating here */
          wallclock_expiry_time_secs = get_guint64_from_bytes ((const guint8 *)data);
          set_expiry_time (self, now_secs,
                           (wallclock_expiry_time_secs > wallclock_now_secs) ? wallclock_expiry_time_secs - wallclock_now_secs : 0);

          /* Delete the file since we now use clock-time and expiry-seconds instead */
          epg_multi_task_increment (task);
          g_file_delete_async (file, G_PRIORITY_DEFAULT, cancellable,
                               file_load_delete_cb, g_object_ref (task));
        }
    }
  else if (g_file_equal (file, expiry_seconds_file))
    {
      if (data_len == 0)
        {
          /* No expiry time is set. Expire immediately. */
          set_expiry_time (self, now_secs, 0);
        }
      else
        {
          /* Check the file is the right size. If not, delete it so that we
           * don’t error next time we start. */
          if (data_len != sizeof (self->last_save_expiry_secs))
            {
              epg_multi_task_increment (task);
              g_task_set_task_data (task, g_steal_pointer (&invalid_data_error), (GDestroyNotify)g_error_free);
              g_file_delete_async (file, G_PRIORITY_DEFAULT, cancellable,
                                   file_load_delete_cb, g_object_ref (task));
              return;
            }

          /* Just record the value; it will be used below */
          self->last_save_expiry_secs = get_guint64_from_bytes ((const guint8 *)data);
          self->last_save_expiry_secs_set = TRUE;
        }
    }
  else if (g_file_equal (file, used_codes_file))
    {
      if (data_len == 0)
        {
          /* No used codes have been stored. Clear previous state anyway. */
          g_array_set_size (self->used_codes, 0);
        }
      else
        {
          /* Check the file is the right size. If not, delete it so that we
           * don’t error next time we start. */
          if ((data_len % sizeof (UsedCode)) != 0)
            {
              epg_multi_task_increment (task);
              g_task_set_task_data (task, g_steal_pointer (&invalid_data_error), (GDestroyNotify)g_error_free);
              g_file_delete_async (file, G_PRIORITY_DEFAULT, cancellable,
                                   file_load_delete_cb, g_object_ref (task));
              return;
            }

          /* Load the stored used codes. We’ve just validated the file size,
           * but we also need to validate the loaded structs: the whole range
           * of the counter type is valid, but the whole range of the period
           * type is not. */
          union
            {
              const UsedCode *p;
              const gchar *u8;
            } used_codes;

          used_codes.u8 = data;
          gsize n_used_codes = data_len / sizeof (UsedCode);

          for (gsize i = 0; i < n_used_codes; i++)
            {
              const UsedCode *used_code = &used_codes.p[i];

              if (!epc_period_validate (used_code->period, &local_error))
                {
                  epg_multi_task_increment (task);
                  g_task_set_task_data (task, g_steal_pointer (&invalid_data_error), (GDestroyNotify)g_error_free);
                  g_file_delete_async (file, G_PRIORITY_DEFAULT, cancellable,
                                       file_load_delete_cb, g_object_ref (task));
                  return;
                }
            }

          g_array_set_size (self->used_codes, 0);
          g_array_append_vals (self->used_codes,
                               data, data_len / sizeof (UsedCode));
          g_array_sort (self->used_codes, used_codes_sort_cb);
        }
    }
  else if (g_file_equal (file, self->key_file))
    {
      if (data == NULL)
        {
          /* The key is missing, so (this flavour of) PAYG is not enabled. */
          self->enabled = FALSE;
          g_object_notify (G_OBJECT (self), "enabled");

          /* Use a key of all zeros, just to avoid having to propagate the special
           * case of (key_bytes != NULL ∨ ¬enabled) throughout the code. */
          data_len = EPC_KEY_MINIMUM_LENGTH_BYTES;
          data = g_malloc0 (data_len);
        }

      self->key_bytes = g_bytes_new_take (g_steal_pointer (&data), data_len);
      data_len = 0;
    }
  else
    {
      g_assert_not_reached ();
    }

  /* Once both the wallclock time and expiry seconds have been loaded, deduce
   * the expiry time from them. */
  if (self->last_save_time_secs_set && self->last_save_expiry_secs_set &&
      (g_file_equal (file, wallclock_time_file) ||
       g_file_equal (file, expiry_seconds_file)))
    {
      if (self->last_save_time_secs > wallclock_now_secs)
        {
          /* Time has gone backwards!? Either the saved time is wrong (and
           * there's no way to know by how much) or the current time is wrong
           * (and NTP will fix it, see T24501). Let's just assume time stood
           * still. */
          set_expiry_time (self, now_secs, self->last_save_expiry_secs);
        }
      else
        {
          /* Time has continued its inexorable march forward while the computer
           * was off. Consume the appropriate credit */
          guint64 unaccounted_time = wallclock_now_secs - self->last_save_time_secs;
          if (unaccounted_time > self->last_save_expiry_secs)
            set_expiry_time (self, now_secs, 0);
          else
            set_expiry_time (self, now_secs, self->last_save_expiry_secs - unaccounted_time);
        }

      /* Kick off an asynchronous save. Otherwise an unclean shutdown would
       * cause us to consume credit for the same time period again.
       *
       * FIXME: pass self->cancellable; see comment in
       * epg_manager_shutdown_async().
       */
      g_assert (self->pending_internal_save_state_calls < G_MAXUINT64);
      self->pending_internal_save_state_calls++;
      epg_manager_save_state_async (EPG_PROVIDER (self), NULL,
                                    internal_save_state_cb, NULL);
    }

  epg_multi_task_return_boolean (task, TRUE);
}

static void
file_load_delete_cb (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) local_error = NULL;
  EpgManager *self = g_task_get_source_object (task);
  g_autoptr(GFile) expiry_time_file = get_expiry_time_file (self);
  GError *error = g_task_get_task_data (task);

  /* If we’re already returning an error that was set earlier, just
   * log this new error. */
  if (!g_file_delete_finish (file, result, &local_error) &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
      if (error == NULL)
        error = local_error;
      else
        g_debug ("%s: Error: %s", G_STRFUNC, local_error->message);
    }

  if (error == NULL)
    epg_multi_task_return_boolean (task, TRUE);

  epg_multi_task_return_error (task, G_STRFUNC, g_error_copy (error));
}

/*
 * epg_manager_init_finish:
 * @initable: an #EpgManager
 * @result: asynchronous operation result
 * @error: return location for an error, or %NULL
 *
 * Finish an asynchronous load operation started with
 * epg_manager_init_async().
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 0.1.0
 */
static gboolean
epg_manager_init_finish (GAsyncInitable  *initable,
                         GAsyncResult    *result,
                         GError         **error)
{
  g_return_val_if_fail (EPG_IS_MANAGER (initable), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, initable), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result, epg_manager_init_async), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void file_replace_cb     (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);
static void file_save_delete_cb (GObject      *source_object,
                                 GAsyncResult *result,
                                 gpointer      user_data);
static void
write_guint64_to_file (guint64       number,
                       GFile        *file,
                       GTask        *task,
                       GCancellable *cancellable)
{
  union
    {
      guint64 u64;
      const guint8 u8[8];
    } number_union;
  number_union.u64 = number;

  g_autoptr(GBytes) number_bytes = g_bytes_new (number_union.u8,
                                                sizeof (number_union.u8));

  g_file_replace_contents_bytes_async (file,
                                       number_bytes,
                                       NULL,  /* ETag */
                                       FALSE,  /* no backup */
                                       G_FILE_CREATE_PRIVATE,
                                       cancellable,
                                       file_replace_cb,
                                       g_object_ref (task));
}

static void
epg_manager_save_state_async (EpgProvider         *provider,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  EpgManager *self = EPG_MANAGER (provider);

  g_return_if_fail (EPG_IS_MANAGER (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_manager_save_state_async);
  epg_multi_task_attach (task, 4);

  /* Save the wall clock time. */
  g_autoptr(GFile) wallclock_time_file = get_wallclock_time_file (self);
  guint64 wallclock_seconds = epg_clock_get_wallclock_time (self->clock);
  write_guint64_to_file (wallclock_seconds, wallclock_time_file, task, cancellable);

  /* Save the expiry seconds. */
  g_autoptr(GFile) expiry_seconds_file = get_expiry_seconds_file (self);
  guint64 now_secs = epg_clock_get_time (self->clock);
  if (now_secs > self->expiry_time_secs)
    write_guint64_to_file (0, expiry_seconds_file, task, cancellable);
  else
    write_guint64_to_file (self->expiry_time_secs - now_secs, expiry_seconds_file, task, cancellable);

  /* And the used codes, if there are any. Otherwise delete the file. */
  g_autoptr(GFile) used_codes_file = get_used_codes_file (self);

  if (self->used_codes->len > 0)
    {
      g_autoptr(GBytes) used_codes_bytes = g_bytes_new (self->used_codes->data,
                                                        self->used_codes->len * sizeof (UsedCode));

      g_file_replace_contents_bytes_async (used_codes_file,
                                           used_codes_bytes,
                                           NULL,  /* ETag */
                                           FALSE,  /* no backup */
                                           G_FILE_CREATE_PRIVATE,
                                           cancellable,
                                           file_replace_cb,
                                           g_object_ref (task));
    }
  else
    {
      g_file_delete_async (used_codes_file, G_PRIORITY_DEFAULT,
                           cancellable, file_save_delete_cb, g_object_ref (task));
    }

  epg_multi_task_return_boolean (task, TRUE);
}

static void
file_replace_cb (GObject      *source_object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) local_error = NULL;

  if (!g_file_replace_contents_finish (file, result, NULL, &local_error))
    epg_multi_task_return_error (task, G_STRFUNC, g_steal_pointer (&local_error));
  else
    epg_multi_task_return_boolean (task, TRUE);
}

static void
file_save_delete_cb (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) local_error = NULL;

  if (!g_file_delete_finish (file, result, &local_error) &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    epg_multi_task_return_error (task, G_STRFUNC, g_steal_pointer (&local_error));
  else
    epg_multi_task_return_boolean (task, TRUE);
}

static gboolean
epg_manager_save_state_finish (EpgProvider   *provider,
                               GAsyncResult  *result,
                               GError       **error)
{
  EpgManager *self = EPG_MANAGER (provider);

  g_return_val_if_fail (EPG_IS_MANAGER (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result, epg_manager_save_state_async), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
epg_manager_shutdown_async (EpgProvider         *provider,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  EpgManager *self = EPG_MANAGER (provider);

  g_return_if_fail (EPG_IS_MANAGER (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  g_autoptr(GTask) task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, epg_manager_shutdown_async);

  g_autoptr(GError) local_error = NULL;

  /* It's only legal to call this method once */
  if (g_cancellable_set_error_if_cancelled (self->cancellable, &local_error))
    {
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  /* Prevent any further state modifications.
   *
   * FIXME: we would like to also pass this to all internal calls to
   * epg_manager_save_state_async() but but this is currently unsafe due to
   * bugs in in g_file_replace_contents_bytes_async() triggered by cancelling
   * it. https://gitlab.gnome.org/GNOME/glib/issues/1561
   */
  g_cancellable_cancel (self->cancellable);

  /* If any saves are in flight, wait for those to finish and return the last
   * result. If none are in flight, perform one final save. */
  if (self->pending_internal_save_state_calls == 0)
    epg_manager_save_state_async (provider, cancellable, shutdown_save_state_cb,
                                  g_steal_pointer (&task));
  else
    self->pending_shutdown = g_steal_pointer (&task);
}

static void
shutdown_save_state_cb (GObject      *source_object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  EpgProvider *provider = EPG_PROVIDER (source_object);
  g_autoptr(GTask) task = G_TASK (user_data);
  g_autoptr(GError) local_error = NULL;

  if (!epg_manager_save_state_finish (provider, result, &local_error))
    g_task_return_error (task, g_steal_pointer (&local_error));
  else
    g_task_return_boolean (task, TRUE);
}

static gboolean
epg_manager_shutdown_finish (EpgProvider   *provider,
                             GAsyncResult  *result,
                             GError       **error)
{
  EpgManager *self = EPG_MANAGER (provider);

  g_return_val_if_fail (EPG_IS_MANAGER (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_async_result_is_tagged (result, epg_manager_shutdown_async), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static guint64
epg_manager_get_expiry_time (EpgProvider *provider)
{
  EpgManager *self = EPG_MANAGER (provider);

  g_return_val_if_fail (EPG_IS_MANAGER (self), 0);

  return self->enabled ? self->expiry_time_secs : 0;
}

static gboolean
epg_manager_get_enabled (EpgProvider *provider)
{
  EpgManager *self = EPG_MANAGER (provider);

  g_return_val_if_fail (EPG_IS_MANAGER (self), FALSE);

  return self->enabled;
}

/**
 * epg_manager_get_key_file:
 * @self: a #EpgManager
 *
 * Get the value of #EpgManager:key-file.
 *
 * Returns: (transfer none): file holding the shared key
 * Since: 0.1.0
 */
GFile *
epg_manager_get_key_file (EpgManager *self)
{
  g_return_val_if_fail (EPG_IS_MANAGER (self), NULL);

  return self->key_file;
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

static guint64
epg_manager_get_rate_limit_end_time (EpgProvider *provider)
{
  EpgManager *self = EPG_MANAGER (provider);

  g_return_val_if_fail (EPG_IS_MANAGER (self), 0);

  return self->enabled ? self->rate_limit_end_time_secs : 0;
}

static EpgClock *
epg_manager_get_clock (EpgProvider *provider)
{
  EpgManager *self = EPG_MANAGER (provider);

  g_return_val_if_fail (EPG_IS_MANAGER (self), NULL);

  return self->clock;
}
