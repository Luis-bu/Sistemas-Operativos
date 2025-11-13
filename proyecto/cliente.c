#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

void extraer_parametros(int argc, char* argv[], char** id_proceso, char** ruta_archivo, char** tubo_principal) {
    for(int i=0; i<argc; i++){
        if(*argv[i] == '-'){
            switch (*(argv[i++]+1))
            {
            case 's':
                *id_proceso = argv[i];
                break;
            case 'a':
                *ruta_archivo = argv[i];
                break;
            case 'p':
                *tubo_principal = argv[i];
                break;
            }
        }
    }
}

void conectar_servidor(char* tubo_principal, char* id_proceso, float* momento_sistema, int* desc_envio, int *desc_recibo, char* tubo_respuesta) {
    if (mkfifo(tubo_respuesta, 0666) == -1) {
        perror("Error al crear FIFO");
    }
    *desc_recibo = open(tubo_respuesta, O_RDONLY | O_NONBLOCK);
    if(*desc_recibo == -1){
        perror("Error al abrir FIFO para lectura");
        exit(1);
    }
    
    *desc_envio = open(tubo_principal, O_WRONLY | O_NONBLOCK);
    char msg_inicial[100];
    snprintf(msg_inicial, 100, "%s,%s", id_proceso, tubo_respuesta);
    write(*desc_envio, msg_inicial, sizeof(msg_inicial));
    usleep(100000);
    read(*desc_recibo, momento_sistema, sizeof(float));
}

void procesar_solicitudes(char* ruta_archivo, int desc_envio, float momento_sistema, int desc_recibo, char* id_proceso, char* tubo_respuesta) {
    FILE* entrada = fopen(ruta_archivo, "r");
    if (entrada == NULL) {
        printf("Error al abrir el archivo");
        return;
    }

    char registro[50];
    char respuesta[100];
    char finalizado = 1;
    
    while (fgets(registro, sizeof(registro), entrada)) {
        finalizado = 0;
        char temporal[50]; 
        strcpy(temporal, registro);
        strtok(temporal, ",");
        char* token = strtok(NULL, ",");
        int hora_pedida = atoi(token);
        
        if (hora_pedida > momento_sistema) {
            char peticion[100];
            snprintf(peticion, 100, "%s,%s%s", id_proceso, registro, feof(entrada) ? "\n" : "");
            write(desc_envio, peticion, strlen(peticion)+1);
            printf("%s", peticion);

            usleep(10000);

            ssize_t recibido = read(desc_recibo, respuesta, sizeof(respuesta));
            if(recibido > 0){
                respuesta[recibido] = '\0';
                if(strcmp(respuesta, "FIN") == 0){
                    break;
                }
                else{
                    printf("Respuesta del controlador: %s\n", respuesta);
                }
            }

            sleep(2);

            recibido = read(desc_recibo, respuesta, sizeof(respuesta));
            if(recibido > 0){
                respuesta[recibido] = '\0';
                if(strcmp(respuesta, "FIN") == 0){
                    break;
                }
            }
        }
        finalizado = 1;
    }

    if(finalizado){
        char msg_cierre[40];
        snprintf(msg_cierre, 40, "Agente %s termina.", id_proceso);
        write(desc_envio, msg_cierre, strlen(msg_cierre)+1);
    }

    fclose(entrada);
    close(desc_envio);
    usleep(10000);
    close(desc_recibo);
    unlink(tubo_respuesta);
}

int main(int argc, char *argv[]){
    char* id_proceso;
    char* ruta_archivo;
    char* tubo_principal;
    int desc_envio;
    int desc_recibo;
    float momento_sistema;
    char tubo_respuesta[20];

    extraer_parametros(argc, argv, &id_proceso, &ruta_archivo, &tubo_principal);

    snprintf(tubo_respuesta, 20, "%s%s", "pipe", id_proceso);
    conectar_servidor(tubo_principal, id_proceso, &momento_sistema, &desc_envio, &desc_recibo, tubo_respuesta);
    procesar_solicitudes(ruta_archivo, desc_envio, momento_sistema, desc_recibo, id_proceso, tubo_respuesta);

    return 0;
}