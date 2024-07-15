/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2020 Endless OS Foundation LLC
 *
 * All rights reserved.
 */

#pragma once

#include <glib.h>

enum eospayg_efi_flags {
  EOSPAYG_EFI_TEST_MODE = 1,
};

enum efivar_states {
  EFIVAR_NOT_EXIST = 0,
  EFIVAR_TRUE,
  EFIVAR_FALSE,
};

gboolean eospayg_efi_init (enum eospayg_efi_flags   flags,
                           GError                 **error);
gboolean eospayg_efi_var_write (const char  *name,
                                const void  *content,
                                int          size,
                                GError     **error);
gboolean eospayg_efi_var_overwrite (const char  *name,
                                    const void  *content,
                                    int          size,
                                    GError     **error);
gboolean eospayg_efi_var_delete (const char  *name,
                                 GError     **error);
gboolean eospayg_efi_var_delete_fullname (const char  *name,
                                          GError     **error);
gboolean eospayg_efi_var_exists (const char *name);
gboolean eospayg_efi_secureboot_active (void);
gboolean eospayg_efi_setupmode_active (void);
enum efivar_states eospayg_efi_secureboot_setup_active (void);
gboolean eospayg_efi_securebootoption_disabled (void);
int eospayg_efi_PK_size (void);
gboolean eospayg_efi_var_supported (void);
void eospayg_efi_list_rewind (void);
const char *eospayg_efi_list_next (void);
void eospayg_efi_root_pivot (void);
void *eospayg_efi_var_read (const char  *name,
                            int          expected_size,
                            int         *size,
                            GError     **error);
gboolean eospayg_efi_var_read_fullname_boolean (const char  *name,
                                                GError     **error);
gboolean eospayg_efi_clear (void);
