/* student_bot.c - 脚本 AI 学生: 可复现初始化 + 有限躲避决策
 *
 * 约束(与 docs/demo-rules.md 一致):
 *   - 只读取 StudentObservation 里的公开数据; 不访问任何全局世界状态,
 *     不使用随机数/时间/文件。
 *   - 同观测 + 同状态 => 同结果(确定性); 平局只由 choice_serial 破开。
 *   - 不写世界; 只产出意图, 由核心裁决与调度。
 *   - 本策略是有限躲避, 不保证不被命中, 也不宣称无敌。
 */
#include "student_bot.h"

#include <math.h>

/* 观察结构里最多 64 颗可见弹; 躲避只统计前 24 颗, 控制单次决策成本。 */
#define BOT_DODGE_SAMPLE 24u
/* 8 个候选方向 + 1 个"停留"候选。 */
#define BOT_DIR_COUNT 8
#define BOT_CANDIDATE_COUNT (BOT_DIR_COUNT + 1)
/* 预测最近距离的安全余量(px)。 */
#define BOT_DODGE_SLACK 8.0f
/* 候选点距边界的最小期望余量(px, 相对 self_radius)。 */
#define BOT_EDGE_SLACK 10.0f
/* 候选点离边界过近时的惩罚基数与斜率。 */
#define BOT_EDGE_PENALTY_BASE 50.0f
#define BOT_EDGE_PENALTY_SLOPE 10.0f
/* 边界预先收力阈值(px, 相对 self_radius)。 */
#define BOT_HARD_EDGE_SLACK 4.0f

/* 8 个方向的固定单位向量, 0/45/.../315 度; 用常量表避免任何运行时数学差异。 */
static const float BOT_DIRS[BOT_DIR_COUNT][2] = {
    { 1.0f, 0.0f},
    { 0.70710678f, 0.70710678f},
    { 0.0f, 1.0f},
    {-0.70710678f, 0.70710678f},
    {-1.0f, 0.0f},
    {-0.70710678f, -0.70710678f},
    { 0.0f, -1.0f},
    { 0.70710678f, -0.70710678f}
};

/* 候选点(cx,cy)的风险: 弹威胁权重 + 边界惩罚。越小越安全。 */
static float bot_candidate_risk(const StudentObservation *obs, uint32_t visible,
                                float self_radius, float cx, float cy, float field_w,
                                float field_h)
{
    float risk = 0.0f;
    uint32_t i;

    for (i = 0u; i < visible; ++i) {
        float rx = cx - obs->projectiles[i].x;
        float ry = cy - obs->projectiles[i].y;
        float pvx = obs->projectiles[i].vx;
        float pvy = obs->projectiles[i].vy;
        float vv = pvx * pvx + pvy * pvy;
        float rv = rx * pvx + ry * pvy;
        float dist;

        /* rv <= 0: 弹并未朝候选点飞来, 忽略。 */
        if (rv <= 0.0f) {
            continue;
        }
        if (vv <= 1e-6f) {
            dist = sqrtf(rx * rx + ry * ry);
        } else {
            /* 相对匀速运动下的预计最近距离。 */
            float t = rv / vv;
            float mx = rx - pvx * t;
            float my = ry - pvy * t;
            dist = sqrtf(mx * mx + my * my);
        }
        if (dist < self_radius + obs->projectiles[i].radius + BOT_DODGE_SLACK) {
            risk += 1.0f / (dist + 1.0f);
        }
    }

    {
        float margin = self_radius + BOT_EDGE_SLACK;
        float min_d = cx;

        if (field_w - cx < min_d) {
            min_d = field_w - cx;
        }
        if (cy < min_d) {
            min_d = cy;
        }
        if (field_h - cy < min_d) {
            min_d = field_h - cy;
        }
        if (min_d < margin) {
            risk += BOT_EDGE_PENALTY_BASE + (margin - min_d) * BOT_EDGE_PENALTY_SLOPE;
        }
    }

    return risk;
}

void student_bot_init(StudentBotState *bot, uint32_t serial_seed)
{
    if (bot == NULL) {
        return;
    }

    bot->decision_cooldown_ticks = 0;
    bot->move_x = 0.0f;
    bot->move_y = 0.0f;
    bot->fire_cooldown_ticks = 0;
    bot->patrol_phase = serial_seed % 4u;
    bot->patrol_dir_x = (serial_seed & 1u) ? 1.0f : -1.0f;
    bot->patrol_dir_y = (serial_seed & 2u) ? 1.0f : -1.0f;
    bot->choice_serial = (uint64_t)serial_seed;
}

