/* demo_config.c - demo 规则配置(母代理独占维护)
 *
 * 配置版本: 4（GPA 击倒数收敛函数；基于公开输入的平衡验证）
 * 已批准规则来源: docs/demo-rules.md (用户 2026-10-06 / 07 指令)
 * 本文件中的"试验数值"可调, 但修改必须递增 version 并更新 docs/demo-rules.md。
 */
#include "demo_base.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define DEMO_CONFIG_VERSION 4u

static void set_default_students(DemoConfig *cfg) {
    /* 默认 3 名学生; 出生点按 960x720 战场等分布置, 与 Boss 初始位置保持安全距离。 */
    cfg->student_count = 3u;
    cfg->student_spawn_x[0] = 200.0f;
    cfg->student_spawn_y[0] = 300.0f;
    cfg->student_spawn_x[1] = 760.0f;
    cfg->student_spawn_y[1] = 300.0f;
    cfg->student_spawn_x[2] = 480.0f;
    cfg->student_spawn_y[2] = 430.0f;
    /* 非默认数量的原生与 Wasm 调试使用同一合法位置；默认三名保持原值。 */
    static const float extra_x[5] = {160.0f, 800.0f, 480.0f, 320.0f, 640.0f};
    static const float extra_y[5] = {160.0f, 160.0f, 160.0f, 470.0f, 470.0f};
    for (uint32_t i = 3u; i < DEMO_MAX_STUDENTS; ++i) {
        cfg->student_spawn_x[i] = extra_x[i - 3u];
        cfg->student_spawn_y[i] = extra_y[i - 3u];
    }
}

static void set_default_patterns(DemoConfig *cfg) {
    /* 0 桃李苑·绿色圆圈好辣: 中消耗·周边压力 */
    PatternConfig *ring = &cfg->patterns[DEMO_PATTERN_RING];
    ring->cost = 30;
    ring->windup_ticks = 72; /* 1.2 s */
    ring->active_ticks = 150;
    ring->bullet_speed = 230.0f; /* v4: 缩短对普通距离学生的追赶时间 */
    ring->wave_count = 3;
    ring->shots_per_wave = 18;
    ring->gap_span_deg = 80.0f;      /* 18 发中连续 4 发缺口 */
    ring->corridor_width = 0.0f;
    ring->spawn_safety_radius = 70.0f; /* 环弹从 Boss 原点放出, 只要求不贴 Boss 自身 */
    ring->first_spawn_sec = 0.0f;
    ring->wave_interval_sec = 0.7f;

    /* 1 选课系统·课表华容道: 中消耗·封路 */
    PatternConfig *course = &cfg->patterns[DEMO_PATTERN_COURSE];
    course->cost = 40; /* v4: 三条分散弹线恢复输出后重定中消耗 */
    course->windup_ticks = 72;
    course->active_ticks = 180;
    course->bullet_speed = 320.0f; /* active 3 s 内能走完整个战场 */
    course->wave_count = 3;
    course->shots_per_wave = 24;
    course->gap_span_deg = 0.0f;
    course->corridor_width = 130.0f; /* 至少保留 110 px 连续通道 */
    course->lane_spread_px = 96.0f; /* v4: 分散到每个封锁列内三条弹线 */
    course->spawn_safety_radius = 90.0f;
    course->first_spawn_sec = 0.0f;
    course->wave_interval_sec = 0.6f;

    /* 2 一教金矿·绩点淘金: 低消耗·追击 */
    PatternConfig *mine = &cfg->patterns[DEMO_PATTERN_MINE];
    mine->cost = 15;
    mine->windup_ticks = 48; /* v4: 0.8 s, 减少锁定后目标走离矿点 */
    mine->active_ticks = 120;
    mine->bullet_speed = 300.0f;
    mine->wave_count = 1;
    mine->shots_per_wave = 12; /* 每面最多 12 发; 三个扇面合计 36 */
    mine->gap_span_deg = 40.0f; /* 扇面之间的间隙 */
    mine->corridor_width = 0.0f;
    mine->spawn_safety_radius = 120.0f; /* v4: 仍检查全部学生与矿点的安全距离 */
    mine->first_spawn_sec = 0.0f;
    mine->wave_interval_sec = 0.0f;

    /* 3 期末总评·绩点淋浴: 高消耗·多目标压制 */
    PatternConfig *shower = &cfg->patterns[DEMO_PATTERN_SHOWER];
    shower->cost = 100;
    shower->windup_ticks = 72;
    shower->active_ticks = 300;
    shower->bullet_speed = 240.0f;
    shower->wave_count = 3; /* v4: 降低满能量招对单招循环的统治程度 */
    shower->shots_per_wave = 16;
    shower->gap_span_deg = 0.0f;
    shower->corridor_width = 120.0f; /* 保留 >= 100 px 竖向缝隙 */
    shower->spawn_safety_radius = 60.0f;
    shower->first_spawn_sec = 0.0f;
    shower->wave_interval_sec = 0.4f;
}

