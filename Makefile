# Makefile for Shaaman. Copyright by Simon Brenner, 2002

.PHONY: shaman all clean install commit

all: shaman

shaman: boot.b kstart.b bootfs.img disk.dat
ehird: ehird.b floppy.img

clean:
	rm -f boot.b kstart.b bootfs.img
	rm -fr boot/*

%.b: %.asm
	nasm -f bin $< -o $@
	
bootfs.img: boot/kstart.b
	genromfs -f bootfs.img -d boot -a 512

$(VMW_DIR)/hd11.dat: boot.b bootfs.img
	dd if=boot.b of=$@ conv=notrunc bs=1 count=438
	dd if=boot.b of=$@ conv=notrunc bs=1 skip=510 seek=510 count=2
	dd if=bootfs.img of=$@ conv=notrunc bs=512 seek=63

disk.dat: boot.b bootfs.img
	dd if=boot.b of=$@ conv=notrunc bs=1 count=438
	dd if=boot.b of=$@ conv=notrunc bs=1 skip=510 seek=510 count=2
	dd if=bootfs.img of=$@ conv=notrunc bs=512 seek=63

floppy.img: ehird.b
	dd if=ehird.b of=$@ conv=notrunc bs=512 count=1
	dd if=/dev/urandom of=$@ conv=notrunc bs=512 seek=1 count=2879

boot/kstart.b: kstart.b
	cp $< $@

commit: clean
	cvs ci
