/* Small helpers shared by every module: byte order and the clock. */
#ifndef FGG_UTIL_H
#define FGG_UTIL_H

#include <stdint.h>
#include <time.h>

/* Milliseconds on the monotonic clock. */
static inline long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Bluetooth HCI and L2CAP are little-endian; RTP is big-endian. */
static inline unsigned le16(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static inline void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)(v >> 8);
}

static inline void put16be(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)(v & 0xFF);
}

static inline void put32be(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

#endif
