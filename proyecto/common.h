/* common.h */
#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define MAX_AGENTS      32
#define MAX_FAMILY      64
#define MAX_PIPE_NAME   256
#define MAX_MSG         512
#define HOURS_DAY       (19-7+1)   /* 7 … 19 */
#define MIN_HOUR        7
#define MAX_HOUR        19

/* Mensajes entre procesos */
typedef enum {
    MSG_REGISTER,      /* agente → controlador */
    MSG_TIME,          /* controlador → agente (hora actual) */
    MSG_RESERVE,       /* agente → controlador */
    MSG_RESPONSE,      /* controlador → agente */
    MSG_END            /* controlador → agentes */
} msg_type_t;

/* Cabecera común */
typedef struct {
    msg_type_t type;
    int        pid;            /* PID del emisor */
    char       name[MAX_FAMILY];
} msg_header_t;

/* Registro */
typedef struct {
    msg_header_t h;
    char         pipe_back[MAX_PIPE_NAME];
} msg_register_t;

/* Solicitud de reserva */
typedef struct {
    msg_header_t h;
    int          hour;         /* hora solicitada (7-19) */
    int          people;
    char         family[MAX_FAMILY];
} msg_reserve_t;

/* Respuesta */
typedef struct {
    msg_header_t h;
    int          ok;           /* 1=ok, 0=reprogram, -1=denied */
    int          hour1;        /* hora asignada (o -1) */
    int          hour2;        /* hora asignada +1 (o -1) */
    char         reason[128];
} msg_response_t;

/* Estructura de ocupación por hora */
typedef struct {
    int  people;
    char families[32][MAX_FAMILY];   /* max 32 familias por hora */
    int  nfam;
} hour_slot_t;

extern hour_slot_t park[HOURS_DAY];
extern int current_hour;
extern int aforo_max;
extern volatile sig_atomic_t finish;

#endif