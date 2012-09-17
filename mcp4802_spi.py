#!/usr/bin/python
# SPI Interface to DAC MPC4802

import spi
import time

def writeDac(device,chan,val):
    v1 = 0x30 | (chan<<7) | (val>>4);
    v2 = ((val & 0xF) << 4);

    #transfers data string
    b = device.xfer([v1,v2],1000)
    
    return b;

def main():
    #open the SPI device /dev/spidevX.Y
    device = spi.SPI(0,1)

    # send DAC value
    for cnt in range(100):
        for chan in range(2):
            for i in range(255):
                writeDac(device,chan,i);
                time.sleep(0.01)
        
    #close SPI device
    device.close()

if __name__ == '__main__':
    main()





