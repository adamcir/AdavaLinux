#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>

#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RESET "\033[0m"

#ifndef SYSPCKG_SOURCE_FILE
#define SYSPCKG_SOURCE_FILE "/etc/syspckg/syspckg-source"
#endif

#ifndef SYSPCKG_VERSION_FILE
#define SYSPCKG_VERSION_FILE "/etc/syspckg/syspckg-version"
#endif

#define SYSPCKG_VERSION "1.0"
#define DEFAULT_SOURCE_URL "http://192.168.10.7/products/AdavaLinux"

typedef struct installed_pkg installed_pkg_t;

static int get_os_version(const char *root, char *out, size_t out_sz);
static int is_safe_version(const char *v);
static int get_pkg_base(const char *input, char *out, size_t out_sz);
static int has_digit(const char *s);
static int selector_has_explicit_version(const char *selector);
static int resolve_latest_local(const char *dir, const char *base, char *out_name, size_t out_sz);
static int resolve_latest_remote(const char *root, const char *base, char *out_name, size_t out_sz);
static int resolve_latest_pkg_dir(const char *root, const char *base, char *out_name, size_t out_sz);
static int list_local_installed_packages(const char *root);
static int collect_installed_update_packages(const char *root, installed_pkg_t **out, size_t *out_count);
static int latest_boot_version(const char *root, const char *prefix, const char *suffix,
                               char *latest, size_t latest_sz,
                               char *previous, size_t previous_sz);
static void free_strings(char **arr, size_t count);

static char g_tmpdir[PATH_MAX];
static char g_tmp_deps_path[PATH_MAX];

static void log_err(const char *msg) {
    fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "%s\n", msg);
}

static void log_ok(const char *msg) {
    printf(COLOR_GREEN "OK: " COLOR_RESET "%s\n", msg);
}

static void log_info(const char *msg) {
    printf("%s\n", msg);
}

static int run_cmd(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("ERR: fork");
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        perror("ERR: execvp");
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("ERR: waitpid");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Command failed: %s\n", argv[0]);
        return -1;
    }
    return 0;
}

/* Every package install/removal can change shared-library SONAMEs.  The
 * AdavaLinux runtime libraries live in /usr/lib and, for some imported
 * packages, /usr/lib/x86_64-linux-gnu; both are configured in ld.so.conf.
 * Rebuild the target cache once the transaction has finished. */
static int refresh_dynamic_linker_cache(const char *root) {
    char ldconfig_path[PATH_MAX];
    if (snprintf(ldconfig_path, sizeof(ldconfig_path), "%s/sbin/ldconfig",
                 strcmp(root, "/") == 0 ? "" : root) >= (int)sizeof(ldconfig_path)) {
        return -1;
    }
    if (access(ldconfig_path, X_OK) != 0) {
        return 0;
    }
    char *argv[] = { "/sbin/ldconfig", "-r", (char *)root, NULL };
    return run_cmd(argv);
}

static int copy_archive_to_dir(const char *archive_path, const char *output_dir) {
    const char *base = strrchr(archive_path, '/');
    char output_path[PATH_MAX];
    FILE *in;
    FILE *out;
    char buffer[32768];
    size_t n;

    base = base ? base + 1 : archive_path;
    if (snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, base) >= (int)sizeof(output_path)) {
        return -1;
    }
    in = fopen(archive_path, "rb");
    out = fopen(output_path, "wb");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return -1;
    }
    while ((n = fread(buffer, 1, sizeof(buffer), in)) != 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    int rc = ferror(in) || fclose(in) != 0 || fclose(out) != 0 ? -1 : 0;
    return rc;
}

static void cleanup_tmpdir(void) {
    if (g_tmp_deps_path[0] != '\0') {
        unlink(g_tmp_deps_path);
        g_tmp_deps_path[0] = '\0';
    }
    if (g_tmpdir[0] == '\0') {
        return;
    }
    char *rm_argv[] = { "rm", "-rf", g_tmpdir, NULL };
    run_cmd(rm_argv);
    g_tmpdir[0] = '\0';
}

static void handle_signal(int sig) {
    cleanup_tmpdir();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_signal_handlers(void) {
    signal(SIGSEGV, handle_signal);
    signal(SIGABRT, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
}

static int has_suffix(const char *s, const char *suffix) {
    size_t sl = strlen(s);
    size_t su = strlen(suffix);
    if (sl < su) {
        return 0;
    }
    return strcmp(s + sl - su, suffix) == 0;
}

static int file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

static int is_url(const char *s) {
    return s != NULL &&
           (strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0);
}

static int is_packager_pkg_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, "syspckg") == 0 || strncmp(name, "syspckg-", 8) == 0;
}

static char *trim_whitespace(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    return s;
}

static int get_source_url(char *out, size_t out_sz) {
    FILE *fp = fopen(SYSPCKG_SOURCE_FILE, "r");
    if (!fp) {
        return snprintf(out, out_sz, "%s", DEFAULT_SOURCE_URL) < (int)out_sz ? 0 : -1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim_whitespace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }
        if (strncmp(trimmed, "SOURCE_URL", 10) != 0) {
            continue;
        }

        trimmed += 10;
        while (*trimmed && isspace((unsigned char)*trimmed)) {
            trimmed++;
        }
        if (*trimmed != '=') {
            continue;
        }
        trimmed++;
        trimmed = trim_whitespace(trimmed);
        if (trimmed[0] == '\0') {
            continue;
        }

        fclose(fp);
        return snprintf(out, out_sz, "%s", trimmed) < (int)out_sz ? 0 : -1;
    }

    fclose(fp);
    return snprintf(out, out_sz, "%s", DEFAULT_SOURCE_URL) < (int)out_sz ? 0 : -1;
}

static int is_core_lib_path(const char *path) {
    if (!path) {
        return 0;
    }
    return has_suffix(path, "/libc.so.6") || has_suffix(path, "/ld-linux-x86-64.so.2");
}

static int download_pkg_wget(const char *url, const char *out_path) {
    char *wget_argv[] = { "wget", "-O", (char *)out_path, (char *)url, NULL };
    return run_cmd(wget_argv);
}

static int join_url2(const char *base, const char *part, char *out, size_t out_sz) {
    size_t base_len = strlen(base);
    const char *slash = (base_len > 0 && base[base_len - 1] == '/') ? "" : "/";
    return snprintf(out, out_sz, "%s%s%s", base, slash, part) < (int)out_sz ? 0 : -1;
}

static int is_pkg_token_char(int c) {
    return isalnum(c) || c == '.' || c == '_' || c == '-' || c == '+';
}

static int name_exists(char **names, size_t count, const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int list_remote_packages(const char *root) {
    char version[64];
    char source_url[PATH_MAX];
    if (get_os_version(root, version, sizeof(version)) != 0) {
        fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "VERSION not found in %s/etc/os-release or %s/usr/lib/os-release\n",
                root, root);
        return -1;
    }
    if (!is_safe_version(version)) {
        fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Invalid VERSION value: %s\n", version);
        return -1;
    }
    if (get_source_url(source_url, sizeof(source_url)) != 0) {
        log_err("Failed to resolve source URL");
        return -1;
    }

    char url[PATH_MAX];
    if (snprintf(url, sizeof(url),
                 "%s/%s/packages/",
                 source_url, version) >= (int)sizeof(url)) {
        log_err("URL too long");
        return -1;
    }

    char cmd[PATH_MAX + 32];
    if (snprintf(cmd, sizeof(cmd), "wget -qO- %s", url) >= (int)sizeof(cmd)) {
        log_err("Command too long");
        return -1;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("ERR: popen");
        return -1;
    }

    char **names = NULL;
    size_t count = 0;
    char line[1024];
    const char *suffix = ".syspckg";
    size_t suffix_len = strlen(suffix);

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while ((p = strstr(p, suffix)) != NULL) {
            char *end = p + suffix_len;
            char *start = p;
            while (start > line && is_pkg_token_char((unsigned char)start[-1])) {
                start--;
            }
            size_t len = (size_t)(end - start);
            if (len > 0 && len < PATH_MAX) {
                char name[PATH_MAX];
                memcpy(name, start, len);
                name[len] = '\0';
                if (!name_exists(names, count, name)) {
                    char *copy = strdup(name);
                    if (!copy) {
                        pclose(fp);
                        for (size_t i = 0; i < count; i++) {
                            free(names[i]);
                        }
                        free(names);
                        return -1;
                    }
                    char **next = realloc(names, (count + 1) * sizeof(*names));
                    if (!next) {
                        free(copy);
                        pclose(fp);
                        for (size_t i = 0; i < count; i++) {
                            free(names[i]);
                        }
                        free(names);
                        return -1;
                    }
                    names = next;
                    names[count++] = copy;
                }
            }
            p = end;
        }
    }

    int status = pclose(fp);
    if (status == -1) {
        perror("ERR: pclose");
        for (size_t i = 0; i < count; i++) {
            free(names[i]);
        }
        free(names);
        return -1;
    }

    if (count == 0) {
        log_info("No packages found on server.");
        return 0;
    }

    for (size_t i = 0; i < count; i++) {
        printf("%s\n", names[i]);
        free(names[i]);
    }
    free(names);
    return 0;
}

static int cmp_string_ptrs(const void *a, const void *b) {
    const char *const *sa = a;
    const char *const *sb = b;
    return strcmp(*sa, *sb);
}

static int list_local_installed_packages(const char *root) {
    char dir[PATH_MAX];
    DIR *d;
    struct dirent *de;
    char **names = NULL;
    size_t count = 0;
    const char *suffix = ".list";
    size_t suffix_len = strlen(suffix);

    if (snprintf(dir, sizeof(dir), "%s/var/lib/syspckg/installed", root) >= (int)sizeof(dir)) {
        return -1;
    }
    d = opendir(dir);
    if (!d) {
        return -1;
    }
    while ((de = readdir(d)) != NULL) {
        size_t len = strlen(de->d_name);
        char name[PATH_MAX];
        char *copy;
        char **next;

        if (len <= suffix_len || strcmp(de->d_name + len - suffix_len, suffix) != 0) {
            continue;
        }
        if (len - suffix_len + 1 > sizeof(name)) {
            closedir(d);
            free_strings(names, count);
            return -1;
        }
        memcpy(name, de->d_name, len - suffix_len);
        name[len - suffix_len] = '\0';
        copy = strdup(name);
        if (!copy) {
            closedir(d);
            free_strings(names, count);
            return -1;
        }
        next = realloc(names, (count + 1) * sizeof(*names));
        if (!next) {
            free(copy);
            closedir(d);
            free_strings(names, count);
            return -1;
        }
        names = next;
        names[count++] = copy;
    }
    closedir(d);
    if (count == 0) {
        log_info("No locally installed packages found.");
        return 0;
    }
    qsort(names, count, sizeof(*names), cmp_string_ptrs);
    for (size_t i = 0; i < count; i++) {
        printf("%s\n", names[i]);
    }
    free_strings(names, count);
    return 0;
}

static int collect_remote_tokens(const char *url, const char *suffix, char ***out, size_t *out_count) {
    char cmd[PATH_MAX + 32];
    FILE *fp;
    char **names = NULL;
    size_t count = 0;
    char line[1024];
    size_t suffix_len = strlen(suffix);

    *out = NULL;
    *out_count = 0;

    if (snprintf(cmd, sizeof(cmd), "wget -qO- %s", url) >= (int)sizeof(cmd)) {
        return -1;
    }
    fp = popen(cmd, "r");
    if (!fp) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while ((p = strstr(p, suffix)) != NULL) {
            char *end = p + suffix_len;
            char *start = p;
            while (start > line && is_pkg_token_char((unsigned char)start[-1])) {
                start--;
            }
            size_t len = (size_t)(end - start);
            if (len > 0 && len < PATH_MAX) {
                char name[PATH_MAX];
                memcpy(name, start, len);
                name[len] = '\0';
                if (!name_exists(names, count, name)) {
                    char *copy = strdup(name);
                    char **next;
                    if (!copy) {
                        pclose(fp);
                        free_strings(names, count);
                        return -1;
                    }
                    next = realloc(names, (count + 1) * sizeof(*names));
                    if (!next) {
                        free(copy);
                        pclose(fp);
                        free_strings(names, count);
                        return -1;
                    }
                    names = next;
                    names[count++] = copy;
                }
            }
            p = end;
        }
    }
    if (pclose(fp) == -1) {
        free_strings(names, count);
        return -1;
    }
    *out = names;
    *out_count = count;
    return 0;
}

static int strip_syspckg_suffix(const char *name, char *out, size_t out_sz) {
    size_t nl = strlen(name);
    const char *suffix = ".syspckg";
    size_t sl = strlen(suffix);
    if (nl >= sl && strcmp(name + nl - sl, suffix) == 0) {
        if (nl - sl + 1 > out_sz) {
            return -1;
        }
        memcpy(out, name, nl - sl);
        out[nl - sl] = '\0';
        return 0;
    }
    if (nl + 1 > out_sz) {
        return -1;
    }
    snprintf(out, out_sz, "%s", name);
    return 0;
}

