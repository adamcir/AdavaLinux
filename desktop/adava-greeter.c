#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "pam_compat.h"

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct credentials {
    const char *user;
    const char *password;
};

static int conversation(int count, const struct pam_message **messages,
                        struct pam_response **responses, void *data) {
    const struct credentials *credentials = data;
    struct pam_response *reply = calloc((size_t)count, sizeof(*reply));
    if (reply == NULL) {
        return 19;
    }
    for (int i = 0; i < count; i++) {
        const char *value = NULL;
        if (messages[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
            value = credentials->password;
        } else if (messages[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            value = credentials->user;
        }
        if (value != NULL) {
            reply[i].resp = strdup(value);
            if (reply[i].resp == NULL) {
                for (int j = 0; j < i; j++) {
                    free(reply[j].resp);
                }
                free(reply);
                return 19;
            }
        }
    }
    *responses = reply;
    return PAM_SUCCESS;
}

static int authenticate(const char *user, const char *password) {
    struct credentials credentials = { user, password };
    struct pam_conv conv = { conversation, &credentials };
    pam_handle_t *handle = NULL;
    int status = pam_start("adava-greeter", user, &conv, &handle);
    if (status == PAM_SUCCESS) {
        status = pam_authenticate(handle, 0);
    }
    if (status == PAM_SUCCESS) {
        status = pam_acct_mgmt(handle, 0);
    }
    if (handle != NULL) {
        (void)pam_end(handle, status);
    }
    return status == PAM_SUCCESS;
}

static int start_session(const char *user) {
    struct passwd *account = getpwnam(user);
    if (account == NULL || account->pw_uid == 0 || account->pw_uid < 1000) {
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        return 0;
    }
    if (pid == 0) {
        (void)initgroups(account->pw_name, account->pw_gid);
        if (setgid(account->pw_gid) != 0 || setuid(account->pw_uid) != 0) {
            _exit(126);
        }
        (void)setenv("HOME", account->pw_dir, 1);
        (void)setenv("USER", account->pw_name, 1);
        (void)setenv("SHELL", account->pw_shell, 1);
        execl("/usr/bin/adava-session", "adava-session", (char *)NULL);
        _exit(127);
    }
    int status = 0;
    return waitpid(pid, &status, 0) > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void draw(Display *display, Window window, GC gc, const char *user,
                 const char *password, const char *status) {
    char hidden[128];
    const size_t len = strlen(password) < sizeof(hidden) - 1 ? strlen(password) : sizeof(hidden) - 1;
    memset(hidden, '*', len);
    hidden[len] = '\0';
    XClearWindow(display, window);
    XDrawString(display, window, gc, 56, 72, "AdavaLinux", 10);
    XDrawString(display, window, gc, 56, 118, "User:", 5);
    XDrawString(display, window, gc, 150, 118, user, (int)strlen(user));
    XDrawString(display, window, gc, 56, 154, "Password:", 9);
    XDrawString(display, window, gc, 150, 154, hidden, (int)strlen(hidden));
    XDrawString(display, window, gc, 56, 212, status, (int)strlen(status));
}

int main(void) {
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fputs("adava-greeter: cannot open X display\n", stderr);
        return 1;
    }
    const int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, 480, 280, 0,
                                        BlackPixel(display, screen), WhitePixel(display, screen));
    XSelectInput(display, window, ExposureMask | KeyPressMask);
    XMapRaised(display, window);
    GC gc = XCreateGC(display, window, 0, NULL);
    char user[64] = "";
    char password[128] = "";
    char status[96] = "Enter your user name and password";
    int field = 0;

    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == Expose) {
            draw(display, window, gc, user, password, status);
            continue;
        }
        if (event.type != KeyPress) {
            continue;
        }
        char text[16];
        KeySym key;
        int count = XLookupString(&event.xkey, text, sizeof(text), &key, NULL);
        char *value = field == 0 ? user : password;
        size_t value_size = field == 0 ? sizeof(user) : sizeof(password);
        if (key == XK_Return) {
            if (field == 0) {
                field = 1;
                snprintf(status, sizeof(status), "%s", "Enter password");
            } else if (user[0] != '\0' && authenticate(user, password) && start_session(user)) {
                password[0] = '\0';
                field = 0;
                snprintf(status, sizeof(status), "%s", "Session ended");
            } else {
                password[0] = '\0';
                field = 1;
                snprintf(status, sizeof(status), "%s", "Authentication failed");
            }
        } else if (key == XK_BackSpace) {
            size_t length = strlen(value);
            if (length > 0) {
                value[length - 1] = '\0';
            }
        } else if (count > 0 && text[0] >= 32 && text[0] < 127) {
            size_t length = strlen(value);
            if (length + (size_t)count < value_size) {
                memcpy(value + length, text, (size_t)count);
                value[length + (size_t)count] = '\0';
            }
        }
        draw(display, window, gc, user, password, status);
    }
}
