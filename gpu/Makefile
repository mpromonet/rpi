OPENMAX=-I /opt/vc/include/ -I /opt/vc/include/interface/vcos/pthreads/ -L /opt/vc/lib/ -l bcm_host -l openmaxil
ILCLIENT=-I /opt/vc/src/hello_pi/libs/ilclient -L /opt/vc/src/hello_pi/libs/ilclient -lilclient

TARGETS=$(foreach file,$(basename $(wildcard *.c)),$(file))

all: $(TARGETS)

%: %.c
	gcc -g -o $@ $< $(OPENMAX) $(ILCLIENT)

clean:
	rm -f $(TARGETS)
