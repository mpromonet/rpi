#!/usr/bin/python

#Python example for SPI bus, written by Brian Hensley
#This script will take any amount of Hex values and determine
#the length and then transfer the data as a string to the "spi" module

import spi
import time

def writeDac(device,chan,val):
    v1 = 0x30 | (chan<<7) | (val>>4);
    v2 = ((val & 0xF) << 4);
    data = "%0.2X" % v1 + "%0.2X" % v2

    #transfers data string
    b = device.transfer(data, len(data)/2)
    
    return b;

def main():
    #open the SPI device /dev/spidevX.Y
    device = spi.SPI(0,1)

    # send DAC value
    for cnt in range(10):
        for chan in range(2):
            for i in range(255):
                writeDac(device,chan,i);
                time.sleep(0.01)
        
    #close SPI device
    device.close()

if __name__ == '__main__':
    main()





