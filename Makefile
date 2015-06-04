filesys: filesys.c
	cc -o $@ $<
clean:
	rm -f filesys
.PHONY: clean
