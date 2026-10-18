/* dmath.h - demo maths: sine tables, fixed point, fast random */
#ifndef DMATH_H
#define DMATH_H

#include <stdint.h>

/* 1024-step sine/cosine table, values in -32767..32767 (Q15).
 * Angle is in "binary degrees": 1024 = full circle, wraps automatically. */
#define DSIN_STEPS 1024
extern int16_t dsin_table[DSIN_STEPS];

void dmath_init (void);

static inline int
dsin (int a)
{
        return dsin_table[a & (DSIN_STEPS - 1)];
}

static inline int
dcos (int a)
{
        return dsin_table[(a + DSIN_STEPS / 4) & (DSIN_STEPS - 1)];
}

/* 16.16 fixed point */
typedef int32_t fix16_t;
#define FIX16(x)     ((fix16_t) ((x) * 65536.0))
#define FIX16_INT(x) ((x) >> 16)

static inline fix16_t
fix16_mul (fix16_t a, fix16_t b)
{
        return (fix16_t) (((int64_t) a * b) >> 16);
}

/* xorshift32; never seed with 0 */
static inline uint32_t
drand (uint32_t *state)
{
        uint32_t x = *state;

        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return *state = x;
}

#endif
