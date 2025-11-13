#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>

#define LIMITE_CLIENTES 10
#define TAM_BUFFER 256

pthread_mutex_t bloqueo;

typedef struct {
    int duracion_hora;
    time_t inicio_real;
    int apertura;
    int cierre;
    int inicio_sim;
    int fin_sim;
    volatile float* reloj;
    int total_horas;
    int* ocupacion;
    char*** registros_familias;
    int* contador_familias;
    volatile char* terminado;
    int* ingresos;
} DatosReloj;

typedef struct {
    int descriptor_lectura;
    char** ids_clientes;
    int* descriptores_escritura;
    int* num_clientes;
    volatile float* reloj;
    int apertura;
    int cierre;
    int inicio_sim;
    int fin_sim;
    int capacidad;
    int* ocupacion;
    int total_horas;
    char*** registros_familias;
    int* contador_familias;
    volatile char* terminado;
    int* estadisticas;
    int* ingresos;
} DatosPipe;

void parsear_argumentos(int argc, char* argv[], int* inicio, int* fin, int* seg, int* cap, char** tubo) {
    for(int i=1; i<argc; i++){
        if(*argv[i] == '-'){
            switch (*(argv[i++]+1)) {
                case 'i':
                    *inicio = atoi(argv[i]);
                    break;
                case 'f':
                    *fin = atoi(argv[i]);
                    break;
                case 's':
                    *seg = atoi(argv[i]);
                    break;
                case 't':
                    *cap = atoi(argv[i]);
                    break;
                case 'p':
                    *tubo = argv[i];
                    break;
            }
        }
    }
}

void preparar_sistema(char* tubo, time_t* momento_inicio, int* fd_lect) {
    if (mkfifo(tubo, 0666) == -1) {
        perror("Error al crear FIFO");
    }
    time(momento_inicio);
    *fd_lect = open(tubo, O_RDONLY | O_NONBLOCK);
    if(*fd_lect == -1){
        perror("Error al abrir FIFO para lectura");
        exit(1);
    }
}

int dividir_mensaje(char mensaje[], char* fragmentos[]){
    int cantidad = 0;
    char* copia = strdup(mensaje);
    char* token = strtok(mensaje, ",");
    while(token != NULL){
        fragmentos[cantidad++] = token;
        token = strtok(NULL, ",");
    }
    if(cantidad == 1){
        cantidad = 0;
        token = strtok(copia, " ");
        while(token != NULL){
            fragmentos[cantidad++] = token;
            token = strtok(NULL, " ");
        }
    }
    return cantidad;
}

void registrar_cliente(char** fragmentos, char* ids[], int descriptores[], int* contador, float momento){
    ids[*contador] = strdup(fragmentos[0]);
    descriptores[*contador] = open(fragmentos[1], O_WRONLY);
    if(descriptores[*contador] == -1){
        perror("Error abrir FIFO de escritura");
        exit(1);
    }
    write(descriptores[*contador], &momento, sizeof(float));
    (*contador)++;
}

int asignar_espacio(int ocupacion[], int horas, int solicitada, int apertura, float momento, int cantidad, int capacidad, char*** registros, char* familia, int contador[], int* ingresos) {
    int posicion = ((solicitada < momento ? ((int)momento + 1) : solicitada) - apertura);
    for(int i=posicion; i<horas; i++){
        if( (ocupacion[i] + cantidad) <= capacidad ){
            if((i+1 < horas) && ((ocupacion[i+1] + cantidad) <= capacidad)){
                ocupacion[i] += cantidad;
                ocupacion[i+1] += cantidad;
                char* duplicado = strdup(familia);
                registros[i][contador[i]] = duplicado;
                ingresos[i] += cantidad;
                contador[i]++;
                registros[i+1][contador[i+1]] = duplicado;
                contador[i+1]++;
                return (i + apertura);
            }
        }
    }
    return 0;
}

