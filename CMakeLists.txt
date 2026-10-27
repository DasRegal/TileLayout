cmake_minimum_required(VERSION 3.10)
project(TileLayout VERSION 1.10 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(TL_BUILD_TESTS "Собирать спецификации ядра" ON)
option(TL_BUILD_APP "Собирать приложение (нужен GTK 3)" ON)

if(MINGW)
    # printf с поддержкой C99 (%zu, %lld) вместо msvcrt
    add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)
endif()

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
endif()

# ---- ядро: модель, геометрия, раскладка, расчёты (без GUI) ----------------
file(GLOB TL_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/core/*.c)
add_library(tlcore STATIC ${TL_CORE_SOURCES})
target_include_directories(tlcore PUBLIC ${CMAKE_SOURCE_DIR}/src/core)
if(UNIX)
    target_link_libraries(tlcore PUBLIC m)
endif()

# ---- ядро для веб-версии: проект, сцена и расчёт строками JSON -------------
set(TL_WEB_SOURCES src/web/tl_web.c src/web/tl_web_edit.c src/web/tl_web_print.c src/web/tl_web_scene.c
                    src/web/tl_web_summary.c)
add_library(tlweb STATIC ${TL_WEB_SOURCES})
target_compile_definitions(tlweb PRIVATE TL_VERSION="${PROJECT_VERSION}")
target_include_directories(tlweb PUBLIC ${CMAKE_SOURCE_DIR}/src/web)
target_link_libraries(tlweb PUBLIC tlcore)

# ---- спецификации ---------------------------------------------------------
if(TL_BUILD_TESTS)
    enable_testing()
    file(GLOB TL_SPEC_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.c)
    add_executable(tl_specs ${TL_SPEC_SOURCES})
    target_link_libraries(tl_specs PRIVATE tlweb)
    target_compile_definitions(tl_specs PRIVATE TL_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    if(EMSCRIPTEN) # под node: файлы примеров — прямо с диска
        target_link_options(tl_specs PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
    endif()
    add_test(NAME specifications COMMAND tl_specs)
endif()

# ---- веб-версия: emcmake cmake … && cmake --build … -------------------------
if(EMSCRIPTEN)
    set(TL_BUILD_APP OFF)
    add_executable(tilelayout_web ${TL_WEB_SOURCES})
    set_target_properties(tilelayout_web PROPERTIES OUTPUT_NAME tilelayout SUFFIX .js)
    target_include_directories(tilelayout_web PRIVATE ${CMAKE_SOURCE_DIR}/src/web)
    target_link_libraries(tilelayout_web PRIVATE tlcore)
    target_compile_definitions(tilelayout_web PRIVATE TL_VERSION="${PROJECT_VERSION}")
    target_link_options(tilelayout_web PRIVATE --no-entry -sMODULARIZE=1 -sEXPORT_NAME=createTileLayout
                        -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0
                        "-sEXPORTED_RUNTIME_METHODS=[\"UTF8ToString\",\"stringToUTF8\",\"lengthBytesUTF8\"]"
                        "-sEXPORTED_FUNCTIONS=[\"_malloc\",\"_free\",\"_tl_web_new_demo\",\"_tl_web_open\",\"_tl_web_error\",\"_tl_web_version\",\"_tl_web_save\",\"_tl_web_scene\",\"_tl_web_space_at\",\"_tl_web_space_tile\",\"_tl_web_set_tile_field\",\"_tl_web_set_tile_fields\",\"_tl_web_set_rooms_tile_fields\",\"_tl_web_swap_tiles\",\"_tl_web_summary\",\"_tl_web_tile_numbers\",\"_tl_web_cut_cards\",\"_tl_web_align_tiles\",\"_tl_web_set_space_name\",\"_tl_web_snap\",\"_tl_web_add_contour\",\"_tl_web_add_partition\",\"_tl_web_opening_preview\",\"_tl_web_add_opening\",\"_tl_web_pick\",\"_tl_web_object\",\"_tl_web_set_partition_thickness\",\"_tl_web_set_partition_length\",\"_tl_web_set_partition_angle\",\"_tl_web_set_opening_width\",\"_tl_web_delete\",\"_tl_web_set_wall_thickness\",\"_tl_web_drag_begin\",\"_tl_web_drag_move\",\"_tl_web_drag_end\",\"_tl_web_set_space_source\",\"_tl_web_set_point\",\"_tl_web_set_wall_length\",\"_tl_web_place_opening\",\"_tl_web_set_threshold\",\"_tl_web_set_locked\",\"_tl_web_insert_vertex\",\"_tl_web_add_house\",\"_tl_web_add_ruler\",\"_tl_web_add_joint\",\"_tl_web_set_joint_gap\",\"_tl_web_add_pipe\",\"_tl_web_set_pipe\",\"_tl_web_set_name\",\"_tl_web_measure_point\",\"_tl_web_undo\",\"_tl_web_redo\",\"_tl_web_undo_count\",\"_tl_web_redo_count\"]")
elseif(UNIX)
    # список каталогов для сайта (запускается при сборке сайта, см. web/build.sh)
    add_executable(tl_catalog_index src/web/tl_catalog_index.c)
    target_link_libraries(tl_catalog_index PRIVATE tlcore)
endif()

# ---- приложение (GTK 3) ---------------------------------------------------
if(TL_BUILD_APP)
    find_package(PkgConfig)
    # Файлы для разработки GTK без установки в систему (tools/fetch-gtk-dev.sh) —
    # используются, только если GTK для разработки не установлен в системе.
    if(DEFINED ENV{XDG_CACHE_HOME})
        set(TL_DEFAULT_GTK_DEV_DIR "$ENV{XDG_CACHE_HOME}/tilelayout/gtk3-dev")
    else()
        set(TL_DEFAULT_GTK_DEV_DIR "$ENV{HOME}/.cache/tilelayout/gtk3-dev")
    endif()
    set(TL_GTK_DEV_DIR "${TL_DEFAULT_GTK_DEV_DIR}" CACHE PATH "Папка от tools/fetch-gtk-dev.sh")
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
        if(GTK3_FOUND)
            message(STATUS "GTK 3: ${GTK3_VERSION}")
        elseif(EXISTS "${TL_GTK_DEV_DIR}/pc/gtk+-3.0.pc")
            set(ENV{PKG_CONFIG_PATH} "${TL_GTK_DEV_DIR}/pc:$ENV{PKG_CONFIG_PATH}")
            pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
            if(GTK3_FOUND)
                message(STATUS "GTK 3: ${GTK3_VERSION}, файлы для разработки из ${TL_GTK_DEV_DIR}")
            endif()
        endif()
    endif()
    if(GTK3_FOUND)
        file(GLOB TL_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/app/*.c)
        if(WIN32)
            enable_language(RC)
            list(APPEND TL_APP_SOURCES ${CMAKE_SOURCE_DIR}/packaging/windows/tilelayout.rc)
        endif()
        add_executable(tilelayout WIN32 ${TL_APP_SOURCES})
        target_link_libraries(tilelayout PRIVATE tlcore PkgConfig::GTK3)
        target_compile_definitions(tilelayout PRIVATE TL_VERSION="${PROJECT_VERSION}"
                                   TL_SOURCE_CATALOGS="${CMAKE_SOURCE_DIR}/data/catalogs") # запуск из папки сборки
        install(TARGETS tilelayout RUNTIME DESTINATION bin)
    else()
        message(WARNING "GTK 3 для разработки не найден: собираются только ядро и спецификации.\n"
                        "Установите пакет разработки GTK 3 (в Ubuntu и Mint — libgtk-3-dev, см. README). "
                        "Если он не ставится, запустите tools/fetch-gtk-dev.sh и повторите cmake.")
    endif()
endif()

# ---- пакет .deb: cpack -G DEB в папке сборки -------------------------------
if(TARGET tilelayout AND UNIX AND NOT APPLE)
    install(FILES packaging/linux/tilelayout.desktop DESTINATION share/applications)
    install(FILES packaging/linux/tilelayout.svg DESTINATION share/icons/hicolor/scalable/apps)
    install(DIRECTORY data/catalogs/ DESTINATION share/tilelayout/catalogs)
    install(FILES CHANGELOG.md README.md packaging/linux/copyright DESTINATION share/doc/tilelayout)

    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGE_NAME tilelayout)
    set(CPACK_PACKAGE_VENDOR DasRegal)
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "раскладка плитки: план дома, расчёт плитки, клея и СВП")
    set(CPACK_PACKAGE_DESCRIPTION "Редактор плана: стены, перегородки любой формы, проёмы, деформационные швы.\n\
Раскладка по комнатам, чертежи подрезок, печать и PDF на листах A4.")
    set(CPACK_STRIP_FILES ON)
    set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
                                                    WORLD_READ WORLD_EXECUTE)
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "DasRegal <DasRegal@users.noreply.github.com>")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE https://github.com/DasRegal/TileLayout)
    set(CPACK_DEBIAN_PACKAGE_SECTION graphics)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) # зависимости — по библиотекам, с которыми собрана программа
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT) # tilelayout_<версия>_<архитектура>.deb
    include(CPack)
endif()
