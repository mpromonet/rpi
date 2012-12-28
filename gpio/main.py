#!/usr/bin/python

from spi import spi_transfer, SPIDev
import time

import adm1602k_gpio
import mcp3002_spi

def main():
    # Initialise display
    adm1602k_gpio.lcd_init()

    #open the SPI device /dev/spidevX.Y
    device = SPIDev('/dev/spidev0.0')

    # read ADC and display on LCD
    for chan in range(50):
        value = mcp3002_spi.readAdc(device, 0);
        output = "%.04d=" % value + "%f V" % (value * 3.3 / 1023);
        adm1602k_gpio.lcd_string(adm1602k_gpio.LCD_LINE_1,output)

        value = mcp3002_spi.readAdc(device, 1);
        output = "%.04d=" % value + "%f V" % (value * 3.3 / 1023);
        adm1602k_gpio.lcd_string(adm1602k_gpio.LCD_LINE_2,output)

        time.sleep(1)
      
    #close SPI device
    device._file.close();
    
    adm1602k_gpio.lcd_clear();

if __name__ == '__main__':
    main()
