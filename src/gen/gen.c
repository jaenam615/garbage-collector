#include "gen_internal.h"

#include <stdlib.h>

GenGC *gen_create(size_t eden_size, size_t survivor_size, size_t old_size, unsigned tenure_threshold) {
    (void)eden_size;
    (void)survivor_size;
    (void)old_size;
    (void)tenure_threshold;
    /* TODO */
    return NULL;
}

void gen_destroy(GenGC *gc) {
    (void)gc;
    /* TODO */
}

Object *gen_alloc(GenGC *gc, size_t num_refs, size_t data_size) {
    (void)gc;
    (void)num_refs;
    (void)data_size;
    /* TODO */
    return NULL;
}

void gen_write_ref(GenGC *gc, Object *obj, size_t index, Object *value) {
    (void)gc;
    (void)obj;
    (void)index;
    (void)value;
    /* TODO */
}

void gen_push_root(GenGC *gc, Object **slot) {
    (void)gc;
    (void)slot;
    /* TODO */
}

void gen_pop_root(GenGC *gc) {
    (void)gc;
    /* TODO */
}

GenSpace gen_space_of(GenGC *gc, const Object *obj) {
    (void)gc;
    (void)obj;
    /* TODO */
    return GEN_NONE;
}

GenStats gen_stats(GenGC *gc) {
    (void)gc;
    /* TODO */
    GenStats stats = {0};
    return stats;
}
