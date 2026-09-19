#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SYSPCKG2_VERSION "0.1.0"
#define ADAVA_REPO_ID "adavalinux"
#define FEDORA_REPO_IDS "fedora,updates"

typedef enum {
    SOURCE_AUTO = 0,
    SOURCE_ADAVA,
    SOURCE_FEDORA
} source_mode_t;

static void banner(void) {
    fprintf(stderr, "SystemPackager 2 by Adava Software for Linux in 2026 v%s\n",
            SYSPCKG2_VERSION);
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s <package>... [--source auto|adava|fedora] [-y]\n"
            "  %s install <package>... [--source auto|adava|fedora] [-y]\n"
            "  %s remove <package>... [-y]\n"
            "  %s update [package]... [--source auto|adava|fedora] [-y]\n"
            "  %s upgrade [--source auto|adava|fedora] [-y]\n"
            "  %s search <term>... [--source auto|adava|fedora]\n"
            "  %s info <package>... [--source auto|adava|fedora]\n"
            "  %s list [--source auto|adava|fedora] [DNF5 list options]\n"
            "  %s repos\n"
            "  %s clean\n"
            "\n"
            "Repository selection:\n"
            "  auto    AdavaLinux + Fedora; repository priority decides (default)\n"
            "  adava   explicitly requested packages come from AdavaLinux\n"
            "  fedora  explicitly requested packages come from Fedora\n"
            "\n"
            "Aliases: --adava, --fedora\n",
            prog, prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

static int is_command(const char *s) {
    static const char *const commands[] = {
        "install", "remove", "update", "upgrade", "search",
        "info", "list", "repo", "repos", "clean"
    };
    size_t i;
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strcmp(s, commands[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int parse_source_name(const char *value, source_mode_t *mode) {
    if (strcmp(value, "auto") == 0) {
        *mode = SOURCE_AUTO;
        return 0;
    }
    if (strcmp(value, "adava") == 0 || strcmp(value, "adavalinux") == 0) {
        *mode = SOURCE_ADAVA;
        return 0;
    }
    if (strcmp(value, "fedora") == 0) {
        *mode = SOURCE_FEDORA;
        return 0;
    }
    return -1;
}

static const char *repo_ids_for_source(source_mode_t mode) {
    switch (mode) {
        case SOURCE_ADAVA:
            return ADAVA_REPO_ID;
        case SOURCE_FEDORA:
            return FEDORA_REPO_IDS;
        default:
            return NULL;
    }
}

static int push_arg(char **out, size_t capacity, size_t *count, char *value) {
    if (*count + 1 >= capacity) {
        return -1;
    }
    out[(*count)++] = value;
    out[*count] = NULL;
    return 0;
}

int main(int argc, char **argv) {
    source_mode_t source = SOURCE_AUTO;
    char **args;
    size_t arg_count = 0;
    size_t i;
    int short_install = 0;
    const char *command;
    const char *backend_env;
    char *backend;
    size_t out_capacity;
    char **out;
    size_t out_count = 0;
    const char *repo_ids;
    char repo_global[128];
    char repo_from[128];

    banner();

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0) {
        printf("%s\n", SYSPCKG2_VERSION);
        return 0;
    }

    args = calloc((size_t)argc + 1, sizeof(*args));
    if (!args) {
        perror("calloc");
        return 1;
    }

    for (i = 1; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--adava") == 0) {
            source = SOURCE_ADAVA;
            continue;
        }
        if (strcmp(argv[i], "--fedora") == 0) {
            source = SOURCE_FEDORA;
            continue;
        }
        if (strncmp(argv[i], "--source=", 9) == 0) {
            if (parse_source_name(argv[i] + 9, &source) != 0) {
                fprintf(stderr, "ERR: Unknown source: %s\n", argv[i] + 9);
                free(args);
                return 1;
            }
            continue;
        }
        if (strcmp(argv[i], "--source") == 0) {
            if (i + 1 >= (size_t)argc ||
                parse_source_name(argv[i + 1], &source) != 0) {
                fprintf(stderr, "ERR: --source expects auto, adava or fedora\n");
                free(args);
                return 1;
            }
            ++i;
            continue;
        }
        args[arg_count++] = argv[i];
    }
    args[arg_count] = NULL;

    if (arg_count == 0) {
        usage(argv[0]);
        free(args);
        return 1;
    }

    command = args[0];
    if (!is_command(command)) {
        command = "install";
        short_install = 1;
    }

    backend_env = getenv("SYSPCKG2_BACKEND");
    backend = (char *)((backend_env && *backend_env) ? backend_env : "dnf5");

    out_capacity = arg_count + 12;
    out = calloc(out_capacity, sizeof(*out));
    if (!out) {
        perror("calloc");
        free(args);
        return 1;
    }

    if (push_arg(out, out_capacity, &out_count, backend) != 0) {
        goto overflow;
    }

    repo_ids = repo_ids_for_source(source);

    if (strcmp(command, "repos") == 0) {
        if (push_arg(out, out_capacity, &out_count, "repo") != 0 ||
            push_arg(out, out_capacity, &out_count, "list") != 0) {
            goto overflow;
        }
    } else if (strcmp(command, "clean") == 0) {
        if (push_arg(out, out_capacity, &out_count, "clean") != 0 ||
            push_arg(out, out_capacity, &out_count, "all") != 0) {
            goto overflow;
        }
    } else {
        if (source != SOURCE_AUTO && strcmp(command, "install") != 0) {
            if (snprintf(repo_global, sizeof(repo_global), "--repo=%s", repo_ids) >=
                (int)sizeof(repo_global)) {
                fprintf(stderr, "ERR: Repository selector too long\n");
                free(out);
                free(args);
                return 1;
            }
            if (push_arg(out, out_capacity, &out_count, repo_global) != 0) {
                goto overflow;
            }
        }

        if (strcmp(command, "update") == 0) {
            command = "upgrade";
        }

        if (push_arg(out, out_capacity, &out_count, (char *)command) != 0) {
            goto overflow;
        }

        if (source != SOURCE_AUTO && strcmp(command, "install") == 0) {
            if (snprintf(repo_from, sizeof(repo_from), "--from-repo=%s", repo_ids) >=
                (int)sizeof(repo_from)) {
                fprintf(stderr, "ERR: Repository selector too long\n");
                free(out);
                free(args);
                return 1;
            }
            if (push_arg(out, out_capacity, &out_count, repo_from) != 0) {
                goto overflow;
            }
        }

        if (short_install) {
            for (i = 0; i < arg_count; ++i) {
                if (push_arg(out, out_capacity, &out_count, args[i]) != 0) {
                    goto overflow;
                }
            }
        } else {
            for (i = 1; i < arg_count; ++i) {
                if (push_arg(out, out_capacity, &out_count, args[i]) != 0) {
                    goto overflow;
                }
            }
        }
    }

    execvp(backend, out);

    fprintf(stderr,
            "ERR: Unable to start DNF5 backend '%s': %s\n"
            "Install the dnf5 + rpm runtime before using syspckg2.\n",
            backend, strerror(errno));
    free(out);
    free(args);
    return 127;

overflow:
    fprintf(stderr, "ERR: Too many arguments\n");
    free(out);
    free(args);
    return 1;
}
