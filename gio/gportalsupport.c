/* GIO - GLib Input, Output and Streaming Library
 *
 * Copyright 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "gportalsupport.h"

#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include <gioerror.h>

static gboolean flatpak_info_read;
static gboolean use_portal;
static gboolean network_available;

static void
read_flatpak_info (void)
{
  char *path;

  if (flatpak_info_read)
    return;

  flatpak_info_read = TRUE;

  path = g_build_filename (g_get_user_runtime_dir (), "flatpak-info", NULL);
  if (g_file_test (path, G_FILE_TEST_EXISTS))
    {
      GKeyFile *keyfile;

      use_portal = TRUE;
      network_available = FALSE;

      keyfile = g_key_file_new ();
      if (g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL))
        {
          char **shared = NULL;

          shared = g_key_file_get_string_list (keyfile, "Context", "shared", NULL, NULL);
          if (shared)
            {
              network_available = g_strv_contains ((const char * const *)shared, "network");
              g_strfreev (shared);
            }
        }
    }
  else
    {
      const char *var;

      var = g_getenv ("GTK_USE_PORTAL");
      if (var && var[0] == '1')
        use_portal = TRUE;
      network_available = TRUE;
    }

  g_free (path);
}

gboolean
glib_should_use_portal (void)
{
  read_flatpak_info ();
  return use_portal;
}

gboolean
glib_network_available_in_sandbox (void)
{
  read_flatpak_info ();
  return network_available;
}

char *
glib_lookup_sandboxed_app_id (GError **error)
{
  char *path;
  char *content = NULL;
  char **lines;
  char *app_id = NULL;
  int i;
  guint32 pid;
  gboolean res;

  pid = getpid ();

  path = g_strdup_printf ("/proc/%u/cgroup", pid);
  res = g_file_get_contents (path, &content, NULL, error);
  g_free (path);

  if (!res)
    {
      g_prefix_error (error, "Can't find peer app id: ");
      return NULL;
    }

  lines =  g_strsplit (content, "\n", -1);
  g_free (content);

  for (i = 0; lines[i] != NULL; i++)
    {
      if (g_str_has_prefix (lines[i], "1:name=systemd:"))
        {
          const char *unit = lines[i] + strlen ("1:name=systemd:");
          char *scope = g_path_get_basename (unit);

          if (g_str_has_prefix (scope, "flatpak-") &&
              g_str_has_suffix (scope, ".scope"))
            {
              const char *name = scope + strlen ("flatpak-");
              char *dash = strchr (name, '-');
              if (dash != NULL)
                {
                  *dash = 0;
                  app_id = g_strdup (name);
                }
            }
          else
            {
              app_id = g_strdup ("");
            }

          g_free (scope);

          if (app_id != NULL)
            break;
        }
    }

  g_strfreev (lines);

  if (app_id == NULL)
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "Can't find peer app id: No name=systemd cgroup");
  return app_id;
}
