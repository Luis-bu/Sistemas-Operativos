# Makefile
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -pthread
TARGETS = controlador agente

all: $(TARGETS)

controlador: controlador.c utils.c common.h
	$(CC) $(CFLAGS) -o $@ controlador.c utils.c

agente: agente.c utils.c common.h
	$(CC) $(CFLAGS) -o $@ agente.c utils.c

clean:
	rm -f $(TARGETS) *.o
	rm -f /tmp/pipe_*

.PHONY: all clean