void student_bot_choose(const StudentObservation *obs, StudentBotState *bot, StudentAction *out)
{
    float cand_x[BOT_CANDIDATE_COUNT];
    float cand_y[BOT_CANDIDATE_COUNT];
    float cand_risk[BOT_CANDIDATE_COUNT];
    int tie[BOT_CANDIDATE_COUNT];
    float field_w;
    float field_h;
    float self_radius;
    float step;
    float dir_x;
    float dir_y;
    float margin;
    float mag2;
    uint32_t visible;
    int tie_count;
    int best;
    int i;

    /* 空指针: 不能写 out 时直接返回, 能写时清零后返回。 */
    if (out == NULL) {
        return;
    }
    out->move_x = 0.0f;
    out->move_y = 0.0f;
    out->request_fire = false;
    if (obs == NULL || bot == NULL) {
        return;
    }

    field_w = (obs->field_w > 0.0f) ? obs->field_w : DEMO_FIELD_WIDTH;
    field_h = (obs->field_h > 0.0f) ? obs->field_h : DEMO_FIELD_HEIGHT;
    self_radius = (obs->self_radius > 0.0f) ? obs->self_radius : 1.0f;

    visible = obs->projectile_count;
    if (visible > (uint32_t)(sizeof(obs->projectiles) / sizeof(obs->projectiles[0]))) {
        visible = (uint32_t)(sizeof(obs->projectiles) / sizeof(obs->projectiles[0]));
    }
    if (visible > BOT_DODGE_SAMPLE) {
        visible = BOT_DODGE_SAMPLE;
    }

    /* 候选: 8 方向 + 停留(最后一格)。 */
    for (i = 0; i < BOT_DIR_COUNT; ++i) {
        cand_x[i] = BOT_DIRS[i][0];
        cand_y[i] = BOT_DIRS[i][1];
    }
    cand_x[BOT_DIR_COUNT] = 0.0f;
    cand_y[BOT_DIR_COUNT] = 0.0f;

    step = self_radius + 16.0f;
    for (i = 0; i < BOT_CANDIDATE_COUNT; ++i) {
        float cx = obs->self_x + cand_x[i] * step;
        float cy = obs->self_y + cand_y[i] * step;

        cand_risk[i] = bot_candidate_risk(obs, visible, self_radius, cx, cy, field_w, field_h);
    }

    best = 0;
    for (i = 1; i < BOT_CANDIDATE_COUNT; ++i) {
        if (cand_risk[i] < cand_risk[best]) {
            best = i;
        }
    }

    /* 风险严格相同: 用 choice_serial 确定性取模破平局(不引入随机源)。 */
    tie_count = 0;
    for (i = 0; i < BOT_CANDIDATE_COUNT; ++i) {
        if (cand_risk[i] == cand_risk[best]) {
            tie[tie_count] = i;
            tie_count += 1;
        }
    }
    if (tie_count > 1) {
        uint32_t slot = (uint32_t)(bot->choice_serial % (uint64_t)tie_count);

        best = tie[slot];
        bot->choice_serial += 1u;
    }

    dir_x = cand_x[best];
    dir_y = cand_y[best];

    /* 边界: 距边界过近时提前把朝界外的分量置 0, 只留切向/内向分量。 */
    margin = self_radius + BOT_HARD_EDGE_SLACK;
    if (obs->self_x <= margin && dir_x < 0.0f) {
        dir_x = 0.0f;
    }
    if (obs->self_x >= field_w - margin && dir_x > 0.0f) {
        dir_x = 0.0f;
    }
    if (obs->self_y <= margin && dir_y < 0.0f) {
        dir_y = 0.0f;
    }
    if (obs->self_y >= field_h - margin && dir_y > 0.0f) {
        dir_y = 0.0f;
    }

    /* 幅度钳到 <= 1。 */
    mag2 = dir_x * dir_x + dir_y * dir_y;
    if (mag2 > 1.0f) {
        float inv = 1.0f / sqrtf(mag2);

        dir_x *= inv;
        dir_y *= inv;
    }

    out->move_x = dir_x;
    out->move_y = dir_y;
    bot->move_x = dir_x;
    bot->move_y = dir_y;

    /* 反击意图: 只在核心允许开火且不与目标重叠时请求。 */
    {
        float dx = obs->target_x - obs->self_x;
        float dy = obs->target_y - obs->self_y;
        bool too_close = (dx * dx + dy * dy) < 1.0f;

        out->request_fire = obs->can_fire && !too_close;
    }
}

const char *student_bot_policy_version(int32_t policy)
{
    if (policy == (int32_t)DEMO_BOT_DODGE) {
        return "dodge-v1";
    }
    if (policy == (int32_t)DEMO_BOT_PATROL) {
        return "patrol-v1";
    }
    return "unknown-v1";
}
