# demo 独立验收记录（第三方验收代理）

- 验收代理：独立验收子代理，路由请求 `a6api/deepseek-v4.1-flash`
- 验收时间：2026-10-06 23:3x–23:4x（本地）
- 验收对象：`work/ustc-danmaku`（仓库根）
- 最后复核时 HEAD：`c4d47e8791d7ec1943d6eb644a61b53a8762f216`（见 §0.2，验收期间 HEAD 变动过）
- 本代理**未修改任何源码**（`core/`、`ai/`、`platform/`、`render/`、`sim/`、`tests/`、`CMakeLists.txt`、`scripts/` 全部只读）。本代理只在 `docs/` 下新增本文与 `docs/demo-findings.md`，所有构建产物写在 `build/vfy/` 下。

---

## 0. 验收方法与重要前提

### 0.1 每项结论都标注了执行状态

本文严格区分三种状态：

| 标记 | 含义 |
| --- | --- |
| **通过** | 本代理亲自执行了命令/读到了源码，且结果符合预期 |
| **失败** | 本代理亲自执行，结果不符合预期 |
| **未执行** | 本代理**没有**亲自验证，仅做静态阅读或完全未覆盖 |

**本文没有把任何未执行的检查写成通过。** 特别地：图形交互、真人操作、视觉观感三项均为**未执行**。

### 0.2 ⚠️ 验收期间仓库被并发修改（影响结论有效期）

这是本次验收最重要的前提，必须写在最前面。

本代理在验收过程中观测到**其他代理正在同时改写仓库**，且改写范围覆盖了被验收的目标文件：

1. 验收开始时（约 23:31）`git status` 只显示 `tests/test_attack.c`、`tests/test_pattern_shower.c` 被修改。
2. 随后 `core/attack.c`、`core/pattern_shower.c`、`platform/input_win.cpp`、`platform/input_win.h`、`docs/demo-interfaces.md` 陆续进入已修改状态。
3. HEAD 提交号在验收期间发生变化（观测到 `607cbde` → `c1b6e52` → `c4d47e8`）。
4. 一次 `gcc` 编译在 23:39 前后**非确定性失败**，1 分钟后对同一命令、同一文件集重跑即成功——与该文件在其间被别的进程改写一致。

因此：

- 我固定过快照：`build/vfy/snapshot/test_attack_pinned.c`(SHA256 `1BCDB8B4…`)、`test_pattern_shower_pinned.c`(SHA256 `B42C3330…`)，并对**快照+当时核心**给出过结论。该快照随后即被作者自己覆盖。
- 本文的**最终结论以 §0.3 记录的那一次修订**为准，全部测试结论都在该修订上重跑过。
- **任何结论都带修订指纹**；若仓库在此之后继续变化，本文结论不自动延续，需按 §0.3 的哈希确认是否仍适用。

### 0.3 本次结论所依据的修订指纹（SHA256 前 16 位）

| 文件 | SHA256(16) |
| --- | --- |
| `core/attack.c` | `956937715C0FCDE8` |
| `core/pattern_shower.c` | `6EE994C553E703DC` |
| `core/world.c` | `8DDD346EAF9FDA89` |
| `core/demo_config.c` | `9B868E4ACB95DCA8` |
| `ai/student_bot.c` | `0AE7C1FB4061067C` |
| `platform/input_win.cpp` | `307BE4E6C113C77D` |
| `render/scene.cpp` | `ED153FCF72AF1731` |
| `render/hud.cpp` | `639CB1EF1CCD0395` |
| `tests/test_attack.c` | `C05D035F60C58B7A` |
| `tests/test_pattern_shower.c` | `F49907A16768A809` |
| `scripts/build.ps1` | `3D5E3A5F1B9017D6` |

> `tests/test_pattern_shower.c` 在验收期间先后出现 `B42C3330…` → `85FBD3EC…` → `F49907A1…` 三个版本；上表是最后一次复核时的版本。

---

## 1. 从源码构建

### 1.1 图形游戏（MSVC 2022 + EasyX 26.9.25）

- **状态：通过**
- 脚本：`work/agents/lead/build_game.bat`
- 命令：
  ```
  cd <repo>
  cmd /c work\agents\lead\build_game.bat
  ```
