# TileLayout

Программа для раскладки плитки на C99 + GTK 3:
- план дома: стены, перегородки, проёмы;
- комнаты появляются из замкнутых областей;
- раскладка плитки, расчёт плитки, клея и СВП;
- печать и PDF с чертежами подрезок.

Файлы проектов — `*.tilelayout.json`. Открываются и файлы прежней Python-версии программы.

<p align="center">
  <a href="docs/screenshots/screenshot_main.png"><img src="docs/screenshots/screenshot_main.png" width="760" alt="Главное окно: план дома, раскладка плитки и расчёт"></a>
</p>

<table>
  <tr>
    <td align="center" width="33%"><a href="docs/screenshots/screenshot_conv_1.png"><img src="docs/screenshots/screenshot_conv_1.png" alt="Печать: план с номерами плиток"></a><br>План с номерами плиток</td>
    <td align="center" width="33%"><a href="docs/screenshots/screenshot_conv_2.png"><img src="docs/screenshots/screenshot_conv_2.png" alt="Печать: чертежи подрезок"></a><br>Чертежи подрезок</td>
    <td align="center" width="33%"><a href="docs/screenshots/screenshot_conv_3.png"><img src="docs/screenshots/screenshot_conv_3.png" alt="Печать: лист расчёта"></a><br>Лист расчёта</td>
  </tr>
</table>

## Сборка

### Linux (Debian, Ubuntu, Mint)

```sh
sudo apt install build-essential cmake pkg-config libgtk-3-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/tilelayout [проект.tilelayout.json]
```

### Windows (MSYS2, окружение «MINGW64»)

```sh
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-gtk3
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/tilelayout.exe
```

### Переносная сборка для Windows x64

Папка и zip, которые запускаются без установки и без MSYS2. Собираются там же, в оболочке MINGW64:

```sh
pacman -S zip mingw-w64-x86_64-librsvg
packaging/windows/make-portable.sh build 1.0
# результат: dist/TileLayout-1.0-win64/ и dist/TileLayout-1.0-win64.zip
```

Скрипт сам находит нужные DLL. Кроме них он кладёт в папку:
- загрузчики значков PNG и SVG;
- схемы настроек GTK;
- символьные значки Adwaita;
- лицензии библиотек.

В конце скрипт проверяет, что программа запускается без MSYS2 в `PATH`.

Переносной режим: если рядом с `tilelayout.exe` лежит `tilelayout.ini`, настройки хранятся в нём. В переносной сборке этот файл уже есть. Без него настройки хранятся в профиле пользователя.

### Только ядро и тесты

Если GTK не найден, CMake выдаёт предупреждение. Тогда собираются только ядро и спецификации. Кросс-сборка ядра из Linux под Windows:

```sh
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DTL_BUILD_APP=OFF
cmake --build build-win && wine64 build-win/tl_specs.exe
```

## Спецификации

```sh
cmake --build build && (cd build && ctest --output-on-failure)
# или подробно:
./build/tl_specs
```

Спецификации описывают поведение ядра. Например:
- «перегородка, замкнувшая область, создаёт комнату»;
- «плитка на стыке двух комнат с общим полом считается один раз».

Набор `spec_storage` открывает проект, сохранённый прежней Python-версией. Он проверяет, что количество плиток, клея и СВП совпадает с её расчётами.

## Релизы на GitHub

Сборку делает GitHub Actions: `.github/workflows/build.yml` в корне репозитория.
- **Каждый push в `main`**: сборка и спецификации на Linux и Windows. Переносной zip для Windows прикладывается к запуску (вкладка Actions → запуск → Artifacts).
- **Тег `vX.Y`**: то же самое, а zip публикуется в Releases.

Как выпустить версию:
1. Поднимите версию в `project(TileLayout VERSION X.Y ...)` в `CMakeLists.txt`.
2. Закоммитьте и отправьте:

   ```sh
   git tag vX.Y
   git push origin main vX.Y
   ```

Если тег не совпадает с версией в `CMakeLists.txt`, сборка остановится с ошибкой. Версию в окне «О программе» CMake тоже берёт из `CMakeLists.txt`.

## Командная строка

```
tilelayout [проект]                               обычный запуск
tilelayout --export-png план.png [проект] [--numbers]
tilelayout --export-pdf листы.pdf [проект] [--mono]
tilelayout --screenshot окно.png [проект]          снимок окна
```

## Устройство

```
src/core/   ядро без GUI: модель, геометрия, раскладка, расчёты, JSON, история
src/app/    интерфейс GTK: окно, канва, инструменты, панель, печать
tests/      спецификации ядра и проект прежней Python-версии для проверки совместимости
```

Ядро (`tl_*`) не зависит от GTK. Его можно проверить отдельно и собрать любым компилятором C99.

| Модуль | Назначение |
|---|---|
| `tl_geometry`, `tl_regions` | векторы, многоугольники, отсечение; разбиение плоскости на области (заменяет булевы операции) |
| `tl_outline`, `tl_partition`, `tl_opening`, `tl_band` | сущности плана: контур дома, перегородка, проём, полоса стены |
| `tl_space`, `tl_tile_settings` | комната и настройки плитки |
| `tl_project*` | проект: поиск комнат, связанные стены, узлы перегородок, транзакции правки |
| `tl_layout` | раскладка плитки: куски, подрезки, СВП, клей, раскладка от угла и по центру |
| `tl_report*` | сводка по видам плитки, сквозная нумерация, общие плитки и остатки, карточки подрезок, «Минимум плиток» |
| `tl_json`, `tl_project_io`, `tl_history` | файлы проекта и отмена правок |
| `app_document` | открытый проект: выделение, история, файл, подписчики |
| `app_canvas`, `app_tool_*` | канва и инструменты |
| `app_render`, `app_draw` | одна отрисовка для экрана, PNG, PDF и принтера |
| `app_print*`, `app_sheet` | листы A4, предпросмотр, лист расчёта |
| `app_panel`, `app_window`, `app_config` | панель свойств, главное окно, настройки |

Паттерны:
- **Наблюдатель** — канва, панель и окно подписаны на изменения `AppDocument` и не знают друг о друге.
- **Стратегия** — каждый инструмент канвы — таблица функций `AppTool`.
- **Снимок (Memento)** — `TlHistory` хранит снимки проекта в JSON для отмены и повтора.
- **Транзакция правки** — `tl_project_begin_edit / apply_edit / end_edit` сохраняют привязку перегородок и проёмов к стенам при изменении геометрии.

Правила кода:
- память выделяется через `tl_memory` (при нехватке памяти программа завершается);
- динамические массивы задаются через `TL_ARRAY_DEFINE`;
- у каждой сущности есть пара `*_new` / `*_free`;
- глобальное изменяемое состояние есть только в трёх местах:
  - настройки плитки по умолчанию для новых комнат;
  - режим ч/б печати в `app_draw`;
  - кэш иконок;
- JSON не зависит от локали.

Настройки хранятся в `tilelayout.ini`:
- Linux: `~/.config/TileLayout/`;
- Windows: `%LOCALAPPDATA%\TileLayout\`.