static int find_install_dir(const char *tmpdir, char *out, size_t out_sz) {
    DIR *d = opendir(tmpdir);
    if (!d) {
        perror("ERR: opendir");
        return -1;
    }
    struct dirent *de;
    int found = 0;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s/%s", tmpdir, de->d_name);
        struct stat st;
        if (stat(candidate, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }
        char info_path[PATH_MAX];
        if (snprintf(info_path, sizeof(info_path), "%s/syspckg-info", candidate) >= (int)sizeof(info_path)) {
            continue;
        }
        if (access(info_path, R_OK) == 0) {
            if (found) {
                log_err("Multiple syspckg-info found in package");
                closedir(d);
                return -1;
            }
            snprintf(out, out_sz, "%s", candidate);
            found = 1;
        }
    }
    closedir(d);
    if (!found) {
        log_err("syspckg-info not found in package");
        return -1;
    }
    return 0;
}

static int is_adavalinux_root(const char *root) {
    char path[PATH_MAX];
    FILE *f = NULL;

    if (snprintf(path, sizeof(path), "%s/etc/os-release", root) >= (int)sizeof(path)) {
        return 0;
    }

    f = fopen(path, "r");
    if (!f) {
        return 0;
    }

    char line[256];
    int match = 0;
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        if (strncmp(p, "ID=", 3) != 0) {
            continue;
        }
        p += 3;
        if (*p == '"' || *p == '\'') {
            p++;
        }
        if (strncmp(p, "adavalinux", 10) == 0) {
            match = 1;
            break;
        }
    }
    fclose(f);
    return match;
}

static int read_key_from_file(const char *path, const char *key, char *out, size_t out_sz) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }

    char line[256];
    int found = 0;
    size_t key_len = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        if (strncmp(p, key, key_len) != 0 || p[key_len] != '=') {
            continue;
        }
        p += key_len + 1;
        if (*p == '"' || *p == '\'') {
            p++;
        }
        size_t len = strcspn(p, "\"'\r\n");
        if (len + 1 > out_sz) {
            fclose(f);
            return -1;
        }
        memcpy(out, p, len);
        out[len] = '\0';
        found = 1;
        break;
    }
    fclose(f);
    return found ? 0 : -1;
}

static int get_packager_version(const char *root, char *out, size_t out_sz) {
    char path[PATH_MAX];
    char line[128];

    if (snprintf(path, sizeof(path), "%s%s", root, SYSPCKG_VERSION_FILE) < (int)sizeof(path)) {
        FILE *f = fopen(path, "r");
        if (f != NULL) {
            if (fgets(line, sizeof(line), f) != NULL) {
                char *trimmed;
                fclose(f);
                trimmed = trim_whitespace(line);
                if (is_safe_version(trimmed) &&
                    snprintf(out, out_sz, "%s", trimmed) < (int)out_sz) {
                    return 0;
                }
            } else {
                fclose(f);
            }
        }
    }
    return snprintf(out, out_sz, "%s", SYSPCKG_VERSION) < (int)out_sz ? 0 : -1;
}

static int read_version_from_file(const char *path, char *out, size_t out_sz) {
    if (read_key_from_file(path, "VERSION", out, out_sz) == 0) {
        return 0;
    }
    return read_key_from_file(path, "VERSION_ID", out, out_sz);
}

static int read_id_from_file(const char *path, char *out, size_t out_sz) {
    return read_key_from_file(path, "ID", out, out_sz);
}

static int get_os_version(const char *root, char *out, size_t out_sz) {
    char path[PATH_MAX];

    if (snprintf(path, sizeof(path), "%s/etc/os-release", root) >= (int)sizeof(path)) {
        return -1;
    }
    if (read_version_from_file(path, out, out_sz) == 0) {
        return 0;
    }

    if (snprintf(path, sizeof(path), "%s/usr/lib/os-release", root) >= (int)sizeof(path)) {
        return -1;
    }
    return read_version_from_file(path, out, out_sz);
}

static int get_os_id(const char *root, char *out, size_t out_sz) {
    char path[PATH_MAX];

    if (snprintf(path, sizeof(path), "%s/etc/os-release", root) >= (int)sizeof(path)) {
        return -1;
    }
    if (read_id_from_file(path, out, out_sz) == 0) {
        return 0;
    }

    if (snprintf(path, sizeof(path), "%s/usr/lib/os-release", root) >= (int)sizeof(path)) {
        return -1;
    }
    return read_id_from_file(path, out, out_sz);
}

static int is_safe_version(const char *v) {
    if (!v || *v == '\0') {
        return 0;
    }
    for (const char *p = v; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') ||
            (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '.' || *p == '_' || *p == '-' || *p == '+') {
            continue;
        }
        return 0;
    }
    return 1;
}

static int is_safe_id(const char *v) {
    if (!v || *v == '\0') {
        return 0;
    }
    for (const char *p = v; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') ||
            (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '.' || *p == '_' || *p == '-') {
            continue;
        }
        return 0;
    }
    return 1;
}

static int is_safe_pkgname(const char *v) {
    if (!v || *v == '\0') {
        return 0;
    }
    for (const char *p = v; *p; p++) {
        if (!is_pkg_token_char((unsigned char)*p)) {
            return 0;
        }
    }
    return 1;
}

static int read_syspckg_info(const char *path, char *pkg_version, size_t pkg_ver_sz,
                             char *id, size_t id_sz, char *version, size_t ver_sz) {
    if (read_key_from_file(path, "PKG_VERSION", pkg_version, pkg_ver_sz) != 0) {
        return -1;
    }
    if (read_key_from_file(path, "ID", id, id_sz) != 0) {
        return -1;
    }
    if (read_key_from_file(path, "VERSION", version, ver_sz) != 0 &&
        read_key_from_file(path, "VERSION_ID", version, ver_sz) != 0) {
        return -1;
    }
    return 0;
}

static int read_syspckg_deps(const char *path, char ***out, size_t *out_count) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    char **deps = NULL;
    size_t count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "DEP=", 4) != 0) {
            continue;
        }
        char *val = line + 4;
        val[strcspn(val, "\r\n")] = '\0';
        if (!is_safe_pkgname(val)) {
            continue;
        }
        if (name_exists(deps, count, val)) {
            continue;
        }
        char *copy = strdup(val);
        if (!copy) {
            fclose(f);
            for (size_t i = 0; i < count; i++) {
                free(deps[i]);
            }
            free(deps);
            return -1;
        }
        char **next = realloc(deps, (count + 1) * sizeof(*deps));
        if (!next) {
            free(copy);
            fclose(f);
            for (size_t i = 0; i < count; i++) {
                free(deps[i]);
            }
            free(deps);
            return -1;
        }
        deps = next;
        deps[count++] = copy;
    }
    fclose(f);
    *out = deps;
    *out_count = count;
    return 0;
}

static int is_pkg_installed(const char *root, const char *pkg_name) {
    char manifest_path[PATH_MAX];
    if (snprintf(manifest_path, sizeof(manifest_path),
                 "%s/var/lib/syspckg/installed/%s.list", root, pkg_name) >= (int)sizeof(manifest_path)) {
        return 0;
    }
    return access(manifest_path, F_OK) == 0;
}

typedef struct {
    char selector[PATH_MAX];
    char pkg_name[PATH_MAX];
    char archive_path[PATH_MAX];
    char install_name[PATH_MAX];
    char pkg_id[64];
    char pkg_version[64];
    char pkg_ver[64];
    char extract_dir[PATH_MAX];
    char install_dir[PATH_MAX];
    char installed_name[PATH_MAX];
    char **deps;
    size_t dep_count;
    int installed;
    int extracted;
} pkg_plan_t;

struct installed_pkg {
    char base[PATH_MAX];
    char installed_name[PATH_MAX];
    char installed_version[PATH_MAX];
};

static int push_string(char ***arr, size_t *count, const char *value) {
    char **next = realloc(*arr, (*count + 1) * sizeof(**arr));
    if (!next) {
        return -1;
    }
    *arr = next;
    (*arr)[*count] = strdup(value);
    if (!(*arr)[*count]) {
        return -1;
    }
    (*count)++;
    return 0;
}

static void free_strings(char **arr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(arr[i]);
    }
    free(arr);
}

static int run_cmd_to_file(char *const argv[], const char *out_path) {
    int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return -1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(fd);
        return -1;
    }
    if (pid == 0) {
        if (dup2(fd, STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(fd);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fd);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    return 0;
}

static int make_tmp_file(char *out_path, size_t out_sz) {
    if (snprintf(out_path, out_sz, "%s/file-XXXXXX", g_tmpdir) >= (int)out_sz) {
        return -1;
    }
    int fd = mkstemp(out_path);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    return 0;
}

static int make_tmp_deps_file(char *out_path, size_t out_sz) {
    if (snprintf(out_path, out_sz, "/tmp/syspckg-deps.XXXXXX") >= (int)out_sz) {
        return -1;
    }
    int fd = mkstemp(out_path);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    snprintf(g_tmp_deps_path, sizeof(g_tmp_deps_path), "%s", out_path);
    return 0;
}

static int find_tar_entry_with_suffix(const char *archive_path, const char *suffix,
                                      char *out_entry, size_t out_sz) {
    char list_path[PATH_MAX];
    if (make_tmp_file(list_path, sizeof(list_path)) != 0) {
        return -1;
    }
    char *argv[] = { "tar", "-tJf", (char *)archive_path, NULL };
    if (run_cmd_to_file(argv, list_path) != 0) {
        unlink(list_path);
        return -1;
    }
    FILE *f = fopen(list_path, "r");
    if (!f) {
        unlink(list_path);
        return -1;
    }
    char line[PATH_MAX];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (has_suffix(line, suffix)) {
            if (snprintf(out_entry, out_sz, "%s", line) >= (int)out_sz) {
                fclose(f);
                unlink(list_path);
                return -1;
            }
            found = 1;
            break;
        }
    }
    fclose(f);
    unlink(list_path);
    return found ? 0 : -1;
}

static int extract_tar_entry_to_file(const char *archive_path, const char *entry, const char *out_path) {
    char *argv[] = { "tar", "-xJf", (char *)archive_path, "-O", (char *)entry, NULL };
    return run_cmd_to_file(argv, out_path);
}

static int read_pkg_metadata_from_archive(const char *archive_path,
                                          char *pkg_ver, size_t pkg_ver_sz,
                                          char *pkg_id, size_t pkg_id_sz,
                                          char *pkg_version, size_t pkg_version_sz,
                                          char ***deps, size_t *dep_count) {
    char info_entry[PATH_MAX];
    if (find_tar_entry_with_suffix(archive_path, "/syspckg-info", info_entry, sizeof(info_entry)) != 0) {
        return -1;
    }
    char info_path[PATH_MAX];
    if (make_tmp_file(info_path, sizeof(info_path)) != 0) {
        return -1;
    }
    if (extract_tar_entry_to_file(archive_path, info_entry, info_path) != 0) {
        unlink(info_path);
        return -1;
    }
    int rc = read_syspckg_info(info_path, pkg_ver, pkg_ver_sz, pkg_id, pkg_id_sz, pkg_version, pkg_version_sz);
    unlink(info_path);
    if (rc != 0) {
        return -1;
    }

    *deps = NULL;
    *dep_count = 0;
    char deps_entry[PATH_MAX];
    if (find_tar_entry_with_suffix(archive_path, "/syspckg-deps", deps_entry, sizeof(deps_entry)) != 0) {
        return 0;
    }
    char deps_path[PATH_MAX];
    if (make_tmp_deps_file(deps_path, sizeof(deps_path)) != 0) {
        return -1;
    }
    if (extract_tar_entry_to_file(archive_path, deps_entry, deps_path) != 0) {
        unlink(deps_path);
        g_tmp_deps_path[0] = '\0';
        return -1;
    }
    rc = read_syspckg_deps(deps_path, deps, dep_count);
    unlink(deps_path);
    g_tmp_deps_path[0] = '\0';
    return rc;
}

