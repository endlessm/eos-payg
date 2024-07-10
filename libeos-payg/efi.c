/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2020 Endless OS Foundation LLC
 *
 * All rights reserved.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <libeos-payg/efi.h>
#include <libglnx.h>

#define EOSPAYG_GUID         "d89c3871-ae0c-4fc5-a409-dc717aee61e7"
#define GLOBAL_VARIABLE_GUID "8be4df61-93ca-11d2-aa0d-00e098032b8c"

/* Can't figure out who owns this GUID. ECS and Lenovo have both used it */
#define SBO_VARIABLE_GUID    "955b9041-133a-4bcf-90d1-97e1693c0e30"
#define NVM_PREFIX           "EOSPAYG_"

/* Totally bogus low performance mock storage for dry runs.
 * Note that the array becomes sparse after deletes, so the
 * higher the FAKE_VAR_COUNT, the more it'll suck.
 */
#define FAKE_VAR_COUNT 200
struct fake_var {
        char *name;
        char *content;
        int size;
};

static struct fake_var fake_vars[FAKE_VAR_COUNT];
static int fake_var_ptr = 0;

static int efi_fd = -1;
DIR *efi_dir = NULL;
static gboolean post_pivot = FALSE;
static gboolean initted = FALSE;
static gboolean test_mode = FALSE;

struct efi_ops {
  gboolean (*exists) (const char *name);
  unsigned char * (*read) (const char  *name,
                           int         *size,
                           GError     **error);
  gboolean (*write) (const char  *name,
                     const void  *content,
                     int          size,
                     gboolean     allow_overwrite,
                     GError     **error);
  gboolean (*delete) (const char  *name,
                      GError     **error);
  void (*list_rewind) (void);
  const char *(*list_next) (void);
  gboolean (*clear) (void);
};

static struct efi_ops *efi;

static gboolean
clear_immutable (const char  *name,
                 GError     **error)
{
  unsigned int flags;
  int ret = 0;
  glnx_autofd int fd = -1;

  if (efi_fd == -1)
    return glnx_throw (error, "EFI ops not initialized");

  fd = openat (efi_fd, name, O_RDONLY);
  if (fd < 0)
    return glnx_throw_errno_prefix (error, "openat(%s) failed", name);

  ret = ioctl (fd, FS_IOC_GETFLAGS, &flags);
  if (ret < 0)
    return glnx_throw_errno_prefix (error, "getflags failed");

  flags &= ~FS_IMMUTABLE_FL;
  ret = ioctl (fd, FS_IOC_SETFLAGS, &flags);
  if (ret < 0)
    return glnx_throw_errno_prefix (error, "setflags failed");

  return TRUE;
}

static char *
full_efi_name (const char *GUID, const char *name)
{
  return g_strdup_printf ("%s-%s", name, GUID);
}

static char *
eospayg_efi_name (const char *name)
{
  g_autofree char *tname;

  tname = g_strdup_printf ("EOSPAYG_%s", name);

  return full_efi_name (EOSPAYG_GUID, tname);
}

/* eospayg_efi_var_supported:
 *
 * Checks if basic EFI variable functionality exists.
 *
 * Return: %TRUE if EFI variables are supported,
 *   %FALSE otherwise
 */
gboolean
eospayg_efi_var_supported (void)
{
  if (test_mode)
    return TRUE;

  return efi_fd != -1;
}

/* eospayg_efi_root_pivot:
 *
 * Signal to the EFI library that the root pivot has
 * been crossed.
 *
 * After the root pivot some functionality is no longer
 * trusted. After we call this those functions will
 * no longer succeed.
 */
void
eospayg_efi_root_pivot (void)
{
  if (post_pivot)
    g_warning ("Root pivot signalled twice.");

  post_pivot = TRUE;
}

static gboolean
test_write (const char  *name,
            const void  *content,
            int          size,
            gboolean     allow_overwrite,
            GError     **error)
{
  struct fake_var *target = NULL;
  int i;

  for (i = 0; i < FAKE_VAR_COUNT; i++)
    {
      if (!target && !fake_vars[i].name)
        target = &fake_vars[i];

      if (fake_vars[i].name && !strcmp (fake_vars[i].name, name))
        {
          target = &fake_vars[i];
          break;
        }
    }

  if (target)
    {
      if (target->name)
        free (target->content);
      else
        target->name = strdup(name);

      target->content = malloc(size);
      target->size = size;
      memcpy (target->content, content, size);
      return TRUE;
    }

  g_set_error (error,
               G_IO_ERROR,
               G_IO_ERROR_NO_SPACE,
               "Could not find storage for %s",
               name);
  return FALSE;
}