- 证据：输出末尾 `GAME_BUILD_OK`，进程退出码 **0**；产物 `build/game/ustc_danmaku.exe`（276992 字节，LastWriteTime 2026/10/6 23:43）。
- 编译告警（非致命，不影响通过）：EasyX 头 `easyx.h(330/343/355) warning C4201`（无名称 struct/union）；`render/hud.cpp` 多处 `warning C4996`（`wcscpy`/`wcsncat`/`wcsncpy` 被判不安全）；在某一修订上 `core/attack.c(1) warning C4828`（UTF-8 BOM/字符集告警）。
- 备注：其中一次运行曾得到 `GAME_BUILD_EXIT=-1`，同分钟后重跑为 0；与 §0.2 的并发改写一致，非稳定缺陷。

### 1.2 headless 仿真（MinGW GCC 8.1.0）

- **状态：通过**
- 编译器：`D:\mingw64\mingw64\bin\gcc.exe` → `gcc.exe (x86_64-win32-seh-rev0, Built by MinGW-W64 project) 8.1.0`
- 命令（与 `docs/demo-guide.md` §3 一致）：
  ```
  D:\mingw64\mingw64\bin\gcc.exe -std=c11 -O2 -Icore -Iai -Isim ^
    core\demo_config.c core\rng.c core\collision.c core\actors.c core\projectiles.c ^
    core\attack.c core\patterns.c core\pattern_ring.c core\pattern_course.c ^
    core\pattern_mine.c core\pattern_shower.c core\student_fire.c core\world.c ^
    ai\student_bot.c sim\main.c sim\log.c -o build\vfy\r3\sim.exe
  ```
- 证据：退出码 **0**，产物 `build/vfy/r3/sim.exe` 可执行。
- 注意：使用 `-w`（去告警）时同样退出 0；不加 `-w` 会打印大量告警但**仍退出 0**（告警不是错误）。

### 1.3 `scripts/build.ps1` 各分支

| 命令 | 状态 | 退出码 | 证据/说明 |
| --- | --- | --- | --- |
| `-Target sim -Toolchain msvc` | **通过** | 0 | 输出 `BUILD_OK`，`build finished: … (exit=0)` |
| `-Target tests -Toolchain msvc` | **失败** | 1 | `MSVC build failed`；真实原因是 `test_attack.c` 与 `patterns.c`/`world.c` 符号重复 —— 详见 `demo-findings.md` F-1 |
| `-Target all -Toolchain mingw` | **失败** | 1 | `test compile failed: test_attack.c`（`collect2.exe: error: ld returned 1 exit status`），同一根因 |
| `-Target tests -Toolchain msvc`（临时副本中删掉 `core/pattern_mine.c`） | **失败** | 1 | 先打印 `[msvc] missing core sources: core/pattern_mine.c`，随后仍去链接并失败 —— 详见 `demo-findings.md` F-2 |
| `-Target game -Toolchain msvc` | **未执行** | — | 未单独跑；图形构建走的是 `build_game.bat`（见 §1.1） |
| `-Toolchain cmake-msvc` / `cmake-mingw` | **未执行** | — | 本次未执行 CMake 分支 |

---

## 2. 启动（进程存活）

- **状态：通过**（仅验证"能起来且活着"，非交互试玩）
- 命令：
  ```powershell
  $p = Start-Process build\game\ustc_danmaku.exe -WorkingDirectory build\game -PassThru
  Start-Sleep -Seconds 5
  Get-Process -Id $p.Id
  ```
- 证据（当前修订，PID 48580）：

  | 观测项 | 值 |
  | --- | --- |
  | 5 秒后仍存活 | TRUE |
  | `MainWindowHandle` | 6363444（非 0 → 确实创建了窗口） |
  | `MainWindowTitle` | `ustc_danmaku` |
  | `Responding` | True |
  | 工作集 | 约 18 MB |
  | `Stop-Process -Force` 后残留 | 无（进程干净退出） |

