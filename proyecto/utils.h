/* utils.h */
#ifndef UTILS_H
#define UTILS_H
#include "common.h"
void die(const char *msg);
void check(int r, const char *msg);
int hour_index(int h);
int valid_hour(int h);
#endif