void procesar_peticion(char** fragmentos, DatosPipe *datos) {
    char* id_cliente = fragmentos[0];
    char* nombre_grupo = fragmentos[1];
    int hora_pedida = atoi(fragmentos[2]);
    int cantidad_personas = atoi(fragmentos[3]);
    char respuesta[100];
    char tiene_respuesta = 0;
    int hora_asignada;

    printf("Agente: %s, solicita reserva para la familia %s de %d personas a las %d:00\n", id_cliente, nombre_grupo, cantidad_personas, hora_pedida);

    if(hora_pedida > datos->cierre || cantidad_personas > datos->capacidad || *(datos->reloj) >= datos->cierre){
        snprintf(respuesta, 100, "Reserva negada, debe volver otro dia");
        datos->estadisticas[2]++;
        tiene_respuesta = 1;
    }

    if(tiene_respuesta == 0){
        hora_asignada = asignar_espacio(datos->ocupacion, datos->total_horas, hora_pedida, datos->apertura,  *(datos->reloj), cantidad_personas, datos->capacidad, datos->registros_familias, nombre_grupo, datos->contador_familias, datos->ingresos);
        if(hora_pedida < *(datos->reloj)){
            tiene_respuesta = 1;
            if(hora_asignada > hora_pedida){
                snprintf(respuesta, 100, "Reserva negada por extemporánea, la nueva hora es %d:00", hora_asignada);
                datos->estadisticas[1]++;
            }else{
                snprintf(respuesta, 100, "Reserva negada por extemporánea, no hay horas disponibles");
                datos->estadisticas[2]++;
            }
        }

        if(tiene_respuesta == 0){
            if(hora_asignada == hora_pedida){
                snprintf(respuesta, 100, "Reserva ok para la hora %d:00", hora_asignada);
                datos->estadisticas[0]++;
            }else if(hora_asignada > hora_pedida){
                snprintf(respuesta, 100, "Reserva garantizada para otras horas, la nueva hora es %d:00", hora_asignada);
                datos->estadisticas[1]++;
            }
            else if(hora_asignada == 0){
                snprintf(respuesta, 100, "Reserva negada, debe volver otro dia");
                datos->estadisticas[2]++;
            }
        }
    }

    for(int i=0; i<*(datos->num_clientes); i++){
        if(strcmp(datos->ids_clientes[i], id_cliente) == 0){
            write(datos->descriptores_escritura[i], respuesta, strlen(respuesta)+1);
            break;
        }
    }
}

void cerrar_cliente(char** fragmentos, char* ids[], int descriptores[], int* contador) {
    char* id_cliente = fragmentos[1];

    for(int i=0; i<(*contador); i++){
        if(strcmp(ids[i], id_cliente) == 0){
            close(descriptores[i]);
            (*contador)--;
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
        time_t actual;
        pthread_mutex_lock(&bloqueo);
        time(&actual);
        *(datos->reloj) = (difftime(actual, datos->inicio_real) / datos->duracion_hora) + datos->inicio_sim;

        if((int)(*(datos->reloj)) != (int)anterior){
            anterior = *(datos->reloj);
            printf("\nHora actual: %.0f:00\n", *(datos->reloj));

            if( (int)(*(datos->reloj)) >= datos->apertura && (int)(*(datos->reloj)) <= datos->cierre ){
                int posicion = (int)(*(datos->reloj)) - datos->apertura;

                if(posicion == datos->total_horas){
                    printf("Han salido %d personas\n", datos->ocupacion[posicion-1]);
                }else if(posicion > 0){
                    int diferencia = datos->ocupacion[posicion-1] + datos->ingresos[posicion] - datos->ocupacion[posicion];
                    printf("Han entrado %d personas\n", datos->ingresos[posicion]);
                    printf("Han salido %d personas\n", diferencia);
                }else if(posicion == 0){
                    printf("Han entrado %d personas\n", datos->ocupacion[posicion]);
                }

                if(posicion == 0){
                    printf("Familias que entran:\n");
                    for(int j=0; j<datos->contador_familias[posicion]; j++){
                        printf("- La familia %s ha entrado \n", datos->registros_familias[posicion][j]);
                    }
                    
                }else if(posicion != datos->total_horas){
                    printf("Familias que entran:\n");
                    for(int i=0; i<datos->contador_familias[posicion]; i++){
                        for(int j=0; j<datos->contador_familias[posicion-1]; j++){
                            if(datos->registros_familias[posicion][i] == datos->registros_familias[posicion-1][j]){
                                break;
                            }
                            if(j == datos->contador_familias[posicion-1]-1){
                                printf("- La familia %s ha entrado \n", datos->registros_familias[posicion][i]);
                            }
                        }
                    }
                    printf("Familias que salen:\n");
                    for(int i=0; i<datos->contador_familias[posicion-1]; i++){
                        for(int j=0; j<datos->contador_familias[posicion]; j++){
                            if(datos->registros_familias[posicion-1][i] == datos->registros_familias[posicion][j]){
                                break;
                            }
                            if(j == datos->contador_familias[posicion]-1){
                                printf("- La familia %s ha salido \n", datos->registros_familias[posicion-1][i]);
                                free(datos->registros_familias[posicion-1][i]);
                            }
                        }
                    }

                }else{
                    printf("Familias que salen:\n");
                    for(int j=0; j<datos->contador_familias[posicion-1]; j++){
                        printf("- La familia %s ha salido \n", datos->registros_familias[posicion-1][j]);
                        free(datos->registros_familias[posicion-1][j]);
                    }
                }
                printf("\n");
            }
        }

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
            if(cantidad == 2){
                registrar_cliente(fragmentos, datos->ids_clientes, datos->descriptores_escritura, datos->num_clientes, *(datos->reloj));
            }
            else if(cantidad == 4){
                procesar_peticion(fragmentos, datos);
            }
            else if(cantidad == 3){
                cerrar_cliente(fragmentos, datos->ids_clientes, datos->descriptores_escritura, datos->num_clientes);
            }
            pthread_mutex_unlock(&bloqueo);
        }
        usleep(1000);
    }
    return NULL;
}

