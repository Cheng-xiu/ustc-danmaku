# 最小可玩 Boss demo：接口冻结文档

接口版本：**1**　配置版本：**1**（`demo-config-v1`）
冻结日期：2026-10-06　冻结人：母代理
权威规则来源：`docs/demo-rules.md`

本文件是并行编码前的一次性接口冻结。任何公共头文件签名的修改必须先由母代理判断，递增接口版本，更新本文件与所有受影响任务卡，完成编译检查后再恢复并行任务。子代理不得自行改签名。

---

## 1 目录与文件所有权

```text
core/       公共 C 头文件与核心实现（world、actors、attack、projectiles、collision、rng、patterns、student_fire、config）
ai/         student_bot.c，纯 C 学生脚本
platform/   input_win.cpp，Windows 输入适配（鼠标摇杆/键盘）
render/     scene.cpp、hud.cpp，只读绘制
sim/        main.c、log.c、scenarios.c、boss_baselines.c
tests/      各模块测试与共享核心回归
configs/    demo 规则配置（默认值来自 core/demo_config.c，CLI 可覆盖）
assets/     实际用到的素材与来源记录
scripts/    母代理维护的构建与打包入口
docs/       demo-rules.md、demo-interfaces.md、demo-tasks.md、demo-guide.md、demo-validation.md、demo-playtest.md、demo-findings.md
runs/       实际测试的日志与重放输入
work/agents/<任务ID>/  各任务的独立构建、日志与临时目录（不提交）
game_main.cpp  薄入口（母代理）
CMakeLists.txt 构建入口（母代理）
```

**母代理独占**：全部公共 `.h`、`core/demo_config.c`、`core/world.c`、`core/patterns.c`、`core/attack.c`、`CMakeLists.txt`、`scripts/*`、`.gitignore`、`game_main.cpp`、根 `README.md`、`docs/*` 索引类文档。

**单一写入负责人**：每个 `.c/.cpp` 在同一时间只能有一个写入者。允许只读他人文件；不得顺手修复、批量格式化或重命名。

**构建产物隔离**：每张任务卡额外授权 `work/agents/<任务ID>/build/`、`work/agents/<任务ID>/logs/`。这些目录不提交 Git，不计入源码文件额度。

---

## 2 公共头文件清单（接口 v1）

| 文件 | 内容 |
| --- | --- |
| `core/demo_base.h` | 全部基础类型、常量、`DemoConfig`、`Rng`、`Actor`、`Projectile`、碰撞、`AttackPlan`、事件、学生 AI、`RawInput`/`BossInput` |
| `core/world.h` | `World`、`WorldView`、`world_reset`、`world_step`、`world_make_view`、目标查询 |
| `core/attack.h` | `attack_try_request`、`attack_step`、`attack_busy`、`attack_progress` |
| `core/student_fire.h` | `student_fire_try`、`student_fire_ready`、`student_fire_tick_cooldown` |
| `core/pattern_ring.h` / `pattern_course.h` / `pattern_mine.h` / `pattern_shower.h` | 每招 `make_plan` / `emit` / `warning` |
| `core/patterns.h` | 招式注册表 `pattern_vtable`、`pattern_name` |
| `ai/student_bot.h` | 学生脚本策略版本字符串 |

头文件必须同时可被 C99/C11 与 C++ 包含（已用 `extern "C"` + `__cplusplus` 保护）。`demo_base.h` 需要 `infinity` 时包含 `<math.h>`。

---

## 3 冻结的接口形状

### 3.1 世界

```c
bool world_reset(World *world, const DemoConfig *config, uint64_t seed);
void world_step(World *world, const BossInput *input);
void world_make_view(const World *world, WorldView *out);
bool world_nearest_student(const World *world, DemoEntityId *out_id, float *out_x, float *out_y);
bool world_pattern_available(const World *world, DemoPattern pattern);
const StudentObservation *world_student_observation(const World *world, uint32_t index);
```

- `world_reset`：空指针或非法配置返回 `false`；成功时清除弹幕、攻击计划、目标、能量、AI 状态、无敌计时、输入边沿与全部计数，回到同一场景初始状态。
- `world_step`：`input` 可为 `NULL`（视为无输入）。终局后不再推进逻辑，只累计 `truncated` 判定。
- `world_make_view`：填充只读视图，**不暴露**未来波次与隐藏随机状态。

### 3.2 世界内部 tick 顺序（核心契约，`core/world.c` 唯一实现）

1. 消费一个 `BossInput`（同 tick 最多一个出招请求）。
2. `attack_step`：预警/攻击状态机推进；本 tick 到期的波次生成弹；攻击结束后按配置清本招 Boss 弹。
3. 应用 Boss 移动（指针到位置或键盘轴，边界夹紧）。
4. 学生的 AI 决策调度：每 `student_decision_ticks` 由 `world` 统一调用 `student_bot_choose`，动作保持到下次决策；学生移动夹紧。
5. 学生反击冷却推进与 `student_fire_try`（瞄准发射瞬间的 Boss 位置）。
6. `pool_advance`：所有弹保存本 tick 起点并积分，处理寿命与出界。
7. 碰撞结算：Boss 弹 × 学生、学生弹 × Boss，用相对运动线段扫掠、最早交点、稳定 ID 破平局；命中即移除该弹。
8. 伤害与击倒：无敌判定、扣血、倒下只发一次事件、击倒后学生退出后续行动。
9. 终局判定：全部学生倒下 → `BOSS_WIN`；Boss 血量 0 → `BOSS_LOSE`；同一 tick 两者同时发生按 `outcome_rule`（配置 1 = Boss 胜）。
10. 能量恢复（按逻辑时间，封顶）、`max_ticks` 截断判定、事件汇总到 `StepResult`。

