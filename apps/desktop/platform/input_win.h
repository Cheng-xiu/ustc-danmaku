/* input_win.h - S13 Windows 输入适配（platform 层）的内部辅助头
 *
 * 接口版本: 2    配置版本: 1
 * 任务: S13。本头文件是 platform/input_win.cpp 的模块内部头，**不是** core 公共头文件；
 * 不修改 core/ 下任何头文件，也不修改 CMakeLists.txt。
 *
 * 仅 C++ 可用（EasyX 本身只支持 C++），不得被任何 .c 文件包含。
 * 但本头文件**不包含 EasyX**，因此全部输入状态机都可以在没有窗口、没有鼠标的环境下
 * 做确定性单元测试（见 work/agents/S13/test_input_state.cpp）。
 *
 * 职责边界（docs/demo-rules.md 1.2 / 1.8、docs/demo-interfaces.md 4）:
 *   - 只做平台输入采集与 RawInput -> BossInput 转换。
 *   - 不读世界、不改世界、不扣能量、不控制弹幕、不写日志文件、不查询墙钟。
 *   - 只调用 EasyX/Win32 的输入与窗口相关 API。
 *
 * 与核心的分工（重要）:
 *   - "指针到位置"的移动曲线（越近越慢、抵达即停）由 core 计算（core/world.c 的 boss_control）。
 *     平台层只负责: 换算战场坐标、判定指针本次是否可用于移动、把键盘轴归一化。
 *   - 指针与键盘的优先级（docs/demo-rules.md 第 3 节"待确认"行当前冻结的实现）:
 *     指针在窗口内且与角色距离**超出死区**时用指针，否则退回键盘轴。
 *     判定"超出死区"需要角色当前位置，因此调用方应在 InputLayout 里填入
 *     boss_position_known / boss_x / boss_y（建议每显示帧从 WorldView 读取）。
 */
#ifndef DEMO_PLATFORM_INPUT_WIN_H
#define DEMO_PLATFORM_INPUT_WIN_H

#ifndef __cplusplus
#error "platform/input_win.h 是 C++ 模块（EasyX 只支持 C++），不得被 .c 文件包含"
#endif

#ifndef NOMINMAX
#define NOMINMAX /* 避免 windows.h 的 min/max 宏污染 */
#endif
#include <windows.h> /* 仅用于 VK_* 虚拟键码常量; 本头文件不引用 easyx.h */

#include <math.h>

#include "demo_base.h"

