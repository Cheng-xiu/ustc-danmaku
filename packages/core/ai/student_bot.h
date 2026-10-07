/* student_bot.h - 脚本 AI 学生: 可复现往返脚本 + 有限躲避脚本
 * 接口版本: 1
 * 声明集中在 demo_base.h; 本文件提供策略切换与诊断辅助。
 */
#ifndef DEMO_STUDENT_BOT_H
#define DEMO_STUDENT_BOT_H

#include "demo_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 在 demo_base.h 中已声明:
 *   void student_bot_choose(const StudentObservation *obs, StudentBotState *bot,
 *                           StudentAction *out);
 *   void student_bot_init(StudentBotState *bot, uint32_t serial_seed);
 */

/* 策略版本字符串, 写入日志与 HUD, 冻结后不得随参数静默改变 */
const char *student_bot_policy_version(int32_t policy);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_STUDENT_BOT_H */
