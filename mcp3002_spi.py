#!/usr/bin/python
# SPI Interface to ADC MPC3002

from spi import spi_transfer, SPIDev

def readAdc(device,chan):
    #This is my data that I want sent through my SPI bus
    v1 = 0xD0 | (chan<<5) ;
    v2 = 0;
    data = "%0.2X" % v1 + "%0.2X" % v2

    #transfers data string
    transfer, buf, _ = spi_transfer(bytes.fromhex(data), readlen=2)
    print(transfer)
    b=device.do_transfers(transfer)
    
    # decode value
    answer=list(buf)
    print(int.from_bytes(answer, byteorder='big'))
    value= ( (answer[0]*128) | (answer[1]/2) )
    value= value & 0x3ff
    
    return value;

def main():
    #open the SPI device /dev/spidevX.Y
    device = SPIDev('/dev/spidev0.0')

    # read ADC
    for chan in range(2):
        value = readAdc(device, chan);
        print("channel:%d" % chan + " value:%d" % value + " voltage:%f V" % (value * 3.3 / 1023) )
        
     #close SPI device
    device.close()

if __name__ == '__main__':
    main()







