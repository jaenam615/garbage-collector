#ifndef GEN_H
#define GEN_H

/*
 * Project 6: Generational GC (JVM Young/Old)
 *
 *   ┌──────────── Young ────────────┐   ┌──────── Old ────────┐
 *   │  Eden  │ Survivor0 │ Survivor1 │   │   Heap (Project 1)  │
 *   └────────────────────────────────┘   └─────────────────────┘
 *
 * - 새 객체는 Eden에 bump pointer로 할당한다.
 * - Eden이 꽉 차면 minor GC: 살아있는 young 객체를 비어 있는 Survivor로 "복사"하고
 *   Eden과 원래 Survivor를 통째로 비운다. (두 Survivor는 GC마다 역할이 바뀐다)
 * - minor GC를 tenure_threshold번 살아남은 객체는 Old로 승격(promotion)한다.
 *   Survivor 공간이 모자라도 Old로 승격한다.
 * - major GC: young의 살아있는 객체를 전부 Old로 승격한 뒤, Old를 mark-and-sweep 한다.
 *
 * 객체가 이동하므로 루트는 "객체"가 아니라 "객체를 담은 변수의 주소"다.
 * GC는 객체를 옮긴 뒤 루트 변수의 값을 새 주소로 고쳐야 한다.
 *
 *     Object *a = gen_alloc(gc, 1, 8);
 *     gen_push_root(gc, &a);
 *     gen_minor_gc(gc);          // a의 값이 Survivor 안의 새 주소로 바뀌어 있다
 *
 * minor GC는 Old를 훑지 않는다. 그래서 Old 객체가 young 객체를 가리키는 경우를
 * 따로 기억해야 한다 (write barrier + remembered set). 객체 사이의 참조는
 * 반드시 gen_write_ref로 쓴다.
 *
 * 테스트는 루트를 최대 256개까지 push 하고, 256KB C 스택에서 돈다.
 */

#include <stdbool.h>
#include <stddef.h>

#include "object.h"

typedef struct GenGC GenGC;  /* src/gen/gen_internal.h 에서 정의 */

typedef enum {
    GEN_NONE,      /* 어느 공간에도 속하지 않음 (NULL, 이미 비워진 공간의 옛 주소 등) */
    GEN_EDEN,
    GEN_SURVIVOR,  /* 현재 사용 중인 Survivor ("from") */
    GEN_OLD,
} GenSpace;

typedef struct {
    size_t eden_size;
    size_t survivor_size;   /* Survivor 하나의 크기 */
    size_t old_size;
    size_t eden_used;       /* Eden에서 할당된 바이트 */
    size_t survivor_used;   /* 현재 Survivor에서 쓰고 있는 바이트 */
    size_t old_used;        /* Old 힙의 used_bytes */
    size_t old_objects;     /* Old에 있는 객체 수 */
    size_t minor_gcs;       /* minor GC 횟수 (major GC 안에서 돈 것은 세지 않음) */
    size_t major_gcs;
    size_t last_copied;     /* 마지막 minor/major GC에서 young에서 옮긴 객체 수 (Survivor + Old) */
    size_t last_promoted;   /* 그중 Old로 간 객체 수 */
} GenStats;

/*
 * Eden eden_size, Survivor 두 개 각 survivor_size, Old old_size 바이트.
 * minor GC를 tenure_threshold번 살아남으면 Old로 승격. 실패 시 NULL.
 */
GenGC *gen_create(size_t eden_size, size_t survivor_size, size_t old_size, unsigned tenure_threshold);
void gen_destroy(GenGC *gc);

/*
 * 객체 할당.
 *   - Eden에 bump 할당. 주소는 HEAP_ALIGNMENT 정렬.
 *   - Eden에 자리가 없으면 minor GC 후 다시 시도.
 *   - Eden보다 큰 객체는 Old에 바로 할당 (실패하면 major GC 후 한 번 더).
 *   - 그래도 안 되면 NULL.
 */
Object *gen_alloc(GenGC *gc, size_t num_refs, size_t data_size);

/* write barrier: obj->refs[index] = value. Old → young 참조가 생기면 기억해 둔다. */
void gen_write_ref(GenGC *gc, Object *obj, size_t index, Object *value);

/* 루트 등록. slot은 Object* 변수의 주소. GC가 객체를 옮기면 *slot을 고친다. */
void gen_push_root(GenGC *gc, Object **slot);
void gen_pop_root(GenGC *gc);

void gen_minor_gc(GenGC *gc);
void gen_major_gc(GenGC *gc);

/* obj가 지금 어느 공간에 있는지. 디버깅과 테스트에 쓴다. */
GenSpace gen_space_of(GenGC *gc, const Object *obj);

GenStats gen_stats(GenGC *gc);

#endif