namespace demo_platform {

/* ---------------------------------------------------------------- 场景常量 */

/* 画面 1280x720，左侧逻辑战场 960x720，右侧 320 px HUD（docs/demo-rules.md 1.2）。
 * 这些只是**回退默认值**：战场左上角在窗口内的偏移必须由调用方通过 InputLayout 给出，
 * 平台层不硬编码偏移。 */
constexpr int FIELD_PIXEL_W = (int)DEMO_FIELD_WIDTH;
constexpr int FIELD_PIXEL_H = (int)DEMO_FIELD_HEIGHT;

/* 指针死区与饱和距离的回退默认值（docs/demo-rules.md 2.1"指针死区 12 px"；
 * 饱和距离 60 px 来自 core/demo_config.c 的 pointer_saturate）。
 * 正常路径应由 game_main 从 DemoConfig 填入 InputLayout；仅在调用方传 <=0 时使用本回退值。 */
constexpr float POINTER_DEADZONE_PX = 12.0f;
constexpr float POINTER_SATURATE_PX = 60.0f;

/* ---------------------------------------------------------------- 布局参数 */

/* 战场在窗口内的布局与手感参数。由 game_main 填入（不得硬编码在平台层）。 */
struct InputLayout {
    /* 战场左上角在窗口客户区内的偏移（像素）。默认 (0,0)。 */
    float field_origin_x;
    float field_origin_y;
    /* 战场尺寸（像素）。默认 960x720，建议由 DemoConfig.field_w/field_h 填入。 */
    float field_w;
    float field_h;
    /* 指针死区与饱和距离（战场像素）。<=0 表示使用 POINTER_*_PX 回退值。 */
    float pointer_deadzone;
    float pointer_saturate;
    /* DemoConfig.pointer_to_position。false 时平台不启用指针移动，只用键盘轴。 */
    bool pointer_movement_enabled;
    /* 角色（Boss）当前战场坐标。boss_position_known=false 时平台无法判定死区，
     * 此时只要指针在窗口与战场内就置 pointer_valid=true，由核心自行套用死区。 */
    bool boss_position_known;
    float boss_x;
    float boss_y;
};

/* 默认布局: 偏移 (0,0)、尺寸 960x720、死区/饱和使用回退值、指针移动开启、角色位置未知。 */
inline InputLayout input_default_layout() {
    InputLayout layout;
    layout.field_origin_x = 0.0f;
    layout.field_origin_y = 0.0f;
    layout.field_w = DEMO_FIELD_WIDTH;
    layout.field_h = DEMO_FIELD_HEIGHT;
    layout.pointer_deadzone = 0.0f; /* <=0 -> 回退到 POINTER_DEADZONE_PX */
    layout.pointer_saturate = 0.0f; /* <=0 -> 回退到 POINTER_SATURATE_PX */
    layout.pointer_movement_enabled = true;
    layout.boss_position_known = false;
    layout.boss_x = 0.0f;
    layout.boss_y = 0.0f;
    return layout;
}

/* ---------------------------------------------------------------- 输入状态机 */

/* 平台输入状态机。所有字段与转换都是纯逻辑，可在无窗口环境下确定性测试
 * （覆盖验收 2/3/4/5/6/7）。*/
struct InputState {
    bool focus;             /* 窗口是否拥有键盘焦点 */
    bool pointer_inside;    /* 指针是否在窗口客户区内 */
    float pointer_client_x; /* 指针客户区坐标（像素） */
    float pointer_client_y;

    /* WASD 与方向键分开记录，避免"按住 W 时松开上方向键"错误地清除前进状态。 */
    bool up_w, down_w, left_w, right_w;
    bool up_arrow, down_arrow, left_arrow, right_arrow;

    bool attack_held[DEMO_PATTERN_COUNT]; /* 1/2/3/4（含小键盘等价键）按住状态 */
    bool attack_edge[DEMO_PATTERN_COUNT]; /* 尚未被消费的"由松开变按下"边沿 */
    bool pause_held;                      /* Esc 按住状态（用于抑制自动重复） */
    bool restart_held;                    /* R 按住状态 */
    bool right_button_held;               /* 鼠标右键按住状态 */

    bool pause_edge;   /* 粘滞: 由 input_pause_toggled() 读取并复位 */
    bool restart_edge; /* 粘滞: 由 input_restart_requested() 读取并复位 */
    bool quit_edge;    /* 粘滞: 由 input_quit_requested() 读取并复位 */

    /* 本显示帧的出招边沿是否尚未被 input_take_boss_input 消费。
     * 保证一个显示帧追赶多个逻辑 tick 时，出招请求只被消费一次。 */
    bool attack_edges_pending;
};

/* 复位为初始状态: 有焦点、无按住、无边沿、指针无效。 */
inline void input_state_init(InputState *state) {
    if (state == nullptr) {
        return;
    }
    *state = InputState(); /* 值初始化: 全部字段清零 */
    state->focus = true;   /* 窗口创建时的 WM_SETFOCUS 早于 input_init，初始视为有焦点 */
}

/* 清空全部按住状态（不动边沿、不动 focus）。 */
inline void input_state_clear_holds(InputState *state) {
    if (state == nullptr) {
        return;
    }
    state->up_w = false;
    state->down_w = false;
    state->left_w = false;
    state->right_w = false;
    state->up_arrow = false;
    state->down_arrow = false;
    state->left_arrow = false;
    state->right_arrow = false;
    for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
        state->attack_held[p] = false;
    }
    state->pause_held = false;
    state->restart_held = false;
    state->right_button_held = false;
}

