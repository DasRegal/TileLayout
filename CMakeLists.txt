cmake_minimum_required(VERSION 3.10)
project(TileLayout VERSION 1.5 LANGUAGES C)

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

# ---- спецификации ---------------------------------------------------------
if(TL_BUILD_TESTS)
    enable_testing()
    file(GLOB TL_SPEC_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.c)
    add_executable(tl_specs ${TL_SPEC_SOURCES})
    target_link_libraries(tl_specs PRIVATE tlcore)
    target_compile_definitions(tl_specs PRIVATE TL_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    add_test(NAME specifications COMMAND tl_specs)
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
        target_compile_definitions(tilelayout PRIVATE TL_VERSION="${PROJECT_VERSION}")
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
