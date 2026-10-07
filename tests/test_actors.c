/* test_actors.c - S04 角色移动与生命的验收测试
 *
 * 覆盖任务卡的全部 9 条验收条件, 外加"必须保持的行为"中未被前 9 条覆盖的
 * 非正参数分支。每条子检查打印 PASS/FAIL, 每个项目打印汇总, 末尾打印总计。
 *
 * 编译(见任务卡):
 *   gcc -std=c11 -Wall -Wextra -I <repo>/core tests/test_actors.c core/actors.c -o test_actors.exe
 * 退出码: 全部通过为 0, 任一失败为 1。
 *
 * 本文件只使用已冻结接口 core/demo_base.h 中的 4 个 actor 函数。
 */
#include "demo_base.h"

#include <math.h>
#include <stdio.h>

/* ---------------------------------------------------------------- 测试框架 */

static int g_checks = 0;
static int g_failed = 0;
static int g_item_failed = 0; /* 当前项目的失败计数 */
static int g_item_checks = 0;

static void check(bool ok, const char *name)
{
    g_checks += 1;
    g_item_checks += 1;
    if (!ok) {
        g_failed += 1;
        g_item_failed += 1;
    }
    printf("    [%s] %s\n", ok ? "PASS" : "FAIL", name);
}

static void check_near(float got, float want, float eps, const char *name)
{
    float diff = got - want;
    bool ok;

    if (diff < 0.0f) {
        diff = -diff;
    }
    ok = (diff <= eps) && isfinite(got);
    if (ok) {
        printf("    [PASS] %s (got=%.9g want=%.9g)\n", name, (double)got, (double)want);
        g_checks += 1;
        g_item_checks += 1;
    } else {
        printf("    [FAIL] %s (got=%.9g want=%.9g diff=%.9g eps=%.9g)\n", name, (double)got,
               (double)want, (double)diff, (double)eps);
        g_checks += 1;
        g_failed += 1;
        g_item_checks += 1;
        g_item_failed += 1;
    }
}

static void item_begin(const char *title)
{
    g_item_checks = 0;
    g_item_failed = 0;
    printf("\n== %s ==\n", title);
}

static void item_end(const char *title)
{
    printf("  -> %s: %s (%d 子项, %d 失败)\n", title, (g_item_failed == 0) ? "PASS" : "FAIL",
           g_item_checks, g_item_failed);
}

/* ---------------------------------------------------------------- 构造工具 */

static float len2(float x, float y)
{
    return sqrtf(x * x + y * y);
}

static Actor make_actor(float x, float y, int32_t hp, bool alive, int32_t invuln)
{
    Actor a;

    a.id = 1u;
    a.alive = alive;
    a.x = x;
    a.y = y;
    a.radius = 3.0f;
    a.hp = hp;
    a.hp_max = hp > 0 ? hp : 3;
    a.invuln_ticks = invuln;
    return a;
}

/* 边界与速度常量取自 docs/demo-rules.md §2.1 已批准的 Boss 移动区域与试验数值。
 * 本测试只验证"传入 min/max 即被夹紧", 不假定任何特定区域;
 * 采用批准值是为了让失败信息可直接与规则对照。 */
#define FIELD_MIN_X 22.0f  /* Boss 区域 x ∈ [22, 938] */
#define FIELD_MAX_X 938.0f
#define FIELD_MIN_Y 22.0f  /* Boss 区域 y ∈ [22, 698] */
#define FIELD_MAX_Y 698.0f
#define BOSS_SPEED 270.0f  /* px/s, 已批准 */
#define STUDENT_SPEED 150.0f
#define POINTER_DEADZONE 12.0f /* px, docs/demo-rules.md §2.1 */

static void move(Actor *a, float dx, float dy, float speed, float dt)
{
    actor_apply_move(a, dx, dy, speed, dt, FIELD_MIN_X, FIELD_MAX_X, FIELD_MIN_Y, FIELD_MAX_Y);
}

static void toward(Actor *a, float tx, float ty, float speed, float dt, float deadzone)
{
    actor_move_toward(a, tx, ty, speed, dt, deadzone, FIELD_MIN_X, FIELD_MAX_X, FIELD_MIN_Y,
                      FIELD_MAX_Y);
}

/* ---------------------------------------------------------------- 1 松开停止 */

static void test_1_release_stop(void)
{
    Actor a = make_actor(480.0f, 400.0f, 3, true, 0);
    float x0 = a.x;
    float y0 = a.y;

    item_begin("1) 松开停止: dir=(0,0) 位置不变");
    move(&a, 0.0f, 0.0f, BOSS_SPEED, 1.0f / 60.0f);
    check(a.x == x0 && a.y == y0, "零向量不改变 x/y");

    /* 极端速度下零向量仍必须停止 */
    move(&a, 0.0f, 0.0f, 1.0e9f, 100.0f);
    check(a.x == x0 && a.y == y0, "零向量在极端 speed/dt 下仍不动");

    /* 极小的非零向量表示极慢速, 不能被当作零 */
    move(&a, 1.0e-3f, 0.0f, 60.0f, 1.0f);
    check(a.x > x0, "非零小幅度向量确实产生移动(慢速语义)");
    item_end("1 松开停止");
}

