# 科大弹幕录：无尽网页 Demo v5

解压后双击 ustc-danmaku.html，使用 Chrome / Edge 离线试玩；无需 Node、编译器或服务器。可选 Start-Web-Demo.bat 以本地 HTTP 运行 web/dist，此选项需要 Node 22.12+。

开始前查看四招说明；局内顶部显示能量与共享CD，其余为战场。鼠标朝指针移动，在战场外仍跟随；WASD/方向键备用，1–4出招，Esc/右键暂停，R重开。失焦/隐藏自动暂停，需要手动继续。
四招分别为25能量快速双环、50能量分列弹墙、20能量单目标窄扇三连、100能量持续全场雨。学生初始3人，每清波增加1人，最高8人后继续刷新。只有死亡结束。
GPA = 4.30 × 累计击倒 /（累计击倒 + 20），增长渐缓、趋近4.30。

当前配置v5、ABI v4、默认种子20261006、15个共享C/AI源码、脚本学生未训练。官方科大校徽以圆形遮罩显示。新局按窗口比例铺满，逻辑面积保持960×720；本局改变窗口比例时等比缩放、允许留边，重开后再按新比例铺满。

- [运行指南](docs/web-demo-guide.md)
- [本版平衡实测与玩法建议](docs/web-balance-findings-v5.md)
- [此前v4平衡记录](docs/web-balance-findings.md)
- [工程验证](docs/web-demo-validation.md)
- [试玩记录模板](docs/web-demo-playtest.md)
- [源码](https://github.com/Cheng-xiu/ustc-danmaku)

自动化回放不是完整真人平衡验收；已知剩余问题和后续建议见报告。
