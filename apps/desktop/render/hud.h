/* hud.h - S15 右侧 320 px HUD 面板的模块内声明（render/ 内部使用）
 *
 * 接口版本: 2（docs/demo-interfaces.md 变更记录）
 * 配置版本: 1（core/demo_config.c 的 demo-config-v1）
 *
 * 本头文件只由 render/hud.cpp 与 S15 的演示程序使用；不改动 render/scene.h，
 * 也不改动任何 core/ 头文件。
 *
 * 硬约束（与任务卡一致，违反即为缺陷）:
 *  - 只读 WorldView 与 DemoConfig；不写世界、不消耗 RNG、不裁决胜负。
 *  - 不写日志文件，不做任何文件 I/O。
 *  - 不使用墙钟或任何影响游戏逻辑的时间量（HUD 动画不参与逻辑）。
 *  - 不 include core 的 .c 文件。
 *  - 只负责在“当前图形设备”上绘制；窗口或离屏 IMAGE 由调用方创建与切换。
 */
#ifndef DEMO_RENDER_HUD_H
#define DEMO_RENDER_HUD_H

#include "demo_base.h"
#include "world.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 画面 1280x720：左侧逻辑战场 960x720，右侧 320 px HUD（x 从 960 到 1280） */
#define HUD_PANEL_X 960
#define HUD_PANEL_Y 0
#define HUD_PANEL_W 320
#define HUD_PANEL_H 720
#define HUD_FIELD_W 960
#define HUD_FIELD_H 720

/* 一帧的 HUD 输入。
 *
 * paused: 暂停由外层状态机管理，core 的 WorldView.paused 恒为 false
 *         （见 core/world.c 的 world_make_view），因此暂停必须由调用方传入。
 *         hud_is_paused() 会把两者取或，以便 core 将来表达暂停时无需改这里。 */
typedef struct HudInput {
    const WorldView *view;
    const DemoConfig *config;
    bool paused;
} HudInput;

/* 文本越界自检累计量：hud.cpp 每画一行文本都会先测量再绘制并记录在这里。
 * 用途是给演示/验收提供“最长文本宽度 vs 面板宽度”的可打印证据，不参与绘制逻辑。
 *
 * 只有面板（320 px 约束）的文本进入 max_* 与 panel_line_count；
 * line_count 累计全部范围（含开始说明与结果卡片，它们画在 960 px 宽的区域）。
 * 这样“最长文本 vs 面板宽度”的比较才处于同一约束下，证据才有效。 */
typedef struct HudTextAudit {
    int panel_width;      /* 面板总宽 320 */
    int content_width;    /* 面板内可用文本宽度 300 */
    int line_count;       /* 累计绘制的文本行数（全部范围） */
    int panel_line_count; /* 其中属于 320 px 面板的文本行数 */
    int clipped_count;    /* 面板内因超宽而省略号截断的行数 */
    int max_drawn_width;  /* 面板内实际绘制文本的最大像素宽度（已收窄到栏宽内） */
    int max_drawn_limit;  /* 该行的栏宽上限 */
    int max_raw_width;    /* 面板内测量到的最长原始文本宽度（截断前） */
    int max_raw_limit;    /* 该原始文本允许的栏宽 */
    wchar_t max_raw_text[192]; /* 最长原始文本内容 */
    int min_elide_chars;  /* 面板内截断后仍保留的最少字符数（确认未退化为空串） */
} HudTextAudit;

/* 面板（右侧 320 px）: 标题、血量、能量、四招、目标、逐学生生命、攻击状态 */
void hud_draw_panel(const HudInput *in);

/* 开始说明画面（整屏）。说明目标、操作与能量机制。 */
void hud_draw_intro(void);

/* 暂停遮罩与结果提示（只覆盖左侧战场，不遮挡右侧信息面板） */
void hud_draw_pause_overlay(const HudInput *in);
void hud_draw_result(const HudInput *in);

/* 组合入口：面板 + (暂停遮罩 或 结果提示)。终局/截断时不再画暂停遮罩。 */
void hud_draw_all(const HudInput *in);

/* 只读判定，供调用方与演示程序复用 */
bool hud_is_paused(const HudInput *in);
bool hud_is_finished(const HudInput *in);

/* 文本越界自检 */
const HudTextAudit *hud_text_audit(void);
void hud_text_audit_reset(void);

/* 布局自检用：把 text 收窄到 max_width 像素后的实际字符串与宽度（纯只读计算）。
 * 供演示程序验证“任何一行文本收窄后都不超过面板栏宽”。 */
int hud_fit_preview(const wchar_t *text, int max_width, wchar_t *out, int out_cap);

/* 供演示与自检复用：招式的可读不可用原因。返回静态宽字符串，不修改世界。
 * 只在 view->pattern_available[r] 为 false 时有意义；不做任何裁决。 */
const wchar_t *hud_pattern_block_reason(const WorldView *view, const DemoConfig *config,
                                        int pattern_index);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DEMO_RENDER_HUD_H */
