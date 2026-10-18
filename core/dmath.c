/* dmath.c - demo maths tables */
#include "dmath.h"

#include <math.h>

int16_t dsin_table[DSIN_STEPS];

void
dmath_init (void)
{
        for (int i = 0; i < DSIN_STEPS; i++)
                dsin_table[i] = (int16_t) lrint (sin (i * 2.0 * M_PI / DSIN_STEPS) * 32767.0);
}
