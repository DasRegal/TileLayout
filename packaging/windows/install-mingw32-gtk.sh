#!/usr/bin/env bash
# GTK 3 для 32-битной сборки (окружение MINGW32 в MSYS2).
#
# MSYS2 сворачивает 32-битное окружение: GTK, Cairo, Pango и их зависимости убраны
# из каталога пакетов mingw32, хотя сами файлы пакетов ещё лежат в репозитории.
# Скрипт ставит последние их версии прямо из репозитория. Остальное (glib2,
# harfbuzz, freetype, компилятор…) берётся из каталога как обычно.
#
# Список — замыкание зависимостей gtk3 и librsvg по пакетам, которых нет в каталоге.
# Набор проверен: программа собирается, спецификации проходят, PDF печатается.
# Если файлы из репозитория удалят, сборка x86 упадёт на этом шаге.
set -euo pipefail

repository=https://repo.msys2.org/mingw/mingw32
packages=(
    adwaita-icon-theme-49.0-1
    adwaita-icon-theme-legacy-46.2-1
    atk-2.60.4-1
    cairo-1.18.4-4
    egl-headers-1.5.r284.3ae2b7c-1
    fontconfig-2.18.2-1
    fribidi-1.0.16-1
    gdk-pixbuf2-2.44.7-1
    gles-headers-3.2.r1065.7fc154c-1
    gtk-update-icon-cache-3.24.51-2
    gtk3-3.24.51-2
    hicolor-icon-theme-0.18-1
    json-glib-1.10.8-2
    libdatrie-0.2.14-1
    libepoxy-1.5.10-7
    librsvg-2.62.0-1
    libthai-0.1.30-1
    lzo2-2.10-3
    pango-1.57.1-1
    pixman-0.46.4-3
    shared-mime-info-2.4-4
)

urls=()
for package in "${packages[@]}"; do
    urls+=("$repository/mingw-w64-i686-$package-any.pkg.tar.zst")
done
pacman -U --noconfirm --needed "${urls[@]}"
