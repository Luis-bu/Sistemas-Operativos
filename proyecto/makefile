COMPILADOR = gcc          # Compilador a utilizar (gcc)
OPCIONES = -Wall -g       # -Wall: mostrar todos los warnings
                          # -g: incluir información de depuración
HILOS = -pthread          # Librería necesaria para usar hilos POSIX

EJECUTABLES = agente controlador   # Programas que se generarán

# Regla principal: compilar ambos ejecutables
all: $(EJECUTABLES)

# Regla para compilar el cliente (agente)
agente: Cliente.c
	$(COMPILADOR) $(OPCIONES) Cliente.c -o agente
    # gcc -Wall -g Cliente.c -o agente

# Regla para compilar el controlador
controlador: Controlador.c
	$(COMPILADOR) $(OPCIONES) $(HILOS) Controlador.c -o controlador
    # gcc -Wall -g -pthread Controlador.c -o controlador
    # (pthread es necesario porque usa hilos)

# Eliminar ejecutables generados
clean:
	rm -f $(EJECUTABLES)

# Indica que estas reglas NO corresponden a archivos reales
.PHONY: all clean
