/*
 * System Library - Memory Module libmem.c 
 * FIXED VERSION WITH WORKING SWAP MECHANISM + TLB ADDED
 */

#include "string.h"
#include "../include/mm64.h"
#include "../include/syscall.h"
#include "../include/libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "../include/common.h"

// Global lock for physical memory resources
static pthread_mutex_t memphy_lock = PTHREAD_MUTEX_INITIALIZER;

/* ========================================================================= */
/* TLB IMPLEMENTATION (ADDED)                                                */
/* ========================================================================= */

#define TLB_SIZE 32 // Kích thước bảng TLB

struct tlb_entry {
    int pid;      // Process ID (Tag)
    int pgn;      // Page Number (Tag)
    int fpn;      // Frame Number (Data)
    int valid;    // Valid Bit
};

static struct tlb_entry tlb_cache[TLB_SIZE];
static pthread_mutex_t tlb_lock = PTHREAD_MUTEX_INITIALIZER;

// Biến thống kê Hit/Miss
static unsigned long tlb_hit_cnt = 0;
static unsigned long tlb_miss_cnt = 0;

/* Xóa sạch TLB */
void tlb_flush_all() {
    pthread_mutex_lock(&tlb_lock);
    for (int i = 0; i < TLB_SIZE; i++) {
        tlb_cache[i].valid = 0;
    }
    pthread_mutex_unlock(&tlb_lock);
}

/* Xóa một entry cụ thể (Dùng khi Free hoặc Swap Out) */
void tlb_clear_entry(int pid, int pgn) {
    pthread_mutex_lock(&tlb_lock);
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb_cache[i].valid && tlb_cache[i].pid == pid && tlb_cache[i].pgn == pgn) {
            tlb_cache[i].valid = 0;
        }
    }
    pthread_mutex_unlock(&tlb_lock);
}

/* In thống kê */
void print_tlb_stats() {
    unsigned long total = tlb_hit_cnt + tlb_miss_cnt;
    float hit_rate = (total > 0) ? ((float)tlb_hit_cnt / total) * 100.0 : 0.0;
    printf("   [TLB STATS] Hit: %lu | Miss: %lu | Total: %lu | Hit Rate: %.2f%%\n", 
           tlb_hit_cnt, tlb_miss_cnt, total, hit_rate);
}

/* Đọc từ TLB (Có cập nhật LRU - Move to Head) */
int tlb_cache_read(int pid, int pgn, int *fpn) {
    pthread_mutex_lock(&tlb_lock);
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb_cache[i].valid && tlb_cache[i].pid == pid && tlb_cache[i].pgn == pgn) {
            *fpn = tlb_cache[i].fpn;
            
            // LRU: Đưa entry vừa tìm thấy lên đầu
            if (i > 0) {
                struct tlb_entry temp = tlb_cache[i];
                for (int j = i; j > 0; j--) tlb_cache[j] = tlb_cache[j-1];
                tlb_cache[0] = temp;
            }
            
            tlb_hit_cnt++;
            pthread_mutex_unlock(&tlb_lock);
            return 0; // Hit
        }
    }
    tlb_miss_cnt++;
    pthread_mutex_unlock(&tlb_lock);
    return -1; // Miss
}

/* Ghi vào TLB (Chèn vào đầu, đẩy đuôi ra) */
void tlb_cache_write(int pid, int pgn, int fpn) {
    pthread_mutex_lock(&tlb_lock);
    
    // Nếu đã tồn tại -> Update
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb_cache[i].valid && tlb_cache[i].pid == pid && tlb_cache[i].pgn == pgn) {
            tlb_cache[i].fpn = fpn;
            // Move to head
            if (i > 0) {
                struct tlb_entry temp = tlb_cache[i];
                for (int j = i; j > 0; j--) tlb_cache[j] = tlb_cache[j-1];
                tlb_cache[0] = temp;
            }
            pthread_mutex_unlock(&tlb_lock);
            return;
        }
    }

    // Nếu chưa có -> Chèn mới (Shift right)
    for (int i = TLB_SIZE - 1; i > 0; i--) {
        tlb_cache[i] = tlb_cache[i-1];
    }
    
    tlb_cache[0].pid = pid;
    tlb_cache[0].pgn = pgn;
    tlb_cache[0].fpn = fpn;
    tlb_cache[0].valid = 1;
    
    pthread_mutex_unlock(&tlb_lock);
}

