#!/usr/bin/env bash
#
# Файлы для разработки с GTK 3 — заголовки и описания для pkg-config — без
# установки в систему. Нужен там, где пакет libgtk-3-dev не ставится: например,
# когда библиотеки GTK взяты из более новой версии Ubuntu, чем сама система.
#
#   tools/fetch-gtk-dev.sh [папка]
#
# Скачивает libgtk-3-dev той же версии, что установленная библиотека libgtk-3-0
# (из архива Ubuntu), и распаковывает в ~/.cache/tilelayout/gtk3-dev. CMake
# находит эту папку сам — дальше сборка обычными командами из README.
# Удалить: rm -rf ~/.cache/tilelayout/gtk3-dev.

set -euo pipefail

target=${1:-${XDG_CACHE_HOME:-$HOME/.cache}/tilelayout/gtk3-dev}

if pkg-config --exists gtk+-3.0 2>/dev/null; then
    echo "GTK 3 для разработки уже установлен в системе — ничего делать не нужно."
    exit 0
fi

version=$(dpkg-query -W -f='${Version}' libgtk-3-0 2>/dev/null || true)
if [[ -z $version ]]; then
    echo "Не найдена установленная библиотека libgtk-3-0." >&2
    echo "Установите GTK 3 для разработки средствами системы (см. README)." >&2
    exit 1
fi

architecture=$(dpkg --print-architecture)
multiarch=$(gcc -print-multiarch 2>/dev/null || echo "x86_64-linux-gnu")
package="libgtk-3-dev_${version}_${architecture}.deb"
url="https://launchpad.net/ubuntu/+archive/primary/+files/$package"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "Скачиваю $package"
if ! curl -fsSL -o "$work/$package" "$url"; then
    echo "Не удалось скачать $url" >&2
    echo "Скрипт работает для GTK из репозиториев Ubuntu и Linux Mint. В других системах" >&2
    echo "установите пакет разработки GTK 3 средствами системы (см. README)." >&2
    exit 1
fi

rm -rf "$target"
mkdir -p "$target/root" "$target/lib" "$target/pc"
dpkg-deb -x "$work/$package" "$target/root"

# Ссылки на установленные библиотеки: в системе есть только libgtk-3.so.0.
for library in gtk gdk; do
    ln -s "/usr/lib/$multiarch/lib$library-3.so.0" "$target/lib/lib$library-3.so"
done

# Requires без модулей, которых нет в системе (wayland, epoxy… — программе не нужны).
available_requirements() {
    local line=$1 result="" module operator required words
    read -r -a words <<<"${line//,/ }"
    local i=0
    while ((i < ${#words[@]})); do
        module=${words[i]} operator="" required=""
        if [[ ${words[i + 1]:-} =~ ^(>=|<=|=|>|<)$ ]]; then
            operator=${words[i + 1]} required=${words[i + 2]}
            i=$((i + 3))
        else
            i=$((i + 1))
        fi
        if PKG_CONFIG_PATH="$target/pc" pkg-config --exists "$module"; then
            result+="${result:+, }$module${operator:+ $operator $required}"
        fi
    done
    echo "$result"
}

source_pc="$target/root/usr/lib/$multiarch/pkgconfig"
for name in gdk-3.0 gtk+-3.0; do # gdk — первым: gtk+ требует его
    {
        while IFS= read -r line; do
            case $line in
            prefix=*) echo "prefix=$target/root/usr" ;;
            libdir=*) echo "libdir=$target/lib" ;;
            Requires.private:*) ;;
            Requires:*) echo "Requires: $(available_requirements "${line#Requires:}")" ;;
            *) echo "$line" ;;
            esac
        done <"$source_pc/$name.pc"
    } >"$target/pc/$name.pc"
done

PKG_CONFIG_PATH="$target/pc" pkg-config --exists gtk+-3.0
echo "Готово: $target"
echo "Теперь собирайте обычными командами из README — CMake найдёт эту папку сам."
