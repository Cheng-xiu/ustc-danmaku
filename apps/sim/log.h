/* sim/log.h - headless 仿真日志: 文件 + stdout 双写, 只读世界, 不影响 RNG
 *
 * 设计约束 (见 docs/demo-tasks.md / S16 任务):
 *   - 日志代码禁止调用任何 core 的 RNG 或 step 接口, 禁止写世界状态;
 *   - 因此写日志失败(打开失败/中途失败)不得改变模拟结果, 只影响证据完整性;
 *   - 禁止在 core/ 内写日志, 本文件属于 sim 层。
 *
 * path == NULL 时只输出到 stdout; 打开失败返回 NULL 并向 stderr 打印明确错误。
 * lg == NULL 时 sim_log_line 仍输出到 stdout, 便于打开失败后继续记录。
 */
#ifndef SIM_LOG_H
#define SIM_LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SimLogger SimLogger;

/* 打开日志。path 为 NULL: 只输出 stdout, 返回非 NULL 句柄。
 * path 无法打开: 向 stderr 打印明确错误, 返回 NULL。 */
SimLogger *sim_log_open(const char *path);

/* 写一行(fmt 不含换行; 函数自动补 '\n')。同时写日志文件与 stdout。 */
#if defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void sim_log_line(SimLogger *lg, const char *fmt, ...);

/* 关闭并释放。允许 lg == NULL。 */
void sim_log_close(SimLogger *lg);

/* 会话头: build 标识 / 配置版本 / 学生策略版本 / seed / scenario。 */
void sim_log_header(SimLogger *lg, const char *build, const char *config_version,
                    const char *bot_version, uint64_t seed, const char *scenario);

#ifdef __cplusplus
}
#endif

#endif /* SIM_LOG_H */
