# Web ABI v1（实现冻结）

基线：`753a9f6`，沿用配置 v1；迁移修复实际漏发的波次时钟，不调整试验数值。母代理负责 Git、公共类型和最终集成。当前执行器为 Codex；先前 DeepSeek 路由提示词供外部代理使用，不能声称本轮执行使用了该模型。

导出：`demo_reset(uint32 lo,uint32 hi,uint32 students)` 返回 1/0；`demo_step(float mx,float my,int pointer_valid,float px,float py,uint32 attack_mask)` 返回 1/0；`demo_snapshot()` 返回缓冲指针；`demo_snapshot_size()` 返回字节数；`demo_dispose()`。单实例，不分配跨调用对象。重置默认 3 名学生（1..8），seed 高低两段。核心每次 step 只前进一个 tick，终局/截断停止。

快照以 uint32 words 连续写入，float 用 memcpy 位转换，little endian。不复制结构体、不依赖对齐。快照下次 snapshot/reset/dispose 失效；TS 立即复制解码，内存 grow 后刷新 DataView。上限 actors 9、bullets 800、warnings 8192（32 波×256）、events 256；超容量明确失败，不截断。

头 64 words：0 magic `0x55444331`；1 ABI=1；2 总字节数；3 tick；4 status（0 running/1 win/2 lose/3 draw）；5 config version；6 student count；7 bullet count；8 warning count；9 event count；10 energy；11 max；12 attackState（0 idle/1 windup/2 active）；13 pattern；14 locked target；15 startTick；16 windup；17 activeTicks；18/19 plan low/high；20/21 seed low/high；22 accepted；23 rejected；24 boss hits；25 student hits；26 boss bullets spawned；27 student bullets spawned；28 overflow；29/30 float fieldW/H；31 markedTarget；32..35 costs；36..39 available；40..43 actors/bullets/warnings/events **字节偏移**；44 events dropped；其余保留为0。

Actors（Boss 先，随后稳定学生槽位）每条10 words：id、alive、float x、float y、float radius、signed hp、signed hpMax、signed invuln、faction、reserved。

Bullets 每条10 words：idLow、idHigh、faction、pattern、float x/y/vx/vy/radius、sourceId。仅活跃弹，64位身份用字符串保留。

Warnings 每条8 words：float x/y/vx/vy/radius、signed absoluteSpawnTick、waveIndex、pattern。C 在计划接受后使用同一不可变计划和真实 emit 函数只读投影每波出生位置/方向/时刻，不消耗 RNG；仅公开已预警的几何，不输出 geometry_seed 或随机流。前端画预警射线/出生轮廓；不得重写四招公式。过往已生成波可保留在快照，由前端按时刻过滤。

Events 每条9 words：type、signed tick、sourceId、targetId、pattern、reject、signed amount、float x/y。每 tick step 后 snapshot 收集，追赶帧汇总所有 tick 事件。

原生与 Wasm 输入归一化/坐标使用相同核心；唯一固定60Hz循环由 TS 调度，页面隐藏/失焦显式暂停，积压>250ms或一次需要>8 tick暂停，恢复清积压。零 tick 留边沿，多 tick 第一次消费边沿；按钮和键盘共同进入位掩码（同 tick 多招由 C 取最低ID）。