/* ========================================================================= */
/* HELPER FUNCTIONS                                                          */
/* ========================================================================= */

/*enlist_vm_freerg_list - add new rg to freerg_list */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;
  return &mm->symrgtbl[rgid];
}

/* ========================================================================= */
/* MEMORY ALLOCATION                                                         */
/* ========================================================================= */

/*__alloc - allocate a region memory */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  pthread_mutex_lock(&caller->krnl-> mm->mm_lock);
  struct vm_rg_struct rgnode;
//print_fifo_status(caller->krnl->mm);
  // Strategy 1: Try to reuse freed regions (Best-Fit)
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0) {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
    *alloc_addr = rgnode.rg_start;
    
    printf("=========> MEMORY ALLOCATION (REUSE) <=========\n");
    printf("PID: %d | Region: %d | Size: %ld | Address: %ld\n", 
           caller->pid, vmaid, size, *alloc_addr);
    
    pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
    return 0;
  }

  // Strategy 2: Expand heap (sbrk)
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  
  int inc_sz = PAGING_PAGE_ALIGNSZ(size);  // Round up to page size
  int old_sbrk = cur_vma->sbrk;
  
  // System call to kernel for physical frame allocation
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
  regs.a3 = inc_sz;
  syscall(caller->krnl, caller->pid, 17, &regs);

  // Update symbol table
  caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;
  *alloc_addr = old_sbrk;
  // CRITICAL: Update sbrk pointer
  cur_vma->sbrk += inc_sz;
  
  printf("=========> MEMORY ALLOCATION (EXPAND) <=========\n");
  printf("PID: %d | Region: %d | Size: %ld | Address: %ld\n", 
         caller->pid, vmaid, size, *alloc_addr);

  pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
  return 0;
}

/* ========================================================================= */
/* MEMORY DEALLOCATION                                                       */
/* ========================================================================= */

/*__free - remove a region memory */
int __free(struct pcb_t *caller, int vmaid, int rgid) 
{
  if (!caller || !caller->mm) return -1;
  struct mm_struct *mm = caller->mm;
  
  pthread_mutex_lock(&caller->krnl->mm->mm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ) {
    pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
    return -1;
  }

  struct vm_rg_struct *rgnode = get_symrg_byid(mm, rgid);
  
  if (rgnode == NULL || (rgnode->rg_start == 0 && rgnode->rg_end == 0)) {
    pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
    return -1;
  }
  
  // Calculate number of pages
  int size = rgnode->rg_end - rgnode->rg_start;
  int num_pages = (size + PAGING_PAGESZ - 1) / PAGING_PAGESZ;
  int pgn_start = (uint64_t)rgnode->rg_start >> PAGING_ADDR_PGN_LOBIT;

  pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
  
  // Free physical resources
  for (int i = 0; i < num_pages; i++) {
    int pgn = pgn_start + i;
    uint32_t pte = pte_get_entry(caller, pgn);
    
    if (PAGING_PAGE_PRESENT(pte)) {
      // Page in RAM
      int fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    } 
    else if (PAGING_PTE_SWP(pte)) {
      // Page in SWAP
      addr_t swpfpn = PAGING_SWP(pte);
      if (swpfpn != 0) {
        MEMPHY_put_freefp(caller->krnl->active_mswp, swpfpn);
      }
    }

    // Clear PTE
    pte_set_entry(caller, pgn, 0);
    
    // [TLB ADDITION] Xóa entry tương ứng trong TLB để tránh truy cập rác
    tlb_clear_entry(caller->pid, pgn);
  }

  pthread_mutex_lock(&caller->krnl->mm->mm_lock);
  
  // Add to free list for reuse
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);
  if (cur_vma != NULL) {
    struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
    freerg_node->rg_start = rgnode->rg_start;
    freerg_node->rg_end = rgnode->rg_end;
    
    freerg_node->rg_next = cur_vma->vm_freerg_list;
    cur_vma->vm_freerg_list = freerg_node;
  }

  // Clear ownership info
  rgnode->rg_start = 0;
  rgnode->rg_end = 0;
  rgnode->rg_next = NULL;
 
  pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
  return 0;
}

