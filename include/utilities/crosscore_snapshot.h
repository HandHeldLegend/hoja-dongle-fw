/*
 * X-macro template for seqlock-style cross-core "latest value" snapshots.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file crosscore_snapshot.h
 * @brief Generates typed last-write-wins snapshots shared between cores.
 *
 * SNAPSHOT_TYPE(name, type) expands to a snapshot struct plus inline write/read
 * functions implementing a single-writer/single-reader seqlock. Use a snapshot
 * (rather than a FIFO from crosscore_fifo.h) when only the most recent value
 * matters and intermediate updates may be coalesced or dropped.
 *
 * Macro contract:
 *   - EXACTLY ONE core may call snapshot_<name>_write (the producer).
 *   - EXACTLY ONE core may call snapshot_<name>_read  (the consumer).
 *   - The sequence counter is odd while a write is in progress and even when
 *     the stored value is stable; release/acquire fences order the payload
 *     copy against the counter so a torn read is detected, not returned.
 *   - On a detected concurrent/in-progress write, the reader copies the last
 *     fully published value (stale_data) and reports staleness instead of
 *     blocking, giving callers a usable value at all times.
 */

#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>

// Generic container for snapshot-protected data with stale value support
#define SNAPSHOT_TYPE(name, type)        \
typedef struct {                         \
    atomic_uint seq;   /**< Sequence: odd while writing, even when stable */ \
    type data;         /**< Live value being published by the writer */ \
    type stale_data;   /**< Last fully published value, returned on torn reads */ \
} snapshot_##name##_t;                   \
                                         \
/* PRODUCER-CORE ONLY. Publishes a new value: bump seq odd, store payload,    \
 * bump seq even, then refresh the stale copy for future torn reads. */        \
static inline void snapshot_##name##_write(snapshot_##name##_t *s, const type *src) { \
    unsigned int seq0 = atomic_load_explicit(&s->seq, memory_order_relaxed); \
    atomic_store_explicit(&s->seq, seq0 + 1, memory_order_relaxed); /* odd = writing */ \
    atomic_thread_fence(memory_order_release);                       \
    memcpy(&s->data, src, sizeof(type));                             \
    atomic_thread_fence(memory_order_release);                       \
    atomic_store_explicit(&s->seq, seq0 + 2, memory_order_release); /* even = stable */ \
    /* Update stale copy after write is complete */                  \
    memcpy(&s->stale_data, src, sizeof(type));                       \
}                                                                    \
                                                                     \
/* CONSUMER-CORE ONLY. Copies the latest value into @p dst. Returns true for a \
 * fresh, consistent read; returns false and copies stale_data if a write was   \
 * in progress or began during the read. */                                     \
static inline bool snapshot_##name##_read(snapshot_##name##_t *s, type *dst) { \
    unsigned int s1 = atomic_load_explicit(&s->seq, memory_order_acquire); \
    if (s1 & 1) {                                                    \
        /* Writer in progress - return stale value */                \
        memcpy(dst, &s->stale_data, sizeof(type));                   \
        return false; /* indicate stale read */                      \
    }                                                                \
    /* Try to get fresh value */                                     \
    memcpy(dst, &s->data, sizeof(type));                             \
    atomic_thread_fence(memory_order_acquire);                       \
    unsigned int s2 = atomic_load_explicit(&s->seq, memory_order_acquire); \
    if (s1 != s2 || (s2 & 1)) {                                      \
        /* Write happened during read - return stale value */        \
        memcpy(dst, &s->stale_data, sizeof(type));                   \
        return false; /* indicate stale read */                      \
    }                                                                \
    return true; /* indicate fresh read */                           \
}  
