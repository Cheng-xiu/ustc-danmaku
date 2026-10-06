/* scene.h - S14 战场绘制模块内部声明（只读 WorldView + DemoConfig）
 *
 * 接口版本: 2（docs/demo-interfaces.md）
 * 配置版本: 1（docs/demo-rules.md，core/demo_config.c）
 *
 * 本头文件只被 render/scene.cpp 与 work/agents/S14 的自检程序使用；
 * 公共头文件（core/*.h）由母代理独占，本模块不修改、不重定义其中的类型。
 */
#ifndef DEMO_RENDER_SCENE_H
#define DEMO_RENDER_SCENE_H

#include "demo_base.h"
#include "world.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 最近一次绘制（EasyX 路径或无窗口统计路径）的计数。
 * 用于验收日志：帧数、绘制的弹数、学生数、预警绘制/跳过次数。 */
typedef struct SceneDrawStats {
    uint32_t frames;            /* scene_draw_* 被调用的帧数 */
    uint32_t boss_bullets;      /* 本帧 active 的 Boss 弹 */
    uint32_t student_bullets;   /* 本帧 active 的学生弹 */
    uint32_t students_alive;    /* 学生总数中的存活数 */
    uint32_t students_down;     /* 学生总数中的倒下数 */
    uint32_t marked_rings;      /* 本帧画出的目标环数量（0 或 1） */
    uint32_t warnings_drawn;    /* 本帧实际画出的预警数量（0 或 1） */
    uint32_t warnings_skipped;  /* 因 view.warning.valid == false 跳过的次数 */
} SceneDrawStats;

/* 最近一帧的计数（指向模块内部静态存储，只读；可为 NULL 指针时为全 0）。 */
const SceneDrawStats *scene_last_stats(void);

/* 渲染使用的像素半径 = round(actor->radius)。
 * 判定区域与画面一致：命中半径由配置决定（boss_hit_radius_equals_body 时等于)
 * boss_radius），绘制圆半径必须等于 Actor.radius，本函数就是这条约束的单一入口。 */
uint32_t scene_draw_radius_px(const Actor *actor);

/* 是否应当绘制预警。唯一读数是 view.warning.valid：
 * valid == false 时本函数返回 false，绘制路径不得自行"猜"预警几何。 */
bool scene_should_draw_warning(const WorldView *view);

/* 无 EasyX 调用的统计路径：不画任何像素，只统计本帧要画的实体数量。
 * 返回本帧统计到的实体总数（Boss 1 + 学生 + active 弹 + 预警几何）。 */
uint32_t scene_draw_headless(const DemoConfig *cfg, const WorldView *view);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/* EasyX 绘制入口（需要已 initgraph 的图形窗口）。
 * 只读 cfg 与 view；不写世界、不消耗世界 RNG、不改变物理。
 *
 * 调用约定与 game_main.cpp 一致: scene_render(&view, &cfg)。 */
void scene_render(const WorldView *view, const DemoConfig *cfg);
#endif

#endif /* DEMO_RENDER_SCENE_H */
