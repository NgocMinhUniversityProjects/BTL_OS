#include "common.h"
#include "syscall.h"
#include <stdio.h>

int __sys_xxxhandler(struct pcb_t *caller, struct sc_regs *regs) {
    /* TODO Implement syscall task*/
    printf("The first system call parameter is %d\n", regs->a1);
    return 0;
}