- 窗口尺寸约定：`game_main.cpp` 中 `kWindowW=1280 / kWindowH=720`，战场为左上 960×720（`platform/input_win.cpp` 用 `FIELD_PIXEL_W/H` 判定指针是否在场内）。
- **本代理没有做任何图形交互试玩**，未点击、未按键、未观察画面。仅确认"创建了窗口的进程存在且响应"。

---

## 3. 输入语义（静态复核，不实际按键）

**方法说明**：本节全部为**源码静态复核**，本代理**没有实际按键、没有实际操作鼠标**。因此结论形式是"代码实现了该语义 / 未实现"，而非"实测手感正确"。

复核对象：`platform/input_win.cpp`、`platform/input_win.h`、`game_main.cpp`。

| 要求 | 状态 | 证据 |
| --- | --- | --- |
| **松开即停** | **通过（静态）** | 轴量每帧用同步状态查询重建：`input_poll()` 里对 `W/A/S/D`、方向键逐个 `update_axis_edges()`；`input_state_build_boss_input` 只在 `down` 为真时累加 `kx/ky`。松开 → `down=false` → 轴归 0。移动由 `core/actors.c: actor_apply_move` 施加，`len <= 0` 直接不动。 |
| **斜向归一化** | **通过（静态）** | 两处独立实现：平台层 `input_win.h:342` `const float len = sqrtf(x*x + y*y);`；核心 `core/actors.c:102-110` 先 `nx=dir_x/len; ny=dir_y/len`，再 `mag=(len>1.0f)?1.0f:len`，`core/world.c:399-403` 也对 `len>1` 做归一化。双重归一化 → 斜向不会超速。 |
| **一次按下只出招一次** | **通过（静态）** | `attack_edge[DEMO_PATTERN_COUNT]` 是"尚未被消费的按下边沿"；`attack_edges_pending` 标记；`input_state_build_boss_input` 仅在 `first_of_frame && attack_edges_pending` 时把边沿交给核心并立即清位（`input_win.h:500-509`）。一次显示帧追赶多个逻辑 tick 时不会重复出招。另有 `pause_held/restart_held` 抑制键盘自动重复（`input_win.h:276-293`）。 |
| **失焦清空** | **通过（静态）** | `input_on_focus_lost()` → `input_state_set_focus(&g_state,false)`；`WM_KILLFOCUS` 在 `input_win.cpp:96` 被处理。清空内容含指针失效、键盘轴清零、按住与边沿全清。**值得注意**：`input_win.cpp` 文件头明确指出 EasyX 的 `peekmessage` **不转发** `WM_KILLFOCUS`/`WM_SETFOCUS`/`WM_CLOSE`/`WM_DESTROY`，因此该修订改用**窗口钩子**（`input_win.cpp:230` "安装窗口钩子以截获 EasyX 不转发的 WM_KILLFOCUS / WM_CLOSE"）来补齐。这一改动方向正确，但其**实际钩子行为未被本代理运行验证**（见下方"未执行"）。 |
| **暂停** | **通过（静态）** | `Esc` 与鼠标右键 → `pause_edge`；`game_main.cpp` 用 `input_pause_edge_consume()` 翻转 `paused`，并在恢复时 `accumulator = 0.0`（注释"恢复不补暂停时间"），避免暂停期间累积的时间在恢复瞬间灌入。暂停/终局时 `input_clear_edges()` 丢弃出招边沿，避免恢复后突然执行。 |
| **重开** | **通过（静态）** | `R` 键 → `restart_edge`；`game_main.cpp:119-128` 重建 `World fresh` 并 `world_reset(&fresh,&cfg,seed)` 后整体赋值，同时复位 `accumulator/paused/intro`。**同 seed 重开**（seed 未变），可复现。 |

**未执行（输入相关）**：

- 未实际按键验证"松手是否真的立刻停"、"斜向速度是否真等于直线速度"。
- 未实际触发失焦（切窗口）验证钩子是否真的收到 `WM_KILLFOCUS`。
- 未实际验证暂停期间不动、恢复后不跳帧。
- 未实际验证 `R` 重开。

以上均只有静态证据。

---

## 4. 能量 / 状态机 / 锁定几何 / 命中 / 多学生 / 暂停重开 / 资源安全 / 复现 / 稳定性

### 4.1 能量与状态机（静态复核 + 测试证据）

