#include "object.h"

struct Object {
    int todo; /* TODO: 객체 헤더를 정의하세요 */
};

Object *object_new(Heap *heap, size_t num_refs, size_t data_size) {
    (void)heap;
    (void)num_refs;
    (void)data_size;
    /* TODO */
    return NULL;
}

size_t object_num_refs(const Object *obj) {
    (void)obj;
    /* TODO */
    return 0;
}

size_t object_data_size(const Object *obj) {
    (void)obj;
    /* TODO */
    return 0;
}

Object *object_get_ref(const Object *obj, size_t index) {
    (void)obj;
    (void)index;
    /* TODO */
    return NULL;
}

void object_set_ref(Object *obj, size_t index, Object *value) {
    (void)obj;
    (void)index;
    (void)value;
    /* TODO */
}

void *object_data(Object *obj) {
    (void)obj;
    /* TODO */
    return NULL;
}

bool object_is_marked(const Object *obj) {
    (void)obj;
    /* TODO */
    return false;
}

void object_set_marked(Object *obj, bool marked) {
    (void)obj;
    (void)marked;
    /* TODO */
}
