#!/usr/bin/python
import smbus
import time
 
bus = smbus.SMBus(0)
 
# configure GPIO all outputs
bus.write_i2c_block_data(0x20,0x00,[0x00])

# set all bits to 1
while 1:
	bus.write_i2c_block_data(0x20,0x09,[0x55])
	time.sleep(0.25)
	bus.write_i2c_block_data(0x20,0x09,[0xAA])
	time.sleep(0.25)