/* 清空全部待消费边沿（不动按住状态）。 */
inline void input_state_clear_edges(InputState *state) {
    if (state == nullptr) {
        return;
    }
    for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
        state->attack_edge[p] = false;
    }
    state->attack_edges_pending = false;
    state->pause_edge = false;
    state->restart_edge = false;
    state->quit_edge = false;
}

/* 设置焦点。失焦时按 docs/demo-rules.md 1.2 清空: 指针无效、按住清空、边沿清空,
 * 避免恢复窗口后残留按键漏到新局（验收 6）。恢复焦点时不恢复任何按住状态。 */
inline void input_state_set_focus(InputState *state, bool focused) {
    if (state == nullptr) {
        return;
    }
    state->focus = focused;
    if (!focused) {
        state->pointer_inside = false;
        input_state_clear_holds(state);
        input_state_clear_edges(state);
    }
}

inline void input_state_set_pointer(InputState *state, float client_x, float client_y,
                                   bool inside) {
    if (state == nullptr) {
        return;
    }
    state->pointer_client_x = client_x;
    state->pointer_client_y = client_y;
    state->pointer_inside = inside;
}

/* 虚拟键码 -> 招式 ID; 返回 false 表示该键不是出招键。 */
inline bool input_vk_to_pattern(unsigned vk, DemoPattern *out) {
    DemoPattern pattern = DEMO_PATTERN_COUNT;
    switch (vk) {
        case '1':
        case VK_NUMPAD1:
            pattern = DEMO_PATTERN_RING; /* 1 -> 招式 ID 0 */
            break;
        case '2':
        case VK_NUMPAD2:
            pattern = DEMO_PATTERN_COURSE; /* 2 -> 招式 ID 1 */
            break;
        case '3':
        case VK_NUMPAD3:
            pattern = DEMO_PATTERN_MINE; /* 3 -> 招式 ID 2 */
            break;
        case '4':
        case VK_NUMPAD4:
            pattern = DEMO_PATTERN_SHOWER; /* 4 -> 招式 ID 3 */
            break;
        default:
            return false;
    }
    if (out != nullptr) {
        *out = pattern;
    }
    return true;
}

/* 键盘事件。first_press 表示"这次 WM_KEYDOWN 是真正的首次按下"
 * （EasyX ExMessage.prevdown == false；已实测自动重复时 prevdown == true）。
 * 出招请求只在 down && first_press && 当前未处于按住状态时产生**一次**边沿：
 * 长按只触发一次，松开再按可再次请求（验收 2）。
 * 返回 true 表示该键由本模块处理。 */
inline bool input_state_key_event(InputState *state, unsigned vk, bool down, bool first_press) {
    if (state == nullptr) {
        return false;
    }

    /* 移动键: 按住状态与首次按下无关，由 down/up 直接决定 */
    switch (vk) {
        case 'W':
            state->up_w = down;
            return true;
        case 'S':
            state->down_w = down;
            return true;
        case 'A':
            state->left_w = down;
            return true;
        case 'D':
            state->right_w = down;
            return true;
        case VK_UP:
            state->up_arrow = down;
            return true;
        case VK_DOWN:
            state->down_arrow = down;
            return true;
        case VK_LEFT:
            state->left_arrow = down;
            return true;
        case VK_RIGHT:
            state->right_arrow = down;
            return true;
        default:
            break;
    }

    DemoPattern pattern = DEMO_PATTERN_COUNT;
    if (input_vk_to_pattern(vk, &pattern)) {
        if (down) {
            if (first_press && !state->attack_held[pattern]) {
                state->attack_edge[pattern] = true;
                state->attack_edges_pending = true;
            }
            state->attack_held[pattern] = true;
        } else {
            state->attack_held[pattern] = false;
        }
        return true;
    }

    if (vk == VK_ESCAPE) {
        if (down) {
            if (first_press && !state->pause_held) {
                state->pause_edge = true;
            }
            state->pause_held = true;
        } else {
            state->pause_held = false;
        }
        return true;
    }

    if (vk == 'R') {
        if (down) {
            if (first_press && !state->restart_held) {
                state->restart_edge = true;
            }
            state->restart_held = true;
        } else {
            state->restart_held = false;
        }
        return true;
    }

    return false;
}

