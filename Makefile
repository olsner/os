# Makefile for Shaaman. Copyright by Simon Brenner, 2002

.PHONY: shaman all clean install commit

BXIMAGE=/opt/bochs/bin/bximage

all: shaman

shaman: disk.dat

clean:
	rm -f bootfs.img disk.dat
	rm -f boot/boot.b boot/kstart.b

boot/%.b: %.asm
	nasm -f bin $< -o $@
	
bootfs.img: boot/kstart.b
	genromfs -v -f bootfs.img -d boot -a 512 -x boot.b

disk.dat: partitions.dat boot/boot.b bootfs.img
	dd if=/dev/zero of=$@ bs=10321920 count=1
	dd if=partitions.dat of=$@ conv=notrunc bs=1 skip=438 seek=438 count=72
	dd if=boot/boot.b of=$@ conv=notrunc bs=1 count=438
	dd if=boot/boot.b of=$@ conv=notrunc bs=1 skip=510 seek=510 count=2
	dd if=bootfs.img of=$@ conv=notrunc bs=512 seek=63