/* ---------------------------------------------------------------- 2 斜向同速 */

static void test_2_diagonal_speed(void)
{
    Actor straight = make_actor(200.0f, 300.0f, 3, true, 0);
    Actor diag = make_actor(200.0f, 300.0f, 3, true, 0);
    float speed = BOSS_SPEED;
    float dt = 1.0f / 60.0f;
    float dx1;
    float dy1;
    float dx2;
    float dy2;
    float d1;
    float d2;
    const float k = 0.70710678f;

    item_begin("2) 斜向同速: (1,0) 与 (0.70710678,0.70710678) 位移长度相等");
    move(&straight, 1.0f, 0.0f, speed, dt);
    move(&diag, k, k, speed, dt);
    dx1 = straight.x - 200.0f;
    dy1 = straight.y - 300.0f;
    dx2 = diag.x - 200.0f;
    dy2 = diag.y - 300.0f;
    d1 = len2(dx1, dy1);
    d2 = len2(dx2, dy2);

    check_near(d1, speed * dt, 1.0e-3f, "直向位移 = speed*dt");
    /* 归一化后长度恰为 1 时位移大小 = speed*dt */
    check_near(d2, speed * dt, 1.0e-3f, "斜向位移 = speed*dt");
    check_near(d2 - d1, 0.0f, 1.0e-3f, "斜向与直向位移差 <= 1e-3");

    /* 归一化方向分量: 22.5 度斜向 (非 45 度) 同样同速 */
    {
        Actor s2 = make_actor(200.0f, 300.0f, 3, true, 0);
        float c = 0.92387953f; /* cos(22.5deg) */
        float s = 0.38268343f; /* sin(22.5deg) */
        move(&s2, c, s, speed, dt);
        check_near(len2(s2.x - 200.0f, s2.y - 300.0f), speed * dt, 1.0e-3f, "22.5 度斜向同速");
    }

    /* 长度 > 1 的输入必须归一化到 1, 不能超速 */
    {
        Actor big = make_actor(200.0f, 300.0f, 3, true, 0);
        move(&big, 3.0f, 4.0f, speed, dt); /* 长度 5 */
        check_near(len2(big.x - 200.0f, big.y - 300.0f), speed * dt, 1.0e-3f, "长度>1 归一化到 1");
    }

    /* 极端有限长度: x*x+y*y 会溢出/下溢, 但语义必须仍然成立。
     * (1e30,1e30) 的朴素平方和是 INF; (1e-30,1e-30) 的朴素平方和下溢为 0。
     * 下溢只影响"是否被误判为零向量", 位移量本身远小于 float 精度:
     * 在坐标 200 处 4.5e-30 会被舍入掉, 所以放在原点附近、边界 0 处观察。 */
    {
        Actor huge = make_actor(200.0f, 300.0f, 3, true, 0);
        Actor tiny = make_actor(0.0f, 0.0f, 3, true, 0);

        move(&huge, 1.0e30f, 1.0e30f, speed, dt);
        check(isfinite(huge.x) != 0 && isfinite(huge.y) != 0, "极大有限向量坐标保持有限");
        check_near(len2(huge.x - 200.0f, huge.y - 300.0f), speed * dt, 1.0e-2f,
                   "极大有限向量仍归一化为满速(不因溢出被当成不动)");

        /* 无需半径留边的临时边界, 以便观察极小位移 */
        actor_apply_move(&tiny, 1.0e-30f, 1.0e-30f, speed, dt, 0.0f, 1.0e9f, 0.0f, 1.0e9f);
        check(tiny.x > 0.0f && tiny.y > 0.0f,
              "极小有限非零向量仍产生移动(不因下溢被当成零向量)");
        check(isfinite(tiny.x) != 0 && isfinite(tiny.y) != 0, "极小有限向量坐标保持有限");

        /* 对照: 真正的零向量在同样边界下必须完全不动 */
        {
            Actor zero = make_actor(0.0f, 0.0f, 3, true, 0);
            actor_apply_move(&zero, 0.0f, 0.0f, speed, dt, 0.0f, 1.0e9f, 0.0f, 1.0e9f);
            check(zero.x == 0.0f && zero.y == 0.0f, "零向量与极小向量行为可区分");
        }
    }
    item_end("2 斜向同速");
}

/* ---------------------------------------------------------------- 3 摇杆幅度 */

