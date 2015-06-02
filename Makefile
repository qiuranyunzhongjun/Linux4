filesys: filesys.c filesys.h
	cc -o $@ $<
clean:
	rm -f filesys
.PHONY: clean