### 3.3 出招

```c
bool attack_try_request(World *world, DemoPattern pattern, DemoRejectReason *out_reason);
void attack_step(World *world);
```

- 接受条件：不在攻击中、能量足够、存在存活目标、计划生成成功。
- 成功：一次扣能量、生成完整不可变 `AttackPlan`、进入 `WINDUP`、写 `ATTACK_ACCEPTED` 事件。
- 失败：不扣能量、不排队、不改变状态、写 `ATTACK_REJECTED` 事件，`out_reason` 给出 `DEMO_REJECT_*`。
- 同一 tick 多个请求：按招式 ID 小者优先，只接受一个。

### 3.4 招式

```c
bool pattern_make_plan(const PatternRequest *request, const DemoConfig *config, Rng *rng, AttackPlan *out);
bool pattern_emit(const AttackPlan *plan, const DemoConfig *config, uint32_t attack_tick, Rng *rng, ProjectileSpawnBuffer *out);
void pattern_warning(const AttackPlan *plan, const DemoConfig *config, PatternWarning *out);
```

- `make_plan` 在**接受请求的那一 tick** 一次生成完整几何，并把需要随机的内容固化进 `AttackPlan`（包括 `geometry_seed` 与波次时刻）。返回 `false` 表示几何无法满足，调用方回滚能量与状态。
- `emit` 只按计划生成，不重新随机、不重新选目标、不跟随 Boss 后续位置。可发放到 `ProjectileSpawnBuffer`，容量不足时 `overflow` 计数并保留明确事件。
- `warning` 只读，供渲染与 AI 使用公开预警。

### 3.5 学生 AI 与反击

```c
void student_bot_choose(const StudentObservation *obs, StudentBotState *bot, StudentAction *out);
void student_bot_init(StudentBotState *bot, uint32_t serial_seed);
bool student_fire_try(const DemoConfig *cfg, Actor *student, float target_x, float target_y,
                      ProjectilePool *pool, int32_t tick, uint32_t *inout_cooldown,
                      DemoEntityId *out_bullet_source);
```

- `student_bot_choose` 只读观测、不写世界；同观测同状态必须同结果。
- 观测里**不得**包含未来波次、未来弹位置或世界随机数。
- 决策调度、动作保持与反击计时由 `world_step` 统一协调，图形端与 sim 不各自决定调用频率。

### 3.6 弹池

```c
bool pool_spawn(ProjectilePool *pool, DemoFaction faction, DemoEntityId source_id,
                uint64_t plan_id, DemoPattern source_pattern, float x, float y, float vx,
                float vy, float radius, float damage, int32_t lifetime_ticks,
                uint64_t id_seed_tick);
void pool_advance(ProjectilePool *pool, float dt);
uint32_t pool_clear_plan(ProjectilePool *pool, uint64_t plan_id);
```

- `id` 整局唯一，不能用可复用下标充当整局 ID；`generation` 记录槽位复用世代。
- `plan_id == 0` 属于学生弹，`pool_clear_plan` 收到 0 时必须返回 0 且不清任何弹。
- 容量不足：`overflow_events++` 并返回 `false`，不越界、不覆盖已激活弹。
- **接口 v2 变更**：新增 `source_pattern` 入参（第 5 个参数）。原因：`Projectile.source_pattern` 原无写入者，
  命中事件的招式归属会读到槽位残留值。Boss 弹传其所属招式，学生弹传 0。同步点：`core/projectiles.c`、
  `tests/test_projectiles.c`、`core/attack.c`（波次生成）。

### 3.7 返回与错误处理约定

- 空指针：写函数返回 `false` 或 0，读函数返回安全默认值，不崩溃。
- 非法配置：`demo_config_validate` 返回 `false` 并写入可读错误串；`world_reset` 拒绝。
- 空目标：出招被拒（`DEMO_REJECT_NO_TARGET`），不扣能量。
- 计划容量/弹池不足：返回失败并计数诊断事件，不静默少发弹后仍宣称预警一致。
- 任何公共函数都不得越界、不得返回 NaN 坐标、不得静默继续。

---

## 4 平台边界

- 核心只消费 `RawInput`（平台填充）与 `BossInput`（核心消费）；`RawInput` → `BossInput` 的转换在 `platform/input_win.cpp` 内完成，且同一显示帧追赶多个逻辑 tick 时，出招请求只能被消费一次。
- 渲染只读 `WorldView`，不消耗世界 RNG，不改变物理。
- sim 与 game 链接同一批 `core/*.c`、`ai/*.c`，读取同一 `DemoConfig`；禁止为 headless 另写一套近似碰撞、能量或招式。

---

## 5 变更记录

| 接口版本 | 日期 | 变更 |
| --- | --- | --- |
| 1 | 2026-10-06 | 首次冻结：类型、配置、世界、出招、招式、学生 AI、弹池、事件、视图 |
| 2 | 2026-10-06 | `pool_spawn` 新增 `source_pattern` 入参，修复 `Projectile.source_pattern` 无写入者的接口缺口（S05 上报）；同步 `core/demo_config.c` 的指针字段重命名为 `pointer_to_position`/`pointer_deadzone`/`pointer_saturate`，`BossInput` 新增指针目标字段 |

**接口版本 2 的同步点**（已全部完成并编译验证）：
- `core/demo_base.h`：`pool_spawn` 签名、`BossInput` 指针字段、`DemoConfig` 指针字段。
- `core/projectiles.c`：写入 `source_pattern`。
- `tests/test_projectiles.c`：17 处调用点补参，测试 223/223 通过（退出码 0）。
- `core/attack.c`（S06 在写）：波次生成时传入 `plan.pattern`。
