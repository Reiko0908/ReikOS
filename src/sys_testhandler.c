// File: src/sys_testhandler.c
#include "common.h"
#include "syscall.h"
#include <stdio.h>

#ifdef MM64
#include "mm64.h"
#endif

int __sys_testhandler(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
#ifdef MM64
        addr_t mapstart = 0;
        int incpgnum = 0;
        int expected = 0;

        switch (regs->a1) {
        case 0:
                mapstart = KERNEL_DYNAMIC_START;
                incpgnum = 1;
                expected = 1;
                break;
        case 1:
                mapstart = KERNEL_STATIC_START;
                incpgnum = 1;
                expected = 0;
                break;
        case 2:
                mapstart = KERNEL_DYNAMIC_END - (PAGING64_PAGESZ - 1);
                incpgnum = 2;
                expected = 0;
                break;
        default:
                printf("MM64 boundary test PID %u case %lu: unknown case\n",
                       pid, (unsigned long)regs->a1);
                return -1;
        }

        int ok = mm64_kernel_dynamic_range_fits(mapstart, incpgnum);
        printf("MM64 boundary test PID %u case %lu: start=" FORMAT_ADDR " pages=%d result=%s expected=%s\n",
               pid,
               (unsigned long)regs->a1,
               mapstart,
               incpgnum,
               ok ? "ALLOW" : "REJECT",
               expected ? "ALLOW" : "REJECT");
        return (ok == expected) ? 0 : -1;
#else
        printf("Hello from Kernel Space! The first parameter is: %lu\n", regs->a1);
        return 0;
#endif
}