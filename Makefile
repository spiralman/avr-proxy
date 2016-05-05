CC = xtensa-lx106-elf-gcc
CFLAGS = -I. -mlongcalls
LDLIBS = -nostdlib -Wl,--start-group -lmain -lnet80211 -lwpa -llwip -lpp -lphy -Wl,--end-group -lgcc
LDFLAGS = -Teagle.app.v6.ld

avr-proxy-0x00000.bin: avr-proxy
	esptool.py elf2image $^

avr-proxy: avr-proxy.o

avr-proxy.o: avr-proxy.c

flash: avr-proxy-0x00000.bin
	esptool.py write_flash 0 avr-proxy-0x00000.bin 0x40000 avr-proxy-0x40000.bin

clean:
	rm -f avr-proxy avr-proxy.o avr-proxy-0x00000.bin avr-proxy-0x400000.bin
