/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#ifndef OSMM_H
#define OSMM_H

#include <stdint.h>
#include <pthread.h>

#define MM_PAGING
#define PAGING_MAX_MMSWP 4 /* max number of supported swapped space */
#define PAGING_MAX_SYMTBL_SZ 30

/* * Forward declaration for PCB to avoid circular dependency 
 * (Khai báo trước cấu trúc pcb_t để dùng trong pgn_t)
 */
struct pcb_t; 

/* * @bksysnet: in long address mode of 64bit or original 32bit
 * the address type need to be redefined
 */

#ifdef MM64
#define ADDR_TYPE uint64_t
#else
#define ADDR_TYPE uint32_t
#endif

typedef char BYTE;
typedef ADDR_TYPE addr_t;
//typedef unsigned int uint32_t;


/* * @bksysnet: the format string need to be redefined
 * based on the address mode
 */
#ifdef MM64
#define FORMAT_ADDR "%ld"
#define FORMATX_ADDR "%16lx"
#else
#define FORMAT_ADDR "%d"
#define FORMATX_ADDR "%08x"
#endif

/* * [FIXED] Added owner field for Global Replacement Algorithm 
 */
struct pgn_t{
   addr_t pgn;
   struct pgn_t *pg_next; 
   struct pcb_t *owner; // <--- THÊM DÒNG NÀY (Lưu chủ sở hữu trang)
};

/*
 * Memory region struct
 */
struct vm_rg_struct {
   addr_t rg_start;
   addr_t rg_end;

   struct vm_rg_struct *rg_next;
   pthread_mutex_t mm_lock; // Move mutex inside struct definition properly

};

/*
 * Memory area struct
 */
struct vm_area_struct {
   unsigned long vm_id;
   addr_t vm_start;
   addr_t vm_end;

   addr_t sbrk;
/*
 * Derived field
 * unsigned long vm_limit = vm_end - vm_start
 */
   struct mm_struct *vm_mm;
   struct vm_rg_struct *vm_freerg_list;
   struct vm_area_struct *vm_next;
   pthread_mutex_t mm_lock;

};

/* * Memory management struct
 */
struct mm_struct {
 /* TODO: The structure of page diractory need to be justify
  * as your design. The single point is draft to avoid
  * compiler noisy only, this design need to be revised
  */
#ifdef MM64
   addr_t *pgd;
   addr_t *p4d;
   addr_t *pud;
   addr_t *pmd;
   addr_t *pt;
#else
   uint32_t *pgd;
#endif

   struct vm_area_struct *mmap;

   /* Currently we support a fixed number of symbol */
   struct vm_rg_struct symrgtbl[PAGING_MAX_SYMTBL_SZ];

   /* list of free page */
   struct pgn_t *fifo_pgn;
   pthread_mutex_t mm_lock;

};

/*
 * FRAME/MEM PHY struct
 */
struct framephy_struct { 
   addr_t fpn;
   struct framephy_struct *fp_next;

   /* Resereed for tracking allocated framed */
   struct mm_struct* owner;
   pthread_mutex_t mm_lock;

};

struct memphy_struct {
   /* Basic field of data and size */
   BYTE *storage;
   int maxsz;
   
   /* Sequential device fields */ 
   int rdmflg;
   int cursor;

   /* Management structure */
   struct framephy_struct *free_fp_list;
   struct framephy_struct *used_fp_list;
   pthread_mutex_t memphy_lock;
   pthread_mutex_t mm_lock;


};

#endif