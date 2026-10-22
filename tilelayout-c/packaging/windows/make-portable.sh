#!/usr/bin/env bash
#
# Переносная сборка TileLayout для Windows x64 — папка и zip, которые
# запускаются без установки. Запускать в оболочке MSYS2 MINGW64 после сборки:
#
#   packaging/windows/make-portable.sh <папка сборки> <версия> [папка результата]
#
# В папку попадает только нужное программе:
#   tilelayout.exe и DLL, от которых он зависит (из $MINGW_PREFIX/bin);
#   загрузчики картинок PNG и SVG (значки GTK);
#   схемы настроек GTK (без них падают диалоги выбора цвета);
#   символьные значки Adwaita (без курсоров — у Windows свои);
#   лицензии библиотек;
#   пустой tilelayout.ini — настройки хранятся рядом с программой.

set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Использование: $0 <папка сборки> <версия> [папка результата]" >&2
    exit 2
fi

build_dir=$1
version=$2
output_root=${3:-dist}
prefix=${MINGW_PREFIX:?Запустите в оболочке MSYS2 MINGW64}
package_prefix=${MINGW_PACKAGE_PREFIX:-mingw-w64-x86_64}
source_dir=$(cd "$(dirname "$0")/../.." && pwd)

package_name="TileLayout-$version-win64"
package_dir="$output_root/$package_name"
pixbuf_version_dir="lib/gdk-pixbuf-2.0/2.10.0"

step() { echo "==> $*"; }

# DLL из $prefix/bin, от которых зависят файлы, — рекурсивно (ldd показывает всё дерево).
copy_dependencies() {
    ldd "$@" | awk '{ print $3 }' | { grep -i "^$prefix/bin/" || true; } | sort -u | while read -r library; do
        cp -n "$library" "$package_dir/"
    done
}

copy_program() {
    step "Программа"
    cp "$build_dir/tilelayout.exe" "$package_dir/"
    strip "$package_dir/tilelayout.exe"
}

copy_pixbuf_loaders() {
    step "Загрузчики картинок"
    local loaders="$package_dir/$pixbuf_version_dir/loaders"
    mkdir -p "$loaders"
    find "$prefix/$pixbuf_version_dir/loaders" -name '*.dll' \( -name '*png*' -o -name '*svg*' \) \
        -exec cp {} "$loaders/" \;
    copy_dependencies "$loaders"/*.dll
    # Пути в кэше — относительно папки программы, как в самом MSYS2.
    gdk-pixbuf-query-loaders "$loaders"/*.dll |
        sed -E 's#^"[^"]*(lib[\\/]+gdk-pixbuf-2\.0)#"\1#' >"$package_dir/$pixbuf_version_dir/loaders.cache"
}

copy_settings_schemas() {
    step "Схемы настроек GTK"
    local schemas="$package_dir/share/glib-2.0/schemas"
    mkdir -p "$schemas"
    cp "$prefix"/share/glib-2.0/schemas/org.gtk.Settings.*.gschema.xml "$schemas/"
    glib-compile-schemas "$schemas"
    rm "$schemas"/*.gschema.xml
}

copy_icons() {
    step "Значки"
    local icons="$package_dir/share/icons"
    mkdir -p "$icons/Adwaita" "$icons/hicolor"
    cp "$prefix/share/icons/Adwaita/index.theme" "$icons/Adwaita/"
    for part in symbolic scalable 16x16; do
        if [[ -d "$prefix/share/icons/Adwaita/$part" ]]; then
            cp -r "$prefix/share/icons/Adwaita/$part" "$icons/Adwaita/"
        fi
    done
    cp "$prefix/share/icons/hicolor/index.theme" "$icons/hicolor/"
    gtk-update-icon-cache -q -f -t "$icons/Adwaita"
    gtk-update-icon-cache -q -f -t "$icons/hicolor"
}

copy_fonts_config() {
    step "Настройки шрифтов"
    mkdir -p "$package_dir/etc"
    cp -r "$prefix/etc/fonts" "$package_dir/etc/"
}

# Лицензии пакетов MSYS2, которым принадлежат скопированные DLL (GTK и др. — LGPL).
copy_licenses() {
    step "Лицензии"
    local licenses="$package_dir/licenses"
    mkdir -p "$licenses"
    find "$package_dir" -name '*.dll' -print0 |
        xargs -0 -I{} basename {} |
        while read -r library; do pacman -Qqo "$prefix/bin/$library" 2>/dev/null || true; done |
        sort -u | while read -r package; do
            local short=${package#"$package_prefix"-}
            if [[ -d "$prefix/share/licenses/$short" ]]; then
                cp -r "$prefix/share/licenses/$short" "$licenses/"
            fi
        done
}

write_readme() {
    : >"$package_dir/tilelayout.ini"
    cat >"$package_dir/README.txt" <<EOF
TileLayout $version — раскладка плитки (переносная версия для Windows x64)

Запуск: tilelayout.exe. Установка не нужна, папку можно держать на флешке.

Настройки (размер окна, недавние файлы, параметры печати) хранятся в
tilelayout.ini рядом с программой. Если удалить этот файл, настройки будут
храниться в профиле пользователя: %LOCALAPPDATA%\\TileLayout.

Справка по управлению — клавиша F1 в программе.
Лицензии использованных библиотек — в папке licenses.
EOF
}

# Проверка: программа запускается только с DLL из папки (без MSYS2 в PATH).
smoke_test() {
    step "Проверка запуска"
    local pdf
    pdf=$(mktemp -u --suffix=.pdf)
    PATH=/usr/bin "$package_dir/tilelayout.exe" --export-pdf "$(cygpath -w "$pdf")" \
        "$(cygpath -w "$source_dir/tests/fixtures/python_demo.tilelayout.json")"
    test -s "$pdf"
    rm -f "$pdf"
}

make_zip() {
    step "Архив"
    (cd "$output_root" && rm -f "$package_name.zip" && zip -qr9 "$package_name.zip" "$package_name")
    echo "Готово: $output_root/$package_name.zip ($(du -h "$output_root/$package_name.zip" | cut -f1))"
}

rm -rf "$package_dir"
mkdir -p "$package_dir"
copy_program
copy_dependencies "$package_dir/tilelayout.exe"
copy_pixbuf_loaders
copy_settings_schemas
copy_icons
copy_fonts_config
copy_licenses
write_readme
smoke_test
make_zip
