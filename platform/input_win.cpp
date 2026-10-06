/* input_win.cpp - Windows/EasyX 输入适配（platform 层，母代理维护）
 *
 * 接口版本: 2    配置版本: 1
 * 职责: 把鼠标摇杆与键盘转成 core 的 RawInput / BossInput；提供暂停、重开、退出事件。
 * 不实现任何游戏规则；不扣能量；不控制弹幕；不写世界。
 *
 * 输入契约（docs/demo-rules.md 1.2）:
 *   - 鼠标摇杆（florr/digdig 风格）: 指针在战场内的绝对位置交给核心，核心按"距指针越近越慢、抵达即停"处理。
 *   - 键盘 WASD/方向键等价八方向；1..4 出招；Esc/鼠标右键 暂停；R 重开；点击窗口关闭按钮 退出。
 *   - 出招只在"由松开变为按下"的边沿产生一次请求。
 *   - 失焦时清空全部按住与边沿，避免残留按键漏到新局。
 *   - 一次显示帧可能追赶多个逻辑 tick：take() 只在 first_of_frame 时返回请求边沿。
 */
#include "input_win.h"

#include <graphics.h>

namespace demo_platform {
namespace {

struct KeyState {
    bool down;
};

KeyState g_keys[256];
bool g_attack_edge[DEMO_PATTERN_COUNT];
bool g_pause_edge;
bool g_restart_edge;
bool g_quit;
bool g_focused = true;

float g_pointer_x;
float g_pointer_y;
bool g_pointer_inside;

DemoPattern vk_to_pattern(int vk) {
    switch (vk) {
        case '1': return DEMO_PATTERN_RING;
        case '2': return DEMO_PATTERN_COURSE;
        case '3': return DEMO_PATTERN_MINE;
        case '4': return DEMO_PATTERN_SHOWER;
        default: return DEMO_PATTERN_COUNT;
    }
}

bool key_down(int vk) {
    if (vk < 0 || vk > 255) {
        return false;
    }
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void update_axis_edges(int vk) {
    if (vk < 0 || vk > 255) {
        return;
    }
    bool now = key_down(vk);
    if (now && !g_keys[vk].down) {
        DemoPattern p = vk_to_pattern(vk);
        if (p != DEMO_PATTERN_COUNT) {
            g_attack_edge[p] = true;
        }
        if (vk == VK_ESCAPE) {
            g_pause_edge = true;
        }
        if (vk == 'R') {
            g_restart_edge = true;
        }
    }
    g_keys[vk].down = now;
}

}  // namespace

void input_poll(void) {
    // 处理 EasyX 消息: 用于检测鼠标位置、鼠标右键边沿、窗口关闭与失焦
    ExMessage msg;
    while (peekmessage(&msg, EX_MOUSE | EX_KEY | EX_WINDOW, true)) {
        if (msg.message == WM_CLOSE || msg.message == WM_DESTROY) {
            g_quit = true;
        }
        if (msg.message == WM_KILLFOCUS) {
            input_on_focus_lost();
        }
        if (msg.message == WM_SETFOCUS) {
            g_focused = true;
        }
        if (msg.message == WM_RBUTTONDOWN) {
            g_pause_edge = true;
        }
        if (msg.message == WM_KEYDOWN && msg.prevdown == 0) {
            DemoPattern p = vk_to_pattern(msg.vkcode);
            if (p != DEMO_PATTERN_COUNT) {
                g_attack_edge[p] = true;
            }
            if (msg.vkcode == VK_ESCAPE) {
                g_pause_edge = true;
            }
            if (msg.vkcode == 'R') {
                g_restart_edge = true;
            }
        }
    }

    // 键盘轴用同步状态查询: 保证按住持续移动、松开立即停止
    static const int keys[] = {'W', 'A', 'S', 'D', VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT};
    for (int vk : keys) {
        update_axis_edges(vk);
    }
    // 1..4 与 Esc/R 也做一次边沿比较, 覆盖 peekmessage 未覆盖的情况
    static const int edge_keys[] = {'1', '2', '3', '4', VK_ESCAPE, 'R'};
    for (int vk : edge_keys) {
        update_axis_edges(vk);
    }

    // 指针位置换算到战场坐标（战场位于窗口左上 960x720）
    HWND hwnd = GetHWnd();
    POINT pt;
    g_pointer_inside = false;
    if (hwnd != nullptr && GetCursorPos(&pt) && ScreenToClient(hwnd, &pt)) {
        if (pt.x >= 0 && pt.y >= 0 && pt.x < FIELD_PIXEL_W && pt.y < FIELD_PIXEL_H) {
            g_pointer_inside = true;
            g_pointer_x = (float)pt.x;
            g_pointer_y = (float)pt.y;
        }
    }
    if (key_down(VK_LBUTTON)) {
        // 左键用于把指针焦点带回游戏窗口, 不绑定招式(招式用 1..4)
        g_focused = true;
    }
}

bool input_take(BossInput *out, bool first_of_frame) {
    if (out == nullptr) {
        return false;
    }
    *out = BossInput{};

    float kx = 0.0f;
    float ky = 0.0f;
    if (g_keys['A'].down || g_keys[VK_LEFT].down) kx -= 1.0f;
    if (g_keys['D'].down || g_keys[VK_RIGHT].down) kx += 1.0f;
    if (g_keys['W'].down || g_keys[VK_UP].down) ky -= 1.0f;
    if (g_keys['S'].down || g_keys[VK_DOWN].down) ky += 1.0f;

    if (g_pointer_inside && g_focused) {
        out->pointer_valid = true;
        out->pointer_x = g_pointer_x;
        out->pointer_y = g_pointer_y;
        out->pointer_deadzone = POINTER_DEADZONE_PX;
        out->pointer_saturate = POINTER_SATURATE_PX;
    } else {
        out->move_x = kx;
        out->move_y = ky;
    }

    if (first_of_frame) {
        for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
            out->attack_requested[p] = g_attack_edge[p];
            g_attack_edge[p] = false;
        }
        out->pause_requested = g_pause_edge;
        out->restart_requested = g_restart_edge;
        out->quit_requested = g_quit;
        g_pause_edge = false;
        g_restart_edge = false;
    }
    return true;
}

bool input_quit_requested(void) { return g_quit; }

bool input_pause_edge_consume(void) {
    bool v = g_pause_edge;
    g_pause_edge = false;
    return v;
}

bool input_restart_edge_consume(void) {
    bool v = g_restart_edge;
    g_restart_edge = false;
    return v;
}

bool input_any_attack_edge(void) {
    for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
        if (g_attack_edge[p]) {
            return true;
        }
    }
    return false;
}

void input_clear_edges(void) {
    for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
        g_attack_edge[p] = false;
    }
    g_pause_edge = false;
    g_restart_edge = false;
}

void input_on_focus_lost(void) {
    g_focused = false;
    for (int i = 0; i < 256; ++i) {
        g_keys[i].down = false;
    }
    g_pointer_inside = false;
    input_clear_edges();
}

bool input_pointer_inside(void) { return g_pointer_inside; }

}  // namespace demo_platform
