# demo 独立验收：问题清单（P0/P1/P2）

- 验收代理：独立验收子代理（路由请求 `a6api/deepseek-v4.1-flash`）
- 验收时间：2026-10-06 23:3x–23:4x
- 验收对象最后修订指纹见 `docs/demo-validation.md` §0.3
- 本代理**未修改任何源码**；本文只记录本代理**亲自复现**的问题。

## 分级标准

| 级别 | 含义 |
| --- | --- |
| **P0** | 阻断性：构建/测试无法通过，或交付说明的验收基线是错的，会误导后续判断 |
| **P1** | 实质缺陷：可运行但存在未定义行为、校验缺口、与规格不一致，需要修 |
| **P2** | 次要：告警、可维护性、文档/一致性，不阻断交付 |

> **前提**：验收期间仓库被其他代理**并发改写**（`core/attack.c`、`core/pattern_shower.c`、`platform/input_win.cpp`、`tests/*` 等，HEAD 提交号变动）。下列问题的状态标注了"复现时的修订"。若仓库继续变化，请先按 `demo-validation.md` §0.3 核对哈希。

---

## P0-1 `scripts/build.ps1` 的 tests 分支必然链接失败（符号重复）

- **现象**：`scripts/build.ps1 -Target tests` 在两种工具链下**都**失败，且失败原因不是"缺文件"，而是 `test_attack.c` 里自带的测试内桩与真实实现**符号重复**。
- **最小复现命令**：
  ```
  cd <repo>
  powershell -NoProfile -File scripts\build.ps1 -Target tests -Toolchain msvc -OutDir build\vfy\mvc_tests
  ```
  MinGW 侧同样复现：
  ```
  powershell -NoProfile -File scripts\build.ps1 -Target all -Toolchain mingw -OutDir build\vfy\mingw
  ```
- **实际输出**（MSVC，摘自生成的 `_build.bat` 实跑）：
  ```
  tobj_test_attack.obj : error LNK2005: events_init 已经在 core_world.obj 中定义
  tobj_test_attack.obj : error LNK2005: events_push 已经在 core_world.obj 中定义
  tobj_test_attack.obj : error LNK2005: pattern_name 已经在 core_patterns.obj 中定义
  tobj_test_attack.obj : error LNK2005: pattern_vtable 已经在 core_patterns.obj 中定义
  ... fatal error LNK1169: 找到一个或多个多重定义的符号
  BUILD_FAILED
  ```
  MinGW 侧：`multiple definition of 'pattern_vtable' / 'pattern_name' / 'events_init' / 'events_push'`，`collect2.exe: error: ld returned 1 exit status`，`test compile failed: test_attack.c`，退出码 **1**。
- **预期 vs 实际**：预期 `-Target tests` 能产出 7 个测试可执行文件；实际 MSVC 退出 1（`MSVC build failed`），MinGW 退出 1。
- **根因**：`tests/test_attack.c` 的**自身设计**就不与 `core/patterns.c`、`core/world.c` 共存。它的文件头写明了：
  > `pattern_vtable / pattern_name: 测试内实现` … `因此 **本次未链接 core/patterns.c, 未验证真实招式几何**`
  > `events_init / events_push: 仓库当前尚无实现(core/world.c 只调用), 测试内给出等价实现`

  即：该测试假设"`events_*` 未被实现"、"`patterns.c` 不参与链接"。而 `scripts/build.ps1` 的第 124-126 行（MinGW）与 177-178 行（MSVC）无条件把**全部** `core_*.obj` 链接进每个测试。
- **影响**：
  - `-Target tests` 与 `-Target all` 在**任何**工具链下都无法完成 —— 这是构建入口的直接阻断。
  - 更麻烦的是**报错信息误导**：错误看起来像"实现里有重复定义"，实际是实现正确、**测试用了过期假设**。排查者容易去改核心而不是改测试/构建。
- **建议的最小修复方向**（三选一，按推荐排序）：
  1. **改测试**：让 `test_attack.c` 使用真实 `patterns.c`/`world.c`（删掉 `pattern_vtable`/`pattern_name`/`events_*` 桩）。`events_*` 现在**确实已实现**于 `core/world.c:93-115`，桩已完全过时。这一步同时会把该测试从"不验证真实招式几何"升级为真实验证，价值最高。
  2. **改构建**：在 `build.ps1` 里给 `test_attack` 指定**专属链接集**（`attack.c rng.c projectiles.c demo_config.c`），而不是 `core_*.obj` 通配。代价是它继续不验证真实招式几何。
  3. **改声明方式**：把 `test_attack.c` 的桩改成 `static` 并重命名（如 `test_pattern_vtable`），从根上消除与真实符号的冲突。这只是消症状，不解决"测试没测真实实现"。

  注意方案 2 还有一个前提：`test_attack.c` 里 `attack_try_request` / `attack_step` 的真正依赖必须被满足（当前它能与 `attack.c+rng.c+projectiles.c+demo_config.c` 链接成功）。
