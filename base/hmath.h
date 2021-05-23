#ifndef HV_MATH_H_
#define HV_MATH_H_

/*
 * @功能：此头文件补充了一些数学工具函数
 *
 */

#include <math.h>

// 向下取2的指数倍, floor2e(3) = 2
static inline unsigned long floor2e(unsigned long num) {
    unsigned long n = num;
    int e = 0;
    while (n>>=1) ++e;
    unsigned long ret = 1;
    while (e--) ret<<=1;
    return ret;
}

// 向上取2的指数倍, floor2e(3) = 4
static inline unsigned long ceil2e(unsigned long num) {
    // 2**0 = 1
    if (num == 0 || num == 1)   return 1;
    unsigned long n = num - 1;
    int e = 1;
    while (n>>=1) ++e;
    unsigned long ret = 1;
    while (e--) ret<<=1;
    return ret;
}

#endif // HV_MATH_H_
