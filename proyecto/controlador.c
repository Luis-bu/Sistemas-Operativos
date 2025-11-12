/* controlador.c */
#include "common.h"
#include "utils.h"

hour_slot_t park[HOURS_DAY];
int current_hour = 0;          /* índice 0 = 7h */
int aforo_max = 0;
volatile sig_atomic_t finish = 0;
char pipe_server[MAX_PIPE_NAME];
int seg_per_hour = 0;

/* estadísticas */
int stats_accepted = 0;
int stats_reprogram = 0;
int stats_denied = 0;

/* tabla de pipes de respuesta */
typedef struct {
    int  pid;
    char pipe_name[MAX_PIPE_NAME];
} client_t;
client_t clients[MAX_AGENTS];
int nclients = 0;

/* prototipos */
void *clock_thread(void *);
void *listener_thread(void *);
void send_time(int agent_pid);
void process_reserve(msg_reserve_t *req);
void advance_hour(void);
void final_report(void);
void cleanup(void);
void sig_handler(int sig) { (void)sig; finish = 1; }  // <-- (void)sig elimina warning

int main(int argc, char *argv[])
{
    int opt, horaIni = -1, horaFin = -1;
    while ((opt = getopt(argc, argv, "i:f:s:t:p:")) != -1) {
        switch (opt) {
            case 'i': horaIni = atoi(optarg); break;
            case 'f': horaFin = atoi(optarg); break;
            case 's': seg_per_hour = atoi(optarg); break;
            case 't': aforo_max = atoi(optarg); break;
            case 'p': strncpy(pipe_server, optarg, MAX_PIPE_NAME-1); break;
            default: fprintf(stderr,
                    "Uso: %s -i horaIni -f horaFin -s segHoras -t total -p pipeRecibe\n",
                    argv[0]); exit(EXIT_FAILURE);
        }
    }
    if (horaIni < MIN_HOUR || horaIni > MAX_HOUR ||
        horaFin < MIN_HOUR || horaFin > MAX_HOUR ||
        horaIni > horaFin || seg_per_hour <= 0 || aforo_max <= 0 ||
        pipe_server[0] == 0) {
        fprintf(stderr, "Parámetros inválidos\n");
        exit(EXIT_FAILURE);
    }

    current_hour = hour_index(horaIni);

    /* crear pipe servidor */
    unlink(pipe_server);
    check(mkfifo(pipe_server, 0644), "mkfifo server");
    int fd_server = open(pipe_server, O_RDONLY | O_NONBLOCK);
    check(fd_server, "open server pipe");

    /* señal para terminar limpiamente */
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    atexit(cleanup);

    /* hilo reloj y listener */
    pthread_t th_clock, th_listener;
    int *fd_ptr = &fd_server;  // <-- pasar puntero
    pthread_create(&th_clock, NULL, clock_thread, fd_ptr);
    pthread_create(&th_listener, NULL, listener_thread, fd_ptr);

    pthread_join(th_clock, NULL);
    pthread_join(th_listener, NULL);
    return 0;
}

/* ----------------- RELOJ ----------------- */
void *clock_thread(void *arg)
{
    int fd_server = *(int *)arg;  // <-- usamos el fd
    (void)fd_server;  // evitar warning si no se usa más

    while (!finish) {
        sleep(seg_per_hour);
        if (finish) break;
        advance_hour();
        /* avisar a los agentes que la hora cambió */
        for (int i = 0; i < nclients; ++i) {
            int fd = open(clients[i].pipe_name, O_WRONLY);
            if (fd >= 0) {
                msg_header_t h = { .type = MSG_TIME, .pid = getpid() };
                write(fd, &h, sizeof(h));
                int cur = current_hour + MIN_HOUR;
                write(fd, &cur, sizeof(cur));
                close(fd);
            }
        }
    }
    return NULL;
}

/* ----------------- LISTENER ----------------- */
void *listener_thread(void *arg)
{
    int fd_server = *(int *)arg;
    char buf[MAX_MSG];
    while (!finish) {
        int r = read(fd_server, buf, sizeof(buf));
        if (r > 0) {
            msg_header_t *hdr = (msg_header_t *)buf;
            switch (hdr->type) {
                case MSG_REGISTER: {
                    msg_register_t *reg = (msg_register_t *)buf;
                    if (nclients < MAX_AGENTS) {
                        clients[nclients].pid = reg->h.pid;
                        strncpy(clients[nclients].pipe_name,
                                reg->pipe_back, MAX_PIPE_NAME-1);
                        nclients++;
                        send_time(reg->h.pid);
                    }
                    break;
                }
                case MSG_RESERVE: {
                    msg_reserve_t *req = (msg_reserve_t *)buf;
                    process_reserve(req);  // <-- quitamos fd_server
                    break;
                }
                default:
                    break;
            }
        }
        usleep(100000);   // <-- ahora sí está declarado (ver abajo)
    }
    return NULL;
}

/* enviar hora actual */
void send_time(int agent_pid)
{
    for (int i = 0; i < nclients; ++i) {
        if (clients[i].pid == agent_pid) {
            int fd = open(clients[i].pipe_name, O_WRONLY);
            if (fd >= 0) {
                msg_header_t h = { .type = MSG_TIME, .pid = getpid() };
                int cur = current_hour + MIN_HOUR;
                write(fd, &h, sizeof(h));
                write(fd, &cur, sizeof(cur));
                close(fd);
            }
            break;
        }
    }
}

