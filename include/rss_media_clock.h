/*
 * rss_media_clock.h -- map producer video timestamps to CLOCK_MONOTONIC
 */

#ifndef RSS_MEDIA_CLOCK_H
#define RSS_MEDIA_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#define RSS_MEDIA_CLOCK_BUCKETS 16

typedef struct {
    int64_t bucket_min_us[RSS_MEDIA_CLOCK_BUCKETS];
    int64_t bucket_start_us;
    int64_t offset_us;
    int64_t err_ewma_us;
    uint8_t bucket;
    bool initialized;
} rss_media_clock_t;

void rss_media_clock_init(rss_media_clock_t *clock);

/*
 * Map one producer timestamp into CLOCK_MONOTONIC. `now_us` is sampled
 * after the frame reaches the ring reader, so now-media contains the
 * clock-domain offset plus non-negative delivery and scheduling delay.
 */
int64_t rss_media_clock_map(rss_media_clock_t *clock, int64_t media_us, int64_t now_us);

static inline bool rss_media_clock_ready(const rss_media_clock_t *clock)
{
    return clock->initialized;
}

static inline int64_t rss_media_clock_offset(const rss_media_clock_t *clock)
{
    return clock->offset_us;
}

#endif /* RSS_MEDIA_CLOCK_H */
