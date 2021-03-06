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

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/* https://www.youtube.com/watch?v=NVPLqbWXdDA */

void epg_multi_task_attach         (GTask         *task,
                                    guint          pending);
void epg_multi_task_increment      (GTask         *task);
void epg_multi_task_return_boolean (GTask         *task,
                                    gboolean       result);
void epg_multi_task_return_pointer (GTask         *task,
                                    gpointer       result,
                                    GDestroyNotify result_destroy);
void epg_multi_task_return_error   (GTask         *task,
                                    const gchar   *tag,
                                    GError        *error);

G_END_DECLS
