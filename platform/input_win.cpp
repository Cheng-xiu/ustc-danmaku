/* input_win.cpp - S13 Windows 输入适配（platform 层, C++/EasyX）
 *
 * 接口版本: 2    配置版本: 1
 * 任务: S13。唯一交付源文件：platform/input_win.cpp（模块内部辅助头 platform/input_win.h）。
 *
 * 职责: 把鼠标摇杆与键盘入口转成 core 的 RawInput / BossInput；提供暂停、重开、退出事件。
 * 不实现任何游戏规则；不读/写世界；不扣能量；不控制弹幕；不写日志文件；不查询墙钟。
 * 只调用 EasyX/Win32 的输入与窗口相关 API。
 *
 * ---------------------------------------------------------------- 已实测的 EasyX 事实
 * 本文件的设计由 work/agents/S13/probe_easyx_window.cpp 与 probe2_messages.cpp 的实际运行
 * 结果决定（日志见 work/agents/S13/logs/）：
 *   1) ExMessage.prevdown 可靠区分"首次按下(=0)"与"长按自动重复(=1)" -> 出招边沿用它判定。
 *   2) EasyX 的 peekmessage 只转发 WM_ACTIVATE / WM_MOVE / WM_SIZE 这几个 EX_WINDOW 消息，
 *      **不**转发 WM_KILLFOCUS / WM_SETFOCUS / WM_CLOSE / WM_DESTROY。
 *      因此仅靠 peekmessage 无法满足"WM_KILLFOCUS 时清空按住状态"与"关闭窗口返回退出事件"。
 *   3) 用 SetWindowLongPtrW 子类化 EasyX 窗口、并对未处理消息 CallWindowProcW 转发，
 *      能收到 WM_KILLFOCUS/WM_CLOSE，且**不破坏** EasyX 自身的消息缓冲（键消息仍可见）。
 *   4) 子类化中吞掉 WM_CLOSE（返回 0）后窗口依然存活（IsWindow == 1），
 *      因此平台层可以先返回"退出事件"，把何时 closegraph 留给 game_main。
 *
 * ---------------------------------------------------------------- 输入契约（docs/demo-rules.md 1.2）
 *   - 鼠标摇杆（florr/digdig 风格）: 平台把指针屏幕坐标换算到战场坐标交给核心，
 *     核心按"距指针越近越慢、抵达即停"处理（core/world.c 的 boss_control）。
 *   - 键盘等价入口: WASD / 方向键八方向，斜向归一化（幅度 <= 1）。
 *   - 优先级: 指针在窗口内且在战场内、且与角色距离超出死区时用指针，否则用键盘轴。
 *   - 指针不在窗口内或窗口失焦: pointer_valid = false 且键盘轴清零（停止移动）。
 *   - 出招: 1/2/3/4（含小键盘等价键）在"由松开变按下"的边沿产生一次请求；
 *     长按只触发一次，松开再按可再次请求。鼠标左键不绑定技能（任务卡明确"不适用"）。
 *   - 暂停/继续: Esc 或鼠标右键（边沿）；重开: R（边沿）；退出: 关闭窗口。
 *   - 失焦（WM_KILLFOCUS）: 指针无效、键盘轴清零、清空全部按住与边沿。
 *   - 一次显示帧可能追赶多个逻辑 tick: input_take_boss_input(out, first_of_frame) 只在
 *     第一次调用给出边沿，后续补帧返回无请求。
 */
#include "input_win.h"

#include <easyx.h>

