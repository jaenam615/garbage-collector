#include "gc_internal.h"

#include <stdlib.h>

GC *gc_create(size_t heap_size) {
    (void)heap_size;
    /* TODO */
    return NULL;
}

void gc_destroy(GC *gc) {
    (void)gc;
    /* TODO */
}

Object *gc_alloc(GC *gc, size_t num_refs, size_t data_size) {
    (void)gc;
    (void)num_refs;
    (void)data_size;
    /* TODO */
    return NULL;
}

void gc_push_root(GC *gc, Object *obj) {
    (void)gc;
    (void)obj;
    /* TODO */
}

void gc_pop_root(GC *gc) {
    (void)gc;
    /* TODO */
}

size_t gc_collect(GC *gc) {
    (void)gc;
    /* TODO */
    return 0;
}

GCStats gc_stats(GC *gc) {
    (void)gc;
    /* TODO */
    GCStats stats = {0};
    return stats;
}
