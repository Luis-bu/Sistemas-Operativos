#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define LIMITE_CLIENTES 10   // Máx. clientes simultáneos
#define TAM_BUFFER 256        // Tamaño de lectura

pthread_mutex_t bloqueo;      // Mutex para proteger secciones críticas

// Datos usados por el hilo que simula el reloj
typedef struct {
    int duracion_hora;            // Duración de 1h simulada (en seg reales)
    time_t inicio_real;           // Momento real de inicio
    int apertura;                 // Hora de apertura parque
    int cierre;                   // Hora de cierre parque
    int inicio_sim;               // Inicio simulación
    int fin_sim;                  // Fin simulación
    volatile float* reloj;        // Hora simulada
    int total_horas;              // Horas totales del día
    int* ocupacion;               // Ocupación por hora
    char*** registros_familias;   // Familias que ingresan por hora
    int* contador_familias;       // Cuántas familias entran por hora
    volatile char* terminado;     // Marca de finalización
    int* ingresos;                // Personas que ingresan por hora
} DatosReloj;

// Datos usados por el hilo que atiende el pipe principal
typedef struct {
    int descriptor_lectura;       // FIFO principal
    char** ids_clientes;          // IDs de clientes registrados
    int* descriptores_escritura;  // Pipes privados de los clientes
    int* num_clientes;            // Nº clientes conectados
    volatile float* reloj;        // Hora actual
    int apertura, cierre;         // Franja operativa
    int inicio_sim, fin_sim;      // Rango de simulación
    int capacidad;                // Capacidad del parque
    int* ocupacion;               // Ocupación por hora
    int total_horas;
    char*** registros_familias;   // Familias por hora
    int* contador_familias;       // Contadores por hora
    volatile char* terminado;
    int* estadisticas;            // Confirmadas / Reprogramadas / Denegadas
    int* ingresos;                // Entradas por hora
} DatosPipe;

void parsear_argumentos(int argc, char* argv[], int* inicio, int* fin, int* seg, int* cap, char** tubo) {
    for(int i=1; i<argc; i++){
        if(*argv[i] == '-'){
            switch (*(argv[i++]+1)) {
                case 'i': *inicio = atoi(argv[i]); break;     // Hora inicio simulación
                case 'f': *fin = atoi(argv[i]); break;        // Hora fin simulación
                case 's': *seg = atoi(argv[i]); break;        // Segundos por hora
                case 't': *cap = atoi(argv[i]); break;        // Capacidad total parque
                case 'p': *tubo = argv[i]; break;             // FIFO principal
            }
        }
    }
}

void preparar_sistema(char* tubo, time_t* momento_inicio, int* fd_lect) {
    if (mkfifo(tubo, 0666) == -1) { // Crear FIFO principal
        perror("Error al crear FIFO");
    }
    time(momento_inicio);  // Guardar tiempo de inicio real
    *fd_lect = open(tubo, O_RDONLY | O_NONBLOCK);  // Abrir FIFO en lectura non-blocking
    if(*fd_lect == -1){ 
        perror("Error al abrir FIFO para lectura");
        exit(1);
    }
}

int dividir_mensaje(char mensaje[], char* fragmentos[]){
    int cantidad = 0;
    char* copia = strdup(mensaje);        // Copia de seguridad
    char* token = strtok(mensaje, ",");

    while(token != NULL){                 // Separar por comas
        fragmentos[cantidad++] = token;
        token = strtok(NULL, ",");
    }

    // Si no venía separado por comas, separar por espacios
    if(cantidad == 1){
        cantidad = 0;
        token = strtok(copia, " ");
        while(token != NULL){
            fragmentos[cantidad++] = token;
            token = strtok(NULL, " ");
        }
    }

    return cantidad;                       // Devuelve número de partes
}

void registrar_cliente(char** fragmentos, char* ids[], int descriptores[], int* contador, float momento){
    ids[*contador] = strdup(fragmentos[0]);                     // Guardar ID del cliente
    descriptores[*contador] = open(fragmentos[1], O_WRONLY);    // Abrir FIFO privado del cliente
    if(descriptores[*contador] == -1){
        perror("Error abrir FIFO de escritura");
        exit(1);
    }
    write(descriptores[*contador], &momento, sizeof(float));    // Enviar hora actual
    (*contador)++;                                              // Aumentar nº clientes
}

