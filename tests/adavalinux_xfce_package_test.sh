#!/bin/sh
set -eu

archive=../syspckg/packages/adavalinux-xfce-0.1.0.syspckg
polkit_archive=../syspckg/packages/polkit-127.syspckg
x11_utils_archive=../syspckg/packages/x11-xserver-utils-7.7.syspckg
adwaita_icons_archive=../syspckg/packages/adwaita-icon-theme-50.0.syspckg
xkb_data_archive=../syspckg/packages/xkb-data-2.46.syspckg
evdev_archive=../syspckg/packages/xf86-input-evdev-2.11.0.syspckg
librsvg_archive=../syspckg/packages/librsvg-2.61.3.syspckg
hicolor_archive=../syspckg/packages/hicolor-icon-theme-0.18.syspckg
gsettings_archive=../syspckg/packages/gsettings-desktop-schemas-50.0.syspckg
gtk3_runtime_archive=../syspckg/packages/gtk3-runtime-3.24.48.syspckg
gdk_pixbuf_archive=../syspckg/packages/gdk-pixbuf-2.42.12.syspckg
garcon_archive=../syspckg/packages/garcon-4.20.0.syspckg
shared_mime_archive=../syspckg/packages/shared-mime-info-2.4.syspckg

[ -f "$archive" ]
[ -f "$polkit_archive" ]
[ -f "$x11_utils_archive" ]
[ -f "$adwaita_icons_archive" ]
[ -f "$xkb_data_archive" ]
[ -f "$evdev_archive" ]
[ -f "$librsvg_archive" ]
[ -f "$hicolor_archive" ]
[ -f "$gsettings_archive" ]
[ -f "$gtk3_runtime_archive" ]
[ -f "$gdk_pixbuf_archive" ]
[ -f "$garcon_archive" ]
[ -f "$shared_mime_archive" ]
[ ! -e filesforlinux/rootfs/usr/share/syspckg/packages/gtk3-runtime-3.24.48.syspckg ]
entries=$(tar -tJf "$archive")
polkit_entries=$(tar -tJf "$polkit_archive")
x11_utils_entries=$(tar -tJf "$x11_utils_archive")
gtk3_runtime_entries=$(tar -tJf "$gtk3_runtime_archive")
gtk3_runtime_deps=$(tar -xOJf "$gtk3_runtime_archive" gtk3-runtime-3.24.48/syspckg-deps)

printf '%s\n' "$entries" | grep -Fqx 'adavalinux-xfce-0.1.0/usr/bin/adavalinux-xfce-start'
printf '%s\n' "$entries" | grep -Fqx 'adavalinux-xfce-0.1.0/etc/X11/xorg.conf'
printf '%s\n' "$entries" | grep -Fqx 'adavalinux-xfce-0.1.0/install.sh'
printf '%s\n' "$entries" | grep -Fqx 'adavalinux-xfce-0.1.0/syspckg-deps'

xfce_install_hook=$(tar -xOJf "$archive" adavalinux-xfce-0.1.0/install.sh)
printf '%s\n' "$xfce_install_hook" | grep -Fq 'wc -c < "$root/etc/machine-id"'
printf '%s\n' "$xfce_install_hook" | grep -Fqx 'ln -sfn /etc/machine-id "$root/var/lib/dbus/machine-id"'

machine_id_test=$(mktemp -d)
trap 'rm -rf "$machine_id_test"' EXIT HUP INT TERM
tar -xJf "$archive" -C "$machine_id_test"
mkdir -p "$machine_id_test/rootfs"
"$machine_id_test/adavalinux-xfce-0.1.0/install.sh" "$machine_id_test/rootfs"
test "$(wc -c < "$machine_id_test/rootfs/etc/machine-id")" -eq 33
grep -Exq '[0-9a-f]{32}' "$machine_id_test/rootfs/etc/machine-id"
test "$(readlink "$machine_id_test/rootfs/var/lib/dbus/machine-id")" = /etc/machine-id

# A previous package version wrote an extra blank line.  Reinstallation must
# repair that invalid but non-empty identity instead of leaving it in place.
printf '%s\n\n' '0123456789abcdef0123456789abcdef' > "$machine_id_test/rootfs/etc/machine-id"
"$machine_id_test/adavalinux-xfce-0.1.0/install.sh" "$machine_id_test/rootfs"
test "$(wc -c < "$machine_id_test/rootfs/etc/machine-id")" -eq 33
grep -Exq '[0-9a-f]{32}' "$machine_id_test/rootfs/etc/machine-id"

