/* loader.c */
#include "../include/loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/mm.h" // [QUAN TRỌNG] Cần để dùng struct mm_struct và init_mm

static uint32_t avail_pid = 1;

#define OPT_CALC    "calc"
#define OPT_ALLOC   "alloc"
#define OPT_FREE    "free"
#define OPT_READ    "read"
#define OPT_WRITE   "write"
#define OPT_SYSCALL "syscall"

static enum ins_opcode_t get_opcode(char * opt) {
    if (!strcmp(opt, OPT_CALC)) {
        return CALC;
    }else if (!strcmp(opt, OPT_ALLOC)) {
        return ALLOC;
    }else if (!strcmp(opt, OPT_FREE)) {
        return FREE;
    }else if (!strcmp(opt, OPT_READ)) {
        return READ;
    }else if (!strcmp(opt, OPT_WRITE)) {
        return WRITE;
    }else if (!strcmp(opt, OPT_SYSCALL)) {
        return SYSCALL;
    }else{
        printf("Opcode: %s\n", opt);
        exit(1);
    }
}

struct pcb_t * load(const char * path) {
    /* Create new PCB for the new process */
    struct pcb_t * proc = (struct pcb_t * )malloc(sizeof(struct pcb_t));
    proc->pid = avail_pid;
    avail_pid++;
    proc->page_table = (struct page_table_t*)malloc(sizeof(struct page_table_t));
    proc->bp = PAGE_SIZE;
    proc->pc = 0;

    // [FIX 1] Cấp phát và Khởi tạo MM riêng cho Process
    // Đây là bước quan trọng nhất để tránh Segmentation Fault ở liballoc
    proc->mm = (struct mm_struct *)calloc(1, sizeof(struct mm_struct));
    init_mm(proc->mm, proc); // Khởi tạo các cấu trúc (VMA, PGD...)

    /* Read process code from file */
    FILE * file;
    if ((file = fopen(path, "r")) == NULL) {
        printf("Cannot find process description at '%s'\n", path);
        exit(1);        
    }

    snprintf(proc->path, 2*sizeof(path)+1, "%s", path);
    char opcode[10];
    proc->code = (struct code_seg_t*)malloc(sizeof(struct code_seg_t));

    fscanf(file,"%u %u", &proc->priority, &proc->code->size);

    // Dùng calloc để đảm bảo vùng nhớ sạch
    proc->code->text = (struct inst_t*)calloc(proc->code->size, sizeof(struct inst_t));
    
    uint32_t i = 0;
    char buf[200];
    
    // [FIX 2] Vòng lặp an toàn hơn: Kiểm tra EOF để tránh lặp vô hạn nếu file bị lỗi
    for (i = 0; i < proc->code->size; i++) {
        opcode[0] = '\0';
        // Nếu không đọc được opcode (hết file), thoát vòng lặp ngay
        if (fscanf(file, "%s", opcode) != 1) {
            proc->code->size = i; // Cập nhật lại size thực tế đọc được
            break;
        }

        proc->code->text[i].opcode = get_opcode(opcode);
        
        switch(proc->code->text[i].opcode) {
        case CALC:
            break;
        case ALLOC:
            fscanf(
                file,
                "" FORMAT_ARG " " FORMAT_ARG "\n",
                &proc->code->text[i].arg_0,
                &proc->code->text[i].arg_1
            );
            break;
        case FREE:
            fscanf(file, "" FORMAT_ARG "\n", &proc->code->text[i].arg_0);
            break;
        case READ:
        case WRITE:
            fscanf(
                file,
                "" FORMAT_ARG " " FORMAT_ARG " " FORMAT_ARG "\n",
                &proc->code->text[i].arg_0,
                &proc->code->text[i].arg_1,
                &proc->code->text[i].arg_2
            );
            break;  
        case SYSCALL:
            fgets(buf, sizeof(buf), file);
            sscanf(buf, "" FORMAT_ARG "" FORMAT_ARG "" FORMAT_ARG "" FORMAT_ARG "",
                       &proc->code->text[i].arg_0,
                       &proc->code->text[i].arg_1,
                       &proc->code->text[i].arg_2,
                       &proc->code->text[i].arg_3
            );
            break;
        default:
            printf("Opcode: %s\n", opcode);
            exit(1);
        }
    }
    
    fclose(file);
    return proc;
}