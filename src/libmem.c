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
  teach reccoment use for page swapping, but we can also not use this
  
Note on free(void * ptr)
free is a function SPECIFICALLY to free memory on the HEAP
aka, memory previously obtained using malloc, realloc, calloc only
doing the normal struct abc x = ....
means its on the HEAP
freeing it is unintended behavior

whats on the heap in our project:
the vm_area_rg struct (malloc in mm-vm-c, function get_vm_area_node_at_brk)
the framephy_struct struct (malloc in mm.c, function alloc_pages_range)
the mm struct (malloc in os.c, function ld_routine)
the pcb_t struct (malloc in loader.c, load funtion)

Probably more but idk, this seems all of it


NOTE: caller->page_table IS OBSOLETED, use mm->pgd instead
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
  
pg_*:
Not sure about fpn * PAGE_SIZE + off, since teacher use
fpn * PAGING_SIZESZ + off insteand, but mine makes more sense

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
 *version by Thiện
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  if(rg_elmt == NULL) return -1; //invalid region
  if (rg_elmt->rg_start >= rg_elmt->rg_end) return -1; //invalid reagion
  rg_elmt->rg_next = NULL; //this line explicitly fixxed the bug mentioned above
  
  struct vm_rg_struct ** runner =& mm->mmap->vm_freerg_list;
  if (!(*runner)) {
    *runner = rg_elmt;
    return 0;
  }
  
  if ((*runner)->rg_start > rg_elmt->rg_start) {
    if(rg_elmt->rg_end==(*runner)->rg_start) {
      (*runner)->rg_start = rg_elmt->rg_start;
      free(rg_elmt);
    } else {
      rg_elmt->rg_next = *runner;
      *runner = rg_elmt;
    }
    return 0;
  }
  while(*runner ){
    if (!(*runner)->rg_next) {
      if ((*runner)->rg_end == rg_elmt->rg_start) {
        
        (*runner)->rg_end = rg_elmt->rg_end;
        free(rg_elmt);
      }
      else (*runner)->rg_next = rg_elmt;
      
      return 0;
    }
    if ((*runner)->rg_next->rg_start >rg_elmt->rg_start) {
      if(rg_elmt->rg_end==(*runner)->rg_next->rg_start) {
        (*runner)->rg_next->rg_start = rg_elmt->rg_start;
        free(rg_elmt);
        if ((*runner)->rg_next->rg_start == (*runner)->rg_end){
          (*runner)->rg_next->rg_start=(*runner)->rg_start;
          struct vm_rg_struct *temp=*runner;
          *runner=(*runner)->rg_next;
          free(temp);
        }
      }else if ((*runner)->rg_end == rg_elmt->rg_start) {
        (*runner)->rg_end = rg_elmt->rg_end;
        free(rg_elmt);
      }else{
        rg_elmt->rg_next = (*runner)->rg_next;
        (*runner)->rg_next = rg_elmt;
      }
      return 0;

    }
  runner = &(*runner)->rg_next;
  }
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

  /* commit the vmaid */
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
  //struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  //int inc_sz = PAGING_PAGE_ALIGNSZ(size);
  //note: inc_sz is passed below to system call to increase the size
  //buttt in mm-vm.c the size is alligned AGAIN
  //thus this is redundant
  //ima keep it in cause its teach's code but ehhhh
  //may be removable
  // int inc_limit_ret;

  /* TODO retrive old_sbrk if needed, current comment out due to compiler redundant warning*/
  // int old_sbrk = cur_vma->sbrk;

  /* INCREASE THE LIMIT as inovking systemcall 
   * sys_memap with SYSMEM_INC_OP 
   */
  //NOTE: use OUR syscall system
  //syscall 17 as noted in syscall.tbl is sys_memmap 
  //a1 - a3 note is taken from sys_mem.c and mm-vm.c

  //luckily, not our job here to know how syscall 17 is implemented
  //its...someone else in the team lmao
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  int old_sbrk = cur_vma->sbrk;
  struct sc_regs regs;
  regs.a1 =  SYSMEM_INC_OP; //memory operation
  regs.a2 = vmaid; //vmaid
  regs.a3 = size; //increase size
  // SYSCALL 17 sys_memmap
  int status = syscall(caller, 17, &regs); 
  if(status != 0) {
    alloc_addr = NULL;
    pthread_mutex_unlock(&mmvm_lock);
    return status;
  }
  /* commit the limit increment */

  // Solution proposed by Thien at merging of patch 7
  // just run the get free again 
  //since by agreement, the new freed area is enlisted again

  if(cur_vma->sbrk-old_sbrk != size) {
    //stil fails to find free area after all that
    alloc_addr = NULL;
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  };
  caller->mm->symrgtbl[rgid].rg_start =old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = cur_vma->sbrk;
  *alloc_addr = old_sbrk;
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
  // the manipulation of rgid later

  if(rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return -1;

    
    /* Manage the collect freed region to freerg_list */
    
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct * rgnode = get_symrg_byid(caller->mm, rgid);

  if(rgnode->rg_start >= rgnode->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1; //invalid region, avoid double free
  }
  //hard copy to a new struct
  struct vm_rg_struct * newEmptyrg = malloc(sizeof(struct vm_rg_struct));
  newEmptyrg->rg_start = rgnode->rg_start;
  newEmptyrg->rg_end = rgnode->rg_end;
  
  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, newEmptyrg);

  //make the old symbol unusable
  rgnode->rg_end = 0;
  rgnode->rg_start = 0;
  rgnode->rg_next = NULL;

  pthread_mutex_unlock(&mmvm_lock);

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
  int val = __alloc(proc, 0, reg_index, size, &addr);
  pthread_mutex_lock(&mmvm_lock);
  printf("===== PHYSICAL MEMORY AFTER ALLOCATION =======\n");
  printf("PID=%d - Region=%d - Address=%08x - Size=%d bytes\n", proc->pid, reg_index, addr, size);
  print_pgtbl(proc, 0, proc->mm->mmap->vm_end);
  for (int i = 0; i < PAGING_MAX_PGN; ++i) {
    if (PAGING_PAGE_PRESENT(proc->mm->pgd[i])){
      printf("Page Number: %d -> Frame Number : %d\n", i, PAGING_FPN(proc->mm->pgd[i]));
    }
  }
  printf("==============================================\n");
  pthread_mutex_unlock(&mmvm_lock);
  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  /* TODO Implement free region */
  int val = __free(proc, 0, reg_index);
  pthread_mutex_lock(&mmvm_lock);
  printf("===== PHYSICAL MEMORY AFTER DEALLOCATION =====\n");
  printf("PID=%d - Region=%d\n", proc->pid, reg_index);
  print_pgtbl(proc, 0, proc->mm->mmap->vm_end);
  for (int i = 0; i < PAGING_MAX_PGN; ++i) {
    if (PAGING_PAGE_PRESENT(proc->mm->pgd[i])){
      printf("Page Number: %d -> Frame Number : %d\n", i, PAGING_FPN(proc->mm->pgd[i]));
    }
  }
  printf("==============================================\n");
  pthread_mutex_unlock(&mmvm_lock);
  /* By default using vmaid = 0 */
  return val;
}

