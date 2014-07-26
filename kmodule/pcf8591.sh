#!/bin/bash

case $1 in
start)
	sudo modprobe -v pcf8591
	echo "pcf8591 0x48" | sudo tee /sys/bus/i2c/devices/i2c-0/new_device
	;;
stop)
	echo 0x48  | sudo tee /sys/bus/i2c/devices/i2c-0/delete_device
	sudo modprobe -rv pcf8591
	;;
*)
	echo "Usage $0 (start|stop)"
	;;
esac
