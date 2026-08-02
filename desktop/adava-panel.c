#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum panel_position {
    PANEL_TOP,
    PANEL_BOTTOM,
};

static enum panel_position parse_position(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--position") == 0 && strcmp(argv[2], "bottom") == 0) {
        return PANEL_BOTTOM;
    }
    return PANEL_TOP;
}

static void draw_panel(Display *display, Window window, GC gc, enum panel_position position, int width) {
    char time_text[32] = "";
    time_t now = time(NULL);
    struct tm local_time;
    if (localtime_r(&now, &local_time) != NULL) {
        (void)strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M", &local_time);
    }

    XClearWindow(display, window);
    XDrawString(display, window, gc, 16, 22,
                position == PANEL_TOP ? "AdavaLinux" : "Applications", 
                position == PANEL_TOP ? 10 : 12);
    if (position == PANEL_TOP) {
        XDrawString(display, window, gc, width - 320, 22, "Network: checking", 17);
        XDrawString(display, window, gc, width - 180, 22, time_text, (int)strlen(time_text));
    } else {
        XDrawString(display, window, gc, width - 80, 22, "Menu", 4);
    }
}

int main(int argc, char **argv) {
    const enum panel_position position = parse_position(argc, argv);
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fputs("adava-panel: cannot open X display\n", stderr);
        return EXIT_FAILURE;
    }

    const int screen = DefaultScreen(display);
    const int width = DisplayWidth(display, screen);
    const int height = 32;
    const int y = position == PANEL_TOP ? 0 : DisplayHeight(display, screen) - height;
    Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, y,
                                        (unsigned int)width, (unsigned int)height, 0,
                                        BlackPixel(display, screen), WhitePixel(display, screen));
    XSelectInput(display, window, ExposureMask | StructureNotifyMask);
    XMapRaised(display, window);
    GC gc = XCreateGC(display, window, 0, NULL);

    for (;;) {
        while (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == Expose || event.type == ConfigureNotify) {
                draw_panel(display, window, gc, position, width);
            }
        }
        draw_panel(display, window, gc, position, width);
        XFlush(display);
        sleep(1);
    }
}
