/* apps/desktop/game_main.cpp - 原生兼容图形调试入口（历史界面接口 v2）
 * 运行配置读取共享核心；完整预瞄交互由 apps/web 提供。
 *
 * 职责: 创建窗口、固定步累积器、暂停/重开/退出状态、调用 world_step、调用渲染层。
 * 不实现任何游戏规则: 移动、能量、出招、碰撞、终局都在 core 内; 渲染只读 WorldView。
 * 输入由 apps/desktop/platform/input_win.cpp 提供。
 */
#include <graphics.h>

#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include "demo_base.h"
#include "hud.h"
#include "patterns.h"
#include "world.h"
}
#include "input_win.h"

void scene_render(const WorldView *view, const DemoConfig *cfg);

namespace {

constexpr int kWindowW = 1280;
constexpr int kWindowH = 720;

const char *status_text(DemoWorldStatus s) {
    switch (s) {
        case DEMO_STATUS_RUNNING: return "RUNNING";
        case DEMO_STATUS_BOSS_WIN: return "BOSS_WIN";
        case DEMO_STATUS_BOSS_LOSE: return "BOSS_LOSE";
        case DEMO_STATUS_DRAW: return "DRAW";
        default: return "UNKNOWN";
    }
}

void print_banner(const DemoConfig &cfg) {
    std::printf("==================================================\n");
    std::printf("  科大弹幕录: 原生兼容调试入口\n");
    std::printf("  操作: 鼠标移动 Boss(朝指针方向, 越近越慢)\n");
    std::printf("        也可用 WASD/方向键; 1/2/3/4 按键直接出招\n");
    std::printf("        Esc 或鼠标右键 暂停/继续; R 重开\n");
    std::printf("  目标: 清波后继续派出学生; Boss 生命耗尽结束, 无胜利终点\n");
    std::printf("  能量: 共享能量按逻辑时间恢复, 接受一次出招扣一次\n");
    std::printf("  费用: 1=%d  2=%d  3=%d  4=%d 能量\n", cfg.patterns[0].cost,
                cfg.patterns[1].cost, cfg.patterns[2].cost, cfg.patterns[3].cost);
    std::printf("  说明: 脚本 AI 未训练; 完整预瞄及 GPA 展示请使用网页 Demo\n");
    std::printf("  学生数=%u  Boss血量=%d  能量上限=%d\n", cfg.student_count, cfg.boss_hp,
                cfg.energy_max);
    std::printf("  运行配置=%s  原生兼容界面 (历史接口 v2)\n", demo_config_version_string());
    std::printf("  按 1/2/3/4 或 Esc 开始\n");
    std::printf("==================================================\n");
}

}  // namespace

int main() {
    DemoConfig cfg;
    if (!demo_config_init(&cfg)) {
        std::printf("配置初始化失败\n");
        return 1;
    }
    char err[256];
    if (!demo_config_validate(&cfg, err, sizeof(err))) {
        std::printf("配置非法: %s\n", err);
        return 1;
    }

    print_banner(cfg);

    initgraph(kWindowW, kWindowH, EX_SHOWCONSOLE);
    if (GetHWnd() == nullptr) {
        std::printf("EasyX 窗口创建失败\n");
        return 1;
    }
    setbkcolor(RGB(12, 16, 28));
    cleardevice();
    BeginBatchDraw();

    World world;
    uint64_t seed = cfg.seed_default;
    if (!world_reset(&world, &cfg, seed)) {
        std::printf("world_reset 失败\n");
        EndBatchDraw();
        closegraph();
        return 1;
    }

    bool intro = true;
    bool paused = true;
    double accumulator = 0.0;
    const double kDt = 1.0 / (double)DEMO_TICKS_PER_SECOND;
    DWORD last = GetTickCount();
    int frames = 0;
    int total_ticks = 0;

    while (!demo_platform::input_quit_requested()) {
        DWORD now = GetTickCount();
        double elapsed = (double)(now - last) / 1000.0;
        last = now;
        if (elapsed > 0.25) {
            elapsed = 0.25; /* 掉帧不扩大 dt */
        }

        demo_platform::input_poll();

        if (intro) {
            if (demo_platform::input_pause_edge_consume() ||
                demo_platform::input_any_attack_edge()) {
                demo_platform::input_clear_edges();
                intro = false;
                paused = false;
                accumulator = 0.0;
            }
        } else if (demo_platform::input_pause_edge_consume()) {
            paused = !paused;
            accumulator = 0.0; /* 恢复不补暂停时间 */
        }

        if (demo_platform::input_restart_edge_consume()) {
            World fresh;
            if (world_reset(&fresh, &cfg, seed)) {
                world = fresh;
                accumulator = 0.0;
                paused = false;
                intro = false;
                std::printf("[重开] seed=%llu tick=0\n", (unsigned long long)seed);
            }
        }

        if (!intro && !paused && world.status == DEMO_STATUS_RUNNING) {
            accumulator += elapsed;
            int steps = 0;
            const int kMaxSteps = 8;
            bool first = true;
            while (accumulator >= kDt && steps < kMaxSteps) {
                BossInput in;
                demo_platform::input_take(&in, first);
                first = false;
                world_step(&world, &in);
                accumulator -= kDt;
                steps++;
                total_ticks++;
                if (world.status != DEMO_STATUS_RUNNING) {
                    break;
                }
            }
            if (steps >= kMaxSteps) {
                accumulator = 0.0; /* 落后太多时丢弃积压, 不扩大 dt */
            }
        } else {
            /* 暂停或终局: 丢弃出招边沿, 避免恢复后突然执行 */
            demo_platform::input_clear_edges();
        }

        WorldView view;
        world_make_view(&world, &view);

        cleardevice();
        if (intro) {
            hud_draw_intro();
        } else {
            scene_render(&view, &cfg);
            HudInput hud;
            hud.view = &view;
            hud.config = &cfg;
            hud.paused = paused;
            if (hud_is_finished(&hud)) {
                hud_draw_panel(&hud);
                hud_draw_result(&hud);
            } else {
                hud_draw_all(&hud);
            }
        }
        FlushBatchDraw();
        frames++;
        Sleep(1);
    }

    std::printf("[退出] 帧数=%d 逻辑tick=%d 状态=%s 接受=%u 拒绝=%u Boss受击=%u 学生受击=%u\n",
                frames, total_ticks, status_text(world.status), world.attack_accept_count,
                world.attack_reject_count, world.boss_hits_taken, world.student_hits_taken);

    EndBatchDraw();
    closegraph();
    return 0;
}