/* ========================================================================= */
/* PAGE MANAGEMENT WITH SWAP                                                 */
/* ========================================================================= */

/*
 * pg_getpage - Get page in RAM, perform swap in/out if needed
 * UPDATED: Checks TLB first
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  // [TLB ADDITION] Check TLB first 
  if (tlb_cache_read(caller->pid, pgn, fpn) == 0) {
      return 0; // TLB Hit
  }

  uint32_t pte = pte_get_entry(caller, pgn);
  
  int is_present = (pte & PAGING_PTE_PRESENT_MASK);
  int is_swapped = (pte & PAGING_PTE_SWAPPED_MASK);

  // ========== CASE 1: PAGE HIT - Already in RAM ==========
  if (is_present && !is_swapped) {
    *fpn = PAGING_FPN(pte);
    
    // [TLB ADDITION] Update TLB
    tlb_cache_write(caller->pid, pgn, *fpn);
    return 0;
  }

  // ========== CASE 2: PAGE FAULT - Need to load page ==========
  addr_t new_fpn;
  addr_t swpfpn = 0;
  int need_swap_in = is_swapped;
  
  if (need_swap_in) {
    printf("Page Fault Need Swap \n");
    swpfpn = PAGING_SWP(pte);  // Get swap location
  }

  // Try to allocate frame in RAM
  int ram_result = MEMPHY_get_freefp(caller->krnl->mram, &new_fpn);
  
  if (ram_result == 0) {
    // Got free frame from RAM
  } 
  else {
    // ========== RAM FULL - Need to SWAP OUT victim ==========
    int vicpgn;
    struct pcb_t *vic_owner;
    
    // Find victim page from global FIFO queue
    if (find_victim_page(caller->krnl->mm, &vicpgn, &vic_owner) < 0) {
      printf("[ERROR] Cannot find victim page for swapping\n");
      return -1;
    }
    
    printf("[SWAP OUT] PID %d needs frame. Victim: PID %d, PGN %d\n", 
           caller->pid, vic_owner->pid, vicpgn);
    
    // Get victim's PTE and frame number
    uint32_t vicpte = pte_get_entry(vic_owner, vicpgn);
    
    if (!(vicpte & PAGING_PTE_PRESENT_MASK)) {
      printf("[ERROR] Victim page not present!\n");
      return -1;
    }
    
    int vicfpn = PAGING_FPN(vicpte);

    // [TLB ADDITION] Xóa TLB của victim ngay lập tức vì frame sắp bị lấy mất
    tlb_clear_entry(vic_owner->pid, vicpgn);

    // Get free frame in SWAP device
    addr_t victim_swpfpn;
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &victim_swpfpn) < 0) {
      printf("[ERROR] SWAP device is also full!\n");
      return -1;
    }

    // Copy victim page: RAM -> SWAP
    __swap_cp_page(caller->krnl->mram, vicfpn, 
                   caller->krnl->active_mswp, victim_swpfpn);

    printf("[SWAP OUT] Copied RAM[%d] -> SWAP[%ld]\n", vicfpn, victim_swpfpn);

    // Update victim's PTE: mark as swapped
    pte_set_swap(vic_owner, vicpgn, 0, victim_swpfpn);

    // Reuse victim's frame
    new_fpn = vicfpn;
  }

  // ========== SWAP IN: Copy from SWAP to RAM (if needed) ==========
  if (need_swap_in && swpfpn != 0) {
    printf("[SWAP IN] PID %d, PGN %d: SWAP[%ld] -> RAM[%ld]\n", 
           caller->pid, pgn, swpfpn, new_fpn);
    
    // Copy data: SWAP -> RAM
    __swap_cp_page(caller->krnl->active_mswp, swpfpn, 
                   caller->krnl->mram, new_fpn);
    
    // Free the swap frame
    MEMPHY_put_freefp(caller->krnl->active_mswp, swpfpn);
  }

  // ========== Update PTE: Mark page as present in RAM ==========
  pte_set_fpn(caller, pgn, new_fpn);

  // Add to FIFO queue for future victim selection
  enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn, caller);

  // [TLB ADDITION] Cập nhật TLB cho trang mới
  tlb_cache_write(caller->pid, pgn, new_fpn);

  *fpn = new_fpn;
  return 0;
}

/*
 * pg_getval - Read a byte from virtual address
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller) 
{
  int pgn = PAGING64_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;
  
  // Ensure page is in RAM (may trigger swap)
  if (pg_getpage(mm, pgn, &fpn, caller) != 0) 
    return -1;
  
  // Calculate physical address
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
  
  // Read from physical memory via system call
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = phyaddr;
  regs.a3 = 0;
  syscall(caller->krnl, caller->pid, 17, &regs);
  
  *data = (BYTE)regs.a3;
  return 0;
}

/*
 * pg_setval - Write a byte to virtual address
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller) 
{
  int pgn = PAGING64_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  // Ensure page is in RAM (may trigger swap)
  if (pg_getpage(mm, pgn, &fpn, caller) != 0) 
    return -1;
  
  // Calculate physical address
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
  
  // Write to physical memory via system call
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = phyaddr;
  regs.a3 = (uint32_t)value;
  syscall(caller->krnl, caller->pid, 17, &regs);
  
  return 0;
}

/* ========================================================================= */
/* READ/WRITE OPERATIONS                                                     */
/* ========================================================================= */