// [ 4/4/2025 - Chung ]
// [20/4/2025 - Minh, remove the lock since this is an internal function called only in getval and setval, which already has a lock]
//granted...i added the lock in the first place, sory
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
    int status;


    //we assume if not in ram = in swap
    int victim_pgn, free_fpn_in_active_swap; 
    /* TODO: Play with your paging theory here */
    /* Find victim page */
    status = find_victim_page(caller->mm, &victim_pgn);
    if(status != 0) return status;
    
    int victim_fpn = PAGING_PTE_FPN(mm->pgd[victim_pgn]);
    int target_fpn = PAGING_PTE_SWP(pte);//the target frame storing our variable
    
    /* Get free frame in MEMSWP */
    status = MEMPHY_get_freefp(caller->active_mswp, &free_fpn_in_active_swap);
    if(status != 0) return status;

    /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/


    //Minh - 20/04/2025, changed back to using syscalls
    //swap procedure:
    // victim -> free
    
    struct sc_regs regs;
    regs.a1 = SYSMEM_SWP_OP;
    regs.a2 = victim_fpn;
    regs.a3 = free_fpn_in_active_swap;
    
    // /* SYSCALL 17 sys_memmap */
    status = syscall(caller, 17, &regs);
    if (status != 0) return status;
    
    // target -> victim 
    regs.a2 = target_fpn;
    regs.a3 = victim_fpn;

    /* SYSCALL 17 sys_memmap */
    status = syscall(caller, 17, &regs);
    if (status != 0) return status;

    /* Update page table entries */

    //sets the frame number of pgn to victim's fpn
    pte_set_fpn(&mm->pgd[pgn], victim_fpn);
    
    //sets the frame number of victim fpn to being in swapped now
    //after 2 hours of reading
    //swap type is unmentioned
    //but i presumed its the id of the active swap device
    //its unused anyway
    pte_set_swap(&mm->pgd[victim_pgn], caller->active_mswp_id, free_fpn_in_active_swap);

    // - Add to the fifo queue
    enlist_pgn_node(&caller->mm->fifo_pgn, pgn);

    //enlist the free of the target's old frames into the current active mem swap device
    MEMPHY_put_freefp(caller->active_mswp, target_fpn);

    *fpn = victim_fpn;
    return 0;
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
  pthread_mutex_lock(&mmvm_lock);
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0) {
    data = NULL;
    pthread_mutex_unlock(&mmvm_lock); 
    return -1; /* invalid page access */
  }

  /* TODO 
   *  MEMPHY_read(caller->mram, phyaddr, data);
   *  MEMPHY READ 
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = (fpn << 8) + off;
  regs.a3 = 0;

  /* SYSCALL 17 sys_memmap */
  int status = syscall(caller, 17, &regs); //pass in caller, caller->mram is called sys_mem.c already
  if(status != 0){
    data = NULL;
    pthread_mutex_unlock(&mmvm_lock); 
    return -1;
  }

  // Update data
  *data = regs.a3;
  pthread_mutex_unlock(&mmvm_lock);
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
  pthread_mutex_lock(&mmvm_lock);
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0){
    pthread_mutex_unlock(&mmvm_lock);
    return -1; /* invalid page access */
  }


  /* TODO
   *  MEMPHY_write(caller->mram, phyaddr, value);
   *  MEMPHY WRITE
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_WRITE
   */
  // int phyaddr
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = (fpn << 8) + off;
  regs.a3 = value;

  /* SYSCALL 17 sys_memmap */
  int status = syscall(caller, 17, &regs);
  if(status != 0){
    pthread_mutex_unlock(&mmvm_lock); 
    return -1;
  }
  pthread_mutex_unlock(&mmvm_lock);
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

  if(currg->rg_start >= currg->rg_end)
    return -1; //invalid or freed region, disallow read

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
*destination = data;

