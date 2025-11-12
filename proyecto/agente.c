/* agente.c */
#include "common.h"
#include "utils.h"

int main(int argc, char *argv[])
{
    int opt;
    char nombre[MAX_FAMILY] = {0};
    char fileSolicitud[256] = {0};
    char pipe_server[MAX_PIPE_NAME] = {0};

    while ((opt = getopt(argc, argv, "s:a:p:")) != -1) {
        switch (opt) {
            case 's': strncpy(nombre, optarg, MAX_FAMILY-1); break;
            case 'a': strncpy(fileSolicitud, optarg, 255); break;
            case 'p': strncpy(pipe_server, optarg, MAX_PIPE_NAME-1); break;
            default:
                fprintf(stderr, "Uso: %s -s nombre -a file -p pipeRecibe\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (!nombre[0] || !fileSolicitud[0] || !pipe_server[0]) {
        fprintf(stderr, "Faltan parámetros\n");
        exit(EXIT_FAILURE);
    }

    /* pipe propio para respuestas */
    char pipe_back[MAX_PIPE_NAME];
    snprintf(pipe_back, sizeof(pipe_back), "/tmp/pipe_%s_%d", nombre, getpid());
    unlink(pipe_back);
    check(mkfifo(pipe_back, 0644), "mkfifo back");

    /* abrir pipe servidor */
    int fd_server = open(pipe_server, O_WRONLY);
    check(fd_server, "open server pipe");

    /* registro */
    msg_register_t reg = {0};
    reg.h.type = MSG_REGISTER;
    reg.h.pid  = getpid();
    strncpy(reg.h.name, nombre, MAX_FAMILY-1);
    strncpy(reg.pipe_back, pipe_back, MAX_PIPE_NAME-1);
    write(fd_server, &reg, sizeof(reg));

    /* abrir pipe propio para leer respuestas */
    int fd_back = open(pipe_back, O_RDONLY);
    check(fd_back, "open back pipe");

    /* recibir hora actual */
    msg_header_t hdr;
    int current_sim = 0;
    read(fd_back, &hdr, sizeof(hdr));
    if (hdr.type == MSG_TIME)
        read(fd_back, &current_sim, sizeof(current_sim));
    printf("[Agente %s] Hora actual del simulador: %d\n", nombre, current_sim);

    /* procesar archivo */
    FILE *f = fopen(fileSolicitud, "r");
    if (!f) die("fopen solicitud");

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char fam[MAX_FAMILY], *p = line;
        int h, pers;
        /* formato: Familia,hora,personas */
        if (sscanf(p, "%[^,],%d,%d", fam, &h, &pers) != 3)
            continue;
        if (strcmp(fam, "Fin") == 0) break;

        /* validar hora */
        if (h < current_sim) {
            printf("[Agente %s] Ignorando %s: hora %d < %d (actual)\n",
                   nombre, fam, h, current_sim);
            continue;
        }

        /* enviar solicitud */
        msg_reserve_t req = {0};
        req.h.type = MSG_RESERVE;
        req.h.pid  = getpid();
        strncpy(req.h.name, nombre, MAX_FAMILY-1);
        req.hour   = h;
        req.people = pers;
        strncpy(req.family, fam, MAX_FAMILY-1);
        write(fd_server, &req, sizeof(req));

        /* esperar respuesta */
        msg_response_t resp;
        read(fd_back, &resp, sizeof(resp));

        if (resp.ok == 1)
            printf("[Agente %s] Reserva OK para %s: %d-%d\n",
                   nombre, fam, resp.hour1, resp.hour2);
        else if (resp.ok == 0)
            printf("[Agente %s] Reprogramada %s a %d-%d\n",
                   nombre, fam, resp.hour1, resp.hour2);
        else
            printf("[Agente %s] %s: %s\n", nombre, fam, resp.reason);

        sleep(2);
    }
    fclose(f);
    close(fd_server);
    close(fd_back);
    unlink(pipe_back);
    printf("Agente %s termina.\n", nombre);
    return 0;
}