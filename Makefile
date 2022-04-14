CC := gcc
CFLAGS := -Wall -g -std=gnu99
LDFLAGS := -L/opt/xilinx/ffmpeg/lib -L/opt/xilinx/xrt/lib -L/opt/xilinx/xvbm/lib -L/opt/xilinx/xrm/lib
LIBS := -lavdevice -lavformat -lavcodec -lavfilter -lswresample -lswscale -lavutil -lxvbm -lxma2api -lxrt_core -lxrt_coreutil -ldl -lm -lpthread -lstdc++ -lxrm -lxma2plugin -lxrt_core -lxrt_coreutil -ldl -lm -lpthread -lstdc++

# importent: the xma have two set api, we are using the xma2 ,  so must put the -I/opt/xilinx/xrt/include/xma2 in the first one in the INCLUDE configuration
INCLUDE := -I/opt/xilinx/ffmpeg/include -I/opt/xilinx/xrt/include/xma2  -I/opt/xilinx/xrt/include -I/opt/xilinx/xvbm/include -I/opt/xilinx/xrm/include 

# export LD_LIBRARY_PATH=/opt/xilinx/xrt/lib/
# export LD_LIBRARY_PATH=/opt/xilinx/ffmpeg/lib/

TARGET = ffmpeg_u30_decode

SRC :=  test_decoder3.c
OBJ := $(SRC:%.c=./%.o)

.PHONY: all clean

all: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) $(LIBS) -o ./$(TARGET)
	@echo "!!![build done]"

./%.o:%.c
	$(CC) -c -o $@ $(CFLAGS) $(INCLUDE) $< 

clean:
	rm *.o
	rm $(TARGET)