static int resolve_pkg_archive_path(const char *root, const char *tmpdir,
                                    const char *selector, int local_only,
                                    char *out_pkg_name, size_t out_pkg_name_sz,
                                    char *out_archive_path, size_t out_archive_path_sz) {
    if (is_url(selector)) {
        const char *base = strrchr(selector, '/');
        char file_name[PATH_MAX];
        char out_path[PATH_MAX];

        if (local_only || base == NULL || base[1] == '\0') {
            return -1;
        }
        base++;
        if (!has_suffix(base, ".syspckg")) {
            return -1;
        }
        if (snprintf(file_name, sizeof(file_name), "%s", base) >= (int)sizeof(file_name) ||
            get_pkg_base(file_name, out_pkg_name, out_pkg_name_sz) != 0 ||
            snprintf(out_path, sizeof(out_path), "%s/%s", tmpdir, file_name) >= (int)sizeof(out_path)) {
            return -1;
        }
        if (!file_exists(out_path)) {
            char msg[PATH_MAX + 64];
            snprintf(msg, sizeof(msg), "Downloading %s", selector);
            log_info(msg);
            if (download_pkg_wget(selector, out_path) != 0) {
                return -1;
            }
            log_ok("Download complete");
        }
        return snprintf(out_archive_path, out_archive_path_sz, "%s", out_path) < (int)out_archive_path_sz ? 0 : -1;
    }

    char selector_name[PATH_MAX];
    if (snprintf(selector_name, sizeof(selector_name), "%s", selector) >= (int)sizeof(selector_name)) {
        return -1;
    }

    if (!strchr(selector, '/') &&
        !has_suffix(selector, ".syspckg") &&
        !selector_has_explicit_version(selector)) {
        if (resolve_latest_local(".", selector, selector_name, sizeof(selector_name)) != 0 &&
            resolve_latest_pkg_dir(root, selector, selector_name, sizeof(selector_name)) != 0) {
            if (local_only ||
                resolve_latest_remote(root, selector, selector_name, sizeof(selector_name)) != 0) {
                return -1;
            }
        }
    }

    char pkg_name[PATH_MAX];
    if (get_pkg_base(selector_name, pkg_name, sizeof(pkg_name)) != 0) {
        return -1;
    }
    if (snprintf(out_pkg_name, out_pkg_name_sz, "%s", pkg_name) >= (int)out_pkg_name_sz) {
        return -1;
    }

    if (strchr(selector, '/')) {
        if (!file_exists(selector)) {
            return -1;
        }
        if (snprintf(out_archive_path, out_archive_path_sz, "%s", selector) >= (int)out_archive_path_sz) {
            return -1;
        }
        return 0;
    }

    char file_name[PATH_MAX];
    if (has_suffix(selector_name, ".syspckg")) {
        if (snprintf(file_name, sizeof(file_name), "%s", selector_name) >= (int)sizeof(file_name)) {
            return -1;
        }
    } else {
        if (snprintf(file_name, sizeof(file_name), "%s.syspckg", selector_name) >= (int)sizeof(file_name)) {
            return -1;
        }
    }

    if (file_exists(file_name)) {
        if (snprintf(out_archive_path, out_archive_path_sz, "%s", file_name) >= (int)out_archive_path_sz) {
            return -1;
        }
        return 0;
    }

    char pkg_dir_path[PATH_MAX];
    if (snprintf(pkg_dir_path, sizeof(pkg_dir_path), "%s/usr/share/syspckg/packages/%s", root, file_name) < (int)sizeof(pkg_dir_path) &&
        file_exists(pkg_dir_path)) {
        if (snprintf(out_archive_path, out_archive_path_sz, "%s", pkg_dir_path) >= (int)out_archive_path_sz) {
            return -1;
        }
        return 0;
    }

    if (local_only) {
        return -1;
    }

    char version[64];
    char source_url[PATH_MAX];
    if (get_os_version(root, version, sizeof(version)) != 0 || !is_safe_version(version)) {
        return -1;
    }
    if (get_source_url(source_url, sizeof(source_url)) != 0) {
        return -1;
    }
    char url[PATH_MAX];
    if (snprintf(url, sizeof(url),
                 "%s/%s/packages/%s.syspckg",
                 source_url, version, pkg_name) >= (int)sizeof(url)) {
        return -1;
    }
    char out_path[PATH_MAX];
    if (snprintf(out_path, sizeof(out_path), "%s/%s.syspckg", tmpdir, pkg_name) >= (int)sizeof(out_path)) {
        return -1;
    }
    if (!file_exists(out_path)) {
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof(msg), "Downloading %s", url);
        log_info(msg);
        if (download_pkg_wget(url, out_path) != 0) {
            return -1;
        }
        log_ok("Download complete");
    }
    if (snprintf(out_archive_path, out_archive_path_sz, "%s", out_path) >= (int)out_archive_path_sz) {
        return -1;
    }
    return 0;
}

static int answer_is_yes_or_default(const char *line) {
    size_t len;

    if (line == NULL) {
        return 1;
    }
    len = strcspn(line, "\r\n");
    if (len == 0) {
        return 1;
    }
    return (len == 1 && (line[0] == 'y' || line[0] == 'Y')) ||
           (len == 3 &&
            (line[0] == 'y' || line[0] == 'Y') &&
            (line[1] == 'e' || line[1] == 'E') &&
            (line[2] == 's' || line[2] == 'S'));
}

static int ask_install_confirmation(char **names, size_t count) {
    if (count == 0) {
        return 1;
    }
    printf("Packages to be installed: ");
    for (size_t i = 0; i < count; i++) {
        printf("%s%s", names[i], (i + 1 < count) ? ", " : "");
    }
    printf("\nDo you want to install these packages? [Y/n] ");
    fflush(stdout);

    char line[32];
    if (!fgets(line, sizeof(line), stdin)) {
        return 1;
    }
    return answer_is_yes_or_default(line);
}

static int ask_remove_confirmation(const char *name) {
    printf("Package to be removed: %s\n", name);
    printf("Do you want to remove this package? [Y/n] ");
    fflush(stdout);

    char line[32];
    if (!fgets(line, sizeof(line), stdin)) {
        return 1;
    }
    return answer_is_yes_or_default(line);
}

static ssize_t find_plan_by_selector(pkg_plan_t *plans, size_t count, const char *selector) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(plans[i].selector, selector) == 0) {
            return (ssize_t)i;
        }
    }
    return -1;
}

/* A dependency can be written either as the package base name ("glib") or
 * as its exact install name ("glib-2.82.2"). */
static ssize_t find_plan_by_dependency(pkg_plan_t *plans, size_t count, const char *dependency) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(plans[i].install_name, dependency) == 0 ||
            strcmp(plans[i].pkg_name, dependency) == 0 ||
            strcmp(plans[i].selector, dependency) == 0) {
            return (ssize_t)i;
        }
    }
    return -1;
}

static int append_plan_index(size_t **order, size_t *count, size_t index) {
    size_t *next = realloc(*order, (*count + 1) * sizeof(**order));
    if (!next) {
        return -1;
    }
    *order = next;
    (*order)[(*count)++] = index;
    return 0;
}

static int visit_install_plan(pkg_plan_t *plans, size_t count, size_t index,
                              unsigned char *state, size_t **order, size_t *order_count) {
    if (state[index] == 2) {
        return 0;
    }
    if (state[index] == 1) {
        fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Circular package dependency involving: %s\n",
                plans[index].install_name);
        return -1;
    }
    state[index] = 1;
    for (size_t i = 0; i < plans[index].dep_count; i++) {
        ssize_t dep_index = find_plan_by_dependency(plans, count, plans[index].deps[i]);
        if (dep_index >= 0 && visit_install_plan(plans, count, (size_t)dep_index,
                                                 state, order, order_count) != 0) {
            return -1;
        }
    }
    state[index] = 2;
    return append_plan_index(order, order_count, index);
}

/* Return a dependency-first order.  Queue order is only a download detail;
 * it must never determine the order in which install hooks are run. */
static int build_install_order(pkg_plan_t *plans, size_t count,
                               size_t **out_order, size_t *out_count) {
    unsigned char *state = calloc(count ? count : 1, sizeof(*state));
    size_t *order = NULL;
    size_t order_count = 0;
    if (!state) {
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        if (visit_install_plan(plans, count, i, state, &order, &order_count) != 0) {
            free(state);
            free(order);
            return -1;
        }
    }
    free(state);
    *out_order = order;
    *out_count = order_count;
    return 0;
}

static void free_plans(pkg_plan_t *plans, size_t count) {
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < plans[i].dep_count; j++) {
            free(plans[i].deps[j]);
        }
        free(plans[i].deps);
    }
    free(plans);
}

static int get_pkg_base(const char *input, char *out, size_t out_sz) {
    const char *name = strrchr(input, '/');
    if (name) {
        name++;
    } else {
        name = input;
    }
    return strip_syspckg_suffix(name, out, out_sz);
}

static int has_digit(const char *s) {
    for (const char *p = s; *p; p++) {
        if (isdigit((unsigned char)*p)) {
            return 1;
        }
    }
    return 0;
}

static int selector_has_explicit_version(const char *selector) {
    char name[PATH_MAX];
    char *dash;

    if (get_pkg_base(selector, name, sizeof(name)) != 0) {
        return 0;
    }
    dash = strrchr(name, '-');
    return dash != NULL && has_digit(dash + 1);
}

static int compare_versions(const char *a, const char *b) {
    const char *pa = a;
    const char *pb = b;
    while (*pa || *pb) {
        while (*pa == '.' || *pa == '-' || *pa == '_') {
            pa++;
        }
        while (*pb == '.' || *pb == '-' || *pb == '_') {
            pb++;
        }
        if (!*pa || !*pb) {
            break;
        }
        const char *sa = pa;
        const char *sb = pb;
        int na = 1;
        int nb = 1;
        while (*pa && *pa != '.' && *pa != '-' && *pa != '_') {
            if (!isdigit((unsigned char)*pa)) {
                na = 0;
            }
            pa++;
        }
        while (*pb && *pb != '.' && *pb != '-' && *pb != '_') {
            if (!isdigit((unsigned char)*pb)) {
                nb = 0;
            }
            pb++;
        }
        size_t la = (size_t)(pa - sa);
        size_t lb = (size_t)(pb - sb);
        if (na && nb) {
            long va = strtol(sa, NULL, 10);
            long vb = strtol(sb, NULL, 10);
            if (va < vb) {
                return -1;
            }
            if (va > vb) {
                return 1;
            }
        } else if (na != nb) {
            return na ? 1 : -1;
        } else {
            size_t lm = la < lb ? la : lb;
            int cmp = strncmp(sa, sb, lm);
            if (cmp != 0) {
                return cmp;
            }
            if (la < lb) {
                return -1;
            }
            if (la > lb) {
                return 1;
            }
        }
    }
    if (*pa) {
        return 1;
    }
    if (*pb) {
        return -1;
    }
    return 0;
}

static int resolve_latest_local(const char *dir, const char *base, char *out_name, size_t out_sz) {
    DIR *d = opendir(dir);
    if (!d) {
        return -1;
    }
    struct dirent *de;
    char best_name[PATH_MAX] = {0};
    char best_ver[PATH_MAX] = {0};
    size_t base_len = strlen(base);
    const char *suffix = ".syspckg";
    size_t suffix_len = strlen(suffix);

    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        size_t nl = strlen(name);
        if (nl <= base_len + 1 + suffix_len) {
            continue;
        }
        if (strncmp(name, base, base_len) != 0 || name[base_len] != '-') {
            continue;
        }
        if (strcmp(name + nl - suffix_len, suffix) != 0) {
            continue;
        }
        size_t ver_len = nl - base_len - 1 - suffix_len;
        if (ver_len >= sizeof(best_ver)) {
            continue;
        }
        char ver[PATH_MAX];
        memcpy(ver, name + base_len + 1, ver_len);
        ver[ver_len] = '\0';
        if (best_name[0] == '\0' || compare_versions(ver, best_ver) > 0) {
            snprintf(best_name, sizeof(best_name), "%s", name);
            snprintf(best_ver, sizeof(best_ver), "%s", ver);
        }
    }
    closedir(d);
    if (best_name[0] == '\0') {
        return -1;
    }
    if (strlen(best_name) + 1 > out_sz) {
        return -1;
    }
    snprintf(out_name, out_sz, "%s", best_name);
    return 0;
}

static int resolve_latest_remote(const char *root, const char *base, char *out_name, size_t out_sz) {
    char version[64];
    char source_url[PATH_MAX];
    if (get_os_version(root, version, sizeof(version)) != 0) {
        return -1;
    }
    if (!is_safe_version(version)) {
        return -1;
    }
    if (get_source_url(source_url, sizeof(source_url)) != 0) {
        return -1;
    }

    char url[PATH_MAX];
    if (snprintf(url, sizeof(url),
                 "%s/%s/packages/",
                 source_url, version) >= (int)sizeof(url)) {
        return -1;
    }

    char cmd[PATH_MAX + 32];
    if (snprintf(cmd, sizeof(cmd), "wget -qO- %s", url) >= (int)sizeof(cmd)) {
        return -1;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return -1;
    }

    char best_name[PATH_MAX] = {0};
    char best_ver[PATH_MAX] = {0};
    char line[1024];
    const char *suffix = ".syspckg";
    size_t suffix_len = strlen(suffix);
    size_t base_len = strlen(base);

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while ((p = strstr(p, suffix)) != NULL) {
            char *end = p + suffix_len;
            char *start = p;
            while (start > line && is_pkg_token_char((unsigned char)start[-1])) {
                start--;
            }
            size_t len = (size_t)(end - start);
            if (len > 0 && len < PATH_MAX) {
                char name[PATH_MAX];
                memcpy(name, start, len);
                name[len] = '\0';
                if (strncmp(name, base, base_len) == 0 && name[base_len] == '-') {
                    size_t ver_len = len - base_len - 1 - suffix_len;
                    if (ver_len < sizeof(best_ver)) {
                        char ver[PATH_MAX];
                        memcpy(ver, name + base_len + 1, ver_len);
                        ver[ver_len] = '\0';
                        if (best_name[0] == '\0' || compare_versions(ver, best_ver) > 0) {
                            snprintf(best_name, sizeof(best_name), "%s", name);
                            snprintf(best_ver, sizeof(best_ver), "%s", ver);
                        }
                    }
                }
            }
            p = end;
        }
    }
    pclose(fp);

    if (best_name[0] == '\0') {
        return -1;
    }
    if (strlen(best_name) + 1 > out_sz) {
        return -1;
    }
    snprintf(out_name, out_sz, "%s", best_name);
    return 0;
}

static int resolve_latest_pkg_dir(const char *root, const char *base, char *out_name, size_t out_sz) {
    char dir[PATH_MAX];
    if (snprintf(dir, sizeof(dir), "%s/usr/share/syspckg/packages", root) >= (int)sizeof(dir)) {
        return -1;
    }
    return resolve_latest_local(dir, base, out_name, out_sz);
}

