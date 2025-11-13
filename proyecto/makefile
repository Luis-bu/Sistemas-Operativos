# Makefile para el Sistema de Reservas de Parque
# Proyecto de Sistemas Operativos 2025-30

CC = gcc
CFLAGS = -Wall -Wextra -pthread -std=c99 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -pthread

# Archivos ejecutables
CONTROLADOR = controlador
AGENTE = agente

# Archivos fuente
CONTROLADOR_SRC = controlador.c
AGENTE_SRC = agente.c

# Archivos objeto
CONTROLADOR_OBJ = controlador.o
AGENTE_OBJ = agente.o

# Target por defecto: compilar todo
all: $(CONTROLADOR) $(AGENTE)

# Compilar el controlador
$(CONTROLADOR): $(CONTROLADOR_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "✓ Controlador compilado exitosamente"

# Compilar el agente
$(AGENTE): $(AGENTE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "✓ Agente compilado exitosamente"

# Regla para generar archivos objeto
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Limpiar archivos generados
clean:
	rm -f $(CONTROLADOR) $(AGENTE) *.o
	rm -f pipe_* solicitud_pipe
	@echo "✓ Archivos limpiados"

# Limpiar todo incluyendo archivos temporales
cleanall: clean
	rm -f *.txt *.csv *.log
	@echo "✓ Limpieza completa realizada"

# Crear archivos de prueba de ejemplo
test-files:
	@echo "Creando archivos de prueba..."
	@echo "Zuluaga,8,10" > solicitudes_agente1.csv
	@echo "Dominguez,8,4" >> solicitudes_agente1.csv
	@echo "Rojas,10,10" >> solicitudes_agente1.csv
	@echo "Fin,0,0" >> solicitudes_agente1.csv
	@echo "Garcia,9,5" > solicitudes_agente2.csv
	@echo "Martinez,11,8" >> solicitudes_agente2.csv
	@echo "Lopez,12,15" >> solicitudes_agente2.csv
	@echo "Fin,0,0" >> solicitudes_agente2.csv
	@echo "✓ Archivos de prueba creados: solicitudes_agente1.csv y solicitudes_agente2.csv"

# Ayuda
help:
	@echo "Makefile del Sistema de Reservas de Parque"
	@echo ""
	@echo "Targets disponibles:"
	@echo "  make              - Compila el controlador y el agente"
	@echo "  make all          - Compila todo"
	@echo "  make clean        - Elimina ejecutables y objetos"
	@echo "  make cleanall     - Elimina todo incluyendo archivos temporales"
	@echo "  make test-files   - Crea archivos CSV de prueba"
	@echo "  make run          - Ejecuta el script simple (run.sh)"
	@echo "  make test         - Ejecuta el script completo (test.sh)"
	@echo "  make help         - Muestra esta ayuda"
	@echo ""
	@echo "Ejemplo de uso:"
	@echo "  1. make              # Compilar"
	@echo "  2. make test-files   # Crear archivos de prueba"
	@echo "  3. make run          # Ejecutar (recomendado)"
	@echo ""
	@echo "O manualmente:"
	@echo "  1. ./controlador -i 7 -f 19 -s 5 -t 20 -p solicitud_pipe &"
	@echo "  2. sleep 3"
	@echo "  3. ./agente -s AgenteA -a solicitudes_agente1.csv -p solicitud_pipe &"

# Ejecutar con script simple
run: $(CONTROLADOR) $(AGENTE) test-files
	@chmod +x run.sh 2>/dev/null || true
	@if [ -f run.sh ]; then \
		./run.sh; \
	else \
		echo "Error: run.sh no encontrado"; \
	fi

# Ejecutar tests completos
test: $(CONTROLADOR) $(AGENTE) test-files
	@chmod +x test.sh 2>/dev/null || true
	@if [ -f test.sh ]; then \
		./test.sh; \
	else \
		echo "Error: test.sh no encontrado"; \
	fi

.PHONY: all clean cleanall test-files help run test