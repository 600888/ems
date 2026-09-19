#pragma once
#include <cstdlib>

typedef struct {
    time_t tm;
    int pid;
    unsigned char initflag;
} ProcessInfo;