void generar_informe(int horas, int ocupacion[], int estadisticas[], int clientes, int descriptores[], int apertura, char* tubo, int fd_lect) {
    char* msg_fin = "FIN";
    for(int i=0; i<clientes; i++){
        write(descriptores[i], msg_fin, strlen(msg_fin));
        close(descriptores[i]);
    }
    
    printf("\n----- Reporte Final -----\n");
    int maximo = 0;
    for(int i=0; i<horas; i++){
        if(ocupacion[i] > maximo){
            maximo = ocupacion[i];
        }
    }
    printf("Horas pico:\n");
    for(int i=0; i<horas; i++){
        if(ocupacion[i] == maximo){
            printf("- %d:00 con %d personas \n", i+apertura, maximo);
        }
    }

    int minimo = maximo;
    for(int i=0; i<horas; i++){
        if(ocupacion[i] < minimo){
            minimo = ocupacion[i];
        }
    }
    printf("\nHoras con menor numero de personas:\n");
    for(int i=0; i<horas; i++){
        if(ocupacion[i] == minimo){
            printf("- %d:00 con %d personas \n", i+apertura, minimo);
        }
    }

    printf("\nCantidad de solicitudes OK: %d\n", estadisticas[0]);
    printf("Cantidad de solicitudes re-programadas: %d\n", estadisticas[1]);
    printf("Cantidad de solicitudes negadas: %d\n", estadisticas[2]);

    close(fd_lect);
    unlink(tubo);
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
    int estadisticas[3]={0,0,0};

    parsear_argumentos(argc, argv, &inicio, &fin, &duracion, &capacidad, &tubo);

    if(inicio >= fin || duracion <= 0 || capacidad <= 0){
        printf("Error: Parámetros de ejecución inválidos.\n");
        return(1);
    }

    pthread_mutex_init(&bloqueo, NULL);
    int apertura = (inicio <= 7) ? 7 : inicio;
    int cierre = (fin <= 19) ? fin : 19;
    int horas = (fin == apertura) ? 1 : cierre - apertura;
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

    char*** registros = malloc(horas * sizeof(char**));
    for(int i=0;i<horas;i++) registros[i] = malloc(capacidad * sizeof(char*));

    preparar_sistema(tubo, &momento_inicio, &fd_lect);

    pthread_t hilo_reloj, hilo_tubo;

    DatosReloj parametros_reloj = {duracion, momento_inicio, apertura, cierre, inicio, fin, &reloj, horas, ocupacion, registros, contadores, &terminado, ingresos};
    DatosPipe parametros_tubo = {fd_lect, ids, descriptores, &clientes_activos, &reloj, apertura, cierre, inicio, fin, capacidad, ocupacion, horas, registros, contadores, &terminado, estadisticas, ingresos};

    pthread_create(&hilo_reloj, NULL, ejecutar_reloj, &parametros_reloj);
    pthread_create(&hilo_tubo, NULL, escuchar_tubo, &parametros_tubo);

    pthread_join(hilo_reloj, NULL);
    pthread_join(hilo_tubo, NULL);

    generar_informe(horas, ocupacion, estadisticas, clientes_activos, descriptores, apertura, tubo, fd_lect);

    pthread_mutex_destroy(&bloqueo);

    for (int i = 0; i < horas; i++) {
        free(registros[i]);
    }
    free(registros);

    return 0;
}