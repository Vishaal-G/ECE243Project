INSTALL	:= C:/intelFPGA/QUARTUS_Lite_V23.1

MAIN	:= main.c
HDRS	:= address_map.h siren_data.h
SRCS	:= $(MAIN)

SHELL	:= cmd.exe

# DE1-SoC
JTAG_INDEX_SoC	:= 2

# Toolchain paths
COMPILER		:= $(INSTALL)/fpgacademy/AMP/cygwin64/home/compiler/bin
BASH			:= $(INSTALL)/fpgacademy/AMP/cygwin64/bin/bash --noprofile -norc -c 
HW_DE1-SoC		:= "$(INSTALL)/fpgacademy/Computer_Systems/DE1-SoC/DE1-SoC_Computer/niosVg/DE1_SoC_Computer.sof"
HW_DE10-Lite	:= "$(INSTALL)/fpgacademy/Computer_Systems/DE10-Lite/DE10-Lite_Computer/niosVg/DE10_Lite_Computer.sof"

# PATH setup
export PATH := $(INSTALL)/quartus/bin64/:$(PATH)
export PATH := $(INSTALL)/qprogrammer/quartus/bin64/:$(PATH)
export PATH := $(INSTALL)/riscfree/debugger/gdbserver-riscv/:$(PATH)
export PATH := $(INSTALL)/riscfree/toolchain/riscv32-unknown-elf/bin/:$(PATH)
export PATH := $(INSTALL)/fpgacademy/AMP/bin/:$(PATH)
export PATH := $(INSTALL)/quartus/sopc_builder/bin/:$(PATH)

CYGWIN_INSTALL := $(shell $(BASH) 'export PATH=/usr/local/bin:/usr/bin; cygpath $(INSTALL)')
CYGWIN_PATH := export PATH=/usr/local/bin:/usr/bin:$(CYGWIN_INSTALL)/fpgacademy/AMP/bin

# Programs
CC	:= $(COMPILER)/riscv32-unknown-elf-gcc.exe
LD	:= $(CC)
OD	:= $(COMPILER)/riscv32-unknown-elf-objdump.exe
NM	:= $(COMPILER)/riscv32-unknown-elf-nm.exe
RM	:= /usr/bin/rm -f

# Flags
USERCCFLAGS	:= -g -O1 -ffunction-sections -fverbose-asm -fno-inline -gdwarf-2 
USERLDFLAGS := -Wl,--defsym=__stack_pointer$$=0x4000000 -Wl,--defsym=JTAG_UART_BASE=0xff201000
ARCHCCFLAGS	:= -march=rv32im_zicsr -mabi=ilp32
ARCHLDFLAGS	:= -march=rv32im_zicsr -mabi=ilp32
CCFLAGS		:= -Wall -c $(USERCCFLAGS) $(ARCHCCFLAGS)
LDFLAGS		:= $(USERLDFLAGS) $(ARCHLDFLAGS)

# Files
OBJS		:= $(patsubst %, %.o, $(SRCS))

############################################
# Build Targets

COMPILE: $(basename $(MAIN)).elf

$(basename $(MAIN)).elf: $(OBJS)
	@$(BASH) 'cd "$(CURDIR)"; $(RM) $@'
	$(CYAN_TEXT)
	@echo Linking
	@$(BASH) 'printf "$(LD) "'
	$(DEF_TEXT)
	@echo $(LDFLAGS) $(OBJS) -lm -o $@
	@$(BASH) 'printf "\n"'
	@$(BASH) 'cd "$(CURDIR)"; $(CYGWIN_PATH); $(LD) $(LDFLAGS) $(OBJS) -lm -o $@'

%.c.o: %.c $(HDRS)
	@$(BASH) 'cd "$(CURDIR)"; $(RM) $@'
	$(GREEN_TEXT)
	@echo Compiling
	@$(BASH) 'printf "$(CC) "'
	$(DEF_TEXT)
	@echo $(CCFLAGS) $< -o $@
	@$(BASH) 'cd "$(CURDIR)"; $(CYGWIN_PATH); $(CC) $(CCFLAGS) $< -o $@'

SYMBOLS: $(basename $(MAIN)).elf
	@echo $(NM) -p $<
	@$(BASH) 'cd "$(CURDIR)"; $(CYGWIN_PATH); $(NM) -p $<'

OBJDUMP: $(basename $(MAIN)).elf
	@echo $(OD) -d -S $<
	@$(BASH) 'cd "$(CURDIR)"; $(CYGWIN_PATH); $(OD) -d -S $<'

CLEAN: 
	@$(BASH) 'printf "\033[31m"'
	@echo Removing build files
	@$(BASH) 'printf "\033[0m"'
	@$(BASH) 'cd "$(CURDIR)"; $(RM) $(basename $(MAIN)).elf $(OBJS)'

############################################
# System Targets

QP_PROGRAMMER	:= quartus_pgm.exe

SYS_FLAG_CABLE_Lite	:= -c "USB-Blaster [USB-0]"
SYS_FLAG_CABLE_SoC 	:= -c "DE-SoC [USB-1]"

JTAG_INDEX_Lite	:= 1

DETECT_DEVICES:
	$(QP_PROGRAMMER) $(SYS_FLAG_CABLE_SoC) --auto

DE1-SoC:
	$(QP_PROGRAMMER) $(SYS_FLAG_CABLE_SoC) -m jtag -o "P;$(HW_DE1-SoC)@$(JTAG_INDEX_SoC)"

DE10-Lite:
	$(QP_PROGRAMMER) $(SYS_FLAG_CABLE_Lite) -m jtag -o "P;$(HW_DE10-Lite)@$(JTAG_INDEX_Lite)"

TERMINAL:
	nios2-terminal.exe --instance 0

############################################
# GDB Targets

GDB_SERVER: 
	ash-riscv-gdb-server.exe --device 02D120DD --gdb-port 2454 --instance 1 --probe-type USB-Blaster-2 --transport-type jtag --auto-detect true

GDB_CLIENT: 
	riscv32-unknown-elf-gdb.exe -silent -ex "target remote:2454" -ex "set $$mstatus=0" -ex "set $$mtvec=0" -ex "load" -ex "set $$pc=_start" -ex "info reg pc" "$(basename $(MAIN)).elf"

############################################

.SILENT: SYMBOLS OBJDUMP
