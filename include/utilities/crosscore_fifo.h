// crosscore_fifo.h
#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

// Single-producer / single-consumer (SPSC) lock-free FIFO.
//
// This is the companion to crosscore_snapshot.h. Use a SNAPSHOT when you only
// care about the latest value (last-write-wins). Use a FIFO when every element
// must be preserved, in order, with nothing dropped (e.g. a reliable RX queue).
//
// HOW IT STAYS CORESAFE WITHOUT LOCKS:
//   - There is no shared "count". A shared counter would need an atomic
//     read-modify-write (count++/count--), which the RP2040 (Cortex-M0+) cannot
//     do across cores. Instead each index has exactly one writer.
//   - The producer owns `tail` and is the ONLY writer of `tail`.
//   - The consumer owns `head` and is the ONLY writer of `head`.
//   - Fullness/emptiness is derived from the free-running head/tail counters.
//   - Release/acquire ordering publishes the slot data before the index that
//     exposes it, exactly like the seqlock fences in crosscore_snapshot.h.
//
// HARD REQUIREMENTS:
//   - EXACTLY ONE core may call fifo_<name>_push (the producer core).
//   - EXACTLY ONE core may call fifo_<name>_pop  (the consumer core).
//   - The two cores must be different (or, if the same core does both, you did
//     not need a crosscore primitive in the first place).
//   - `len` MUST be a power of two (so `& (len - 1)` can replace `% len`).
//
// Calling either half from more than one core, or swapping which core owns
// which half at runtime, breaks the single-writer invariant and corrupts the
// queue.
#define CROSSCORE_FIFO_TYPE(name, type, len)                                   \
typedef struct {                                                               \
    type        buf[len];                                                      \
    atomic_uint head; /* free-running; written ONLY by the consumer core */    \
    atomic_uint tail; /* free-running; written ONLY by the producer core */    \
} fifo_##name##_t;                                                             \
                                                                               \
/* PRODUCER-CORE ONLY. Must never be called from the consumer core.            \
 * Returns false (and drops the element) when the FIFO is full. */             \
static inline bool fifo_##name##_push(fifo_##name##_t *f, const type *src) {   \
    unsigned int t = atomic_load_explicit(&f->tail, memory_order_relaxed);     \
    unsigned int h = atomic_load_explicit(&f->head, memory_order_acquire);     \
    if ((unsigned int)(t - h) >= (len)) return false; /* full */               \
    memcpy(&f->buf[t & ((len) - 1u)], src, sizeof(type));                      \
    atomic_store_explicit(&f->tail, t + 1u, memory_order_release);             \
    return true;                                                               \
}                                                                              \
                                                                               \
/* CONSUMER-CORE ONLY. Must never be called from the producer core.            \
 * Returns false when the FIFO is empty. */                                    \
static inline bool fifo_##name##_pop(fifo_##name##_t *f, type *dst) {          \
    unsigned int h = atomic_load_explicit(&f->head, memory_order_relaxed);     \
    unsigned int t = atomic_load_explicit(&f->tail, memory_order_acquire);     \
    if (h == t) return false; /* empty */                                      \
    memcpy(dst, &f->buf[h & ((len) - 1u)], sizeof(type));                      \
    atomic_store_explicit(&f->head, h + 1u, memory_order_release);             \
    return true;                                                               \
}

// -----------------------------------------------------------------------------
// Example usage
// -----------------------------------------------------------------------------
//
// 1) Declare the FIFO type and an instance (len must be a power of two):
//
//      typedef struct { uint8_t data[64]; uint16_t len; } my_packet_s;
//      CROSSCORE_FIFO_TYPE(rx_reliable, my_packet_s, 16);
//      static fifo_rx_reliable_t _rx_reliable_fifo;
//
// 2) Producer core (and ONLY this core) pushes:
//
//      my_packet_s in;
//      memcpy(in.data, pkt->data, pkt->len);
//      in.len = pkt->len;
//      if (!fifo_rx_reliable_push(&_rx_reliable_fifo, &in)) {
//          // FIFO full: element was dropped.
//      }
//
// 3) Consumer core (and ONLY this core) pops:
//
//      my_packet_s out;
//      while (fifo_rx_reliable_pop(&_rx_reliable_fifo, &out)) {
//          // handle `out`
//      }
