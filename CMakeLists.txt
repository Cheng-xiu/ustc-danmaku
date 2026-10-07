cmake_minimum_required(VERSION 3.16)
project(ustc_danmaku_demo C CXX)

# 最小可玩 Boss demo 构建入口（母代理维护）
#
# 目标：
#   demo_core        纯 C 核心静态库（core/*.c + ai/*.c），不含 EasyX
#   sim              headless 入口（sim/*.c），链接 demo_core
#   demo_tests       模块与集成测试，链接 demo_core
#   ustc_danmaku     图形游戏（game_main.cpp + platform + render），仅当找到 EasyX 时构建
#
# 用法：
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DEASYX_ROOT=<EasyX 目录>
#   cmake --build build --config Release
# 无 EasyX 时仍可构建 core/sim/tests（headless 验证）。

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(DEMO_BUILD_GAME "构建 EasyX 图形游戏" ON)
option(DEMO_BUILD_TESTS "构建测试" ON)
set(EASYX_ROOT "" CACHE PATH "EasyX 头文件与库所在目录（例如包含 easyx.h 与 EasyXa.lib 的目录）")

# 静默 MSVC 对某些 C 代码的弃用警告，但不放松错误
# 源文件全部是 UTF-8（含中文注释）。MSVC 默认按系统代码页(936)读取，
# 对多字节边界会报 C2001/C2059/C1004 而编译失败（S13 实测 core/patterns.c 失败，其余文件侥幸通过）。
# 这里统一加 /utf-8，不依赖每台机器的代码页，也不要求每个文件带 BOM。
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------- 纯 C 核心

set(DEMO_CORE_SOURCES
    core/demo_config.c
    core/field_config.c
    core/rng.c
    core/collision.c
    core/actors.c
    core/projectiles.c
    core/attack.c
    core/patterns.c
    core/pattern_aim.c
    core/pattern_ring.c
    core/pattern_course.c
    core/pattern_mine.c
    core/pattern_shower.c
    core/student_fire.c
    core/world.c
    ai/student_bot.c
)

add_library(demo_core STATIC ${DEMO_CORE_SOURCES})
target_include_directories(demo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core
                                            ${CMAKE_CURRENT_SOURCE_DIR}/ai)
target_compile_definitions(demo_core PUBLIC DEMO_CORE_BUILD=1)
if(NOT MSVC)
    target_link_libraries(demo_core PUBLIC m)
endif()

# 核心必须是 C：显式设置语言，防止 CMake 误判为 C++
set_source_files_properties(${DEMO_CORE_SOURCES} PROPERTIES LANGUAGE C)

# ---------------------------------------------------------------- headless sim

add_executable(sim sim/main.c sim/log.c)
set_source_files_properties(sim/main.c sim/log.c
                            PROPERTIES LANGUAGE C)
target_link_libraries(sim PRIVATE demo_core)
target_include_directories(sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sim)

# ---------------------------------------------------------------- 测试

