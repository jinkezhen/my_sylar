/**
 * @brief 常用宏的封装
 */

#ifndef __SYLAR_MACRO_H__
#define __SYLAR_MACRO_H__

#include "log.h"
#include "util.h"
#include <string>
#include <assert.h>

using namespace sylar;

// _builtin_except：是GCC和LLVM(CLang)提供的一个内建函数，用于优化分支预测，减少CPU误判带来的性能损失
// 对于CPU来说，他会同等概率的执行所有分支，在某些情况下，我们想告诉CPU哪个分支为真的概率比较低或高，就可以用这个函数
// long __builtin_expect(long EXP, long EXPECTED);
// EXP：表达式，通常就是if语句的条件
// EXPECTED：预期的值，用于告诉编译器该表达式可能的结果
//返回值就是EXP的结果
// if (__builtin_expect(x == 10, 0))：会告诉编译器，x==10很少发生，CPU预测时会更倾向于认为x!=10,从而优化指令流水线，提高执行效率



#if defined __GNUC__ || defined __llvm__
/// LIKCLY 宏的封装, 告诉编译器优化,条件大概率成立
#   define SYLAR_LIKELY(x)       __builtin_expect(!!(x), 1)
/// LIKCLY 宏的封装, 告诉编译器优化,条件大概率不成立
#   define SYLAR_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#else
#   define SYLAR_LIKELY(x)      (x)
#   define SYLAR_UNLIKELY(x)      (x)
#endif

/// 断言宏封装
#define SYLAR_ASSERT(x) \
    if(SYLAR_UNLIKELY(!(x))) { \
        SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ASSERTION: " #x \
            << "\nbacktrace:\n" \
            << BacktraceToString(100, 2, "    "); \
        assert(x); \
    }

/// 断言宏封装
#define SYLAR_ASSERT2(x, w) \
    if(SYLAR_UNLIKELY(!(x))) { \
        SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ASSERTION: " #x \
            << "\n" << w \
            << "\nbacktrace:\n" \
            << BacktraceToString(100, 2, "    "); \
        assert(x); \
    }

#endif