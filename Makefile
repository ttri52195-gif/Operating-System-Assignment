INC = -Iinclude
LIB = -lpthread

SRC = src
OBJ = obj
INCLUDE = include

CC = gcc
DEBUG = -g

# [QUAN TRỌNG] Thêm -DMM64 để kích hoạt code trong mm64.c
# [QUAN TRỌNG] Thêm -DMLQ_SCHED để kích hoạt lập lịch ưu tiên trong sched.c
CFLAGS = -Wall -c $(DEBUG) -DMM64 -DMLQ_SCHED
LFLAGS = -Wall $(DEBUG)

vpath %.c $(SRC)
vpath %.h $(INCLUDE)

MAKE_CMD = $(CC) $(INC) 

# Object files
MEM_OBJ = $(addprefix $(OBJ)/, paging.o mem.o cpu.o loader.o)
SYSCALL_OBJ = $(addprefix $(OBJ)/, syscall.o sys_mem.o sys_listsyscall.o sys_xxxhandler.o)

# Danh sách các file object cần biên dịch
# Lưu ý: Cả mm.o và mm64.o đều được liệt kê, nhưng nhờ cờ -DMM64:
# - mm.c sẽ bị vô hiệu hóa (do #if !defined(MM64))
# - mm64.c sẽ được kích hoạt (do #if defined(MM64))
OS_OBJ = $(addprefix $(OBJ)/, cpu.o mem.o loader.o queue.o os.o sched.o timer.o mm-vm.o mm64.o mm.o mm-memphy.o libstd.o libmem.o)
OS_OBJ += $(SYSCALL_OBJ)

SCHED_OBJ = $(addprefix $(OBJ)/, cpu.o loader.o)
HEADER = $(wildcard $(INCLUDE)/*.h)

all: os

# Compile memory management modules
mem: $(MEM_OBJ)
	$(MAKE_CMD) $(LFLAGS) $(MEM_OBJ) -o mem $(LIB)

# Compile scheduler
sched: $(SCHED_OBJ)
	$(MAKE_CMD) $(LFLAGS) $(MEM_OBJ) -o sched $(LIB)

# Compile syscall table generation script
syscalltbl.lst: $(SRC)/syscall.tbl
	@echo $(OS_OBJ)
	chmod +x $(SRC)/syscalltbl.sh
	$(SRC)/syscalltbl.sh $< $(SRC)/$@ 

# Compile the whole OS simulation
os: $(OBJ) syscalltbl.lst $(OS_OBJ)
	$(MAKE_CMD) $(LFLAGS) $(OS_OBJ) -o os $(LIB)

# Rule: Compile .c to .o
$(OBJ)/%.o: %.c ${HEADER} $(OBJ)
	$(MAKE_CMD) $(CFLAGS) $< -o $@

# Prepare object directory
$(OBJ):
	mkdir -p $(OBJ)

# Clean build artifacts
clean:
	rm -f $(SRC)/*.lst
	rm -f $(OBJ)/*.o os sched mem pdg
	rm -rf $(OBJ)