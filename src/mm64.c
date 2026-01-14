/*
 * PAGING based Memory Management
 * Memory management unit mm/mm64.c
 */

#include "../include/mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h> 
#include <pthread.h> 
#include "../include/libmem.h"

#if defined(MM64)


int find_victim_page(struct mm_struct *mm, int *retpgn, struct pcb_t **ret_owner);

/* __swap_cp_page: Copy dữ liệu giữa RAM và Swap Disk */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }
  return 0;
}

/* init_pte - Initialize PTE entry */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Case 1: RAM Online
      if (fpn == 0) return -1; 
      
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else { // Case 2: Swapped Out
      CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }
  return 0;
}

/* get_pd_from_address */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
  *pgd = (addr & PAGING64_ADDR_PGD_MASK) >> PAGING64_ADDR_PGD_LOBIT;
  *p4d = (addr & PAGING64_ADDR_P4D_MASK) >> PAGING64_ADDR_P4D_LOBIT;
  *pud = (addr & PAGING64_ADDR_PUD_MASK) >> PAGING64_ADDR_PUD_LOBIT;
  *pmd = (addr & PAGING64_ADDR_PMD_MASK) >> PAGING64_ADDR_PMD_LOBIT;
  *pt  = (addr & PAGING64_ADDR_PT_MASK)  >> PAGING64_ADDR_PT_LOBIT;
  return 0;
}

int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT, pgd, p4d, pud, pmd, pt);
}

/* pte_set_swap - Set PTE entry for swapped page */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte;
  addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;
  int ret = 0;

  pthread_mutex_lock(&mm->mm_lock);

#ifdef MM64 
  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);
  
  if (mm->pgd == NULL) { mm->pgd = malloc(512 * sizeof(addr_t)); memset(mm->pgd, 0, 512 * sizeof(addr_t)); }
  if (mm->pgd[pgd_idx] == 0) { mm->pgd[pgd_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)mm->pgd[pgd_idx], 0, 512 * sizeof(addr_t)); }
  addr_t *p4d_table = (addr_t *)mm->pgd[pgd_idx];
  
  if (p4d_table[p4d_idx] == 0) { p4d_table[p4d_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)p4d_table[p4d_idx], 0, 512 * sizeof(addr_t)); }
  addr_t *pud_table = (addr_t *)p4d_table[p4d_idx];
  
  if (pud_table[pud_idx] == 0) { pud_table[pud_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)pud_table[pud_idx], 0, 512 * sizeof(addr_t)); }
  addr_t *pmd_table = (addr_t *)pud_table[pud_idx];
  
  if (pmd_table[pmd_idx] == 0) { pmd_table[pmd_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)pmd_table[pmd_idx], 0, 512 * sizeof(addr_t)); }
  addr_t *pt_table = (addr_t *)pmd_table[pmd_idx];
  
  pte = &pt_table[pt_idx];
#else
  pte = &mm->pgd[pgn];
#endif
  
  CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  pthread_mutex_unlock(&mm->mm_lock);
  return ret;
}

/* pte_set_fpn - Set PTE entry for on-line page */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte;
  addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;
  int ret = 0;
  pthread_mutex_lock(&mm->mm_lock);
  
#ifdef MM64 
  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);
  
  if (mm->pgd == NULL) { mm->pgd = malloc(512 * sizeof(addr_t)); memset(mm->pgd, 0, 512 * sizeof(addr_t)); }
  if (mm->pgd[pgd_idx] == 0) { mm->pgd[pgd_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)mm->pgd[pgd_idx], 0, 512 * sizeof(addr_t)); }
  addr_t *p4d_table = (addr_t *)mm->pgd[pgd_idx];
  
  if (p4d_table[p4d_idx] == 0) { p4d_table[p4d_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)p4d_table[p4d_idx], 0, 512 * sizeof(addr_t)); }
  addr_t *pud_table = (addr_t *)p4d_table[p4d_idx];
  
  if (pud_table[pud_idx] == 0) { pud_table[pud_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)pud_table[pud_idx], 0, 512 * sizeof(addr_t)); }
  addr_t *pmd_table = (addr_t *)pud_table[pud_idx];
  
  if (pmd_table[pmd_idx] == 0) { pmd_table[pmd_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)pmd_table[pmd_idx], 0, 512 * sizeof(addr_t)); }
  addr_t *pt_table = (addr_t *)pmd_table[pmd_idx];
  
  pte = &pt_table[pt_idx];
#else
  pte = &mm->pgd[pgn];
#endif

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  pthread_mutex_unlock(&mm->mm_lock);
  return ret;
}