static gboolean
efivarfs_write (const char  *name,
                const void  *content,
                int          size,
                gboolean     allow_overwrite,
                GError     **error)
{
  /* This is the attribute pattern required for all our variables,
   * non volatile, runtime services, boot services */
  unsigned char attr[4] = { 7, 0, 0, 0 };
  int tsize = 4 + size;
  char tbuf[tsize];
  glnx_autofd int fd = -1;
  ssize_t ret;
  int flags = O_WRONLY | O_CREAT;

  /* It may not exist, and this will fail harmlessly */
  if (allow_overwrite)
    clear_immutable (name, NULL);

  memcpy (tbuf, attr, 4);
  memcpy (tbuf + 4, content, size);

  if (!allow_overwrite)
    flags |= O_EXCL;

  fd = openat (efi_fd, name, flags, 0600);
  if (fd < 0)
    return glnx_throw_errno_prefix (error, "Failed to open %s", name);

  /* libefivar doesn't handle EINTR, so I guess writes are atomic
   * on efivarfs.
   */
  ret = write (fd, tbuf, tsize);
  if (ret < 0)
    return glnx_throw_errno_prefix (error, "Failed to write to %s", name);
  if (ret < tsize)
    return glnx_throw (error,
                       "Wrote only %" G_GSSIZE_FORMAT " bytes of %d to %s",
                       ret,
                       tsize,
                       name);

  return TRUE;
}

static gboolean
efi_var_write (const char  *name,
               const void  *content,
               int          size,
               gboolean     allow_overwrite,
               GError     **error)
{
  return efi->write (name, content, size, allow_overwrite, error);
}

/* eospayg_efi_var_write:
 * @name: short name of variable to write
 * @content: data to store in variable
 * @size: number of bytes in content
 * @error: return location for an error, or %NULL
 *
 * Write a new EFI variable.
 *
 * The name will be automatically prefixed with EOSPAYG_
 * and suffixed with the eos payg variable UUID.
 *
 * If the variable exists, a malicious user can bind mount
 * something else over it to intercept the write. For this
 * reason, this function should only be used to write new
 * variables. It will fail to write over an existing
 * variable after the root pivot.
 *
 * Returns: %TRUE if successful, otherwise %FALSE
 */
gboolean
eospayg_efi_var_write (const char  *name,
                       const void  *content,
                       int          size,
                       GError     **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  gboolean allow_overwrite = TRUE;
  g_autofree char *tname = eospayg_efi_name (name);

  if (post_pivot)
    allow_overwrite = FALSE;

  return efi_var_write (tname, content, size, allow_overwrite, error);
}

/* eospayg_efi_var_overwrite:
 * @name: short name of variable to write
 * @content: data to store in variable
 * @size: number of bytes in content
 * @error: return location for an error, or %NULL
 *
 * Overwrite an existing EFI variable, or create a new one.
 *
 * The name will be automatically prefixed with EOSPAYG_
 * and suffixed with the eos payg variable UUID.
 *
 * This variant should be used if the variable may exist.
 * It will always fail if used after the root pivot.
 *
 * Returns: %TRUE if successful, otherwise %FALSE
 */
gboolean
eospayg_efi_var_overwrite (const char  *name,
                           const void  *content,
                           int          size,
                           GError     **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_autofree char *tname = eospayg_efi_name (name);

  if (post_pivot)
    return glnx_throw (error, "Attempted to overwrite %s after pivot", name);

  return efi_var_write (tname, content, size, TRUE, error);
}

static void
test_zap_var (int index)
{
  free (fake_vars[index].name);
  free (fake_vars[index].content);
  fake_vars[index].size = 0;
  fake_vars[index].name = NULL;
  fake_vars[index].content = NULL;
}

static gboolean
test_delete (const char  *name,
             GError     **error)
{
  int i;

  for (i = 0; i < FAKE_VAR_COUNT; i++)
    if (fake_vars[i].name && !strcmp (fake_vars[i].name, name))
      {
        test_zap_var (i);
        return TRUE;
      }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
               "Variable %s not found", name);
  return FALSE;
}

static gboolean
efivarfs_delete (const char  *name,
                 GError     **error)
{
  int ret;
  g_autoptr(GError) local_error = NULL;

  if (!clear_immutable (name, &local_error))
    g_warning ("Failed to remove immutable flag on %s: %s",
               name,
               local_error->message);
  ret = unlinkat (efi_fd, name, 0);
  if (ret < 0)
    return glnx_throw_errno_prefix (error, "Failed to delete %s", name);

  return TRUE;
}