- 状态：**通过（静态 + 单元测试）**
- `core/world.c: world_pattern_available()` 的判定顺序：`status != RUNNING` → `attack_state != IDLE` → `energy < cost` → `world_nearest_student()` 存在。`consume_requests()` 同 tick 只接受一个请求，按招式 ID 从小到大取第一个（`for p in 0..COUNT`，命中即 `return`）。
- 能量只在接受成功时扣；`energy_regen_accum` 用浮点累加、`while >= 1.0f` 逐点恢复，恢复上限 `energy_max`（`core/world.c:626-632`）。
- 测试证据：`test_attack` 覆盖"能量不足拒绝且能量不变"“忙碌只扣一次”“无目标拒绝且能量不变”“原子接受”“同 tick 三次请求只接受一次”“拒绝不排队（100 tick 后仍不自动补发）”，其中与状态机/能量相关的断言全部通过（当前修订仅剩 1 项失败，见 §4.7）。

### 4.2 锁定几何（静态 + 测试证据）

- 状态：**通过（静态 + 单元测试）**
- 计划在 `attack_try_request` 时一次性固化（`origin/aim/target_id/start_tick/wave_*`），`attack_step` 只推进不重算。
- 测试证据（`test_pattern_shower.c`）：`test_lock_no_migration` 用"改 request 后重新 emit"对比，断言"波内 x 集合逐字节不变""完整弹规格逐字节不变""emit 不改动 AttackPlan 本体""emit 不消耗 rng 状态"，当前修订**全通过**（0 失败）。
- `test_attack.c: test_e_lock_does_not_migrate`：接受后移动 Boss 与全部学生 100 tick，断言 `origin/aim/target_id` 不迁移，当前修订**通过**。

### 4.3 命中与碰撞（静态 + 测试证据）

- 状态：**通过（静态 + 单元测试）**
- `core/world.c: resolve_collisions()` 使用**相对运动扫掠**：`actor_bullet_hit()` 把角色位移段与弹位移段分别作为 `Segment`，交给 `sweep_hit(&a,&b, r_actor + r_bullet, &t, &dist)`；多学生命中时取**最早交点 `t`**，`t` 相同则取**稳定 ID 小者**（`core/world.c:484-485`）。
- 命中即移除：`p->active = false` 并递减 `live_count`；但**先**结算伤害再移除。
- 伤害后由 world 统一赋无敌（`s->invuln_ticks = cfg.student_hurt_invuln_ticks`），注释明确说明"`actor_apply_damage` 是纯函数，不改无敌，这样同一 tick 内的后续弹会被 invuln 挡住，不会重复扣血"。
- 测试证据：`test_collision` → 124 项全 PASS；`test_actors` → 268 项全 PASS；`test_projectiles` → 223 项全 PASS。

### 4.4 多学生

- 状态：**通过（静态 + 单元测试）**
- `DEMO_MAX_STUDENTS = 8`；默认配置 3 名（`core/demo_config.c: set_default_students`）。
- 每人独立 `StudentBotState` / `StudentObservation` / `StudentAction`；`world_reset` 用 `seed ^ (i * 2654435761u)` 派生各学生 bot 子种子，保证同 seed 同初始状态。
- 决策调度由 world 统一协调（`student_since_decision`），非各端自决：`decide` 为真时全体重新决策，否则全体**动作保持**。
- 倒下学生退出后续行动：`students_think_and_move`/`students_fire`/`build_observations` 均 `if (!s->alive) continue;`。
- 测试证据：`test_actors` 覆盖"学生 hp 归零后倒下""倒下后不再受击""倒下后 hp 保持 0"等，全 PASS。

### 4.5 暂停/重开

- 状态：**通过（静态）**
- 核心不表达暂停：`core/world.c: world_make_view()` 恒置 `out->paused = false`，注释"暂停由外层状态机管理，核心不表达暂停"。暂停/重开完全在 `game_main.cpp` 外壳状态机内实现（见 §3）。
- 重开通过 `world_reset` 重建，`world_reset` 清除弹幕、计划、目标、能量、AI 状态、无敌计时、待处理请求与全部计数（`core/world.h` 注释与 `core/world.c` 实现一致）。
- **未执行**：未在图形端实测暂停/重开（见 §6）。

