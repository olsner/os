# Makefile for Shaaman. Copyright by Simon Brenner, 2002

.PHONY: shaman all clean install commit

all: shaman

shaman: bootfs.img disk.dat

clean:
	rm -f bootfs.img
	rm -f boot/boot.b boot/kstart.b

boot/%.b: %.asm
	nasm -f bin $< -o $@
	
bootfs.img: boot/kstart.b
	genromfs -f bootfs.img -d boot -a 512

disk.dat: boot/boot.b bootfs.img
	dd if=boot/boot.b of=$@ conv=notrunc bs=1 count=438
	dd if=boot/boot.b of=$@ conv=notrunc bs=1 skip=510 seek=510 count=2
	dd if=bootfs.img of=$@ conv=notrunc bs=512 seek=63
