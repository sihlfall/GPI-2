C=gcc
CC=g++
CFLAGS=-std=c11 -pthread -Wall -pedantic -DDEBUG=1 -O3 -flto
CCFLAGS=-std=c++20 -Wall -pedantic -O3 -flto
LINKFLAGS=-pthread -lucp -lucs -flto
SRC=./src
SRC_GPI2UCX=../src/devices/ucx
SRC_STUB=./stub/src
BIN=./bin
INC=-I./stub/include -I../src/devices/ucx -I.
LIBS=/home/cluster/local/lib64/libGPI2.la
OBJ_NAMES_GPI2UCX=GPI2_UCX_ATOMIC.o GPI2_UCX_GRP.o GPI2_UCX_IO.o \
  GPI2_UCX_PASSIVE.o GPI2_UCX_SEG.o GPI2_UCX.o \
	ucx_device.o
OBJ_NAMES_STUB=GPI2.o GPI2_Utility.o

OBJS_GPI2UCX = $(addprefix $(BIN)/, $(OBJ_NAMES_GPI2UCX))
OBJS_STUB = $(addprefix $(BIN)/, $(OBJ_NAMES_STUB))

all: $(BIN) $(BIN)/test01 $(BIN)/test02 $(BIN)/test03 $(BIN)/queue_timing

$(BIN):
	mkdir -p bin

$(BIN)/queue_timing: $(BIN)/mpmc_queue.o $(BIN)/queue_timing.o $(BIN)/queue_timing_cpp_interop.o
	$(C) -o $@ $^ $(LINKFLAGS) -lstdc++

$(BIN)/test01: $(BIN)/test01.o $(OBJS_GPI2UCX) $(OBJS_STUB)
	$(C) -o $@ $^ $(LINKFLAGS) 

$(BIN)/test02: $(BIN)/test02.o $(OBJS_GPI2UCX) $(OBJS_STUB)
	$(C) -o $@ $^ $(LINKFLAGS)

$(BIN)/test03: $(BIN)/test03.o $(BIN)/mpmc_queue.o
	$(C) -o $@ $^ $(LINKFLAGS)

$(BIN)/queue_timing.o: $(SRC)/queue_timing.cc
	$(CC) $(CCFLAGS) $< -c -o $@ $(INC)

$(BIN)/mpmc_queue.o: $(SRC_GPI2UCX)/mpmc_queue.c
	$(C) $(CFLAGS) -mcx16 -O3 $< -c -o $@ $(INC)

$(BIN)/%.o: $(SRC_GPI2UCX)/%.c Makefile
	$(C) $(CFLAGS) $< -c -o $@ $(INC)

$(BIN)/%.o: $(SRC)/%.c Makefile
	$(C) $(CFLAGS) $< -c -o $@ $(INC)

$(BIN)/%.o: $(SRC_STUB)/%.c Makefile
	$(C) $(CFLAGS) $< -c -o $@ $(INC)

.PHONY clean:
	rm bin/*