static void test_3_joystick_magnitude(void)
{
    Actor half = make_actor(200.0f, 300.0f, 3, true, 0);
    Actor full = make_actor(200.0f, 300.0f, 3, true, 0);
    float speed = BOSS_SPEED;
    float dt = 1.0f / 60.0f;

    item_begin("3) 摇杆幅度: dir=(0.5,0) 位移 = speed*dt*0.5");
    move(&half, 0.5f, 0.0f, speed, dt);
    check_near(half.x - 200.0f, speed * dt * 0.5f, 1.0e-4f, "半幅位移 = speed*dt*0.5");
    check(half.y == 300.0f, "半幅不产生 y 位移");

    move(&full, 1.0f, 0.0f, speed, dt);
    check_near(full.x - 200.0f, speed * dt, 1.0e-4f, "满幅位移 = speed*dt");
    check_near((full.x - 200.0f) / (half.x - 200.0f), 2.0f, 1.0e-3f, "满幅位移是半幅的 2 倍");

    /* 幅度 1.5 与 1.0 结果相同(归一化上界) */
    {
        Actor over = make_actor(200.0f, 300.0f, 3, true, 0);
        move(&over, 1.5f, 0.0f, speed, dt);
        check_near(over.x - full.x, 0.0f, 1.0e-4f, "幅度>1 与幅度=1 位移相同");
    }
    item_end("3 摇杆幅度");
}

/* ---------------------------------------------------------------- 4 边界夹紧 */

static void test_4_boundary(void)
{
    Actor left = make_actor(100.0f, 400.0f, 3, true, 0);
    Actor right = make_actor(800.0f, 400.0f, 3, true, 0);
    Actor top = make_actor(400.0f, 250.0f, 3, true, 0);
    Actor bottom = make_actor(400.0f, 600.0f, 3, true, 0);
    Actor corner = make_actor(FIELD_MIN_X, FIELD_MIN_Y, 3, true, 0);
    Actor corner2 = make_actor(FIELD_MAX_X, FIELD_MAX_Y, 3, true, 0);

    item_begin("4) 边界不越界: 朝边界外移动后等于夹紧值; 角落斜向出界两轴都夹紧");
    move(&left, -1.0f, 0.0f, 1000.0f, 1.0f);
    check(left.x == FIELD_MIN_X, "左边界夹紧到 min_x");
    check(left.y == 400.0f, "左边界移动不改变 y");

    move(&right, 1.0f, 0.0f, 1000.0f, 1.0f);
    check(right.x == FIELD_MAX_X, "右边界夹紧到 max_x");

    move(&top, 0.0f, -1.0f, 1000.0f, 1.0f);
    check(top.y == FIELD_MIN_Y, "上边界夹紧到 min_y");

    move(&bottom, 0.0f, 1.0f, 1000.0f, 1.0f);
    check(bottom.y == FIELD_MAX_Y, "下边界夹紧到 max_y");

    /* 从角落斜向出界: 两个轴都夹紧 */
    move(&corner, -1.0f, -1.0f, 1000.0f, 1.0f);
    check(corner.x == FIELD_MIN_X && corner.y == FIELD_MIN_Y, "左下角斜向出界两轴都夹紧");

    move(&corner2, 1.0f, 1.0f, 1000.0f, 1.0f);
    check(corner2.x == FIELD_MAX_X && corner2.y == FIELD_MAX_Y, "右上角斜向出界两轴都夹紧");

    /* 反复越界不累积穿出 */
    {
        int i;
        for (i = 0; i < 10; i += 1) {
            move(&right, 1.0f, 0.0f, BOSS_SPEED, 1.0f / 60.0f);
        }
        check(right.x == FIELD_MAX_X, "连续多 tick 越界不穿出");

        for (i = 0; i < 10; i += 1) {
            move(&corner, -1.0f, -1.0f, BOSS_SPEED, 1.0f / 60.0f);
        }
        check(corner.x == FIELD_MIN_X && corner.y == FIELD_MIN_Y, "角落连续越界不穿出");
    }
    item_end("4 边界夹紧");
}

/* ---------------------------------------------------------------- 5 非有限输入 */

static void test_5_nonfinite(void)
{
    float nan_v = (float)NAN;
    float inf_v = (float)INFINITY;
    Actor a = make_actor(480.0f, 400.0f, 3, true, 0);
    Actor b = make_actor(480.0f, 400.0f, 3, true, 0);

    item_begin("5) 非有限输入: dir=(NaN,0)、dir=(INF,0) 不动且坐标保持有限");
    move(&a, nan_v, 0.0f, BOSS_SPEED, 1.0f / 60.0f);
    check(a.x == 480.0f && a.y == 400.0f, "dir=(NaN,0) 位置不变");
    check(isfinite(a.x) != 0 && isfinite(a.y) != 0, "dir=(NaN,0) 坐标保持有限");

    move(&b, inf_v, 0.0f, BOSS_SPEED, 1.0f / 60.0f);
    check(b.x == 480.0f && b.y == 400.0f, "dir=(+INF,0) 位置不变");
    check(isfinite(b.x) != 0 && isfinite(b.y) != 0, "dir=(+INF,0) 坐标保持有限");

    /* 各分量组合与 y 轴 */
    {
        Actor c = make_actor(300.0f, 300.0f, 3, true, 0);
        move(&c, 0.0f, nan_v, BOSS_SPEED, 1.0f / 60.0f);
        check(c.x == 300.0f && c.y == 300.0f, "dir=(0,NaN) 位置不变");
        move(&c, -inf_v, inf_v, BOSS_SPEED, 1.0f / 60.0f);
        check(c.x == 300.0f && c.y == 300.0f, "dir=(-INF,+INF) 位置不变");
        check(isfinite(c.x) != 0 && isfinite(c.y) != 0, "组合非有限输入坐标保持有限");
    }

    /* NULL 指针与非法当前坐标不崩溃 */
    actor_apply_move(NULL, 1.0f, 0.0f, BOSS_SPEED, 1.0f / 60.0f, FIELD_MIN_X, FIELD_MAX_X,
                     FIELD_MIN_Y, FIELD_MAX_Y);
    check(true, "NULL Actor 调用不崩溃");
    {
        Actor bad = make_actor(nan_v, 400.0f, 3, true, 0);
        move(&bad, 1.0f, 0.0f, BOSS_SPEED, 1.0f / 60.0f);
        check(isnan(bad.x) != 0, "已损坏坐标不被改写成其他 NaN/越界值");
    }
    item_end("5 非有限输入");
}

