/* sim/main.c - ustc-danmaku headless 仿真入口 (S16)
 *
 * 职责: 参数解析 -> demo_config_init/validate -> world_reset -> 逐 tick 脚本动作
 *       -> world_step -> 日志 + 回放行 + 结果摘要。
 *
 * 硬性约束:
 *   - 不实现任何碰撞/能量/招式逻辑: 全部通过 core 的 world_step / demo_config_* 完成;
 *   - 不使用 rand() 与墙钟: 唯一随机来源是 CLI 的 --seed (缺省 cfg.seed_default);
 *   - 日志与回放只读世界视图, 不调用 core 的 RNG 或 step 之外的写入接口。
 *
 * CLI:
 *   sim.exe --config default --seed <n> --scenario <name> --max-ticks <n>
 *           --log <path> --replay <path> --script <patrol|dodge|mixed|wait>
 *           [--replay-interval <n>] [--tick-log <path>] [--help]
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo_base.h"
#include "patterns.h"
#include "student_bot.h"
#include "world.h"

#include "log.h"

#ifndef SIM_BUILD_ID
#define SIM_BUILD_ID "s16-sim-1"
#endif

/* 未显式给出 --max-ticks 且 cfg.max_ticks == 0 时的仿真层安全上限。
 * 触发该上限同样记为 TRUNCATED/未完成, 绝不判胜负。 */
#define SIM_SAFETY_MAX_TICKS 36000

#define SIM_DEFAULT_REPLAY_INTERVAL 60

typedef enum ScriptKind {
    SCRIPT_PATROL = 0,
    SCRIPT_DODGE,
    SCRIPT_MIXED,
    SCRIPT_WAIT
} ScriptKind;

typedef struct SimTally {
    uint32_t requests;
    uint32_t requests_by_pattern[DEMO_PATTERN_COUNT];
    uint32_t accepts;
    uint32_t rejects;
    uint32_t reject_by_reason[DEMO_REJECT_COUNT];
    uint32_t boss_hits;
    uint32_t student_hits;
    uint32_t students_down;
    uint32_t boss_down;
    uint32_t overflow_events;
    uint32_t truncated_events;
} SimTally;

typedef struct ScriptState {
    float patrol_dir;
} ScriptState;

static const char *status_name(DemoWorldStatus s) {
    switch (s) {
        case DEMO_STATUS_BOSS_WIN:
            return "BOSS_WIN";
        case DEMO_STATUS_BOSS_LOSE:
            return "BOSS_LOSE";
        case DEMO_STATUS_DRAW:
            return "DRAW";
        case DEMO_STATUS_RUNNING:
        default:
            return "RUNNING";
    }
}

static const char *reject_name(DemoRejectReason r) {
    switch (r) {
        case DEMO_REJECT_NONE:
            return "NONE";
        case DEMO_REJECT_NO_ENERGY:
            return "NO_ENERGY";
        case DEMO_REJECT_BUSY:
            return "BUSY";
        case DEMO_REJECT_NO_TARGET:
            return "NO_TARGET";
        case DEMO_REJECT_NO_REQUEST:
            return "NO_REQUEST";
        default:
            return "UNKNOWN";
    }
}

static const char *pattern_label(DemoPattern p) {
    const char *n = pattern_name(p);
    return (n != NULL) ? n : "?";
}

static const char *script_name(ScriptKind k) {
    switch (k) {
        case SCRIPT_PATROL:
            return "patrol";
        case SCRIPT_DODGE:
            return "dodge";
        case SCRIPT_MIXED:
            return "mixed";
        case SCRIPT_WAIT:
        default:
            return "wait";
    }
}

static bool parse_script(const char *s, ScriptKind *out) {
    if (s == NULL || out == NULL) {
        return false;
    }
    if (strcmp(s, "patrol") == 0) {
        *out = SCRIPT_PATROL;
        return true;
    }
    if (strcmp(s, "dodge") == 0) {
        *out = SCRIPT_DODGE;
        return true;
    }
    if (strcmp(s, "mixed") == 0) {
        *out = SCRIPT_MIXED;
        return true;
    }
    if (strcmp(s, "wait") == 0) {
        *out = SCRIPT_WAIT;
        return true;
    }
    return false;
}

