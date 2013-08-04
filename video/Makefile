TARGETS=$(foreach file,$(basename $(wildcard *.c)),$(file))
LIVE555_CFLAGS=-I /usr/include/UsageEnvironment -I /usr/include/groupsock -I/usr/include/liveMedia -I/usr/include/BasicUsageEnvironment
LIVE555_FDFLAGS=-lliveMedia -lgroupsock  -lBasicUsageEnvironment -lUsageEnvironment 

all: $(TARGETS)

%: %.c
	g++ -g -o $@ $< $(LIVE555_CFLAGS) $(LIVE555_FDFLAGS) -lv4l2 

clean:
	rm -f $(TARGETS)

help:
	@echo Needed packages
	@echo apt-get install libv4l-dev liblivemedia-dev