### 4.6 资源安全

- 状态：**通过（静态 + 实测）**
- 静态：`core/` 与 `ai/` 全部为定长数组/值类型，**无 `malloc`/`free`/`new`/`delete`**；全库唯一的动态分配在 `sim/log.c: calloc/free`（日志句柄），且失败路径已 `free(lg)` 后返回 NULL，调用方降级为"仅 stdout"继续。
- 实测结构体尺寸（MinGW x64）：

  | 结构 | 字节 |
  | --- | --- |
  | `World` | 79560 |
  | `ProjectilePool` | 57616（`Projectile` 72 字节 × `DEMO_MAX_PROJECTILES` 800） |
  | `StudentObservation` | 1388 |
  | `StepEvents` | 9228（`DEMO_MAX_STEP_EVENTS` 256） |
  | `WorldView` | 200 |

  `World` 约 78 KB，按值放在 `game_main` 栈上（另有重开用的 `World fresh`，同帧两个实例约 156 KB，仍在默认 1 MB 栈内）。`WorldView` 是轻量只读视图（含裸指针），换出成本低。
- 事件溢出有明确处理：`events_push` 在满时 `dropped++` 并返回 false；`sim/main.c` 会打印 `WARN events_dropped=`。弹池溢出走 `spawn_overflow_count` / `overflow_events` 计数，不静默丢失。
- 未使用 AddressSanitizer / Valgrind（**未执行**内存越界专项检测）。§1.2 的 MinGW 构建有告警，未逐一审计。

### 4.7 测试总览（当前修订，全部亲自执行）

编译方式：`gcc -std=c11 -O2 -w -Icore -Iai -Itests`；`test_attack` 按其文件头自述的最小链接集（`attack.c rng.c projectiles.c demo_config.c`），其余测试链接全部核心 + `ai/student_bot.c`。

| 测试 | 编译 | 运行 | 通过项 | 失败项 | 结论 |
| --- | --- | --- | --- | --- | --- |
| `test_actors` | 0 | 0 | 268 | 0 | **通过** |
| `test_attack` | 0 | **1** | 80 | **1** | **失败**（`f6 第一波恰好 1 波`） |
| `test_collision` | 0 | 0 | 124 | 0 | **通过** |
| `test_pattern_shower` | 0 | 0 | 1574 | 0 | **通过** |
| `test_projectiles` | 0 | 0 | 223 | 0 | **通过** |
| `test_rng` | 0 | 0 | 46 | 0 | **通过** |
| `test_student_fire` | 0 | 0 | 137 | 0 | **通过** |

> `test_collision`/`test_rng` 的输出中含 `FAIL: 0` 字样，grep `FAIL` 会命中摘要行；实际失败数为 0。

**`test_pattern_shower.c` 的历史（重要，因为它反映了验收期间的变化）**：在验收早期的快照修订（`B42C3330…`，即被测模块仍是旧 `pattern_shower.c` 时），该测试**失败**：`总子项 1558, 失败 16`，全部是"相邻波 x 中心平移方向与 gap_angle_deg 编码的扫描方向一致"。我确认过这是**测试的断言对象用错**（用**弹体 x 的均值**去判方向，而 16 发弹在缝隙两侧的分布导致均值反向漂移；实测"相邻波 x 集合确实不同""缝隙中心按编码方向平移"都成立）。该断言后来由作者改为用**缝隙中心**判定，`pattern_shower.c` 亦被重写，当前修订**全通过（1574 项 0 失败）**。

**`test_attack.c` 的历史**：验收早期该测试若按 `scripts/build.ps1` 的方式链接（连同 `patterns.c`+"world.c"）会**编译失败**；按文件头自述的最小链接集则能建，早期为 `77 passed, 3 failed`（`f6`/`f7`/`j6`），随后变为 `80 passed, 1 failed`，当前修订为 `80 passed, 1 failed`（仅剩 `f6`）。

### 4.8 复现性 / 稳定性（实测）

- **状态：通过**