int asignar_espacio(int ocupacion[], int horas, int solicitada,
    int apertura, float momento, int cantidad, int capacidad,
    char*** registros, char* familia, int contador[], int* ingresos) {

    int posicion = ((solicitada < momento ? ((int)momento + 1) : solicitada) - apertura);

    // Buscar una hora donde haya espacio
    for(int i=posicion; i<horas; i++){
        if( (ocupacion[i] + cantidad) <= capacidad ){
            if((i+1 < horas) && ((ocupacion[i+1] + cantidad) <= capacidad)){

                ocupacion[i] += cantidad;   // Registrar ocupación hora 1
                ocupacion[i+1] += cantidad; // Registrar ocupación hora 2

                char* duplicado = strdup(familia); // Guardar familia en registros

                registros[i][contador[i]] = duplicado;
                ingresos[i] += cantidad;
                contador[i]++;

                registros[i+1][contador[i+1]] = duplicado;
                contador[i+1]++;

                return (i + apertura);      // Devolver hora asignada
            }
        }
    }

    return 0;   // No hay cupo
}

void procesar_peticion(char** f, DatosPipe *d) {
    char* id_cliente = f[0];
    char* nombre = f[1];
    int hora = atoi(f[2]);
    int personas = atoi(f[3]);
    char respuesta[100];
    char tiene_respuesta = 0;
    int hora_asignada;

    // Imprimir la petición recibida
    printf("[PETICION] Cliente %s solicita espacio para grupo %s (%d personas) - Hora deseada: %d:00\n", 
           id_cliente, nombre, personas, hora);

    // Validaciones iniciales
    if(hora > d->cierre || personas > d->capacidad || *(d->reloj) >= d->cierre){
        snprintf(respuesta, 100, "DENEGADO: Solicitud fuera de rango operativo");
        d->estadisticas[2]++;
        tiene_respuesta = 1;
    }

    if(tiene_respuesta == 0){
        // Intentar asignar espacio
        hora_asignada = asignar_espacio(d->ocupacion, d->total_horas, hora, d->apertura, 
                                        *(d->reloj), personas, d->capacidad, d->registros_familias, 
                                        nombre, d->contador_familias, d->ingresos);
        
        // Caso hora pasada (extemporánea)
        if(hora < *(d->reloj)){
            tiene_respuesta = 1;
            if(hora_asignada > hora){
                snprintf(respuesta, 100, "REPROGRAMADO: Hora extemporanea - Nueva asignacion: %d:00", hora_asignada);
                d->estadisticas[1]++;
            } else {
                snprintf(respuesta, 100, "DENEGADO: Hora extemporanea sin disponibilidad posterior");
                d->estadisticas[2]++;
            }
        }

        // Horario válido
        if(tiene_respuesta == 0){
            if(hora_asignada == hora){
                snprintf(respuesta, 100, "CONFIRMADO: Espacios asignados para %d:00", hora_asignada);
                d->estadisticas[0]++;
            } else if(hora_asignada > hora){
                snprintf(respuesta, 100, "REPROGRAMADO: Sin cupo - Nueva asignacion: %d:00", hora_asignada);
                d->estadisticas[1]++;
            } else {
                snprintf(respuesta, 100, "DENEGADO: Capacidad insuficiente en todas las franjas");
                d->estadisticas[2]++;
            }
        }
    }

    // Enviar la respuesta al cliente correspondiente
    for(int i=0; i<*(d->num_clientes); i++){
        if(strcmp(d->ids_clientes[i], id_cliente) == 0){
            write(d->descriptores_escritura[i], respuesta, strlen(respuesta)+1);
            break;
        }
    }
}

void cerrar_cliente(char** f, char* ids[], int descriptores[], int* contador) {
    char* id = f[1];

    // Buscar cliente y quitarlo del arreglo
    for(int i=0; i<(*contador); i++){
        if(strcmp(ids[i], id) == 0){
            close(descriptores[i]);
            (*contador)--;

            // Compactar arreglo
            for(int j=i; j<(*contador); j++){
                ids[j] = ids[j+1];
                descriptores[j] = descriptores[j+1];
            }
            break;
        }
    }
}

