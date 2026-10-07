# 网页 Demo 默认配置可玩性探针

日期：2026-10-07。对象：本地 `ustc-danmaku-web-demo` 工作树的默认配置 v2、3 名学生、种子 20261006。

**已找到未修改默认规则的真实 Boss 赢局。** 原生 C 核心有限采样完成：96 个预设脚本中，50 个 Boss 胜、44 个 Boss 败、2 个在探针的 7200 tick 预算内未结束。它们是同一固定种子的相关脚本，不能解释为总体胜率，也不代表真人体验、浏览器性能或 ML 训练结果。

## 方法与授权边界

探针位于 [playability_probe.c](../tests/web/playability_probe.c)。配置只由 `demo_config_init` 初始化，没有改血量、移动速度、学生策略、弹速、能量、生命、种子或游戏时限。学生仍使用默认未训练的脚本。世界只通过真实 `world_reset/world_step` 推进；没有直接伤害、注入弹、强制位置或替代碰撞。

策略只收到 `PublicState`。其来源白名单为当前 Boss 位置、学生位置/生命/存活、当前屏幕内学生弹的位置/速度/半径、HUD 招式可用状态与逻辑 tick；不读取隐藏 RNG、学生内部状态、攻击计划或未来波次。主代理在执行前已核对观察白名单与输入生成并批准运行。

输入为八方向键盘轴及停止，每次只有一个招式请求，两次请求至少隔 12 tick；指针输入关闭，瞄准由原核心决定。部分脚本根据当前可见直线弹估算未来 0.45 秒的接近风险；这只是选择输入，不推进复制世界，也不生成未来弹。首条保存的赢局关闭此避险估算。

采样包含：

- 6 个站桩脚本：环、课表、矿、淋浴、按可见局面混招、脚本显式循环请求。
- 追击最近学生与两个方向的绕行：距离目标 48/100/180 px，避险权重 0/4/20，分别仅环、仅矿或混招，共 81 个。
- 四个公开固定边缘路点移动：三个避险权重、三种出招，共 9 个。

距离和避险权重是控制器参数，不是游戏规则修改。每局最多驱动 7200 tick（120 秒），游戏配置 `max_ticks` 仍为 0；`BUDGET_UNFINISHED` 表示驱动停止采样时世界仍运行，不能记作游戏平局或失败。进程另有 120 秒 CPU 上限，实际完成全部 96 个样本所用 CPU 时间为 0.743 秒。该数字仅用于说明本次采样预算，没有浏览器帧率含义。

## 结果与可复现赢局

完整本次输出归档在 [playability-probe-output.txt](validation/playability-probe-output.txt)，含全部 96 行 CSV、第一条赢局保存消息和采样汇总。下表选择几个有解释力的样本。用招列按“环/课表/矿/淋浴”计接受次数；命中列按“Boss 受击/学生受击”计次数。

| 脚本 | 结果 | 终止 tick / 秒 | Boss 剩余生命 | 学生剩余生命合计 | 命中 | 用招 | 拒绝次数 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| stand-spell0 | Boss 败 | 360 / 6.00 | 0 | 9 | 6/0 | 2/0/0/0 | 0 |
| stand-spell3 | Boss 败 | 461 / 7.68 | 0 | 1 | 6/8 | 0/0/0/2 | 0 |
| move1-d48-avoid0-spell0 | **Boss 胜；保存输入** | 1470 / 24.50 | 1 | 0 | 5/9 | 7/0/0/0 | 0 |
| move1-d48-avoid20-spell0 | Boss 胜 | 1531 / 25.52 | 6 | 0 | 0/9 | 7/0/0/0 | 0 |
| move1-d48-avoid20-spell2 | 预算内未结束 | 7200 / 120.00 | 6 | 1 | 0/8 | 0/0/8/0 | 472 |
| move1-d100-avoid4-spell2 | Boss 胜 | 2247 / 37.45 | 5 | 0 | 1/9 | 0/0/8/0 | 64 |
| move1-d48-avoid20-spell4 | Boss 胜 | 1826 / 30.43 | 5 | 0 | 1/9 | 6/0/1/1 | 0 |
| move3-d180-avoid0-spell4 | Boss 胜 | 412 / 6.87 | 6 | 0 | 0/9 | 0/0/1/1 | 2 |
| move4-d48-avoid20-spell0 | 预算内未结束 | 7200 / 120.00 | 6 | 3 | 0/6 | 33/0/0/0 | 0 |

六个站桩脚本全部在 353–467 tick（约 5.88–7.78 秒）失败。50 个赢局中有 30 个 Boss 零受击；这些是确定脚本结果，不能推断真人容易无伤。

第一条赢局输入归档在 [winning-inputs.json](validation/winning-inputs.json)：

