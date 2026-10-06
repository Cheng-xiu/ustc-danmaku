# 最小可玩 Boss demo：交付说明（母代理）

更新：2026-10-06。分支 `demo/minimal-playable`，提交 `fafe175`（基于 `main` 的 `7a2fe12`）。

## 1 这是什么

一个 Windows 上可玩的**最小可玩 Boss demo 原型**：玩家操作 Boss，用鼠标摇杆移动，主动选四种招式、花费共享能量，对战会躲弹并反击的脚本 AI 学生。在自己血量耗尽前击倒全部学生 = 胜。

本 demo 不是 v3.1 全量课程交付：不含 GPA、四阶段训练快照、GA/Q 表训练、擦弹与 `ddl 逆转` 技能、排行榜。

## 2 已实测的构建与运行

| 项目 | 命令 | 结果 |
| --- | --- | --- |
| 图形游戏（MSVC + EasyX） | `cmd /c work\agents\lead\build_game.bat` | 退出码 0，输出 `GAME_BUILD_OK`，产物 `build\game\ustc_danmaku.exe` |
| headless 仿真（MinGW） | 见 §3 命令 | 退出码 0，产物 `build\sim\sim.exe` |
| 单元测试（MinGW） | 每文件独立编译链接 | test_actors/test_collision/test_projectiles/test_rng/test_student_fire 全 PASS |

工具链（本机实测）：MinGW GCC 8.1.0（`D:\mingw64\mingw64\bin`）、MSVC BuildTools 2022（`vcvars64.bat`）、EasyX 26.9.25（解包在 `C:\Users\jhsly\.dsh\toolchains\easyx-26.9.25`，用 `lib_vc2015\x64\EasyXw.lib` + `user32/gdi32/winmm/shell32`）。

EasyX 官方安装包是 7-Zip SFX 且清单要求管理员权限（本会话无法提权），母代理用 7z 直接解包归档（签名偏移 36413）取得头文件与库，未运行安装程序。包内同时提供 MSVC 与 MinGW 两套静态库。

### 图形游戏构建命令（逐字）

```text
cmd /c work\agents\lead\build_game.bat
```

