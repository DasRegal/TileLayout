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
cp -r "$source_dir/data/catalogs" "$output/catalogs"
"$build/native/tl_catalog_index" "$source_dir/data/catalogs" "$output/catalogs.json"
touch "$output/.nojekyll" # GitHub Pages: отдавать файлы как есть
du -sh "$output"
