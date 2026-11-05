#include "heap_internal.h"

#include <stdlib.h>

Heap *heap_create(size_t size) {
    (void)size;
    /* TODO */
    return NULL;
}

void heap_destroy(Heap *heap) {
    (void)heap;
    /* TODO */
}

void *heap_alloc(Heap *heap, size_t size) {
    (void)heap;
    (void)size;
    /* TODO */
    return NULL;
}

void heap_free(Heap *heap, void *ptr) {
    (void)heap;
    (void)ptr;
    /* TODO */
}

void *heap_first(Heap *heap) {
    (void)heap;
    /* TODO */
    return NULL;
}

void *heap_next(Heap *heap, void *ptr) {
    (void)heap;
    (void)ptr;
    /* TODO */
    return NULL;
}

HeapStats heap_stats(Heap *heap) {
    (void)heap;
    /* TODO */
    HeapStats stats = {0};
    return stats;
}
