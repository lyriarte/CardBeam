all:	CardBeam.prc

clean:
	rm -f *.res *.bin *.grc *.o CardBeam CardBeam.prc

force:	clean	all

archive:	force
	rm -f *.res *.bin *.grc *.o CardBeam

bin.res:	CardBeam.rcp
	rm -f *.res *.bin
	pilrc CardBeam.rcp
	touch bin.res

CardBeam.o:	CardBeam.c CardBeam.h
	m68k-palmos-gcc -fno-builtin -o CardBeam.o -I/m68k-palmos/include -c CardBeam.c

CardBeam.prc:	CardBeam bin.res
	build-prc CardBeam.prc 'CardBeam' CaBe *.bin *.grc

CardBeam:	CardBeam.o
	rm -f *.grc
	m68k-palmos-gcc -o CardBeam CardBeam.o -L/m68k-palmos/lib
	m68k-palmos-obj-res CardBeam

