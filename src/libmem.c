/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

//change list 31/3/2025 - Minh: 
//  + implemented(?) get_free_vmrg_area, 
//  + added comments in __alloc
//

//change list 2/4/2025 - Minh:
// + implemented 80% of __alloc
// + implemented (?) __free
// + fixxed a bug in best_fit
// + moved pthread lock to the begin of __alloc
// + added detailed comments about specifications (below)

/* Note on specification: 

# Distinguish between memory region and memory area

we are building a virtual memory allocator library
we want each process THINKS it has multiple memory area next to each other (code, stack, etc)
there are VIRTUAL memory, aka, its addresses are virtual addresses

vm_area_struct has start and end pointer, these pointers are VIRTUAL boundaries
however, we further limit the usable within this area up to the sbrk, NOT vm_end

I think we do not need to care how virtual is mappped to physical ram
cause thats is in mm_vm_c
and is done through syscalls

we here provides a library for mem.c to interact with

tldr:
memory area >  memory region
a memory area contains many memory regions

crucially, the struct for area DO NOT have a list of all regions it has
it only has a list of the free ones


# Distinguish between page and frame

our virtual memory is segmented into pages
the actual physcial memory is segmented into frames
we have a map from pages to frames handled in a page table

# Mapping process handler

each process has a mm_struct inside that handles this virtual -> physical mapping 

mm_struct has 4 parts : 

pgd : <physical memory address> 
  address of the start of this process's page table
  used for virtual -> physical mapping

mmap : <linked list> 
  a big linked list of every memory areas of this process

symrgtbl : <array of 30 memory regions reserved to represent namespaces>
  we allow a maximum of 30 namespaces per process
  each namespace is 1 region

fifo_page : <the next page if fifo is used for page swap>
  <currently unused, teach reccoment use for page swapping, but we can also not use this>

# Explains every functions in this file: (unfinished note)

enlist_vm_freerg_list: (teach implement)
  add a region to the free region list of the first area in the given manager
  add to top
  return -1 if invalid region, 0 if successful

  however, 1 potential bug(?) i noticed is that it only fix the link if the free list isnt null
  consequence is if we invoke this function with a already linked region
  aka, the passed in region is linked to something else
  it will only break this link if the free list is not null
  leading to...potentially adding multiple regions to the free list at once
  dont know if this is a bug or a feature honestly

get_symrg_byid: (teach implement)
  get a symbol by symbol index (id)
  returns null if invalid index

__alloc: (we implement)
  the caller wants to allocate a memory with size <size> to the symbol <rgid> 
  in region <rgid> in area <vmaid>
  params:
    <pcb data> caller
    <memory area id> vmaid
    <memory region id> rgid
    size
    
    
    <change this to the start of the allocated address> alloc_addr

  progress: 80%, problem with syscall 17 rn

__free: (we implement)  
  adds a region to freerg list

liballoc: wrapper to call __alloc on virtual area 0 (guess)
libfree: wrapper to call __free on virtual area 0 (guess)
  


get_free_vmrg_area (we implement) (implemented?)
  


*/