- **复现时修订**：`scripts/build.ps1` `3D5E3A5F1B9017D6`，`tests/test_attack.c` `C05D035F60C58B7A`。

---

## P0-2 `test_attack.c` 仍有 1 项断言失败（`f6`）

- **现象**：`test_attack` 能编译（按文件头自述的最小链接集），但运行退出码 **1**，`80 passed, 1 failed`，失败项为 `f6 第一波恰好 1 波`。
- **最小复现命令**：
  ```
  cd <repo>
  D:\mingw64\mingw64\bin\gcc.exe -std=c11 -O2 -w -Icore -Iai -Itests ^
    -o build\vfy\test_attack.exe tests\test_attack.c ^
    core\attack.c core\rng.c core\projectiles.c core\demo_config.c
  build\vfy\test_attack.exe
  ```
  输出末行：`== summary: 80 passed, 1 failed ==`，进程退出码 **1**。
- **预期 vs 实际**：预期 `f6`（"第一波恰好 1 波"）通过；实际失败。
- **历史（同一测试在验收期间的变化）**：
  - 早期：`77 passed, 3 failed`（`f6`、`f7 攻击进度在 ACTIVE 起点为 1`、`j6 关闭清弹配置时 Boss 弹保留`）。
  - 后期：`80 passed, 1 failed`（仅剩 `f6`）。
  - 即 `f7`、`j6` 已被修掉，`f6` 仍在。
- **根因推测（未定论，`core/attack.c` 同时在变，本代理不做结论）**：`f6` 断言"切换到 ACTIVE 的那一 tick 恰好产生 1 个 `WAVE_SPAWN` 事件"，而 `f7`（同一 tick 的 `attack_progress == 1.0f`）在早期一起失败、后期单独通过 —— 说明该 tick 的**波次生成与状态切换的时序**是这一族的焦点。测试用的 `stub_emit` 只在 `start_tick + windup_ticks` 那一 tick 生成 1 发；若 `attack.c` 在同一 tick 内先切状态、后生成，或生成发生在下一 tick，`event_count_at_tick(..., t_active)` 就会是 0。
- **影响**：`test_attack` 无法作为绿色回归门；`-Target tests` 即便修好 P0-1 也仍会红灯。
- **建议的最小修复方向**：
  1. 先确认契约：`docs/demo-interfaces.md` §3.2 的 tick 顺序规定"2) attack_step: 预警/攻击推进 + 波次生成"，即**同一 tick 内**推进到 ACTIVE 与生成第 0 波。据此判定是实现偏了还是测试断言偏了，**不要两头都改**。
  2. 若实现正确，把 `f6` 的断言改成对"ACTIVE 起点或其后第一个生成 tick"的精确描述；若实现偏离契约，修 `attack.c` 的推进/生成顺序。
  3. 修完后重跑 `f3..f7`，它们是一组边界断言，应一起绿。
- **复现时修订**：`tests/test_attack.c` `C05D035F60C58B7A`，`core/attack.c` `956937715C0FCDE8`。

---

## P0-3 `docs/demo-guide.md` 的验收基线 `BOSS_WIN tick=4196` 无法复现

- **现象**：交付说明把一个**取自过期日志**的结果写成了验收标准。本代理在验收全程 **60+ 次运行中从未复现 `BOSS_WIN`**。
- **涉及文档**：`docs/demo-guide.md` L68
  > | 完整对局可结束 | `--script mixed --max-ticks 7200` → `RESULT status=BOSS_WIN tick=4196 accepts=16` |
- **最小复现命令**：
  ```
  build\vfy\r3\sim.exe --seed 12345 --script mixed --max-ticks 7200 --log out.log
  findstr /C:"RESULT status=" out.log
  ```
  实测（两个不同修订）：
  - 早期修订：`RESULT status=BOSS_LOSE tick=1257 accepts=5 rejects=873 boss_hits=6 student_hits=3`
  - 末期修订：`RESULT status=BOSS_LOSE tick=3254 accepts=12 rejects=2723 boss_hits=6 student_hits=4`