/* eospayg_efi_var_delete_fullname:
 * @name: Full name of variable to delete
 * @error: return location for an error, or %NULL
 *
 * Delete an EFI variable by its full name including GUID.
 *
 * If this fails, @error will be the result of the unlink
 * operation. Notably, G_IO_ERROR_BUSY will indicate that the deletion
 * probably failed due to the existence of a bind mount for
 * the file.
 *
 * Returns: %TRUE if successful, otherwise %FALSE
 */
gboolean
eospayg_efi_var_delete_fullname (const char  *name,
                                 GError     **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Make sure we never delete a non EOSPAYG_
   * variable, as some of those are required to boot!
   */
  if (strncmp (name, "EOSPAYG_", 8) != 0)
    return glnx_throw (error,
                       "Refusing to delete non-PAYG variable %s",
                       name);

  return efi->delete (name, error);
}


/* eospayg_efi_var_delete:
 * @name: Short name of variable to delete
 * @error: return location for an error, or %NULL
 *
 * Delete an EFI variable.
 *
 * The name will be automatically prefixed with EOSPAYG_
 * and suffixed with the eos payg variable UUID.
 *
 * If this fails, @error will be the result of the unlink
 * operation. Notably, G_IO_ERROR_BUSY will indicate that the deletion
 * probably failed due to the existence of a bind mount for
 * the file.
 * Returns: %TRUE if successful, otherwise %FALSE
 */
gboolean
eospayg_efi_var_delete (const char  *name,
                        GError     **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_autofree char *tname = eospayg_efi_name (name);

  return eospayg_efi_var_delete_fullname (tname, error);
}

static gboolean
test_exists (const char *name)
{
  int i;

  for (i = 0; i < FAKE_VAR_COUNT; i++)
    if (fake_vars[i].name && !strcmp (fake_vars[i].name, name))
      return TRUE;

  return FALSE;
}

static gboolean
efivarfs_exists (const char *name)
{
  g_autofree char *tname = eospayg_efi_name (name);

  return !faccessat (efi_fd, tname, F_OK, 0);
}

/* eospayg_efi_var_exists:
 * @name: Short name of variable to check
 *
 * Check if an EFI variable exists.
 *
 * The name will be automatically prefixed with EOSPAYG_
 * and suffixed with the eos payg variable UUID.
 *
 * Returns: %TRUE if variable exists, otherwise %FALSE
 */
gboolean
eospayg_efi_var_exists (const char *name)
{
  return efi->exists (name);
}

static unsigned char *
test_read (const char  *name,
           int         *size,
           GError     **error)
{
  unsigned char *out;
  int i;

  for (i = 0; i < FAKE_VAR_COUNT; i++)
    if (fake_vars[i].name && !strcmp (fake_vars[i].name, name))
      {
        out = malloc (fake_vars[i].size);
        memcpy (out, fake_vars[i].content, fake_vars[i].size);
        *size = fake_vars[i].size;
        return out;
      }

  return glnx_null_throw (error, "%s not found", name);
}

static unsigned char *
efivarfs_read (const char  *name,
               int         *size,
               GError     **error)
{
  struct stat sb;
  glnx_autofd int fd = -1;
  int ret, fsize;
  char attr[4];
  g_autofree unsigned char *tout = NULL;

  *size = -1;
  fd = openat (efi_fd, name, O_RDONLY);
  if (fd == -1)
    return glnx_null_throw_errno_prefix (error, "Failed to open %s", name);

  /* Apparently efivarfs reads are atomic and I don't have to
   * handle EINTR - libefivar doesn't.
   */
  ret = read (fd, &attr, 4);
  if (ret == -1)
    return glnx_null_throw_errno_prefix (error,
                                         "Failed to read attributes from %s",
                                         name);
  if (ret != 4)
    return glnx_null_throw (error,
                            "Read only %d bytes of 4-byte attributes for %s",
                            ret,
                            name);

  ret = fstat (fd, &sb);
  if (ret < 0)
    return glnx_null_throw_errno_prefix (error, "fstat() failed for %s", name);

  fsize = sb.st_size;
  if (fsize < 5)
    {
      /* This should be impossible, but efivarfs is a tire fire.
       * For example, on a system that has a PK enrolled, trying to
       * overwrite that PK without a properly signed request will result
       * in the expected write failure and the unintended side effect of
       * the kernel thinking the PK is 0 bytes long until the next reboot.
       *
       * If we return -1 here, the caller will think the file doesn't exist,
       * so let's return a 0. It's not possible for an efi variable to exist
       * without content, so this shouldn't be ambiguous.
       */
      *size = 0;
      return glnx_null_throw (error, "%s has length %d", name, fsize);
    }

  /* Throw away the attributes */
  fsize -= 4;
  tout = malloc (fsize);
  ret = read (fd, tout, fsize);
  if (ret == -1)
    return glnx_null_throw_errno_prefix (error,
                                         "Failed to read contents of %s",
                                         name);
  if (ret != fsize)
    return glnx_null_throw (error,
                            "Read %d bytes, not %d, of %s",
                            ret,
                            fsize,
                            name);

  *size = fsize;
  return g_steal_pointer (&tout);
}

