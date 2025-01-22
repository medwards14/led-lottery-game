obj-m += led_lottery.o

all: module dt
	@echo "Built LED Lottery kernel module and overlay"

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

dt: led_lottery_overlay.dts
	dtc -@ -I dts -O dtb -o led_lottery.dtbo led_lottery_overlay.dts

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f led_lottery.dtbo