#include "string.h"
#include "mm.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1; //invalid reagion?

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, int size, int *alloc_addr)
{
  // [ 31/03/2025 - Minh ]
  //the problem: each memory area starts at vm_start, ends at vm_ends
  //however, in the middle, we have chunks called vm_rg, which is used and cannot be given to programs
  //the free regions therefore, is segmented
  //free regions list is captured in the struct vm_freerg_list

  //our task here, is to 
  //1. find the next free region
  //2. try to allocate it
  // if fail 1 : no free region in list --> syscall meminc
  // if fail 2 : limit reached (free region recceived at NEWLIMIT) --> attempt to increase limit
  // more comment below by teach

  /*Allocate at the toproof */
  struct vm_rg_struct rgnode;

  /* TODO: commit the vmaid */
  //convert the vmaid into mem area
  //vmaid is the id

  pthread_mutex_lock(&mmvm_lock);
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
 
    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  /* TODO retrive current vma if needed, current comment out due to compiler redundant warning*/
  /*Attempt to increate limit to get space */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  int inc_sz = PAGING_PAGE_ALIGNSZ(size);
  //note: inc_sz is passed below to system call to increase the size
  //buttt in mm-vm.c the size is alligned AGAIN
  //thus this is redundant
  //ima keep it in cause its teach's code but ehhhh
  //may be removable
  int inc_limit_ret;

  /* TODO retrive old_sbrk if needed, current comment out due to compiler redundant warning*/
  int old_sbrk = cur_vma->sbrk;

  /* INCREASE THE LIMIT as inovking systemcall 
   * sys_memap with SYSMEM_INC_OP 
   */
  //NOTE: use OUR syscall system
  //syscall 17 as noted in syscall.tbl is sys_memmap 
  //a1 - a3 note is taken from sys_mem.c and mm-vm.c

  //luckily, not our job here to know how syscall 17 is implemented
  //its...someone else in the team lmao
  struct sc_regs regs;
  regs.a1 =  SYSMEM_INC_OP; //memory operation
  regs.a2 = vmaid; //vmaid
  regs.a3 = inc_sz; //increase size
  
  // SYSCALL 17 sys_memmap
  int status = syscall(caller, 17, &regs); 
  if(status != 0) return status;

  //ok, I have no clue where the allocation is 
  //its a bunch of unfinished implementation???
  //commit here means we take whatever the syscall do and apply it
  //but i dont know how to apply it cause idk where the syscall allocates the new thing?


  /* commit the limit increment */
    
  caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = old_sbrk+size;
  *alloc_addr =old_sbrk;

  /* TODO: commit the allocation address 
  // *alloc_addr = ...
  */

  pthread_mutex_unlock(&mmvm_lock);
  return 0;

}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  // struct vm_rg_struct rgnode;

  // Dummy initialization for avoding compiler dummay warning
  // in incompleted TODO code rgnode will overwrite through implementing
  // the manipulation of rgid later

  if(rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return -1;

  /* Manage the collect freed region to freerg_list */
  struct vm_rg_struct * rgnode = get_symrg_byid(caller->mm, rgid);


  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, rgnode);

  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
  /* TODO Implement allocation on vm area 0 */
  int addr;

  /* By default using vmaid = 0 */
  return __alloc(proc, 0, reg_index, size, &addr);
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  /* TODO Implement free region */

  /* By default using vmaid = 0 */
  return __free(proc, 0, reg_index);
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
// [ 4/4/2025 - Chung ]
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  uint32_t pte = mm->pgd[pgn];

  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    int vicpgn, swpfpn; 

    uint32_t vicpte = mm->pgd[vicpgn];
    
    int vicfpn = PAGING_PTE_FPN(vicpte);
    int tgtfpn = PAGING_PTE_SWP(pte);//the target frame storing our variable

    /* TODO: Play with your paging theory here */
    /* Find victim page */
    find_victim_page(caller->mm, &vicpgn);

    /* Get free frame in MEMSWP */
    MEMPHY_get_freefp(caller->active_mswp, &swpfpn);

    /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/

    /* TODO copy victim frame to swap 
     * SWP(vicfpn <--> swpfpn)
     * SYSCALL 17 sys_memmap 
     * with operation SYSMEM_SWP_OP
     */
    struct sc_regs regs;
    regs.a1 = SYSMEM_SWP_OP;
    regs.a2 = vicfpn;
    regs.a3 = swpfpn;

    /* SYSCALL 17 sys_memmap */
    int status = syscall(caller, 17, &regs);
    if (status != 0) return status;

    /* TODO copy target frame form swap to mem 
     * SWP(tgtfpn <--> vicfpn)
     * SYSCALL 17 sys_memmap
     * with operation SYSMEM_SWP_OP
     */
    /* TODO copy target frame form swap to mem 
    */
   regs.a1 = SYSMEM_SWP_OP
   regs.a2 = tgtfpn;
   regs.a3 = vicfpn;

    /* SYSCALL 17 sys_memmap */
    status = syscall(caller, 17, &regs);
    if (status != 0) return status;

    /* Update page table */
    // pte_set_swap() 
    //mm->pgd;

    /* Update its online status of the target page */
    //pte_set_fpn() &
    //mm->pgd[pgn];
    //pte_set_fpn();

    enlist_pgn_node(&caller->mm->fifo_pgn,pgn);
  }

  *fpn = PAGING_FPN(mm->pgd[pgn]);

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  //int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  /* TODO 
   *  MEMPHY_read(caller->mram, phyaddr, data);
   *  MEMPHY READ 
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */
  // int phyaddr
  //struct sc_regs regs;
  //regs.a1 = ...
  //regs.a2 = ...
  //regs.a3 = ...

  /* SYSCALL 17 sys_memmap */

  // Update data
  // data = (BYTE)

  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  //int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  /* TODO
   *  MEMPHY_write(caller->mram, phyaddr, value);
   *  MEMPHY WRITE
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_WRITE
   */
  // int phyaddr
  //struct sc_regs regs;
  //regs.a1 = ...
  //regs.a2 = ...
  //regs.a3 = ...

  /* SYSCALL 17 sys_memmap */

  // Update data
  // data = (BYTE) 

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE *data)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
    return -1;

  pg_getval(caller->mm, currg->rg_start + offset, data, caller);

  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    uint32_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  /* TODO update result of reading action*/
  //destination 