/* 鼠标按键边沿。右键 = 暂停/继续的等价入口; 左键**不绑定任何技能**（任务卡明确"不适用"）。 */
inline void input_state_right_button_event(InputState *state, bool down) {
    if (state == nullptr) {
        return;
    }
    if (down) {
        if (!state->right_button_held) {
            state->pause_edge = true;
        }
        state->right_button_held = true;
    } else {
        state->right_button_held = false;
    }
}

/* 窗口关闭等外部来源的退出事件。 */
inline void input_state_quit_event(InputState *state) {
    if (state == nullptr) {
        return;
    }
    state->quit_edge = true;
}

/* 键盘八方向轴，斜向归一化，幅度 <= 1（验收 5）。无输入时返回 (0,0)。 */
inline void input_state_keyboard_axis(const InputState *state, float *out_x, float *out_y) {
    float x = 0.0f;
    float y = 0.0f;
    if (state != nullptr) {
        if (state->left_w || state->left_arrow) {
            x -= 1.0f;
        }
        if (state->right_w || state->right_arrow) {
            x += 1.0f;
        }
        if (state->up_w || state->up_arrow) {
            y -= 1.0f;
        }
        if (state->down_w || state->down_arrow) {
            y += 1.0f;
        }
    }
    const float len = sqrtf(x * x + y * y);
    if (len > 1.0e-6f) {
        x /= len;
        y /= len;
    } else {
        x = 0.0f;
        y = 0.0f;
    }
    if (out_x != nullptr) {
        *out_x = x;
    }
    if (out_y != nullptr) {
        *out_y = y;
    }
}

/* 一次移动解析的结果: 指针可用性与最终写入 BossInput 的移动意图。 */
struct InputMovement {
    bool pointer_valid;
    float pointer_x;
    float pointer_y;
    float pointer_deadzone;
    float pointer_saturate;
    float move_x;
    float move_y;
};

/* 解析本帧的移动意图。优先级（docs/demo-rules.md 第 3 节当前冻结实现）:
 *   1) 指针在窗口内、在战场内、且与角色距离**超出死区**  -> 用指针（pointer_valid = true）
 *   2) 指针在窗口内但未超出死区                          -> 用键盘轴
 *   3) 指针不在窗口内 或 窗口失焦                        -> 指针无效且键盘轴清零（停止移动）
 * 指针移动被配置关闭时（pointer_to_position == false）只用键盘轴，指针恒为无效。 */
inline void input_state_resolve_movement(const InputState *state, const InputLayout *layout,
                                        InputMovement *out) {
    if (out == nullptr) {
        return;
    }
    const InputLayout l = (layout != nullptr) ? *layout : input_default_layout();

    float deadzone = (l.pointer_deadzone > 0.0f) ? l.pointer_deadzone : POINTER_DEADZONE_PX;
    float saturate = (l.pointer_saturate > 0.0f) ? l.pointer_saturate : POINTER_SATURATE_PX;
    if (saturate <= deadzone) {
        saturate = deadzone + 1.0f; /* 与 core/world.c 的兜底一致，避免除零 */
    }

    out->pointer_valid = false;
    out->pointer_x = 0.0f;
    out->pointer_y = 0.0f;
    out->pointer_deadzone = deadzone;
    out->pointer_saturate = saturate;
    out->move_x = 0.0f;
    out->move_y = 0.0f;

    float key_x = 0.0f;
    float key_y = 0.0f;
    input_state_keyboard_axis(state, &key_x, &key_y);

    if (!l.pointer_movement_enabled) {
        out->move_x = key_x;
        out->move_y = key_y;
        return;
    }

    bool in_window = (state != nullptr) && state->focus && state->pointer_inside;
    float field_x = 0.0f;
    float field_y = 0.0f;
    if (in_window) {
        field_x = state->pointer_client_x - l.field_origin_x;
        field_y = state->pointer_client_y - l.field_origin_y;
    }
    const bool in_field = in_window && field_x >= 0.0f && field_y >= 0.0f &&
                          field_x <= l.field_w && field_y <= l.field_h;

    bool use_pointer = false;
    if (in_field) {
        if (l.boss_position_known) {
            const float dx = field_x - l.boss_x;
            const float dy = field_y - l.boss_y;
            use_pointer = sqrtf(dx * dx + dy * dy) > deadzone; /* 边界取"不大于", 死区内不移动 */
        } else {
            use_pointer = true; /* 未知角色位置: 交给核心套用死区 */
        }
    }

    if (use_pointer) {
        out->pointer_valid = true;
        out->pointer_x = field_x;
        out->pointer_y = field_y;
        return; /* 用指针时键盘轴保持 0 */
    }
    if (in_field) {
        out->move_x = key_x; /* 指针在窗口内但死区内 -> 退回键盘 */
        out->move_y = key_y;
        return;
    }
    /* 指针不在窗口内或窗口失焦: 键盘轴清零，停止移动 */
}

