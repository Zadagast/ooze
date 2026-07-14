/* Minimal PAM declarations so we can link libpam without libpam0g-dev.
 * Prefer the system <security/pam_appl.h> when present. */
#pragma once

#if defined(__has_include)
#  if __has_include(<security/pam_appl.h>)
#    include <security/pam_appl.h>
#    define OOZE_HAVE_SYSTEM_PAM_APPL 1
#  endif
#endif

#ifndef OOZE_HAVE_SYSTEM_PAM_APPL

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pam_handle pam_handle_t;

struct pam_message
{
  int msg_style;
  const char *msg;
};

struct pam_response
{
  char *resp;
  int resp_retcode;
};

struct pam_conv
{
  int (*conv) (int num_msg,
               const struct pam_message **msg,
               struct pam_response **resp,
               void *appdata_ptr);
  void *appdata_ptr;
};

#define PAM_SUCCESS               0
#define PAM_AUTH_ERR              7
#define PAM_CONV_ERR              19

#define PAM_PROMPT_ECHO_OFF       1
#define PAM_PROMPT_ECHO_ON        2
#define PAM_ERROR_MSG             3
#define PAM_TEXT_INFO             4

int pam_start (const char *service_name,
               const char *user,
               const struct pam_conv *pam_conversation,
               pam_handle_t **pamh);
int pam_authenticate (pam_handle_t *pamh, int flags);
int pam_acct_mgmt (pam_handle_t *pamh, int flags);
int pam_end (pam_handle_t *pamh, int pam_status);
const char *pam_strerror (pam_handle_t *pamh, int errnum);

#ifdef __cplusplus
}
#endif

#endif /* !OOZE_HAVE_SYSTEM_PAM_APPL */
