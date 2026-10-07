# 手动预瞄无尽网页 Demo v6 工程验证

日期2026-10-07。当前配置v6、Web ABI5，PixiJS/TypeScript显示、同一16个C/AI源码用于Native和Wasm。游戏文件为demo/ustc-danmaku.html。[v5工程记录](archive/web-demo-validation-v5-20261007.md)逐字保留；旧自动方向平衡结论与本版分别记录。

## 已实现

按住1–4或HUD技能按钮预瞄，松开释放，Space取消。按住不扣费、不开始CD、不暂停世界；释放由C原子检查几何/能量/CD，拒绝不排队。新一次选择先从当前Boss指向鼠标，持有后仅鼠标真实移动更新方向；Boss移动、松手或窗口缩放不自动转向。12px死区保持最后有效朝向。WASD移动优先，取消不清移动键。

四招为前向160°宽弧双波、可转向分列墙、Boss原点40°窄扇三连、可转向扫描雨；费用25/50/20/100，保持既有速度/占用时间。墙/雨从场地后缘入场。纯C预瞄工厂和真实发弹共用emitter，完整几何接受时冻结，TS只绘制第一波大概轨迹和方向箭头。预瞄不是整段必然命中的保证。完整规则、技术边界和玩法建议见[瞄准审查](validation/aim-v6.md)。

修复实测发现的两处问题：开始新一次预瞄必须对准当前鼠标，不能沿用走位前的角度；页面销毁后晚到visibility/blur不得访问已销毁Graphics。另补新局尺寸验证，避免极宽短场地成功开始后某方向的弹墙始终不可用，非法重开保留旧世界。

## 实际验收

| 检查 | 结果 |
| --- | --- |
| Native CTest | 13/13；手动几何338288检查、World预瞄/释放事务1774检查、场地39773检查均0失败；旧碰撞、无尽/GPA、弹池与攻击回归通过 |
| Native桥接 | 四manual预瞄与接受warning逐字节一致；预瞄不改原snapshot、忙碌仍有合法rays；legacy四招真实波次继续一致 |
| Native/Wasm | 6legacy+4manual场景，36010快照、所有离散字段/事件一致，最大float差0.000003814697265625，小于0.002容差 |
| 输入DOM夹具 | 54用例/431断言通过，包含最新owner、pendingrelease取消、死区、resize、新hold当前mouse、持有Boss移动稳定 |
| 最终离线Chrome | 282/282检查、三窗口1440×900/1024×768/390×844，四招候选与锁定warning最大误差0；实际首发位置/速度/半径匹配，0error/0外部HTTP |
| 真实交互 | 长按无扣费、松开单次、Space保留WASD、短tap、HUD拖出释放、旧owner事件、pause/blur/R防晚到释放、越过静止mouse不翻转、新hold重算、resize不转向与重开比例均通过 |
| 无尽实际回放 | 18检查；公开状态WASD避弹与真实mouse+hold/release，3次课程技能在tick866真实击倒3人清波，HP3/GPA0.56；完整120tick固定marker，tick986精确出生4人，tick1016仍playing，R清零 |
| 最终嵌入Wasm场地 | 75检查/32previews；1800×384及转置拒绝且旧已开招snapshot不变；390×1772.3077及转置边界场地两招八方向合法 |
| TS/产物 | strict tsc、Vite、Emscripten6.0.11和单文件通过；内嵌Wasm/JPG逐字等于源码资源，许可证完整，无外部script/link |

截图已目视核对：[桌面预瞄](assets/aim-v6-1440.png)、[竖屏预瞄](assets/aim-v6-390.png)、[第二波](assets/aim-endless-v6-wave2.png)。原始11份JSON/日志位于[validation/aim-v6](validation/aim-v6/)，含构建/CTest/bridge及实际浏览器输入证据。独立HUD人工snapshot夹具明确单列，不能代替完整核心验收。

浏览器使用Chrome154.0.8037.98与Playwright1.56.1，导航前安装并暂停受控时钟，仅读取公开display快照，用真实keyboard/mouse事件操作；没有注入生命、位置、弹、能量或RNG。blur与主动capture loss的合成处理器检查在JSON中标明，未宣称真实操作系统隐藏或BFCache验收。36010快照包含终局后的静态重复，不等于36010活跃tick。自动回放不代表真人或强化学习。

## 最终文件与范围

HTML 1168141字节，SHA256 `9de27925103b644236b736e635932be215ecd0440d095311657c08673d22680d`；内嵌Wasm 62152字节，SHA256 `9dbd096c2eac3792de2c22b5af54725591fd99502f6239b16a41257a13c21b93`。源与官方校徽指纹、许可证检查见[资源报告](validation/aim-v6/v6-artifact-integrity.json)。网页发布相同已测字节，线上结果见[Pages记录](github-pages.md)。

本轮没有重新测FPS、弱设备、长时内存或真人乐趣，也没有进行新几何的多种子策略平衡。旧v5的2808局只能作为历史自动方向对照。当前证据支持手动输入、弹道一致性和无尽流程正常；长期难度和新手瞄准负担需真人试玩。后续训练沿用C手动方向入口，不能以legacy自动锁定替代网页行为。

复跑命令见[运行指南](web-demo-guide.md)；可双击离线HTML，Pages在线无需安装。最终打包记录与线上部署身份在发布后补充。