static int resolve_latest_installed(const char *root, const char *base, char *out_name, size_t out_sz) {
    char dir[PATH_MAX];
    if (snprintf(dir, sizeof(dir), "%s/var/lib/syspckg/installed", root) >= (int)sizeof(dir)) {
        return -1;
    }
    DIR *d = opendir(dir);
    if (!d) {
        return -1;
    }
    struct dirent *de;
    char best_name[PATH_MAX] = {0};
    char best_ver[PATH_MAX] = {0};
    size_t base_len = strlen(base);
    const char *suffix = ".list";
    size_t suffix_len = strlen(suffix);

    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        size_t nl = strlen(name);
        if (nl <= base_len + suffix_len) {
            continue;
        }
        if (strncmp(name, base, base_len) != 0) {
            continue;
        }
        if (strcmp(name + nl - suffix_len, suffix) != 0) {
            continue;
        }
        if (name[base_len] == '-') {
            size_t ver_len = nl - base_len - 1 - suffix_len;
            if (ver_len >= sizeof(best_ver)) {
                continue;
            }
            char ver[PATH_MAX];
            memcpy(ver, name + base_len + 1, ver_len);
            ver[ver_len] = '\0';
            if (best_name[0] == '\0' || compare_versions(ver, best_ver) > 0) {
                snprintf(best_name, sizeof(best_name), "%s", name);
                snprintf(best_ver, sizeof(best_ver), "%s", ver);
            }
        } else if (name[base_len] == '.' && best_name[0] == '\0') {
            snprintf(best_name, sizeof(best_name), "%s", name);
            best_ver[0] = '\0';
        }
    }
    closedir(d);
    if (best_name[0] == '\0') {
        return -1;
    }
    if (strlen(best_name) <= suffix_len) {
        return -1;
    }
    size_t pkg_len = strlen(best_name) - suffix_len;
    if (pkg_len + 1 > out_sz) {
        return -1;
    }
    memcpy(out_name, best_name, pkg_len);
    out_name[pkg_len] = '\0';
    return 0;
}

static int strip_trailing_slash(const char *in, char *out, size_t out_sz) {
    size_t len = strlen(in);
    if (len > 0 && in[len - 1] == '/') {
        len--;
    }
    if (len == 0 || len + 1 > out_sz) {
        return -1;
    }
    memcpy(out, in, len);
    out[len] = '\0';
    return 0;
}

static int version_from_prefixed_pkg(const char *name, const char *prefix, const char *suffix,
                                     char *out, size_t out_sz) {
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t name_len = strlen(name);
    size_t version_len;

    if (name_len <= prefix_len + suffix_len ||
        strncmp(name, prefix, prefix_len) != 0 ||
        strcmp(name + name_len - suffix_len, suffix) != 0) {
        return -1;
    }
    version_len = name_len - prefix_len - suffix_len;
    if (version_len == 0 || version_len + 1 > out_sz) {
        return -1;
    }
    memcpy(out, name + prefix_len, version_len);
    out[version_len] = '\0';
    return is_safe_version(out) ? 0 : -1;
}

static int select_latest_prefixed_pkg(const char *url, const char *prefix, char *out_name, size_t out_sz) {
    char **names = NULL;
    size_t count = 0;
    char best_name[PATH_MAX] = {0};
    char best_ver[PATH_MAX] = {0};

    if (collect_remote_tokens(url, ".syspckg", &names, &count) != 0) {
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        char ver[PATH_MAX];
        if (version_from_prefixed_pkg(names[i], prefix, ".syspckg", ver, sizeof(ver)) != 0) {
            continue;
        }
        if (best_name[0] == '\0' || compare_versions(ver, best_ver) > 0) {
            snprintf(best_name, sizeof(best_name), "%s", names[i]);
            snprintf(best_ver, sizeof(best_ver), "%s", ver);
        }
    }
    free_strings(names, count);
    if (best_name[0] == '\0' || strlen(best_name) + 1 > out_sz) {
        return -1;
    }
    snprintf(out_name, out_sz, "%s", best_name);
    return 0;
}

static int resolve_latest_system_update(const char *root, char *out_url, size_t out_url_sz,
                                        char *out_version, size_t out_version_sz) {
    char current_version[64];
    char source_url[PATH_MAX];
    char versions_url[PATH_MAX];
    char **names = NULL;
    size_t count = 0;
    char best[64] = {0};

    if (get_os_version(root, current_version, sizeof(current_version)) != 0 ||
        !is_safe_version(current_version) ||
        get_source_url(source_url, sizeof(source_url)) != 0 ||
        join_url2(source_url, "", versions_url, sizeof(versions_url)) != 0 ||
        collect_remote_tokens(versions_url, "/", &names, &count) != 0) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        char ver[64];
        if (strip_trailing_slash(names[i], ver, sizeof(ver)) != 0 || !is_safe_version(ver)) {
            continue;
        }
        if (strcmp(ver, "kernel") == 0 || strcmp(ver, "packages") == 0 || strcmp(ver, "packager") == 0) {
            continue;
        }
        if (compare_versions(ver, current_version) <= 0) {
            continue;
        }
        if (best[0] == '\0' || compare_versions(ver, best) > 0) {
            snprintf(best, sizeof(best), "%s", ver);
        }
    }
    free_strings(names, count);
    if (best[0] == '\0') {
        return 1;
    }
    if (snprintf(out_version, out_version_sz, "%s", best) >= (int)out_version_sz) {
        return -1;
    }
    return snprintf(out_url, out_url_sz, "%s/%s/adavalinux-%s.syspckg", source_url, best, best) < (int)out_url_sz ? 0 : -1;
}

static int resolve_latest_kernel_update(const char *root, char *out_url, size_t out_url_sz,
                                        char *out_remote_version, size_t out_remote_version_sz,
                                        char *out_local_version, size_t out_local_version_sz) {
    char source_url[PATH_MAX];
    char kernel_url[PATH_MAX];
    char best_name[PATH_MAX];
    char remote_version[128];
    char local_version[128] = {0};
    char previous_version[128];

    out_url[0] = '\0';
    out_remote_version[0] = '\0';
    out_local_version[0] = '\0';

    if (get_source_url(source_url, sizeof(source_url)) != 0 ||
        join_url2(source_url, "kernel/", kernel_url, sizeof(kernel_url)) != 0 ||
        select_latest_prefixed_pkg(kernel_url, "kernel-", best_name, sizeof(best_name)) != 0 ||
        version_from_prefixed_pkg(best_name, "kernel-", ".syspckg", remote_version, sizeof(remote_version)) != 0 ||
        snprintf(out_remote_version, out_remote_version_sz, "%s", remote_version) >= (int)out_remote_version_sz) {
        return -1;
    }
    if (latest_boot_version(root, "vmlinuz-", "", local_version, sizeof(local_version),
                            previous_version, sizeof(previous_version)) == 0 &&
        local_version[0] != '\0') {
        if (snprintf(out_local_version, out_local_version_sz, "%s", local_version) >= (int)out_local_version_sz) {
            return -1;
        }
        if (compare_versions(remote_version, local_version) <= 0) {
            return 1;
        }
    }
    return snprintf(out_url, out_url_sz, "%s%s", kernel_url, best_name) < (int)out_url_sz ? 0 : -1;
}

static int resolve_latest_packager_update(const char *root, char *out_url, size_t out_url_sz,
                                          char *out_remote_version, size_t out_remote_version_sz,
                                          char *out_local_version, size_t out_local_version_sz) {
    char version[64];
    char local_version[64];
    char remote_version[64];
    char source_url[PATH_MAX];
    char packager_part[PATH_MAX];
    char packager_url[PATH_MAX];
    char best_name[PATH_MAX];

    out_url[0] = '\0';
    out_remote_version[0] = '\0';
    out_local_version[0] = '\0';
    if (get_os_version(root, version, sizeof(version)) != 0 ||
        !is_safe_version(version) ||
        get_packager_version(root, local_version, sizeof(local_version)) != 0 ||
        get_source_url(source_url, sizeof(source_url)) != 0 ||
        snprintf(packager_part, sizeof(packager_part), "%s/packager/", version) >= (int)sizeof(packager_part) ||
        join_url2(source_url, packager_part, packager_url, sizeof(packager_url)) != 0 ||
        select_latest_prefixed_pkg(packager_url, "syspckg-", best_name, sizeof(best_name)) != 0) {
        return -1;
    }
    if (version_from_prefixed_pkg(best_name, "syspckg-", ".syspckg", remote_version, sizeof(remote_version)) != 0 ||
        snprintf(out_remote_version, out_remote_version_sz, "%s", remote_version) >= (int)out_remote_version_sz ||
        snprintf(out_local_version, out_local_version_sz, "%s", local_version) >= (int)out_local_version_sz) {
        return -1;
    }
    if (compare_versions(remote_version, local_version) <= 0) {
        return 1;
    }
    return snprintf(out_url, out_url_sz, "%s%s", packager_url, best_name) < (int)out_url_sz ? 0 : -1;
}

static int selector_base_without_version(const char *manifest_name, char *out, size_t out_sz) {
    char name[PATH_MAX];
    char *dash;

    if (strip_syspckg_suffix(manifest_name, name, sizeof(name)) != 0) {
        return -1;
    }
    dash = strrchr(name, '-');
    if (dash != NULL && has_digit(dash + 1)) {
        *dash = '\0';
    }
    if (!is_safe_pkgname(name) || strcmp(name, "adavalinux") == 0 ||
        strcmp(name, "kernel") == 0 || strcmp(name, "syspckg") == 0) {
        return -1;
    }
    return snprintf(out, out_sz, "%s", name) < (int)out_sz ? 0 : -1;
}

static int strip_list_suffix(const char *name, char *out, size_t out_sz) {
    size_t name_len = strlen(name);
    const char *suffix = ".list";
    size_t suffix_len = strlen(suffix);

    if (name_len <= suffix_len || strcmp(name + name_len - suffix_len, suffix) != 0 ||
        name_len - suffix_len + 1 > out_sz) {
        return -1;
    }
    memcpy(out, name, name_len - suffix_len);
    out[name_len - suffix_len] = '\0';
    return 0;
}

static int version_from_install_name(const char *install_name, const char *base, char *out, size_t out_sz) {
    size_t base_len = strlen(base);
    size_t name_len = strlen(install_name);
    size_t ver_len;

    if (name_len <= base_len + 1 || strncmp(install_name, base, base_len) != 0 ||
        install_name[base_len] != '-') {
        return -1;
    }
    ver_len = name_len - base_len - 1;
    if (ver_len == 0 || ver_len + 1 > out_sz) {
        return -1;
    }
    memcpy(out, install_name + base_len + 1, ver_len);
    out[ver_len] = '\0';
    return 0;
}

static int collect_installed_update_packages(const char *root, installed_pkg_t **out, size_t *out_count) {
    char dir[PATH_MAX];
    DIR *d;
    struct dirent *de;
    installed_pkg_t *items = NULL;
    size_t count = 0;

    *out = NULL;
    *out_count = 0;
    if (snprintf(dir, sizeof(dir), "%s/var/lib/syspckg/installed", root) >= (int)sizeof(dir)) {
        return -1;
    }
    d = opendir(dir);
    if (!d) {
        return -1;
    }
    while ((de = readdir(d)) != NULL) {
        char installed_name[PATH_MAX];
        char selector[PATH_MAX];
        if (!has_suffix(de->d_name, ".list")) {
            continue;
        }
        if (strip_list_suffix(de->d_name, installed_name, sizeof(installed_name)) != 0 ||
            selector_base_without_version(installed_name, selector, sizeof(selector)) != 0) {
            continue;
        }
        installed_pkg_t item;
        memset(&item, 0, sizeof(item));
        if (snprintf(item.base, sizeof(item.base), "%s", selector) >= (int)sizeof(item.base) ||
            snprintf(item.installed_name, sizeof(item.installed_name), "%s", installed_name) >= (int)sizeof(item.installed_name) ||
            version_from_install_name(installed_name, selector, item.installed_version, sizeof(item.installed_version)) != 0) {
            continue;
        }
        installed_pkg_t *next = realloc(items, (count + 1) * sizeof(*items));
        if (!next) {
            free(items);
            closedir(d);
            return -1;
        }
        items = next;
        items[count++] = item;
    }
    closedir(d);
    *out = items;
    *out_count = count;
    return 0;
}

static int find_installed_package_for_update(const char *root, const char *install_name,
                                             char *out, size_t out_sz) {
    char wanted_base[PATH_MAX];
    char dir[PATH_MAX];
    DIR *d;
    struct dirent *de;
    char best[PATH_MAX] = {0};

    out[0] = '\0';
    if (selector_base_without_version(install_name, wanted_base, sizeof(wanted_base)) != 0 ||
        snprintf(dir, sizeof(dir), "%s/var/lib/syspckg/installed", root) >= (int)sizeof(dir)) {
        return 0;
    }
    d = opendir(dir);
    if (!d) {
        return 0;
    }
    while ((de = readdir(d)) != NULL) {
        char installed_name[PATH_MAX];
        char installed_base[PATH_MAX];

        if (!has_suffix(de->d_name, ".list") ||
            strip_list_suffix(de->d_name, installed_name, sizeof(installed_name)) != 0 ||
            selector_base_without_version(installed_name, installed_base, sizeof(installed_base)) != 0 ||
            strcmp(installed_base, wanted_base) != 0) {
            continue;
        }
        if (best[0] == '\0' || compare_versions(installed_name, best) > 0) {
            snprintf(best, sizeof(best), "%s", installed_name);
        }
    }
    closedir(d);
    if (best[0] == '\0') {
        return 0;
    }
    return snprintf(out, out_sz, "%s", best) < (int)out_sz ? 1 : -1;
}