/*
 * __read - Read from memory region
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data) 
{
  struct mm_struct *mm = caller->mm;
  pthread_mutex_lock(&caller->krnl->mm->mm_lock);
  
  struct vm_rg_struct *currg = get_symrg_byid(mm, rgid);
  
  // Validate region
  if (currg == NULL || (currg->rg_start == 0 && currg->rg_end == 0)) {
    pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
    return -1;
  }
  
  // Check bounds
  if (currg->rg_start + offset >= currg->rg_end) {
    pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
    return -1;
  }
  
  int ret = pg_getval(mm, currg->rg_start + offset, data, caller);
  pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
  return ret;
}

/*
 * __write - Write to memory region
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value) 
{
  struct mm_struct *mm = caller->mm;
  pthread_mutex_lock(&caller->krnl->mm->mm_lock);

  struct vm_rg_struct *currg = get_symrg_byid(mm, rgid);
  
  // Validate
  if (currg == NULL || (currg->rg_start == 0 && currg->rg_end == 0)) {
    printf("[ERROR] Write to unallocated register %d\n", rgid);
    pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
    return -1;
  }
  
  if (currg->rg_start + offset >= currg->rg_end) {
    printf("[ERROR] Segmentation fault at register %d\n", rgid);
    pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
    return -1;
  }
  
  pg_setval(mm, currg->rg_start + offset, value, caller);
  pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
  return 0;
}

/* ========================================================================= */
/* WRAPPER FUNCTIONS                                                         */
/* ========================================================================= */

int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index) 
{
  addr_t addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  proc->regs[reg_index] = addr;
  if (val == -1) return -1;

#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif
  return val;
}

int libfree(struct pcb_t *proc, uint32_t reg_index) 
{
  int val = __free(proc, 0, reg_index);
  if (val == -1) return -1;

#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif
  return 0;
}

