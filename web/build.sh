#!/usr/bin/env bash
#
# Сборка сайта TileLayout — статические файлы для GitHub Pages или любого
# веб-сервера:
#
#   web/build.sh [папка результата]        # по умолчанию dist/web
#
# Нужны Emscripten (emcmake в PATH; ядро → WebAssembly) и обычный компилятор C
# (tl_catalog_index — список встроенных каталогов для сайта).
#
# Проверить локально: python3 -m http.server -d dist/web 8000 → http://localhost:8000
# (просто открыть index.html из папки нельзя: браузер не грузит WebAssembly с file://).

set -euo pipefail

source_dir=$(cd "$(dirname "$0")/.." && pwd)
output=${1:-dist/web}
build=${TL_WEB_BUILD_DIR:-build-web}

step() { echo "==> $*"; }

step "Ядро → WebAssembly"
emcmake cmake -S "$source_dir" -B "$build/wasm" -DCMAKE_BUILD_TYPE=Release -DTL_BUILD_TESTS=OFF
cmake --build "$build/wasm" --target tilelayout_web -j

step "Список каталогов"
cmake -S "$source_dir" -B "$build/native" -DCMAKE_BUILD_TYPE=Release -DTL_BUILD_APP=OFF -DTL_BUILD_TESTS=OFF
cmake --build "$build/native" --target tl_catalog_index -j

step "Сайт → $output"
rm -rf "$output"
mkdir -p "$output"
cp "$source_dir/web/index.html" "$source_dir/web/style.css" "$output/"
cp -r "$source_dir/web/js" "$output/"
cp "$build/wasm/tilelayout.js" "$build/wasm/tilelayout.wasm" "$output/"
cp "$source_dir/packaging/linux/tilelayout.svg" "$output/icon.svg"
cp -r "$source_dir/web/icons" "$source_dir/web/manifest.webmanifest" "$source_dir/web/sw.js" "$output/"
cp -r "$source_dir/data/catalogs" "$output/catalogs"
"$build/native/tl_catalog_index" "$source_dir/data/catalogs" "$output/catalogs.json"
step "Метка сборки в адресах файлов"
# GitHub Pages разрешает браузеру 10 минут держать файлы в кэше: без метки после
# обновления браузер мог взять новую страницу со старыми скриптами. Метка — коммит
# (или время сборки вне git): у каждой сборки свои адреса, всё грузится одной версией.
stamp=$(git -C "$source_dir" rev-parse --short HEAD 2>/dev/null || date +%s)
sed -i -e "s#href=\"style.css\"#href=\"style.css?v=$stamp\"#" \
       -e "s#src=\"tilelayout.js\"#src=\"tilelayout.js?v=$stamp\"#" \
       -e "s#src=\"js/main.js\"#src=\"js/main.js?v=$stamp\"#" "$output/index.html"
sed -i -E "s#from '(\./[a-z_]+\.js)'#from '\1?v=$stamp'#" "$output"/js/*.js
sed -i "s#tilelayout\.wasm#tilelayout.wasm?v=$stamp#g" "$output/tilelayout.js"
step "Офлайн-режим: список файлов для кэша"
# Сервис-воркер (web/sw.js) кладёт в кэш всё, что нужно сайту без интернета, — по адресам с меткой.
files=$(cd "$output" && {
    echo "./"
    echo "index.html"
    grep -oE '(href|src)="[^"#:]+"' index.html | sed -E 's/^(href|src)="//; s/"$//'
    for script in js/*.js; do echo "$script?v=$stamp"; done
    echo "tilelayout.wasm?v=$stamp"
    echo "catalogs.json"
    find icons catalogs -type f
} | sort -u)
STAMP=$stamp FILES=$files python3 - "$output/sw.js" <<'PY'
import json, os, sys
path = sys.argv[1]
text = open(path, encoding='utf-8').read()
files = [line for line in os.environ['FILES'].splitlines() if line.strip()]
text = text.replace("const BUILD = 'dev';", f"const BUILD = '{os.environ['STAMP']}';", 1)
text = text.replace('const FILES = [];', 'const FILES = ' + json.dumps(files, ensure_ascii=False) + ';', 1)
open(path, 'w', encoding='utf-8').write(text)
PY
touch "$output/.nojekyll" # GitHub Pages: отдавать файлы как есть
du -sh "$output"