/* ---------------------------------------------------------------- 6 move_toward */

static void test_6_move_toward(void)
{
    float speed = BOSS_SPEED;
    float dt = 1.0f / 60.0f;

    item_begin("6) move_toward: 远距离/一步内/deadzone/目标出界/目标等于自身");

    /* 远距离: 按 speed*dt 前进, 方向正确 */
    {
        Actor a = make_actor(200.0f, 400.0f, 3, true, 0);
        toward(&a, 900.0f, 400.0f, speed, dt, 1.0f);
        check_near(a.x - 200.0f, speed * dt, 1.0e-4f, "远距离每 tick 前进 speed*dt");
        check_near(a.y, 400.0f, 1.0e-6f, "远距离不产生 y 偏移");
    }

    /* 斜向远距离: 位移长度为 speed*dt */
    {
        Actor a = make_actor(200.0f, 400.0f, 3, true, 0);
        toward(&a, 900.0f, 690.0f, speed, dt, 1.0f);
        check_near(len2(a.x - 200.0f, a.y - 400.0f), speed * dt, 1.0e-4f,
                   "斜向远距离位移长度 = speed*dt");
    }

    /* 一步走不完: 剩余距离小于 speed*dt 时停在目标上, 不越过 */
    {
        Actor a = make_actor(500.0f, 400.0f, 3, true, 0);
        toward(&a, 503.0f, 400.0f, speed, dt, 0.0f);
        check_near(a.x, 503.0f, 1.0e-5f, "小于一步时停在目标 x 上");
        check_near(a.y, 400.0f, 1.0e-6f, "小于一步时 y 不变");
        check(a.x <= 503.0f, "不越过目标");

        toward(&a, 503.0f, 400.0f, speed, dt, 0.0f);
        check_near(a.x, 503.0f, 1.0e-5f, "到达后再次调用不再移动");
    }

    /* 斜向小于一步: 不越过目标点 */
    {
        Actor a = make_actor(500.0f, 400.0f, 3, true, 0);
        toward(&a, 501.0f, 401.0f, speed, dt, 0.0f);
        check_near(a.x, 501.0f, 1.0e-4f, "斜向小于一步停在目标 x");
        check_near(a.y, 401.0f, 1.0e-4f, "斜向小于一步停在目标 y");
    }

    /* deadzone 内不动 */
    {
        Actor a = make_actor(500.0f, 400.0f, 3, true, 0);
        toward(&a, 500.5f, 400.5f, speed, dt, 5.0f);
        check(a.x == 500.0f && a.y == 400.0f, "deadzone 内完全不移动");

        toward(&a, 504.9f, 400.0f, speed, dt, 5.0f);
        check(a.x == 500.0f, "恰好小于 deadzone 仍不动");

        /* 边界语义: 任务卡规定 "剩余距离 <= deadzone 视为到达"。
         * 注意 docs/demo-rules.md §1.2 的措辞是"距离小于死区时视为已到达"。
         * 本实现采用任务卡的 <= (即恰等于死区也算到达), 此处固定该边界以便变更被发现。 */
        toward(&a, 505.0f, 400.0f, speed, dt, 5.0f);
        check(a.x == 500.0f, "恰好等于 deadzone 视为到达(任务卡 <= 语义)");

        /* 死区外半步: 仍走一步 (speed*dt = 4.5 px), 落入死区后停止并保持稳定 */
        toward(&a, 505.5f, 400.0f, speed, dt, 5.0f);
        check_near(a.x, 504.5f, 1.0e-4f, "死区外半步走满一步后落入死区");
        {
            float settled = a.x;
            toward(&a, 505.5f, 400.0f, speed, dt, 5.0f);
            check(a.x == settled, "落入死区后不再移动(防抖动)");
            check_near(505.5f - a.x, 1.0f, 1.0e-4f, "停在距目标 1 px 处, 未越过目标");
        }

        toward(&a, 506.0f, 400.0f, speed, dt, 5.0f);
        check(a.x > 500.0f, "超出 deadzone 后开始移动");
    }

    /* 用 docs/demo-rules.md §2.1 批准的指针死区 12 px 复核"指针到位置"手感:
     * 离指针近则慢、抵达(进入死区)即停。
     * 注意: 270 px/s 在 dt=1/60 下每 tick 只走 4.5 px, 小于 12 px 死区,
     * 因此角色不会精确停在指针上, 而是在进入死区后停住 —— 这正是防抖动的预期行为。 */
    {
        Actor a = make_actor(480.0f, 620.0f, 6, true, 0); /* Boss 初始位置 */
        float pointer_x = 480.0f + POINTER_DEADZONE + 1.0f; /* 距指针 13 px, 在死区外 */
        int i;

        toward(&a, 480.0f + (POINTER_DEADZONE - 1.0f), 620.0f, speed, dt, POINTER_DEADZONE);
        check(a.x == 480.0f && a.y == 620.0f, "指针距离 < 12 px 死区时不动");

        /* 死区外一步: 只走 speed*dt, 不会瞬移 */
        toward(&a, pointer_x, 620.0f, speed, dt, POINTER_DEADZONE);
        check_near(a.x - 480.0f, speed * dt, 1.0e-4f, "死区外一步只移动 speed*dt");
        check_near(a.y, 620.0f, 1.0e-6f, "指针到位置移动不产生横向漂移");
        check(len2(pointer_x - a.x, 0.0f) <= POINTER_DEADZONE, "一步后已进入死区, 视为抵达");

        /* 已抵达: 继续调用不再移动, 也不往复抖动 */
        {
            float settled = a.x;
            for (i = 0; i < 200; i += 1) {
                toward(&a, pointer_x, 620.0f, speed, dt, POINTER_DEADZONE);
            }
            check(a.x == settled, "抵达死区后 200 tick 完全不移动(无抖动)");
            check(len2(pointer_x - a.x, 0.0f) <= POINTER_DEADZONE, "停在死区之内");
        }

        /* 远离指针: 全速 270 px/s; "离指针近则慢"来自剩余距离而非本函数限速 */
        {
            Actor b = make_actor(480.0f, 620.0f, 6, true, 0);
            toward(&b, 938.0f, 620.0f, speed, dt, POINTER_DEADZONE);
            check_near(b.x - 480.0f, speed * dt, 1.0e-4f, "远离指针时全速前进");
        }

        /* 单调收敛: 逐 tick 逼近指针, 剩余距离单调不增且最终停止 */
        {
            Actor c = make_actor(100.0f, 620.0f, 6, true, 0);
            float target = 900.0f;
            float prev = len2(target - c.x, 0.0f);
            bool monotone = true;
            int moved_ticks = 0;
            for (i = 0; i < 400; i += 1) {
                toward(&c, target, 620.0f, speed, dt, POINTER_DEADZONE);
                {
                    float now = len2(target - c.x, 0.0f);
                    if (now > prev) {
                        monotone = false;
                    }
                    if (now < prev) {
                        moved_ticks += 1;
                    }
                    prev = now;
                }
            }
            check(monotone, "剩余距离单调不增(不抖动、不越过)");
            check(prev <= POINTER_DEADZONE, "最终停在死区之内");
            check(moved_ticks > 0, "确实发生了移动");
            check(c.x <= target, "从不越过目标点");
        }
    }

    /* 目标在边界外: 被夹紧, 且不振荡 */
    {
        Actor a = make_actor(200.0f, 400.0f, 3, true, 0);
        int i;
        for (i = 0; i < 1000; i += 1) {
            toward(&a, FIELD_MIN_X - 500.0f, FIELD_MIN_Y - 500.0f, speed, dt, 1.0f);
        }
        check(a.x == FIELD_MIN_X, "目标在边界外时 x 夹紧到 min_x");
        check(a.y == FIELD_MIN_Y, "目标在边界外时 y 夹紧到 min_y");
    }

    /* 目标恰等于当前坐标: 不除零, 不产生 NaN */
    {
        Actor a = make_actor(300.0f, 300.0f, 3, true, 0);
        toward(&a, 300.0f, 300.0f, speed, dt, 0.0f);
        check(a.x == 300.0f && a.y == 300.0f, "目标等于自身时不动");
        check(isfinite(a.x) != 0 && isfinite(a.y) != 0, "目标等于自身时无 NaN");
    }

    /* 极端远的有限目标: 距离平方会溢出, 但仍应按 speed*dt 前进并被夹紧 */
    {
        Actor a = make_actor(400.0f, 400.0f, 3, true, 0);
        toward(&a, 1.0e30f, 400.0f, speed, dt, 1.0f);
        check(isfinite(a.x) != 0 && isfinite(a.y) != 0, "极远目标坐标保持有限");
        check_near(a.x - 400.0f, speed * dt, 1.0e-3f, "极远目标仍按 speed*dt 前进");

        {
            Actor b = make_actor(400.0f, 400.0f, 3, true, 0);
            int i;
            for (i = 0; i < 2000; i += 1) {
                toward(&b, 1.0e30f, 400.0f, speed, dt, 1.0f);
            }
            check(b.x == FIELD_MAX_X, "极远目标最终夹紧在右边界上");
            check(isfinite(b.x) != 0 && isfinite(b.y) != 0, "极远目标收敛后仍无 NaN");
        }
    }

    /* 非有限目标与死区 */
    {
        Actor a = make_actor(300.0f, 300.0f, 3, true, 0);
        toward(&a, (float)NAN, 300.0f, speed, dt, 1.0f);
        check(a.x == 300.0f && a.y == 300.0f, "NaN 目标不动");
        toward(&a, (float)INFINITY, 300.0f, speed, dt, 1.0f);
        check(a.x == 300.0f && a.y == 300.0f, "INF 目标不动");
        toward(&a, 400.0f, 300.0f, speed, dt, (float)NAN);
        check(a.x == 300.0f + speed * dt, "NaN deadzone 按 0 处理并正常移动");
        actor_move_toward(NULL, 400.0f, 300.0f, speed, dt, 1.0f, FIELD_MIN_X, FIELD_MAX_X,
                          FIELD_MIN_Y, FIELD_MAX_Y);
        check(true, "NULL Actor 调用不崩溃");
    }
    item_end("6 move_toward");
}

