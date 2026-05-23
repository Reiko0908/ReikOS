#include "common.h"
#include "syscall.h"
#include <stdio.h>

int __sys_testhandler(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs) {
  printf("Hello from Kernel Space! The first parameter is: %lu\n",
         regs->a1);
  return 0;
}
