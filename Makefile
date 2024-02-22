.PHONY : all clean

all : compress decompress

compress : compress.o
	$(CC) --std=c99 $^ -o $@

decompress : decompress.o
	$(CC) --std=c99 $^ -o $@

clean :
	$(RM) *.o compress decompress
