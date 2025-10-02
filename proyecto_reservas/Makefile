# Makefile del proyecto
CC=gcc
CFLAGS=-Wall -Wextra -O2 -std=c11 -pthread

all: controlador agente

controlador: controlador.c
	$(CC) $(CFLAGS) controlador.c -o controlador

agente: agente.c
	$(CC) $(CFLAGS) agente.c -o agente

clean:
	rm -f controlador agente
