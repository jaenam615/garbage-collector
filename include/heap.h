#ifndef HEAP_H
#define HEAP_H

/*
 * Project 1: Heap
 *
 * 처음에 한 번 받아온 고정 크기 메모리 영역 위에서 동작하는 할당기.
 * malloc/free를 직접 만드는 것과 같다. (GC는 이 위에 올라간다)
 *
 * 요구사항
 *   - heap_alloc이 반환하는 주소는 HEAP_ALIGNMENT 바이트로 정렬되어 있어야 한다.
 *   - 해제된 블록은 다시 할당에 쓸 수 있어야 한다.
 *   - 큰 빈 블록에 작은 요청이 오면 남는 부분을 쪼개서(split) 따로 쓸 수 있어야 한다.
 *   - 주소상 인접한 빈 블록은 하나로 합쳐야 한다(coalesce).
 *   - 블록 관리용 메타데이터(헤더 등)는 힙 영역 안에 두어도 되고, 그 크기는 자유.
 */

#include <stddef.h>

#define HEAP_ALIGNMENT 8

typedef struct Heap Heap;  /* src/heap/heap_internal.h 에서 정의 */

typedef struct {
    size_t size;        /* heap_create에 넘긴 크기 */
    size_t used_bytes;  /* 할당된 블록들이 차지하는 바이트 (헤더, 패딩 포함). 다 해제하면 0 */
    size_t num_blocks;  /* 할당된(아직 해제되지 않은) 블록 수 */
} HeapStats;

/* size 바이트짜리 힙을 만든다. 실패 시 NULL. */
Heap *heap_create(size_t size);

/* 힙과 힙이 쓰는 모든 메모리를 해제한다. NULL이면 아무것도 안 한다. */
void heap_destroy(Heap *heap);

/*
 * size 바이트 이상을 쓸 수 있는 블록을 할당하고 그 시작 주소를 반환한다.
 * 들어갈 자리가 없으면 NULL. 내용은 초기화하지 않아도 된다 (malloc처럼).
 */
void *heap_alloc(Heap *heap, size_t size);

/* heap_alloc이 반환한 블록을 해제한다. ptr이 NULL이면 아무것도 안 한다. */
void heap_free(Heap *heap, void *ptr);

/*
 * 할당된 블록 순회. 주소가 낮은 블록부터 heap_alloc이 반환했던 주소를 돌려준다.
 * 빈 블록은 건너뛰고, 더 없으면 NULL.
 *
 *   for (void *p = heap_first(h); p != NULL; p = heap_next(h, p)) { ... }
 *
 * 순회 중에 블록을 해제하려면, heap_free(p) 하기 전에 heap_next(h, p)를 먼저 구해 둔다.
 */
void *heap_first(Heap *heap);
void *heap_next(Heap *heap, void *ptr);

HeapStats heap_stats(Heap *heap);

#endif