bool demo_config_init(DemoConfig *cfg) {
    if (cfg == NULL) {
        return false;
    }
    memset(cfg, 0, sizeof(*cfg));

    cfg->version = DEMO_CONFIG_VERSION;
    cfg->seed_default = 20261006u;

    cfg->field_w = DEMO_FIELD_WIDTH;
    cfg->field_h = DEMO_FIELD_HEIGHT;

    /* Boss 活动区域: 全战场按半径留边。学生可在同一战场内活动。 */
    cfg->boss_radius = 22.0f;
    cfg->boss_move_min_x = 22.0f;
    cfg->boss_move_max_x = 938.0f;
    cfg->boss_move_min_y = 22.0f;
    cfg->boss_move_max_y = 698.0f;
    cfg->boss_hit_radius = 0.0f;
    cfg->boss_hit_radius_equals_body = true; /* 命中半径 = 22 px */
    cfg->boss_hp = 6;
    cfg->boss_hurt_invuln_ticks = 36; /* 0.6 s: demo 手感, 记录为试验值 */
    cfg->boss_speed = 270.0f;         /* 复现移动速度 */
    cfg->pointer_to_position = true;  /* 指针到位置(鼠标摇杆, florr 风格) */
    cfg->pointer_deadzone = 12.0f;
    cfg->pointer_saturate = 60.0f;

    cfg->student_radius = 20.0f;
    cfg->student_speed = 150.0f;
    cfg->student_hp = 3;
    cfg->student_hurt_invuln_ticks = 18;
    cfg->student_decision_ticks = 6; /* 每 6 tick 决策一次, 动作保持 */
    cfg->student_bot_policy = DEMO_BOT_DODGE;
    cfg->student_fire_interval_ticks = 78; /* 1.3 s */
    cfg->student_fire_damage = 1;
    cfg->student_bullet_speed = 260.0f;
    cfg->student_bullet_radius = 5.0f;
    cfg->student_bullet_lifetime_ticks = 240;
    cfg->student_fire_min_range = 50.0f; /* 20 + 22 + 5 + 3; 消除 48 px 贴脸免火 */

    cfg->energy_max = 100;
    cfg->energy_start = 60;
    cfg->energy_regen_per_sec = 10.0f;

    cfg->projectile_cap = DEMO_MAX_PROJECTILES;
    cfg->boss_bullet_radius = 6.0f;
    cfg->boss_bullet_damage = 1.0f;
    cfg->boss_bullet_lifetime_ticks = 480;
    cfg->boss_bullet_clear_on_attack_end = 1;
    cfg->boss_bullet_straight = true; /* 已批准: 直线基础运动, 无默认场力 */

    cfg->endless_mode = true;
    cfg->wave_gap_ticks = 120;
    cfg->gpa_half_saturation_kills = 20u;
    cfg->gpa_max_hundredths = 430;
    cfg->outcome_rule = DEMO_OUTCOME_BOSS_LOSE; /* 无尽模式: Boss 死亡优先 */
    cfg->max_ticks = 0; /* 0 = 不设强制时限 */

    set_default_students(cfg);
    set_default_patterns(cfg);
    return true;
}

