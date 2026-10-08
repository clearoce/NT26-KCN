/*
 * at_types.h - 基础类型与错误码
 */
#ifndef AT_TYPES_H
#define AT_TYPES_H 1

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AT 层的错误码。
 *
 * ⚠️ **数值是冻结的契约，不要改。**
 *
 * 理由：at_obj_exec_cmd() 的返回值会被调用方拿去与这些宏内的数字比较
 * （`module/nt26e.c` 判 `ret == 0 || ret == AT_ERR_PERM` 认定 LIPCFG 已生效；
 * `src/iot.c` 判 `== 0`）。改数值等于**静默改变**这些判断的含义。
 *
 * 历史上这些数值还要与 durian 预编译库内的比较保持一致（nt26e.o 曾以字面量
 * `-EPERM` 比较）—— 该成员已于 C3 退出镜像，但契约不随调用方一起作废。
 *
 * 取值来自本项目历史上所用的 alumy/errno.h（armclang 分支，ERRNO_BASE = 0），
 * 已用预处理器实测确认：EPERM=(0+1)、EBUSY=(0+16)、ENOBUFS=(0+105)、ETIMEDOUT=(0+110)；
 * 而 EINVAL / ENOMEM 由工具链的 <errno.h> 提供（5 / 6）。
 */
#define AT_OK              0
#define AT_ERR_PERM      (-1)     /* = -EPERM      */
#define AT_ERR_INVAL     (-5)     /* = -EINVAL     */
#define AT_ERR_NOMEM     (-6)     /* = -ENOMEM     */
#define AT_ERR_BUSY     (-16)     /* = -EBUSY      */
#define AT_ERR_NOBUF   (-105)     /* = -ENOBUFS    */
#define AT_ERR_TIMEOUT (-110)     /* = -ETIMEDOUT  */

#ifdef __cplusplus
}
#endif

#endif /* AT_TYPES_H */