/* eospayg_efi_secureboot_active:
 *
 * Check if the system was booted via SecureBoot
 *
 * Returns: %TRUE if booted via SecureBoot, %FALSE otherwise
 */
gboolean
eospayg_efi_secureboot_active (void)
{
  g_autofree char *tname = full_efi_name (GLOBAL_VARIABLE_GUID, "SecureBoot");
  g_autofree unsigned char *content = NULL;
  int size;

  /* In test mode let's pretend secure boot is enabled */
  if (test_mode)
    return TRUE;

  content = efivarfs_read (tname, &size, NULL);
  if (!content || size != 1)
    return FALSE;

  return !!content[0];
}

/* eospayg_efi_securebootoption_disabled:
 *
 * Check if the SecureBootOption EFI variable exists and
 * is disabled.
 *
 * The oddly inverted logic is due to the fact that most
 * systems don't have this variable at all - if it doesn't
 * exist, we can infer nothing about the state of Secure Boot,
 * if it does it tells us if the Secure Boot option in the
 * BIOS is enabled or disabled.
 *
 * Thus, on and not existing should likely be treated in the
 * same way by a caller, but existing and off is a red flag
 * for PAYG enforcement.
 *
 * Returns: %TRUE if the variable exists and is disabled, %FALSE otherwise
 */
gboolean
eospayg_efi_securebootoption_disabled (void)
{
  g_autofree char *tname = full_efi_name (SBO_VARIABLE_GUID, "SecureBootOption");
  g_autofree unsigned char *content = NULL;
  int size;

  if (test_mode)
    return FALSE;

  content = efivarfs_read (tname, &size, NULL);
  if (!content || size != 1)
    return FALSE;

  return !content[0];
}

/* eospayg_efi_PK_size:
 *
 * Get the size of the PK efi variable, including EFI attribute overhead.
 *
 * The size of the PK variable is useful in determining if the system is
 * set up properly for Secure Boot. If it has non zero size, it's properly
 * installed. If the size is 0, there is no variable present in EFI storage
 * space, but the kernel has a placeholder file for it due to a failed
 * write.
 *
 * Returns: size of the PK EFI variable, excluding the 4-byte attribute
 *          overhead, or -1 if missing
 */
int
eospayg_efi_PK_size (void)
{
  g_autofree char *tname = full_efi_name (GLOBAL_VARIABLE_GUID, "PK");
  g_autofree unsigned char *content = NULL;
  int size;

  if (test_mode)
    return -1;

  content = efivarfs_read (tname, &size, NULL);
  return size;
}

/* eospayg_efi_var_read:
 * @name: Short name of variable
 * @expected_size: Expected size of the variable contents, in bytes, or
 *                 -1
 * @size: Returns the number of bytes in the variable
 * @error: return location for an error, or %NULL
 *
 * Read the contents of an EFI variable.
 *
 * If @expected_size is not -1, and the variable exists but does not
 * have the expected size, %NULL is returned rather than the variable
 * contents.
 *
 * The name will be automatically prefixed with EOSPAYG_
 * and suffixed with the eos payg variable UUID.
 *
 * Returns: (transfer full): A pointer to the variable contents, or %NULL on
 *          error
 */
