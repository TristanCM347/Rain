EXERCISES	  += rain

SRC = rain.c rain_main.c rain_hash.c rain_6_bit.c
INCLUDES = rain.h

# if you add extra .c files, add them here
SRC += 

# if you add extra .h files, add them here
INCLUDES +=

rain:	$(SRC) $(INCLUDES)
	$(CC) $(SRC) -o $@
