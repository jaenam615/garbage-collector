#ifndef GC_H
#define GC_H

/*
 * Project 3~5: Mark / Sweep / Integration
 *
 * GC는 힙 하나를 소유하고, 그 힙의 객체를 관리한다.
 *
 * 루트
 *   JVM 스택 프레임의 지역 변수처럼 push/pop 하는 루트 스택.
 *   루트에서 refs를 따라 도달 가능한 객체만 살아남는다.
 *   테스트는 루트를 최대 256개까지 동시에 push 한다.
 *
 * C 스택
 *   테스트는 256KB 스택을 가진 스레드에서 돈다 (JVM의 -Xss 같은 것).
 *   객체 그래프의 깊이만큼 재귀하면 깊은 리스트에서 스택이 터진다.
 */

#include <stdio.h>

#include "object.h"

typedef struct GC GC;  /* src/gc/gc_internal.h 에서 정의 */

typedef struct {
    size_t heap_size;     /* gc_create에 넘긴 크기 */
    size_t used_bytes;    /* 힙에서 할당된 바이트 (heap_stats와 같음) */
    size_t live_objects;  /* 할당된(아직 해제되지 않은) 객체 수 */
    size_t collections;   /* 지금까지 gc_collect가 돈 횟수 (자동 GC 포함) */
    size_t last_freed;    /* 가장 최근 gc_collect에서 해제한 객체 수 */
} GCStats;

/* heap_size 바이트짜리 힙을 가진 GC를 만든다. 실패 시 NULL. */
GC *gc_create(size_t heap_size);

/* GC와 힙, GC가 쓰는 모든 메모리를 해제한다. NULL이면 아무것도 안 한다. */
void gc_destroy(GC *gc);

/*
 * 객체를 할당한다. 공간이 부족하면 gc_collect를 한 번 돌리고 다시 시도한다.
 * (JVM의 allocation failure → GC) 그래도 부족하면 NULL.
 *
 * 이 함수 안에서 GC가 돌 수 있으므로, 루트에서 도달할 수 없는 객체는 사라질 수 있다.
 */
Object *gc_alloc(GC *gc, size_t num_refs, size_t data_size);

void gc_push_root(GC *gc, Object *obj);
void gc_pop_root(GC *gc);

/* [Project 3] 루트에서 도달 가능한 모든 객체의 mark bit를 켠다. 해제는 하지 않는다. */
void gc_mark(GC *gc);

/*
 * [Project 4] mark bit가 꺼진 객체를 전부 해제하고, 살아남은 객체의 mark bit를 끈다.
 * 해제한 객체 수를 반환한다. 루트는 보지 않는다 (mark 결과만 믿는다).
 */
size_t gc_sweep(GC *gc);

/* [Project 5] gc_mark + gc_sweep, 그리고 통계 갱신. 해제한 객체 수를 반환한다. */
size_t gc_collect(GC *gc);

GCStats gc_stats(GC *gc);

/*
 * (선택) 디버깅용 힙 덤프. 테스트 스크립트의 `dump` 명령이 호출한다.
 * 구현하지 않으면 테스트 러너의 기본 구현이 쓰인다.
 */
void gc_dump(GC *gc, FILE *out);

#endif