static bool parse_i64(const char *s, long long *out) {
    if (s == NULL || *s == '\0') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    *out = v;
    return true;
}

static bool parse_u64(const char *s, uint64_t *out) {
    if (s == NULL || *s == '\0') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    *out = (uint64_t)v;
    return true;
}

static void print_usage(FILE *f) {
    (void)fprintf(f,
                  "用法: sim.exe [选项]\n"
                  "  --config <name>            配置版本, 目前只接受 default (配置版本 1)\n"
                  "  --seed <n>                 随机种子 (无符号整数; 缺省 cfg.seed_default)\n"
                  "  --scenario <name>          场景名 (仅作为日志标签记录)\n"
                  "  --max-ticks <n>            最大 tick 数; 到达仍未结束时记 TRUNCATED/未完成\n"
                  "  --log <path>               日志文件路径 (NULL/缺省: 只输出 stdout)\n"
                  "  --replay <path>            回放状态行文件\n"
                  "  --script <patrol|dodge|mixed|wait>\n"
                  "                             脚本动作 (缺省 mixed)\n"
                  "  --replay-interval <n>      回放行间隔 tick (缺省 %d)\n"
                  "  --tick-log <path>          额外逐 tick 详细日志文件\n"
                  "  --help                     打印本用法并退出 0\n"
                  "退出码: 0 正常结束(含 TRUNCATED); 1 配置/世界初始化失败; 2 参数错误\n",
                  SIM_DEFAULT_REPLAY_INTERVAL);
}

/* ------------------------------------------------------------ 脚本动作 */

/* 只读 world 并填写 BossInput; 不调用 RNG, 不修改世界。 */
static void script_fill_input(const World *world, ScriptKind kind, ScriptState *st,
                              BossInput *in) {
    boss_input_clear(in);
    if (kind == SCRIPT_WAIT) {
        return;
    }
    if (kind == SCRIPT_PATROL) {
        float min_x = world->cfg.boss_move_min_x;
        float max_x = world->cfg.boss_move_max_x;
        if (world->boss.x >= max_x - 0.5f) {
            st->patrol_dir = -1.0f;
        } else if (world->boss.x <= min_x + 0.5f) {
            st->patrol_dir = 1.0f;
        }
        if (st->patrol_dir == 0.0f) {
            st->patrol_dir = 1.0f;
        }
        in->move_x = st->patrol_dir; /* 只移动, 不出招 */
        in->move_y = 0.0f;
        return;
    }

    /* dodge / mixed: 朝最近存活学生移动 (指针到位置语义, 与 cfg.pointer_to_position 一致) */
    DemoEntityId id = 0u;
    float tx = 0.0f;
    float ty = 0.0f;
    if (world_nearest_student(world, &id, &tx, &ty)) {
        in->pointer_valid = true;
        in->pointer_x = tx;
        in->pointer_y = ty;
        in->pointer_deadzone = world->cfg.pointer_deadzone;
        in->pointer_saturate = world->cfg.pointer_saturate;
    }

    if (kind == SCRIPT_MIXED) {
        /* 固定顺序轮转 + 能量足够时才请求 (能量不足不算请求, 不产生 REJECTED 事件) */
        DemoPattern p = (DemoPattern)(world->tick % (int32_t)DEMO_PATTERN_COUNT);
        int32_t cost = world->cfg.patterns[p].cost;
        if (world->energy >= cost) {
            in->attack_requested[p] = true;
        }
    }
}

static void count_requests(const BossInput *in, SimTally *t) {
    for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
        if (in->attack_requested[p]) {
            t->requests++;
            t->requests_by_pattern[p]++;
        }
    }
}

/* ------------------------------------------------------------ 事件处理 */

