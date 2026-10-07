# Web ABI v3

当前配置 v4。v3 将头部 word 55 从旧逐杀加分改为 GPA 半饱和击倒数；同一内存布局也必须用新版本号区分，客户端拒绝旧 ABI。原生和 Wasm 共用桥接源码，不 memcpy C 结构体。

## 导出与所有权

demo_reset(uint32 seedLo, uint32 seedHi, uint32 students) 返回 1/0；students 为初始 1..8，默认界面为 3。demo_step(float mx, float my, int pointerValid, float px, float py, uint32 attackMask) 每次只前进一 tick。demo_snapshot 返回字节缓冲指针，demo_snapshot_size 返回长度，demo_dispose 释放单实例状态。终局或实验截断停止。

缓冲在下一次 snapshot/reset/dispose 时失效。TypeScript 每次重新获取 HEAPU8 并复制，再严格检查 magic、版本、容量、连续偏移和字段。64 位 ID/seed 以十进制字符串保存。内存增长后不复用旧视图。

## 头部

共 64 个 32 位小端 word；带符号整型按 int32，float 用 memcpy 转换位。偏移量单位为字节。

| Word | 值或意义 |
| --- | --- |
| 0 / 1 / 2 | magic 0x55444331 / ABI 3 / 总字节数 |
| 3 / 4 / 5 | tick / status / config version |
| 6–9 | 学生数 / 活弹数 / 公开预警数 / 本 tick 事件数 |
| 10 / 11 | 能量 / 上限 |
| 12–17 | 攻击状态 / pattern / 锁定目标 / 接受tick / windup / active |
| 18–21 | plan ID低高位 / seed低高位 |
| 22–28 | 接受 / 拒绝 / Boss受击 / 学生受击 / Boss累计弹数 / 学生累计弹数 / 溢出 |
| 29 / 30 / 31 | float 场宽 / 场高 / 当前标记目标 |
| 32–35 / 36–39 | 四招消耗 / 四招可用boolean |
| 40–43 | 角色 / 活弹 / 公开预警 / 事件的字节偏移 |
| 44 | 事件丢弃数 |
| 45 / 46 / 47 | 波次 / 已清波数 / 波阶段（0 active、1 preview） |
| 48 / 49 | 下波人数 / 预定出生tick |
| 50 / 51 / 52 | GPA 百分位整数 / 累计真实击倒 / 累计派出学生 |
| 53 / 54 | 出生点预告数 / 预告数组字节偏移 |
| 55 / 56 | GPA 半饱和击倒数（20）/ 极限百分位值（430） |
| 57–63 | 保留，必须0 |

status 枚举 0 running / 1 win / 2 lose / 3 draw 保留用于历史有限夹具。产品无尽默认只运行或死亡失败。windup/active 为 1/2，idle 为0；pattern 为0..3。

## 连续记录

头后依次为 actors、bullets、warnings、events、spawn preview。容量：9角色、800活弹、8192预警、256事件、8预告点。超容量明确失败，不静默截断。

Actors 每条10 words，Boss先：id、alive、float x/y/radius、int hp/hpMax/invuln、faction、reserved。学生稳定 ID 跨波唯一。

Bullets 每条10 words：idLow/idHigh、faction、pattern、float x/y/vx/vy/radius、sourceId；只有活跃弹。

Warnings 每条8 words：float x/y/vx/vy/radius、int absoluteSpawnTick、waveIndex、pattern。C 对同一接受计划调用真实 emit，缓存公开几何；不消耗 RNG，不输出隐藏随机流。课表的 lane_spread_px 接受时锁定，预警与实际弹使用同一值。

Events 每条9 words：type、int tick、sourceId、targetId、pattern、reject、int amount、float x/y。事件类型13清波、14下一波预告、15新波开始；其余事件沿用定义。追赶显示帧收集每 tick 事件。

Spawn preview 每条2 words：float x/y。2秒预告点在清波时冻结，学生实际在对应 tick、对应位置出生，不因 Boss 后来走近而迁移。

## 时钟与计分

TypeScript 负责60 Hz唯一固定循环，显示帧可0/1/多tick；超过250ms或需超过8tick时显式暂停。零tick保留边沿，多tick仅首次消费；恢复清积压。前端只读展示，不计算游戏分数。

GPA由C累计真实击倒n后重算 floor(430*n/(n+20))，64位中间值避免溢出。word50即已量化显示值，时间/伤害/波数不参与。有限n低于430。