void* ejecutar_reloj(void* parametros){
    DatosReloj* datos = (DatosReloj*)parametros;
    float anterior = -1;
    
    while(1){
        // Calcular hora simulada
        time_t actual;
        pthread_mutex_lock(&bloqueo);
        time(&actual);
        *(datos->reloj) = (difftime(actual, datos->inicio_real) / datos->duracion_hora) + datos->inicio_sim;

        // Imprimir cambios de hora
        if((int)(*(datos->reloj)) != (int)anterior){
            anterior = *(datos->reloj);
            printf("\n========== TIEMPO: %.0f:00 ==========\n", *(datos->reloj));

            // Cálculo de entradas y salidas por hora (lógica de ocupación del parque)
            if( (int)(*(datos->reloj)) >= datos->apertura && (int)(*(datos->reloj)) <= datos->cierre ){
                int posicion = (int)(*(datos->reloj)) - datos->apertura;

                if(posicion == datos->total_horas){
                    printf(">> Salidas: %d personas abandonan las instalaciones\n", datos->ocupacion[posicion-1]);
                }else if(posicion > 0){
                    int diferencia = datos->ocupacion[posicion-1] + datos->ingresos[posicion] - datos->ocupacion[posicion];
                    printf(">> Ingresos: %d personas acceden al parque\n", datos->ingresos[posicion]);
                    printf(">> Salidas: %d personas abandonan las instalaciones\n", diferencia);
                }else if(posicion == 0){
                    printf(">> Ingresos: %d personas acceden al parque\n", datos->ocupacion[posicion]);
                }

                // Mostrar grupos que entran/salen
                if(posicion == 0){
                    printf("-- Grupos que ingresan:\n");
                    for(int j=0; j<datos->contador_familias[posicion]; j++){
                        printf("   * Grupo %s (ingreso confirmado)\n", datos->registros_familias[posicion][j]);
                    }
                }else if(posicion != datos->total_horas){
                    printf("-- Grupos que ingresan:\n");
                    for(int i=0; i<datos->contador_familias[posicion]; i++){
                        for(int j=0; j<datos->contador_familias[posicion-1]; j++){
                            if(datos->registros_familias[posicion][i] == datos->registros_familias[posicion-1][j]){
                                break;
                            }
                            if(j == datos->contador_familias[posicion-1]-1){
                                printf("   * Grupo %s (ingreso confirmado)\n", datos->registros_familias[posicion][i]);
                            }
                        }
                    }
                    printf("-- Grupos que se retiran:\n");
                    for(int i=0; i<datos->contador_familias[posicion-1]; i++){
                        for(int j=0; j<datos->contador_familias[posicion]; j++){
                            if(datos->registros_familias[posicion-1][i] == datos->registros_familias[posicion][j]){
                                break;
                            }
                            if(j == datos->contador_familias[posicion]-1){
                                printf("   * Grupo %s (salida registrada)\n", datos->registros_familias[posicion-1][i]);
                                free(datos->registros_familias[posicion-1][i]);
                            }
                        }
                    }
                }else{
                    printf("-- Grupos que se retiran:\n");
                    for(int j=0; j<datos->contador_familias[posicion-1]; j++){
                        printf("   * Grupo %s (salida registrada)\n", datos->registros_familias[posicion-1][j]);
                        free(datos->registros_familias[posicion-1][j]);
                    }
                }
                printf("\n");
            }
        }

        // Fin de simulación
        if(*(datos->reloj) >= datos->fin_sim){
            *(datos->terminado) = 1;
            break;
        }

        pthread_mutex_unlock(&bloqueo);
        usleep(100000);
    }
    return NULL;
}

void* escuchar_tubo(void* parametros){
    DatosPipe* datos = (DatosPipe*)parametros;
    char buffer[TAM_BUFFER];
    char* fragmentos[4];

    while(*(datos->terminado) == 0){
        ssize_t leidos = read(datos->descriptor_lectura, buffer, sizeof(buffer)-1);

        if(leidos > 0){
            buffer[leidos] = '\0';
            int cantidad = dividir_mensaje(buffer, fragmentos);

            pthread_mutex_lock(&bloqueo);
            
            if(cantidad == 2){                      // Registro de nuevo cliente
                registrar_cliente(fragmentos, datos->ids_clientes, datos->descriptores_escritura, 
                                 datos->num_clientes, *(datos->reloj));
            }
            else if(cantidad == 4){                 // Solicitud de reserva
                procesar_peticion(fragmentos, datos);
            }
            else if(cantidad == 3){                 // Cierre de cliente
                cerrar_cliente(fragmentos, datos->ids_clientes, datos->descriptores_escritura, 
                              datos->num_clientes);
            }
            
            pthread_mutex_unlock(&bloqueo);
        }
        usleep(1000);
    }
    return NULL;
}

