/* scene.h - S14 战场绘制模块内部声明（只读 WorldView + DemoConfig）
 *
 * 任务 ID: S14
 * 接口版本: 2（docs/demo-interfaces.md）
 * 配置版本: 1（docs/demo-rules.md / core/demo_config.c 的 demo-config-v1）
 *
 * 本头文件只被 render/scene.cpp 与 work/agents/S14 的演示程序使用；
 * 不改动任何 core/ 头文件，也不改 CMakeLists.txt。
 *
 * 硬约束:
 *  - 只读 WorldView 与 DemoConfig；不写世界、不消耗世界 RNG、不改变物理与判定。
 *  - 不实现任何游戏规则（移动、能量、出招、碰撞、终局都在 core 内）。
 *  - 入口只依赖 EasyX 与公共只读类型，不链接 core 的 .c 目标文件即可编译。
 */
#ifndef DEMO_RENDER_SCENE_H
#define DEMO_RENDER_SCENE_H

#include "demo_base.h"
#include "world.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 累计绘制计数（scene_stats_reset 之后一直累加，供验收日志使用）。 */
typedef struct SceneDrawStats {
    uint32_t frames;                  /* scene_render 调用次数（帧数） */
    uint32_t boss_drawn;              /* 画出的 Boss 数（每帧 0 或 1） */
    uint32_t students_drawn;          /* 画出的存活学生累计 */
    uint32_t students_down_drawn;     /* 画出的倒下学生累计 */
    uint32_t boss_bullets_drawn;      /* 画出的 Boss 弹累计 */
    uint32_t student_bullets_drawn;   /* 画出的学生弹累计 */
    uint32_t target_rings_drawn;      /* 画出的目标环累计 */
    uint32_t warnings_drawn;          /* 实际画出的预警几何累计 */
    uint32_t warnings_skipped;        /* 因 view.warning.valid == false 跳过累计 */
    /* 断言计数: view.warning.valid == false 时仍画出预警的次数。
     * 正确实现必须恒为 0（验收条件 4 的证据）。 */
    uint32_t warnings_drawn_when_invalid;
} SceneDrawStats;

/* 最近一帧的实体计数。 */
typedef struct SceneFrameStats {
    uint32_t boss_bullets;
    uint32_t student_bullets;
    uint32_t students_alive;
    uint32_t students_down;
    uint32_t warning_drawn; /* 本帧是否画了预警: 0 或 1 */
} SceneFrameStats;

const SceneDrawStats *scene_stats(void);
const SceneFrameStats *scene_frame_stats(void);
void scene_stats_reset(void);

/* 渲染使用的像素半径 = round(Actor.radius)。
 * 判定区域与画面一致: 命中半径由配置决定（配置 v1 中 boss_hit_radius_equals_body
 * 为 true，故 Boss 命中半径 == boss_radius == 22 px；学生 == student_radius == 20 px）。
 * 本函数是"画出来的圆半径 == Actor.radius"这条约束的唯一入口。 */
uint32_t scene_draw_radius_px(const Actor *actor);

/* 预警是否应当绘制。唯一读取字段是 view.warning.valid（由 core 的
 * pattern_warning(&world->plan, ...) 填充）。valid == false 时返回 false，
 * 绘制路径不得自行猜测预警几何。 */
bool scene_warning_visible(const WorldView *view);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus
/* 战场绘制入口（需要已 initgraph 的图形窗口）。
 * 只读 view 与 cfg；不写世界、不消耗 RNG、不改变物理。
 * 与 game_main.cpp 的调用约定一致: scene_render(&view, &cfg)。 */
void scene_render(const WorldView *view, const DemoConfig *cfg);
#endif

#endif /* DEMO_RENDER_SCENE_H */
