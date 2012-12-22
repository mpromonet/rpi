OPENMAX=-I /opt/vc/include/ -I /opt/vc/include/interface/vcos/pthreads/ -L /opt/vc/lib/ -l bcm_host -l openmaxil
ILCLIENT=-I /opt/vc/src/hello_pi/libs/ilclient -L /opt/vc/src/hello_pi/libs/ilclient -lilclient

all: tvservice omx v4l2_h264

tvservice: tvservice.c
	gcc -o $@ $@.c $(OPENMAX)

omx: omx.c
	gcc -o $@ $@.c $(OPENMAX)

v4l2_h264: v4l2_h264.c
	gcc -o $@ $@.c $(OPENMAX) $(ILCLIENT)

clean:
	rm omx tvservice v4l2_h264
