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
#include <libeos-payg/multi-task.h>

GQuark epg_multi_task_quark (void);
G_DEFINE_QUARK (epg-multi-task, epg_multi_task)

static guint
epg_multi_task_get (GTask *task)
{
  return GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (task),
                                               epg_multi_task_quark ()));
}

static void
epg_multi_task_set (GTask *task,
                    guint pending)
{
  g_object_set_qdata (G_OBJECT (task),
                      epg_multi_task_quark (),
                      GUINT_TO_POINTER (pending));
}

/**
 * epg_multi_task_attach:
 * @task: (transfer none): the task
 * @pending: initial number of pending operations for @task.
 *
 * Initializes @task as having @pending operations pending.  The number of
 * pending operations can be incremented with epg_multi_task_increment(), and
 * decremented with epg_multi_task_return_boolean(),
 * epg_multi_task_return_pointer(), and epg_multi_task_return_error().
 *
 * The number of pending operations is stored by occult means, leaving the
 * task_data of @task free for other uses.
 *
 * Since: 0.2.0
 */
void
epg_multi_task_attach (GTask *task,
                       guint  pending)
{
  g_return_if_fail (G_IS_TASK (task));
  g_return_if_fail (epg_multi_task_get (task) == 0);
  g_return_if_fail (pending > 0);

  epg_multi_task_set (task, pending);
}

/**
 * epg_multi_task_increment:
 * @task: (transfer none): the task
 *
 * Increments the number of pending operations for @task by 1.
 *
 * Since: 0.2.0
 */
void
epg_multi_task_increment (GTask *task)
{
  g_return_if_fail (G_IS_TASK (task));

  guint operation_count = epg_multi_task_get (task);
  /* If it's already returned, we're too late. */
  g_return_if_fail (operation_count > 0);
  g_return_if_fail (operation_count < G_MAXUINT);

  epg_multi_task_set (task, operation_count + 1);
}

/**
 * epg_multi_task_return_boolean:
 * @task: (transfer none): the task
 * @result: the result
 *
 * Decrements the number of pending operations for @task by 1, causing it to
 * return @result if no further operations are pending and no error has been
 * encountered.
 *
 * Since: 0.2.0
 */
void
epg_multi_task_return_boolean (GTask   *task,
                               gboolean result)
{
  g_return_if_fail (G_IS_TASK (task));

  guint operation_count = epg_multi_task_get (task);
  g_return_if_fail (operation_count > 0);

  epg_multi_task_set (task, --operation_count);
  if (operation_count == 0 && !g_task_had_error (task))
    g_task_return_boolean (task, result);
}

/**
 * epg_multi_task_return_pointer:
 * @task: (transfer none): the task
 * @result: (transfer full): the result
 * @result_destroy: (optional): a #GDestroyNotify function for @result
 *
 * Decrements the number of pending operations for @task by 1, causing it to
 * return @result if no further operations are pending and no error has been
 * encountered. If an error has been encountered, or further operations are
 * pending, @result is immediately destroyed with @result_destroy.
 *
 * One way to use this function is to use a #GPtrArray as @task's task_data.
 * Each time a subtask completes, add its output to the array, and call
 * `epg_multi_task_return_pointer (task, g_ptr_array_ref (array),
 * g_ptr_array_unref);`.
 *
 * Since: 0.2.0
 */
void
epg_multi_task_return_pointer (GTask         *task,
                               gpointer       result,
                               GDestroyNotify result_destroy)
{
  g_return_if_fail (G_IS_TASK (task));

  guint operation_count = epg_multi_task_get (task);
  g_return_if_fail (operation_count > 0);

  epg_multi_task_set (task, --operation_count);
  if (operation_count == 0 && !g_task_had_error (task))
    g_task_return_pointer (task, result, result_destroy);
  else if (result_destroy != NULL)
    result_destroy (result);
}

/**
 * epg_multi_task_return_error:
 * @task: (transfer none): the task
 * @tag: a string tag to include in the log message if @task has already
 *    returned an error (typically #G_STRFUNC)
 * @error: (transfer full): an error
 *
 * Decrements the number of pending operations for @task. If @task has not
 * returned an error yet, return @error; otherwise, log @error prefixed by @tag
 * and ignore it.
 *
 * Since: 0.2.0
 */
void
epg_multi_task_return_error (GTask *task,
                             const gchar *tag,
                             GError *error)
{
  g_return_if_fail (G_IS_TASK (task));
  g_return_if_fail (tag != NULL);
  g_return_if_fail (error != NULL);

  guint operation_count = epg_multi_task_get (task);
  g_return_if_fail (operation_count > 0);

  epg_multi_task_set (task, --operation_count);
  if (g_task_had_error (task))
    {
      g_debug ("%s: Error: %s", tag, error->message);
      g_error_free (error);
    }
  else
    {
      g_task_return_error (task, error);
    }
}
