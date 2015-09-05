GPIO
-------------
apt-get install python-pip
pip install RPi.GPIO
pip install SPIlib

Repository rpi
- SPI Interface ADC MCP3002 (/dev/spidev0.0)
- SPI Interface DAC MCP4802 (/dev/spidev0.1)
- I2C Interface to GPIO Expander MCP23008 to LCD ADM1602K (RS=22 E=17 D4=25 D5=24 D6=23 D7=18)

kmodule
-----------
- snd-pcf8591 : ALSA driver for I2C PCF8591 ADC
- spi-mcp3002 : ALSA driver for SPI MCP3002 ADC
