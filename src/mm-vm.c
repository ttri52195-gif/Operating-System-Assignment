/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "../include/mm.h"
#include "../include/mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  struct vm_area_struct *pvma = mm->mmap;

  if (mm->mmap == NULL)
    return NULL;

  int vmait = pvma->vm_id;

  while (vmait < vmaid)
  {
    if (pvma == NULL)
      return NULL;

    pvma = pvma->vm_next;
    vmait = pvma->vm_id;
  }

  return pvma;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t size, addr_t alignedsz)
{
  struct vm_rg_struct * newrg;
  /* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  /* TODO: update the newrg boundary
  // newrg->rg_start = ...
  // newrg->rg_end = ...
  */

  newrg = malloc(sizeof(struct vm_rg_struct));
  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end = newrg->rg_start + size;
  /* END TODO */

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
{
  //struct vm_area_struct *vma = caller->krnl->mm->mmap;

  /* TODO validate the planned memory area is not overlapped */
  if (vmastart >= vmaend)
  {
    return -1;
  }

  struct vm_area_struct *vma = caller->mm->mmap;
  if (vma == NULL)
  {
    return -1;
  }

  /* TODO validate the planned memory area is not overlapped */

  struct vm_area_struct *cur_area = get_vma_by_num(caller->mm, vmaid);
  if (cur_area == NULL)
  {
    return -1;
  }

  while (vma != NULL)
  {
    if (vma != cur_area && OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end))
    {
      return -1;
    }
    vma = vma->vm_next;
  }
  /* End TODO*/

  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  pthread_mutex_lock(&caller->krnl->mm->mm_lock);
  struct vm_rg_struct * newrg = malloc(sizeof(struct vm_rg_struct));
  /* TOTO with new address scheme, the size need tobe aligned 
  *      the raw inc_sz maybe not fit pagesize
  */
  struct vm_area_struct * cur_area = get_vma_by_num(caller->mm, vmaid);
  #ifdef MM64
  int pagesz = PAGING64_PAGESZ; // 4096 bytes
  #else
  int pagesz = PAGING_PAGESZ;   // 256 bytes
  #endif

  addr_t old_sbrk = cur_area->sbrk;
  addr_t new_sbrk = old_sbrk + inc_sz;
  addr_t old_bound = (old_sbrk + pagesz - 1) / pagesz * pagesz;
  if(new_sbrk < old_bound){
    cur_area->sbrk = new_sbrk;
    free(newrg);
    pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
    return 0;
  }
  
  int inc_num_page = (new_sbrk - old_bound + pagesz - 1) / pagesz;
  addr_t inc_amt = inc_num_page * pagesz;

  struct vm_rg_struct *area = get_vm_area_node_at_brk(caller, vmaid, inc_sz, inc_amt);
  //area->rg_start = old_bound;
  /* TODO Validate overlap of obtained region */
  if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) < 0){
    free(newrg);
    free(area);
    pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
    return -1;
  }/*Overlap and failed allocation */

  /* TODO: Obtain the new vm area based on vmaid */
  // cur_vma->vm_end... 
  // inc_limit_ret...
  /* The obtained vm area (only)
    * now will be alloc real ram region */

  if (vm_map_ram(caller, area->rg_start, area->rg_end, 
                      old_bound, inc_num_page, newrg) < 0) {
    pthread_mutex_unlock(&caller->krnl->mm->mm_lock); 
    return -1; /* Map the memory to MEMRAM */
  }

  free(newrg);
  free(area);

  pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
  return 0;
}

// #endif