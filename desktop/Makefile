CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
CPPFLAGS ?= -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=
LDLIBS ?= -lX11

BUILD_DIR := build
PACKAGE_DIR := packages
VERSION := 0.1.0

.PHONY: all clean package

all: $(BUILD_DIR)/usr/bin/adava-panel $(BUILD_DIR)/usr/bin/adava-session \
	$(BUILD_DIR)/usr/bin/adava-greeter $(BUILD_DIR)/usr/bin/adava-display-manager \
	$(BUILD_DIR)/etc/pam.d/adava-greeter $(BUILD_DIR)/usr/share/adava/themes/default/theme.conf

$(BUILD_DIR)/usr/bin/adava-panel: adava-panel.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/usr/bin/adava-session: adava-session
	mkdir -p $(dir $@)
	install -m 0755 $< $@

$(BUILD_DIR)/usr/bin/adava-greeter: adava-greeter.c pam_compat.h
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ adava-greeter.c -l:libpam.so.0 -lX11

$(BUILD_DIR)/usr/bin/adava-display-manager: adava-display-manager
	mkdir -p $(dir $@)
	install -m 0755 $< $@

$(BUILD_DIR)/etc/pam.d/adava-greeter: pam.d/adava-greeter
	mkdir -p $(dir $@)
	install -m 0644 $< $@

$(BUILD_DIR)/usr/share/adava/themes/default/theme.conf: themes/default/theme.conf
	mkdir -p $(dir $@)
	install -m 0644 $< $@

package: all
	rm -rf $(BUILD_DIR)/package-root out
	mkdir -p $(BUILD_DIR)/package-root/adavalinux-desktop-$(VERSION)
	cp -a $(BUILD_DIR)/usr $(BUILD_DIR)/etc $(BUILD_DIR)/package-root/adavalinux-desktop-$(VERSION)/
	cp $(PACKAGE_DIR)/adavalinux-desktop/syspckg-info $(PACKAGE_DIR)/adavalinux-desktop/syspckg-deps $(PACKAGE_DIR)/adavalinux-desktop/install.sh $(BUILD_DIR)/package-root/adavalinux-desktop-$(VERSION)/
	mkdir -p $(BUILD_DIR)/package-root/adava-theme-default-$(VERSION)/usr/share/adava/themes/default
	cp themes/default/theme.conf $(BUILD_DIR)/package-root/adava-theme-default-$(VERSION)/usr/share/adava/themes/default/
	cp $(PACKAGE_DIR)/adava-theme-default/syspckg-info $(BUILD_DIR)/package-root/adava-theme-default-$(VERSION)/
	mkdir -p out
	tar -C $(BUILD_DIR)/package-root -cJf out/adavalinux-desktop-$(VERSION).syspckg adavalinux-desktop-$(VERSION)
	tar -C $(BUILD_DIR)/package-root -cJf out/adava-theme-default-$(VERSION).syspckg adava-theme-default-$(VERSION)

clean:
	rm -rf $(BUILD_DIR) out
