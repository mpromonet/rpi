CFLAGS = -W -Wall -pthread -g -pipe $(CFLAGS_EXTRA)
RM = rm -rf
ALL_PROGS = v4l2web

CC = g++
CFLAGS += -lssl -g -O0 
CFLAGS += -I /usr/include/jsoncpp 
CFLAGS += -I mongoose
CFLAGS += -I h264_v4l2_rtspserver/inc
LDFLAGS += -ljsoncpp -lv4l2

all: $(ALL_PROGS)

v4l2web: v4l2web.c mongoose/mongoose.c h264_v4l2_rtspserver/src/V4l2Device.cpp h264_v4l2_rtspserver/src/V4l2MMAPDeviceSource.cpp
	$(CC) -o $@ $(CFLAGS) $^ $(LDFLAGS)

clean:
	-@$(RM) $(ALL_PROGS) *.exe *.obj *.exp .*o *.lib 