/* procesar reserva */
void process_reserve(msg_reserve_t *req)  // <-- quitamos fd_server
{
    printf("[Controlador] Recibida petición de %s: hora %d, %d personas\n",
           req->h.name, req->hour, req->people);

    msg_response_t resp = {0};
    resp.h.type = MSG_RESPONSE;
    resp.h.pid  = getpid();
    strncpy(resp.h.name, req->h.name, MAX_FAMILY-1);

    int now = current_hour + MIN_HOUR;
    if (req->hour < now) {
        resp.ok = 0;
        strcpy(resp.reason, "Reserva negada por extemporánea");
        goto reprogram;
    }

    if (req->people > aforo_max) {
        resp.ok = -1;
        strcpy(resp.reason, "Número de personas excede aforo");
        goto send;
    }

    int idx = hour_index(req->hour);
    int idx2 = (idx + 1 < HOURS_DAY) ? idx + 1 : -1;

    if (idx2 == -1 || park[idx].people + req->people <= aforo_max &&
        park[idx2].people + req->people <= aforo_max) {
        park[idx].people += req->people;
        strncpy(park[idx].families[park[idx].nfam++], req->family, MAX_FAMILY-1);
        if (idx2 != -1) {
            park[idx2].people += req->people;
            strncpy(park[idx2].families[park[idx2].nfam++], req->family, MAX_FAMILY-1);
        }
        resp.ok = 1;
        resp.hour1 = req->hour;
        resp.hour2 = (idx2 == -1) ? -1 : req->hour + 1;
        stats_accepted++;
        goto send;
    }

reprogram:
    for (int h = now; h <= MAX_HOUR-1; ++h) {
        int i1 = hour_index(h);
        int i2 = i1 + 1;
        if (park[i1].people + req->people <= aforo_max &&
            park[i2].people + req->people <= aforo_max) {
            park[i1].people += req->people;
            strncpy(park[i1].families[park[i1].nfam++], req->family, MAX_FAMILY-1);
            park[i2].people += req->people;
            strncpy(park[i2].families[park[i2].nfam++], req->family, MAX_FAMILY-1);
            resp.ok = 0;
            resp.hour1 = h;
            resp.hour2 = h + 1;
            stats_reprogram++;
            goto send;
        }
    }

    resp.ok = -1;
    strcpy(resp.reason, "Reserva negada, debe volver otro día");
    stats_denied++;

send:
    for (int i = 0; i < nclients; ++i) {
        if (clients[i].pid == req->h.pid) {
            int fd = open(clients[i].pipe_name, O_WRONLY);
            if (fd >= 0) {
                write(fd, &resp, sizeof(resp));
                close(fd);
            }
            break;
        }
    }
}

/* avanzar hora */
void advance_hour(void)
{
    int hora = current_hour + MIN_HOUR;
    printf("\n=== HORA %02d:00 ===\n", hora);

    if (current_hour > 0) {
        int prev = current_hour - 1;
        printf("Salen %d personas (%d familias)\n",
               park[prev].people, park[prev].nfam);
        park[prev].people = park[prev].nfam = 0;
    }

    printf("Entran %d personas (%d familias)\n",
           park[current_hour].people, park[current_hour].nfam);

    current_hour++;
    if (current_hour + MIN_HOUR > MAX_HOUR) {
        finish = 1;
        final_report();
    }
}

/* reporte final */
void final_report(void)
{
    printf("\n=== REPORTE FINAL ===\n");
    int maxp = 0, minp = 1<<30;
    int hora_max[HOURS_DAY], hora_min[HOURS_DAY];
    int nmax = 0, nmin = 0;

    for (int i = 0; i < HOURS_DAY; ++i) {
        int p = park[i].people;
        int h = i + MIN_HOUR;
        if (p > maxp) { maxp = p; nmax = 0; hora_max[nmax++] = h; }
        else if (p == maxp) hora_max[nmax++] = h;

        if (p < minp) { minp = p; nmin = 0; hora_min[nmin++] = h; }
        else if (p == minp) hora_min[nmin++] = h;
    }

    printf("Horas pico (máx %d personas): ", maxp);
    for (int i = 0; i < nmax; i++) printf("%d ", hora_max[i]);
    printf("\n");

    printf("Horas valle (mín %d personas): ", minp);
    for (int i = 0; i < nmin; i++) printf("%d ", hora_min[i]);
    printf("\n");

    printf("Solicitudes aceptadas en hora: %d\n", stats_accepted);
    printf("Solicitudes reprogramadas: %d\n", stats_reprogram);
    printf("Solicitudes denegadas: %d\n", stats_denied);
}

/* limpieza */
void cleanup(void)
{
    unlink(pipe_server);
    for (int i = 0; i < nclients; ++i) {
        int fd = open(clients[i].pipe_name, O_WRONLY);
        if (fd >= 0) {
            msg_header_t h = { .type = MSG_END };
            write(fd, &h, sizeof(h));
            close(fd);
        }
        unlink(clients[i].pipe_name);
    }
}