#!/usr/bin/python
# SPI Interface to DAC MPC4802

from spi import spi_transfer, SPIDev
import time

def writeDac(device,chan,val):
    v1 = 0x30 | (chan<<7) | (val>>4);
    v2 = ((val & 0xF) << 4);
    data = "%0.2X" % v1 + "%0.2X" % v2

    #transfers data string
    transfer, buf, _ = spi_transfer(bytes.fromhex(data), readlen=0)
    b=device.do_transfers(transfer)
    
    return b;

def main():
    #open the SPI device /dev/spidevX.Y
    device = SPIDev('/dev/spidev0.1')

    # send DAC value
    for cnt in range(100):
        for chan in range(2):
            for i in range(255):
                writeDac(device,chan,i);
                time.sleep(0.01)
        
    #close SPI device
    device._file.close()

if __name__ == '__main__':
    main()





