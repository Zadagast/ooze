/*
 * ooze-pam-helper — authenticate the current user via PAM.
 *
 * Usage: ooze-pam-helper [username]
 * Reads a single password from stdin (NUL- or newline-terminated).
 * Exit: 0 success, 1 auth failure, 2 usage/IO/PAM setup error.
 *
 * Stay out of the compositor process: PAM modules can block and talk to
 * logind/keyring. Soft-fail from Ooze if this binary is missing.
 */

#define _POSIX_C_SOURCE 200809L

#include "ooze-pam-compat.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OOZE_PAM_SERVICE "ooze-lock"
#define OOZE_PAM_FALLBACK "login"
#define OOZE_MAX_PASSWORD 4096

typedef struct
{
  char *password;
  char *username;
} OozePamData;

static void
secure_clear (char *s)
{
  volatile char *p;

  if (!s)
    return;
  for (p = s; *p; p++)
    *p = '\0';
}

static int
read_password (char *buf, size_t buflen)
{
  size_t n = 0;
  int c;

  if (buflen < 2)
    return -1;

  while (n + 1 < buflen)
    {
      c = fgetc (stdin);
      if (c == EOF)
        break;
      if (c == '\0' || c == '\n')
        break;
      buf[n++] = (char) c;
    }

  buf[n] = '\0';
  return (n > 0 || !ferror (stdin)) ? 0 : -1;
}

static int
ooze_pam_conv (int                         num_msg,
               const struct pam_message  **msg,
               struct pam_response       **resp,
               void                       *appdata_ptr)
{
  OozePamData *data = appdata_ptr;
  struct pam_response *replies;
  int i;

  if (num_msg <= 0 || !msg || !resp || !data)
    return PAM_CONV_ERR;

  replies = calloc ((size_t) num_msg, sizeof (*replies));
  if (!replies)
    return PAM_CONV_ERR;

  for (i = 0; i < num_msg; i++)
    {
      const char *answer = NULL;

      switch (msg[i]->msg_style)
        {
        case PAM_PROMPT_ECHO_OFF:
          answer = data->password;
          break;
        case PAM_PROMPT_ECHO_ON:
          answer = data->username;
          break;
        case PAM_ERROR_MSG:
        case PAM_TEXT_INFO:
          break;
        default:
          free (replies);
          return PAM_CONV_ERR;
        }

      if (answer)
        {
          replies[i].resp = strdup (answer);
          if (!replies[i].resp)
            {
              int j;
              for (j = 0; j < i; j++)
                {
                  secure_clear (replies[j].resp);
                  free (replies[j].resp);
                }
              free (replies);
              return PAM_CONV_ERR;
            }
        }
    }

  *resp = replies;
  return PAM_SUCCESS;
}

static int
try_auth (const char *service, OozePamData *data)
{
  pam_handle_t *pamh = NULL;
  struct pam_conv conv = { ooze_pam_conv, data };
  int rc;

  rc = pam_start (service, data->username, &conv, &pamh);
  if (rc != PAM_SUCCESS)
    {
      fprintf (stderr, "ooze-pam-helper: pam_start(%s) failed: %s\n",
               service, pam_strerror (pamh, rc));
      if (pamh)
        pam_end (pamh, rc);
      return 2;
    }

  rc = pam_authenticate (pamh, 0);
  if (rc == PAM_SUCCESS)
    rc = pam_acct_mgmt (pamh, 0);

  if (rc != PAM_SUCCESS)
    {
      fprintf (stderr, "ooze-pam-helper: auth failed (%s): %s\n",
               service, pam_strerror (pamh, rc));
      pam_end (pamh, rc);
      return 1;
    }

  pam_end (pamh, PAM_SUCCESS);
  return 0;
}

int
main (int argc, char **argv)
{
  char password[OOZE_MAX_PASSWORD];
  OozePamData data = { 0 };
  const char *user;
  int rc;

  user = (argc > 1 && argv[1] && argv[1][0]) ? argv[1] : getlogin ();
  if (!user || !user[0])
    user = getenv ("USER");
  if (!user || !user[0])
    {
      fprintf (stderr, "ooze-pam-helper: cannot determine username\n");
      return 2;
    }

  if (read_password (password, sizeof password) != 0)
    {
      fprintf (stderr, "ooze-pam-helper: failed to read password\n");
      return 2;
    }

  data.password = password;
  data.username = (char *) user;

  /* Prefer ooze-lock when packaged; otherwise use login (nest / pre-install). */
  if (access ("/etc/pam.d/ooze-lock", R_OK) == 0)
    rc = try_auth (OOZE_PAM_SERVICE, &data);
  else
    rc = try_auth (OOZE_PAM_FALLBACK, &data);

  if (rc == 2 && access ("/etc/pam.d/ooze-lock", R_OK) == 0)
    rc = try_auth (OOZE_PAM_FALLBACK, &data);

  secure_clear (password);
  return rc;
}
