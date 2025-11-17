#include <stdio.h>    
#include <stdlib.h>    
#include <string.h>     
#include <time.h>       
#include <sys/types.h>  
#include <sys/stat.h>   
#include <unistd.h>     
#include <fcntl.h>      

// Extrae parámetros -s (id), -a (archivo), -p (pipe principal)
void extraer_parametros(int argc, char* argv[], char** id_proceso, char** ruta_archivo, char** tubo_principal) {
    for(int i=0; i<argc; i++){
        if(*argv[i] == '-'){
            switch (*(argv[i++]+1))
            {
            case 's': *id_proceso = argv[i]; break;   // ID del agente
            case 'a': *ruta_archivo = argv[i]; break; // Archivo de solicitudes
            case 'p': *tubo_principal = argv[i]; break; // Pipe hacia el servidor
            }
        }
    }
}

// Conexión inicial con el servidor (crea FIFO privado y envía registro)
void conectar_servidor(char* tubo_principal, char* id_proceso, float* momento_sistema,
                       int* desc_envio, int *desc_recibo, char* tubo_respuesta) {

    // Crear FIFO propio para recibir respuestas
    if (mkfifo(tubo_respuesta, 0666) == -1) {
        perror("Error al crear FIFO");
    }

    // Abrir pipe privado en modo lectura
    *desc_recibo = open(tubo_respuesta, O_RDONLY | O_NONBLOCK);

    // Abrir pipe principal en modo escritura
    *desc_envio = open(tubo_principal, O_WRONLY | O_NONBLOCK);

    // Enviar mensaje de registro: "id,pipePropio"
    char msg_inicial[100];
    snprintf(msg_inicial, 100, "%s,%s", id_proceso, tubo_respuesta);
    write(*desc_envio, msg_inicial, sizeof(msg_inicial));

    usleep(100000); // Pequeña espera

    // Recibir hora del sistema enviada por el servidor
    read(*desc_recibo, momento_sistema, sizeof(float));

    // Mensajes de estado
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║    PROCESO CLIENTE INICIALIZADO       ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("» ID del proceso: %s\n", id_proceso);
    printf("» Hora sistema: %.0f:00\n", *momento_sistema);
    printf("» Estado: CONECTADO\n");
    printf("════════════════════════════════════════\n\n");
}

// Procesa cada solicitud del archivo y la envía al servidor
void procesar_solicitudes(char* ruta_archivo, int desc_envio, float momento_sistema,
                          int desc_recibo, char* id_proceso, char* tubo_respuesta) {

    FILE* entrada = fopen(ruta_archivo, "r");   // Abrir archivo CSV
    if (entrada == NULL) {
        printf("[ERROR] No se pudo acceder al archivo de datos\n");
        return;
    }

    char registro[50];
    char respuesta[100];
    char finalizado = 1;
    int num_solicitud = 0;

    printf("┌─ Iniciando procesamiento de solicitudes\n");

    // Leer línea por línea del archivo
    while (fgets(registro, sizeof(registro), entrada)) {

        finalizado = 0;
        char temporal[50]; 
        strcpy(temporal, registro);

        // Separar datos: nombre, hora, cantidad
        char* nombre_grupo = strtok(temporal, ",");
        char* token = strtok(NULL, ",");
        int hora_pedida = atoi(token);
        token = strtok(NULL, ",");
        int personas = atoi(token);

        // Solo enviar si la hora es mayor que la hora actual del sistema
        if (hora_pedida > momento_sistema) {

            num_solicitud++;

            // Formar solicitud completa
            char peticion[100];
            snprintf(peticion, 100, "%s,%s%s",
                     id_proceso, registro, feof(entrada) ? "\n" : "");

            // Mostrar datos
            printf("│\n├─ [SOLICITUD #%d]\n", num_solicitud);
            printf("│  ├─ Grupo: %s\n", nombre_grupo);
            printf("│  ├─ Horario solicitado: %d:00\n", hora_pedida);
            printf("│  ├─ Cantidad personas: %d\n", personas);
            printf("│  └─ Estado: ENVIANDO...\n");

            // Enviar al servidor
            write(desc_envio, peticion, strlen(peticion)+1);

            usleep(10000); // Espera corta

            // Leer respuesta del servidor
            ssize_t recibido = read(desc_recibo, respuesta, sizeof(respuesta));
            if(recibido > 0){
                respuesta[recibido] = '\0';

                if(strcmp(respuesta, "FIN") == 0){
                    printf("│  └─ SERVIDOR FINALIZADO\n");
                    break; // Servidor terminó
                }
                else{
                    printf("│  └─ RESPUESTA: %s\n", respuesta);
                }
            }

            sleep(2); // Pausa de 2 segundos requerida por el proyecto

            // Segunda lectura por si hay otra respuesta
            recibido = read(desc_recibo, respuesta, sizeof(respuesta));
            if(recibido > 0){
                respuesta[recibido] = '\0';

                if(strcmp(respuesta, "FIN") == 0){
                    printf("│  └─ SERVIDOR FINALIZADO\n");
                    break;
                }
            }

        } else {
            // Si la hora de la solicitud ya pasó
            printf("│\n├─ [SOLICITUD OMITIDA]\n");
            printf("│  └─ Grupo %s solicita hora %d:00 (anterior al tiempo actual)\n",
                   nombre_grupo, hora_pedida);
        }

        finalizado = 1;
    }

    // Si todo terminó correctamente
    if(finalizado){
        char msg_cierre[40];
        snprintf(msg_cierre, 40, "Agente %s termina.", id_proceso);
        write(desc_envio, msg_cierre, strlen(msg_cierre)+1);

        printf("│\n└─ Todas las solicitudes han sido procesadas\n\n");
        printf("╔════════════════════════════════════════╗\n");
        printf("║      PROCESO CLIENTE FINALIZADO       ║\n");
        printf("╚════════════════════════════════════════╝\n");
        printf("» Cliente: %s\n", id_proceso);
        printf("» Solicitudes tramitadas: %d\n", num_solicitud);
        printf("» Estado: DESCONECTADO\n");
        printf("════════════════════════════════════════\n\n");
    }

    fclose(entrada);     // Cierra archivo
    close(desc_envio);   // Cierra pipe principal
    usleep(10000);
    close(desc_recibo);  // Cierra pipe privado
    unlink(tubo_respuesta); // Elimina FIFO privado
}

int main(int argc, char *argv[]){
    char* id_proceso;        // Identificador del agente
    char* ruta_archivo;      // Archivo de solicitudes
    char* tubo_principal;    // FIFO principal hacia servidor
    int desc_envio;          // Descriptor de escritura
    int desc_recibo;         // Descriptor de lectura
    float momento_sistema;   // Hora actual simulada
    char tubo_respuesta[20]; // FIFO privado del agente

    // Leer parámetros del terminal
    extraer_parametros(argc, argv, &id_proceso, &ruta_archivo, &tubo_principal);

    // Crear nombre del pipe privado: "pipe<ID>"
    snprintf(tubo_respuesta, 20, "%s%s", "pipe", id_proceso);

    // Conexión inicial
    conectar_servidor(tubo_principal, id_proceso, &momento_sistema,
                      &desc_envio, &desc_recibo, tubo_respuesta);

    // Procesar archivo y enviar solicitudes
    procesar_solicitudes(ruta_archivo, desc_envio, momento_sistema,
                         desc_recibo, id_proceso, tubo_respuesta);

    return 0;
}