/* 把状态机快照填成核心的 RawInput（每显示帧一次）。raw 可为 NULL。
 * mouse_x/mouse_y 按 RawInput 约定填**窗口客户区像素坐标**；joy_x/joy_y 填本帧
 * 实际生效的移动意图（与 BossInput 的 move_x/move_y 一致），便于将来回放/headless 复用。
 * attack_pressed 反映尚未被消费的本帧边沿（不消费）。 */
inline void input_state_fill_raw(const InputState *state, const InputLayout *layout,
                                RawInput *raw) {
    if (raw == nullptr) {
        return;
    }
    *raw = RawInput(); /* 全部字段先清零 */

    if (state != nullptr) {
        raw->mouse_x = state->pointer_client_x;
        raw->mouse_y = state->pointer_client_y;
        raw->mouse_inside = state->focus && state->pointer_inside;
        for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
            raw->attack_pressed[p] = state->attack_edge[p];
            raw->attack_held[p] = state->attack_held[p];
        }
        raw->pause_pressed = state->pause_edge;
        raw->restart_pressed = state->restart_edge;
        raw->quit_pressed = state->quit_edge;
    }

    InputMovement movement;
    input_state_resolve_movement(state, layout, &movement);
    raw->joy_x = movement.move_x;
    raw->joy_y = movement.move_y;
}

/* 把状态机快照转换成核心的 BossInput。
 * first_of_frame 为 true 且本帧出招边沿尚未被消费时，才写入 attack_requested[]，
 * 随后立即消费；同一显示帧的后续补帧调用不会重复给出请求。
 * 返回 true 表示本次调用确实写入了至少一个出招请求（验收 2/3）。 */
inline bool input_state_build_boss_input(InputState *state, const InputLayout *layout,
                                        bool first_of_frame, BossInput *out) {
    if (out == nullptr) {
        return false;
    }
    *out = BossInput(); /* 全部字段先清零; 不调用 core 的 boss_input_clear 以保持本 TU 自足 */

    InputMovement movement;
    input_state_resolve_movement(state, layout, &movement);
    out->pointer_valid = movement.pointer_valid;
    out->pointer_x = movement.pointer_x;
    out->pointer_y = movement.pointer_y;
    out->pointer_deadzone = movement.pointer_deadzone;
    out->pointer_saturate = movement.pointer_saturate;
    out->move_x = movement.move_x;
    out->move_y = movement.move_y;

    /* 暂停/重开/退出不写入 BossInput: 核心不消费这三个字段（core/world.c 只读
     * attack_requested 与移动/指针字段），由 game_main 通过 input_pause_toggled() 等
     * 查询接口读取并复位，保证"读取即消费、只触发一次"。 */

    if (state == nullptr) {
        return false;
    }

    bool wrote_request = false;
    if (first_of_frame && state->attack_edges_pending) {
        for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
            if (state->attack_edge[p]) {
                out->attack_requested[p] = true;
                state->attack_edge[p] = false;
                wrote_request = true;
            }
        }
        /* 多个键同帧按下时全部写入，由核心按"招式 ID 小者优先"再裁决一个 */
        state->attack_edges_pending = false;
    }
    return wrote_request;
}

