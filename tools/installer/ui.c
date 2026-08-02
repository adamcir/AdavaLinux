#define _POSIX_C_SOURCE 200809L

#include "ui.h"

#include "install.h"
#include "sys.h"

#include <ctype.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define INSTALLER_LOG_PATH "/tmp/adavalinux-installer.log"
#define TARGET_INSTALLER_LOG_PATH "/mnt/root/var/log/adavalinux-installer.log"

enum {
    C_BG = 1,
    C_DIALOG,
    C_TITLE,
    C_HILITE,
    C_BUTTON,
    C_ERROR,
    C_LOG,
    C_OK
};

typedef struct {
    char lines[18][160];
    int line_colors[18];
    int line_count;
    int percent;
    char step[128];
    int done;
    int rc;
    int active;
    long last_draw_ms;
    FILE *log_file;
} ProgressUi;

typedef enum {
    STEP_WELCOME = 0,
    STEP_MEDIA,
    STEP_DISK,
    STEP_BOOT,
    STEP_ACPI,
    STEP_PARTSIZE,
    STEP_HOSTNAME,
    STEP_USER,
    STEP_USERPASS,
    STEP_ROOTPASS,
    STEP_CONFIRM,
    STEP_SUMMARY
} WizardStep;

static void draw_progress(ProgressUi *ui);

static void center_text(int y, const char *text, int color)
{
    int x = (COLS - (int)strlen(text)) / 2;
    attron(COLOR_PAIR(color) | A_BOLD);
    mvaddstr(y, x > 0 ? x : 0, text);
    attroff(COLOR_PAIR(color) | A_BOLD);
}

static WINDOW *dialog_window(int h, int w, const char *title)
{
    WINDOW *win;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    if (h > LINES - 2) {
        h = LINES - 2;
        y = 1;
    }
    if (w > COLS - 2) {
        w = COLS - 2;
        x = 1;
    }

    bkgd(COLOR_PAIR(C_BG));
    erase();
    center_text(1, "AdavaLinux Installer", C_TITLE);
    win = newwin(h, w, y, x);
    wbkgd(win, COLOR_PAIR(C_DIALOG));
    box(win, 0, 0);
    wattron(win, COLOR_PAIR(C_TITLE) | A_BOLD);
    mvwprintw(win, 0, 3, " %s ", title);
    wattroff(win, COLOR_PAIR(C_TITLE) | A_BOLD);
    return win;
}

static void footer(const char *text)
{
    attron(COLOR_PAIR(C_TITLE));
    mvhline(LINES - 2, 0, ' ', COLS);
    mvprintw(LINES - 2, 2, "%s", text);
    attroff(COLOR_PAIR(C_TITLE));
    wnoutrefresh(stdscr);
}

static long monotonicish_ms(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }
    return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

static int draw_wrapped_text(WINDOW *win, int row, int col, int width, int max_row, const char *text)
{
    const char *p = text;

    while (*p != '\0' && row <= max_row) {
        int len = 0;
        int last_space = -1;
        int take;

        while (p[len] != '\0' && p[len] != '\n' && len < width) {
            if (p[len] == ' ') {
                last_space = len;
            }
            len++;
        }

        if (p[len] == '\n') {
            take = len;
        } else if (p[len] == '\0') {
            take = len;
        } else if (last_space > 0) {
            take = last_space;
        } else {
            take = width;
        }

        mvwprintw(win, row++, col, "%-*.*s", width, take, p);
        p += take;
        while (*p == ' ') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }

    return row;
}

static void draw_button_centered(WINDOW *win, int row, int win_width, const char *label)
{
    char button[64];
    int x;

    snprintf(button, sizeof(button), "< %s >", label);
    x = (win_width - (int)strlen(button)) / 2;
    wattron(win, COLOR_PAIR(C_BUTTON) | A_BOLD);
    mvwprintw(win, row, x > 1 ? x : 1, "%s", button);
    wattroff(win, COLOR_PAIR(C_BUTTON) | A_BOLD);
}

static int message_box(const char *title, const char *body, const char *button)
{
    const int w = 68;
    WINDOW *win = dialog_window(12, w, title);

    (void)draw_wrapped_text(win, 2, 3, w - 6, 8, body);
    draw_button_centered(win, 9, w, button);
    footer("Enter: continue   Esc: cancel");
    wrefresh(win);
    for (;;) {
        int ch = getch();
        if (ch == '\n' || ch == KEY_ENTER) {
            delwin(win);
            return 1;
        }
        if (ch == 27) {
            delwin(win);
            return 0;
        }
    }
}

