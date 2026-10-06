# 最小可玩 Boss demo：任务板

更新：2026-10-06。状态取值：`TODO` / `BLOCKED` / `RUNNING` / `REVIEW` / `DONE`。
**子代理宣布完成只进入 `REVIEW`；母代理验证后才记 `DONE`。**

执行平台事实（影响层级设计，必须如实记录）：
- 母代理模型：`deepseek-account/deepseek-flash`（平台显示 `deepseek-flash`）。
- `spawn_teammate` **不支持**指定模型，创建出的持久队友继承母代理路由。因此持久队友 `s01-audit` 实际运行在 `deepseek-flash` 上，与用户要求的 `a6api/deepseek-v4.1-flash` 不符。
- `subagent` / `subagent_fork` **支持**显式 `provider` + `model`。母代理已改用 `subagent(provider="a6api", model="deepseek-v4.1-flash")` 派发全部子任务，以满足用户"所有子智能体统一使用 a6api/deepseek-v4.1-flash"的明确指令。
- 实际模型标识：平台不回显运行中的模型身份，`list_subagent_models` 只列可选路由。**路由已请求，实际模型未能核验。** 不以此为已完成证据。
- 层级：平台下母代理为唯一可派生者，子代理会话禁止 `spawn_teammate`（S01 实测确认）。本 demo 实际为"母代理 + 直接子代理"两层；提示词中的孙代理层由母代理直接派发等价子任务替代，已在任务卡中记录该偏差。
- 定时任务实际由母代理派发，不再嵌套派生；不存在递归风险。

---

## 任务总览

| ID | 名称 | 层 | 指定模型 | 依赖 | 写文件 | 状态 | 证据路径 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| S01 | 仓库与规则只读审查 | 1 | a6api/deepseek-v4.1-flash（实际 `deepseek-flash`，见上方平台事实） | 无 | 无 | DONE（母代理已核对） | `work/agents/S01/S01-report.md` |
| F01 | 接口与配置冻结 | 0 | 母代理 | S01 | `core/*.h`、`core/demo_config.c`、`CMakeLists.txt`、`docs/demo-interfaces.md`、`docs/demo-rules.md` | DONE | 编译通过（GCC 8.1.0 `-std=c11 -Wall -Wextra -Werror`） |
| S02 | 确定性随机数 | 1 | a6api/deepseek-v4.1-flash | F01 | `core/rng.c`、`tests/test_rng.c` | RUNNING | `work/agents/S02/` |
| S03 | 扫掠碰撞 | 1 | a6api/deepseek-v4.1-flash | F01 | `core/collision.c`、`tests/test_collision.c` | RUNNING | `work/agents/S03/` |
| S04 | 角色移动和生命 | 1 | a6api/deepseek-v4.1-flash | F01 | `core/actors.c`、`tests/test_actors.c` | RUNNING | `work/agents/S04/` |
| S05 | 弹池和弹运动 | 1 | a6api/deepseek-v4.1-flash | F01 | `core/projectiles.c`、`tests/test_projectiles.c` | RUNNING | `work/agents/S05/` |
| S06 | 能量与单招状态机 | 1 | a6api/deepseek-v4.1-flash | F01、S02 | `core/attack.c`、`tests/test_attack.c` | TODO | `work/agents/S06/` |
| S07 | 绿色圆圈招式 | 1 | a6api/deepseek-v4.1-flash | F01、S02、S06 | `core/pattern_ring.c`、`tests/test_pattern_ring.c` | TODO | `work/agents/S07/` |
| S08 | 课表华容道招式 | 1 | a6api/deepseek-v4.1-flash | F01、S02、S06 | `core/pattern_course.c`、`tests/test_pattern_course.c` | TODO | `work/agents/S08/` |
| S09 | 一教金矿招式 | 1 | a6api/deepseek-v4.1-flash | F01、S02、S06 | `core/pattern_mine.c`、`tests/test_pattern_mine.c` | TODO | `work/agents/S09/` |
| S10 | 绩点淋浴招式 | 1 | a6api/deepseek-v4.1-flash | F01、S02、S06 | `core/pattern_shower.c`、`tests/test_pattern_shower.c` | TODO | `work/agents/S10/` |
| S11 | 脚本 AI 学生 | 1 | a6api/deepseek-v4.1-flash | F01 | `ai/student_bot.c`、`tests/test_student_bot.c` | TODO | `work/agents/S11/` |
| S12 | 学生反击 | 1 | a6api/deepseek-v4.1-flash | F01、S05 | `core/student_fire.c`、`tests/test_student_fire.c` | TODO | `work/agents/S12/` |
| S13 | Windows 输入适配 | 1 | a6api/deepseek-v4.1-flash | F01、平台 | `platform/input_win.cpp` | TODO | `work/agents/S13/` |
| S14 | 战场绘制 | 1 | a6api/deepseek-v4.1-flash | F01、S13 | `render/scene.cpp` | TODO | `work/agents/S14/` |
| S15 | HUD 与界面文案 | 1 | a6api/deepseek-v4.1-flash | F01、S14 | `render/hud.cpp` | TODO | `work/agents/S15/` |
| S16 | Headless 入口与记录 | 1 | a6api/deepseek-v4.1-flash | F01、S02..S12 | `sim/main.c`、`sim/log.c` | TODO | `work/agents/S16/` |
| S17 | 场景与 Boss 脚本对照 | 1 | a6api/deepseek-v4.1-flash | S16 | `sim/scenarios.c`、`sim/boss_baselines.c` | TODO | `work/agents/S17/` |
| S18 | 核心集成回归 | 1 | a6api/deepseek-v4.1-flash | S06..S12、S16 | `tests/test_world.c`、`tests/test_replay.c`、`tests/test_main.c` | TODO | `work/agents/S18/` |
| S19 | 运行手册与试玩模板 | 1 | a6api/deepseek-v4.1-flash | S14、S15、S16 | `docs/demo-guide.md`、`docs/demo-playtest.md` | TODO | `work/agents/S19/` |
| S20 | 独立验收与问题报告 | 1 | a6api/deepseek-v4.1-flash | S01..S19 | `docs/demo-validation.md`、`docs/demo-findings.md` | TODO | `work/agents/S20/` |

