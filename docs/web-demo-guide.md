# 网页无尽 Demo 运行指南

当前配置 v4、Web ABI v3。预构建的 demo/ustc-danmaku.html 双击即可离线打开，无需服务器；可复制给其他电脑试玩。所有资源和许可证都在同一文件中。

## 操作与规则

鼠标指针在战场内时角色朝指针移动，接近时减速；WASD/方向键备用。数字 1–4 或 HUD 按钮选择招式；Esc/右键暂停，R 重开。暂停、失焦、页面隐藏及死亡后逻辑停止，恢复不补进度。

环招消耗 30，课表 40，金矿 15，淋浴 100。能量初始 60、上限 100、每逻辑秒恢复 10。接受才扣费，拒绝不扣、不排队。

初始 3 名学生；清波后清掉旧弹并显示 2 秒确定出生点，下一波人数 +1，到 8 名后持续 8 名。血量、能量、位置及累计击倒保留。出生预告期间可移动。Boss 血量耗尽即结算，即使同 tick 击倒最后学生也结束；清波不会出现胜利界面。

GPA = 4.30 × n /（n + 20），n 仅为累计真实击倒数。20 人为 2.15，100 人显示 3.58；向下保留两位，有限 n 不显示 4.30。击倒人数仍逐次增加，不能把显示值暂时不变当作漏记击倒。

## 源码构建

需要 Node 22.12+、npm 和固定 Emscripten 6.0.11。首次在根目录执行：

    powershell -ExecutionPolicy Bypass -File scripts/setup-web-toolchain.ps1
    powershell -ExecutionPolicy Bypass -File scripts/build-wasm.ps1
    Set-Location web
    npm.cmd ci
    npm.cmd run build
    npm.cmd run build:standalone
    Set-Location ..

单文件默认输出 build/release/ustc-danmaku-endless.html。指定交付路径可执行：

    node scripts/build-standalone.mjs demo/ustc-danmaku.html

修改 C 后先构建 Wasm。构建器把已生成的 Wasm、Emscripten 工厂、PixiJS、样式及官方 JPG 内嵌，生成器本身不编译 C。原始 web/index.html 是开发入口，需 Vite/HTTP 加载，不能当作预构建单文件。

可选开发服务：在 web 目录执行 npm.cmd run dev；生产 HTTP 验证可执行 Start-Web-Demo.bat。网站部署可直接使用单文件，或提供 web/dist 内容并正确设置 Wasm MIME。

## 原生与网页验证

从仓库根目录执行：

    powershell -ExecutionPolicy Bypass -File scripts/build-native.ps1
    .\build\native\web_bridge_probe.exe
    node tests/web/compare-core.mjs
    .\build\native\endless_native_replay.exe build/web-validation/endless-inputs.json build/web-validation/endless-native-result.json build/web-validation/endless-native-snapshots.bin
    node tests/web/compare-endless.mjs
    node tests/web/standalone-smoke.mjs demo/ustc-danmaku.html
    node tests/web/endless-browser-replay.mjs demo/ustc-danmaku.html

真实键鼠浏览器检查需本机 Chrome。browser-smoke.mjs 默认访问 http://127.0.0.1:4173/，先启动生产预览：在 web 目录 npm.cmd run preview -- --port 4173；测完关闭服务。浏览器每个测试都关闭自建实例。平衡探针的复跑命令与参数见 [平衡报告](web-balance-findings.md)。

历史 winning-inputs / winning-browser / playability_probe 是配置 v2 有限局资料，不能用于验收当前无尽规则。当前证据保存在 validation/endless-v4；此前记录保留在 archive 与 validation 原路径。

## 排错

加载失败时使用完整预构建 HTML 和支持 WebAssembly/WebGL 的浏览器。开发入口需要已生成 mjs/wasm。配置与 ABI 错配会明确报错，须一起重新构建，而不是扩大解码容错。

玩法建议与尚需真人核验的问题见 [平衡报告](web-balance-findings.md) 和 [试玩模板](web-demo-playtest.md)。本轮没有训练机器学习学生。