/* ---------------------------------------------------------------- 7 有效受击与无敌 */

static void test_7_damage_and_invuln(void)
{
    item_begin("7) 有效受击: 3->2; 无敌中不扣血不重置; 计时归零后可再受击");

    {
        Actor a = make_actor(400.0f, 400.0f, 3, true, 0);
        bool hit = actor_apply_damage(&a, 1);
        check(hit == true, "有效受击返回 true");
        check(a.hp == 2, "hp 从 3 扣到 2");
        check(a.alive == true, "非致命受击后仍存活");
    }

    /* 无敌期: 不扣血, 不重置计时 */
    {
        Actor a = make_actor(400.0f, 400.0f, 3, true, 90);
        bool hit = actor_apply_damage(&a, 1);
        check(hit == false, "无敌中受击返回 false");
        check(a.hp == 3, "无敌中 hp 不变");
        check(a.invuln_ticks == 90, "无敌中受击不重置 invuln_ticks");
        actor_apply_damage(&a, 100);
        check(a.hp == 3 && a.alive == true, "无敌中再大的伤害也不扣血");

        /* 计时递减到 0 后可再次受击 */
        {
            int i;
            for (i = 0; i < 90; i += 1) {
                actor_tick_timers(&a);
            }
            check(a.invuln_ticks == 0, "90 tick 后无敌计时归零");
            check(actor_apply_damage(&a, 1) == true, "无敌归零后可再次受击");
            check(a.hp == 2, "无敌归零后 hp 扣到 2");
        }
    }

    /* invuln 只降到 0: 受击本身不设置无敌(由 world 按配置设置) */
    {
        Actor a = make_actor(400.0f, 400.0f, 3, true, 0);
        actor_apply_damage(&a, 1);
        check(a.invuln_ticks == 0, "actor_apply_damage 不自行设置无敌计时");
    }

    /* damage <= 0 不扣血 */
    {
        Actor a = make_actor(400.0f, 400.0f, 3, true, 0);
        check(actor_apply_damage(&a, 0) == false, "damage=0 返回 false");
        check(actor_apply_damage(&a, -5) == false, "damage<0 返回 false");
        check(a.hp == 3, "非正伤害不扣血");
    }

    /* 多 tick 无敌窗口: 1 tick 的无敌在 tick 后失效 */
    {
        Actor a = make_actor(400.0f, 400.0f, 3, true, 1);
        check(actor_apply_damage(&a, 1) == false, "invuln=1 时仍受保护");
        actor_tick_timers(&a);
        check(a.invuln_ticks == 0, "递减 1 次即归零");
        check(actor_apply_damage(&a, 1) == true, "归零后立刻可受击");
    }

    /* NULL 指针 */
    check(actor_apply_damage(NULL, 1) == false, "NULL Actor 返回 false 不崩溃");

    /* 超额伤害不溢出且归零 */
    {
        Actor a = make_actor(400.0f, 400.0f, 3, true, 0);
        a.hp_max = 3;
        check(actor_apply_damage(&a, 1000000) == true, "超额伤害仍返回 true");
        check(a.hp == 0, "超额伤害把 hp clamp 到 0");
        check(a.alive == false, "超额伤害使角色倒下");
    }
    item_end("7 有效受击与无敌");
}

