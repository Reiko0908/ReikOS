// File: src/sys_testhandler.c
#include "common.h"
#include "syscall.h"
#include <stdio.h>

int __sys_testhandler(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
        /* TODO: implement syscall job */
        printf("Hello from Kernel Space! The first parameter is: %lu\n", regs->a1); // It might be regs->al in your specific common.h
        return 0;
}