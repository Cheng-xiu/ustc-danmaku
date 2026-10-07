# 网页 Demo 运行与试玩

日期：2026-10-07。本轮交付 PixiJS 网页最小版，默认配置 v2、种子 20261006、3 名脚本学生；不包含训练模型或完整课程计分。

## 预构建试玩包

解压 `ustc-danmaku-web-demo.zip`，进入 `ustc-danmaku-web-demo` 文件夹，双击 `Start-Web-Demo.bat`。本机需要 Node.js 22.12 或更新版本；本轮机器已安装 Node 24.19.0。玩家无需下载 npm 依赖或安装 Emscripten/EasyX。

启动器提供本地 HTTP 服务并打开 `http://127.0.0.1:4173/`。保留启动窗口；结束试玩时在窗口按 Ctrl+C 或关闭窗口。该服务只监听本机，不是公共网站。

也可在包根目录运行：

```powershell
node scripts/serve-web.mjs web/dist 4173 --open
```

不能通过 `file://` 双击 HTML。端口被占用时关闭自己之前的试玩服务，或手动把 4173 改为 4174。

## 操作与目标

| 操作 | 效果 |
| --- | --- |
| 鼠标移动到战场内 | 金色 Boss 朝指针位置移动，接近时减速，12 px 死区停止 |
| WASD / 方向键 | 八方向备用移动；鼠标在战场内且超死区时优先鼠标，使用键盘可先移出战场 |
| 1 / 2 / 3 / 4 或对应按钮 | 环弹 / 课表 / 金矿 / 淋浴；按一次请求一次 |
| Esc / 战场内右键 / 暂停按钮 | 暂停或继续；暂停时血量、能量、弹幕、时间冻结 |
| R / 重开按钮 | 按相同默认种子重新开始 |

先点击开始。学生全部倒下即获胜，Boss 生命耗尽即失败；同 tick 双方全倒记 Boss 胜。无强制时间限制。青色为学生，粉色小弹为学生反击，金色描边为锁定目标。

四招共用一条能量，每秒恢复 10。接受时扣一次，拒绝不扣、不排队；当前招结束前不能叠加新招。预警锁定接受时的几何，目标之后移动不会改变本招路径。金矿可能因出生位置不满足安全距离而拒绝，可移动或换招。

窗口失焦、隐藏或长时间掉帧时显示暂停；返回后需要点击继续，暂停时间不会补算。首版面向桌面键鼠；移动端触控未实现。

## 从源码构建

依赖固定为 PixiJS 8.22.0、TypeScript 5.9.3、Vite 7.1.12、Emscripten 6.0.11；提交的 npm 锁文件固定依赖树。首次源码构建需联网安装工具链及依赖：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/setup-web-toolchain.ps1
powershell -ExecutionPolicy Bypass -File scripts/build-wasm.ps1
Set-Location web
npm.cmd ci
npm.cmd run build
Set-Location ..
.\Start-Web-Demo.bat
```

已安装 SDK 时省略第一步；自定义目录见 [工具链说明](web-toolchain.md)。修改 C 后重新执行 Wasm 构建，再构建前端；修改 TS/CSS 后只需重建前端。开发服务在 `web` 目录运行 `npm.cmd run dev`。

## 原生与浏览器验证

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-native.ps1
node tests/web/compare-core.mjs
node tests/web/replay-winning.mjs
node tests/web/browser-smoke.mjs
node tests/web/winning-browser.mjs
```

浏览器检查先启动上述本地服务，且需要已安装 Chrome。回放检查读取 `build/web-validation/winning-inputs.json`；Git 中保存的固定回放在 `docs/validation/winning-inputs.json`，先复制到对应 build 目录，或先按 [玩法探针](web-playability-findings.md) 生成。

## 静态部署

发布 `web/dist` 内容到支持静态资源的主机。Vite 使用相对资源路径，可放网站子目录；目录 URL 要有尾部斜杠。服务器提供 `.wasm` 为 `application/wasm`，`.mjs` 为 JavaScript MIME。无需 Node 后端、账号或跨源隔离；本地 Node 只用来提供静态 HTTP。

本轮已产出静态包，并验证其子目录加载；没有发布到公共域名。若后续要上线，以相同产物另行配置托管。

完成生产构建后可运行 `powershell -ExecutionPolicy Bypass -File scripts/package-web.ps1`，默认输出 `build/release`。脚本保留生产依赖许可、生成文件 SHA256 manifest 及 ZIP；已有输出时要求换目录以保留历史包，使用 `-OutputRoot` 指定新位置。

## 先记录体验再调参

[玩法发现](web-playability-findings.md) 已列出能量取舍偏弱、近身反击空档及矿点拒绝等风险。按 [真人试玩模板](web-demo-playtest.md) 记录看不懂、失控、重复用一招或卡住的具体场景；本轮保留既有试验数值，不将脚本胜局当成平衡结论。