等价的可移植命令（`scripts/build.ps1`，需 UTF-8 BOM，Windows PowerShell 5.1 读取中文注释需要它）：

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Target game -Toolchain msvc -EasyXRoot C:\Users\jhsly\.dsh\toolchains\easyx-26.9.25
```

### headless 构建与运行命令（逐字）

```text
set R=<repo>
D:\mingw64\mingw64\bin\gcc.exe -std=c11 -O2 -I %R%\core -I %R%\ai -I %R%\sim %R%\core\*.c %R%\ai\student_bot.c %R%\sim\main.c %R%\sim\log.c -o %R%\build\sim\sim.exe
%R%\build\sim\sim.exe --seed 12345 --script mixed --max-ticks 7200 --log %R%\build\sim\run.log
```

## 3 操作方式（已批准规则，见 docs/demo-rules.md）

| 操作 | 作用 |
| --- | --- |
| 鼠标移动 | Boss 朝指针方向移动，离指针越近越慢，抵达即停（florr.io / digdig.io 风格） |
| WASD / 方向键 | 等价八方向移动，斜向归一化 |
| 1 / 2 / 3 / 4 | 主动触发四种招式（金矿 / 环弹 / 课表 / 淋浴） |
| Esc 或鼠标右键 | 暂停 / 继续（逻辑全冻结，恢复不补暂停时间） |
| R | 重开（清弹幕、计划、目标、能量、AI 状态、无敌与输入边沿） |

## 4 四招与当前试验用途（配置版本 1）

| ID | 招式 | 档位与用途 | 消耗 | 预警 | 攻击 |
| --- | --- | --- | --- | --- | --- |
| 0 | 桃李苑·绿色圆圈好辣 | 中消耗·周边压力 | 30 | 1.2 s | 2.5 s，3 波×18 发带 80° 缺口 |
| 1 | 选课系统·课表华容道 | 中消耗·封路 | 35 | 1.2 s | 3.0 s，3 波×24 发，保留 ≥130 px 通道 |
| 2 | 一教金矿·绩点淘金 | 低消耗·追击 | 15 | 1.2 s | 2.0 s，3 扇面×12 发，矿点距目标 ≥180 px |
| 3 | 期末总评·绩点淋浴 | 高消耗·多目标压制 | 60 | 1.2 s | 5.0 s，5 波×16 发，保留 ≥120 px 缝隙 |

能量上限 100、初始 60、每秒恢复 10。学生 3 名、每人 3 血；Boss 6 血，受击无敌 0.6 s。学生每 1.3 s 朝 Boss 当时位置发射直线弹（不追踪）。同 tick 双方倒下记 **Boss 胜**（已批准）。

## 5 已验证的行为（实际运行证据）

| 检查项 | 证据 |
| --- | --- |
| 完整对局可结束（失败方向） | `--script wait --max-ticks 7200` → `RESULT status=BOSS_LOSE tick=478 boss_hits=6` |
| 完整对局可结束（胜利方向） | **本轮实测未复现**。见 §5.1 |
| 四招都被真实请求 | `pattern=0/1/2/3 requests=1015/997/1038/738` |
| 能量扣减与拒绝 | `accepts=16 rejects=3772`，拒绝原因为 BUSY/NO_ENERGY；能量随恢复上升 |
| 弹幕双向生效 | `boss_hits=3~6 student_hits=4~9`，学生弹与 Boss 弹都造成伤害 |
| 可复现性 | 同 seed 两次 `--replay` 输出逐行一致（45 行，Compare-Object 无差异） |
| 截断不判胜负 | `--max-ticks 300` → `RESULT status=TRUNCATED tick=300` |
| 错误处理 | `--seed abc` → 退出码 2 + 明确错误 |
| 图形程序可启动 | `ustc_danmaku.exe` 启动后 `MainWindowHandle` 非 0、`Responding=True`、结束无残留 |

### 5.1 必须知道的当前缺陷：Boss 胜利在实测中不可达

2026-10-06 修正：本文件早先引用的 `BOSS_WIN tick=4196` **取自过期日志，不可复现**，已删除。独立验收（S20）跑 60+ 次从未出现 `BOSS_WIN`；母代理用当前提交复测 4 种脚本 × 6 个 seed = **24 局**，结果：

| 脚本 | WIN | LOSE | TRUNCATED | 崩溃 |
| --- | --- | --- | --- | --- |
| mixed | 0 | 5 | 1 | 0 |
| dodge | 0 | 5 | 1 | 0 |
| patrol | 0 | 6 | 0 | 0 |
| wait | 0 | 6 | 0 | 0 |

**结论：当前配置下"击倒全部学生"这一胜利条件在自动脚本中不可达，Boss 稳定失败。** 这是玩法平衡问题（P1），不是逻辑崩溃：对局能正常结束、能正确判负、能重开。

初步定位（待进一步确认，不是结论）：Boss 弹命中后学生进入 0.3 s 无敌，而三招的弹在时间上高度集中，导致"命中"次数被无敌窗口大量吸收；`mixed` 脚本 12 次成功出招只换到 6 次学生掉血（3 名学生 × 3 血 = 9 次所需）。建议的下一轮最小调整方向（需真人试玩与实测支持，不得凭此直接改规则）：先测"学生无敌时长 / Boss 弹伤害 / 学生血量"三者中单独一项的变化，记录 20 seed 的胜负分布，再决定是否调整配置。

**在这条缺陷修复前，"玩家能赢"这一体验未经验证。** 不得把本 demo 描述为已通过可玩性验收。

## 6 未验证 / 待办（如实记录）

- **未做真人试玩**：无图形交互能力，操作手感、可理解性、是否有趣全部待真人验证。
- **图形画面未做视觉核对**：仅源码静态复核绘制与 HUD 字段。
- `tests/test_attack.c` 与 `tests/test_pattern_shower.c` 当前不可用/有失败项（详见 `docs/demo-findings.md`）。
- 本轮延后项见 `docs/demo-rules.md` 第 1.7/4 节。