namespace demo_platform {
namespace {

/* 全局状态机实例。单线程（窗口消息与主循环同线程）。 */
InputState g_state;
InputLayout g_layout = input_default_layout();

void *g_hwnd = nullptr;   /* EasyX 图形窗口句柄, NULL 表示未绑定 */
WNDPROC g_prev_proc = nullptr;
bool g_subclassed = false;

/* ---------------------------------------------------------------- 指针查询 */

/* 读取指针在客户区内的位置。返回 false 表示窗口不可用。 */
bool query_pointer_client(long *out_x, long *out_y) {
    if (g_hwnd == nullptr) {
        return false;
    }
    HWND hwnd = static_cast<HWND>(g_hwnd);
    if (IsWindow(hwnd) == 0) {
        return false;
    }
    POINT pt;
    if (GetCursorPos(&pt) == 0) {
        return false;
    }
    if (ScreenToClient(hwnd, &pt) == 0) {
        return false;
    }
    if (out_x != nullptr) {
        *out_x = static_cast<long>(pt.x);
    }
    if (out_y != nullptr) {
        *out_y = static_cast<long>(pt.y);
    }
    return true;
}

/* 指针是否位于窗口客户区内。 */
bool pointer_in_client(long x, long y) {
    if (g_hwnd == nullptr) {
        return false;
    }
    HWND hwnd = static_cast<HWND>(g_hwnd);
    RECT rc;
    if (GetClientRect(hwnd, &rc) == 0) {
        return false;
    }
    return x >= rc.left && y >= rc.top && x < rc.right && y < rc.bottom;
}

/* ---------------------------------------------------------------- 窗口子类化 */

/* 只处理 EasyX 不转发的、且本模块必须知道的窗口消息，其余一律转发给 EasyX 原处理过程。
 * 实测 CallWindowProcW 转发不会破坏 EasyX 的输入消息缓冲。 */
LRESULT CALLBACK input_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_KILLFOCUS:
            /* 失焦: 指针无效、键盘轴清零、清空全部按住与边沿（验收 6） */
            input_state_set_focus(&g_state, false);
            break;
        case WM_SETFOCUS:
            input_state_set_focus(&g_state, true);
            break;
        case WM_ACTIVATE: {
            /* wparam 低字: WA_INACTIVE 表示窗口被取消激活 -> 视为失焦 */
            const UINT action = LOWORD(wparam);
            input_state_set_focus(&g_state, action != WA_INACTIVE);
            break;
        }
        case WM_CLOSE:
            /* 先返回退出事件，由 game_main 决定何时 closegraph；不在窗口过程里销毁窗口 */
            input_state_quit_event(&g_state);
            return 0;
        case WM_DESTROY:
            /* 兜底: 窗口确实被销毁时也要给出退出事件 */
            input_state_quit_event(&g_state);
            g_hwnd = nullptr;
            break;
        default:
            break;
    }
    if (g_prev_proc != nullptr) {
        return CallWindowProcW(g_prev_proc, hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void detach_window() {
    if (g_subclassed && g_hwnd != nullptr && g_prev_proc != nullptr) {
        HWND hwnd = static_cast<HWND>(g_hwnd);
        if (IsWindow(hwnd) != 0) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_prev_proc));
        }
    }
    g_subclassed = false;
    g_prev_proc = nullptr;
    g_hwnd = nullptr;
}

/* ---------------------------------------------------------------- 消息泵 */

void drain_messages() {
    ExMessage msg;
    while (peekmessage(&msg, EX_MOUSE | EX_KEY | EX_WINDOW, true)) {
        switch (msg.message) {
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                /* 指针位置每帧用 GetCursorPos 统一刷新，这里不重复记录。
                 * 左键/中键不绑定任何游戏动作（左键技能绑定不适用）。 */
                break;
            case WM_RBUTTONDOWN:
                input_state_right_button_event(&g_state, true);
                break;
            case WM_RBUTTONUP:
                input_state_right_button_event(&g_state, false);
                break;
            case WM_KEYDOWN:
                /* prevdown == true 表示这是长按自动重复，不是新的按下边沿 */
                (void)input_state_key_event(&g_state, static_cast<unsigned>(msg.vkcode), true,
                                            msg.prevdown == false);
                break;
            case WM_KEYUP:
                (void)input_state_key_event(&g_state, static_cast<unsigned>(msg.vkcode), false,
                                            false);
                break;
            case WM_ACTIVATE: {
                /* peekmessage 确实会转发 WM_ACTIVATE（实测）；与窗口钩子保持同一语义 */
                const UINT action = LOWORD(msg.wParam);
                input_state_set_focus(&g_state, action != WA_INACTIVE);
                break;
            }
            case WM_CLOSE:
            case WM_DESTROY:
                /* 一般已被窗口钩子拦下；这里兜底 */
                input_state_quit_event(&g_state);
                break;
            default:
                break;
        }
    }
}

/* ---------------------------------------------------------------- 每帧刷新 */