static int menu_box(const char *title, const char *intro, const char **items, int count, int selected)
{
    int i;
    int ch;
    const int w = 76;
    const int item_col = 5;
    const int item_width = w - item_col - 4;

    for (;;) {
        WINDOW *win = dialog_window(14 + count, w, title);
        draw_wrapped_text(win, 2, 3, w - 6, 3, intro);
        for (i = 0; i < count; i++) {
            if (i == selected) {
                wattron(win, COLOR_PAIR(C_HILITE) | A_BOLD);
            }
            mvwprintw(win, 4 + i, item_col, "%-*.*s", item_width, item_width, items[i]);
            if (i == selected) {
                wattroff(win, COLOR_PAIR(C_HILITE) | A_BOLD);
            }
        }
        footer("Up/Down: select   Enter: select   Esc: previous");
        wrefresh(win);
        ch = getch();
        delwin(win);
        if (ch == KEY_UP && selected > 0) {
            selected--;
        } else if (ch == KEY_DOWN && selected + 1 < count) {
            selected++;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            return selected;
        } else if (ch == 27) {
            return -1;
        }
    }
}

static int input_box(const char *title, const char *label, char *out, size_t out_size, int secret)
{
    int pos = (int)strlen(out);
    int ch;

    curs_set(1);
    for (;;) {
        WINDOW *win = dialog_window(10, 72, title);
        char display[128];
        size_t i;

        memset(display, 0, sizeof(display));
        if (secret) {
            for (i = 0; i < strlen(out) && i + 1 < sizeof(display); i++) {
                display[i] = '*';
            }
        } else {
            snprintf(display, sizeof(display), "%s", out);
        }
        mvwprintw(win, 2, 3, "%s", label);
        mvwprintw(win, 4, 4, "[%-58.58s]", display);
        wmove(win, 4, 5 + pos);
        footer("Type text   Backspace: delete   Enter: confirm   Esc: previous");
        wrefresh(win);

        ch = getch();
        delwin(win);
        if (ch == '\n' || ch == KEY_ENTER) {
            curs_set(0);
            return 1;
        }
        if (ch == 27) {
            curs_set(0);
            return 0;
        }
        if ((ch == KEY_BACKSPACE || ch == 127 || ch == '\b') && pos > 0) {
            out[--pos] = '\0';
        } else if (isprint(ch) && pos + 1 < (int)out_size && pos < 58) {
            out[pos++] = (char)ch;
            out[pos] = '\0';
        }
    }
}

static int confirm_phrase_box(const InstallerConfig *cfg)
{
    char expected[96];
    char body[512];

    snprintf(expected, sizeof(expected), "ERASE %s", cfg->disk);
    snprintf(body, sizeof(body),
             "Target disk: %s\n\nALL DATA ON THIS DISK WILL BE PERMANENTLY DESTROYED.\n\nType exactly: %s",
             cfg->disk, expected);
    if (!message_box("Destructive Install", body, "I understand")) {
        return 0;
    }

    for (;;) {
        char typed[96] = "";

        if (!input_box("Final Confirmation", expected, typed, sizeof(typed), 0)) {
            return 0;
        }
        if (installer_confirmation_phrase_matches(cfg->disk, typed)) {
            return 1;
        }
        message_box("Confirmation Failed", "The confirmation text did not match.", "Back");
    }
}

static void ui_log(void *ctx, const char *line)
{
    ProgressUi *ui = (ProgressUi *)ctx;
    int color = strncmp(line, "OK:", 3) == 0 ? C_OK : C_LOG;
    int i;

    if (ui->log_file != NULL) {
        fprintf(ui->log_file, "%s\n", line);
        fflush(ui->log_file);
    }

    if (ui->line_count < 18) {
        snprintf(ui->lines[ui->line_count], sizeof(ui->lines[0]), "%s", line);
        ui->line_colors[ui->line_count++] = color;
    } else {
        for (i = 1; i < 18; i++) {
            snprintf(ui->lines[i - 1], sizeof(ui->lines[0]), "%s", ui->lines[i]);
            ui->line_colors[i - 1] = ui->line_colors[i];
        }
        snprintf(ui->lines[17], sizeof(ui->lines[0]), "%s", line);
        ui->line_colors[17] = color;
    }
    if (ui->active && installer_should_redraw_progress_log(monotonicish_ms(), ui->last_draw_ms, 200)) {
        draw_progress(ui);
    }
}

