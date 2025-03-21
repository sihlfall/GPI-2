C=gcc
CFLAGS=-std=c99 -pthread -Wall -pedantic
SRC=./src
SRC_GPI2UCX=../src/devices/ucx
SRC_STUB=./stub/src
BIN=./bin
INC=-I./stub/include -I../src/devices/ucx -I.
LIBS=/home/cluster/local/lib64/libGPI2.la
OBJ_NAMES_GPI2UCX=GPI2_UCX_ATOMIC.o GPI2_UCX_GRP.o GPI2_UCX_IO.o \
  GPI2_UCX_PASSIVE.o GPI2_UCX_SEG.o GPI2_UCX.o oob.o \
	ucx_device.o
OBJ_NAMES_STUB=GPI2.o GPI2_Utility.o

OBJS_GPI2UCX = $(addprefix $(BIN)/, $(OBJ_NAMES_GPI2UCX))
OBJS_STUB = $(addprefix $(BIN)/, $(OBJ_NAMES_STUB))

all: $(BIN) $(BIN)/test01

$(BIN):
	mkdir -p bin

#$(BIN)/server: $(BIN)/server.o $(BIN)/clientserver.o
#	$(C) $(CFLAGS) -o $@ $^ -lucp
#
#$(BIN)/client: $(BIN)/client.o $(BIN)/clientserver.o
#	$(C) $(CFLAGS) -o $@ $^ -lucp

$(BIN)/test01: $(BIN)/test01.o $(OBJS_GPI2UCX) $(OBJS_STUB)
	$(C) $(CFLAGS) -o $@ $^ -lucp -pthread

$(BIN)/%.o: $(SRC_GPI2UCX)/%.c Makefile
	$(C) $(CFLAGS) $< -c -o $@ -lucp -pthread $(INC)

$(BIN)/%.o: $(SRC)/%.c Makefile
	$(C) $(CFLAGS) $< -c -o $@ -lucp -pthread $(INC)

$(BIN)/%.o: $(SRC_STUB)/%.c Makefile
	$(C) $(CFLAGS) $< -c -o $@ -lucp -pthread $(INC)

.PHONY clean:
	rm bin/*