cmake_minimum_required(VERSION 3.10)
project(TileLayout VERSION 1.0 LANGUAGES C)

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
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0)
    endif()
    if(GTK3_FOUND)
        file(GLOB TL_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/app/*.c)
        add_executable(tilelayout WIN32 ${TL_APP_SOURCES})
        target_link_libraries(tilelayout PRIVATE tlcore PkgConfig::GTK3)
        target_compile_definitions(tilelayout PRIVATE TL_VERSION="${PROJECT_VERSION}")
        install(TARGETS tilelayout RUNTIME DESTINATION bin)
    else()
        message(WARNING "GTK 3 не найден: собирается только ядро и спецификации")
    endif()
endif()
