#!/bin/bash

case $1 in
  start)
    echo "start gpio-mcp23008"
    insmod /lib/modules/$(uname -r)/extra/gpio-mcp23008.ko p_base=128
    if [ ! -d /sys/class/i2c-dev/i2c-0/device/0-0020/ ]
    then
      echo "New I2C device MCP23008"
      echo "mcp23008 0x20" > /sys/class/i2c-dev/i2c-0/device/new_device
    fi
    if [ -d /sys/class/gpio/gpiochip128 ]
    then
      echo "GPIO interface OK"
    else
      echo "GPIO interface failed"
    fi
    ;;
  stop)
    echo "stop gpio-mcp23008"
    if [ -d /sys/class/i2c-dev/i2c-0/device/0-0020/ ]
    then
      find  /sys/class/i2c-dev/i2c-0/device/0-0020/gpio -name "gpio[0-9]*" -print | sed 's/.*gpio//' | while read gpio
      do
         echo "Unexport GPIO $gpio"
         echo $gpio > /sys/class/gpio/unexport
      done
      echo "Delete I2C device MCP23008"
      echo "0x20" > /sys/class/i2c-dev/i2c-0/device/delete_device
    fi
    rmmod gpio-mcp23008.ko
    ;;
  *)
    echo "usage: $0 {start|stop}"
esac

