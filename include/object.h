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

/*
 * ---------------- Project 6에서 추가 ----------------
 * copying GC는 객체를 통째로 다른 곳에 복사한다. 그러려면 객체 크기를 알아야 하고,
 * Heap이 아닌 메모리(Eden, Survivor)에도 객체를 만들 수 있어야 한다.
 * 또 JVM의 mark word처럼 헤더에 age와 forwarding pointer를 둔다.
 */

/* num_refs, data_size인 객체가 차지하는 전체 바이트 (헤더 포함). HEAP_ALIGNMENT의 배수. */
size_t object_size_for(size_t num_refs, size_t data_size);

/* obj가 차지하는 전체 바이트. object_size_for(num_refs, data_size)와 같다. */
size_t object_size(const Object *obj);

/*
 * object_size_for(num_refs, data_size) 바이트 이상인 mem 위에 새 객체를 만든다.
 * object_new와 같은 초기 상태: refs NULL, data 0, mark 꺼짐, age 0, forwarding NULL.
 */
Object *object_init(void *mem, size_t num_refs, size_t data_size);

/* 이 객체가 살아남은 minor GC 횟수 */
unsigned object_age(const Object *obj);
void object_set_age(Object *obj, unsigned age);

/* 복사된 객체의 새 주소. 아직 복사되지 않았으면 NULL. */
Object *object_forwarded(const Object *obj);
void object_set_forwarded(Object *obj, Object *new_location);

#endif