/* ---------------------------------------------------------------- 8 倒下只一次 */

static void test_8_knockdown_once(void)
{
    item_begin("8) 倒下只一次: hp 归零后 alive=false, 再次受击返回 false 且 hp 保持 0");

    {
        Actor a = make_actor(400.0f, 400.0f, 3, true, 0);
        int i;
        int hits = 0;

        for (i = 0; i < 3; i += 1) {
            if (actor_apply_damage(&a, 1)) {
                hits += 1;
            }
        }
        check(hits == 3, "3 次有效受击各返回一次 true");
        check(a.hp == 0, "hp 归零");
        check(a.alive == false, "hp 归零后 alive=false");

        /* 已经倒下: 任何后续调用都不再变化 */
        check(actor_apply_damage(&a, 1) == false, "倒下后再次受击返回 false");
        check(a.hp == 0, "倒下后 hp 保持 0");
        check(a.alive == false, "倒下后 alive 保持 false");
        check(actor_apply_damage(&a, 100) == false, "倒下后大伤害同样返回 false");
        check(a.hp == 0 && a.alive == false, "倒下后状态完全不变");

        /* 计时递减不影响已倒下角色 */
        actor_tick_timers(&a);
        check(a.alive == false, "tick_timers 不复活角色");
    }

    /* 非致命受击不倒下, 之后仍可继续扣到 0 */
    {
        Actor a = make_actor(400.0f, 400.0f, 5, true, 0);
        check(actor_apply_damage(&a, 2) == true, "5 血受 2 伤返回 true");
        check(a.hp == 3 && a.alive == true, "5 血受 2 伤剩 3 且存活");
        check(actor_apply_damage(&a, 3) == true, "再受 3 伤返回 true");
        check(a.hp == 0 && a.alive == false, "恰好扣到 0 时倒下");
        check(actor_apply_damage(&a, 1) == false, "归零后不再触发第二次倒下");
        check(a.hp == 0 && a.alive == false, "归零后状态冻结");
    }
    item_end("8 倒下只一次");
}

