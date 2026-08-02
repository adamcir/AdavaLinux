#ifndef ADAVA_PAM_COMPAT_H
#define ADAVA_PAM_COMPAT_H

typedef struct pam_handle pam_handle_t;

struct pam_message {
    int msg_style;
    const char *msg;
};

struct pam_response {
    char *resp;
    int resp_retcode;
};

struct pam_conv {
    int (*conv)(int, const struct pam_message **, struct pam_response **, void *);
    void *appdata_ptr;
};

#define PAM_SUCCESS 0
#define PAM_PROMPT_ECHO_OFF 1
#define PAM_PROMPT_ECHO_ON 2

int pam_start(const char *service_name, const char *user, const struct pam_conv *pam_conversation, pam_handle_t **pamh);
int pam_end(pam_handle_t *pamh, int pam_status);
int pam_authenticate(pam_handle_t *pamh, int flags);
int pam_acct_mgmt(pam_handle_t *pamh, int flags);

#endif
