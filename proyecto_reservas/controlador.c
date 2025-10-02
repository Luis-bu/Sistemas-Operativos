// controlador.c
// Simulador de reservas - Servidor (Controlador de Reserva)
// Compilar: make controlador
// Ejecutar:  ./controlador -i <horaIni> -f <horaFin> -s <segHoras> -t <aforoMax> -p <pipeRecibe>

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
#include <pthread.h>
#include <getopt.h>
#include <signal.h>

#define HORA_MIN 7
#define HORA_MAX 19
#define DURACION_RESERVA 2

// ========================= Utilidades =========================

static void die(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

static char *trim(char *s) {
    if (!s) return s;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

static int clamp(int v, int lo, int hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static void logmsg(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

// ========================= Estructuras =========================

typedef struct Agent {
    char name[64];
    char reply_pipe[256];
    struct Agent *next;
} Agent;

typedef struct Reservation {
    char family[64];
    int people;
    int start;      // hora de inicio (inclusiva)
    int end;        // hora de fin (exclusiva)
    struct Reservation *next;
} Reservation;

typedef struct {
    int horaIni, horaFin; // rango de simulación
    int segHora;          // seg reales por hora simulada
    int aforoMax;         // aforo por hora
    char pipeRecibe[256]; // FIFO principal (Agentes -> Controlador)
} Config;

// ========================= Estado global =========================

static Config g_cfg;
static int g_currentHour = 0;
static bool g_running = true;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

static int g_occupancy[HORA_MAX + 1];     // ocupación por hora
static Reservation *g_resv_head = NULL;   // reservas confirmadas
static Agent *g_agents = NULL;            // agentes registrados

// Métricas reporte
static int g_cnt_negadas_extemp = 0;
static int g_cnt_negadas_out_of_range = 0;
static int g_cnt_negadas_over_cap = 0;
static int g_cnt_negadas_no_block = 0;
static int g_cnt_aceptadas_en_hora = 0;
static int g_cnt_reprogramadas = 0;

// ========================= Helpers de estado =========================

static Agent* find_or_add_agent(const char *name, const char *reply_pipe_opt /*nullable*/) {
    Agent *p = g_agents;
    while (p) {
        if (strcmp(p->name, name) == 0) return p;
        p = p->next;
    }
    if (!reply_pipe_opt) return NULL;
    Agent *a = (Agent*)calloc(1, sizeof(Agent));
    strncpy(a->name, name, sizeof(a->name)-1);
    strncpy(a->reply_pipe, reply_pipe_opt, sizeof(a->reply_pipe)-1);
    a->next = g_agents;
    g_agents = a;
    return a;
}

static void add_reservation(const char *family, int people, int start) {
    Reservation *r = (Reservation*)calloc(1, sizeof(Reservation));
    strncpy(r->family, family, sizeof(r->family)-1);
    r->people = people;
    r->start = start;
    r->end = start + DURACION_RESERVA;
    r->next = g_resv_head;
    g_resv_head = r;

    for (int h = start; h < r->end && h <= HORA_MAX; ++h) {
        g_occupancy[h] += people;
    }
}

static bool block_is_available(int start, int people) {
    if (start < HORA_MIN || start + DURACION_RESERVA - 1 > HORA_MAX) return false;
    for (int h = start; h < start + DURACION_RESERVA; ++h) {
        if (g_occupancy[h] + people > g_cfg.aforoMax) return false;
    }
    return true;
}

static int find_next_available_block(int fromHour, int people) {
    for (int h = fromHour; h <= g_cfg.horaFin - (DURACION_RESERVA - 1); ++h) {
        if (block_is_available(h, people)) return h;
    }
    return -1;
}

// envía una línea a un pipe de respuesta
static void send_line_to_pipe(const char *pipe_path, const char *line) {
    int fd = open(pipe_path, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        fd = open(pipe_path, O_WRONLY);
        if (fd == -1) {
            fprintf(stderr, "[SERVIDOR] No pude abrir pipe de respuesta '%s': %s\n",
                    pipe_path, strerror(errno));
            return;
        }
    }
    size_t len = strlen(line);
    if (write(fd, line, len) < 0) {
        fprintf(stderr, "[SERVIDOR] Error escribiendo a '%s': %s\n",
                pipe_path, strerror(errno));
    }
    if (len == 0 || line[len-1] != '\n') {
        const char nl = '\n';
        (void)write(fd, &nl, 1);
    }
    close(fd);
}

// ========================= Protocolo =========================

static void handle_hello(char *agentName, char *replyPipe) {
    pthread_mutex_lock(&g_mtx);
    Agent *ag = find_or_add_agent(agentName, replyPipe);
    int hora = g_currentHour;
    pthread_mutex_unlock(&g_mtx);

    if (ag) {
        char msg[128];
        snprintf(msg, sizeof(msg), "TIME:%d\n", hora);
        send_line_to_pipe(ag->reply_pipe, msg);
    }
}

static void handle_request(char *agentName, char *family, int hora, int people) {
    pthread_mutex_lock(&g_mtx);
    Agent *ag = find_or_add_agent(agentName, NULL);
    const char *reply = (ag ? ag->reply_pipe : NULL);

    // Fuera de rango
    if (hora > g_cfg.horaFin || hora < HORA_MIN || hora > HORA_MAX) {
        g_cnt_negadas_out_of_range++;
        pthread_mutex_unlock(&g_mtx);
        if (reply) {
            char msg[256]; snprintf(msg, sizeof(msg), "RESP:NEG_OUT_OF_RANGE:%s\n", family);
            send_line_to_pipe(reply, msg);
        }
        return;
    }

    // Extemporánea
    if (hora < g_currentHour) {
        int alt = find_next_available_block(clamp(g_currentHour, HORA_MIN, HORA_MAX), people);
        if (alt != -1 && alt + DURACION_RESERVA - 1 <= g_cfg.horaFin) {
            add_reservation(family, people, alt);
            g_cnt_reprogramadas++;
            int start_assigned = alt;
            pthread_mutex_unlock(&g_mtx);
            if (reply) {
                char msg[256];
                snprintf(msg, sizeof(msg), "RESP:REPROG:%s:%d\n", family, start_assigned);
                send_line_to_pipe(reply, msg);
            }
            return;
        } else {
            g_cnt_negadas_extemp++;
            pthread_mutex_unlock(&g_mtx);
            if (reply) {
                char msg[256]; snprintf(msg, sizeof(msg), "RESP:NEG_EXTEMP:%s\n", family);
                send_line_to_pipe(reply, msg);
            }
            return;
        }
    }

    // Personas > aforo
    if (people > g_cfg.aforoMax) {
        g_cnt_negadas_over_cap++;
        pthread_mutex_unlock(&g_mtx);
        if (reply) {
            char msg[256]; snprintf(msg, sizeof(msg), "RESP:NEG_OVER_CAP:%s\n", family);
            send_line_to_pipe(reply, msg);
        }
        return;
    }

    // Intentar en hora solicitada
    if (hora + DURACION_RESERVA - 1 <= g_cfg.horaFin && block_is_available(hora, people)) {
        add_reservation(family, people, hora);
        g_cnt_aceptadas_en_hora++;
        int start_assigned = hora;
        pthread_mutex_unlock(&g_mtx);
        if (reply) {
            char msg[256]; snprintf(msg, sizeof(msg), "RESP:OK:%s:%d\n", family, start_assigned);
            send_line_to_pipe(reply, msg);
        }
        return;
    }

    // Reprogramar dentro del rango
    int fromH = (hora < g_currentHour) ? g_currentHour : hora;
    int alt = find_next_available_block(fromH, people);
    if (alt != -1 && alt + DURACION_RESERVA - 1 <= g_cfg.horaFin) {
        add_reservation(family, people, alt);
        g_cnt_reprogramadas++;
        int start_assigned = alt;
        pthread_mutex_unlock(&g_mtx);
        if (reply) {
            char msg[256]; snprintf(msg, sizeof(msg), "RESP:REPROG:%s:%d\n", family, start_assigned);
            send_line_to_pipe(reply, msg);
        }
        return;
    }

    // No hay bloque
    g_cnt_negadas_no_block++;
    pthread_mutex_unlock(&g_mtx);
    if (reply) {
        char msg[256]; snprintf(msg, sizeof(msg), "RESP:NEG_NO_BLOCK:%s\n", family);
        send_line_to_pipe(reply, msg);
    }
}

// ========================= Reloj / Simulación =========================

static void print_hour_transition(int newHour) {
    int salen = 0, entran = 0;
    char buf_salen[1024] = {0}, buf_entran[1024] = {0};
    size_t ps = 0, pe = 0;

    for (Reservation *r = g_resv_head; r; r = r->next) {
        if (r->end == newHour) {
            int n = snprintf(buf_salen + ps, sizeof(buf_salen) - ps, "%s(%d) ", r->family, r->people);
            if (n > 0) ps += (size_t)n;
            salen += r->people;
        }
        if (r->start == newHour) {
            int n = snprintf(buf_entran + pe, sizeof(buf_entran) - pe, "%s(%d) ", r->family, r->people);
            if (n > 0) pe += (size_t)n;
            entran += r->people;
        }
    }

    logmsg("[SERVIDOR] ===== Ha transcurrido una hora. Son las %d hr. =====\n", newHour);
    logmsg("[SERVIDOR] Salen: %d personas. Familias: %s\n", salen, ps ? buf_salen : "(ninguna)");
    logmsg("[SERVIDOR] Entran: %d personas. Familias: %s\n", entran, pe ? buf_entran : "(ninguna)");

    int occ = (newHour >= HORA_MIN && newHour <= HORA_MAX) ? g_occupancy[newHour] : 0;
    logmsg("[SERVIDOR] Aforo en hora %d: %d / %d\n", newHour, occ, g_cfg.aforoMax);
}

static void* clock_thread(void *arg) {
    (void)arg;
    while (1) {
        sleep(g_cfg.segHora);

        pthread_mutex_lock(&g_mtx);
        if (!g_running) { pthread_mutex_unlock(&g_mtx); break; }

        int nextHour = g_currentHour + 1;
        g_currentHour = nextHour;

        print_hour_transition(nextHour);

        bool finish = (nextHour > g_cfg.horaFin);
        if (finish) {
            // marca fin de simulación; NO manda END ni reporte aquí
            g_running = false;
            pthread_mutex_unlock(&g_mtx);
            break;
        }
        pthread_mutex_unlock(&g_mtx);
    }
    return NULL;
}

// ========================= Lector FIFO =========================

static void process_line(char *line) {
    char *s = trim(line);
    if (*s == 0) return;

    if (strncmp(s, "HELLO:", 6) == 0) {
        s += 6;
        char *tok1 = strtok(s, ":");
        char *tok2 = strtok(NULL, ":");
        if (!tok1 || !tok2) return;
        handle_hello(trim(tok1), trim(tok2));
        return;
    }
    if (strncmp(s, "REQ:", 4) == 0) {
        s += 4;
        char *tokA = strtok(s, ":");
        char *tokF = strtok(NULL, ":");
        char *tokH = strtok(NULL, ":");
        char *tokP = strtok(NULL, ":");
        if (!tokA || !tokF || !tokH || !tokP) return;
        int hora = atoi(tokH);
        int ppl  = atoi(tokP);

        pthread_mutex_lock(&g_mtx);
        logmsg("[SERVIDOR] Petición de %s: familia=%s, hora=%d, personas=%d\n",
               tokA, tokF, hora, ppl);
        pthread_mutex_unlock(&g_mtx);

        handle_request(trim(tokA), trim(tokF), hora, ppl);
        return;
    }
}

static void* reader_thread(void *arg) {
    (void)arg;
    char buf[1024];

    while (1) {
        int fd = open(g_cfg.pipeRecibe, O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "[SERVIDOR] open('%s') fallo: %s\n", g_cfg.pipeRecibe, strerror(errno));
            sleep(1);
            continue;
        }

        ssize_t n;
        size_t acc = 0;
        char accbuf[4096]; accbuf[0] = '\0';

        while ((n = read(fd, buf, sizeof(buf)-1)) > 0) {
            buf[n] = '\0';
            for (ssize_t i = 0; i < n; ++i) {
                if (acc < sizeof(accbuf)-1) {
                    accbuf[acc++] = buf[i];
                    accbuf[acc] = '\0';
                }
                if (buf[i] == '\n') {
                    process_line(accbuf);
                    acc = 0; accbuf[0] = '\0';
                }
            }
        }
        close(fd);

        if (acc > 0) process_line(accbuf);

        // si ya pidieron parar, salimos
        if (!g_running) break;
    }
    return NULL;
}

// ========================= END & Reporte =========================

static void broadcast_end_to_agents(void) {
    Agent *a = g_agents;
    while (a) {
        send_line_to_pipe(a->reply_pipe, "END\n");
        a = a->next;
    }
}

static void generate_report(void) {
    int maxOcc = -1, minOcc = 1<<30;
    for (int h = HORA_MIN; h <= HORA_MAX; ++h) {
        if (g_occupancy[h] > maxOcc) maxOcc = g_occupancy[h];
        if (g_occupancy[h] < minOcc) minOcc = g_occupancy[h];
    }
    logmsg("\n[SERVIDOR] ===== REPORTE FINAL =====\n");
    logmsg("Horas pico (aforo=%d): ", maxOcc);
    for (int h = HORA_MIN; h <= HORA_MAX; ++h) if (g_occupancy[h] == maxOcc) logmsg("%d ", h);
    logmsg("\nHoras mínimas (aforo=%d): ", minOcc);
    for (int h = HORA_MIN; h <= HORA_MAX; ++h) if (g_occupancy[h] == minOcc) logmsg("%d ", h);
    logmsg("\nSolicitudes negadas (extemporáneas): %d\n", g_cnt_negadas_extemp);
    logmsg("Solicitudes aceptadas en su hora: %d\n", g_cnt_aceptadas_en_hora);
    logmsg("Solicitudes reprogramadas: %d\n", g_cnt_reprogramadas);
    logmsg("Solicitudes negadas (fuera de rango): %d\n", g_cnt_negadas_out_of_range);
    logmsg("Solicitudes negadas (personas > aforo): %d\n", g_cnt_negadas_over_cap);
    logmsg("Solicitudes negadas (sin bloque disponible): %d\n", g_cnt_negadas_no_block);
    logmsg("=====================================\n");
}

// ========================= Main =========================

static void uso(const char *prog) {
    fprintf(stderr,
        "Uso: %s -i <horaIni> -f <horaFin> -s <segHoras> -t <aforoMax> -p <pipeRecibe>\n"
        "  Rango valido horas: %d..%d, reserva dura %d horas.\n",
        prog, HORA_MIN, HORA_MAX, DURACION_RESERVA);
}

int main(int argc, char *argv[]) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    int opt;
    bool ok_i=false, ok_f=false, ok_s=false, ok_t=false, ok_p=false;
    while ((opt = getopt(argc, argv, "i:f:s:t:p:")) != -1) {
        switch (opt) {
            case 'i': g_cfg.horaIni = atoi(optarg); ok_i=true; break;
            case 'f': g_cfg.horaFin = atoi(optarg); ok_f=true; break;
            case 's': g_cfg.segHora = atoi(optarg); ok_s=true; break;
            case 't': g_cfg.aforoMax = atoi(optarg); ok_t=true; break;
            case 'p': strncpy(g_cfg.pipeRecibe, optarg, sizeof(g_cfg.pipeRecibe)-1); ok_p=true; break;
            default: uso(argv[0]); return EXIT_FAILURE;
        }
    }
    if (!(ok_i && ok_f && ok_s && ok_t && ok_p)) { uso(argv[0]); return EXIT_FAILURE; }
    if (g_cfg.horaIni < HORA_MIN || g_cfg.horaIni > HORA_MAX ||
        g_cfg.horaFin < HORA_MIN || g_cfg.horaFin > HORA_MAX ||
        g_cfg.horaIni > g_cfg.horaFin || g_cfg.segHora <= 0 || g_cfg.aforoMax <= 0) {
        uso(argv[0]); return EXIT_FAILURE;
    }

    g_currentHour = g_cfg.horaIni;

    if (mkfifo(g_cfg.pipeRecibe, 0666) == -1) {
        if (errno != EEXIST) die("mkfifo('%s'): %s", g_cfg.pipeRecibe, strerror(errno));
    }

    logmsg("[SERVIDOR] Iniciado. horaIni=%d, horaFin=%d, segHora=%d, aforo=%d, pipe=%s\n",
           g_cfg.horaIni, g_cfg.horaFin, g_cfg.segHora, g_cfg.aforoMax, g_cfg.pipeRecibe);

    pthread_t thr_reader, thr_clock;
    if (pthread_create(&thr_reader, NULL, reader_thread, NULL) != 0) die("pthread_create(reader) fallo");
    if (pthread_create(&thr_clock, NULL, clock_thread, NULL) != 0)  die("pthread_create(clock) fallo");

    // Esperar fin del reloj (termina simulación y marca g_running=false)
    pthread_join(thr_clock, NULL);

    // Asegurar bandera de fin
    pthread_mutex_lock(&g_mtx);
    g_running = false;
    pthread_mutex_unlock(&g_mtx);

    // Destrabar lector si está bloqueado
    int fd_unblock = open(g_cfg.pipeRecibe, O_WRONLY | O_NONBLOCK);
    if (fd_unblock != -1) close(fd_unblock);

    // Esperar a que el lector termine (ya no se procesan más REQs)
    pthread_join(thr_reader, NULL);

    // Ahora sí: avisar a agentes que terminó la simulación
    broadcast_end_to_agents();

    // Y recién ahora: imprimir reporte final
    generate_report();

    logmsg("[SERVIDOR] Terminando.\n");
    return 0;
}