static void copy_log_to_target(void)
{
    FILE *src;
    FILE *dst;
    char buf[512];
    size_t got;

    mkdir("/mnt/root/var", 0755);
    mkdir("/mnt/root/var/log", 0755);

    src = fopen(INSTALLER_LOG_PATH, "r");
    if (src == NULL) {
        return;
    }
    dst = fopen(TARGET_INSTALLER_LOG_PATH, "w");
    if (dst == NULL) {
        fclose(src);
        return;
    }
    while ((got = fread(buf, 1, sizeof(buf), src)) > 0) {
        (void)fwrite(buf, 1, got, dst);
    }
    fclose(dst);
    fclose(src);
}

static void request_reboot(void)
{
    char *reboot_sbin[] = { "/sbin/reboot", NULL };
    char *busybox_reboot[] = { "/bin/busybox", "reboot", NULL };
    char *reboot_path[] = { "reboot", NULL };

    endwin();
    if (installer_run_command(reboot_sbin, NULL, NULL) != 0 &&
        installer_run_command(busybox_reboot, NULL, NULL) != 0) {
        (void)installer_run_command(reboot_path, NULL, NULL);
    }
    reset_prog_mode();
    refresh();
    message_box("Reboot Failed", "The reboot command did not complete.\nYou can reboot manually from the shell.", "Close");
}

static void draw_progress(ProgressUi *ui)
{
    WINDOW *win;
    char bar[80];
    char visual_lines[64][160];
    int visual_colors[64];
    int visual_count = 0;
    int i;
    int h = LINES > 26 ? 20 : LINES - 4;
    int w = COLS > 78 ? 72 : COLS - 4;
    int bar_width;
    int bar_x;
    int log_rows;
    int log_width;

    if (h < 16) {
        h = 16;
    }
    if (w < 64) {
        w = 64;
    }
    bar_width = w - 28;
    if (bar_width > 48) {
        bar_width = 48;
    }
    if (bar_width < 24) {
        bar_width = 24;
    }
    log_rows = h - 10;
    if (log_rows > 15) {
        log_rows = 15;
    }
    log_width = w - 8;
    bar_x = (w - (bar_width + 7)) / 2;

    for (i = 0; i < ui->line_count && visual_count < 64; i++) {
        size_t start = 0;
        size_t next = 0;
        do {
            if (!installer_next_wrapped_log_segment(ui->lines[i],
                                                   start,
                                                   (size_t)log_width,
                                                   visual_lines[visual_count],
                                                   sizeof(visual_lines[visual_count]),
                                                   &next)) {
                break;
            }
            visual_colors[visual_count++] = ui->line_colors[i];
            start = next;
        } while (ui->lines[i][start] != '\0' && visual_count < 64);
    }

    win = dialog_window(h, w, "Installing");
    mvwprintw(win, 2, 3, "%s", ui->step[0] ? ui->step : "Starting");
    installer_format_progress_bar(ui->percent, bar_width, bar, sizeof(bar));
    wattron(win, COLOR_PAIR(C_HILITE) | A_BOLD);
    mvwprintw(win, 4, bar_x > 2 ? bar_x : 2, "%s", bar);
    wattroff(win, COLOR_PAIR(C_HILITE) | A_BOLD);
    mvwprintw(win, 6, 3, "Installation log:");
    for (i = 0; i < visual_count && i < log_rows; i++) {
        int idx = visual_count > log_rows ? visual_count - log_rows + i : i;
        wattron(win, COLOR_PAIR(visual_colors[idx]));
        if (visual_colors[idx] == C_OK) {
            wattron(win, A_BOLD);
        }
        mvwprintw(win, 8 + i, 3, "%-*.*s", log_width, log_width, visual_lines[idx]);
        if (visual_colors[idx] == C_OK) {
            wattroff(win, A_BOLD);
        }
        wattroff(win, COLOR_PAIR(visual_colors[idx]));
    }
    footer("Please wait. Do not power off this machine.");
    wnoutrefresh(win);
    doupdate();
    ui->last_draw_ms = monotonicish_ms();
    delwin(win);
}