/* 读取并复位暂停/重开/退出边沿（验收 7）。 */
inline bool input_state_take_pause(InputState *state) {
    if (state == nullptr) {
        return false;
    }
    const bool value = state->pause_edge;
    state->pause_edge = false;
    return value;
}

inline bool input_state_take_restart(InputState *state) {
    if (state == nullptr) {
        return false;
    }
    const bool value = state->restart_edge;
    state->restart_edge = false;
    return value;
}

inline bool input_state_take_quit(InputState *state) {
    if (state == nullptr) {
        return false;
    }
    const bool value = state->quit_edge;
    state->quit_edge = false;
    return value;
}

/* ---------------------------------------------------------------- 公共接口 */

/* 绑定 EasyX 图形窗口并安装窗口钩子（用于截获 EasyX 不转发的 WM_KILLFOCUS/WM_CLOSE）。
 * hwnd 可为 NULL: 此时仅状态机可用（unit test 用），不做消息钩子。幂等。 */
void input_init(void *hwnd);
/* 卸载窗口钩子并复位状态。幂等；可在重开或退出时调用。不销毁窗口（由 game_main 调 closegraph）。 */
void input_shutdown();

/* 设置/读取战场布局与手感参数。NULL 表示恢复 input_default_layout()。
 * 建议 game_main 每显示帧填入当前 WorldView 的 Boss 位置。 */
void input_set_layout(const InputLayout *layout);
const InputLayout *input_get_layout();

/* 每显示帧调用一次: 取走消息、刷新指针与焦点、开始新一帧的边沿收集。 */
void input_poll(RawInput *raw);
/* 兼容旧调用形状（不导出 RawInput）。等价于 input_poll(nullptr)。 */
void input_poll();

/* 每逻辑 tick 调用一次。一个显示帧追赶多个 tick 时，第一次（first_of_frame=true）
 * 返回本帧的出招请求，后续补帧返回"无请求"。 */
bool input_take_boss_input(BossInput *out, bool first_of_frame);

/* 暂停/继续、重开、退出事件查询。每次读取即复位（读取并消费）。 */
bool input_pause_toggled();
bool input_restart_requested();
bool input_quit_requested();

/* 外部触发的失焦处理: 指针无效、按键清空、边沿清空（等价于收到 WM_KILLFOCUS）。 */
void input_on_focus_lost();

/* 清空所有待消费边沿（不动按住状态）。建议在 world_reset 之后调用，
 * 避免上一局的出招/暂停边沿漏进新局。 */
void input_clear_pending();

/* ---------------------------------------------------------------- 测试注入 */

/* 仅供单元测试/集成测试使用的状态注入点，与真实窗口消息走同一套状态机代码路径。
 * 正式 game_main 不应调用。 */
void input_test_key(unsigned vk, bool down, bool first_press);
void input_test_right_button(bool down);
void input_test_pointer(float client_x, float client_y, bool inside);
void input_test_focus(bool focused);
void input_test_quit_event();
/* 读取全局状态机（只读），供集成测试断言。 */
const InputState *input_test_state();

/* ------------------------------------------------- 兼容旧调用形状（提交前已存在） */

/* 以下 4 个函数保留 repo 初始提交中 platform/input_win.h 的旧名字，
 * 便于母代理已按旧头文件写好的 game_main.cpp 继续编译；语义与上面的新接口一致。 */
bool input_take(BossInput *out, bool first_of_frame);
bool input_pause_edge_consume();
bool input_restart_edge_consume();
bool input_any_attack_edge();
void input_clear_edges();
bool input_pointer_inside();

}  // namespace demo_platform

#endif /* DEMO_PLATFORM_INPUT_WIN_H */