int libread(struct pcb_t *proc, uint32_t source, addr_t offset, uint32_t *destination) 
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);
  *destination = data;
  //print_fifo_status(proc->krnl->mm);

  printf("\n");
  printf("[READ] PID: %d | Src: %d | Offset: %ld | Value: %d\n",
         proc->pid, source, offset, *destination);

  // [TLB ADDITION] Print stats
  print_tlb_stats();

#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif
  return val;
}

int libwrite(struct pcb_t *proc, BYTE data, uint32_t destination, addr_t offset) 
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1) return -1;
  //print_fifo_status(proc->krnl->mm);

  printf("[WRITE] PID: %d | Dst: %d | Offset: %ld | Value: %d\n",
         proc->pid, destination, offset, data);

  // [TLB ADDITION] Print stats
  print_tlb_stats();

#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
  MEMPHY_dump(proc->krnl->mram);
#endif
  return val;
}

/* ========================================================================= */
/* CLEANUP AND VICTIM SELECTION                                              */
/* ========================================================================= */

/*
 * free_pcb_memph - Free all memory of a process
 */
int free_pcb_memph(struct pcb_t *caller) 
{
  pthread_mutex_lock(&caller->krnl->mm->mm_lock);
  
  int pagenum;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING64_MAX_PGN; pagenum++) {
    pte = pte_get_entry(caller, pagenum);
    
    if (PAGING_PAGE_PRESENT(pte)) {
      int fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else if (PAGING_PTE_SWP(pte)) {
      addr_t swpfpn = PAGING_SWP(pte);
      if (swpfpn != 0) {
        MEMPHY_put_freefp(caller->krnl->active_mswp, swpfpn);
      }
    }
    
    // [TLB ADDITION] Clear from TLB
    tlb_clear_entry(caller->pid, pagenum);
  }
  
  pthread_mutex_unlock(&caller->krnl->mm->mm_lock);
  return 0;
}

/*
 * find_victim_page - FIFO page replacement
 * Select oldest page from global queue
 */
int find_victim_page(struct mm_struct *mm, int *retpgn, struct pcb_t **ret_owner)
{
  struct pgn_t *pg = mm->fifo_pgn;
  if (!pg) {
    printf("[ERROR] FIFO queue is empty!\n");
    return -1;
  }

  *retpgn = pg->pgn;
  *ret_owner = pg->owner;

  // Remove from list
  mm->fifo_pgn = pg->pg_next;
  free(pg);

  return 0;
}



/*
 * get_free_vmrg_area - Best-Fit strategy
 * Find smallest suitable free region
 */
void print_fifo_status(struct mm_struct *mm) {
    if (!mm) return;
    
    struct pgn_t *pg = mm->fifo_pgn;
    printf("   [FIFO QUEUE - HEAD]: ");
    
    if (!pg) {
        printf("EMPTY\n");
        return;
    }

    int count = 0;
    while (pg) {
        printf("%ld", pg->pgn);
        if (pg->owner) {
            printf("(PID:%d)", pg->owner->pid);
        }
        if (pg->pg_next) printf(" -> ");
        pg = pg->pg_next;
        count++;
        if (count > 20) { // Tránh in quá dài nếu lỗi vòng lặp
            printf(" ... (truncated)");
            break;
        }
    }
    printf("\n");
}



int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, 
                        struct vm_rg_struct *newrg) 
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  if (cur_vma == NULL) return -1;

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
  struct vm_rg_struct *prev = NULL;
  
  if (rgit == NULL) return -1;
  
  while (rgit != NULL) {
    long current_space = rgit->rg_end - rgit->rg_start;
    
    if (current_space >= size) {
      // Found suitable region
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;
      
      if (current_space > size) {
        // Shrink region
        rgit->rg_start += size;
      } else {
        // Remove region from list (exact fit)
        if (prev != NULL) 
          prev->rg_next = rgit->rg_next;
        else 
          cur_vma->vm_freerg_list = rgit->rg_next;
        free(rgit);
      }
      
      return 0;
    }
    
    prev = rgit;
    rgit = rgit->rg_next;
  }
  
  return -1;  // No suitable region found
}