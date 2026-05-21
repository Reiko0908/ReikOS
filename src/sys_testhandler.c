// File: src/sys_testhandler.c
#include "common.h"
#include "syscall.h"
#include <stdio.h>

/*
 * Lightweight wrapper: the actual test implementation lives in
 * `__sys_testhandler_impl` (in a separate file). We provide a weak
 * fallback here so builds succeed even when the test implementation
 * is not present.
 */

/* Implementation is provided in a separate test file. */
extern int __sys_testhandler_impl(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs);

int __sys_testhandler(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
    return __sys_testhandler_impl(krnl, pid, regs);
}