- **全量扫描（证明 `BOSS_WIN` 不可达）**：4 种 script × 多 seed，含 `--max-ticks 36000`：

  | 脚本 | 观测到的状态 |
  | --- | --- |
  | `patrol` | 只有 `BOSS_LOSE` |
  | `dodge` | 只有 `BOSS_LOSE` |
  | `mixed` | `BOSS_LOSE` + `TRUNCATED` |
  | `wait` | 只有 `BOSS_LOSE` |

  `mixed` × 20 seed × 3600：`BOSS_LOSE=13`、`TRUNCATED=7`；另 40 次（seed 21–30）：`BOSS_LOSE=29`、`TRUNCATED=11`。**`BOSS_WIN` 出现 0 次。**
- **溯源证据**：`BOSS_WIN tick=4196` 确实存在于仓库内旧日志：
  - `build/sim/check.log:3812` → `FINAL status=BOSS_WIN tick=4196 boss_hp=3 alive=0/3`
  - `build/sim/run1.log:3812` → 同上

  但这批日志 mtime 为 `23:27:41`/`23:30:05`，而 `build/sim/sim.exe` mtime 为 `23:32:32`，`core/attack.c` 在 `23:30:30` 之后仍被改写，HEAD 提交号也在验收期间变动（`607cbde` → `c1b6e52` → `c4d47e8`）。
- **预期 vs 实际**：预期按文档命令得到 `BOSS_WIN`；实际得到 `BOSS_LOSE`（或长时限下的 `TRUNCATED`）。
- **影响**：
  - 这是**交付说明的一个错误验收断言**。任何人拿它当"验收通过"的判据都会得到"项目不达标"的假结论；反过来也可能被当作"已验收"的证据而掩盖真实状态。
  - 它同时掩盖了一个**产品或平衡问题**：当前实现下 Boss 几乎无法取胜（见 P1-4）。
- **建议的最小修复方向**：
  1. **立刻**把 `docs/demo-guide.md` L68 改成可复现的锚点，例如 `--script wait --seed 12345` → `BOSS_LOSE tick=479`（本代理在多个修订上实测稳定）或 `--max-ticks 300` → `TRUNCATED tick=300`。
  2. 若确实要保留"完整对局可结束"这一验收点，应改为"能到达任一终局状态（`BOSS_WIN`/`BOSS_LOSE`/`DRAW`）"，并附上**当时构建**重新生成的日志，不引用 `build/` 下的历史产物。
  3. 把 `build/` 下的历史日志归档或标注"非当前构建"，避免再次被误引。

---

## P1-1 `StudentBotState.fire_cooldown_ticks` 是 `int32_t`，却以 `(uint32_t *)` 强转传入

- **现象**：`core/world.c` 把一个 `int32_t` 成员的地址**强转**为 `uint32_t *` 传给 `student_fire_try()`，构成严格别名违规（strict-aliasing violation，C11 6.5p7）。
- **最小复现命令**（找证据，非崩溃复现）：
  ```
  cd <repo>
  findstr /N "uint32_t \*" core\world.c
  findstr /N "fire_cooldown_ticks" core\demo_base.h core\world.c
  ```
  实测输出：
  - `core/demo_base.h:459: int32_t fire_cooldown_ticks;`
  - `core/world.c:354: (uint32_t *)&world->bot[i].fire_cooldown_ticks, &source)) {`
- **相关签名**：`core/student_fire.h:17`
  ```c
  bool student_fire_try(..., uint32_t *inout_cooldown, DemoEntityId *out_bullet_source);
  ```
  函数体 `core/student_fire.c:144` 写入 `*inout_cooldown = (uint32_t)cfg->student_fire_interval_ticks;`
- **预期 vs 实际**：
  - 预期：类型一致，无需强转（把成员改成 `uint32_t`，或把接口参数改成 `int32_t *`）。
  - 实际：两种不同类型的指针指向同一对象。**当前在 MinGW `-O2` 与 MSVC `/O2` 下均未观测到错误行为**，但这是编译器有权优化的未定义行为——`-O2` 下"看似工作"不构成正确性证据。
- **影响**：潜在的错误优化/时序错误；在同一处还造成 `int32_t` 与 `uint32_t` 混用的语义噪声（`student_fire_ready` 取 `int32_t`，`student_fire_tick_cooldown` 取 `int32_t *`，只在这一个调用点变成 `uint32_t *`）。属于典型的"现在没事、换个编译器版本就出事"。
  - 顺带：`(uint32_t)cfg->student_fire_interval_ticks` 若配置为负值会**回绕成巨大正数**，冷却被设成天文数字。见 P1-2。