/* ---------------------------------------------------------------- 9 无敌计时 */

static void test_9_tick_timers(void)
{
    item_begin("9) actor_tick_timers: 递减 1, 归零后不为负");

    {
        Actor a = make_actor(400.0f, 400.0f, 3, true, 5);
        int i;

        for (i = 0; i < 5; i += 1) {
            int32_t before = a.invuln_ticks;
            actor_tick_timers(&a);
            check(a.invuln_ticks == before - 1, "每次递减恰好 1");
        }
        check(a.invuln_ticks == 0, "5 次后归零");

        for (i = 0; i < 10; i += 1) {
            actor_tick_timers(&a);
        }
        check(a.invuln_ticks == 0, "归零后继续 tick 保持 0, 不为负");

        /* 不改变其他字段 */
        check(a.x == 400.0f && a.y == 400.0f, "tick_timers 不移动位置");
        check(a.hp == 3 && a.alive == true, "tick_timers 不改变生命状态");
    }

    {
        Actor a = make_actor(400.0f, 400.0f, 3, true, 0);
        actor_tick_timers(&a);
        check(a.invuln_ticks == 0, "invuln=0 时保持 0");
    }

    actor_tick_timers(NULL);
    check(true, "NULL Actor 调用不崩溃");
    item_end("9 无敌计时");
}

/* ---------------------------------------------------------------- 附加: 非正参数 */

static void test_10_nonpositive_params(void)
{
    item_begin("10) 附加) speed<0 视为 0; dt<=0 不动 (任务卡必须保持的行为)");

    {
        Actor a = make_actor(200.0f, 300.0f, 3, true, 0);
        move(&a, 1.0f, 0.0f, -BOSS_SPEED, 1.0f / 60.0f);
        check(a.x == 200.0f && a.y == 300.0f, "speed<0 不移动");

        move(&a, 1.0f, 0.0f, 0.0f, 1.0f / 60.0f);
        check(a.x == 200.0f && a.y == 300.0f, "speed=0 不移动");

        move(&a, 1.0f, 0.0f, BOSS_SPEED, 0.0f);
        check(a.x == 200.0f && a.y == 300.0f, "dt=0 不移动");

        move(&a, 1.0f, 0.0f, BOSS_SPEED, -1.0f);
        check(a.x == 200.0f && a.y == 300.0f, "dt<0 不移动");

        move(&a, 1.0f, 0.0f, (float)NAN, 1.0f / 60.0f);
        check(a.x == 200.0f, "speed=NaN 不移动");
        move(&a, 1.0f, 0.0f, (float)INFINITY, 1.0f / 60.0f);
        check(a.x == 200.0f, "speed=INF 不移动并保持坐标有限");
        check(isfinite(a.x) && isfinite(a.y), "坐标保持有限");

        toward(&a, 900.0f, 300.0f, -BOSS_SPEED, 1.0f / 60.0f, 0.0f);
        check(a.x == 200.0f, "move_toward speed<0 不移动");
        toward(&a, 900.0f, 300.0f, BOSS_SPEED, 0.0f, 0.0f);
        check(a.x == 200.0f, "move_toward dt=0 不移动");
    }
    item_end("10 非正参数");
}

/* ---------------------------------------------------------------- 确定性 */

static void test_11_determinism(void)
{
    float speed = BOSS_SPEED;
    float dt = 1.0f / 60.0f;
    Actor a;
    Actor b;
    float ax0 = 200.0f;
    int i;

    item_begin("11) 附加) 纯函数式: 相同输入相同输出, 无隐藏状态");
    for (i = 0; i < 50; i += 1) {
        a = make_actor(ax0, 300.0f, 3, true, 0);
        b = make_actor(ax0, 300.0f, 3, true, 0);
        move(&a, 0.6f, -0.8f, speed, dt);
        move(&b, 0.6f, -0.8f, speed, dt);
        check(a.x == b.x && a.y == b.y, "两次独立调用得到完全相同坐标");
        check_near(len2(a.x - ax0, a.y - 300.0f), speed * dt, 1.0e-3f,
                   "50 次独立调用的位移长度都等于 speed*dt");
    }
    /* 角落夹紧结果在重复调用下同样稳定 */
    for (i = 0; i < 20; i += 1) {
        a = make_actor(FIELD_MIN_X, FIELD_MIN_Y, 3, true, 0);
        move(&a, -0.6f, -0.8f, speed, dt);
        check(a.x == FIELD_MIN_X && a.y == FIELD_MIN_Y, "角落越界夹紧结果稳定");
    }
    item_end("11 确定性");
}

