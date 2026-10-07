# 历史 Windows/EasyX 入口；依赖仅传给此目标，纯 C 核心不依赖窗口库。
find_path(EASYX_INCLUDE_DIR easyx.h
    HINTS "${EASYX_ROOT}" "${EASYX_ROOT}/include"
    PATHS "$ENV{EASYX_ROOT}" NO_DEFAULT_PATH)
find_path(EASYX_INCLUDE_DIR easyx.h)
if(EASYX_INCLUDE_DIR)
    add_executable(ustc_danmaku
        game_main.cpp platform/input_win.cpp render/scene.cpp render/hud.cpp)
    target_link_libraries(ustc_danmaku PRIVATE demo_core)
    target_compile_definitions(ustc_danmaku PRIVATE UNICODE _UNICODE)
    target_include_directories(ustc_danmaku PRIVATE "${EASYX_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform" "${CMAKE_CURRENT_SOURCE_DIR}/render")
    if(MSVC)
        find_library(EASYX_LIB NAMES EasyXw EasyXa
            HINTS "${EASYX_ROOT}" "${EASYX_ROOT}/lib/VC2015/x64"
                "${EASYX_ROOT}/lib/VC2015/x86" "${EASYX_ROOT}/lib_vc2015/x64"
            PATHS "$ENV{EASYX_ROOT}" NO_DEFAULT_PATH)
        if(NOT EASYX_LIB)
            find_library(EASYX_LIB NAMES EasyXw EasyXa)
        endif()
        if(EASYX_LIB)
            target_link_libraries(ustc_danmaku PRIVATE "${EASYX_LIB}")
        else()
            message(WARNING "找到 easyx.h，但未找到 EasyXa.lib/EasyXw.lib")
        endif()
        set_target_properties(ustc_danmaku PROPERTIES WIN32_EXECUTABLE OFF)
    else()
        message(WARNING "EasyX 仅支持 MSVC，当前图形游戏可能无法链接")
    endif()
else()
    message(STATUS "未找到 easyx.h，跳过图形游戏；core/sim/tests 仍可构建")
endif()