- **建议的最小修复方向**（推荐 1）：
  1. 把 `StudentBotState.fire_cooldown_ticks` 改成 `uint32_t`，并同步 `student_fire_ready` 取参、`student_fire_tick_cooldown` 取参为 `uint32_t`（三处签名一起改，去掉强转）。这是**唯一**能同时消掉别名违规与符号混用的方案。
  2. 若不愿动公共头文件，把 `student_fire_try` 的 `inout_cooldown` 参数改成 `int32_t *`，内部做夹紧；`world.c` 的强转随之删除。
  3. 无论选哪个，都在 `student_fire_try` 入口把配置值夹到合法范围（`> 0` 才使用），避免负值回绕。
- **复现时修订**：`core/world.c` `8DDD346EAF9FDA89`，`core/student_fire.c` `ABFD255E9979D3E2`，`core/demo_base.h` `6582DF967739BF19`。

---

## P1-2 `demo_config_validate()` 完全未校验学生反击相关配置字段

- **现象**：`core/demo_config.c` 把 6 个学生反击/学生弹字段**只赋初值、从不校验**。它们的值直接进入发射路径与弹生成，非法值不会被 `world_reset()` 拦住（`world_reset` 唯一的配置门就是 `demo_config_validate`）。
- **最小复现命令**：
  ```
  cd <repo>
  findstr /N "student_fire\|student_bullet" core\demo_config.c
  ```
  实测输出（全部只出现在 `demo_config_init` 的赋值区 L121-L126，`demo_config_validate` 区间 L155-L254 内**一条都没有**）：
  ```
  121: cfg->student_fire_interval_ticks = 78;
  122: cfg->student_fire_damage = 1;
  123: cfg->student_bullet_speed = 260.0f;
  124: cfg->student_bullet_radius = 5.0f;
  125: cfg->student_bullet_lifetime_ticks = 240;
  126: cfg->student_fire_min_range = 60.0f;
  ```
- **未校验字段清单**：

  | 字段 | 未校验 | 非法值后果 |
  | --- | --- | --- |
  | `student_fire_interval_ticks` | 是 | `<= 0` → 冷却恒为 0，学生**每 tick 都发射**；负值经 `(uint32_t)` 转换回绕成巨数（见 P1-1） |
  | `student_fire_damage` | 是 | 负值 → `(float)` 后进入 `pool_spawn` 的 `damage`，命中时 `(int32_t)p->damage` 为负 → 可能**给 Boss 回血** |
  | `student_bullet_speed` | 是 | `<= 0` → 弹不动或倒飞（`student_fire_try` 内有 `!(speed > 0)` 兜底，返回 false，属"静默永不发射"） |
  | `student_bullet_radius` | 是 | 负值 → 负半径进入 `pool_spawn`；`student_fire_try` 有 `!(radius >= 0)` 兜底 |
  | `student_bullet_lifetime_ticks` | 是 | `<= 0` → 弹生成即消失（`student_fire_try` 内有兜底） |
  | `student_fire_min_range` | 是 | 负值/NaN → 最小射程失效；NaN 使 `distance <= NaN` 恒为 false，射程检查被静默跳过 |

- **预期 vs 实际**：预期 `demo_config_validate` 覆盖所有进入模拟的数值字段（它已经校验了 `boss_hp`、`student_hp`、`student_decision_ticks`、`energy_*`、`projectile_cap`、`boss_speed`、每个 `PatternConfig` 的 cost/时长/弹速/波数等）。实际上述 6 个字段**一个都没校验**。
- **影响**：
  - `world_reset()` 在配置非法时**本应**返回 false 并阻止开局；这些字段不受保护，非法配置会进入模拟，破坏"配置驱动、失败可见"的设计约定。
  - `student_fire_damage` 为负导致 Boss 回血是最严重的一例：不崩溃、不报错，只是静默产生错误结果。
  - 这类缺陷在将来做**参数搜索/自动平衡**（仓库里已有 `ai-balance-plan.md`）时会被放大：搜索空间里一个越界点就可能产出"看起来合理但其实非法"的结果。
  - 注意部分字段在 `student_fire.c` 内有局部兜底（`speed/radius/lifetime`），所以**不是**所有非法值都会立刻爆；但兜底是"静默返回 false"，同样不可见。
- **建议的最小修复方向**：
  1. 在 `demo_config_validate()` 的 `student_hp` / `student_decision_ticks` 检查附近，加一组对称校验：
     - `student_fire_interval_ticks > 0`
     - `student_fire_damage > 0`
     - `student_bullet_speed` 有限且 `> 0`
     - `student_bullet_radius` 有限且 `>= 0`
     - `student_bullet_lifetime_ticks > 0`
     - `student_fire_min_range` 有限且 `>= 0`
  2. 复用文件内已有的 `is_finite_f()` 与 `FAIL(msg)` 宏（L153、L159-165），保持风格一致。
  3. 若改动会让 v1 配置失效，按文件头约定递增 `DEMO_CONFIG_VERSION` 并更新 `docs/demo-rules.md`。