printf("======= PHYSICAL MEMORY AFTER READING ========\n");
#ifdef IODUMP
printf("read region=%d offset=%d value=%d\n", source, offset, data);
#ifdef PAGETBL_DUMP
print_pgtbl(proc, 0, -1); //print max TBL
#endif
MEMPHY_dump(proc->mram);
for (int i = 0; i < PAGING_MAX_PGN; ++i) {
  if (PAGING_PAGE_PRESENT(proc->mm->pgd[i])){
    printf("Page Number: %d -> Frame Number : %d\n", i, PAGING_FPN(proc->mm->pgd[i]));
  }
}
printf("==============================================\n");
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

  if(currg->rg_start >= currg->rg_end)
    return -1; //invalid or freed region, disallow write

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
int val = __write(proc, 0, destination, offset, data);
#ifdef IODUMP
pthread_mutex_lock(&mmvm_lock);
printf("======= PHYSICAL MEMORY AFTER WRITING ========\n");
printf("write region=%d offset=%d value=%d\n", destination, offset, data);

#ifdef PAGETBL_DUMP
print_pgtbl(proc, 0, -1); //print max TBL
#endif
MEMPHY_dump(proc->mram);
for (int i = 0; i < PAGING_MAX_PGN; ++i) {
  if (PAGING_PAGE_PRESENT(proc->mm->pgd[i])){
    printf("Page Number: %d -> Frame Number : %d\n", i, PAGING_FPN(proc->mm->pgd[i]));
  }
}
printf("==============================================\n");
pthread_mutex_unlock(&mmvm_lock);

#endif

return val;
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

    //why the ! exist ?
    if (PAGING_PAGE_PRESENT(pte))
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
  struct pgn_t ** pg = &(mm->fifo_pgn);

  /* Implement the theorical mechanism to find the victim page */
  //current implementation: uses fifo for now

  //page finding strategy:
  //OPTIMAL: unimplementable
  //FIFO: returns the last page of mm->fifo_pgn list, since enlist_pgn_node adds it to the front
  //FILO: returns the first page of the mm->fifo_pgn_list
  //second chance: each page needs a reference bit, we....do not store...that bit
  //one possible fix is just change the pgn struct
  //but seems dangerous
  //working set: require time interupts, we dont have that rn

  //just go with fifo for now

  if (!(*pg)) {
    return -1;
  }

  //returns the last page
  while((*pg)->pg_next) pg = &((*pg)->pg_next);
  (*retpgn) = (*pg)->pgn;
  //deletion
  free((*pg));
  (*pg) = NULL; 

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

  struct vm_rg_struct ** runner = &cur_vma->vm_freerg_list;

  if (* runner == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  return get_free_helper_best_fit(runner, size, newrg);
}

// modified on 31/03/2025 by Minh
//helper func

// 0 = overlapped
// 1 = A -> B
// 2 = B -> A
// -1 = at least 1 region ptr is NULL
//assumtion: region is correct and end > start 
int get_order_between_2_regions(struct vm_rg_struct * A, struct vm_rg_struct * B){
  if(A == NULL || B == NULL) return -1;
  if(A->rg_end < B->rg_start) return 1;
  if(A->rg_start > B->rg_end) return 2;
  return 0;
}

//unused, implemneted as a study case
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
        free(c); //calling free is safe since c is on the heap
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
    int s = c->rg_end - c->rg_start;
    if(s >= size) {
      int score = s - size;
      if((score > 0 && score < bestScore) || bestScore == -1) {
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

  //decrease region start
  c->rg_start = target->rg_end;

  // if region start overruns region end, remove it from list
  if(c->rg_start >= c->rg_end){
   //free(c); //calling free is safe since c is on the heap
   *bestRunner = c->rg_next;
  //  printf("next region start = %d, end = %d\n", c->rg_next->rg_start, c->next->rg_end);
    free(c);  
   return 0;
  }
  return 0;
}

//#endif