#ifdef IODUMP
  printf("read region=%d offset=%d value=%d\n", source, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  MEMPHY_dump(proc->mram);
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE value)
{
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
    return -1;

  pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    uint32_t offset)
{
#ifdef IODUMP
  printf("write region=%d offset=%d value=%d\n", destination, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); //print max TBL
#endif
  MEMPHY_dump(proc->mram);
#endif

  return __write(proc, 0, destination, offset, data);
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  int pagenum, fpn;
  uint32_t pte;


  for(pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte= caller->mm->pgd[pagenum];

    if (!PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_PTE_FPN(pte);
      MEMPHY_put_freefp(caller->mram, fpn);
    } else {
      fpn = PAGING_PTE_SWP(pte);
      MEMPHY_put_freefp(caller->active_mswp, fpn);    
    }
  }

  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
// [ 03/04/2025 - Chung ]
int find_victim_page(struct mm_struct *mm, int *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */

  if (!pg) {
    return -1;
  }

  retpgn = pg->pgn;
  mm->fifo_pgn = mm->fifo_pgn->pg_next;

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *returns 0 if success, -1 if fail
 */
// modified on 31/03/2025 by Minh
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* TODO Traverse on list of free vm region to find a fit space */
  //while (...)
  // ..
  struct vm_rg_struct ** runner = &rgit;
  return get_free_helper_best_fit(runner, size, newrg);
  return 0;
}

// modified on 31/03/2025 by Minh
//helper func
int get_free_helper_first_fit(
  struct vm_rg_struct ** runner, 
  int size, struct 
  vm_rg_struct *target
){
  if(runner == NULL || *runner == NULL) return -1;
  while(*runner){
    //first fit algo
    struct vm_rg_struct * c = *runner;
    int s = c->rg_end - c->rg_start + 1;
    if(s >= size){
      target->rg_start = c->rg_start;
      target->rg_end = target->rg_start + size;
      //remove from free list
      c->rg_start = c->rg_end + 1;
      if(c->rg_start > c->rg_end){
        //cut (c) out of list
        struct vm_rg_struct * d = c->rg_next;
        //I actually have no clue what the heck to do here
        //normally i would call delete c
        //but in this context, that would mean putting the region 
        //previously occupied by c into free list again
        //that makes no sense tbh, the system mem would decreases
        //ahhhh
        //edit: call free?
        free(c);
        *runner = d;
        return 0;
      }
    }
    runner = &((*runner)->rg_next);
  }
  return -1;
}

int get_free_helper_best_fit(
  struct vm_rg_struct ** runner, 
  int size, struct 
  vm_rg_struct *target
){
  if(runner == NULL || *runner == NULL) return -1;
  struct vm_rg_struct ** bestRunner = NULL;
  struct vm_rg_struct * c = *runner;
  int bestScore = -1;
  while(*runner){
    //best fit algo
    struct vm_rg_struct * c = *runner;
    int s = c->rg_end - c->rg_start + 1;
    if(s >= size) {
      int score = s - size;
      if(score < bestScore || bestScore == -1) {
        bestRunner = runner;
        bestScore = score;
      }
    }
    runner = &((*runner)->rg_next);
  }
  if(bestRunner == NULL) return -1;
  c = *bestRunner;
  target->rg_start = c->rg_start;
  target->rg_end = target->rg_start + size;
  //remove from free list
  c->rg_start = c->rg_end + 1;
  if(c->rg_start > c->rg_end){
    //cut (c) out of list
    struct vm_rg_struct * d = c->rg_next;
    free(c);
    *runner = d;
    return 0;
  }
  return -1;
}

//#endif