- **复现时修订**：`core/demo_config.c` `9B868E4ACB95DCA8`。

---

## P1-3 `scripts/build.ps1` 在缺核心文件时仍去链接，把"缺文件"报成"构建失败"

- **现象**：缺核心源文件时，脚本只在开头打一行黄色提示，**随后照样编译测试对象并链接全部 `core_*.obj`**，最终以"构建失败"告终。使用者看到的是链接错误，而不是"文件缺失"。
- **最小复现命令**（用**临时副本**，仓库保持只读）：
  ```powershell
  $src = "<repo>"
  $tmp = "$env:TEMP\repo_missing"
  Remove-Item -Recurse -Force $tmp -EA SilentlyContinue
  New-Item -ItemType Directory -Force -Path $tmp | Out-Null
  Copy-Item "$src\core","$src\ai","$src\tests","$src\sim","$src\scripts" -Destination $tmp -Recurse -Force
  Remove-Item "$tmp\core\pattern_mine.c" -Force
  powershell -NoProfile -File "$tmp\scripts\build.ps1" -Target tests -Toolchain msvc -OutDir "$tmp\build\out"
  ```
  > 说明：本次复现在**临时副本**里做，未改动仓库任何文件。
- **实测输出**：
  ```
  [msvc] missing core sources: core/pattern_mine.c
  ... (仍继续编译测试对象、仍继续 link core_*.obj)
  MSVC build failed        ← 退出码 1
  ```
  即真实失败原因是 `LNK2019/LNK1120`（找不到 `pattern_mine_*` 符号），但**首要可见信息**只有 `missing core sources` 一行黄字，随后被"构建失败"淹没。
- **预期 vs 实际**：
  - 预期：缺 `core/` 源文件 → 明确报"缺哪些文件"并以该原因失败（或直接跳过需要它的目标）。
  - 实际：继续走到链接，把"缺文件"错误转译成"链接错误 + 构建失败"。
- **根因**：`scripts/build.ps1` 的两处"缺文件仍继续"：
  - MinGW：L92-96 计算 `$present` 并打印 `missing core sources (other tasks still running)`，L98-104 只编译 `$present`；但 L120-128 的 tests 分支**无条件**用 `$coreObjs` 链接。
  - MSVC：L145-149 同样只打印提示、只编译存在的文件；但 L174-179 的 tests 分支与 L170 的 sim 分支生成 `link ... "core_*.obj"`，通配符会匹配到**不完整**的目标集。
  - 注释里的 `(other tasks still running)` 说明这是**有意为多代理并行开发期**设计的宽容行为。问题是它把失败信号弄糊了。
- **影响**：
  - 在多代理并行开发期间（正是本仓库的现状）会**频繁**出现：一个代理删/改了核心文件，另一个代理跑 `build.ps1` 得到"MSVC build failed"，于是去排查链接错误，而不是"还差一个文件"。
  - 掩盖真实的编译错误：如果某个核心文件**编译失败**被跳过，最终仍表现为链接失败。
  - 与 P0-1 叠加：当前 `-Target tests` 本来就因符号重复而失败，缺文件的失败信息更难区分。
- **建议的最小修复方向**（推荐 1）：
  1. 在 tests/sim 分支链接**之前**加硬门：若 `$missing.Count -gt 0`，直接 `throw "missing core sources: $($missing -join ', ')"`（或 `Write-Error` + 非零退出），不再往下走。这样"缺文件"永远是"缺文件"。
  2. 若必须保留并行开发的宽容，则把链接目标从 `"$OutDir\core_*.obj"` 通配符改成**实际编译成功的 `$coreObjs` 列表**，让链接错误直接指向缺失符号，而不是先经历一轮通配。
  3. 无论哪种，都把 `missing core sources` 从"黄色提示"提升为最终错误摘要的一部分（例如最后打印 `BUILD_FAILED (reason: missing core sources: …)`）。

  > 补充：本代理注意到 `-Target sim -Toolchain msvc` 当前**退出 0**（`BUILD_OK`），因为 `scripts/build.ps1` L77 的 `$SimSources` 包含 `sim/scenarios.c`、`sim/boss_baselines.c` 两个**仓库中并不存在**的文件。sim 分支因为"`Get-MissingSources $SimSources`.Count -ne 0"而在 MSVC 路径上被整体跳过（L165），但结尾仍无条件 `echo BUILD_OK`。**这是与上一条同源的另一个表现**：脚本报告成功，却没有构建 sim。建议把 sim 的判定与 `BUILD_OK` 的打印条件对齐。
