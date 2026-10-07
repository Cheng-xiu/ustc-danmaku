# 网页迁移：C 核心基线修复与回归

日期：2026-10-07。状态：以下窄范围核心修复已完成，原生 C 回归通过。**本记录不代表网页界面、Wasm 对照、浏览器性能或真人玩法验收通过。**

## 修复依据与范围

迁移复用已有 C 核心，以 [Demo 规则](demo-rules.md) 的获准规则为准。这些是基线缺陷修复，不是重新设计四招或引入新玩法。配置版本由集成代理统一升级为 v2；本次修复没有调整消耗、弹速、波次、发数、学生生命或胜负规则。

| 缺陷 | 修复 | 保持的边界 |
| --- | --- | --- |
| `attack_step` 已传攻击阶段相对 tick，但四个 `emit` 仍加减绝对 `plan.start_tick`；晚于开局的攻击漏发或不发 | 四招第三参数与 `wave_tick[]` 都以预警结束、攻击开始为零点；更新公共注释和旧淋浴测试的调用语义 | 请求仍在接受时锁定完整计划，预警时长、波次相对时刻和状态机不变 |
| 课表按 `field_h / n` 在整个战场同时生成弹，默认出生点达到 y=660，违背已批准顶部出生 | 保留列、x 抖动、通道、发数、速度与时刻；每列在顶部 y=0..100 紧凑排列：`n>1` 时 `y=100*k/(n-1)`，单发时 `y=100` | 顶部几何在 C 中生成，前端不得改出生点或补碰撞规则 |
| 矿点夹紧到边界后，目标学生可以绕过 180 px 安全距离检查 | 与其余学生一样，目标不足安全距离时直接拒绝，沿用原 0.001 px 浮点容差 | 不重抽随机、不挪矿点、不扣能量、不排队；后续进入锁定出生点的规则仍另行核对 |
| `truncated=true` 后状态仍为 RUNNING，后续 `world_step` 继续推进并反复产生截断事件 | 截断也进入停止分支；不推进 tick、运动、计时、能量或 RNG，不产生新事件 | 截断仍为未完成，不转换成 Boss 胜、败或平局；默认对局不设时限 |

## 实际回归

新增 `tests/web/test_core_regression.c`，链接 14 个真实核心/AI 源文件，没有招式或碰撞桩。

- 四招分别在实际推进后的 tick 0、300、1000 请求，检查每波生成时刻和发数，预警结束前不发弹，无容量溢出。
- Boss 与学生移动、锁定目标倒下后，攻击原点、目标、几何种子和通道初值保持接受时的计划；下一目标标记可独立改变。
- 攻击结束后本招 Boss 弹清除，单独注入且仍在寿命内的学生弹保留。
- 忙碌、能量不足和矿点非法几何均不扣能量、不延后执行；安全局面中的矿点仍可接受。
- 实际命中触发 Boss 胜、Boss 败及同 tick 双倒 Boss 胜；终局停止。实验截断停止且没有 GAME_OVER 事件。
- 直接读取真实课表 `emit` 的出生数据，检查顶部带、发数及速度。

为了隔离核心行为，夹具使用较高生命、较长学生射击间隔，并在波次检查中使用几乎静止的学生；锁定检查另恢复学生实际移动速度。终局检查直接注入明确命中的弹。以上均为测试夹具，不写入游戏默认配置，也不构成难度或真人体验结论。

实测编译器：`D:/mingw64/mingw64/bin/gcc.exe`，MinGW-W64 GCC 8.1.0，`-std=c11 -O2 -Wall -Wextra`。从仓库根目录运行：

```powershell
$regressionExe = Join-Path $env:TEMP 'ustc-web-core-regression-demo-scope-review.exe'
& 'D:/mingw64/mingw64/bin/gcc.exe' -std=c11 -O2 -Wall -Wextra -I core -I ai `
    tests/web/test_core_regression.c core/demo_config.c core/rng.c core/collision.c `
    core/actors.c core/projectiles.c core/attack.c core/patterns.c core/pattern_ring.c `
    core/pattern_course.c core/pattern_mine.c core/pattern_shower.c core/student_fire.c `
    core/world.c ai/student_bot.c -lm -o $regressionExe
if ($LASTEXITCODE -ne 0) { throw 'C regression build failed' }
& $regressionExe
if ($LASTEXITCODE -ne 0) { throw 'C regression failed' }
```

本次输出：`core regression: 10425 checks, 0 failures`，编译和运行退出码均为 0。

旧 `tests/test_pattern_shower.c` 的波次 helper 和 `emit` 调用已改为相对 tick，保留原几何、容量、边界和锁定断言。旧测试全集由集成代理另行运行，其结果不由本次回归代替。

## 集成还需验证

当前 `world_make_view` 的 `PatternWarning` 只提供粗略提示，不能独立准确绘制四招的全部预警：课表首通道、淋浴初始缝心和环弹逐波漂移等仍保存在 `AttackPlan` 内。Wasm 显示适配须从同一计划提供 C 端只读几何投影，复用真实生成计算，不由 TypeScript 重新实现四招算法；已公开的后续波次预警和 AI 的可见观察分别规定权限。

上述投影、Wasm/原生逐 tick 对照、默认配置下的对局、浏览器操作和性能采样不在本次回归结论内。历史报告也不能继承为本次构建通过；安全通道可达性与四招实际用途仍需集成及试玩检查。
