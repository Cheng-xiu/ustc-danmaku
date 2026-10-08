# 网页无尽 Demo 运行指南

当前配置 v6、Web ABI v5。[在线试玩](https://cheng-xiu.github.io/ustc-danmaku/)，或双击 `demo/ustc-danmaku.html` 离线打开；所有资源和许可证均在单文件内。

## 操作与界面

顶部显示生命、GPA、波次、击倒、明显的能量条和共享出招 CD；余下窗口为战场。四招说明放在开始菜单，开始后隐藏。鼠标移到顶部栏仍朝指针方向移动；移出浏览器可观察区域保留最后位置，失焦自动暂停。有 WASD/方向键输入时优先键盘，按住数字 1–4 或顶栏技能按钮预瞄，松开释放，Space 取消，Esc/战场右键暂停，R 重开。

手机默认使用左摇杆、右转盘：在左下区域按下形成摇杆圆心，轻推慢移、推远快移；按住右侧技能转盘并滑向一招预瞄，左杆调整方向，松开转盘释放。四个方向为上1、右2、下3、左4；中心未选时松手不发招，滑回中心撤选后可再滑重新选择。松杆立即停步并保留最后朝向；选招手指不影响角色移动或瞄准。上滑到“取消”区域或按取消按钮永久撤销当前手势，移动不中断。菜单与顶部“触屏/键鼠”按钮可手动切换；自动识别依据触屏能力及手机/iPad/主指针信号，不按窗口宽度猜测。横竖屏、旋转清理和实测边界见[手机操作](mobile-controls.md)。

每局开始按战场窗口比例取等面积场地，保持面积 960×720。用户选择中途改变窗口比例时本局等比显示，重开才按新比例铺满；圆形校徽和已公开弹道不变。极小窗口重开若不满足合法场地，提示调整窗口并保留旧暂停局。

| 键 | 用途 | 能量 | 预警 + 攻击期 |
| --- | --- | --- | --- |
| 1 | 桃李苑·绿色圆圈好辣，近中距前向双波 | 25 | 0.4 + 1.5 秒 |
| 2 | 选课系统·课表华容道，分列弹墙 | 50 | 1 + 4 秒 |
| 3 | 一教金矿·绩点淘金，窄扇三连 | 20 | 0.3 + 1.25 秒 |
| 4 | 期末总评·绩点淋浴，全场扫描雨 | 100 | 1.5 + 5.5 秒 |

共享能量初始 60、上限 100、每逻辑秒恢复 10；接受才扣费，拒绝不扣、不排队。CD 是当前招式占用的剩余时间，IDLE 才可再出招；没有四套独立冷却。金矿 Boss 原点与全部存活学生距离不足120时拒绝，可换位置或换招。

初始 3 名学生，清波后 120 tick（2 秒）出生预告并增加 1 名，最多8名后持续刷新；只有Boss死亡结束。GPA仅由累计真实击倒n计算 `floor(430*n/(n+20))/100`，20人为2.15，100人为3.58。暂停/失焦/页面隐藏/死亡停止逻辑，重开清零。

按住期间游戏继续运行，不扣费、不启动 CD。键鼠模式新一次预瞄从当前 Boss 指向鼠标；持有后角色走动只平移预瞄原点，鼠标实际移动才改变方向，松手不重新计算。鼠标距 Boss 不足 12 个逻辑像素时保留最后有效方向，避免近处抖动或反转。没有鼠标朝向时使用最近键盘朝向，默认向上。触屏模式使用左杆最近有效方向，右侧转盘只选招。方向预瞄只画第一波的大概轨迹，并不保证整条线都会命中；松手后 C 锁定完整计划。

四招沿选定方向发射：1 为 160° 前向宽弧，2 为可转向分列墙，3 为 40° 窄扇三连，4 为可转向扫描雨。墙和雨从场地后缘生成，箭头表示整体行进方向。能量/CD 不足仍可预瞄，松手时检查；拒绝不排队，条件恢复不会自动补发。取消、暂停、失焦、重开、死亡会清除待释放。

## 构建

需要 Node 22.12+、npm 10+、固定 Emscripten 6.0.11。原生回归另需 CMake 3.16+ 和 C/C++ 编译器；当前 Windows 脚本可使用 MinGW，浏览器验收使用本机 Chrome。从仓库根执行：

```powershell
npm.cmd ci
npm.cmd run setup:wasm
npm.cmd run build
```

`setup:wasm` 安装/激活固定 SDK；已安装同版本时无需重复执行。`build` 依次构建共享 C 的 Wasm、`apps/web` 网页和自包含单文件，输出 `build/release/ustc-danmaku-endless.html`。根目录安装后，依赖使用根 `node_modules/`，不在 `apps/web/` 再运行 `npm ci`。

原生与 Wasm 读取 `packages/core/sources.txt` 的同一 16 个 C/AI 源文件，TypeScript 只做显示与输入。原始 `apps/web/index.html` 需 Vite/HTTP；可双击交付文件是生成后的单文件。应用和共享包职责、旧路径对应关系见 [monorepo 指南](monorepo.md)。

开发先运行 `npm.cmd run build:wasm`，再运行根 `npm.cmd run dev`；网页生产输出在 `apps/web/dist/`，使用根 `npm.cmd run preview -- --port 4173` 预览。修改 C 后先重建 Wasm。最终单文件无需服务器；已提交交付物为 `demo/ustc-danmaku.html`，更新它前先验收构建输出，发布方式见 [Pages 指南](github-pages.md)。

## 验证与复跑

```powershell
npm.cmd test
```

完整入口先构建，再执行原生 CTest、输入、逐 tick Native/Wasm 对照、浏览器和无尽回放；失败会停止后续步骤。已有构建时，可从根逐项复跑：

```powershell
npm.cmd run build:native
npm.cmd run test:input
npm.cmd run test:core
npm.cmd run test:browser
npm.cmd run test:endless
npm.cmd run test:mobile
```

迁移或修改构建/打包入口时，另运行 `npm.cmd run test:monorepo`，检查开发、生产预览、仓库服务器及下载包服务器的真实启动与发招；每次创建一个新试玩包。源码包可运行 `npm.cmd run package:source -- -OutDir build/release-monorepo-source`，选择未存在的输出路径。

浏览器脚本需要本机 Chrome，使用真实鼠标/键盘与受控时钟；QA 只读。aim-browser-smoke 检查三种窗口、按住/松手/取消、四招 C 预瞄与接受后弹道、真实首发弹、HUD 拖出释放、方向稳定、新一次按住对准当前鼠标、缩放和生命周期。aim-endless-smoke 验证当前手动操作的清波和下一波出生。受控时钟用于逻辑验收，不作为 FPS 测量。

`test:mobile` 先执行设备识别和手机输入探针，再用Chrome设备模拟及CDP真实多点触摸检查最终单文件。`test:mobile:input` 和 `test:mobile:browser` 可独立复跑；后者需要已完成构建。手机浏览器模拟不等同于实体iOS Safari、Android WebView或真人手感测试，原始结果与限制记录在手机报告。

目录迁移后已按上述根入口重新构建并通过原生、输入、对照、单文件浏览器与无尽回放，见 [迁移复验](monorepo-validation.md)。当前玩法的既有证据见 [v6 工程验证](web-demo-validation.md) 和 [瞄准规则与玩法建议](validation/aim-v6.md)。此前 [v5 平衡报告](web-balance-findings-v5.md) 记录旧自动方向弹形，不能替代 v6 手动瞄准的真人平衡验收；历史脚本与报告独立保留。

可选打包：`npm.cmd run package:web -- -OutputRoot build/release-v6`（先完成构建和验收）。输出目录和 ZIP 存在时明确拒绝覆盖。加载错误时重新完整构建并使用支持 WebAssembly/WebGL 的浏览器，不放宽 ABI 校验。
