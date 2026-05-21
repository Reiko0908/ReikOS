#include "common.h"
#include "syscall.h"
#include <stdio.h>
#include "mem.h"

#ifdef MM64
#include "mm64.h"
#endif

#ifdef MM64
static const char *mm64_region_name(addr_t addr)
{
        if (IS_USER_SPACE(addr))
                return "USER";
        if (IS_CANONICAL_HOLE(addr))
                return "CANONICAL_HOLE";
        if (IS_KERNEL_DYNAMIC_SPACE(addr))
                return "KERNEL_DYNAMIC";
        if (IS_KERNEL_STATIC_SPACE(addr))
                return "KERNEL_STATIC";
        return "UNKNOWN";
}
#endif

int __sys_testhandler_impl(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
        /* Simple assertion helpers:
         * syscall 440 100 reg_index expected_addr
         *   - checks caller->regs[reg_index] == expected_addr
         * syscall 440 101 reg_index offset expected_byte
         *   - reads user memory at caller->regs[reg_index] + offset and compares to expected_byte
         */
        if (regs->a1 == 100 || regs->a1 == 101) {
                struct pcb_t *proc = (krnl) ? krnl->current : NULL;
                if (!proc) {
                        printf("TEST_ASSERT: no current proc (pid=%u)\n", pid);
                        return -1;
                }
                if (regs->a1 == 100) {
                        int reg_index = (int)regs->a2;
                        addr_t expect = (addr_t)regs->a3;
                        addr_t val = 0;
                        if (reg_index < 0 || reg_index >= 10) {
                                printf("TEST_ASSERT REG: invalid reg %d\n", reg_index);
                                return -1;
                        }
                        val = proc->regs[reg_index];
                        int ok = (val == expect) ? 1 : 0;
                        printf("TEST_ASSERT REG r%d: val=0x%016lx expected=0x%016lx => %s\n",
                                   reg_index,
                                   (unsigned long)val,
                                   (unsigned long)expect,
                                   ok ? "PASS" : "FAIL");
                        return ok ? 0 : -1;
                } else {
                        int reg_index = (int)regs->a2;
                        addr_t offset = (addr_t)regs->a3;
                        int expect = (int)regs->a4;
                        BYTE v;
                        addr_t addr = proc->regs[reg_index] + offset;
                        int r = read_mem(addr, proc, &v);
                        if (r != 0) {
                                printf("TEST_ASSERT MEM: read failed addr=0x%016lx\n", (unsigned long)addr);
                                return -1;
                        }
                        printf("TEST_ASSERT MEM [%d+%lu]=0x%02x expected=0x%02x => %s\n",
                                   reg_index, (unsigned long)offset, (unsigned int)v, (unsigned int)expect,
                                   (v == (BYTE)expect) ? "PASS" : "FAIL");
                        return (v == (BYTE)expect) ? 0 : -1;
                }
        }
#ifdef MM64
        addr_t addr = 0;
        addr_t mapstart = 0;
        int incpgnum = 0;
        int expected = 0;

        switch (regs->a1) {
        case 0:
                addr = USER_SPACE_START;
                expected = IS_USER_SPACE(addr);
                break;
        case 1:
                addr = USER_SPACE_END;
                expected = IS_USER_SPACE(addr);
                break;
        case 2:
                addr = CANONICAL_HOLE_START;
                expected = IS_CANONICAL_HOLE(addr);
                break;
        case 3:
                addr = CANONICAL_HOLE_END;
                expected = IS_CANONICAL_HOLE(addr);
                break;
        case 4:
                addr = KERNEL_DYNAMIC_START;
                incpgnum = 1;
                expected = 1;
                break;
        case 5:
                addr = KERNEL_DYNAMIC_END;
                incpgnum = 1;
                expected = 0;
                break;
        case 6:
                addr = KERNEL_STATIC_START;
                incpgnum = 1;
                expected = 0;
                break;
        case 7:
                addr = KERNEL_STATIC_END;
                incpgnum = 2;
                expected = 0;
                break;
        default:
                printf("MM64 boundary test PID %u case %lu: unknown case\n",
                       pid, (unsigned long)regs->a1);
                return -1;
        }

        if (regs->a1 <= 3) {
                printf("MM64 region edge test PID %u case %lu: addr=0x%016lx region=%s expected=%s\n",
                       pid,
                       (unsigned long)regs->a1,
                       (unsigned long)addr,
                       mm64_region_name(addr),
                       expected ? "INSIDE" : "OUTSIDE");
                return expected ? 0 : -1;
        }

        if (regs->a1 == 4)
                mapstart = KERNEL_DYNAMIC_START;
        else if (regs->a1 == 5)
                mapstart = KERNEL_DYNAMIC_END;
        else if (regs->a1 == 6)
                mapstart = KERNEL_STATIC_START;
        else
                mapstart = KERNEL_STATIC_END;

        int ok = mm64_kernel_dynamic_range_fits(mapstart, incpgnum);
        printf("MM64 boundary test PID %u case %lu: start=0x%016lx pages=%d result=%s expected=%s\n",
               pid,
               (unsigned long)regs->a1,
               (unsigned long)mapstart,
               incpgnum,
               ok ? "ALLOW" : "REJECT",
               expected ? "ALLOW" : "REJECT");
        return (ok == expected) ? 0 : -1;
#else
        /* Fallback: mirror previous simple behaviour */
        printf("Hello from Kernel Space! The first parameter is: %lu\n", regs->a1);
        return 0;
#endif
}
