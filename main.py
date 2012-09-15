#!/usr/bin/python

#import
import spi
import time

import adm1602k_gpio
import mpc3002_spi

def main():
    # Initialise display
    adm1602k_gpio.lcd_init()

    #open the SPI device /dev/spidevX.Y
    device = spi.SPI(0,0)

    # read ADC and display on LCD
    for chan in range(50):
        value = mpc3002_spi.readAdc(device, 0);
        output = "%.04d=" % value + "%f V" % (value * 3.3 / 1023);
        adm1602k_gpio.lcd_byte(adm1602k_gpio.LCD_LINE_1, adm1602k_gpio.LCD_CMD)
        adm1602k_gpio.lcd_string(output)

        value = mpc3002_spi.readAdc(device, 1);
        output = "%.04d=" % value + "%f V" % (value * 3.3 / 1023);
        adm1602k_gpio.lcd_byte(adm1602k_gpio.LCD_LINE_2, adm1602k_gpio.LCD_CMD)
        adm1602k_gpio.lcd_string(output)

        time.sleep(1)
      
    #close SPI device
    device.close();

if __name__ == '__main__':
    main()