/* pte_get_entry */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  struct mm_struct *mm = caller->mm;
  uint32_t pte = 0;
  addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;
  
  pthread_mutex_lock(&mm->mm_lock);
  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);
  
  if (mm->pgd && mm->pgd[pgd_idx]) {
      addr_t *p4d = (addr_t *)mm->pgd[pgd_idx];
      if (p4d[p4d_idx]) {
          addr_t *pud = (addr_t *)p4d[p4d_idx];
          if (pud[pud_idx]) {
              addr_t *pmd = (addr_t *)pud[pud_idx];
              if (pmd[pmd_idx]) {
                  addr_t *pt = (addr_t *)pmd[pmd_idx];
                  pte = (uint32_t)pt[pt_idx];
              }
          }
      }
  }
  pthread_mutex_unlock(&mm->mm_lock);
  return pte;
}

/* pte_set_entry */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
  struct mm_struct *mm = caller->mm;
  addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;
  int ret = 0;

  pthread_mutex_lock(&mm->mm_lock);

#ifdef MM64
  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);
  
  if (mm->pgd == NULL) {
    mm->pgd = malloc(512 * sizeof(addr_t));
    memset(mm->pgd, 0, 512 * sizeof(addr_t));
  }
  
  if (mm->pgd[pgd_idx] == 0) {
    mm->pgd[pgd_idx] = (addr_t)malloc(512 * sizeof(addr_t));
    memset((void*)mm->pgd[pgd_idx], 0, 512 * sizeof(addr_t));
  }
  addr_t *p4d_table = (addr_t *)mm->pgd[pgd_idx];
  
  if (p4d_table[p4d_idx] == 0) {
    p4d_table[p4d_idx] = (addr_t)malloc(512 * sizeof(addr_t));
    memset((void*)p4d_table[p4d_idx], 0, 512 * sizeof(addr_t));
  }
  addr_t *pud_table = (addr_t *)p4d_table[p4d_idx];
  
  if (pud_table[pud_idx] == 0) {
    pud_table[pud_idx] = (addr_t)malloc(512 * sizeof(addr_t));
    memset((void*)pud_table[pud_idx], 0, 512 * sizeof(addr_t));
  }
  addr_t *pmd_table = (addr_t *)pud_table[pud_idx];
  
  if (pmd_table[pmd_idx] == 0) {
    pmd_table[pmd_idx] = (addr_t)malloc(512 * sizeof(addr_t));
    memset((void*)pmd_table[pmd_idx], 0, 512 * sizeof(addr_t));
  }
  addr_t *pt_table = (addr_t *)pmd_table[pmd_idx];
  
  pt_table[pt_idx] = pte_val;
#else
  mm->pgd[pgn] = pte_val;
#endif
  
  pthread_mutex_unlock(&mm->mm_lock);
  return ret;
}

/* vmap_pgd_memset */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int pgnum)
{
  struct mm_struct *mm = caller->mm;
  int pgit = 0;
  addr_t pgn;
  int ret = 0;

  pthread_mutex_lock(&mm->mm_lock);

  for (pgit = 0; pgit < pgnum; pgit++) {
    pgn = (addr >> PAGING64_ADDR_PT_SHIFT) + pgit;
    
    addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;
    get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);
    
    // (Logic malloc tương tự pte_set_entry)
    if (mm->pgd == NULL) { mm->pgd = malloc(512 * sizeof(addr_t)); memset(mm->pgd, 0, 512 * sizeof(addr_t)); }
    if (mm->pgd[pgd_idx] == 0) { mm->pgd[pgd_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)mm->pgd[pgd_idx], 0, 512 * sizeof(addr_t)); }
    addr_t *p4d_table = (addr_t *)mm->pgd[pgd_idx];
    if (p4d_table[p4d_idx] == 0) { p4d_table[p4d_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)p4d_table[p4d_idx], 0, 512 * sizeof(addr_t)); }
    addr_t *pud_table = (addr_t *)p4d_table[p4d_idx];
    if (pud_table[pud_idx] == 0) { pud_table[pud_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)pud_table[pud_idx], 0, 512 * sizeof(addr_t)); }
    addr_t *pmd_table = (addr_t *)pud_table[pud_idx];
    if (pmd_table[pmd_idx] == 0) { pmd_table[pmd_idx] = (addr_t)malloc(512 * sizeof(addr_t)); memset((void*)pmd_table[pmd_idx], 0, 512 * sizeof(addr_t)); }
    addr_t *pt_table = (addr_t *)pmd_table[pmd_idx];
    
    pt_table[pt_idx] = 0xDEADBEEF; 
  }

  pthread_mutex_unlock(&mm->mm_lock);
  return ret;
}

/* enlist_pgn_node */
int enlist_pgn_node(struct pgn_t **plist, int pgn, struct pcb_t *owner)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));
  pnode->pgn = pgn;
  pnode->owner = owner; 
  pnode->pg_next = *plist;
  *plist = pnode;
  return 0;
}

/* vmap_page_range */
addr_t vmap_page_range(struct pcb_t *caller, addr_t addr, int pgnum, 
                       struct framephy_struct *frames, struct vm_rg_struct *ret_rg)
{
  int pgit = 0;
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;
  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr + pgnum * PAGING64_PAGESZ;
  
