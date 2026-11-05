#ifndef OBJECT_H
#define OBJECT_H

/*
 * Project 2: Object
 *
 * 힙 블록 하나 = 객체 하나. JVM 객체처럼 헤더 뒤에 필드가 붙는다.
 *
 *   [ 헤더 | refs[0] ... refs[num_refs-1] | data[0] ... data[data_size-1] ]
 *
 *   - 헤더: mark bit, num_refs, data_size 등. 구성은 자유.
 *   - refs: 다른 객체를 가리키는 참조. GC는 이것만 따라간다. (JVM의 참조 필드)
 *   - data: GC가 해석하지 않는 원시 바이트. (JVM의 int, double 같은 필드)
 *
 * 요구사항
 *   - object_new가 반환하는 Object*는 heap_alloc이 반환한 주소와 같아야 한다.
 *     그래야 sweep이 heap_first/heap_next로 받은 주소를 Object*로 쓸 수 있다.
 *   - 새 객체의 refs는 전부 NULL, data는 전부 0, mark bit는 꺼져 있어야 한다.
 *     (재사용된 메모리라도 마찬가지)
 */

#include <stdbool.h>
#include <stddef.h>

#include "heap.h"

typedef struct Object Object;  /* src/object/object.c 에서 정의 */

/* heap에 객체 하나를 만든다. 공간이 없으면 NULL. GC는 돌리지 않는다. */
Object *object_new(Heap *heap, size_t num_refs, size_t data_size);

size_t object_num_refs(const Object *obj);
size_t object_data_size(const Object *obj);

/* obj->refs[index] */
Object *object_get_ref(const Object *obj, size_t index);

/* obj->refs[index] = value. value는 NULL 가능. */
void object_set_ref(Object *obj, size_t index, Object *value);

/* data 영역의 시작 주소 */
void *object_data(Object *obj);

bool object_is_marked(const Object *obj);
void object_set_marked(Object *obj, bool marked);

#endif