- 默认种子低 32 位为 20261006，高 32 位为 0，学生数量 3。
- 控制器为 `move1-d48-avoid0-spell0`：接近最近存活学生约 48 px，仅请求环弹，不使用弹的预测避险。
- 输入含 tick 0..1469 的 1470 条合法轴和单招请求；执行后应为 `status=1`（Boss 胜）、`tick=1470`、Boss 生命 1、Boss 受击 5、学生受击 9。
- 文件 SHA256：`D051E74407C5900B27884D232FB009EA519B002F0145BDC321E18139B88A6068`。
- 已检查 tick 连续、轴长度不超过 1（浮点容差）、指针关闭、攻击 mask 只含单招及请求间距至少 12 tick。集成代理已独立 Node/Wasm 复放一致；另一代理经真实浏览器 WASD/数字键逐 tick 复放也一致：tick 1470、Boss 生命 1、受击 5/9、全部学生倒下，无页面异常。见 [验证报告](web-demo-validation.md) 和 `docs/validation/winning-browser-result.json`。

## 需要真人体验验证的风险

### 能量取舍在默认数值下退化

默认每秒恢复 10 能量、初始 60、上限 100；四招初始都可支付。能量在预警和攻击期间持续恢复，单攻击通道在这段时间占用。

| 招式 | 预警 + 攻击占用 | 占用期间恢复 | 消耗 |
| --- | --- | --- | --- |
| 环弹 | 3.7 秒 | 37 | 30 |
| 课表 | 4.2 秒 | 42 | 35 |
| 矿点 | 3.2 秒 | 32 | 15 |
| 淋浴 | 6.2 秒 | 62 | 60 |

恢复量均不少于消耗，所以在正常完成一招、回到 idle 时，共享能量不会成为限制。“攒能量还是立即进攻”的取舍当前数值基本退化为等待攻击通道空闲。依据为 `core/demo_config.c` 的招式时长/消耗与能量默认值，以及 `core/world.c` 的逐 tick 恢复代码。这是当前配置的设计风险，尚未经真人体验验证；本轮没有擅自调参。

### 可用按钮与矿点实际接受存在差距

只用矿点的 `move1-d48-avoid20-spell2` 在 120 秒预算内有 472 次拒绝，仅接受 8 次，剩一名 1 血学生。另一个仅矿赢局也有 64 次拒绝。持续拒绝可能使玩家难以理解按键为何不起作用。

源码中 `world_pattern_available`（`core/world.c:225`）检查 idle、能量与存活目标，但不检查矿点几何可行性；计划生成失败路径（`core/attack.c:141`）又使用 `DEMO_REJECT_NONE`。矿点安全距离不可满足是现有合法拒绝路径。这次探针未逐次记录拒绝原因和位置，不能断言 472 次全部由同一几何原因产生。建议集成时核对实际失败反馈，真人体验时尝试走位和切换招式；不能据这个脚本断言全局卡死或 Boss 不可能赢。

### 近身追击与很短的混招赢局需要观察

默认学生在距离不超过 60 px 时不反击（`core/student_fire.c:116`），规则也没有角色接触伤害。约 48 px 的近身策略因此值得真人验证：这是既有规则行为，不是探针新增的保护。

混招样本存在 412 tick（约 6.87 秒）、Boss 满血的赢局，只接受一次矿和一次淋浴就产生 9 次学生受击。这个具体样本提示可能有很短的胜局节奏，但不能直接推断某招普遍过强。后续应让真人比较近身、常规距离和混招的操作体验，并扩展独立种子；任何规则或数值调整另行获得批准。

## 构建与复跑

实测编译器为 MinGW-W64 GCC 8.1.0；以 `-std=c11 -O2 -Wall -Wextra` 链接 14 个真实核心/AI 源，没有碰撞或招式桩。编译与执行退出码均为 0，编译无告警。从仓库根目录运行：

```powershell
New-Item -ItemType Directory -Force 'build/web-validation' | Out-Null
$playProbeExe = Join-Path $env:TEMP 'ustc-web-playability-probe-demo-scope-review.exe'
& 'D:/mingw64/mingw64/bin/gcc.exe' -std=c11 -O2 -Wall -Wextra -I core -I ai `
    tests/web/playability_probe.c core/demo_config.c core/rng.c core/collision.c `
    core/actors.c core/projectiles.c core/attack.c core/patterns.c core/pattern_ring.c `
    core/pattern_course.c core/pattern_mine.c core/pattern_shower.c core/student_fire.c `
    core/world.c ai/student_bot.c -lm -o $playProbeExe
if ($LASTEXITCODE -ne 0) { throw 'playability probe build failed' }
& $playProbeExe 'build/web-validation/winning-inputs.json'
if ($LASTEXITCODE -ne 0) { throw 'playability probe failed' }
```

这是原生核心中的有限默认配置验证。未执行真人键鼠操作、网页帧率测量、ML 训练或多种子难度验收；这些项目不能由本探针替代。