const char *demo_config_version_string(void) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "demo-config-v%u", (unsigned)DEMO_CONFIG_VERSION);
    return buf;
}

static bool is_finite_f(float v) { return isfinite(v) != 0; }

bool demo_config_validate(const DemoConfig *cfg, char *err, size_t err_cap) {
    if (err != NULL && err_cap > 0u) {
        err[0] = '\0';
    }
#define FAIL(msg)                        \
    do {                                 \
        if (err != NULL && err_cap > 0u) { \
            snprintf(err, err_cap, "%s", (msg)); \
        }                                \
        return false;                    \
    } while (0)

    if (cfg == NULL) {
        FAIL("config pointer is null");
    }
    if (cfg->version == 0u) {
        FAIL("config version must be >= 1");
    }
    if (!is_finite_f(cfg->field_w) || !is_finite_f(cfg->field_h) || cfg->field_w <= 0.0f ||
        cfg->field_h <= 0.0f) {
        FAIL("field size must be positive and finite");
    }
    if (cfg->boss_move_min_x > cfg->boss_move_max_x ||
        cfg->boss_move_min_y > cfg->boss_move_max_y) {
        FAIL("boss move bounds inverted");
    }
    if (cfg->boss_move_min_x < cfg->boss_radius || cfg->boss_move_min_y < cfg->boss_radius ||
        cfg->boss_move_max_x > cfg->field_w - cfg->boss_radius ||
        cfg->boss_move_max_y > cfg->field_h - cfg->boss_radius) {
        FAIL("boss move bounds must keep the boss body inside the field");
    }
    if (cfg->boss_hp <= 0) {
        FAIL("boss hp must be positive");
    }
    if (!is_finite_f(cfg->boss_speed) || cfg->boss_speed <= 0.0f) {
        FAIL("boss speed must be positive");
    }
    if (cfg->student_count == 0u || cfg->student_count > DEMO_MAX_STUDENTS) {
        FAIL("student count out of range");
    }
    if (cfg->student_hp <= 0) {
        FAIL("student hp must be positive");
    }
    if (cfg->student_decision_ticks <= 0) {
        FAIL("student decision period must be positive");
    }
    /* 学生反击字段: 未校验时非法值会被静默接受, 例如负伤害会让 Boss 回血 */
    if (cfg->student_fire_interval_ticks <= 0) {
        FAIL("student fire interval must be positive");
    }
    if (cfg->student_fire_damage <= 0) {
        FAIL("student fire damage must be positive");
    }
    if (!is_finite_f(cfg->student_bullet_speed) || cfg->student_bullet_speed <= 0.0f) {
        FAIL("student bullet speed must be positive and finite");
    }
    if (!is_finite_f(cfg->student_bullet_radius) || cfg->student_bullet_radius <= 0.0f) {
        FAIL("student bullet radius must be positive and finite");
    }
    if (cfg->student_bullet_lifetime_ticks <= 0) {
        FAIL("student bullet lifetime must be positive");
    }
    if (!is_finite_f(cfg->student_fire_min_range) || cfg->student_fire_min_range < 0.0f) {
        FAIL("student fire min range must be non-negative and finite");
    }
    if (!is_finite_f(cfg->boss_bullet_radius) || cfg->boss_bullet_radius <= 0.0f) {
        FAIL("boss bullet radius must be positive and finite");
    }
    if (!is_finite_f(cfg->boss_bullet_damage) || cfg->boss_bullet_damage <= 0.0f) {
        FAIL("boss bullet damage must be positive and finite");
    }
    if (cfg->boss_bullet_lifetime_ticks <= 0) {
        FAIL("boss bullet lifetime must be positive");
    }
    if (!is_finite_f(cfg->student_radius) || cfg->student_radius <= 0.0f) {
        FAIL("student radius must be positive and finite");
    }
    if (!is_finite_f(cfg->student_speed) || cfg->student_speed <= 0.0f) {
        FAIL("student speed must be positive and finite");
    }
    if (!is_finite_f(cfg->boss_radius) || cfg->boss_radius <= 0.0f) {
        FAIL("boss radius must be positive and finite");
    }
    if (cfg->boss_hurt_invuln_ticks < 0 || cfg->student_hurt_invuln_ticks < 0) {
        FAIL("invulnerability ticks must be non-negative");
    }
    if (cfg->energy_max <= 0 || cfg->energy_start < 0 || cfg->energy_start > cfg->energy_max) {
        FAIL("energy settings out of range");
    }
    if (!is_finite_f(cfg->energy_regen_per_sec) || cfg->energy_regen_per_sec < 0.0f) {
        FAIL("energy regen must be non-negative and finite");
    }
    if (cfg->projectile_cap == 0u || cfg->projectile_cap > DEMO_MAX_PROJECTILES) {
        FAIL("projectile capacity out of range");
    }
    if (cfg->outcome_rule != DEMO_OUTCOME_DRAW && cfg->outcome_rule != DEMO_OUTCOME_BOSS_WIN &&
        cfg->outcome_rule != DEMO_OUTCOME_BOSS_LOSE) {
        FAIL("outcome rule unknown");
    }
    if (cfg->max_ticks < 0) {
        FAIL("max ticks must be >= 0");
    }
    if (cfg->endless_mode && cfg->wave_gap_ticks < 120) {
        FAIL("endless wave preview must last at least 120 ticks");
    }
    if (cfg->gpa_max_hundredths != 430 || cfg->gpa_half_saturation_kills == 0u) {
        FAIL("GPA maximum must be 430 and half saturation kills positive");
    }

    for (uint32_t i = 0u; i < DEMO_MAX_STUDENTS; ++i) {
        if (!cfg->endless_mode && i >= cfg->student_count) {
            continue;
        }
        float sx = cfg->student_spawn_x[i];
        float sy = cfg->student_spawn_y[i];
        if (!is_finite_f(sx) || !is_finite_f(sy) || sx < cfg->student_radius ||
            sx > cfg->field_w - cfg->student_radius || sy < cfg->student_radius ||
            sy > cfg->field_h - cfg->student_radius) {
            FAIL("student spawn point outside legal area");
        }
    }

    for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
        const PatternConfig *pc = &cfg->patterns[p];
        if (pc->cost <= 0 || pc->cost > cfg->energy_max) {
            FAIL("pattern cost out of range");
        }
        if (pc->windup_ticks < 0 || pc->active_ticks <= 0) {
            FAIL("pattern durations invalid");
        }
        if (!is_finite_f(pc->bullet_speed) || pc->bullet_speed <= 0.0f) {
            FAIL("pattern bullet speed must be positive");
        }
        if (!is_finite_f(pc->lane_spread_px) || pc->lane_spread_px < 0.0f) {
            FAIL("pattern lane spread must be non-negative and finite");
        }
        if (pc->wave_count <= 0 || pc->wave_count > 8) {
            FAIL("pattern wave count out of range");
        }
        if (pc->first_spawn_sec < 0.0f || pc->wave_interval_sec < 0.0f) {
            FAIL("pattern wave timing must be non-negative");
        }
    }
    /* 环弹每圈发数不得超过计划几何容量 */
    const PatternConfig *course = &cfg->patterns[DEMO_PATTERN_COURSE];
    float column_width = cfg->field_w / 3.0f;
    if (course->lane_spread_px > column_width * 0.5f - cfg->boss_bullet_radius ||
        column_width * 1.5f - course->lane_spread_px < course->corridor_width) {
        FAIL("course lane spread must keep the full channel clear");
    }
    if (cfg->patterns[DEMO_PATTERN_RING].shots_per_wave > 180) {
        FAIL("ring shots per wave too large");
    }
    return true;
#undef FAIL
}