---

## 已冻结的母代理独占文件（子代理只读）

| 文件 | 说明 |
| --- | --- |
| `core/demo_base.h` | 全部公共类型与声明（接口 v1） |
| `core/world.h`、`core/attack.h`、`core/student_fire.h` | 世界与状态机接口 |
| `core/pattern_*.h`、`core/patterns.h`、`ai/student_bot.h` | 招式与学生 AI 接口 |
| `core/demo_config.c` | 配置默认值与校验（配置 v1） |
| `core/patterns.c` | 招式注册表 |
| `core/world.c` | 世界集成与 tick 顺序（母代理实现） |
| `CMakeLists.txt`、`scripts/*` | 构建与打包入口 |
| `game_main.cpp` | 图形薄入口 |
| `docs/demo-rules.md`、`docs/demo-interfaces.md`、`docs/demo-tasks.md` | 规则、接口、任务板 |

---

## 平台与工具链事实

| 项目 | 实测结果 |
| --- | --- |
| 本机 C 编译器 | MinGW GCC 8.1.0（`D:\mingw64\mingw64\bin\gcc.exe`，x86_64-w64-mingw32）；MSVC BuildTools 安装中 |
| EasyX | 官方 `EasyX_26.9.25.exe` 可下载，安装流程见 `runs/` 记录 |
| CMake | winget 安装中 |
| 网络 | easyx.cn 与 GitHub `git ls-remote`（HTTPS）可达；`api.github.com` 与部分 Fandom 域名被拒 |

---

## 变更记录

| 时间 | 变更 |
| --- | --- |
| 2026-10-06 | 建立任务板；F01 冻结完成；S02–S05 派发 |