static int find_remote_update_for_installed(char **remote_names,
                                            size_t remote_count,
                                            const installed_pkg_t *installed,
                                            char *out_name,
                                            size_t out_name_sz,
                                            char *out_version,
                                            size_t out_version_sz) {
    char best_name[PATH_MAX] = {0};
    char best_ver[PATH_MAX] = {0};
    size_t base_len = strlen(installed->base);

    out_name[0] = '\0';
    out_version[0] = '\0';
    for (size_t i = 0; i < remote_count; i++) {
        const char *name = remote_names[i];
        size_t name_len = strlen(name);
        const char *suffix = ".syspckg";
        size_t suffix_len = strlen(suffix);
        char ver[PATH_MAX];
        size_t ver_len;

        if (name_len <= base_len + 1 + suffix_len ||
            strncmp(name, installed->base, base_len) != 0 ||
            name[base_len] != '-' ||
            strcmp(name + name_len - suffix_len, suffix) != 0) {
            continue;
        }
        ver_len = name_len - base_len - 1 - suffix_len;
        if (ver_len == 0 || ver_len >= sizeof(ver)) {
            continue;
        }
        memcpy(ver, name + base_len + 1, ver_len);
        ver[ver_len] = '\0';
        if (!is_safe_version(ver)) {
            continue;
        }
        if (best_name[0] == '\0' || compare_versions(ver, best_ver) > 0) {
            snprintf(best_name, sizeof(best_name), "%s", name);
            snprintf(best_ver, sizeof(best_ver), "%s", ver);
        }
    }
    if (best_name[0] == '\0') {
        return 0;
    }
    if (snprintf(out_name, out_name_sz, "%s", best_name) >= (int)out_name_sz ||
        snprintf(out_version, out_version_sz, "%s", best_ver) >= (int)out_version_sz) {
        return -1;
    }
    return compare_versions(best_ver, installed->installed_version) > 0 ? 1 : 0;
}

static int check_all_updates_from_manifest(const char *root, char ***out_urls, size_t *out_url_count) {
    char version[64];
    char source_url[PATH_MAX];
    char packages_part[PATH_MAX];
    char packages_url[PATH_MAX];
    installed_pkg_t *installed = NULL;
    size_t installed_count = 0;
    char **remote_names = NULL;
    size_t remote_count = 0;
    char **urls = NULL;
    size_t url_count = 0;
    int rc = 0;

    if (out_urls != NULL) {
        *out_urls = NULL;
    }
    if (out_url_count != NULL) {
        *out_url_count = 0;
    }

    if (get_os_version(root, version, sizeof(version)) != 0 ||
        !is_safe_version(version) ||
        get_source_url(source_url, sizeof(source_url)) != 0 ||
        snprintf(packages_part, sizeof(packages_part), "%s/packages/", version) >= (int)sizeof(packages_part) ||
        join_url2(source_url, packages_part, packages_url, sizeof(packages_url)) != 0 ||
        collect_installed_update_packages(root, &installed, &installed_count) != 0 ||
        collect_remote_tokens(packages_url, ".syspckg", &remote_names, &remote_count) != 0) {
        free(installed);
        free_strings(remote_names, remote_count);
        return -1;
    }

    for (size_t i = 0; i < installed_count; i++) {
        char remote_name[PATH_MAX];
        char remote_version[PATH_MAX];
        int update = find_remote_update_for_installed(remote_names,
                                                      remote_count,
                                                      &installed[i],
                                                      remote_name,
                                                      sizeof(remote_name),
                                                      remote_version,
                                                      sizeof(remote_version));
        if (update < 0) {
            rc = -1;
            break;
        }
        if (update > 0) {
            char remote_url[PATH_MAX];
            printf("Update available: %s %s -> %s\n",
                   installed[i].base,
                   installed[i].installed_version,
                   remote_version);
            if (snprintf(remote_url, sizeof(remote_url), "%s%s", packages_url, remote_name) >= (int)sizeof(remote_url) ||
                push_string(&urls, &url_count, remote_url) != 0) {
                rc = -1;
                break;
            }
        }
    }

    free(installed);
    free_strings(remote_names, remote_count);
    if (rc == 0 && out_urls != NULL && out_url_count != NULL) {
        *out_urls = urls;
        *out_url_count = url_count;
    } else {
        free_strings(urls, url_count);
    }
    return rc;
}

static int ensure_dir_recursive(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        return -1;
    }
    snprintf(tmp, sizeof(tmp), "%s", path);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
            return -1;
        }
        *p = '/';
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int ensure_parent_dir(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) {
        return -1;
    }
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) {
        return 0;
    }
    *slash = '\0';
    return ensure_dir_recursive(tmp, mode);
}

static int copy_file_contents(const char *src, const char *dst, mode_t mode) {
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) {
        return -1;
    }
    if (ensure_parent_dir(dst, 0755) != 0) {
        close(in_fd);
        return -1;
    }
    char tmp_path[PATH_MAX];
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.syspckg-tmp.XXXXXX", dst) >= (int)sizeof(tmp_path)) {
        close(in_fd);
        return -1;
    }
    int out_fd = mkstemp(tmp_path);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }

    char buf[16384];
    ssize_t r;
    while ((r = read(in_fd, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out_fd, buf + off, (size_t)(r - off));
            if (w < 0) {
                close(in_fd);
                close(out_fd);
                unlink(tmp_path);
                return -1;
            }
            off += w;
        }
    }
    if (r < 0) {
        close(in_fd);
        close(out_fd);
        unlink(tmp_path);
        return -1;
    }
    if (fchmod(out_fd, mode) != 0) {
        close(in_fd);
        close(out_fd);
        unlink(tmp_path);
        return -1;
    }
    if (close(out_fd) != 0) {
        close(in_fd);
        unlink(tmp_path);
        return -1;
    }
    if (rename(tmp_path, dst) != 0) {
        close(in_fd);
        unlink(tmp_path);
        return -1;
    }
    close(in_fd);
    return 0;
}

static int is_exec_path(const char *path) {
    return (strncmp(path, "/bin/", 5) == 0 ||
            strncmp(path, "/sbin/", 6) == 0 ||
            strncmp(path, "/usr/bin/", 9) == 0 ||
            strncmp(path, "/usr/sbin/", 10) == 0 ||
            strncmp(path, "/usr/libexec/", 13) == 0);
}

static int is_package_control_file(const char *name) {
    return (strcmp(name, "install.sh") == 0 ||
            strcmp(name, "remove.sh") == 0 ||
            strcmp(name, "syspckg-info") == 0 ||
            strcmp(name, "syspckg-deps") == 0);
}

