#include "../include/common.h"
#include "../include/syscall.h"
#include "stdio.h"

int __sys_xxxhandler(struct krnl_t *krnl, uint32_t pid, struct sc_regs *reg)
{
    /* TODO: implement syscall job */
    printf("The first system call parameter %ld\n", reg->a1);
    return 0;
}