- **复现时修订**：`scripts/build.ps1` `3D5E3A5F1B9017D6`。

---

## P1-4 图形游戏的"画面/HUD/操作闭环"**未做真人验证**（明确声明）

- **现象**：本代理**本会话没有图形交互能力**（不能点击、按键、观察窗口、截图判读），因此图形端的画面正确性、HUD 可读性、操作手感与操作闭环**全部未验证**。
- **最小复现命令**（供有图形能力的验收者使用，本代理**未执行**）：
  ```powershell
  Start-Process <repo>\build\game\ustc_danmaku.exe -WorkingDirectory <repo>\build\game
  # 然后人工执行: 鼠标移动 / WASD / 1 2 3 4 / Esc / 右键 / R / 切窗口再切回 / 关闭窗口
  ```
- **本代理实际做到的**：只验证了"进程能起来、5 秒后仍存活、`MainWindowHandle != 0`、`Responding=True`、能干净结束"。
- **预期 vs 实际**：预期（作为交付验收）至少要确认"鼠标越近越慢且抵达即停""1/2/3/4 每次都能出招""Esc/右键暂停、R 重开生效""失焦后回来无残留按键""关闭窗口干净退出"。实际**以上均未验证**。
- **影响**：
  - `platform/input_win.cpp` 在本验收期间被**大幅重写**（`git diff --stat` 显示 423 行改动，`input_win.h` 增加约 602 行），改动核心是"EasyX 的 `peekmessage` 不转发 `WM_KILLFOCUS`/`WM_CLOSE`，改用窗口钩子截获"。**这是行为性改动，恰恰只能靠真人操作验证**，静态复核无法替代。
  - 因此不能把 §3 的静态结论当作"输入功能正确"的交付证据。
- **建议的最小修复方向**：
  1. 由有图形能力的验收者按上面的清单逐项真人验证，并把结果（含失败时的窗口截图）写入验收记录。
  2. 重点验证**钩子路径**：`WM_KILLFOCUS` 时按住键是否清零、`WM_CLOSE` 是否能退出（这两条是新钩子存在的**唯一理由**，若钩子没装上，行为会退回到旧 bug）。
  3. 重点验证**暂停/恢复**：暂停期间不动、恢复后不出现"时间灌入导致跳一大段"（`game_main.cpp` 用 `accumulator = 0.0` 防这个，但未实测）。
- **复现时修订**：`platform/input_win.cpp` `307BE4E6C113C77D`，`platform/input_win.h` `B02781E4ED346E0B`。

---

## P2-1 `render/scene.cpp` 的淋浴（SHOWER）预警把缝隙画在屏幕中央，而非真实缝隙中心

- **现象**：淋浴招的预警矩形带固定画在 `field_w * 0.5`，但实际安全缝隙是**逐波扫描**的。预警位置与真实安全通道可能对不上，玩家会被引到错误的通道。
- **最小复现命令**：
  ```
  cd <repo>
  findstr /N "DEMO_PATTERN_SHOWER" render\scene.cpp
  ```
  实测 `render/scene.cpp` 中该分支：
  ```cpp
  case DEMO_PATTERN_SHOWER: {
      line(0, 100, (int)cfg->field_w, 100);
      float gap = w->corridor_width;
      float center = cfg->field_w * 0.5f;          // ← 恒为屏幕中心
      solidrectangle((int)(center - gap*0.5f), 0, (int)(center + gap*0.5f), 140);
      break;
  }
  ```
  对照实现 `core/pattern_shower.c`：缝隙中心是逐波扫描的
  ```c
  static float shower_wave_center(const AttackPlan *plan, int32_t wave) {
      float step = plan->corridor_width * SHOWER_SCAN_STEP_FACTOR;
      float dir = (plan->gap_angle_deg >= 0.0f) ? 1.0f : -1.0f;
      return plan->wave_offset + dir * (float)wave * step;
  }
  ```
  且测试实测缝隙确实在移动，例如 `seed=3 方向=向左(-x)` 时相邻波缝隙交叠区从 `[757.5, 865.5]` 一路移动到 `[604.5, 712.5]`。
- **预期 vs 实际**：
  - 预期：预警画出**当前波的实际缝隙位置**（及扫描方向），与实际几何一致。
  - 实际：恒画在 `480±60`（`field_w=960`），与"缝隙在 604–866 之间游走"的实际情况不符（在 `gap_angle_deg=-90` 的 seed 上偏差可达 300+ px）。