deps=$(tar -xOJf "$archive" adavalinux-xfce-0.1.0/syspckg-deps)
printf '%s\n' "$deps" | grep -Fqx 'DEP=xorg-server-21.1.12'
printf '%s\n' "$deps" | grep -Fqx 'DEP=font-dejavu-2.37'
printf '%s\n' "$deps" | grep -Fqx 'DEP=xfce4-session'
printf '%s\n' "$deps" | grep -Fqx 'DEP=xfwm4'
printf '%s\n' "$deps" | grep -Fqx 'DEP=xfdesktop'
printf '%s\n' "$deps" | grep -Fqx 'DEP=xfce4-panel'
printf '%s\n' "$deps" | grep -Fqx 'DEP=polkit-127'
printf '%s\n' "$deps" | grep -Fqx 'DEP=x11-xserver-utils-7.7'
printf '%s\n' "$deps" | grep -Fqx 'DEP=gtk3-runtime-3.24.48'
printf '%s\n' "$deps" | grep -Fqx 'DEP=adwaita-icon-theme-50.0'
printf '%s\n' "$deps" | grep -Fqx 'DEP=xkb-data-2.46'
printf '%s\n' "$deps" | grep -Fqx 'DEP=librsvg-2.61.3'
printf '%s\n' "$deps" | grep -Fqx 'DEP=hicolor-icon-theme-0.18'
printf '%s\n' "$deps" | grep -Fqx 'DEP=gsettings-desktop-schemas-50.0'
printf '%s\n' "$deps" | grep -Fqx 'DEP=shared-mime-info-2.4'

printf '%s\n' "$polkit_entries" | grep -Fqx \
  'polkit-127/usr/lib/x86_64-linux-gnu/libpolkit-gobject-1.so.0'
printf '%s\n' "$x11_utils_entries" | grep -Fqx \
  'x11-xserver-utils-7.7/usr/bin/xrdb'
tar -tJf "$adwaita_icons_archive" | grep -Fqx \
  'adwaita-icon-theme-50.0/usr/share/icons/Adwaita/index.theme'
tar -tJf "$adwaita_icons_archive" | grep -Fqx \
  'adwaita-icon-theme-50.0/usr/share/icons/Adwaita/16x16/status/image-missing.png'
tar -tJf "$adwaita_icons_archive" | grep -Fqx 'adwaita-icon-theme-50.0/install.sh'
adwaita_hook=$(tar -xOJf "$adwaita_icons_archive" adwaita-icon-theme-50.0/install.sh)
printf '%s\n' "$adwaita_hook" | grep -Fq 'scalable/status/image-missing.svg'
accessories_directory=$(tar -xOJf "$garcon_archive" \
  garcon-4.20.0/usr/share/desktop-directories/xfce-accessories.directory)
settings_directory=$(tar -xOJf "$garcon_archive" \
  garcon-4.20.0/usr/share/desktop-directories/xfce-settings.directory)
printf '%s\n' "$accessories_directory" | grep -Fqx 'Icon=applications-utilities-symbolic'
printf '%s\n' "$settings_directory" | grep -Fqx 'Icon=preferences-system-symbolic'
shared_mime_entries=$(tar -tJf "$shared_mime_archive")
printf '%s\n' "$shared_mime_entries" | grep -Fqx 'shared-mime-info-2.4/syspckg-info'
shared_mime_info=$(tar -xOJf "$shared_mime_archive" shared-mime-info-2.4/syspckg-info)
printf '%s\n' "$shared_mime_info" | grep -Fqx 'PKG_VERSION=2.4'
printf '%s\n' "$shared_mime_info" | grep -Fqx 'ID="adavalinux"'
printf '%s\n' "$shared_mime_info" | grep -Fqx 'VERSION="1.0"'
printf '%s\n' "$shared_mime_entries" | grep -Fqx \
  'shared-mime-info-2.4/usr/share/mime/globs2'
printf '%s\n' "$shared_mime_entries" | grep -Fqx \
  'shared-mime-info-2.4/usr/share/mime/mime.cache'
tar -tJf "$hicolor_archive" | grep -Fqx \
  'hicolor-icon-theme-0.18/usr/share/icons/hicolor/index.theme'
tar -tJf "$gsettings_archive" | grep -Fqx \
  'gsettings-desktop-schemas-50.0/usr/share/glib-2.0/schemas/org.gnome.desktop.interface.gschema.xml'
tar -tJf "$gsettings_archive" | grep -Fqx \
  'gsettings-desktop-schemas-50.0/usr/share/glib-2.0/schemas/org.gnome.desktop.enums.xml'
tar -tJf "$gsettings_archive" | grep -Fqx 'gsettings-desktop-schemas-50.0/install.sh'
gsettings_hook=$(tar -xOJf "$gsettings_archive" gsettings-desktop-schemas-50.0/install.sh)
printf '%s\n' "$gsettings_hook" | grep -Fq 'glib-compile-schemas'
tar -tJf "$xkb_data_archive" | grep -Fqx 'xkb-data-2.46/install.sh'
xkb_hook=$(tar -xOJf "$xkb_data_archive" xkb-data-2.46/install.sh)
printf '%s\n' "$xkb_hook" | grep -Fqx 'ln -sfn ../xkeyboard-config-2 "$root/usr/share/X11/xkb"'
tar -tJf "$evdev_archive" | grep -Fqx 'xf86-input-evdev-2.11.0/usr/share/X11/xorg.conf.d/10-evdev.conf'
tar -tJf "$evdev_archive" | grep -Fqx \
  'xf86-input-evdev-2.11.0/usr/lib/x86_64-linux-gnu/xorg/modules/input/evdev_drv.so'