static void ui_progress(void *ctx, int percent, const char *step_name)
{
    ProgressUi *ui = (ProgressUi *)ctx;

    ui->percent = percent;
    snprintf(ui->step, sizeof(ui->step), "%s", step_name);
    draw_progress(ui);
}

int installer_ui_run_install(const InstallerConfig *cfg)
{
    ProgressUi ui;

    memset(&ui, 0, sizeof(ui));
    ui.log_file = fopen(INSTALLER_LOG_PATH, "w");
    if (ui.log_file != NULL) {
        fprintf(ui.log_file, "AdavaLinux installer log\n");
        fflush(ui.log_file);
    }
    ui.active = 1;
    draw_progress(&ui);
    ui.rc = installer_run_install(cfg, ui_log, ui_progress, &ui);
    ui.done = 1;
    ui.active = 0;
    copy_log_to_target();
    draw_progress(&ui);
    if (ui.log_file != NULL) {
        fclose(ui.log_file);
        ui.log_file = NULL;
    }
    if (ui.rc == 0) {
        if (message_box("Installation Complete",
                        "AdavaLinux has been installed.\n\nLog: /tmp/adavalinux-installer.log\nTarget log: /var/log/adavalinux-installer.log\n\nDetach the installer media and press Enter to reboot.",
                        "Reboot now")) {
            request_reboot();
        }
        return 1;
    }
    char failure_body[1024];
    installer_format_failure_summary(INSTALLER_LOG_PATH, failure_body, sizeof(failure_body));
    message_box("Installation Failed",
                failure_body,
                "Close");
    return 0;
}

static int password_pair_box(const char *title, const char *label, char *password, size_t password_size)
{
    char confirm[128] = "";

    for (;;) {
        password[0] = '\0';
        confirm[0] = '\0';
        if (!input_box(title, label, password, password_size, 1)) {
            return 0;
        }
        if (!input_box("Confirm Password", "Confirm the password:", confirm, sizeof(confirm), 1)) {
            return 0;
        }
        if (password[0] != '\0' && strcmp(password, confirm) == 0) {
            return 1;
        }
        message_box("Password Error", "Passwords must be non-empty and must match.", "Back");
    }
}

static WizardStep previous_step(WizardStep step)
{
    switch (step) {
    case STEP_MEDIA:
        return STEP_WELCOME;
    case STEP_DISK:
        return STEP_MEDIA;
    case STEP_BOOT:
        return STEP_DISK;
    case STEP_ACPI:
        return STEP_BOOT;
    case STEP_PARTSIZE:
        return STEP_ACPI;
    case STEP_HOSTNAME:
        return STEP_PARTSIZE;
    case STEP_USER:
        return STEP_HOSTNAME;
    case STEP_USERPASS:
        return STEP_USER;
    case STEP_ROOTPASS:
        return STEP_USERPASS;
    case STEP_CONFIRM:
        return STEP_ROOTPASS;
    case STEP_SUMMARY:
        return STEP_CONFIRM;
    case STEP_WELCOME:
    default:
        return STEP_WELCOME;
    }
}

static WizardStep next_step(WizardStep step)
{
    switch (step) {
    case STEP_WELCOME:
        return STEP_MEDIA;
    case STEP_MEDIA:
        return STEP_DISK;
    case STEP_DISK:
        return STEP_BOOT;
    case STEP_BOOT:
        return STEP_ACPI;
    case STEP_ACPI:
        return STEP_PARTSIZE;
    case STEP_PARTSIZE:
        return STEP_HOSTNAME;
    case STEP_HOSTNAME:
        return STEP_USER;
    case STEP_USER:
        return STEP_USERPASS;
    case STEP_USERPASS:
        return STEP_ROOTPASS;
    case STEP_ROOTPASS:
        return STEP_CONFIRM;
    case STEP_CONFIRM:
        return STEP_SUMMARY;
    case STEP_SUMMARY:
    default:
        return STEP_SUMMARY;
    }
}

