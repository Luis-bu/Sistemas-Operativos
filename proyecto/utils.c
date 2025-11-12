/* utils.c */
#include "utils.h"

void die(const char *msg) { perror(msg); exit(EXIT_FAILURE); }

void check(int r, const char *msg)
{
    if (r < 0) die(msg);
}

int hour_index(int h)
{
    return h - MIN_HOUR;               /* 0 … 12 */
}

int valid_hour(int h)
{
    return h >= MIN_HOUR && h <= MAX_HOUR;
}