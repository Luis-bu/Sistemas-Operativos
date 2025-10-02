// agente.c
// Simulador de reservas - Cliente (Agente de Reserva)
// Compilar: make agente
// Ejecutar:  ./agente -s <nombreAgente> -a <archivoSolicitudes> -p <pipeRecibe>
//
// CSV: lineas "Familia,Hora,Personas" y cierre "Fin,0,0"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>

#define HORA_MIN 7
#define HORA_MAX 19

static void die(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n"); exit(EXIT_FAILURE);
}

static char *trim(char *s) {
    if (!s) return s;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s)-1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

typedef struct {
    char family[64];
    int hour;
    int people;
} Row;

static bool parse_csv_line(const char *line, Row *out) {
    char tmp[256]; strncpy(tmp, line, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = 0;
    char *p = tmp;
    char *a = strtok(p, ",");
    char *b = strtok(NULL, ",");
    char *c = strtok(NULL, ",");
    if (!a || !b || !c) return false;
    strncpy(out->family, trim(a), sizeof(out->family)-1);
    out->hour   = atoi(trim(b));
    out->people = atoi(trim(c));
    return true;
}

int main(int argc, char *argv[]) {
    const char *nombre = NULL;
    const char *fileSolicitud = NULL;
    const char *pipeRecibe = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "s:a:p:")) != -1) {
        switch (opt) {
            case 's': nombre = optarg; break;
            case 'a': fileSolicitud = optarg; break;
            case 'p': pipeRecibe = optarg; break;
            default:
                fprintf(stderr, "Uso: %s -s <nombreAgente> -a <archivoSolicitudes> -p <pipeRecibe>\n", argv[0]);
                return EXIT_FAILURE;
        }
    }
    if (!nombre || !fileSolicitud || !pipeRecibe) {
        fprintf(stderr, "Uso: %s -s <nombreAgente> -a <archivoSolicitudes> -p <pipeRecibe>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char reply_pipe[256];
    snprintf(reply_pipe, sizeof(reply_pipe), "pipe_resp_%s", nombre);

    if (mkfifo(reply_pipe, 0666) == -1) {
        if (errno != EEXIST) die("mkfifo('%s'): %s", reply_pipe, strerror(errno));
    }

    // Registrar con el servidor
    int fd_w = open(pipeRecibe, O_WRONLY);
    if (fd_w == -1) die("No pude abrir pipeRecibe '%s' para escribir: %s", pipeRecibe, strerror(errno));

    char hello[512];
    snprintf(hello, sizeof(hello), "HELLO:%s:%s\n", nombre, reply_pipe);
    if (write(fd_w, hello, strlen(hello)) < 0) die("write(HELLO) fallo: %s", strerror(errno));

    // Abrir pipe de respuesta (no bloqueante para leer TIME inicial)
    int fd_r = open(reply_pipe, O_RDONLY | O_NONBLOCK);
    if (fd_r == -1) die("No pude abrir mi reply_pipe '%s': %s", reply_pipe, strerror(errno));

    // Esperar TIME:<hora>
    int horaActual = HORA_MIN;
    {
        char buf[512]; ssize_t n;
        for (int tries = 0; tries < 50; ++tries) {
            n = read(fd_r, buf, sizeof(buf)-1);
            if (n > 0) {
                buf[n] = 0;
                char *line = strtok(buf, "\n");
                while (line) {
                    if (strncmp(line, "TIME:", 5) == 0) {
                        horaActual = atoi(line + 5);
                        break;
                    }
                    line = strtok(NULL, "\n");
                }
                break;
            }
            usleep(100 * 1000);
        }
    }
    printf("[AGENTE %s] Hora actual recibida: %d\n", nombre, horaActual);

    // Reabrir en NO BLOQUEANTE para evitar deadlock durante el flujo normal
    close(fd_r);
    fd_r = open(reply_pipe, O_RDONLY | O_NONBLOCK);
    if (fd_r == -1) die("No pude reabrir reply_pipe '%s': %s", reply_pipe, strerror(errno));

    FILE *fp = fopen(fileSolicitud, "r");
    if (!fp) die("No pude abrir archivo '%s': %s", fileSolicitud, strerror(errno));

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        Row row;
        if (!parse_csv_line(line, &row)) continue;
        if (strcasecmp(row.family, "Fin") == 0) break;

        if (row.hour < horaActual) {
            printf("[AGENTE %s] Solicitud %s@%d ignorada (extemporánea, horaActual=%d)\n",
                   nombre, row.family, row.hour, horaActual);
            continue;
        }

        char req[512];
        snprintf(req, sizeof(req), "REQ:%s:%s:%d:%d\n", nombre, row.family, row.hour, row.people);
        if (write(fd_w, req, strlen(req)) < 0) {
            fprintf(stderr, "[AGENTE %s] Error enviando REQ: %s\n", nombre, strerror(errno));
            break;
        }
        printf("[AGENTE %s] Enviada solicitud: familia=%s, hora=%d, personas=%d\n",
               nombre, row.family, row.hour, row.people);

        // Esperar RESP (o END), sondeando el FIFO no bloqueante
        while (1) {
            char rbuf[512];
            ssize_t n = read(fd_r, rbuf, sizeof(rbuf)-1);
            if (n <= 0) { usleep(100 * 1000); continue; }

            rbuf[n] = 0;
            char *ln = strtok(rbuf, "\n");
            bool gotResp = false;
            while (ln) {
                if (strncmp(ln, "END", 3) == 0) {
                    // Pequeña gracia para drenar mensajes pegados
                    usleep(200 * 1000);
                    printf("[AGENTE %s] Recibido END. Terminando.\n", nombre);
                    fclose(fp); close(fd_r); close(fd_w);
                    printf("Agente %s termina.\n", nombre);
                    return 0;
                }
                if (strncmp(ln, "RESP:", 5) == 0) {
                    printf("[AGENTE %s] Respuesta: %s\n", nombre, ln + 5);
                    gotResp = true;
                }
                if (strncmp(ln, "TIME:", 5) == 0) {
                    horaActual = atoi(ln + 5);
                    printf("[AGENTE %s] (update) Hora actual ahora: %d\n", nombre, horaActual);
                }
                ln = strtok(NULL, "\n");
            }
            if (gotResp) break;
        }

        sleep(2); // enunciado: 2s entre solicitudes
    }

    // Esperar END de forma bloqueante (sin timeout)
    printf("[AGENTE %s] No hay más solicitudes. Esperando END del servidor...\n", nombre);

    int flags = fcntl(fd_r, F_GETFL, 0);
    if (flags != -1) fcntl(fd_r, F_SETFL, flags & ~O_NONBLOCK);

    while (1) {
        char rbuf[512];
        ssize_t n = read(fd_r, rbuf, sizeof(rbuf)-1);
        if (n <= 0) continue;  // bloquear hasta que llegue algo
        rbuf[n] = 0;

        char *ln = strtok(rbuf, "\n");
        while (ln) {
            if (strncmp(ln, "END", 3) == 0) {
                printf("[AGENTE %s] Recibido END. Terminando.\n", nombre);
                close(fd_r); close(fd_w);
                printf("Agente %s termina.\n", nombre);
                return 0;
            }
            if (strncmp(ln, "RESP:", 5) == 0) {
                printf("[AGENTE %s] Respuesta: %s\n", nombre, ln + 5);
            }
            if (strncmp(ln, "TIME:", 5) == 0) {
                int h = atoi(ln + 5);
                printf("[AGENTE %s] (update) Hora actual ahora: %d\n", nombre, h);
            }
            ln = strtok(NULL, "\n");
        }
    }
}
