###############################################################
#
# Purpose: Makefile for "M-JPEG Streamer" fifo plugin
# Author.: Soldatov Dmitry
# Version: 0.3
# License: GPL
#
###############################################################



OTHER_HEADERS = ../../mjpg_streamer.h ../../utils.h ../output.h ../input.h

CFLAGS += -O -g -DLINUX -D_GNU_SOURCE -Wall -shared -fPIC
#CFLAGS += -DDEBUG
LFLAGS += 

all: input_fifo.so

clean:
	rm -f *.a *.o core *~ *.so *.lo

input_fifo.so: $(OTHER_HEADERS) input_fifo.c
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ input_fifo.c