  for (pgit = 0; pgit < pgnum && frames != NULL; pgit++, frames = frames->fp_next) {
    pte_set_fpn(caller, pgn + pgit, frames->fpn);
    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn + pgit, caller);
  }
  return 0;
}

/* alloc_pages_range */
addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  int pgit;
  addr_t fpn;
  struct framephy_struct *newfp_str;
  struct framephy_struct *last_fp = NULL; 

  for(pgit = 0; pgit < req_pgnum; pgit++)
  {
    newfp_str = malloc(sizeof(struct framephy_struct));
    newfp_str->fp_next = NULL;
    newfp_str->owner = caller->mm;

    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) == 0) {
       newfp_str->fpn = fpn;
    } 
    else { 
       // RAM đầy -> Swap Out (Global Replacement)
       int vicpgn;
       addr_t swpfpn;
       struct pcb_t *vic_owner;
       
       if (find_victim_page(caller->krnl->mm, &vicpgn, &vic_owner) < 0) { 
           printf("Error: OOM - Cannot find victim page\n");
           free(newfp_str); 
           return -3000;
       }

       uint32_t vicpte = pte_get_entry(vic_owner, vicpgn);
       fpn = PAGING_FPN(vicpte); 

       if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) < 0) {
           free(newfp_str); return -3000;
       }
       
       __swap_cp_page(caller->krnl->mram, fpn, caller->krnl->active_mswp, swpfpn);
       pte_set_swap(vic_owner, vicpgn, 0, swpfpn);
       newfp_str->fpn = fpn;
    }
    
    if (*frm_lst == NULL) *frm_lst = newfp_str;
    else last_fp->fp_next = newfp_str;
    last_fp = newfp_str;
  }
  return 0;
}

/* vm_map_ram */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;
  int pgnum = incpgnum;
  ret_alloc = alloc_pages_range(caller, pgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000) return -1;
  if (ret_alloc == -3000) return -1;

  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);
  return 0;
}

/* init_mm */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
  if (vma0 == NULL) return -1; 

  mm->pgd = NULL;
  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt  = NULL;

  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  
  vma0->vm_freerg_list = NULL; 

  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  vma0->vm_next = NULL;
  vma0->vm_mm = mm;
  mm->mmap = vma0;

  for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++) {
     mm->symrgtbl[i].rg_start = 0;
     mm->symrgtbl[i].rg_end = 0;
     mm->symrgtbl[i].rg_next = NULL;
  }

  mm->fifo_pgn = NULL;

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP); 
  pthread_mutex_init(&mm->mm_lock, &attr); 
  pthread_mutexattr_destroy(&attr);

  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));
  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;
  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;
  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;
  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;
  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;
  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  int i, j, k, l, m; 
  struct mm_struct *mm = caller->mm;

  if (!mm || !mm->pgd) {
      printf("print_pgtbl: PGD is NULL\n"); 
      return 0;
  }

  printf("====================================> print_pgtbl with process pid %d <==================================\n", caller->pid);

  for (i = 0; i < 512; i++) {
    if (mm->pgd[i] == 0) continue;
    addr_t *p4d_base = (addr_t *)mm->pgd[i];
    
    for (j = 0; j < 512; j++) {
      if (p4d_base[j] == 0) continue;
      addr_t *pud_base = (addr_t *)p4d_base[j];
      
      for (k = 0; k < 512; k++) {
        if (pud_base[k] == 0) continue;
        addr_t *pmd_base = (addr_t *)pud_base[k];
        
        for (l = 0; l < 512; l++) {
          if (pmd_base[l] == 0) continue;
          addr_t *pt_base = (addr_t *)pmd_base[l];

          for (m = 0; m < 512; m++) {
            if (pt_base[m] == 0) continue; 
            uint32_t pte = (uint32_t)pt_base[m]; 
            
            printf("\tPDG=%016lx P4g=%016lx PUD=%016lx PMD=%016lx PTE=%08x",
                   (uint64_t)mm->pgd[i], (uint64_t)p4d_base[j], (uint64_t)pud_base[k], (uint64_t)pmd_base[l], pte);

            if (pte & PAGING_PTE_SWAPPED_MASK) {
                int swptyp = (pte & PAGING_PTE_SWPTYP_MASK) >> PAGING_PTE_SWPTYP_LOBIT;
                int swpoff = (pte & PAGING_PTE_SWPOFF_MASK) >> PAGING_PTE_SWPOFF_LOBIT;
                printf(" [SWAP] Device: %d, Offset: %d", swptyp, swpoff);
            } else if (pte & PAGING_PTE_PRESENT_MASK) {
                int fpn = (pte & PAGING_PTE_FPN_MASK) >> PAGING_PTE_FPN_LOBIT;
                printf(" [RAM] FPN: %d", fpn);
            }
            printf("\n");
          }
        }
      }
    }
  }
  return 0;
}

#endif // defined(MM64)