void *
eospayg_efi_var_read (const char  *name,
                      int          expected_size,
                      int         *size,
                      GError     **error)
{
  g_return_val_if_fail (expected_size >= -1, FALSE);
  g_return_val_if_fail (size != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_autofree char *tname = eospayg_efi_name (name);

  *size = -1;
  if (post_pivot)
    return glnx_null_throw (error, "Cannot read %s after pivot", name);

  void *ret = efi->read (tname, size, error);
  if (ret &&
      expected_size >= 0 &&
      expected_size != *size)
    {
      g_clear_pointer (&ret, free);
      return glnx_null_throw (error,
                              "Variable data was %d bytes; expected %d bytes",
                              *size,
                              expected_size);
    }
  return ret;
}

static void
test_list_rewind (void)
{
  fake_var_ptr = 0;
}

static void
efivarfs_list_rewind (void)
{
  rewinddir (efi_dir);
}

/* eospayg_efi_list_rewind:
 *
 * Rewind to the start of the list of EFI variables.
 */
void
eospayg_efi_list_rewind (void)
{
  efi->list_rewind ();
}

static const char *
test_list_next (void)
{
  for (;fake_var_ptr < FAKE_VAR_COUNT; fake_var_ptr++)
    {
      if (fake_vars[fake_var_ptr].name)
        return fake_vars[fake_var_ptr++].name;
    }
  return NULL;
}

static const char *
efivarfs_list_next (void)
{
  struct dirent *de;

  while ((de = readdir (efi_dir)))
    {
      /* Filter non payg EFI variables */
      if (strncmp (de->d_name, NVM_PREFIX, strlen (NVM_PREFIX)) != 0)
        continue;

      return de->d_name;
    }
  return NULL;
}

/* eospayg_efi_list_next:
 *
 * Get the name of the next EFI variable in the
 * EFI variable storage directory.
 *
 * Only returns EOSPAYG_ prefixed variables.
 *
 * Returns: The variable name as provided by readdir.
 *   Must not be changed, freed, or stored by the caller.
 */
const char *
eospayg_efi_list_next (void)
{
  return efi->list_next ();
}

static gboolean
test_clear (void)
{
  int i;

  fake_var_ptr = 0;

  for (i = 0; i < FAKE_VAR_COUNT; i++)
    test_zap_var (i);

  return TRUE;
}

/* eospayg_efi_clear:
 *
 * Clear out all the PAYG EFI variables. This is really only
 * for testing, and is unimplemented for real storage.
 *
 * Returns: %TRUE if all PAYG EFI variables could be cleared,
 *   %FALSE otherwise.
 */
gboolean eospayg_efi_clear (void)
{
  if (!efi->clear)
    return FALSE;

  return efi->clear ();
}

static struct efi_ops efivarfs_ops =
{
  .read = efivarfs_read,
  .write = efivarfs_write,
  .delete = efivarfs_delete,
  .list_rewind = efivarfs_list_rewind,
  .list_next = efivarfs_list_next,
  .exists = efivarfs_exists,
  .clear = NULL,
};

static struct efi_ops test_ops =
{
  .read = test_read,
  .write = test_write,
  .delete = test_delete,
  .list_rewind = test_list_rewind,
  .list_next = test_list_next,
  .exists = test_exists,
  .clear = test_clear,
};

/* eospayg_efi_init:
 * @flags: pass EOSPAYG_EFI_TEST_MODE for fake EFI storage
 * @error: return location for an error, or %NULL
 *
 * Initialize our EFI functionality. This must be done
 * before the root pivot, as it needs a trusted fd to
 * the efi storage directory.
 *
 * When in EOSPAYG_EFI_TEST_MODE the EFI storage is not
 * persistent, nor is it backed by real UEFI firmware
 * provided variables. This mode is for testing only.
 *
 * Return: %TRUE if successful or %FALSE otherwise.
 */
gboolean
eospayg_efi_init (enum eospayg_efi_flags   flags,
                  GError                 **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  glnx_autofd int local_efi_fd = -1;
  glnx_autofd int tmpfd = -1;

  if (initted)
    return TRUE;

  if (flags & EOSPAYG_EFI_TEST_MODE)
    {
      test_mode = TRUE;
      efi = &test_ops;
      return TRUE;
    }

  efi = &efivarfs_ops;
  local_efi_fd = open ("/sys/firmware/efi/efivars", O_DIRECTORY);
  if (local_efi_fd < 0)
    return glnx_throw_errno_prefix (error, "Failed to open efivars");

  tmpfd = open ("/sys/firmware/efi/efivars", O_DIRECTORY);
  if (tmpfd < 0)
    return glnx_throw_errno_prefix (error, "Failed to open efivars twice");

  efi_dir = fdopendir (tmpfd);
  if (!efi_dir)
    return glnx_throw_errno_prefix (error, "Failed to open efivars directory stream");

  /* "After a successful call to fdopendir(), fd is used internally by the
   * implementation, and should not otherwise be used by the application."
   */
  tmpfd = -1;

  /* Transfer ownership to the global variable */
  efi_fd = _glnx_steal_fd (&local_efi_fd);

  return TRUE;
}
