/* input_win.h - platform 输入适配的内部声明
 * 接口版本: 2    配置版本: 1
 * 只被 game_main.cpp 与 platform/input_win.cpp 使用；不在 core 内引用。
 */
#ifndef DEMO_PLATFORM_INPUT_WIN_H
#define DEMO_PLATFORM_INPUT_WIN_H

#include <windows.h>

extern "C" {
#include "demo_base.h"
}

namespace demo_platform {

/* 战场在窗口内的像素尺寸（画面 1280x720，左侧战场 960x720） */
constexpr int FIELD_PIXEL_W = 960;
constexpr int FIELD_PIXEL_H = 720;
constexpr float POINTER_DEADZONE_PX = 12.0f;
constexpr float POINTER_SATURATE_PX = 60.0f;

/* 每个显示帧调用一次: 收集鼠标/键盘边沿与指针位置 */
void input_poll(void);

/* 每个逻辑 tick 调用一次。first_of_frame 为 true 时才返回出招/暂停/重开边沿。
 * 返回 false 表示没有可用输入（当前实现总是返回 true）。 */
bool input_take(BossInput *out, bool first_of_frame);

bool input_quit_requested(void);
bool input_pause_edge_consume(void);
bool input_restart_edge_consume(void);
bool input_any_attack_edge(void);
void input_clear_edges(void);
void input_on_focus_lost(void);
bool input_pointer_inside(void);

}  // namespace demo_platform

#endif /* DEMO_PLATFORM_INPUT_WIN_H */