**复现性**：
- 同 seed 跑两次并对 `--replay` 输出哈希比对：

  | seed | script | replay 行数 | 两次一致 |
  | --- | --- | --- | --- |
  | 12345 | mixed | 24 | **是** |
  | 1 | mixed | 51 | **是** |
  | 7 | mixed | 44 | **是** |

- 不同 seed 的 replay **不同**（证明随机确实生效，不是被写死）。
- **重新编译后**（另一次 `gcc` 调用产出 `sim_rebuild.exe`）同 seed replay 与首次构建**逐字节一致** → 构建可复现、无未初始化内存/时间依赖污染。

**稳定性（多次运行不崩溃）**：
- `mixed` × 20 seed × `--max-ticks 3600`：**0 次崩溃/非零退出**。状态分布 `BOSS_LOSE=13`、`TRUNCATED=7`。
- 追加 `{patrol,dodge,mixed,wait}` × seed 21–30（40 次，`--max-ticks 3600`）：**0 次崩溃/非零退出**；状态分布 `BOSS_LOSE=29`、`TRUNCATED=11`。
- 长时限（`--max-ticks 36000`）多 seed 运行：无崩溃；部分 seed 长期停在 `TRUNCATED`（例如 seed 9、20 跑满 36000 tick 仍未分胜负）。
- 合计 **60+ 次运行 0 崩溃**。

**CLI 退出码**（实测）：

| 命令 | 退出码 |
| --- | --- |
| `--seed abc` | **2** |
| `--script bogus` | **2** |
| `--max-ticks -5` | **2** |
| `--config other` | **2** |
| `--help` | **0** |

**跨修订结果漂移（必须记录）**：同一命令 `--seed 12345 --script mixed --max-ticks 7200` 在不同修订上给出不同结果：

| 修订时刻 | 结果 |
| --- | --- |
| 验收早期（23:35 前） | `BOSS_LOSE tick=1257 accepts=5` |
| 中期 | `BOSS_LOSE tick=2823`、`tick=3254`（随改写漂移） |
| 末期复核 | `BOSS_LOSE tick=3254 accepts=12 rejects=2723 boss_hits=6 student_hits=4` |

`--script wait`（seed 12345）在各修订上稳定为 `BOSS_LOSE tick=479 boss_hits=6`（Boss 完全不出招、被打 6 次倒下），可作为回归锚点。`--max-ticks 300` 稳定为 `TRUNCATED tick=300`。

### 4.9 ⚠️ 文档中的 `BOSS_WIN` 基线**未能复现**

- **状态：失败（文档基线不可复现）**
- `docs/demo-guide.md` L68 声称：
  > `--script mixed --max-ticks 7200` → `RESULT status=BOSS_WIN tick=4196 accepts=16`

- 本代理在验收过程中**从未**复现出 `BOSS_WIN`。实跑全部 4 种 script × 多 seed（含 `--max-ticks 36000` 长时限）共 60+ 次，结果**只有 `BOSS_LOSE` 与 `TRUNCATED` 两种**，`BOSS_WIN` 一次都没出现。`mixed` 的取值全部落在"Boss 被击倒"或"未分胜负"。
- 溯源：`BOSS_WIN tick=4196` 确实存在于仓库内的**旧日志**（`build/sim/check.log:3812`、`build/sim/run1.log:3812`，mtime 23:27:41/23:30:05）。但那批日志由**旧构建**产生；`build/sim/sim.exe` 的 mtime 是 23:32:32，而 `core/attack.c`（23:30:30）、`tests/*`（23:34+）、后续 HEAD 变动都在旧日志之后。
- 结论：`docs/demo-guide.md` L68 的验收基线**取自过期日志**，当前代码已不满足。这既不是"实现坏了"，也不是"文档全错"，而是**文档基线没有随实现更新**。

---

## 5. 画面 / HUD（仅静态复核）

- **状态：未执行（真人视觉验证）**；仅完成**源码静态复核**。
- **本代理没有任何图形交互能力**，未截图、未观察窗口内容、未验证任何像素。

静态复核结论（`render/scene.cpp`、`render/hud.cpp`）：

