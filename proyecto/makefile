COMPILADOR = gcc
OPCIONES = -Wall -g
HILOS = -pthread

EJECUTABLES = agente controlador

all: $(EJECUTABLES)

agente: Cliente.c
	$(COMPILADOR) $(OPCIONES) Cliente.c -o agente

controlador: Controlador.c
	$(COMPILADOR) $(OPCIONES) $(HILOS) Controlador.c -o controlador

clean:
	rm -f $(EJECUTABLES)

.PHONY: all clean