static int summary_box(const InstallerConfig *cfg)
{
    char body[1024];

    snprintf(body, sizeof(body),
             "Disk:       %s\n"
             "Media:      %s\n"
             "Boot mode:  %s\n"
             "ACPI:       %s\n"
             "Root size:  %s\n"
             "Hostname:   %s\n"
             "Username:   %s\n\n"
             "The next screen starts the installation.",
             cfg->disk,
             cfg->install_media,
             cfg->boot_mode == INSTALLER_BOOT_UEFI ? "UEFI" : "BIOS",
             cfg->acpi_mode == INSTALLER_ACPI_OFF ? "off" : "default",
             cfg->part_size[0] ? cfg->part_size : "use remaining/default",
             cfg->hostname,
             cfg->username);
    return message_box("Installation Summary", body, "Install");
}

int installer_ui_collect_config(InstallerConfig *cfg)
{
    InstallerDisk media[32];
    InstallerDisk disks[32];
    size_t media_count = 0;
    size_t disk_count = 0;
    size_t scanned_disk_count = 0;
    const char *boot_items[] = { "BIOS (legacy)", "UEFI" };
    const char *acpi_items[] = { "ACPI default", "ACPI=off (old hardware)" };
    char media_labels[32][160];
    char disk_labels[32][160];
    const char *media_items[32];
    const char *disk_items[32];
    int disk_map[32];
    int selected;
    int media_selected = 0;
    int disk_selected = 0;
    int boot_selected = 0;
    int acpi_selected = 0;
    WizardStep step = STEP_WELCOME;
    size_t i;

    memset(cfg, 0, sizeof(*cfg));
    cfg->boot_mode = INSTALLER_BOOT_BIOS;
    cfg->acpi_mode = INSTALLER_ACPI_ON;
    snprintf(cfg->hostname, sizeof(cfg->hostname), "adavalinux");

    for (;;) {
        switch (step) {
        case STEP_WELCOME:
            if (!message_box("Welcome",
                             "Welcome to AdavaLinux.\n\nThis installer will erase the selected disk and install a AdavaLinux.",
                             "Continue")) {
                return 0;
            }
            step = next_step(step);
            break;

        case STEP_MEDIA:
            if (installer_scan_install_media(media, 32, &media_count) != 0 || media_count == 0) {
                message_box("No Install Media Found",
                            "No supported installation media were found.\nSupported media include srX, sdX, vdX, xvdX, nvme and mmcblk devices.",
                            "Back");
                step = previous_step(step);
                break;
            }
            for (i = 0; i < media_count; i++) {
                snprintf(media_labels[i], sizeof(media_labels[i]), "%-16.16s %8llu MiB  %.96s",
                         media[i].path, media[i].mib, media[i].model);
                media_items[i] = media_labels[i];
            }
            if ((size_t)media_selected >= media_count) {
                media_selected = 0;
            }
            selected = menu_box("Select Install Media",
                                "Choose the media this installer is running from:",
                                media_items,
                                (int)media_count,
                                media_selected);
            if (selected < 0) {
                step = previous_step(step);
                break;
            }
            media_selected = selected;
            snprintf(cfg->install_media, sizeof(cfg->install_media), "%s", media[selected].path);
            if (strcmp(cfg->disk, cfg->install_media) == 0) {
                cfg->disk[0] = '\0';
                disk_selected = 0;
            }
            step = next_step(step);
            break;

        case STEP_DISK:
            if (installer_scan_disks(disks, 32, &scanned_disk_count) != 0 || scanned_disk_count == 0) {
                message_box("No Disks Found",
                            "No supported target disks were found.\nSupported disks include sdX, vdX, xvdX, nvme and mmcblk.",
                            "Back");
                step = previous_step(step);
                break;
            }
            disk_count = 0;
            for (i = 0; i < scanned_disk_count; i++) {
                if (!installer_target_disk_available(disks[i].path, cfg->install_media)) {
                    continue;
                }
                snprintf(disk_labels[disk_count], sizeof(disk_labels[disk_count]), "%-16.16s %8llu MiB  %.96s",
                         disks[i].path, disks[i].mib, disks[i].model);
                disk_items[disk_count] = disk_labels[disk_count];
                disk_map[disk_count] = (int)i;
                disk_count++;
            }
            if (disk_count == 0) {
                message_box("No Target Disks Found",
                            "No supported target disks were found after excluding the selected installation media.",
                            "Back");
                step = previous_step(step);
                break;
            }
            if ((size_t)disk_selected >= disk_count) {
                disk_selected = 0;
            }
            selected = menu_box("Select Target Disk",
                                "Choose the disk to erase and install AdavaLinux onto:",
                                disk_items,
                                (int)disk_count,
                                disk_selected);
            if (selected < 0) {
                step = previous_step(step);
                break;
            }
            disk_selected = selected;
            snprintf(cfg->disk, sizeof(cfg->disk), "%s", disks[disk_map[selected]].path);
            step = next_step(step);
            break;

        case STEP_BOOT:
            selected = menu_box("Boot Mode", "Select how this machine should boot:", boot_items, 2, boot_selected);
            if (selected < 0) {
                step = previous_step(step);
                break;
            }
            boot_selected = selected;
            cfg->boot_mode = selected == 1 ? INSTALLER_BOOT_UEFI : INSTALLER_BOOT_BIOS;
            step = next_step(step);
            break;

        case STEP_ACPI:
            selected = menu_box("ACPI Mode", "Select kernel ACPI behavior:", acpi_items, 2, acpi_selected);
            if (selected < 0) {
                step = previous_step(step);
                break;
            }
            acpi_selected = selected;
            cfg->acpi_mode = selected == 1 ? INSTALLER_ACPI_OFF : INSTALLER_ACPI_ON;
            step = next_step(step);
            break;

        case STEP_PARTSIZE:
            if (!input_box("Root Partition Size",
                           "fdisk size, for example +10G. Empty uses fdisk default:",
                           cfg->part_size,
                           sizeof(cfg->part_size),
                           0)) {
                step = previous_step(step);
                break;
            }
            step = next_step(step);
            break;

        case STEP_HOSTNAME:
            if (!input_box("Hostname", "Hostname for this machine:", cfg->hostname, sizeof(cfg->hostname), 0)) {
                step = previous_step(step);
                break;
            }
            if (cfg->hostname[0] == '\0') {
                snprintf(cfg->hostname, sizeof(cfg->hostname), "adavalinux");
            }
            step = next_step(step);
            break;

        case STEP_USER:
            if (!input_box("User Account", "Username (lowercase, not root):", cfg->username, sizeof(cfg->username), 0)) {
                step = previous_step(step);
                break;
            }
            if (!installer_valid_username(cfg->username)) {
                message_box("Invalid Username", "Use lowercase letters, numbers, '_' or '-'.\nThe username cannot be root.", "Back");
                break;
            }
            step = next_step(step);
            break;

        case STEP_USERPASS:
            if (!password_pair_box("User Password",
                                   "Password for the user account:",
                                   cfg->password,
                                   sizeof(cfg->password))) {
                step = previous_step(step);
                break;
            }
            step = next_step(step);
            break;

        case STEP_ROOTPASS:
            if (!password_pair_box("Root Password", "Password for root:", cfg->root_password, sizeof(cfg->root_password))) {
                step = previous_step(step);
                break;
            }
            step = next_step(step);
            break;

        case STEP_CONFIRM:
            if (!confirm_phrase_box(cfg)) {
                step = previous_step(step);
                break;
            }
            step = next_step(step);
            break;

        case STEP_SUMMARY:
            if (!summary_box(cfg)) {
                step = previous_step(step);
                break;
            }
            return 1;
        }
    }
}