static void process_events(const World *world, SimTally *t, SimLogger *tick_log) {
    const StepEvents *ev = &world->events;
    if (ev->dropped > 0u) {
        sim_log_line(tick_log, "TICK %d WARN events_dropped=%u", world->tick, ev->dropped);
    }
    for (uint32_t i = 0u; i < ev->count; ++i) {
        const StepEvent *e = &ev->items[i];
        switch (e->type) {
            case DEMO_EVENT_ATTACK_ACCEPTED:
                t->accepts++;
                sim_log_line(tick_log,
                             "TICK %d ATTACK_ACCEPTED pattern=%s target=%u energy=%d", e->tick,
                             pattern_label(e->pattern), (unsigned)e->target_id, world->energy);
                break;
            case DEMO_EVENT_ATTACK_REJECTED:
                t->rejects++;
                if ((int)e->reject >= 0 && (int)e->reject < (int)DEMO_REJECT_COUNT) {
                    t->reject_by_reason[e->reject]++;
                }
                sim_log_line(tick_log,
                             "TICK %d ATTACK_REJECTED pattern=%s reason=%s energy=%d", e->tick,
                             pattern_label(e->pattern), reject_name(e->reject), world->energy);
                break;
            case DEMO_EVENT_HIT:
                if (e->target_id == world->boss.id) {
                    t->boss_hits++;
                    sim_log_line(tick_log, "TICK %d HIT target=BOSS damage=%d boss_hp=%d",
                                 e->tick, e->amount, world->boss.hp);
                } else {
                    t->student_hits++;
                    sim_log_line(tick_log, "TICK %d HIT target=STUDENT id=%u damage=%d", e->tick,
                                 (unsigned)e->target_id, e->amount);
                }
                break;
            case DEMO_EVENT_KNOCKDOWN:
                if (e->target_id == world->boss.id) {
                    t->boss_down++;
                } else {
                    t->students_down++;
                }
                sim_log_line(tick_log, "TICK %d KNOCKDOWN target=%u boss_hp=%d", e->tick,
                             (unsigned)e->target_id, world->boss.hp);
                break;
            case DEMO_EVENT_SPAWN_OVERFLOW:
                t->overflow_events++;
                sim_log_line(tick_log, "TICK %d SPAWN_OVERFLOW amount=%d", e->tick, e->amount);
                break;
            case DEMO_EVENT_GAME_OVER:
                sim_log_line(tick_log, "TICK %d GAME_OVER status=%s", e->tick,
                             status_name((DemoWorldStatus)e->amount));
                break;
            case DEMO_EVENT_TRUNCATED:
                t->truncated_events++;
                sim_log_line(tick_log, "TICK %d TRUNCATED (未完成, 不判胜负)", e->tick);
                break;
            case DEMO_EVENT_ENERGY_SPENT:
                sim_log_line(tick_log, "TICK %d ENERGY_SPENT pattern=%s amount=%d energy=%d",
                             e->tick, pattern_label(e->pattern), e->amount, world->energy);
                break;
            default:
                break;
        }
    }
}

static void log_tick_line(SimLogger *tick_log, const World *world) {
    uint32_t alive = 0u;
    for (uint32_t i = 0u; i < world->student_count; ++i) {
        if (world->students[i].alive) {
            alive++;
        }
    }
    uint32_t boss_bullets = pool_count_faction(&world->pool, DEMO_FACTION_BOSS);
    uint32_t student_bullets = pool_count_faction(&world->pool, DEMO_FACTION_STUDENT);
    sim_log_line(tick_log,
                 "TICK %d energy=%d attack_state=%d boss=(%.2f,%.2f) boss_hp=%d alive=%u/%u "
                 "bullets(boss=%u,student=%u) live=%u",
                 world->tick, world->energy, (int)world->attack_state, world->boss.x,
                 world->boss.y, world->boss.hp, alive, world->student_count, boss_bullets,
                 student_bullets, world->pool.live_count);
}

/* ------------------------------------------------------------ 回放 */

