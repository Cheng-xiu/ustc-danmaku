/* sim/log.c - headless 仿真日志实现
 *
 * 本文件不包含任何 core 头文件, 不触碰世界状态, 不调用 RNG:
 * 日志失败的唯一后果是证据缺失, 不会改变 tick 序列或随机序列。
 */
#include "log.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

struct SimLogger {
    FILE *file; /* NULL 表示只输出 stdout */
};

static void log_vwrite(FILE *f, const char *fmt, va_list ap) {
    if (f == NULL) {
        return;
    }
    (void)vfprintf(f, fmt, ap);
    (void)fputc('\n', f);
}

SimLogger *sim_log_open(const char *path) {
    SimLogger *lg = (SimLogger *)calloc(1u, sizeof(SimLogger));
    if (lg == NULL) {
        (void)fprintf(stderr, "sim_log_open: 内存不足, 无法创建日志句柄\n");
        return NULL;
    }
    lg->file = NULL;
    if (path != NULL) {
        lg->file = fopen(path, "w");
        if (lg->file == NULL) {
            (void)fprintf(stderr, "sim_log_open: 无法打开日志文件 '%s'\n", path);
            free(lg);
            return NULL;
        }
    }
    return lg;
}

void sim_log_line(SimLogger *lg, const char *fmt, ...) {
    va_list ap;
    if (fmt == NULL) {
        return;
    }
    /* stdout 始终写: 打开失败时调用方传 NULL 仍能保留 stdout 证据。 */
    va_start(ap, fmt);
    log_vwrite(stdout, fmt, ap);
    va_end(ap);

    if (lg == NULL || lg->file == NULL) {
        (void)fflush(stdout);
        return;
    }
    va_start(ap, fmt);
    log_vwrite(lg->file, fmt, ap);
    va_end(ap);
    (void)fflush(lg->file);
}

void sim_log_header(SimLogger *lg, const char *build, const char *config_version,
                    const char *bot_version, uint64_t seed, const char *scenario) {
    sim_log_line(lg, "HEADER build=%s", (build != NULL) ? build : "(null)");
    sim_log_line(lg, "HEADER config_version=%s",
                 (config_version != NULL) ? config_version : "(null)");
    sim_log_line(lg, "HEADER bot_policy_version=%s", (bot_version != NULL) ? bot_version : "(null)");
    /* printf 长度修饰符与 MSVCRT 的差异由 PRIu64 统一 (MinGW 下展开为 I64u) */
    sim_log_line(lg, "HEADER seed=%" PRIu64, seed);
    sim_log_line(lg, "HEADER scenario=%s", (scenario != NULL) ? scenario : "(null)");
}

void sim_log_close(SimLogger *lg) {
    if (lg == NULL) {
        return;
    }
    if (lg->file != NULL) {
        (void)fflush(lg->file);
        (void)fclose(lg->file);
        lg->file = NULL;
    }
    free(lg);
}