void generar_informe(int horas, int ocupacion[], int estadisticas[], int clientes, 
                     int descriptores[], int apertura, char* tubo, int fd_lect) {
    char* msg_fin = "FIN";
    
    // Enviar "FIN" a todos los clientes
    for(int i=0; i<clientes; i++){
        write(descriptores[i], msg_fin, strlen(msg_fin));
        close(descriptores[i]);
    }
    
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║     INFORME FINAL DE OPERACIONES      ║\n");
    printf("╚════════════════════════════════════════╝\n\n");
    
    // Cálculo de franjas con mayor ocupación
    int maximo = 0;
    for(int i=0; i<horas; i++){
        if(ocupacion[i] > maximo){
            maximo = ocupacion[i];
        }
    }
    printf("📊 PERIODOS DE MAXIMA OCUPACION:\n");
    for(int i=0; i<horas; i++){
        if(ocupacion[i] == maximo){
            printf("   └─ Franja horaria %d:00 → %d visitantes\n", i+apertura, maximo);
        }
    }

    // Cálculo de franjas con menor ocupación
    int minimo = maximo;
    for(int i=0; i<horas; i++){
        if(ocupacion[i] < minimo){
            minimo = ocupacion[i];
        }
    }
    printf("\n📉 PERIODOS DE MINIMA OCUPACION:\n");
    for(int i=0; i<horas; i++){
        if(ocupacion[i] == minimo){
            printf("   └─ Franja horaria %d:00 → %d visitantes\n", i+apertura, minimo);
        }
    }

    // Resumen de estadísticas de solicitudes
    printf("\n📋 RESUMEN DE SOLICITUDES PROCESADAS:\n");
    printf("   ✓ Aprobadas en horario solicitado: %d\n", estadisticas[0]);
    printf("   ↻ Reprogramadas a otro horario: %d\n", estadisticas[1]);
    printf("   ✗ Rechazadas definitivamente: %d\n", estadisticas[2]);
    printf("\n════════════════════════════════════════\n");

    close(fd_lect);
    unlink(tubo); // Eliminar FIFO principal
}

int main(int argc, char *argv[]){
    int inicio, fin, duracion, capacidad;
    char* tubo;
    time_t momento_inicio;
    volatile float reloj = 0;
    int fd_lect;
    volatile char terminado = 0;
    char* ids[LIMITE_CLIENTES];
    int descriptores[LIMITE_CLIENTES];
    int clientes_activos = 0;
    int estadisticas[3]={0,0,0};  // [confirmadas, reprogramadas, denegadas]

    // Leer parámetros
    parsear_argumentos(argc, argv, &inicio, &fin, &duracion, &capacidad, &tubo);

    // Validar entrada
    if(inicio >= fin || duracion <= 0 || capacidad <= 0){
        printf("Error: Parámetros de ejecución inválidos.\n");
        return(1);
    }

    pthread_mutex_init(&bloqueo, NULL);
    
    // Ajustar apertura/cierre reales del parque (7–19)
    int apertura = (inicio <= 7) ? 7 : inicio;
    int cierre = (fin <= 19) ? fin : 19;
    int horas = (fin == apertura) ? 1 : cierre - apertura;
    
    // Inicializar arrays de ocupación
    int ocupacion[horas];
    for(int i=0;i<horas;i++){
        ocupacion[i]=0;
    }

    int ingresos[horas];
    for(int i=0;i<horas;i++){
        ingresos[i]=0;
    }

    int contadores[horas];
    for(int i=0;i<horas;i++){
        contadores[i]=0;
    }

    // Reservar memoria para registros de familias
    char*** registros = malloc(horas * sizeof(char**));
    for(int i=0;i<horas;i++) registros[i] = malloc(capacidad * sizeof(char*));

    // Inicializar estructuras internas
    preparar_sistema(tubo, &momento_inicio, &fd_lect);

    // Crear hilos del reloj y pipe
    pthread_t hilo_reloj, hilo_tubo;

    DatosReloj parametros_reloj = {duracion, momento_inicio, apertura, cierre, inicio, fin, 
                                   &reloj, horas, ocupacion, registros, contadores, &terminado, ingresos};
    DatosPipe parametros_tubo = {fd_lect, ids, descriptores, &clientes_activos, &reloj, apertura, cierre, 
                                inicio, fin, capacidad, ocupacion, horas, registros, contadores, 
                                &terminado, estadisticas, ingresos};

    pthread_create(&hilo_reloj, NULL, ejecutar_reloj, &parametros_reloj);
    pthread_create(&hilo_tubo, NULL, escuchar_tubo, &parametros_tubo);

    // Esperar hilos
    pthread_join(hilo_reloj, NULL);
    pthread_join(hilo_tubo, NULL);

    // Informe final
    generar_informe(horas, ocupacion, estadisticas, clientes_activos, descriptores, apertura, tubo, fd_lect);

    pthread_mutex_destroy(&bloqueo);

    // Liberar memoria
    for (int i = 0; i < horas; i++) {
        free(registros[i]);
    }
    free(registros);

    return 0;
}