void refresh_pointer_and_focus() {
    if (g_hwnd == nullptr) {
        return; /* 无窗口（单元测试）: 保持测试注入的状态 */
    }
    HWND hwnd = static_cast<HWND>(g_hwnd);
    if (IsWindow(hwnd) == 0) {
        g_hwnd = nullptr;
        input_state_quit_event(&g_state);
        return;
    }

    /* 最小化/不可见时不接受输入 */
    if (IsIconic(hwnd) != 0 || IsWindowVisible(hwnd) == 0) {
        input_state_set_focus(&g_state, false);
        input_state_set_pointer(&g_state, 0.0f, 0.0f, false);
        return;
    }

    long x = 0;
    long y = 0;
    const bool have = query_pointer_client(&x, &y);
    const bool inside = have && pointer_in_client(x, y);
    input_state_set_pointer(&g_state, static_cast<float>(x), static_cast<float>(y), inside);
}

}  // namespace

/* ---------------------------------------------------------------- 公共接口 */

void input_init(void *hwnd) {
    input_state_init(&g_state);
    g_layout = input_default_layout();

    detach_window();
    if (hwnd == nullptr) {
        return; /* 无窗口: 仅状态机可用（单元测试） */
    }

    HWND handle = static_cast<HWND>(hwnd);
    if (IsWindow(handle) == 0) {
        return;
    }
    g_hwnd = hwnd;
    /* 安装窗口钩子以截获 EasyX 不转发的 WM_KILLFOCUS / WM_CLOSE（见文件头实测结论） */
    SetLastError(0);
    LONG_PTR prev = SetWindowLongPtrW(handle, GWLP_WNDPROC,
                                      reinterpret_cast<LONG_PTR>(&input_window_proc));
    if (prev != 0) {
        g_prev_proc = reinterpret_cast<WNDPROC>(prev);
        g_subclassed = true;
    } else {
        /* 子类化失败: 仍可用, 但失焦/关闭只能依赖 WM_ACTIVATE 与 game_main 自己判定 */
        g_subclassed = false;
        g_prev_proc = nullptr;
    }
}

void input_shutdown() {
    detach_window();
    g_state = InputState();
}

void input_set_layout(const InputLayout *layout) {
    g_layout = (layout != nullptr) ? *layout : input_default_layout();
}

const InputLayout *input_get_layout() { return &g_layout; }

void input_poll(RawInput *raw) {
    refresh_pointer_and_focus();
    drain_messages();
    /* 开始新一帧的边沿收集: 本帧第一次 input_take_boss_input 可以消费出招边沿 */
    g_state.attack_edges_pending = true;
    input_state_fill_raw(&g_state, &g_layout, raw);
}

void input_poll() { input_poll(nullptr); }

bool input_take_boss_input(BossInput *out, bool first_of_frame) {
    return input_state_build_boss_input(&g_state, &g_layout, first_of_frame, out);
}

bool input_pause_toggled() { return input_state_take_pause(&g_state); }

bool input_restart_requested() { return input_state_take_restart(&g_state); }

bool input_quit_requested() { return input_state_take_quit(&g_state); }

void input_on_focus_lost() { input_state_set_focus(&g_state, false); }

void input_clear_pending() { input_state_clear_edges(&g_state); }

/* ---------------------------------------------------------------- 测试注入 */

void input_test_key(unsigned vk, bool down, bool first_press) {
    (void)input_state_key_event(&g_state, vk, down, first_press);
}

void input_test_right_button(bool down) { input_state_right_button_event(&g_state, down); }

void input_test_pointer(float client_x, float client_y, bool inside) {
    input_state_set_pointer(&g_state, client_x, client_y, inside);
}

void input_test_focus(bool focused) { input_state_set_focus(&g_state, focused); }

void input_test_quit_event() { input_state_quit_event(&g_state); }

const InputState *input_test_state() { return &g_state; }

/* ------------------------------------------------- 兼容旧调用形状（repo 初始提交） */

bool input_take(BossInput *out, bool first_of_frame) {
    return input_take_boss_input(out, first_of_frame);
}

bool input_pause_edge_consume() { return input_pause_toggled(); }

bool input_restart_edge_consume() { return input_restart_requested(); }

bool input_any_attack_edge() {
    for (int p = 0; p < static_cast<int>(DEMO_PATTERN_COUNT); ++p) {
        if (g_state.attack_edge[p]) {
            return true;
        }
    }
    return false;
}

void input_clear_edges() { input_clear_pending(); }

bool input_pointer_inside() { return g_state.pointer_inside && g_state.focus; }

}  // namespace demo_platform