static void write_replay_header(FILE *replay, const char *build, const char *config_version,
                                const char *bot_version, uint64_t seed, const char *scenario) {
    (void)fprintf(replay, "# replay build=%s config=%s bot=%s seed=%" PRIu64 " scenario=%s\n",
                  build, config_version, bot_version, seed, scenario);
    (void)fprintf(replay,
                  "# fields: tick boss_x boss_y boss_hp students_alive student_hp_total energy "
                  "boss_bullets student_bullets attack_state\n");
}

static void write_replay_line(FILE *replay, const World *world) {
    uint32_t alive = 0u;
    int32_t hp_total = 0;
    for (uint32_t i = 0u; i < world->student_count; ++i) {
        if (world->students[i].alive) {
            alive++;
            hp_total += world->students[i].hp;
        }
    }
    (void)fprintf(replay,
                  "%d %.2f %.2f %d %u %d %d %u %u %d\n", world->tick, (double)world->boss.x,
                  (double)world->boss.y, world->boss.hp, alive, hp_total, world->energy,
                  pool_count_faction(&world->pool, DEMO_FACTION_BOSS),
                  pool_count_faction(&world->pool, DEMO_FACTION_STUDENT),
                  (int)world->attack_state);
}

/* ------------------------------------------------------------ main */