if(DEMO_BUILD_TESTS)
    enable_testing()
    # 每个 tests/test_*.c 自带 main(), 因此一个测试文件一个可执行目标。
    # 不要把它们 glob 进同一个可执行文件(会出现 multiple definition of 'main')，
    # 也不要引入 tests/test_main.c。本约定与 scripts/build.ps1 的 MinGW 路径一致。
    file(GLOB DEMO_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.c)
    foreach(TEST_SOURCE ${DEMO_TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
        add_executable(${TEST_NAME} ${TEST_SOURCE})
        set_source_files_properties(${TEST_SOURCE} PROPERTIES LANGUAGE C)
        if(TEST_NAME STREQUAL "test_attack")
            # 历史窄单元测试自带 pattern/event 桩，只链接所需真实模块。
            target_sources(${TEST_NAME} PRIVATE core/attack.c core/pattern_aim.c core/demo_config.c core/rng.c core/projectiles.c)
            target_include_directories(${TEST_NAME} PRIVATE core ai)
            if(NOT MSVC)
                target_link_libraries(${TEST_NAME} PRIVATE m)
            endif()
        else()
            target_link_libraries(${TEST_NAME} PRIVATE demo_core)
        endif()
        target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()
    add_executable(test_web_core tests/web/test_core_regression.c)
    target_link_libraries(test_web_core PRIVATE demo_core)
    add_test(NAME test_web_core COMMAND test_web_core)
    add_executable(test_endless tests/web/test_endless.c)
    target_link_libraries(test_endless PRIVATE demo_core)
    add_test(NAME test_endless COMMAND test_endless)
    add_executable(test_field_size tests/web/test_field_size.c)
    target_link_libraries(test_field_size PRIVATE demo_core)
    add_test(NAME test_field_size COMMAND test_field_size)
endif()

add_executable(web_native_replay tests/web/native_replay.c wasm/demo_bridge.c)
target_link_libraries(web_native_replay PRIVATE demo_core)
target_include_directories(web_native_replay PRIVATE wasm)

add_executable(endless_native_replay tests/web/endless_replay.c wasm/demo_bridge.c)
target_link_libraries(endless_native_replay PRIVATE demo_core)
target_include_directories(endless_native_replay PRIVATE wasm)
target_compile_definitions(endless_native_replay PRIVATE ENDLESS_SNAPSHOT_TRACE=1)

add_executable(web_bridge_probe wasm/bridge_probe.c wasm/demo_bridge.c)
target_link_libraries(web_bridge_probe PRIVATE demo_core)
target_include_directories(web_bridge_probe PRIVATE wasm)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/web/balance_probe.c")
    add_executable(web_balance_probe tests/web/balance_probe.c)
    target_link_libraries(web_balance_probe PRIVATE demo_core)
endif()

# ---------------------------------------------------------------- EasyX 图形游戏

find_path(EASYX_INCLUDE_DIR easyx.h
          HINTS ${EASYX_ROOT} ${EASYX_ROOT}/include
          PATHS "$ENV{EASYX_ROOT}"
          NO_DEFAULT_PATH)
find_path(EASYX_INCLUDE_DIR easyx.h)

if(DEMO_BUILD_GAME AND EASYX_INCLUDE_DIR)
    add_executable(ustc_danmaku
        game_main.cpp
        platform/input_win.cpp
        render/scene.cpp
        render/hud.cpp)
    target_link_libraries(ustc_danmaku PRIVATE demo_core)
    target_include_directories(ustc_danmaku PRIVATE ${EASYX_INCLUDE_DIR}
                                                    ${CMAKE_CURRENT_SOURCE_DIR}/platform
                                                    ${CMAKE_CURRENT_SOURCE_DIR}/render)
    if(MSVC)
        # EasyX 官方分发同时提供 EasyXa.lib(ANSI) 与 EasyXw.lib(Unicode)
        find_library(EASYX_LIB NAMES EasyXa EasyXw
                     HINTS ${EASYX_ROOT} ${EASYX_ROOT}/lib/VC2015/x64
                           ${EASYX_ROOT}/lib/VC2015/x86
                     PATHS "$ENV{EASYX_ROOT}" NO_DEFAULT_PATH)
        if(NOT EASYX_LIB)
            find_library(EASYX_LIB NAMES EasyXa EasyXw)
        endif()
        if(EASYX_LIB)
            target_link_libraries(ustc_danmaku PRIVATE ${EASYX_LIB})
        else()
            message(WARNING "找到 easyx.h 但未找到 EasyXa.lib/EasyXw.lib，图形游戏链接可能失败")
        endif()
        set_target_properties(ustc_danmaku PROPERTIES WIN32_EXECUTABLE OFF)
    else()
        message(WARNING "EasyX 仅支持 MSVC，当前编译器为 ${CMAKE_CXX_COMPILER_ID}，图形游戏可能无法链接")
    endif()
elseif(DEMO_BUILD_GAME)
    message(STATUS "未找到 easyx.h，跳过图形游戏目标（headless core/sim/tests 仍可构建）")
endif()
