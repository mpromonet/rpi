#!/bin/bash

case $1 in
start)
	sudo insmod i2c.ko
	sudo insmod pcf8591.ko
	echo "pcf8591 0x48" | sudo tee /sys/bus/i2c/devices/i2c-0/new_device
	;;
stop)
	echo 0x48  | sudo tee /sys/bus/i2c/devices/i2c-0/delete_device
	sudo rmmod pcf8591.ko
	;;
*)
	echo "Usage $0 (start|stop)"
	;;
esac