int main(int argc, char **argv) {
    const char *config_name = "default";
    const char *scenario = "default";
    const char *log_path = NULL;
    const char *replay_path = NULL;
    const char *tick_log_path = NULL;
    ScriptKind script = SCRIPT_MIXED;
    bool seed_given = false;
    uint64_t seed = 0u;
    bool max_ticks_given = false;
    long long max_ticks_arg = 0;
    long long replay_interval = SIM_DEFAULT_REPLAY_INTERVAL;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else if (strcmp(a, "--config") == 0) {
            if (++i >= argc) {
                (void)fprintf(stderr, "错误: --config 缺少参数\n");
                print_usage(stderr);
                return 2;
            }
            config_name = argv[i];
        } else if (strcmp(a, "--seed") == 0) {
            if (++i >= argc || !parse_u64(argv[i], &seed)) {
                (void)fprintf(stderr, "错误: --seed 需要无符号整数\n");
                print_usage(stderr);
                return 2;
            }
            seed_given = true;
        } else if (strcmp(a, "--scenario") == 0) {
            if (++i >= argc || argv[i][0] == '\0') {
                (void)fprintf(stderr, "错误: --scenario 需要非空名称\n");
                print_usage(stderr);
                return 2;
            }
            scenario = argv[i];
        } else if (strcmp(a, "--max-ticks") == 0) {
            if (++i >= argc || !parse_i64(argv[i], &max_ticks_arg) || max_ticks_arg < 0 ||
                max_ticks_arg > 2000000000LL) {
                (void)fprintf(stderr, "错误: --max-ticks 需要 [0, 2000000000] 的整数\n");
                print_usage(stderr);
                return 2;
            }
            max_ticks_given = true;
        } else if (strcmp(a, "--log") == 0) {
            if (++i >= argc) {
                (void)fprintf(stderr, "错误: --log 缺少参数\n");
                print_usage(stderr);
                return 2;
            }
            log_path = argv[i];
        } else if (strcmp(a, "--replay") == 0) {
            if (++i >= argc) {
                (void)fprintf(stderr, "错误: --replay 缺少参数\n");
                print_usage(stderr);
                return 2;
            }
            replay_path = argv[i];
        } else if (strcmp(a, "--script") == 0) {
            if (++i >= argc || !parse_script(argv[i], &script)) {
                (void)fprintf(stderr,
                              "错误: --script 只接受 patrol|dodge|mixed|wait (收到 '%s')\n",
                              (i < argc) ? argv[i] : "");
                print_usage(stderr);
                return 2;
            }
        } else if (strcmp(a, "--replay-interval") == 0) {
            if (++i >= argc || !parse_i64(argv[i], &replay_interval) || replay_interval <= 0 ||
                replay_interval > 2000000000LL) {
                (void)fprintf(stderr, "错误: --replay-interval 需要正整数\n");
                print_usage(stderr);
                return 2;
            }
        } else if (strcmp(a, "--tick-log") == 0) {
            if (++i >= argc) {
                (void)fprintf(stderr, "错误: --tick-log 缺少参数\n");
                print_usage(stderr);
                return 2;
            }
            tick_log_path = argv[i];
        } else {
            (void)fprintf(stderr, "错误: 未知参数 '%s'\n", a);
            print_usage(stderr);
            return 2;
        }
    }

    if (strcmp(config_name, "default") != 0) {
        (void)fprintf(stderr,
                      "错误: --config 目前只接受 'default' (配置版本 1), 收到 '%s'\n",
                      config_name);
        return 2;
    }

    /* 配置: core 唯一来源 */
    DemoConfig cfg;
    char err[256];
    if (!demo_config_init(&cfg)) {
        (void)fprintf(stderr, "错误: demo_config_init 失败\n");
        return 1;
    }
    if (!seed_given) {
        seed = (uint64_t)cfg.seed_default;
    }
    if (max_ticks_given) {
        cfg.max_ticks = (int32_t)max_ticks_arg;
    }
    if (!demo_config_validate(&cfg, err, sizeof(err))) {
        (void)fprintf(stderr, "错误: 配置校验失败: %s\n", err);
        return 1;
    }

    const char *config_version = demo_config_version_string();
    const char *bot_version = student_bot_policy_version(cfg.student_bot_policy);
    if (bot_version == NULL) {
        bot_version = "(unavailable)";
    }

    SimLogger *lg = sim_log_open(log_path);
    if (lg == NULL) {
        /* 打开失败: 不改变模拟, 仅丧失文件证据; 继续以 stdout(NULL 句柄) 记录。 */
        (void)fprintf(stderr, "警告: 日志文件无法打开, 本次仅输出 stdout\n");
    }
    SimLogger *tick_lg = lg;
    if (tick_log_path != NULL) {
        tick_lg = sim_log_open(tick_log_path);
        if (tick_lg == NULL) {
            (void)fprintf(stderr, "警告: --tick-log 无法打开, 逐 tick 日志并入主日志\n");
            tick_lg = lg;
        }
    }

    FILE *replay = NULL;
    if (replay_path != NULL) {
        replay = fopen(replay_path, "w");
        if (replay == NULL) {
            (void)fprintf(stderr, "警告: --replay 无法打开 '%s' (errno=%d), 继续模拟\n",
                          replay_path, errno);
        }
    }

    sim_log_header(lg, SIM_BUILD_ID, config_version, bot_version, seed, scenario);
    sim_log_line(lg, "HEADER script=%s config=%s log=%s replay=%s", script_name(script),
                 config_name, (log_path != NULL) ? log_path : "(stdout)",
                 (replay_path != NULL) ? replay_path : "(none)");

    World world;
    if (!world_reset(&world, &cfg, seed)) {
        (void)fprintf(stderr, "错误: world_reset 失败 (配置非法或指针为空)\n");
        sim_log_close(lg);
        if (tick_lg != lg) {
            sim_log_close(tick_lg);
        }
        if (replay != NULL) {
            (void)fclose(replay);
        }
        return 1;
    }

    if (replay != NULL) {
        write_replay_header(replay, SIM_BUILD_ID, config_version, bot_version, seed, scenario);
        write_replay_line(replay, &world);
    }

    sim_log_line(lg, "INFO max_ticks=%d replay_interval=%ld effective_tick_limit=%d",
                 cfg.max_ticks, (long)replay_interval,
                 (cfg.max_ticks > 0) ? cfg.max_ticks : SIM_SAFETY_MAX_TICKS);

    SimTally tally;
    memset(&tally, 0, sizeof(tally));
    ScriptState script_state;
    memset(&script_state, 0, sizeof(script_state));

    const int32_t tick_limit =
        (cfg.max_ticks > 0) ? cfg.max_ticks : (int32_t)SIM_SAFETY_MAX_TICKS;

    while (world.status == DEMO_STATUS_RUNNING && !world.truncated && world.tick < tick_limit) {
        BossInput in;
        script_fill_input(&world, script, &script_state, &in);
        count_requests(&in, &tally);
        world_step(&world, &in);
        process_events(&world, &tally, tick_lg);
        if (tick_log_path != NULL) {
            log_tick_line(tick_lg, &world);
        }
        if (replay != NULL && replay_interval > 0 &&
            (world.tick % (int32_t)replay_interval) == 0) {
            write_replay_line(replay, &world);
        }
    }

    const bool truncated = (world.status == DEMO_STATUS_RUNNING);
    if (truncated) {
        sim_log_line(lg,
                     "TRUNCATED/未完成 tick=%d limit=%d (未分胜负; 不记为胜或负)", world.tick,
                     tick_limit);
    } else {
        sim_log_line(lg, "FINAL status=%s tick=%d boss_hp=%d alive=%u/%u", status_name(world.status),
                     world.tick, world.boss.hp, world.student_count - tally.students_down,
                     world.student_count);
    }
    if (world.truncated && !truncated) {
        sim_log_line(lg, "NOTE world 已到达 cfg.max_ticks=%d 的截断标记 (状态已判定)", cfg.max_ticks);
    }
    if (tally.overflow_events > 0u || world.spawn_overflow_count > 0u) {
        sim_log_line(lg, "WARN spawn_overflow events=%u world_counter=%u", tally.overflow_events,
                     world.spawn_overflow_count);
    }

    /* 权威计数取自 world (事件计数作为交叉核对) */
    tally.boss_hits = world.boss_hits_taken;
    tally.student_hits = world.student_hits_taken;
    tally.accepts = world.attack_accept_count;
    tally.rejects = world.attack_reject_count;

    sim_log_line(lg,
                 "TALLY tick=%d accepts=%u rejects=%u reject_no_energy=%u reject_busy=%u "
                 "reject_no_target=%u reject_no_request=%u",
                 world.tick, tally.accepts, tally.rejects,
                 tally.reject_by_reason[DEMO_REJECT_NO_ENERGY],
                 tally.reject_by_reason[DEMO_REJECT_BUSY],
                 tally.reject_by_reason[DEMO_REJECT_NO_TARGET],
                 tally.reject_by_reason[DEMO_REJECT_NO_REQUEST]);
    sim_log_line(lg,
                 "TALLY boss_hits=%u student_hits=%u students_down=%u boss_down=%u "
                 "boss_bullets=%u student_bullets=%u events_dropped=%u",
                 tally.boss_hits, tally.student_hits, tally.students_down, tally.boss_down,
                 world.boss_bullets_spawned, world.student_bullets_spawned,
                 world.events.dropped);
    for (int p = 0; p < (int)DEMO_PATTERN_COUNT; ++p) {
        sim_log_line(lg, "TALLY pattern=%d(%s) requests=%u", p, pattern_label((DemoPattern)p),
                     tally.requests_by_pattern[p]);
    }

    if (truncated) {
        sim_log_line(lg, "RESULT status=TRUNCATED tick=%d accepts=%u rejects=%u boss_hits=%u "
                         "student_hits=%u",
                     world.tick, tally.accepts, tally.rejects, tally.boss_hits, tally.student_hits);
    } else {
        sim_log_line(lg, "RESULT status=%s tick=%d accepts=%u rejects=%u boss_hits=%u "
                         "student_hits=%u",
                     status_name(world.status), world.tick, tally.accepts, tally.rejects,
                     tally.boss_hits, tally.student_hits);
    }

    if (replay != NULL) {
        write_replay_line(replay, &world);
        (void)fclose(replay);
    }
    sim_log_close(lg);
    if (tick_lg != lg) {
        sim_log_close(tick_lg);
    }
    return 0;
}
