# 无尽网页 Demo v4 工程验证

日期：2026-10-07。当前配置v4、Web ABI v3，单文件交付 demo/ustc-danmaku.html。原生与网页共享14个C核心/AI源文件。历史配置v2有限局报告保存在 [归档](archive/web-demo-validation-v2-20261007.md)，其胜局与数量不沿用于当前无尽版。

本轮由Codex及其子代理完成，没有使用提示词规划的外部DeepSeek路由。原始主工作树的未提交test_attack.c和完整原PDF未修改，SHA256仍为 C05D035F60C58B7AD9FDEACA7E6DB526397CDDC5D6E2AE2A5E85AB4B582F9C51 / 1668d0d3d62a8fa991f6822b85d101925ef434bb8c139370abcace2f6b3b4f7a。

## 实现与实测

官方JPG由Pixi圆形mask裁切，校徽圆盘与Boss22px命中半径对齐，原图字节保留。GPA由C用64位中间值按累计真实击倒n计算 floor(430*n/(n+20))，向下保留两位；无逐杀累计舍入。无尽学生从3增至8后持续刷新，只有Boss死亡结束。

| 检查 | 实际结果 |
| --- | --- |
| 原生 CTest | 9/9通过；核心回归10214检查，无尽/GPA4694检查，淋浴1561子项均0失败 |
| ABI独立探针 | 生命周期、64位seed、8学生、四招真实预警与发弹一致；42/72/36/48发，淋浴自然蓄能至tick240后接受 |
| C/Wasm回放 | 三个seed/数量组合10803快照 + 默认150秒多波回放9001快照，最大float差0（预设容差0.002），离散word/事件全部相同 |
| 离线单文件基础 | 当前仓库HTML13/13通过，零外部HTTP请求、零JS运行异常；4招初始60拒绝、自然100接受、真实发弹、暂停/GPA冻结、R清零、死亡终局 |
| 广泛浏览器交互 | 45/45通过；四招接受/边沿/真实发弹、忙碌拒绝、满能量门槛、真实WASD和缩放鼠标、暂停/失焦/重开/页面生命周期、死亡冻结 |
| 最终HTML实际清波 | 21/21通过；真实键盘输入2089次、2178次RAF推进、11个原生里程碑一致；tick1939清第一波，tick2059恰好120tick后4人按marker位置出生；GPA0.56，继续playing |
| 出生预告 | 119个中间tick预告点不变；清波仍为RUNNING，无胜利界面；重开回到0分、第一波 |
| 圆形/布局 | 合成渲染夹具圆外四角为背景、场景销毁正常；真实四人波2截图圆形正确、HUD720无滚动；按钮bottom814，小于可见clipBottom822 |
| 数值平衡 | 12种子1152局主要前后对照完整，零弹池溢出；6调参/6留出种子分开，静止/48px/150px/绕行、单招/混招/不出招均覆盖 |
| 打包资源 | HTML内嵌Wasm与当前生成wasm逐字一致，JPG与官方原图逐字一致，无外部script/link；独立文件可复制试玩 |

10803组中含终局后的重复快照，不代表10803个活跃tick。9000tick回放仍运行：第4波6学生、已清3波、击倒12、HP6、GPA1.61；这是预算未结束，没有判胜或实验截断。GPA上限趋势和8人重复波次由核心定向测试覆盖，不把短浏览器回放称完整无尽难度验收。

基础与多波浏览器测试以offline file://打开最终HTML。多波测试用真实输入处理器与受控时钟，QA只读，未向核心注入状态。暂停前后的逻辑时间、能量/GPA与敌人行为均通过。菜单点击后鼠标移出战场，避免与键盘回放混用。

页面生命周期的BFCache检查是派发persisted事件核对处理器，并非证明真实浏览器发生缓存；实际后台标签、多浏览器和移动端未验收。广泛输入检查后，末次UI仅压缩技能行并让4人列表采用两列，最终21项回放专门核对新的按钮裁切边界。

## 工具与交付指纹

Node24.19.0、npm11.17.0、TypeScript5.9.3、Vite7.1.12、PixiJS8.22.0、esbuild0.25.12、Emscripten6.0.11（-O2）、MinGW GCC8.1.0、CMake4.4.4。浏览器Chrome154.0.8037.98，Playwright1.56.1，headless，1440×1000、DPR1。

HTML1,134,675字节，SHA256为 5571dafbce38695f9dce4e3ffba5b68fb777a8d7c2585102dcc7112b6a9ac7ea。内嵌Wasm51,354字节；完整指纹及内嵌检查见 [artifact-integrity.json](validation/endless-v4/artifact-integrity.json)。重建时构建器嵌入当前核心、资产、CSS和生产依赖许可，无CDN或服务启动依赖。

TS严格检查、Vite生产构建和独立单文件构建均成功。较大的静态JS块包含内嵌官方JPG，这是自包含交付的数据，不据此删资产或改为外网依赖。

## 性能样本

当前常规样本515个显示帧。独立800 Sprite渲染夹具使用真实时钟、960×720、DPR1，预热后300帧；CPU工作中位0.70ms、P950.90ms，RAF间隔中位/P95约6.10ms。样本包含JS更新与绘制提交，不含GPU完成时间。

800 Sprite只压力测试渲染，不代表同时800发真实核心碰撞与AI。未测原生图形对比、完整内存峰值、弱设备和长时性能，不宣称浏览器与EasyX性能几乎相同。受控时钟的键盘回放不作为真实FPS测量。

## 证据与复跑

本轮提交证据在 [validation/endless-v4](validation/endless-v4/)：native-ctest、core/endless-parity、endless-inputs/native-result、standalone-results、browser-results、endless-browser-result、artifact-integrity、balance主要矩阵与summary。23MB原生二进制快照保留在build，仅为复跑临时产物，不提交。

独立CMake目标web_balance_probe再复跑96个留出课表/混招用例，与已保存最终留出矩阵的同场景战斗结果一致；balance-reproduction-current.json保存本次原始结果。

[运行指南](web-demo-guide.md)列出完整构建和回放命令；[平衡报告](web-balance-findings.md)列出参数前后变化、留出结果、未采用候选和下一轮建议。

这个版本支持验证操作、选招、攻防和逐波派学生的设计。自动化检查没有替代真人易懂程度、紧张感、疲劳与重开意愿；尚未训练机器学习学生或验证所有设备。此报告的游戏验证完成后，用户要求的 GitHub Pages 托管已另行配置，发布与更新说明见 [Pages 指南](github-pages.md)。