- **影响**：
  - 预警是唯一的**可读性**信息源；画错位置会让玩家按预警走反而撞弹，属于"看起来是 bug 的难度"。
  - `PatternWarning` 结构里已经带了可供计算的信息（`corridor_width`、`gap_angle_deg`；`WorldView` 另有 `plan_*`），不改核心就能修。
  - 本代理**未做视觉验证**，因此这是**静态**判断；但从两侧源码读取的字段可以确定"绘制用的 center 与实现用的 center 不是同一个量"。
- **建议的最小修复方向**：
  1. 让 `PatternWarning`（或 `WorldView`）暴露**当前波的缝隙中心/扫描方向**，`scene.cpp` 据此绘制。最小改动是复用已有的 `view->wave_offset` 语义：把 `center` 换成"当前 tick 对应的缝隙中心"。
  2. 或者在本招预警里改为画**扫描方向 + 起始缝隙**（例如画一条从起始缝隙出发的横向箭头带），明确表达"缝隙在移动"，而不是画一个固定的假通道。
  3. 修完必须**真人视觉验证**（当前 `demo-validation.md` §5 标注为未执行）。
- **复现时修订**：`render/scene.cpp` `ED153FCF72AF1731`，`core/pattern_shower.c` `6EE994C553E703DC`。

---

## P2-2 `test_attack.c` 的文档化链接方式与 `scripts/build.ps1` 的链接方式互相矛盾

- **现象**：同一个测试有两种互不兼容的"正确编译方法"，且都写在仓库里。文件头说"不链接 `patterns.c`/`world.c`"，构建脚本却链接全部核心。
- **最小复现命令**：
  ```
  cd <repo>
  findstr /N "未链接 core/patterns.c\|尚无实现\|gcc -std=c11" tests\test_attack.c
  findstr /N "core_\*.obj\|tobj_" scripts\build.ps1
  ```
  证据：
  - `tests/test_attack.c` 文件头 L8-9：`因此 **本次未链接 core/patterns.c, 未验证真实招式几何**`；L9：`events_init / events_push: 仓库当前尚无实现(core/world.c 只调用), 测试内给出等价实现`。
  - `scripts/build.ps1` L178：`link ... "$OutDir\core_*.obj" "$OutDir\tobj_$($t.BaseName).obj"` —— 无条件链接全部核心。
- **预期 vs 实际**：预期仓库内对"某测试如何链接"只有**一个**权威说法；实际有两套，且第二套（脚本）会失败。
- **影响**：
  - 新人/其他代理会照着 `build.ps1` 跑，得到"仓库构建不了"的错误印象。
  - 文件头那句"`events_*` 仓库当前尚无实现"**已经过期**——`core/world.c:93-115` 已经实现了 `events_init`/`events_push`。这份注释会持续误导读者，也是 P0-1 的根因来源。
- **建议的最小修复方向**：
  1. 按 P0-1 方案 1 修好 `test_attack.c`（改用真实实现），然后**删掉**文件头那段过期的"测试内桩"说明。
  2. 若短期不修测试，至少在文件头加一行醒目的**当前状态**注记："本测试**不能**与 `core/patterns.c`/`core/world.c` 同时链接；`scripts/build.ps1 -Target tests` 目前会因此失败（见 docs/demo-findings.md P0-1）"。
  3. 顺带修 `events_init/events_push` 那句过期描述。
- **复现时修订**：`tests/test_attack.c` `C05D035F60C58B7A`，`scripts/build.ps1` `3D5E3A5F1B9017D6`。

---

## P2-3 构建告警未清理（EasyX C4201 / CRT C4996 / UTF-8 C4828）

- **现象**：图形构建产出大量重复告警，`/W4` 下把真正的信号淹掉。
- **最小复现命令**：
  ```
  cd <repo>
  cmd /c work\agents\lead\build_game.bat 2>&1 | findstr /C:"warning"
  ```
- **实际告警（三类，均非致命，`GAME_BUILD_OK` 退出 0）**：
  1. `easyx.h(330)(343)(355): warning C4201: 使用了非标准扩展: 无名称的结构/联合` —— 第三方头文件，每个 C++ 编译单元重复 3 次。
  2. `render/hud.cpp(98)(179)(193)(195)(203)(208)(210)(283): warning C4996` —— `wcsncpy`/`wcscat`/`wcscpy` 被判不安全。
  3. `core/attack.c(1): warning C4828` —— "文件包含…在当前源字符集中无效(代码页 65001)"，在某修订上出现（UTF-8/BOM 与源字符集设置不一致）。