if tar -tJf "$evdev_archive" | grep -Fqx \
  'xf86-input-evdev-2.11.0/usr/lib/xorg/modules/input/evdev_drv.so'; then
  echo 'evdev driver is installed outside Xorg ModulePath' >&2
  exit 1
fi
evdev_config=$(tar -xOJf "$evdev_archive" xf86-input-evdev-2.11.0/usr/share/X11/xorg.conf.d/10-evdev.conf)
printf '%s\n' "$evdev_config" | grep -Fqx '        MatchIsPointer "on"'
printf '%s\n' "$evdev_config" | grep -Fqx '        Driver "evdev"'
tar -tJf "$librsvg_archive" | grep -Fqx \
  'librsvg-2.61.3/usr/lib/x86_64-linux-gnu/librsvg-2.so.2.61.3'
tar -tJf "$librsvg_archive" | grep -Fqx \
  'librsvg-2.61.3/usr/lib/x86_64-linux-gnu/libxml2.so.16.1.2'
tar -tJf "$librsvg_archive" | grep -Fqx \
  'librsvg-2.61.3/usr/lib/x86_64-linux-gnu/libxml2.so.16'
tar -tJf "$librsvg_archive" | grep -Fqx \
  'librsvg-2.61.3/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader_svg.so'
tar -tJf "$librsvg_archive" | grep -Fqx 'librsvg-2.61.3/install.sh'
librsvg_deps=$(tar -xOJf "$librsvg_archive" librsvg-2.61.3/syspckg-deps)
printf '%s\n' "$librsvg_deps" | grep -Fqx 'DEP=gtk3-runtime-3.24.48'
printf '%s\n' "$librsvg_deps" | grep -Fqx 'DEP=gdk-pixbuf-2.42.12'
librsvg_hook=$(tar -xOJf "$librsvg_archive" librsvg-2.61.3/install.sh)
printf '%s\n' "$librsvg_hook" | grep -Fq 'libpixbufloader_svg.so'
gdk_svg_cache=$(tar -xOJf ../syspckg/packages/gdk-pixbuf-2.42.12.syspckg \
  gdk-pixbuf-2.42.12/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders.cache)
printf '%s\n' "$gdk_svg_cache" | grep -Fqx \
  '"/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader_svg.so"'
tar -tJf "$gdk_pixbuf_archive" | grep -Fqx 'gdk-pixbuf-2.42.12/install.sh'
gdk_pixbuf_hook=$(tar -xOJf "$gdk_pixbuf_archive" gdk-pixbuf-2.42.12/install.sh)
printf '%s\n' "$gdk_pixbuf_hook" | grep -Fq 'gdk-pixbuf-query-loaders'
printf '%s\n' "$gdk_pixbuf_hook" | grep -Fq 'loaders.cache'

# Installing librsvg after an older gdk-pixbuf cache must make SVG visible.
svg_hook_test=$(mktemp -d)
mkdir -p "$svg_hook_test/rootfs/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0"
tar -xOJf ../syspckg/packages/gdk-pixbuf-2.42.12.syspckg \
  gdk-pixbuf-2.42.12/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders.cache |
  sed '/libpixbufloader_svg.so/,+6d' > \
  "$svg_hook_test/rootfs/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders.cache"
tar -xJf "$librsvg_archive" -C "$svg_hook_test"
"$svg_hook_test/librsvg-2.61.3/install.sh" "$svg_hook_test/rootfs"
grep -Fqx \
  '"/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader_svg.so"' \
  "$svg_hook_test/rootfs/usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders.cache"
rm -rf "$svg_hook_test"

for library in \
  libwnck-3.so.0 \
  libgtk-3.so.0 \
  libgdk-3.so.0 \
  libepoxy.so.0 \
  libxkbcommon.so.0 \
  libwayland-client.so.0 \
  libwayland-cursor.so.0 \
  libwayland-egl.so.1 \
  libdisplay-info.so.3 \
  libgcc_s.so.1 \
  libXRes.so.1 \
  libXpresent.so.1; do
  printf '%s\n' "$gtk3_runtime_entries" | grep -Eq "/${library}$"
done

printf '%s\n' "$gtk3_runtime_entries" | grep -Fqx \
  'gtk3-runtime-3.24.48/usr/share/glib-2.0/schemas/org.gtk.Settings.FileChooser.gschema.xml'
printf '%s\n' "$gtk3_runtime_entries" | grep -Fqx 'gtk3-runtime-3.24.48/install.sh'
gtk3_hook=$(tar -xOJf "$gtk3_runtime_archive" gtk3-runtime-3.24.48/install.sh)
printf '%s\n' "$gtk3_hook" | grep -Fq 'glib-compile-schemas'

printf '%s\n' "$gtk3_runtime_deps" | grep -Fqx 'DEP=polkit-127'
printf '%s\n' "$gtk3_runtime_deps" | grep -Fqx 'DEP=dbus-1.14.10'

if printf '%s\n' "$gtk3_runtime_entries" | grep -Eq '/libdbus-1\.so\.3$'; then
  echo 'gtk3 runtime must not shadow the dbus package library' >&2
  exit 1
fi

printf '%s\n' 'adavalinux XFCE package test passed'
