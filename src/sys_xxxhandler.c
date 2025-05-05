
#include "common.h"
#include "syscall.h"
#include "mm.h"
// write word
int __sys_xxxhandler(struct pcb_t *caller, struct sc_regs *regs) {
    /* TODO Implement syscall task*/

    for(int i=0;i<4;i++){
        BYTE data= (regs->a3&(0xff<<(i*8)))>>(8*i);
        __write(caller, 0 ,regs->a1,regs->a2+i,data);
    }
#ifdef IODUMP
    printf("======= PHYSICAL MEMORY AFTER WRITING ========\n");
    printf("write region=%d offset=%d - %d value=%d\n", regs->a1, regs->a2,regs->a2+3, regs->a3);
   
    #ifdef PAGETBL_DUMP
    print_pgtbl(caller, 0, -1); 
#endif
    MEMPHY_dump(caller->mram);
    for (int i = 0; i < PAGING_MAX_PGN; ++i) {
        if (PAGING_PAGE_PRESENT(caller->mm->pgd[i])){
            printf("Page Number: %d -> Frame Number : %d\n", i, PAGING_FPN(caller->mm->pgd[i]));
        }
    }
    printf("==============================================\n");
#endif
    return 0;
}

// read word
int __sys_xxxhandler1(struct pcb_t *caller, struct sc_regs *regs) {
    /* TODO Implement syscall task*/
    regs->a3=0;
    for(int i=0;i<4;i++){
        BYTE data;
        __read(caller,0,regs->a1,regs->a2+i,&data);
        regs->a3|=(((0xff)&((uint32_t)data))<<(i*8));
    }
#ifdef IODUMP
    printf("======= PHYSICAL MEMORY AFTER READING ========\n");
    printf("read region=%d offset=%d - %d value=%d\n", regs->a1, regs->a2,regs->a2+3, regs->a3);

#ifdef PAGETBL_DUMP
    print_pgtbl(caller, 0, -1); //print max TBL
#endif
    MEMPHY_dump(caller->mram);
    for (int i = 0; i < PAGING_MAX_PGN; ++i) {
    if (PAGING_PAGE_PRESENT(caller->mm->pgd[i])){
        printf("Page Number: %d -> Frame Number : %d\n", i, PAGING_FPN(caller->mm->pgd[i]));
    }
    }
    printf("==============================================\n");

#endif
    return 0;
}
