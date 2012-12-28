#!/usr/bin/python
#
# ST7066 LCD Test Script using sysfs
#
#

# The wiring for the LCD is as follows:
# 1 : GND
# 2 : 3.3V
# 3 : Contrast (0-5V)*
# 4 : RS (Register Select)
# 5 : R/W (Read Write)       - GROUND THIS PIN
# 6 : Enable or Strobe
# 7 : Data Bit 0             - NOT USED
# 8 : Data Bit 1             - NOT USED
# 9 : Data Bit 2             - NOT USED
# 10: Data Bit 3             - NOT USED
# 11: Data Bit 4
# 12: Data Bit 5
# 13: Data Bit 6
# 14: Data Bit 7
# 15: LCD Backlight +5V**
# 16: LCD Backlight GND

#import
import time
import datetime

# Define GPIO to LCD mapping
LCD_RS = 132
LCD_E  = 134
LCD_D4 = 128 
LCD_D5 = 129
LCD_D6 = 130
LCD_D7 = 131

# Define some device constants
LCD_WIDTH = 16    # Maximum characters per line
LCD_CHR = True
LCD_CMD = False

LCD_LINE_1 = 0x80 # LCD RAM address for the 1st line
LCD_LINE_2 = 0xC0 # LCD RAM address for the 2nd line 

# Timing constants
E_PULSE = 0.0005
E_DELAY = 0.0005

def main():
  # Initialise display
  lcd_init()

  # Send some text
  lcd_string(LCD_LINE_1,"Raspberrypi")
  for cnt in range(20):
    lcd_string(LCD_LINE_2,datetime.datetime.now().strftime("%H:%M:%S.%f"))
    time.sleep(0.1)


def gpio_init(pin,mode):
  fd = open("/sys/class/gpio/export","w")
  try:
    fd.write(str(pin))
    fd.close()
  except:
    pass
  try:
    fd = open("/sys/class/gpio/gpio"+str(pin)+"/direction","w")
    fd.write(mode)
    fd.close()
  except:
    pass

def gpio_write(pin,value):
  fd = open("/sys/class/gpio/gpio"+str(pin)+"/value","w")
  val="0"
  if value == True:
    val="1"
  fd.write(val)
  fd.close()

def lcd_init():
  # Initialise GPIO
  gpio_init(LCD_E, "out")  # E
  gpio_init(LCD_RS, "out") # RS
  gpio_init(LCD_D4, "out") # DB4
  gpio_init(LCD_D5, "out") # DB5
  gpio_init(LCD_D6, "out") # DB6
  gpio_init(LCD_D7, "out") # DB7

  
  # Initialise display using 4bits bus mode
  lcd_byte(0x33,LCD_CMD)
  lcd_byte(0x32,LCD_CMD)
  # function set
  lcd_byte(0x28,LCD_CMD)  
  # display on/off
  lcd_byte(0x0C,LCD_CMD)  
  # clear
  lcd_clear()  
  # left to right
  lcd_byte(0x06,LCD_CMD)

def lcd_string(line,message):
  # select line
  lcd_byte(line,LCD_CMD)
  
  # Send string to display
  message = message.ljust(LCD_WIDTH," ")  

  for i in range(LCD_WIDTH):
    lcd_byte(ord(message[i]),LCD_CHR)


def lcd_clear():
  lcd_byte(0x01,LCD_CMD)  

def lcd_byte(bits, mode):
  # Send byte to data pins
  # bits = data
  # mode = True  for character
  #        False for command

  gpio_write(LCD_RS, mode) # RS

  # High bits
  lcd_4bits(bits>>4)  
  # Low bits
  lcd_4bits(bits&0xf)

def lcd_4bits(bits):
  bits=bits&0xf
  
  # set gpio values
  gpio_write(LCD_D4, False)
  gpio_write(LCD_D5, False)
  gpio_write(LCD_D6, False)
  gpio_write(LCD_D7, False)
  if bits&0x01==0x01:
    gpio_write(LCD_D4, True)
  if bits&0x02==0x02:
    gpio_write(LCD_D5, True)
  if bits&0x04==0x04:
    gpio_write(LCD_D6, True)
  if bits&0x08==0x08:
    gpio_write(LCD_D7, True)

  # Toggle 'Enable' pin
  time.sleep(E_DELAY)    
  gpio_write(LCD_E, True)  
  time.sleep(E_PULSE)
  gpio_write(LCD_E, False)  
  time.sleep(E_DELAY)   

if __name__ == '__main__':
  main()