- `scene_render()` 只读 `WorldView` 与 `DemoConfig`，不写世界、不取 RNG、不裁决。
- 形状区分（不依赖颜色）**已实现**：
  - Boss 弹 = **实心圆 + 白芯**（`draw_boss_bullet`）；
  - 学生弹 = **菱形轮廓 + 深色填充**（`draw_student_bullet`，4 点 `fillpolygon`）；
  - 学生 = 实心圆 + 头顶血条，无敌时变白；倒下画**叉号**；
  - Boss = 实心圆 + 轮廓 + 体内小方口 + 身下血条；
  - 最近目标有额外黄色圆环标记（`view->marked_target`）。
- 预警 `draw_warning()` 按招区分：RING 画圆 + 两条缺口边线；COURSE 画 3 列矩形；MINE 画矿点圆 + 3 条扇面方向线；SHOWER 画 y=100 顶部线 + 矩形缝隙带。
- **发现一处几何不一致（静态）**：`scene.cpp` 的 SHOWER 预警把缝隙画在**恒定的 `field_w * 0.5`**（`center = cfg->field_w * 0.5f`），并未读取 `w->corridor_width` 所对应的**实际缝隙中心**。而实现里缝隙中心是**逐波扫描**的（`wave_offset + dir*i*step`）。因此预警画的竖向通道位置与真实安全通道位置可能**对不上**。详见 `demo-findings.md` F-6。
- `render/hud.cpp` 读取的字段（文件末注释已自述清单）：`status/truncated/paused/attack_state/plan_start_tick/plan_windup_ticks/plan_active_ticks/tick`、`boss->hp/hp_max`、`student_count/students[i].alive/id/hp/hp_max`、`energy/energy_max` + `cfg.energy_regen_per_sec`、`pattern_available[]/marked_target`；`warning` **不读**（交给 `scene.cpp`，避免重复绘制）。
- HUD 不自行裁决胜负：`hud_is_finished()` 直接取 `view->status != RUNNING || view->truncated`；进度条由 `view->tick` 与计划时刻自算（因为 `WorldView` 没有 `progress` 字段）。

**未执行**：字体是否可读、中文是否乱码、颜色对比、窗口在 1280×720 下是否遮挡、预警是否真的可见、血条/能量条是否与实际数值一致。

---

## 6. 操作闭环

- **状态：未执行**
- 明确声明：**未做真人操作验证（本会话无图形交互能力）**。
- 未验证的闭环环节：鼠标移动手感（"越近越慢、抵达即停"是否真的成立）、键盘八方向手感、`1/2/3/4` 出招是否真的每次都触发、`Esc`/右键暂停是否真的生效、`R` 重开是否真的回到干净初始局、切窗口（失焦）后再回来是否无残留按键、关闭窗口是否干净退出。
- §3 全部为静态复核，不能替代真人操作验证。

---

## 7. 未执行项汇总（不得视为通过）

| 项 | 原因 |
| --- | --- |
| 图形交互试玩（点击/按键/观察） | 本会话无图形交互能力 |
| 真人操作闭环验证 | 同上 |
| 真人视觉验证（HUD/画面可读性） | 同上 |
| 失焦钩子的实际运行行为 | 需真人切窗口才能触发 |
| `scripts/build.ps1 -Toolchain cmake-msvc` / `cmake-mingw` | 本次未执行 |
| `scripts/build.ps1 -Target game -Toolchain msvc` | 本次未单独执行（图形构建走 `build_game.bat`） |
| 内存越界检测（ASan/Valgrind） | 未执行 |
| 内存泄漏检测 | 未执行（但静态确认 `core/`+`ai/` 无堆分配） |
| AI 性能/平衡性调优评估 | 超出本次验收范围；`mixed` 60+ 次全部未出现 `BOSS_WIN`，平衡性另见 `demo-findings.md` F-5 |

---

## 8. 一句话总结

构建（图形 + headless）与启动（窗口进程存活）**通过**；核心单元测试 7 个中 6 个全通过、`test_attack` 剩 1 项失败；复现性与稳定性（60+ 次 0 崩溃）**通过**；输入语义静态复核**符合契约**；`docs/demo-guide.md` 的 `BOSS_WIN tick=4196` 基线**无法复现**；画面与操作闭环**未做真人验证**。验收期间仓库被并发改写，结论以 §0.3 的修订指纹为准。