/* ---------------------------------------------------------------- 12 Boss/学生同契约 */

static void test_12_boss_and_student(void)
{
    float dt = 1.0f / 60.0f;

    item_begin("12) 附加) Boss 与学生共用同一移动/生命契约 (批准数值: Boss 6血/270, 学生 3血/150)");

    /* Boss: 6 血, 270 px/s, 无敌 36 tick */
    {
        Actor boss = make_actor(480.0f, 620.0f, 6, true, 0);
        float x0 = boss.x;

        boss.radius = 22.0f;
        move(&boss, 1.0f, 0.0f, BOSS_SPEED, dt);
        check_near(boss.x - x0, BOSS_SPEED * dt, 1.0e-4f, "Boss 全速位移 = 270*dt");

        boss.invuln_ticks = 36;
        check(actor_apply_damage(&boss, 1) == false, "Boss 无敌 36 tick 内不扣血");
        check(boss.hp == 6 && boss.invuln_ticks == 36, "Boss 无敌期 hp 与计时都不变");

        {   /* 6 血需要 6 次有效受击才倒下 */
            int i;
            int hits = 0;
            boss.invuln_ticks = 0;
            for (i = 0; i < 6; i += 1) {
                if (actor_apply_damage(&boss, 1)) {
                    hits += 1;
                }
            }
            check(hits == 6, "Boss 6 次有效受击");
            check(boss.hp == 0 && boss.alive == false, "Boss 6 血扣到 0 后倒下");
        }
    }

    /* 学生: 3 血, 150 px/s, 无敌 18 tick */
    {
        Actor stu = make_actor(200.0f, 300.0f, 3, true, 0);
        float x0 = stu.x;

        stu.radius = 20.0f;
        move(&stu, 1.0f, 0.0f, STUDENT_SPEED, dt);
        check_near(stu.x - x0, STUDENT_SPEED * dt, 1.0e-4f, "学生全速位移 = 150*dt");

        stu.invuln_ticks = 18;
        check(actor_apply_damage(&stu, 1) == false, "学生无敌 18 tick 内不扣血");
        {
            int i;
            for (i = 0; i < 18; i += 1) {
                actor_tick_timers(&stu);
            }
        }
        check(stu.invuln_ticks == 0, "学生无敌 18 次递减后归零");
        check(actor_apply_damage(&stu, 1) == true, "学生无敌归零后可受击");
        check(stu.hp == 2, "学生 hp 3 -> 2");

        /* 倒下只发一次: 学生退出后续行动由 world 集成, 这里只保证状态冻结 */
        check(actor_apply_damage(&stu, 2) == true, "学生再受 2 伤");
        check(stu.hp == 0 && stu.alive == false, "学生 hp 归零后倒下");
        check(actor_apply_damage(&stu, 1) == false, "学生倒下后不再受击");
        check(stu.hp == 0, "学生倒下后 hp 保持 0");
    }

    /* 同速比较: 两种速度互不串扰 */
    {
        Actor boss = make_actor(400.0f, 400.0f, 6, true, 0);
        Actor stu = make_actor(400.0f, 400.0f, 3, true, 0);
        move(&boss, 0.6f, 0.8f, BOSS_SPEED, dt);
        move(&stu, 0.6f, 0.8f, STUDENT_SPEED, dt);
        check_near(len2(boss.x - 400.0f, boss.y - 400.0f), BOSS_SPEED * dt, 1.0e-4f,
                   "Boss 斜向位移 = 270*dt");
        check_near(len2(stu.x - 400.0f, stu.y - 400.0f), STUDENT_SPEED * dt, 1.0e-4f,
                   "学生斜向位移 = 150*dt");
        check(boss.hp == 6 && stu.hp == 3, "移动不改变各自 hp");
    }
    item_end("12 Boss/学生同契约");
}

/* ---------------------------------------------------------------- main */

int main(void)
{
    printf("S04 角色移动和生命 - 验收测试 (接口版本 1, core/demo_base.h)\n");

    test_1_release_stop();
    test_2_diagonal_speed();
    test_3_joystick_magnitude();
    test_4_boundary();
    test_5_nonfinite();
    test_6_move_toward();
    test_7_damage_and_invuln();
    test_8_knockdown_once();
    test_9_tick_timers();
    test_10_nonpositive_params();
    test_11_determinism();
    test_12_boss_and_student();

    printf("\n================ 汇总 ================\n");
    printf("子项总数: %d, 失败: %d\n", g_checks, g_failed);
    printf("结果: %s\n", (g_failed == 0) ? "PASS" : "FAIL");
    return (g_failed == 0) ? 0 : 1;
}
