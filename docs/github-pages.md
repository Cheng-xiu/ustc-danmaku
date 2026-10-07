# GitHub Pages 试玩站

公开试玩地址：[科大弹幕录](https://cheng-xiu.github.io/ustc-danmaku/)。

站点采用仓库的 `gh-pages` 分支根目录发布，关闭 Jekyll 处理。`index.html` 是已验收的 `demo/ustc-danmaku.html` 原始字节；不在发布时重新构建游戏，JavaScript、Wasm、CSS 与校徽均已内嵌。项目路径 `/ustc-danmaku/` 不需要额外资源路径配置。

`gh-pages` 只包含游戏入口、`.nojekyll`、保护 HTML 字节的 `.gitattributes` 和公开的 `deployment.json`。后者记录游戏来源提交、HTML 大小和 SHA256，不包含凭据。源代码仍在开发分支，原有草稿 PR 独立保留。

## 更新游戏

先按 [构建指南](web-demo-guide.md) 重建 Wasm 和单文件 HTML，完成必要试玩检查，把最终产物更新至 `demo/ustc-danmaku.html` 并提交。然后在仓库根目录运行：

```powershell
node scripts/publish-pages.mjs
```

脚本需要 Node 和可用的 Git 推送权限。它直接创建发布 Git 对象并正常推送 `gh-pages`，不切换当前工作树；拒绝发布未提交的 HTML、替换非本项目的发布分支或覆盖额外文件。重复发布相同来源与相同文件时保持原发布提交；并发更新造成冲突时 Git 会拒绝非快进推送。

网站由 GitHub Pages 在发布分支变化后更新。GitHub 仓库 Settings → Pages 应保持 `Deploy from a branch`、分支 `gh-pages`、目录 `/ (root)`，启用 HTTPS；不需要自定义域名。

若本机 Git 不能直接联网，可使用本机已有代理，或先配置 Git 的网络连接；代理地址属于本机环境，不写入游戏仓库。脚本不会保存或打印 GitHub 令牌。

## 核对上线

- 等待 Pages 构建与部署成功，打开上述地址；首次发布可能需要几分钟。
- 确认菜单可开始、显示圆形校徽和三个学生，1–4 出招、暂停与重开正常，控制台没有运行异常或失败的游戏资源请求。
- 在线 HTML 的 SHA256 应与已提交的 `demo/ustc-danmaku.html` 和 `deployment.json` 一致。试玩页不传输玩家数据，也没有服务器端存档。

后续机器学习训练继续使用原生 C 核心；网站只是现有游戏的浏览器入口。本次托管不改变玩法或设备支持范围。

## 首次上线记录

2026-10-07 的 [Pages 构建与部署](https://github.com/Cheng-xiu/ustc-danmaku/actions/runs/37630396240) 已成功，HTTPS 强制开启。发布提交为 `cf9743f436cfd8213fb78186e66a7dcc163fde55`，游戏来源提交为 `98a221b9fa2b89c7434ca527dff2dd1d1d756626`。

在线 HTML 返回 200、大小 1,134,675 字节，SHA256 为 `5571dafbce38695f9dce4e3ffba5b68fb777a8d7c2585102dcc7112b6a9ac7ea`，与先前离线验收产物逐字一致。实际 Chrome 154 在线检查 12/12 通过：加载当前核心、菜单、开始推进、环招真实发弹、暂停、重开、第四招初始能量拒绝与自然满能量接受及发弹。运行异常、失败请求和 HTTP 错误均为 0，游戏画面截图另经目视核对。受控时钟仅用于验证交互，不作为网络性能或 FPS 测量。

发布脚本同一来源重复运行返回 `unchanged: true`，发布提交保持不变。原始证据见 [validation/github-pages](validation/github-pages/)；这里只部署现有 v4 游戏，没有新增玩法。后续说明与证据提交不需要重新发布相同的游戏文件。

[GitHub 官方发布源说明](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site)。

## v5 更新上线记录

2026-10-07 的[v5构建与部署](https://github.com/Cheng-xiu/ustc-danmaku/actions/runs/37637302206)成功。游戏来源提交为`ce07fc266335f6afae1e23d30b960e8e3465b061`，发布提交为`b9260dfa4d2d0c92003f832b134d0f24f913ffd1`。源分支后续文档提交不改变已发布游戏身份。

当前配置5、ABI4，顶部能量/共享CD、开局技能介绍、场外指针和四招差异已上线。在线HTML返回200，1,145,405字节，SHA256为`7c71994fa81c9007b9e13a1f736bf2db69286f04d5deb8799feba1a50d98542c`，与1191项最终离线检查所测文件、仓库HTML和deployment.json一致。

实际Chrome154在1440×900、1024×768、390×844在线检查34/34通过：各自核对响应字节、核心与费用、菜单说明、开始与顶栏/战场比例、鼠标移入HUD继续朝方向移动、键盘覆盖、环弹扣能/CD/实际发弹、暂停冻结及重开。运行异常、资源失败和HTTP错误均0。完整四招、清波与120tick出生预告的离线结果见[当前工程报告](web-demo-validation.md)，网站发布同一份已测字节。

线上复跑命令为`node tests/web/pages-smoke.mjs`。需要Chrome及正常网络；本机必须代理时可临时传PAGES_QA_PROXY，不把本机代理或令牌保存到仓库。证据保存在[validation/github-pages-v5](validation/github-pages-v5/)，首次v4上线证据仍独立保留。