static void init_colors(void)
{
    start_color();
    use_default_colors();
    init_pair(C_BG, COLOR_WHITE, COLOR_GREEN);
    init_pair(C_DIALOG, COLOR_BLACK, COLOR_WHITE);
    init_pair(C_TITLE, COLOR_WHITE, COLOR_YELLOW);
    init_pair(C_HILITE, COLOR_WHITE, COLOR_BLUE);
    init_pair(C_BUTTON, COLOR_WHITE, COLOR_BLUE);
    init_pair(C_ERROR, COLOR_WHITE, COLOR_YELLOW);
    init_pair(C_LOG, COLOR_BLACK, COLOR_WHITE);
    init_pair(C_OK, COLOR_GREEN, COLOR_WHITE);
}

int installer_ui_main(void)
{
    InstallerConfig cfg;
    int rc = 1;

    initscr();
    set_escdelay(25);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    init_colors();
    bkgd(COLOR_PAIR(C_BG));
    refresh();

    if (COLS < 80 || LINES < 24) {
        endwin();
        fprintf(stderr, "Terminal must be at least 80x24.\n");
        return 1;
    }

    if (installer_ui_collect_config(&cfg)) {
        installer_ui_run_install(&cfg);
        rc = 0;
    }

    endwin();
    return rc;
}