- **预期 vs 实际**：预期 `/W4` 下的输出能突出真实问题；实际被这三类模板化告警刷屏（单次构建 96 行输出中大部分是告警）。
- **影响**：可维护性——真正的告警（例如未初始化变量、隐式转换）会被淹没。C4828 还提示**文件编码/BOM 不一致**，这在多代理并行写文件时容易演变成"某个文件在某个工具链下乱码"。
- **建议的最小修复方向**：
  1. C4201 来自 EasyX 头：用 `/external:I"$EZ\include" /external:W0`（或包一层 `#pragma warning(push/disable:4201)`）把它降为外部告警。
  2. C4996：在构建脚本里加 `/D_CRT_SECURE_NO_WARNINGS`，或把 `hud.cpp` 的宽字符拷贝换成 `wcsncpy_s`/`std::wstring`。
  3. C4828：统一源码编码约定（例如全部 UTF-8 **with BOM**，与 `scripts/build.ps1` 文件头"必须保存为 UTF-8 with BOM"的既有约定一致），并在 `AGENTS.md` 里写明。
  4. 建议把"告警数"纳入构建输出摘要，防止再次无声增长。
- **复现时修订**：`core/attack.c` `956937715C0FCDE8`，`render/hud.cpp` `639CB1EF1CCD0395`。

---

## P2-4 `docs/demo-guide.md` 的 headless 构建命令依赖通配符，与已验证的命令不一致

- **现象**：交付说明给的编译命令用 `%R%\core\*.c` 通配符，会把**所有**核心源文件一起编入；本代理实际验证通过的命令是**显式列举**。二者在文件集变化时行为不同。
- **最小复现命令**：
  ```
  cd <repo>
  findstr /N "gcc.exe" docs\demo-guide.md
  ```
  实测 `docs/demo-guide.md` L39：
  ```
  D:\mingw64\mingw64\bin\gcc.exe -std=c11 -O2 -I %R%\core -I %R%\ai -I %R%\sim %R%\core\*.c %R%\ai\student_bot.c %R%\sim\main.c %R%\sim\log.c -o %R%\build\sim\sim.exe
  ```
- **预期 vs 实际**：预期文档命令开箱可用；实际该命令**能工作**（本代理的 `-Icore -Iai -Isim` + 显式文件集构建退出 0，通配符版本等价），但存在两个隐患：
  - `-I %R%\core` 中 `-I` 与路径之间有空格。GCC 接受 `-I dir` 形式，但这是非惯用写法，容易被误改成不合法形式。
  - 通配符 `core\*.c` 会把**未来新增**的核心文件自动纳入，可能引入不该进 sim 的文件（例如将来加 `core/xxx_main.c`）。
- **影响**：低。当前可工作，属文档一致性与健壮性问题。
- **建议的最小修复方向**：
  1. 把 L39 改成与本代理验证一致的显式文件列表（可加注释说明"新增核心源文件需同步此列表"），或改用 `-Icore -Iai -Isim`（去掉空格）。
  2. 更好的是让文档直接引用 `scripts/build.ps1 -Target sim -Toolchain mingw`，避免同一件事有两份命令清单——但需先修好 P0-1/P1-3，否则脚本路径本身是红的。
- **复现时修订**：`docs/demo-guide.md`（本次未记录该文件哈希；与 P0-3 同一文件）。

---

## 附：本代理**未能**复现的问题（不计入清单）

以下项本代理尝试复现但**未观测到**，据实记录，不作为问题：

| 项 | 结果 |
| --- | --- |
| 内存越界 / 段错误 | 60+ 次 sim 运行 0 崩溃；各测试 0 崩溃（**未用 ASan/Valgrind**，故不等于"无内存错误"） |
| 同 seed 结果不稳定 | 未观测到；同 seed 两次 replay 逐字节一致，跨构建亦一致 |
| 非法参数导致崩溃 | 未观测到；`--seed abc`/`--script bogus`/`--max-ticks -5`/`--config other` 均干净退出码 2；`--help` 退出 0 |
| 弹池溢出导致崩溃 | 未观测到；`test_projectiles`/`test_pattern_shower` 的容量边界用例全 PASS，溢出计入 `overflow_events` |
| 事件缓冲区溢出导致崩溃 | 未观测到；`test_attack` 事件容量用例通过，溢出走 `dropped` 计数 |
| `int32_t`/`uint32_t` 强转导致的实际错误 | **未观测到**（但这不代表没有问题——见 P1-1，属未定义行为） |
| 学生负伤害导致 Boss 回血 | **未观测到**（默认配置 `student_fire_damage = 1`；仅静态判定"负值未校验"这一缺口） |

## 附：本代理**未执行**因而**未**下结论的项

图形交互试玩、真人操作闭环、真人视觉验证（HUD/画面可读性）、失焦钩子的实际运行行为、`build.ps1` 的 CMake 分支、内存越界/泄漏专项检测、AI 平衡性调优评估。以上均**不得**被视为通过。