static int run_package_hook(const char *install_dir, const char *root, const char *action) {
    char hook_path[PATH_MAX];
    struct stat st;
    if (snprintf(hook_path, sizeof(hook_path), "%s/%s.sh", install_dir, action) >= (int)sizeof(hook_path)) {
        return -1;
    }
    if (lstat(hook_path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (!S_ISREG(st.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    log_info("Running package hook");
    char *argv[] = { "/bin/sh", hook_path, (char *)root, (char *)action, NULL };
    return run_cmd(argv);
}

static int save_remove_hook(const char *install_dir, const char *root, const char *pkg_name) {
    char source[PATH_MAX];
    char hooks_dir[PATH_MAX];
    char destination[PATH_MAX];
    struct stat st;

    if (snprintf(source, sizeof(source), "%s/remove.sh", install_dir) >= (int)sizeof(source) ||
        snprintf(hooks_dir, sizeof(hooks_dir), "%s/var/lib/syspckg/hooks", root) >= (int)sizeof(hooks_dir) ||
        snprintf(destination, sizeof(destination), "%s/%s.remove.sh", hooks_dir, pkg_name) >= (int)sizeof(destination)) {
        return -1;
    }
    if (lstat(source, &st) != 0) {
        if (errno == ENOENT) {
            unlink(destination);
            return 0;
        }
        return -1;
    }
    if (!S_ISREG(st.st_mode) || ensure_dir_recursive(hooks_dir, 0755) != 0) {
        return -1;
    }
    return copy_file_contents(source, destination, 0700);
}

static int run_remove_hook(const char *root, const char *pkg_name) {
    char hook_path[PATH_MAX];
    struct stat st;

    if (snprintf(hook_path, sizeof(hook_path), "%s/var/lib/syspckg/hooks/%s.remove.sh", root, pkg_name) >=
        (int)sizeof(hook_path)) {
        return -1;
    }
    if (lstat(hook_path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }
    if (!S_ISREG(st.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    log_info("Running remove.sh");
    char *argv[] = { "/bin/sh", hook_path, (char *)root, "remove", NULL };
    return run_cmd(argv);
}

static int file_has_exec_content(const char *path) {
    unsigned char buf[4] = {0};
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    ssize_t r = read(fd, buf, sizeof(buf));
    if (r < 2) {
        close(fd);
        return 0;
    }
    if (buf[0] == 0x7f && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
        close(fd);
        return 1;
    }
    if (buf[0] == '#' && buf[1] == '!') {
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}

static mode_t add_exec_from_read(mode_t mode) {
    mode_t exec = (mode & 0444) >> 2;
    return mode | exec;
}

static int copy_symlink_entry(const char *src, const char *dst) {
    char target[PATH_MAX];
    ssize_t len = readlink(src, target, sizeof(target) - 1);
    if (len < 0) {
        return -1;
    }
    target[len] = '\0';
    if (ensure_parent_dir(dst, 0755) != 0) {
        return -1;
    }
    if (unlink(dst) != 0 && errno != ENOENT) {
        return -1;
    }
    if (symlink(target, dst) != 0) {
        return -1;
    }
    return 0;
}

static int copy_tree(const char *src_base, const char *rel, const char *root) {
    char path[PATH_MAX];
    if (rel && *rel) {
        if (snprintf(path, sizeof(path), "%s/%s", src_base, rel) >= (int)sizeof(path)) {
            return -1;
        }
    } else {
        if (snprintf(path, sizeof(path), "%s", src_base) >= (int)sizeof(path)) {
            return -1;
        }
    }

    DIR *d = opendir(path);
    if (!d) {
        return -1;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        if ((!rel || *rel == '\0') && is_package_control_file(de->d_name)) {
            continue;
        }

        char child_rel[PATH_MAX];
        if (rel && *rel) {
            if (snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, de->d_name) >= (int)sizeof(child_rel)) {
                closedir(d);
                return -1;
            }
        } else {
            if (snprintf(child_rel, sizeof(child_rel), "%s", de->d_name) >= (int)sizeof(child_rel)) {
                closedir(d);
                return -1;
            }
        }

        char src_path[PATH_MAX];
        if (snprintf(src_path, sizeof(src_path), "%s/%s", src_base, child_rel) >= (int)sizeof(src_path)) {
            closedir(d);
            return -1;
        }
        char dst_path[PATH_MAX];
        if (snprintf(dst_path, sizeof(dst_path), "%s%s%s",
                     root, (strcmp(root, "/") == 0) ? "" : "/", child_rel) >= (int)sizeof(dst_path)) {
            closedir(d);
            return -1;
        }

        struct stat st;
        if (lstat(src_path, &st) != 0) {
            closedir(d);
            return -1;
        }

        if (S_ISDIR(st.st_mode)) {
            if (ensure_dir_recursive(dst_path, st.st_mode & 07777) != 0) {
                closedir(d);
                return -1;
            }
            if (copy_tree(src_base, child_rel, root) != 0) {
                closedir(d);
                return -1;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (copy_file_contents(src_path, dst_path, st.st_mode & 07777) != 0) {
                closedir(d);
                return -1;
            }
            if (is_exec_path(dst_path) && (st.st_mode & 0111) == 0) {
                if (file_has_exec_content(src_path)) {
                    mode_t new_mode = add_exec_from_read(st.st_mode & 07777);
                    if (chmod(dst_path, new_mode) != 0) {
                        closedir(d);
                        return -1;
                    }
                }
            }
        } else if (S_ISLNK(st.st_mode)) {
            if (copy_symlink_entry(src_path, dst_path) != 0) {
                closedir(d);
                return -1;
            }
        }
    }
    closedir(d);
    return 0;
}

static int write_manifest_entry(FILE *out, const char type, const char *path) {
    if (fprintf(out, "%c %s\n", type, path) < 0) {
        return -1;
    }
    return 0;
}

static int cmp_dir_len_desc(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    size_t la = strlen(sa);
    size_t lb = strlen(sb);
    if (la > lb) {
        return -1;
    }
    if (la < lb) {
        return 1;
    }
    return strcmp(sa, sb);
}

static int add_manifest_entries(const char *base, const char *rel, const char *root, FILE *out) {
    char path[PATH_MAX];
    if (rel && *rel) {
        if (snprintf(path, sizeof(path), "%s/%s", base, rel) >= (int)sizeof(path)) {
            return -1;
        }
    } else {
        if (snprintf(path, sizeof(path), "%s", base) >= (int)sizeof(path)) {
            return -1;
        }
    }

    DIR *d = opendir(path);
    if (!d) {
        return -1;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        if ((!rel || *rel == '\0') && is_package_control_file(de->d_name)) {
            continue;
        }

        char child_rel[PATH_MAX];
        if (rel && *rel) {
            if (snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, de->d_name) >= (int)sizeof(child_rel)) {
                closedir(d);
                return -1;
            }
        } else {
            if (snprintf(child_rel, sizeof(child_rel), "%s", de->d_name) >= (int)sizeof(child_rel)) {
                closedir(d);
                return -1;
            }
        }

        char child_path[PATH_MAX];
        if (snprintf(child_path, sizeof(child_path), "%s/%s", base, child_rel) >= (int)sizeof(child_path)) {
            closedir(d);
            return -1;
        }
        struct stat st;
        if (lstat(child_path, &st) != 0) {
            closedir(d);
            return -1;
        }

        if (S_ISDIR(st.st_mode)) {
            if (add_manifest_entries(base, child_rel, root, out) != 0) {
                closedir(d);
                return -1;
            }
            char target[PATH_MAX];
            if (snprintf(target, sizeof(target), "%s%s%s",
                         root, (strcmp(root, "/") == 0) ? "" : "/", child_rel) >= (int)sizeof(target)) {
                closedir(d);
                return -1;
            }
            if (write_manifest_entry(out, 'D', target) != 0) {
                closedir(d);
                return -1;
            }
        } else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
            char target[PATH_MAX];
            if (snprintf(target, sizeof(target), "%s%s%s",
                         root, (strcmp(root, "/") == 0) ? "" : "/", child_rel) >= (int)sizeof(target)) {
                closedir(d);
                return -1;
            }
            if (is_core_lib_path(target)) {
                continue;
            }
            if (write_manifest_entry(out, 'F', target) != 0) {
                closedir(d);
                return -1;
            }
        }
    }
    closedir(d);
    return 0;
}

static int write_manifest(const char *install_dir, const char *root, const char *pkg_name) {
    char db_dir[PATH_MAX];
    char manifest_path[PATH_MAX];
    if (snprintf(db_dir, sizeof(db_dir), "%s/var/lib/syspckg", root) >= (int)sizeof(db_dir)) {
        return -1;
    }
    if (ensure_dir_recursive(db_dir, 0755) != 0) {
        perror("mkdir");
        return -1;
    }
    if (snprintf(db_dir, sizeof(db_dir), "%s/var/lib/syspckg/installed", root) >= (int)sizeof(db_dir)) {
        return -1;
    }
    if (ensure_dir_recursive(db_dir, 0755) != 0) {
        perror("mkdir");
        return -1;
    }
    if (snprintf(manifest_path, sizeof(manifest_path), "%s/%s.list", db_dir, pkg_name) >= (int)sizeof(manifest_path)) {
        return -1;
    }

    FILE *f = fopen(manifest_path, "w");
    if (!f) {
        perror("fopen");
        return -1;
    }
    if (add_manifest_entries(install_dir, "", root, f) != 0) {
        perror("manifest");
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int remove_pkg_impl(const char *root, const char *pkg_name, int protect_packager) {
    char manifest_path[PATH_MAX];

    if (protect_packager && is_packager_pkg_name(pkg_name)) {
        fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Cannot remove SystemPackager.\n");
        return -1;
    }
    if (snprintf(manifest_path, sizeof(manifest_path),
                 "%s/var/lib/syspckg/installed/%s.list", root, pkg_name) >= (int)sizeof(manifest_path)) {
        return -1;
    }
    FILE *f = fopen(manifest_path, "r");
    if (!f) {
        fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Package not installed: %s\n", pkg_name);
        return -1;
    }

    char line[PATH_MAX + 8];
    char **dirs = NULL;
    size_t dir_count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'F' && line[1] == ' ') {
            char *path = line + 2;
            path[strcspn(path, "\r\n")] = '\0';
            if (path[0] != '/' || strstr(path, "/../")) {
                continue;
            }
            unlink(path);
        } else if (line[0] == 'D' && line[1] == ' ') {
            char *path = line + 2;
            path[strcspn(path, "\r\n")] = '\0';
            if (path[0] != '/' || strstr(path, "/../")) {
                continue;
            }
            char *copy = strdup(path);
            if (!copy) {
                fclose(f);
                return -1;
            }
            char **next = realloc(dirs, (dir_count + 1) * sizeof(*dirs));
            if (!next) {
                free(copy);
                fclose(f);
                return -1;
            }
            dirs = next;
            dirs[dir_count++] = copy;
        }
    }
    fclose(f);

    if (run_remove_hook(root, pkg_name) != 0) {
        fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Remove hook failed for: %s\n", pkg_name);
        for (size_t i = 0; i < dir_count; i++) {
            free(dirs[i]);
        }
        free(dirs);
        return -1;
    }

    if (dir_count > 1) {
        qsort(dirs, dir_count, sizeof(*dirs), cmp_dir_len_desc);
    }
    for (size_t i = dir_count; i > 0; i--) {
        rmdir(dirs[i - 1]);
        free(dirs[i - 1]);
    }
    free(dirs);
    unlink(manifest_path);
    {
        char hook_path[PATH_MAX];
        if (snprintf(hook_path, sizeof(hook_path), "%s/var/lib/syspckg/hooks/%s.remove.sh", root, pkg_name) <
            (int)sizeof(hook_path)) {
            unlink(hook_path);
        }
    }
    return 0;
}

static int remove_pkg(const char *root, const char *pkg_name) {
    return remove_pkg_impl(root, pkg_name, 1);
}

static int remove_pkg_with_deps(const char *root, const char *pkg_name,
                                const char *tmpdir, int local_only, int depth) {
    char resolved_name[PATH_MAX];
    char archive_path[PATH_MAX];
    char pkg_ver[64], pkg_id[64], pkg_version[64];
    char **deps = NULL;
    size_t dep_count = 0;

    if (depth > 64 || resolve_pkg_archive_path(root, tmpdir, pkg_name, local_only,
                                                resolved_name, sizeof(resolved_name),
                                                archive_path, sizeof(archive_path)) != 0 ||
        read_pkg_metadata_from_archive(archive_path, pkg_ver, sizeof(pkg_ver),
                                       pkg_id, sizeof(pkg_id), pkg_version, sizeof(pkg_version),
                                       &deps, &dep_count) != 0) {
        free_strings(deps, dep_count);
        return -1;
    }
    if (remove_pkg(root, pkg_name) != 0) {
        free_strings(deps, dep_count);
        return -1;
    }
    for (size_t i = 0; i < dep_count; i++) {
        if (is_safe_pkgname(deps[i]) && is_pkg_installed(root, deps[i]) &&
            remove_pkg_with_deps(root, deps[i], tmpdir, local_only, depth + 1) != 0) {
            free_strings(deps, dep_count);
            return -1;
        }
    }
    free_strings(deps, dep_count);
    return 0;
}

static int latest_boot_version(const char *root, const char *prefix, const char *suffix,
                               char *latest, size_t latest_sz,
                               char *previous, size_t previous_sz) {
    char dir_path[PATH_MAX];
    DIR *d;
    struct dirent *de;
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);

    latest[0] = '\0';
    previous[0] = '\0';
    if (snprintf(dir_path, sizeof(dir_path), "%s/boot", root) >= (int)sizeof(dir_path)) {
        return -1;
    }
    d = opendir(dir_path);
    if (!d) {
        return -1;
    }
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        size_t name_len = strlen(name);
        char ver[128];
        size_t ver_len;

        if (name_len <= prefix_len + suffix_len ||
            strncmp(name, prefix, prefix_len) != 0 ||
            strcmp(name + name_len - suffix_len, suffix) != 0) {
            continue;
        }
        ver_len = name_len - prefix_len - suffix_len;
        if (ver_len == 0 || ver_len >= sizeof(ver)) {
            continue;
        }
        memcpy(ver, name + prefix_len, ver_len);
        ver[ver_len] = '\0';
        if (!is_safe_version(ver)) {
            continue;
        }
        if (latest[0] == '\0' || compare_versions(ver, latest) > 0) {
            if (latest[0] != '\0') {
                snprintf(previous, previous_sz, "%s", latest);
            }
            snprintf(latest, latest_sz, "%s", ver);
        } else if (strcmp(ver, latest) != 0 &&
                   (previous[0] == '\0' || compare_versions(ver, previous) > 0)) {
            snprintf(previous, previous_sz, "%s", ver);
        }
    }
    closedir(d);
    return latest[0] != '\0' ? 0 : -1;
}

static int relink_boot_file(const char *root, const char *target, const char *link_name) {
    char link_path[PATH_MAX];
    if (snprintf(link_path, sizeof(link_path), "%s/boot/%s", root, link_name) >= (int)sizeof(link_path)) {
        return -1;
    }
    if (unlink(link_path) != 0 && errno != ENOENT) {
        return -1;
    }
    return symlink(target, link_path);
}

static int target_uses_uefi(const char *root) {
    char path[PATH_MAX];

    if (snprintf(path, sizeof(path), "%s/sys/firmware/efi", root) < (int)sizeof(path) &&
        file_exists(path)) {
        return 1;
    }
    if (snprintf(path, sizeof(path), "%s/boot/efi", root) < (int)sizeof(path) &&
        file_exists(path)) {
        return 1;
    }
    return 0;
}

static const char *target_grub_package(const char *root) {
    return target_uses_uefi(root) ? "grub-efi" : "grub-bios";
}

static int run_grub_mkconfig(const char *root) {
    char *argv[] = { "chroot", (char *)root, "/usr/sbin/grub-mkconfig", "-o", "/boot/grub/grub.cfg", NULL };
    return run_cmd(argv);
}

static int root_arg_from_file(const char *path, char *out, size_t out_sz) {
    FILE *fp;
    char line[2048];

    if (path == NULL || out == NULL || out_sz == 0) {
        return -1;
    }
    fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *arg = strstr(line, "root=");
        char *end;
        if (arg == NULL) {
            continue;
        }
        end = arg;
        while (*end != '\0' && !isspace((unsigned char)*end) && *end != '\"' && *end != '\'') {
            end++;
        }
        if (end == arg || (size_t)(end - arg) >= out_sz) {
            fclose(fp);
            return -1;
        }
        memcpy(out, arg, (size_t)(end - arg));
        out[end - arg] = '\0';
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return -1;
}

static int write_default_grub(const char *root) {
    char default_path[PATH_MAX];
    char grub_cfg_path[PATH_MAX];
    char temp_path[PATH_MAX];
    char root_arg[256];
    char contents[1024];
    int fd;
    int written;
    size_t length;
    size_t offset = 0;

    if (snprintf(default_path, sizeof(default_path), "%s/etc/default/grub", root) >= (int)sizeof(default_path) ||
        snprintf(grub_cfg_path, sizeof(grub_cfg_path), "%s/boot/grub/grub.cfg", root) >= (int)sizeof(grub_cfg_path)) {
        return -1;
    }
    if (root_arg_from_file(default_path, root_arg, sizeof(root_arg)) != 0 &&
        root_arg_from_file(grub_cfg_path, root_arg, sizeof(root_arg)) != 0) {
        return -1;
    }
    written = snprintf(contents, sizeof(contents),
                       "GRUB_TIMEOUT=10\n"
                       "GRUB_DEFAULT=0\n"
                       "GRUB_CMDLINE_LINUX=\"%s rootfstype=ext4 rootwait rootdelay=5 rw console=ttyS0 console=tty1 libata.force=noncq\"\n"
                       "GRUB_CMDLINE_LINUX_DEFAULT=\"quiet\"\n",
                       root_arg);
    if (written < 0 || (size_t)written >= sizeof(contents) || ensure_parent_dir(default_path, 0755) != 0 ||
        snprintf(temp_path, sizeof(temp_path), "%s.syspckg-tmp.XXXXXX", default_path) >= (int)sizeof(temp_path)) {
        return -1;
    }
    fd = mkstemp(temp_path);
    if (fd < 0) {
        return -1;
    }
    length = (size_t)written;
    while (offset < length) {
        ssize_t result = write(fd, contents + offset, length - offset);
        if (result < 0) {
            close(fd);
            unlink(temp_path);
            return -1;
        }
        offset += (size_t)result;
    }
    if (fchmod(fd, 0644) != 0 || close(fd) != 0 || rename(temp_path, default_path) != 0) {
        unlink(temp_path);
        return -1;
    }
    return 0;
}

static int chmod_grub_generator(const char *root, const char *dir, const char *name, mode_t mode) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s/%s", root, dir, name) >= (int)sizeof(path)) {
        return -1;
    }
    if (chmod(path, mode) != 0 && errno != ENOENT) {
        return -1;
    }
    return 0;
}

static int disable_standard_grub_generators(const char *root) {
    const char *dirs[] = { "usr/etc/grub.d", "etc/grub.d" };
    const char *disable[] = {
        "10_linux", "20_linux_xen", "25_bli", "30_os-prober",
        "30_uefi-firmware", "40_custom", "41_custom"
    };

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        for (size_t j = 0; j < sizeof(disable) / sizeof(disable[0]); j++) {
            if (chmod_grub_generator(root, dirs[i], disable[j], 0644) != 0) {
                return -1;
            }
        }
        if (chmod_grub_generator(root, dirs[i], "00_header", 0755) != 0 ||
            chmod_grub_generator(root, dirs[i], "10_adavalinux", 0755) != 0) {
            return -1;
        }
    }
    return 0;
}

static int configure_kernel_update_boot(const char *root) {
    char latest[128];
    char previous[128];
    char initrd_latest[128];
    char initrd_previous[128];
    char target[256];

    if (latest_boot_version(root, "vmlinuz-", "", latest, sizeof(latest), previous, sizeof(previous)) != 0) {
        return -1;
    }
    snprintf(target, sizeof(target), "vmlinuz-%s", latest);
    if (relink_boot_file(root, target, "vmlinuz") != 0) {
        return -1;
    }
    if (latest_boot_version(root, "initramfs-disk-", ".gz",
                            initrd_latest, sizeof(initrd_latest),
                            initrd_previous, sizeof(initrd_previous)) == 0) {
        snprintf(target, sizeof(target), "initramfs-disk-%s.gz", initrd_latest);
        if (relink_boot_file(root, target, "initramfs-disk.gz") != 0) {
            return -1;
        }
    }
    (void)previous;
    if (write_default_grub(root) != 0) {
        return -1;
    }
    if (disable_standard_grub_generators(root) != 0) {
        return -1;
    }
    return run_grub_mkconfig(root);
}

int main(int argc, char *argv[]) {
    fprintf(stderr, "SystemPackager by Adava Software for Linux in 2026 v1.0\n");
    fflush(stderr);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s list [--root <path>] [--allow-root]\n", argv[0]);
        fprintf(stderr, "       %s install <name-or-file>... [--no-deps] [--root <path>] [--allow-root] [-local] [-y|--yes]\n", argv[0]);
        fprintf(stderr, "       %s update <name>|--all|--system|--kernel [--with-deps] [--dry-run] [--root <path>] [--allow-root] [-y|--yes]\n", argv[0]);
        fprintf(stderr, "       %s remove <name> [--with-deps] [--root <path>] [--allow-root]\n", argv[0]);
        fprintf(stderr, "       %s download <name-or-file> [--no-deps] [--output <dir>] [-local]\n", argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    int is_list = (strcmp(cmd, "list") == 0 || strcmp(cmd, "list") == 0);
    int is_update = strcmp(cmd, "update") == 0;
    int is_download = strcmp(cmd, "download") == 0;
    if (!is_list && !is_update && !is_download && strcmp(cmd, "install") != 0 && strcmp(cmd, "remove") != 0) {
        fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Unknown argument: %s\n", argv[1]);
        return 1;
    }

    if (!is_list && !is_update && argc < 3) {
        fprintf(stderr, "Usage: %s list [--root <path>] [--allow-root]\n", argv[0]);
        fprintf(stderr, "       %s install <name-or-file>... [--root <path>] [--allow-root] [-local] [-y|--yes]\n", argv[0]);
        fprintf(stderr, "       %s update <name>|--all|--system|--kernel [--dry-run] [--root <path>] [--allow-root] [-y|--yes]\n", argv[0]);
        fprintf(stderr, "       %s remove <name> [--root <path>] [--allow-root]\n", argv[0]);
        return 1;
    }

    const char *root = "/";
    int allow_root = 0;
    int local_only = 0;
    int assume_yes = 0;
    int dry_run = 0;
    int update_all = 0;
    int update_system = 0;
    int update_kernel = 0;
    int update_packager = 0;
    const char *update_pkg_arg = NULL;
    char update_selector[PATH_MAX] = {0};
    char update_expected_version[64] = {0};
    char packager_local_version[64] = {0};
    char packager_remote_version[64] = {0};
    char kernel_local_version[64] = {0};
    char kernel_remote_version[64] = {0};
    int packager_up_to_date = 0;
    int kernel_up_to_date = 0;
    int with_deps = 0;
    int no_deps = 0;
    const char *download_output = ".";
    const char *install_pkg_args[argc];
    size_t install_pkg_count = 0;
    if (strcmp(cmd, "install") == 0) {
        install_pkg_args[install_pkg_count++] = argv[2];
    }
    int opt_start = is_list ? 2 : (is_update ? 2 : 3);
    for (int i = opt_start; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0) {
            if (i + 1 >= argc) {
                log_err("Missing value for --root");
                return 1;
            }
            root = argv[i + 1];
            i++;
            continue;
        }
        if (strcmp(argv[i], "--allow-root") == 0) {
            allow_root = 1;
            continue;
        }
        if (strcmp(argv[i], "--with-deps") == 0) {
            if (strcmp(cmd, "install") == 0 || is_download) {
                log_err("--with-deps is only valid for update or remove");
                return 1;
            }
            with_deps = 1;
            continue;
        }
        if (strcmp(argv[i], "--no-deps") == 0) {
            if (strcmp(cmd, "install") != 0 && !is_download) {
                log_err("--no-deps is only valid for install or download");
                return 1;
            }
            no_deps = 1;
            continue;
        }
        if (is_download && strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                log_err("Missing value for --output");
                return 1;
            }
            download_output = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-local") == 0 || strcmp(argv[i], "--local") == 0) {
            if (is_update) {
                log_err("-local/--local is only valid for install or list");
                return 1;
            }
            local_only = 1;
            continue;
        }
        if (strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "--yes") == 0) {
            if (is_list) {
                log_err("-y/--yes is only valid for install or update");
                return 1;
            }
            assume_yes = 1;
            continue;
        }
        if (is_update && strcmp(argv[i], "--dry-run") == 0) {
            dry_run = 1;
            continue;
        }
        if (is_update && strcmp(argv[i], "--all") == 0) {
            update_all = 1;
            continue;
        }
        if (is_update && strcmp(argv[i], "--system") == 0) {
            update_system = 1;
            continue;
        }
        if (is_update && strcmp(argv[i], "--kernel") == 0) {
            update_kernel = 1;
            continue;
        }
        if (is_update && strcmp(argv[i], "--packager") == 0) {
            log_err("update --packager was removed; use: syspckg update syspckg");
            return 1;
        }
        if (is_update && argv[i][0] != '-') {
            if (update_pkg_arg != NULL) {
                log_err("Only one update package can be specified");
                return 1;
            }
            update_pkg_arg = argv[i];
            continue;
        }
        if (strcmp(cmd, "install") == 0 && argv[i][0] != '-') {
            install_pkg_args[install_pkg_count++] = argv[i];
            continue;
        }
        fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Unknown option: %s\n", argv[i]);
        return 1;
    }

    if (with_deps && no_deps) {
        log_err("Use only one dependency mode");
        return 1;
    }
    if (is_download) {
        struct stat st;
        if (stat(download_output, &st) != 0 || !S_ISDIR(st.st_mode)) {
            log_err("Download output directory does not exist");
            return 1;
        }
    }

    if (strcmp(cmd, "remove") == 0 && local_only) {
        log_err("-local is only valid for install");
        return 1;
    }

    if (!is_download && strcmp(root, "/") == 0 && !allow_root && !is_adavalinux_root(root)) {
        log_err("Refusing to operate on / on non-AdavaLinux. Use --root <path> or --allow-root.");
        return 1;
    }

    /* Keep the existing single-package transaction implementation, but make
     * the CLI accept a package list.  Each child receives the same install
     * options and handles its own dependency closure. */
    if (strcmp(cmd, "install") == 0 && install_pkg_count > 1) {
        int all_rc = 0;

        for (size_t i = 0; i < install_pkg_count; i++) {
            char *child_argv[10];
            size_t n = 0;

            child_argv[n++] = argv[0];
            child_argv[n++] = "install";
            child_argv[n++] = (char *)install_pkg_args[i];
            child_argv[n++] = "--root";
            child_argv[n++] = (char *)root;
            if (allow_root) {
                child_argv[n++] = "--allow-root";
            }
            if (local_only) {
                child_argv[n++] = "-local";
            }
            if (assume_yes) {
                child_argv[n++] = "-y";
            }
            child_argv[n] = NULL;
            if (run_cmd(child_argv) != 0) {
                all_rc = 1;
            }
        }
        return all_rc;
    }

    if (is_list) {
        log_info(local_only ? "Listing locally installed packages" : "Listing packages on server");
        if ((local_only ? list_local_installed_packages(root) : list_remote_packages(root)) != 0) {
            return 1;
        }
        log_ok("Done");
        return 0;
    }

    if (is_update) {
        int mode_count = update_all + update_system + update_kernel + update_packager + (update_pkg_arg != NULL ? 1 : 0);
        if (mode_count != 1) {
            log_err("Use exactly one update target: <name>, --all, --system, --kernel or --packager");
            return 1;
        }

        if (update_system) {
            int resolved = resolve_latest_system_update(root, update_selector, sizeof(update_selector),
                                                        update_expected_version, sizeof(update_expected_version));
            if (resolved == 1) {
                log_info("System is up to date");
                return 0;
            }
            if (resolved != 0) {
                log_err("Failed to resolve system update");
                return 1;
            }
        } else if (update_kernel) {
            int kernel_resolved = resolve_latest_kernel_update(root,
                                                               update_selector,
                                                               sizeof(update_selector),
                                                               kernel_remote_version,
                                                               sizeof(kernel_remote_version),
                                                               kernel_local_version,
                                                               sizeof(kernel_local_version));
            if (kernel_resolved < 0) {
                log_err("Failed to resolve kernel update");
                return 1;
            }
            if (kernel_resolved == 1) {
                kernel_up_to_date = 1;
            }
        } else if (update_packager) {
            int packager_resolved = resolve_latest_packager_update(root,
                                                                   update_selector,
                                                                   sizeof(update_selector),
                                                                   packager_remote_version,
                                                                   sizeof(packager_remote_version),
                                                                   packager_local_version,
                                                                   sizeof(packager_local_version));
            if (packager_resolved < 0) {
                log_err("Failed to resolve packager update");
                return 1;
            }
            if (packager_resolved == 1) {
                packager_up_to_date = 1;
            }
        } else if (update_pkg_arg != NULL) {
            if (snprintf(update_selector, sizeof(update_selector), "%s", update_pkg_arg) >= (int)sizeof(update_selector)) {
                log_err("Update selector too long");
                return 1;
            }
        }

        if (dry_run) {
            if (update_all) {
                char **update_urls = NULL;
                size_t update_url_count = 0;
                if (check_all_updates_from_manifest(root, &update_urls, &update_url_count) != 0) {
                    log_err("Failed to check package updates");
                    return 1;
                }
                free_strings(update_urls, update_url_count);
                return 0;
            }
            if (update_packager && packager_up_to_date) {
                printf("Packager is up to date: %s\n", packager_local_version);
                return 0;
            }
            if (update_kernel && kernel_up_to_date) {
                printf("Kernel is up to date: %s\n", kernel_local_version[0] ? kernel_local_version : kernel_remote_version);
                return 0;
            }
            printf("Would install: %s\n", update_selector);
            return 0;
        }

        if (update_packager && packager_up_to_date) {
            printf("Packager is up to date: %s\n", packager_local_version);
            return 0;
        }
        if (update_kernel && kernel_up_to_date) {
            printf("Kernel is up to date: %s\n", kernel_local_version[0] ? kernel_local_version : kernel_remote_version);
            return 0;
        }

        if (update_all) {
            char **update_urls = NULL;
            size_t update_url_count = 0;
            int all_rc = 0;
            if (check_all_updates_from_manifest(root, &update_urls, &update_url_count) != 0) {
                log_err("Failed to check package updates");
                return 1;
            }
            for (size_t i = 0; i < update_url_count; i++) {
                char *child_argv[] = {
                    argv[0], "update", update_urls[i], "--root", (char *)root,
                    "--allow-root", "-y", NULL
                };
                if (run_cmd(child_argv) != 0) {
                    all_rc = 1;
                }
            }
            free_strings(update_urls, update_url_count);
            return all_rc;
        }

        cmd = "install";
    }

    const char *pkg_arg = is_update ? update_selector : argv[2];

    char pkg_name[PATH_MAX];
    if (get_pkg_base(pkg_arg, pkg_name, sizeof(pkg_name)) != 0) {
        log_err("Package name too long");
        return 1;
    }

    if (strcmp(cmd, "remove") == 0) {
        if (!strchr(argv[2], '/') &&
            !has_suffix(argv[2], ".syspckg") &&
            !selector_has_explicit_version(argv[2])) {
            char resolved_remove[PATH_MAX];
            if (resolve_latest_installed(root, argv[2], resolved_remove, sizeof(resolved_remove)) == 0) {
                snprintf(pkg_name, sizeof(pkg_name), "%s", resolved_remove);
            }
        }
        if (!assume_yes && !ask_remove_confirmation(pkg_name)) {
            log_info("Removal cancelled");
            return 0;
        }
        log_info("Removing package");
        if (!with_deps && remove_pkg(root, pkg_name) != 0) {
            return 1;
        }
        if (with_deps) {
            char remove_tmp_template[] = "/tmp/syspckg-remove-XXXXXX";
            char *remove_tmpdir = mkdtemp(remove_tmp_template);
            if (remove_tmpdir) {
                snprintf(g_tmpdir, sizeof(g_tmpdir), "%s", remove_tmpdir);
            }
            if (!remove_tmpdir ||
                remove_pkg_with_deps(root, pkg_name, remove_tmpdir, local_only, 0) != 0) {
                if (remove_tmpdir) {
                    char *rm_argv[] = { "rm", "-rf", remove_tmpdir, NULL };
                    run_cmd(rm_argv);
                }
                g_tmpdir[0] = '\0';
                return 1;
            }
            char *rm_argv[] = { "rm", "-rf", remove_tmpdir, NULL };
            run_cmd(rm_argv);
            g_tmpdir[0] = '\0';
        }
        if (refresh_dynamic_linker_cache(root) != 0) {
            log_err("Failed to refresh dynamic linker cache");
            return 1;
        }
        log_ok("Removed");
        return 0;
    }

    char tmp_template[] = "/tmp/syspckg-XXXXXX";
    char *tmpdir = mkdtemp(tmp_template);
    if (!tmpdir) {
        perror("ERR: mkdtemp");
        return 1;
    }
    snprintf(g_tmpdir, sizeof(g_tmpdir), "%s", tmpdir);
    atexit(cleanup_tmpdir);
    install_signal_handlers();

    char os_id[64];
    char os_version[64];
    if (get_os_id(root, os_id, sizeof(os_id)) != 0 || get_os_version(root, os_version, sizeof(os_version)) != 0) {
        fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "OS ID/VERSION not found in %s/etc/os-release or %s/usr/lib/os-release\n", root, root);
        return 1;
    }

    pkg_plan_t *plans = NULL;
    size_t plan_count = 0;
    char **queue = NULL;
    size_t queue_count = 0;
    size_t queue_idx = 0;
    char **missing = NULL;
    size_t missing_count = 0;
    size_t *install_order = NULL;
    size_t install_order_count = 0;
    int rc = 1;

    char initial_selector[PATH_MAX];
    if (strchr(pkg_arg, '/') || has_suffix(pkg_arg, ".syspckg")) {
        snprintf(initial_selector, sizeof(initial_selector), "%s", pkg_arg);
    } else {
        snprintf(initial_selector, sizeof(initial_selector), "%s", pkg_name);
    }
    if (push_string(&queue, &queue_count, initial_selector) != 0) {
        log_err("Out of memory");
        goto install_cleanup;
    }
    if (update_kernel && push_string(&queue, &queue_count, target_grub_package(root)) != 0) {
        log_err("Out of memory");
        goto install_cleanup;
    }

    log_info("Phase 1/3: Downloading package set");
    while (queue_idx < queue_count) {
        const char *selector = queue[queue_idx++];
        if (find_plan_by_selector(plans, plan_count, selector) >= 0) {
            continue;
        }

        if (!strchr(selector, '/') &&
            !has_suffix(selector, ".syspckg") &&
            selector_has_explicit_version(selector) &&
            !is_download &&
            is_pkg_installed(root, selector)) {
            continue;
        }

        int is_root_request = (strcmp(selector, initial_selector) == 0);
        pkg_plan_t plan;
        memset(&plan, 0, sizeof(plan));
        if (snprintf(plan.selector, sizeof(plan.selector), "%s", selector) >= (int)sizeof(plan.selector)) {
            log_err("Package selector too long");
            goto install_cleanup;
        }

        if (resolve_pkg_archive_path(root, tmpdir, selector, local_only,
                                     plan.pkg_name, sizeof(plan.pkg_name),
                                     plan.archive_path, sizeof(plan.archive_path)) != 0) {
            if (is_root_request) {
                log_err("Failed to resolve root package");
                goto install_cleanup;
            }
            if (push_string(&missing, &missing_count, selector) != 0) {
                log_err("Out of memory");
                goto install_cleanup;
            }
            continue;
        }

        if (read_pkg_metadata_from_archive(plan.archive_path,
                                           plan.pkg_ver, sizeof(plan.pkg_ver),
                                           plan.pkg_id, sizeof(plan.pkg_id),
                                           plan.pkg_version, sizeof(plan.pkg_version),
                                           &plan.deps, &plan.dep_count) != 0) {
            if (is_root_request) {
                log_err("Root package metadata is missing or invalid");
                goto install_cleanup;
            }
            if (push_string(&missing, &missing_count, selector) != 0) {
                log_err("Out of memory");
                goto install_cleanup;
            }
            continue;
        }
        if (!is_safe_id(plan.pkg_id) || !is_safe_version(plan.pkg_version) || !is_safe_version(plan.pkg_ver)) {
            if (is_root_request) {
                log_err("Invalid root package metadata values");
                goto install_cleanup;
            }
            if (push_string(&missing, &missing_count, selector) != 0) {
                log_err("Out of memory");
                goto install_cleanup;
            }
            continue;
        }
        const char *expected_version = update_expected_version[0] != '\0' ? update_expected_version : os_version;
        if (strcmp(plan.pkg_id, os_id) != 0 || strcmp(plan.pkg_version, expected_version) != 0) {
            if (is_root_request) {
                fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Package built for %s %s, but target is %s %s\n",
                        plan.pkg_id, plan.pkg_version, os_id, expected_version);
                goto install_cleanup;
            }
            if (push_string(&missing, &missing_count, selector) != 0) {
                log_err("Out of memory");
                goto install_cleanup;
            }
            continue;
        }

        if (snprintf(plan.install_name, sizeof(plan.install_name), "%s", plan.pkg_name) >= (int)sizeof(plan.install_name)) {
            log_err("Package name too long");
            goto install_cleanup;
        }
        if (plan.pkg_ver[0] != '\0') {
            char suffix[128];
            if (snprintf(suffix, sizeof(suffix), "-%s", plan.pkg_ver) < (int)sizeof(suffix) &&
                !has_suffix(plan.install_name, suffix)) {
                if (snprintf(plan.install_name, sizeof(plan.install_name), "%s-%s", plan.pkg_name, plan.pkg_ver) >= (int)sizeof(plan.install_name)) {
                    log_err("Package name too long after version append");
                    goto install_cleanup;
                }
            }
        }
        int installed_match = find_installed_package_for_update(root, plan.install_name,
                                                               plan.installed_name,
                                                               sizeof(plan.installed_name));
        if (installed_match < 0) {
            log_err("Failed to inspect installed package state");
            goto install_cleanup;
        }
        if (!is_update && is_root_request && installed_match > 0 &&
            strcmp(plan.installed_name, plan.install_name) != 0) {
            fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET
                    "Package already installed as %s. Use update instead.\n",
                    plan.installed_name);
            goto install_cleanup;
        }
        plan.installed = is_pkg_installed(root, plan.install_name);
        if (is_update && with_deps) {
            plan.installed = 0;
        }

        pkg_plan_t *next = realloc(plans, (plan_count + 1) * sizeof(*plans));
        if (!next) {
            log_err("Out of memory");
            goto install_cleanup;
        }
        plans = next;
        plans[plan_count++] = plan;

        int include_deps = !no_deps && (!is_update || with_deps);
        for (size_t i = 0; include_deps && i < plan.dep_count; i++) {
            const char *dep = plan.deps[i];
            if (!is_safe_pkgname(dep)) {
                continue;
            }
            if (strcmp(dep, plan.install_name) == 0 || strcmp(dep, plan.pkg_name) == 0) {
                continue;
            }
            if (name_exists(queue, queue_count, dep)) {
                continue;
            }
            if (push_string(&queue, &queue_count, dep) != 0) {
                log_err("Out of memory");
                goto install_cleanup;
            }
        }
    }
    log_ok("Download phase complete");

    if (missing_count > 0) {
        fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Missing required dependencies: ");
        for (size_t i = 0; i < missing_count; i++) {
            fprintf(stderr, "%s%s", missing[i], (i + 1 < missing_count) ? ", " : "");
        }
        fprintf(stderr, "\n");
        goto install_cleanup;
    }

    if (is_download) {
        for (size_t i = 0; i < plan_count; i++) {
            if (copy_archive_to_dir(plans[i].archive_path, download_output) != 0) {
                log_err("Failed to save downloaded package archive");
                goto install_cleanup;
            }
            printf("Downloaded %s to %s\n", plans[i].install_name, download_output);
        }
        log_ok("Download complete");
        rc = 0;
        goto install_cleanup;
    }

    char **to_install_names = NULL;
    size_t to_install_count = 0;
    for (size_t i = 0; i < plan_count; i++) {
        if (!plans[i].installed) {
            if (push_string(&to_install_names, &to_install_count, plans[i].install_name) != 0) {
                free_strings(to_install_names, to_install_count);
                log_err("Out of memory");
                goto install_cleanup;
            }
        }
    }
    if (to_install_count == 0) {
        log_info("All selected packages are already installed, nothing to do");
        free_strings(to_install_names, to_install_count);
        rc = 0;
        goto install_cleanup;
    }
    if (!assume_yes && !ask_install_confirmation(to_install_names, to_install_count)) {
        free_strings(to_install_names, to_install_count);
        log_info("Installation cancelled");
        rc = 0;
        goto install_cleanup;
    }
    free_strings(to_install_names, to_install_count);

    log_info("Phase 2/3: Extracting packages");
    for (size_t i = 0; i < plan_count; i++) {
        if (plans[i].installed) {
            continue;
        }
        if (snprintf(plans[i].extract_dir, sizeof(plans[i].extract_dir), "%s/extract-%zu", tmpdir, i) >= (int)sizeof(plans[i].extract_dir)) {
            log_err("Extract path too long");
            goto install_cleanup;
        }
        if (ensure_dir_recursive(plans[i].extract_dir, 0755) != 0) {
            perror("mkdir");
            goto install_cleanup;
        }
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof(msg), "Extracting %s", plans[i].archive_path);
        log_info(msg);
        char *tar_argv[] = { "tar", "-xJf", plans[i].archive_path, "-C", plans[i].extract_dir, NULL };
        if (run_cmd(tar_argv) != 0) {
            goto install_cleanup;
        }
        if (find_install_dir(plans[i].extract_dir, plans[i].install_dir, sizeof(plans[i].install_dir)) != 0) {
            goto install_cleanup;
        }
        plans[i].extracted = 1;
    }
    log_ok("Extraction phase complete");

    if (build_install_order(plans, plan_count, &install_order, &install_order_count) != 0) {
        log_err("Failed to determine dependency installation order");
        goto install_cleanup;
    }

    log_info("Phase 3/3: Installing packages");
    int install_errors = 0;
    /* Stage every payload first.  A hook may execute a program supplied by a
     * different package, so files and the dynamic linker cache must exist
     * before any hook is allowed to run. */
    for (size_t i = 0; i < install_order_count; i++) {
        pkg_plan_t *p = &plans[install_order[i]];
        if (p->installed || !p->extracted) {
            continue;
        }
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof(msg), "Installing %s", p->install_name);
        log_info(msg);
        if (is_update && p->installed_name[0] != '\0' &&
            strcmp(p->installed_name, p->install_name) != 0 &&
            remove_pkg_impl(root, p->installed_name, 0) != 0) {
            fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Failed to remove old package version: %s\n",
                    p->installed_name);
            p->extracted = 0;
            install_errors++;
            continue;
        }
        if (copy_tree(p->install_dir, "", root) != 0) {
            perror("copy_tree");
            fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Failed to install package: %s\n", p->install_name);
            p->extracted = 0;
            install_errors++;
            continue;
        }
        if (write_manifest(p->install_dir, root, p->install_name) != 0) {
            fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Failed to write manifest for: %s\n", p->install_name);
            p->extracted = 0;
            install_errors++;
            continue;
        }
        if (save_remove_hook(p->install_dir, root, p->install_name) != 0) {
            fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Failed to save remove hook for: %s\n", p->install_name);
            (void)remove_pkg_impl(root, p->install_name, 0);
            p->extracted = 0;
            install_errors++;
            continue;
        }
    }
    if (install_errors > 0) {
        log_err("Some packages failed to install");
        goto install_cleanup;
    }

    if (refresh_dynamic_linker_cache(root) != 0) {
        log_err("Failed to refresh dynamic linker cache");
        goto install_cleanup;
    }

    for (size_t i = 0; i < install_order_count; i++) {
        pkg_plan_t *p = &plans[install_order[i]];
        if (p->installed || !p->extracted) {
            continue;
        }
        if (run_package_hook(p->install_dir, root, "install") != 0) {
            fprintf(stderr, COLOR_RED "ERR: " COLOR_RESET "Install hook failed for: %s\n", p->install_name);
            (void)remove_pkg_impl(root, p->install_name, 0);
            install_errors++;
            continue;
        }
        log_ok("Install complete");
    }
    if (install_errors > 0) {
        log_err("Some packages failed to install");
        goto install_cleanup;
    }

    if (refresh_dynamic_linker_cache(root) != 0) {
        log_err("Failed to refresh dynamic linker cache");
        goto install_cleanup;
    }

    if (update_kernel && configure_kernel_update_boot(root) != 0) {
        log_err("Kernel package installed, but boot configuration update failed");
        goto install_cleanup;
    }

    log_ok("Done");
    rc = 0;

install_cleanup:
    free_strings(queue, queue_count);
    free_strings(missing, missing_count);
    free(install_order);
    free_plans(plans, plan_count);
    return rc;
}
