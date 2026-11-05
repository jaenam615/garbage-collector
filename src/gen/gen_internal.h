#ifndef GEN_INTERNAL_H
#define GEN_INTERNAL_H

/* gen.c, minor.c, major.c가 같이 쓰는 정의 */

#include "gen.h"
#include "heap.h"

struct GenGC {
    int todo; /* TODO: Eden, Survivor 두 개, Old 힙, 루트, remembered set ... */
};

#endif
