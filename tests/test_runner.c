/*
 * 테스트 러너: .gct 스크립트를 한 줄씩 읽어 heap.h / object.h / gc.h API를 호출한다.
 *
 *   build/test_runner tests/heap/alloc-one.gct
 *
 * 스크립트 문법은 tests/README.md 참고.
 */
#include "gc.h"
#include "gen.h"
#include "heap.h"
#include "object.h"

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VARS   256
#define MAX_LETS   64
#define MAX_FILL   4096
#define MAX_TOKENS 16
#define MAX_LINE   512
#define STACK_SIZE (256 * 1024) /* 테스트 스레드의 C 스택 크기 */

/* ---------- 상태 ---------- */

typedef enum { MODE_NONE, MODE_HEAP, MODE_GC, MODE_GEN } Mode;
typedef enum { VAR_PTR, VAR_OBJ } VarKind;

typedef struct {
    char name[32];
    VarKind kind;
    void *ptr;          /* VAR_PTR: heap_alloc 결과, VAR_OBJ: Object* */
    size_t size;        /* VAR_PTR: 요청 크기, VAR_OBJ: data_size */
    size_t num_refs;    /* VAR_OBJ 전용 */
    bool freed;         /* heap_free로 해제됨 */
    bool stale;         /* MODE_GEN: 루트가 아닌데 GC가 돌았음 (객체가 옮겨졌을 수 있음) */
    void *remembered;   /* remember 명령으로 저장한 주소 */
} Var;

typedef struct {
    char name[32];
    size_t value;
} Let;

static Mode mode;
static Heap *heap;      /* MODE_HEAP */
static GC *gc;          /* MODE_GC */
static GenGC *gen;      /* MODE_GEN */

static Var vars[MAX_VARS];
static size_t num_vars;
static Let lets[MAX_LETS];
static size_t num_lets;

static size_t live_allocs;  /* MODE_HEAP: 러너가 아는 살아있는 블록 수 */
static size_t root_depth;   /* MODE_GC / MODE_GEN: 러너가 push한 루트 수 */
static Var *gen_roots[256]; /* MODE_GEN: 루트로 등록한 변수들 */
static size_t last_result;  /* gc_mark/gc_sweep/gc_collect의 반환값 */

static void *fill_ptrs[MAX_FILL];
static size_t fill_count;
static size_t fill_data_size;
static bool fill_is_obj;

static const char *script_path;
static int line_no;
static int failures;

/* gc_dump을 구현하지 않았을 때 쓰이는 기본 구현 */
__attribute__((weak)) void gc_dump(GC *g, FILE *out) {
    GCStats s = gc_stats(g);
    fprintf(out, "    (gc_dump 미구현) heap=%zu used=%zu live=%zu collections=%zu\n",
            s.heap_size, s.used_bytes, s.live_objects, s.collections);
}

/* ---------- 출력 / 에러 ---------- */

static void check_failed(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL %s:%d: ", script_path, line_no);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    fflush(stdout);
    failures++;
}

/* 스크립트 자체가 잘못된 경우: 즉시 종료 */
static void script_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("SCRIPT ERROR %s:%d: ", script_path, line_no);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    fflush(stdout);
    exit(2);
}

/* 구현이 테스트를 더 진행할 수 없는 값을 돌려준 경우: FAIL로 즉시 종료 */
static void fatal_fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL %s:%d: ", script_path, line_no);
    vprintf(fmt, ap);
    printf("\nFAIL (stopped)\n");
    va_end(ap);
    fflush(stdout);
    exit(1);
}

static void need_args(int argc, int n, const char *usage) {
    if (argc != n) {
        script_error("usage: %s", usage);
    }
}

static void need_mode(Mode m, const char *cmd) {
    if (mode == MODE_NONE) {
        script_error("'heap_create SIZE' or 'gc_create SIZE' must come first");
    }
    if (mode != m) {
        script_error("'%s' needs %s", cmd, m == MODE_HEAP ? "heap_create" : "gc_create");
    }
}

/* ---------- 변수 ---------- */

static Var *find_var(const char *name) {
    for (size_t i = 0; i < num_vars; i++) {
        if (strcmp(vars[i].name, name) == 0) {
            return &vars[i];
        }
    }
    return NULL;
}

static Var *get_var(const char *name) {
    Var *v = find_var(name);
    if (v == NULL) {
        script_error("unknown variable '%s'", name);
    }
    return v;
}

/* NULL이 아니고 해제되지 않은 변수 */
static Var *get_live_var(const char *name) {
    Var *v = get_var(name);
    if (v->ptr == NULL) {
        script_error("'%s' is NULL (allocation failed earlier?)", name);
    }
    if (v->stale) {
        script_error("'%s' is stale: it is not a root and a GC ran, so the object may have moved."
                     " Use gen_push_root %s", name, name);
    }
    if (v->freed) {
        script_error("'%s' was already freed", name);
    }
    return v;
}

static Var *get_obj_var(const char *name) {
    Var *v = get_live_var(name);
    if (v->kind != VAR_OBJ) {
        script_error("'%s' is not an object", name);
    }
    return v;
}

static Var *set_var(const char *name, VarKind kind, void *ptr, size_t size, size_t num_refs) {
    Var *v = find_var(name);
    if (v == NULL) {
        if (num_vars == MAX_VARS) {
            script_error("too many variables");
        }
        v = &vars[num_vars++];
        snprintf(v->name, sizeof v->name, "%s", name);
    }
    v->kind = kind;
    v->ptr = ptr;
    v->size = size;
    v->num_refs = num_refs;
    v->freed = false;
    v->stale = false;
    return v;
}

/* "null" 또는 객체 변수 이름 → Object* */
static Object *resolve_obj(const char *tok) {
    if (strcmp(tok, "null") == 0) {
        return NULL;
    }
    return get_obj_var(tok)->ptr;
}

static const char *ptr_name(const void *p) {
    if (p == NULL) {
        return "null";
    }
    for (size_t i = 0; i < num_vars; i++) {
        if (vars[i].ptr == p && !vars[i].freed) {
            return vars[i].name;
        }
    }
    return "?";
}

static unsigned char *var_data(Var *v) {
    return v->kind == VAR_OBJ ? object_data(v->ptr) : v->ptr;
}

/* ---------- 숫자 / 통계 ---------- */

static size_t parse_size(const char *tok) {
    char *end;
    unsigned long long v = strtoull(tok, &end, 0);
    if (*end == 'K' || *end == 'k') {
        v *= 1024;
        end++;
    } else if (*end == 'M' || *end == 'm') {
        v *= 1024 * 1024;
        end++;
    }
    if (*tok == '\0' || *end != '\0') {
        script_error("expected a number, got '%s'", tok);
    }
    return (size_t)v;
}

static bool stat_value(const char *name, size_t *out) {
    if (strcmp(name, "filled") == 0) {
        *out = fill_count;
        return true;
    }
    if (mode == MODE_HEAP) {
        HeapStats s = heap_stats(heap);
        if (strcmp(name, "size") == 0)        *out = s.size;
        else if (strcmp(name, "used") == 0)   *out = s.used_bytes;
        else if (strcmp(name, "blocks") == 0) *out = s.num_blocks;
        else return false;
        return true;
    }
    if (mode == MODE_GEN) {
        GenStats s = gen_stats(gen);
        if (strcmp(name, "eden_used") == 0)          *out = s.eden_used;
        else if (strcmp(name, "survivor_used") == 0) *out = s.survivor_used;
        else if (strcmp(name, "old_used") == 0)      *out = s.old_used;
        else if (strcmp(name, "old_objects") == 0)   *out = s.old_objects;
        else if (strcmp(name, "minor_gcs") == 0)     *out = s.minor_gcs;
        else if (strcmp(name, "major_gcs") == 0)     *out = s.major_gcs;
        else if (strcmp(name, "copied") == 0)        *out = s.last_copied;
        else if (strcmp(name, "promoted") == 0)      *out = s.last_promoted;
        else if (strcmp(name, "roots") == 0)         *out = root_depth;
        else return false;
        return true;
    }
    if (mode == MODE_GC) {
        GCStats s = gc_stats(gc);
        if (strcmp(name, "heap") == 0)             *out = s.heap_size;
        else if (strcmp(name, "used") == 0)        *out = s.used_bytes;
        else if (strcmp(name, "live") == 0)        *out = s.live_objects;
        else if (strcmp(name, "collections") == 0) *out = s.collections;
        else if (strcmp(name, "freed") == 0)       *out = s.last_freed;
        else if (strcmp(name, "roots") == 0)       *out = root_depth;
        else if (strcmp(name, "result") == 0)      *out = last_result;
        else return false;
        return true;
    }
    return false;
}

/* 숫자, let 이름, 통계 이름 중 하나 → 값 */
static size_t resolve_value(const char *tok) {
    size_t v;
    if (isdigit((unsigned char)tok[0])) {
        return parse_size(tok);
    }
    for (size_t i = 0; i < num_lets; i++) {
        if (strcmp(lets[i].name, tok) == 0) {
            return lets[i].value;
        }
    }
    if (stat_value(tok, &v)) {
        return v;
    }
    script_error("unknown value '%s' (not a number, let, or stat)", tok);
    return 0;
}

static bool compare(size_t a, const char *op, size_t b) {
    if (strcmp(op, "==") == 0) return a == b;
    if (strcmp(op, "!=") == 0) return a != b;
    if (strcmp(op, "<") == 0)  return a < b;
    if (strcmp(op, "<=") == 0) return a <= b;
    if (strcmp(op, ">") == 0)  return a > b;
    if (strcmp(op, ">=") == 0) return a >= b;
    script_error("unknown operator '%s'", op);
    return false;
}

/* ---------- 루트 ---------- */

static void do_push(Object *obj) {
    gc_push_root(gc, obj);
    root_depth++;
}

static void do_pop(void) {
    if (root_depth == 0) {
        script_error("gc_pop_root with empty root stack");
    }
    gc_pop_root(gc);
    root_depth--;
}

/* ---------- expect ---------- */

static void expect_walk(void) {
    need_mode(MODE_HEAP, "expect walk");
    size_t n = 0;
    uintptr_t prev = 0;
    for (void *p = heap_first(heap); p != NULL; p = heap_next(heap, p)) {
        if (n > live_allocs) {
            check_failed("heap walk returned more than %zu blocks (infinite loop?)", live_allocs);
            return;
        }
        if ((uintptr_t)p <= prev) {
            check_failed("heap walk is not in increasing address order (%p after %p)", p, (void *)prev);
            return;
        }
        prev = (uintptr_t)p;
        n++;
    }
    if (n != live_allocs) {
        check_failed("heap walk returned %zu blocks, expected %zu", n, live_allocs);
        return;
    }
    for (size_t i = 0; i < num_vars; i++) {
        Var *v = &vars[i];
        if (v->ptr == NULL || v->freed) {
            continue;
        }
        bool found = false;
        for (void *p = heap_first(heap); p != NULL; p = heap_next(heap, p)) {
            if (p == v->ptr) {
                found = true;
                break;
            }
        }
        if (!found) {
            check_failed("heap walk did not return %s (%p)", v->name, v->ptr);
        }
    }
}

static GenSpace parse_space(const char *tok) {
    if (strcmp(tok, "eden") == 0) return GEN_EDEN;
    if (strcmp(tok, "survivor") == 0) return GEN_SURVIVOR;
    if (strcmp(tok, "old") == 0) return GEN_OLD;
    if (strcmp(tok, "none") == 0) return GEN_NONE;
    script_error("unknown space '%s' (eden, survivor, old, none)", tok);
    return GEN_NONE;
}

static const char *space_name(GenSpace s) {
    switch (s) {
    case GEN_EDEN: return "eden";
    case GEN_SURVIVOR: return "survivor";
    case GEN_OLD: return "old";
    default: return "none";
    }
}

/* VAR에서 도달 가능한 모든 객체가 mark 되어 있는지 (러너가 직접 순회, 재귀 없음) */
static void expect_all_marked(Var *start) {
    size_t cap = 1024, top = 0, seen_cap = 4096, seen_n = 0, checked = 0;
    Object **stack = malloc(cap * sizeof *stack);
    Object **seen = calloc(seen_cap, sizeof *seen);  /* 열린 주소법 해시셋 */
    stack[top++] = start->ptr;
    while (top > 0) {
        Object *o = stack[--top];
        if (o == NULL) {
            continue;
        }
        if (seen_n * 2 >= seen_cap) {
            size_t new_cap = seen_cap * 2;
            Object **grown = calloc(new_cap, sizeof *grown);
            for (size_t i = 0; i < seen_cap; i++) {
                if (seen[i] != NULL) {
                    size_t h = ((uintptr_t)seen[i] >> 3) & (new_cap - 1);
                    while (grown[h] != NULL) h = (h + 1) & (new_cap - 1);
                    grown[h] = seen[i];
                }
            }
            free(seen);
            seen = grown;
            seen_cap = new_cap;
        }
        size_t h = ((uintptr_t)o >> 3) & (seen_cap - 1);
        while (seen[h] != NULL && seen[h] != o) h = (h + 1) & (seen_cap - 1);
        if (seen[h] == o) {
            continue;
        }
        seen[h] = o;
        seen_n++;
        checked++;
        if (!object_is_marked(o)) {
            check_failed("object %s (%p), reachable from %s, is not marked (%zu objects checked before it)",
                         ptr_name(o), (void *)o, start->name, checked - 1);
            break;
        }
        size_t n = object_num_refs(o);
        for (size_t i = 0; i < n; i++) {
            if (top == cap) {
                cap *= 2;
                stack = realloc(stack, cap * sizeof *stack);
            }
            stack[top++] = object_get_ref(o, i);
        }
    }
    free(stack);
    free(seen);
}

static void cmd_expect(int argc, char **argv) {
    const char *what = argv[1];

    if (strcmp(what, "null") == 0 || strcmp(what, "notnull") == 0) {
        need_args(argc, 3, "expect null|notnull VAR");
        Var *v = get_var(argv[2]);
        bool want_null = strcmp(what, "null") == 0;
        if ((v->ptr == NULL) != want_null) {
            check_failed("expected %s to be %s, got %p", v->name, what, v->ptr);
        }
    } else if (strcmp(what, "same") == 0 || strcmp(what, "differ") == 0) {
        need_args(argc, 4, "expect same|differ VAR VAR");
        void *a = get_var(argv[2])->ptr;
        void *b = get_var(argv[3])->ptr;
        bool want_same = strcmp(what, "same") == 0;
        if ((a == b) != want_same) {
            check_failed("expected %s and %s to %s (%p vs %p)", argv[2], argv[3],
                         want_same ? "be the same" : "differ", a, b);
        }
    } else if (strcmp(what, "aligned") == 0) {
        need_args(argc, 3, "expect aligned VAR");
        Var *v = get_live_var(argv[2]);
        if ((uintptr_t)v->ptr % HEAP_ALIGNMENT != 0) {
            check_failed("expected %s (%p) to be %d-byte aligned", v->name, v->ptr, HEAP_ALIGNMENT);
        }
    } else if (strcmp(what, "data") == 0) {
        need_args(argc, 4, "expect data VAR TEXT");
        Var *v = get_live_var(argv[2]);
        size_t len = strlen(argv[3]);
        if (len > v->size) {
            script_error("'%s' is longer than %s (%zu bytes)", argv[3], v->name, v->size);
        }
        const unsigned char *data = var_data(v);
        if (memcmp(data, argv[3], len) != 0) {
            check_failed("expected %s data to start with \"%s\", got \"%.*s\"",
                         v->name, argv[3], (int)len, (const char *)data);
        }
    } else if (strcmp(what, "ref") == 0) {
        need_args(argc, 5, "expect ref VAR INDEX VAR|null");
        Var *v = get_obj_var(argv[2]);
        size_t idx = parse_size(argv[3]);
        if (idx >= v->num_refs) {
            script_error("%s has only %zu refs", v->name, v->num_refs);
        }
        Object *want = resolve_obj(argv[4]);
        Object *got = object_get_ref(v->ptr, idx);
        if (got != want) {
            check_failed("expected %s.refs[%zu] == %s (%p), got %s (%p)",
                         v->name, idx, argv[4], (void *)want, ptr_name(got), (void *)got);
        }
    } else if (strcmp(what, "shape") == 0) {
        need_args(argc, 5, "expect shape VAR NUM_REFS DATA_SIZE");
        Var *v = get_obj_var(argv[2]);
        size_t refs = parse_size(argv[3]);
        size_t size = parse_size(argv[4]);
        if (object_num_refs(v->ptr) != refs || object_data_size(v->ptr) != size) {
            check_failed("expected %s to have %zu refs / %zu data bytes, got %zu / %zu",
                         v->name, refs, size, object_num_refs(v->ptr), object_data_size(v->ptr));
        }
    } else if (strcmp(what, "zeroed") == 0) {
        need_args(argc, 3, "expect zeroed VAR");
        Var *v = get_obj_var(argv[2]);
        for (size_t i = 0; i < v->num_refs; i++) {
            Object *r = object_get_ref(v->ptr, i);
            if (r != NULL) {
                check_failed("expected %s.refs[%zu] == NULL on a new object, got %p", v->name, i, (void *)r);
                return;
            }
        }
        const unsigned char *data = object_data(v->ptr);
        for (size_t i = 0; i < v->size; i++) {
            if (data[i] != 0) {
                check_failed("expected %s.data[%zu] == 0 on a new object, got 0x%02x", v->name, i, data[i]);
                return;
            }
        }
        if (object_is_marked(v->ptr)) {
            check_failed("expected %s to be unmarked on a new object", v->name);
        }
    } else if (strcmp(what, "marked") == 0 || strcmp(what, "unmarked") == 0) {
        need_args(argc, 3, "expect marked|unmarked VAR");
        Var *v = get_obj_var(argv[2]);
        bool want = strcmp(what, "marked") == 0;
        if (object_is_marked(v->ptr) != want) {
            check_failed("expected %s to be %s", v->name, what);
        }
    } else if (strcmp(what, "all-marked") == 0) {
        need_args(argc, 3, "expect all-marked VAR");
        expect_all_marked(get_obj_var(argv[2]));
    } else if (strcmp(what, "space") == 0) {
        need_args(argc, 4, "expect space VAR eden|survivor|old|none");
        need_mode(MODE_GEN, "expect space");
        Var *v = get_obj_var(argv[2]);
        GenSpace want = parse_space(argv[3]);
        GenSpace got = gen_space_of(gen, v->ptr);
        if (got != want) {
            check_failed("expected %s to be in %s, got %s", v->name, argv[3], space_name(got));
        }
    } else if (strcmp(what, "refspace") == 0) {
        need_args(argc, 5, "expect refspace VAR INDEX eden|survivor|old|none");
        need_mode(MODE_GEN, "expect refspace");
        Var *v = get_obj_var(argv[2]);
        size_t idx = parse_size(argv[3]);
        if (idx >= v->num_refs) {
            script_error("%s has only %zu refs", v->name, v->num_refs);
        }
        GenSpace want = parse_space(argv[4]);
        Object *r = object_get_ref(v->ptr, idx);
        GenSpace got = gen_space_of(gen, r);
        if (got != want) {
            check_failed("expected %s.refs[%zu] (%p) to be in %s, got %s%s", v->name, idx, (void *)r,
                         argv[4], space_name(got),
                         got == GEN_NONE && r != NULL ? " (stale pointer: not updated after the object moved?)" : "");
        }
    } else if (strcmp(what, "refdata") == 0) {
        need_args(argc, 5, "expect refdata VAR INDEX TEXT");
        Var *v = get_obj_var(argv[2]);
        size_t idx = parse_size(argv[3]);
        if (idx >= v->num_refs) {
            script_error("%s has only %zu refs", v->name, v->num_refs);
        }
        Object *r = object_get_ref(v->ptr, idx);
        size_t len = strlen(argv[4]);
        if (r == NULL) {
            check_failed("expected %s.refs[%zu] to have data \"%s\", but it is NULL", v->name, idx, argv[4]);
        } else if (object_data_size(r) < len || memcmp(object_data(r), argv[4], len) != 0) {
            check_failed("expected %s.refs[%zu] data to start with \"%s\", got \"%.*s\"", v->name, idx,
                         argv[4], (int)len, (const char *)object_data(r));
        }
    } else if (strcmp(what, "sameref") == 0) {
        need_args(argc, 6, "expect sameref VAR INDEX VAR INDEX");
        Var *a = get_obj_var(argv[2]);
        Var *b = get_obj_var(argv[4]);
        Object *ra = object_get_ref(a->ptr, parse_size(argv[3]));
        Object *rb = object_get_ref(b->ptr, parse_size(argv[5]));
        if (ra != rb) {
            check_failed("expected %s.refs[%s] and %s.refs[%s] to be the same object, got %p and %p"
                         " (copied twice? check the forwarding pointer)",
                         a->name, argv[3], b->name, argv[5], (void *)ra, (void *)rb);
        }
    } else if (strcmp(what, "age") == 0) {
        need_args(argc, 4, "expect age VAR N");
        Var *v = get_obj_var(argv[2]);
        size_t want = parse_size(argv[3]);
        if (object_age(v->ptr) != want) {
            check_failed("expected %s to have age %zu, got %u", v->name, want, object_age(v->ptr));
        }
    } else if (strcmp(what, "moved") == 0 || strcmp(what, "stayed") == 0) {
        need_args(argc, 3, "expect moved|stayed VAR");
        Var *v = get_obj_var(argv[2]);
        if (v->remembered == NULL) {
            script_error("use 'remember %s' first", v->name);
        }
        bool want_moved = strcmp(what, "moved") == 0;
        if ((v->ptr != v->remembered) != want_moved) {
            check_failed("expected %s to have %s (was %p, now %p)%s", v->name,
                         want_moved ? "moved" : "stayed in place", v->remembered, v->ptr,
                         want_moved ? " (root slot not updated?)" : "");
        }
    } else if (strcmp(what, "forwarded") == 0) {
        need_args(argc, 4, "expect forwarded VAR VAR|null");
        Var *v = get_obj_var(argv[2]);
        Object *want = resolve_obj(argv[3]);
        if (object_forwarded(v->ptr) != want) {
            check_failed("expected forwarding of %s to be %s (%p), got %p", v->name, argv[3],
                         (void *)want, (void *)object_forwarded(v->ptr));
        }
    } else if (strcmp(what, "objsize") == 0) {
        need_args(argc, 3, "expect objsize VAR");
        Var *v = get_obj_var(argv[2]);
        size_t got = object_size(v->ptr);
        size_t want = object_size_for(v->num_refs, v->size);
        size_t min = v->num_refs * sizeof(Object *) + v->size;
        if (got != want) {
            check_failed("object_size(%s) = %zu but object_size_for(%zu, %zu) = %zu",
                         v->name, got, v->num_refs, v->size, want);
        } else if (got < min) {
            check_failed("object_size(%s) = %zu is smaller than refs + data (%zu)", v->name, got, min);
        } else if (got % HEAP_ALIGNMENT != 0) {
            check_failed("object_size(%s) = %zu is not a multiple of %d", v->name, got, HEAP_ALIGNMENT);
        }
    } else if (strcmp(what, "walk") == 0) {
        need_args(argc, 2, "expect walk");
        expect_walk();
    } else if (strcmp(what, "listlen") == 0) {
        need_args(argc, 4, "expect listlen VAR N");
        Var *v = get_obj_var(argv[2]);
        size_t want = parse_size(argv[3]);
        size_t n = 0;
        for (Object *cur = v->ptr; cur != NULL && n <= want; cur = object_get_ref(cur, 0)) {
            size_t stored;
            memcpy(&stored, object_data(cur), sizeof stored);
            if (n < want && stored != want - 1 - n) {
                check_failed("list %s: node #%zu has value %zu, expected %zu (corrupted?)",
                             v->name, n, stored, want - 1 - n);
                return;
            }
            n++;
        }
        if (n != want) {
            check_failed("expected list %s to have %zu nodes, got %s%zu",
                         v->name, want, n > want ? "more than " : "", n > want ? want : n);
        }
    } else if (strcmp(what, "fill-intact") == 0) {
        need_args(argc, 2, "expect fill-intact");
        for (size_t i = 0; i < fill_count; i++) {
            const unsigned char *data = fill_is_obj ? object_data(fill_ptrs[i]) : fill_ptrs[i];
            for (size_t j = 0; j < fill_data_size; j++) {
                if (data[j] != (unsigned char)i) {
                    check_failed("fill block #%zu: byte %zu is 0x%02x, expected 0x%02x (overwritten?)",
                                 i, j, data[j], (unsigned char)i);
                    return;
                }
            }
        }
    } else {
        need_args(argc, 4, "expect STAT OP VALUE");
        size_t actual;
        if (!stat_value(what, &actual)) {
            script_error("unknown stat '%s' in this mode", what);
        }
        size_t want = resolve_value(argv[3]);
        if (!compare(actual, argv[2], want)) {
            check_failed("expected %s %s %s (%zu), got %zu", what, argv[2], argv[3], want, actual);
        }
    }
}

/* ---------- 명령 ---------- */

static void exec(int argc, char **argv, bool trace);

static void cmd_list(const char *name, size_t n) {
    /* 노드 i의 data = i, head가 가장 마지막 노드. head는 만드는 동안 루트로 잡아둔다. */
    Object *head = NULL;
    for (size_t i = 0; i < n; i++) {
        Object *node = gc_alloc(gc, 1, sizeof(size_t));
        if (node == NULL) {
            check_failed("gc_list %s: gc_alloc returned NULL at node %zu of %zu", name, i, n);
            break;
        }
        object_set_ref(node, 0, head);
        memcpy(object_data(node), &i, sizeof i);
        if (head != NULL) {
            do_pop();
        }
        do_push(node);
        head = node;
    }
    if (head != NULL) {
        do_pop();
    }
    set_var(name, VAR_OBJ, head, sizeof(size_t), 1);
}

static void cmd_fill(bool obj, size_t num_refs, size_t size, bool trace) {
    fill_is_obj = obj;
    fill_data_size = size;
    fill_count = 0;
    for (;;) {
        void *p = obj ? (void *)gc_alloc(gc, num_refs, size) : heap_alloc(heap, size);
        if (p == NULL) {
            break;
        }
        if (fill_count == MAX_FILL || (obj && fill_count == 256)) {
            script_error("fill: too many blocks fit; use a smaller heap or bigger blocks");
        }
        memset(obj ? object_data(p) : p, (unsigned char)fill_count, size);
        if (obj) {
            do_push(p);
        } else {
            live_allocs++;
        }
        fill_ptrs[fill_count++] = p;
    }
    if (trace) {
        printf("    filled %zu\n", fill_count);
    }
}

static void gen_push_var(Var *v) {
    if (root_depth == 256) {
        script_error("too many roots");
    }
    gen_push_root(gen, (Object **)&v->ptr);
    gen_roots[root_depth++] = v;
}

static void gen_pop_var(void) {
    if (root_depth == 0) {
        script_error("gen_pop_root with empty root stack");
    }
    gen_pop_root(gen);
    root_depth--;
}

static void cmd_gen_list(const char *name, size_t n) {
    /* 노드 i의 data = i, head가 가장 마지막 노드. 변수 자체를 루트로 등록해 두고 만든다. */
    Var *head = set_var(name, VAR_OBJ, NULL, sizeof(size_t), 1);
    gen_push_var(head);
    for (size_t i = 0; i < n; i++) {
        Object *node = gen_alloc(gen, 1, sizeof(size_t));  /* 여기서 GC가 돌면 head->ptr이 바뀐다 */
        if (node == NULL) {
            check_failed("gen_list %s: gen_alloc returned NULL at node %zu of %zu", name, i, n);
            break;
        }
        gen_write_ref(gen, node, 0, head->ptr);
        memcpy(object_data(node), &i, sizeof i);
        head->ptr = node;
    }
    gen_pop_var();
}

static size_t gen_gc_count(void) {
    GenStats s = gen_stats(gen);
    return s.minor_gcs + s.major_gcs;
}

/* GC가 돌았으면 루트가 아닌 변수는 더 이상 믿을 수 없다 */
static void gen_mark_stale(void) {
    for (size_t i = 0; i < num_vars; i++) {
        bool rooted = false;
        for (size_t r = 0; r < root_depth; r++) {
            if (gen_roots[r] == &vars[i]) {
                rooted = true;
                break;
            }
        }
        if (!rooted && vars[i].kind == VAR_OBJ && vars[i].ptr != NULL) {
            vars[i].stale = true;
        }
    }
}

static void exec_one(int argc, char **argv, bool trace);

static void exec(int argc, char **argv, bool trace) {
    if (mode != MODE_GEN) {
        exec_one(argc, argv, trace);
        return;
    }
    size_t before = gen_gc_count();
    exec_one(argc, argv, trace);
    if (gen_gc_count() != before) {
        gen_mark_stale();
    }
}

static void exec_one(int argc, char **argv, bool trace) {
    const char *cmd = argv[0];

    /* ---- 생성 ---- */
    if (strcmp(cmd, "gen_create") == 0) {
        need_args(argc, 5, "gen_create EDEN SURVIVOR OLD TENURE_THRESHOLD");
        if (mode != MODE_NONE) {
            script_error("heap already created");
        }
        mode = MODE_GEN;
        gen = gen_create(parse_size(argv[1]), parse_size(argv[2]), parse_size(argv[3]),
                         (unsigned)parse_size(argv[4]));
        if (gen == NULL) {
            fatal_fail("gen_create returned NULL");
        }
    } else if (strcmp(cmd, "heap_create") == 0 || strcmp(cmd, "gc_create") == 0) {
        need_args(argc, 2, "heap_create|gc_create SIZE");
        if (mode != MODE_NONE) {
            script_error("heap already created");
        }
        size_t size = parse_size(argv[1]);
        if (cmd[0] == 'h') {
            mode = MODE_HEAP;
            heap = heap_create(size);
            if (heap == NULL) {
                fatal_fail("heap_create(%zu) returned NULL", size);
            }
        } else {
            mode = MODE_GC;
            gc = gc_create(size);
            if (gc == NULL) {
                fatal_fail("gc_create(%zu) returned NULL", size);
            }
        }

    /* ---- heap ---- */
    } else if (strcmp(cmd, "heap_alloc") == 0) {
        need_args(argc, 3, "heap_alloc VAR SIZE");
        need_mode(MODE_HEAP, cmd);
        size_t size = parse_size(argv[2]);
        void *p = heap_alloc(heap, size);
        set_var(argv[1], VAR_PTR, p, size, 0);
        if (p != NULL) {
            live_allocs++;
        }
        if (trace) {
            printf("    %s = %p\n", argv[1], p);
        }
    } else if (strcmp(cmd, "heap_free") == 0) {
        need_args(argc, 2, "heap_free VAR|null");
        need_mode(MODE_HEAP, cmd);
        if (strcmp(argv[1], "null") == 0) {
            heap_free(heap, NULL);
            return;
        }
        Var *v = get_live_var(argv[1]);
        heap_free(heap, v->ptr);
        v->freed = true;
        live_allocs--;
    } else if (strcmp(cmd, "heap_fill") == 0) {
        need_args(argc, 2, "heap_fill SIZE");
        need_mode(MODE_HEAP, cmd);
        cmd_fill(false, 0, parse_size(argv[1]), trace);
    } else if (strcmp(cmd, "heap_free_fill") == 0) {
        need_args(argc, 1, "heap_free_fill");
        need_mode(MODE_HEAP, cmd);
        if (fill_is_obj) {
            script_error("heap_free_fill after gc_fill");
        }
        for (size_t i = 0; i < fill_count; i++) {
            heap_free(heap, fill_ptrs[i]);
            live_allocs--;
        }
        fill_count = 0;

    /* ---- object ---- */
    } else if (strcmp(cmd, "object_new") == 0) {
        need_args(argc, 4, "object_new VAR NUM_REFS DATA_SIZE");
        need_mode(MODE_HEAP, cmd);
        size_t refs = parse_size(argv[2]);
        size_t size = parse_size(argv[3]);
        Object *obj = object_new(heap, refs, size);
        set_var(argv[1], VAR_OBJ, obj, size, refs);
        if (obj != NULL) {
            live_allocs++;
        }
        if (trace) {
            printf("    %s = %p\n", argv[1], (void *)obj);
        }
    } else if (strcmp(cmd, "object_set_ref") == 0) {
        need_args(argc, 4, "object_set_ref VAR INDEX VAR|null");
        Var *v = get_obj_var(argv[1]);
        size_t idx = parse_size(argv[2]);
        if (idx >= v->num_refs) {
            script_error("%s has only %zu refs", v->name, v->num_refs);
        }
        object_set_ref(v->ptr, idx, resolve_obj(argv[3]));
    } else if (strcmp(cmd, "object_set_marked") == 0) {
        need_args(argc, 3, "object_set_marked VAR 0|1");
        object_set_marked(get_obj_var(argv[1])->ptr, parse_size(argv[2]) != 0);

    /* ---- gc ---- */
    } else if (strcmp(cmd, "gc_alloc") == 0) {
        need_args(argc, 4, "gc_alloc VAR NUM_REFS DATA_SIZE");
        need_mode(MODE_GC, cmd);
        size_t refs = parse_size(argv[2]);
        size_t size = parse_size(argv[3]);
        Object *obj = gc_alloc(gc, refs, size);
        set_var(argv[1], VAR_OBJ, obj, size, refs);
        if (trace) {
            printf("    %s = %p\n", argv[1], (void *)obj);
        }
    } else if (strcmp(cmd, "gc_push_root") == 0) {
        need_args(argc, 2, "gc_push_root VAR");
        need_mode(MODE_GC, cmd);
        do_push(get_obj_var(argv[1])->ptr);
    } else if (strcmp(cmd, "gc_pop_root") == 0) {
        need_args(argc, 1, "gc_pop_root");
        need_mode(MODE_GC, cmd);
        do_pop();
    } else if (strcmp(cmd, "gc_pop_all") == 0) {
        need_args(argc, 1, "gc_pop_all");
        need_mode(MODE_GC, cmd);
        while (root_depth > 0) {
            do_pop();
        }
    } else if (strcmp(cmd, "gc_mark") == 0) {
        need_args(argc, 1, "gc_mark");
        need_mode(MODE_GC, cmd);
        gc_mark(gc);
    } else if (strcmp(cmd, "gc_sweep") == 0 || strcmp(cmd, "gc_collect") == 0) {
        need_args(argc, 1, cmd);
        need_mode(MODE_GC, cmd);
        last_result = cmd[3] == 's' ? gc_sweep(gc) : gc_collect(gc);
        if (trace) {
            printf("    freed %zu\n", last_result);
        }
    } else if (strcmp(cmd, "gc_fill") == 0) {
        need_args(argc, 3, "gc_fill NUM_REFS DATA_SIZE");
        need_mode(MODE_GC, cmd);
        cmd_fill(true, parse_size(argv[1]), parse_size(argv[2]), trace);
    } else if (strcmp(cmd, "gc_list") == 0) {
        need_args(argc, 3, "gc_list VAR N");
        need_mode(MODE_GC, cmd);
        cmd_list(argv[1], parse_size(argv[2]));
    } else if (strcmp(cmd, "gc_fanout") == 0) {
        need_args(argc, 2, "gc_fanout VAR");
        need_mode(MODE_GC, cmd);
        Var *v = get_obj_var(argv[1]);
        for (size_t i = 0; i < v->num_refs; i++) {
            Object *child = gc_alloc(gc, 0, sizeof(size_t));
            if (child == NULL) {
                check_failed("gc_fanout %s: gc_alloc returned NULL at child %zu", v->name, i);
                return;
            }
            memcpy(object_data(child), &i, sizeof i);
            object_set_ref(v->ptr, i, child);
        }

    /* ---- gen (Project 6) ---- */
    } else if (strcmp(cmd, "gen_alloc") == 0) {
        need_args(argc, 4, "gen_alloc VAR NUM_REFS DATA_SIZE");
        need_mode(MODE_GEN, cmd);
        size_t refs = parse_size(argv[2]);
        size_t size = parse_size(argv[3]);
        Object *obj = gen_alloc(gen, refs, size);
        set_var(argv[1], VAR_OBJ, obj, size, refs);
        if (trace) {
            printf("    %s = %p\n", argv[1], (void *)obj);
        }
    } else if (strcmp(cmd, "gen_write_ref") == 0) {
        need_args(argc, 4, "gen_write_ref VAR INDEX VAR|null");
        need_mode(MODE_GEN, cmd);
        Var *v = get_obj_var(argv[1]);
        size_t idx = parse_size(argv[2]);
        if (idx >= v->num_refs) {
            script_error("%s has only %zu refs", v->name, v->num_refs);
        }
        gen_write_ref(gen, v->ptr, idx, resolve_obj(argv[3]));
    } else if (strcmp(cmd, "gen_push_root") == 0) {
        need_args(argc, 2, "gen_push_root VAR");
        need_mode(MODE_GEN, cmd);
        Var *v = get_obj_var(argv[1]);
        gen_push_var(v);
    } else if (strcmp(cmd, "gen_pop_root") == 0) {
        need_args(argc, 1, "gen_pop_root");
        need_mode(MODE_GEN, cmd);
        gen_pop_var();
    } else if (strcmp(cmd, "gen_pop_all") == 0) {
        need_args(argc, 1, "gen_pop_all");
        need_mode(MODE_GEN, cmd);
        while (root_depth > 0) {
            gen_pop_var();
        }
    } else if (strcmp(cmd, "gen_minor_gc") == 0) {
        need_args(argc, 1, cmd);
        need_mode(MODE_GEN, cmd);
        gen_minor_gc(gen);
        if (trace) {
            GenStats s = gen_stats(gen);
            printf("    copied %zu, promoted %zu\n", s.last_copied, s.last_promoted);
        }
    } else if (strcmp(cmd, "gen_major_gc") == 0) {
        need_args(argc, 1, cmd);
        need_mode(MODE_GEN, cmd);
        gen_major_gc(gen);
        if (trace) {
            GenStats s = gen_stats(gen);
            printf("    promoted %zu, old_objects %zu\n", s.last_promoted, s.old_objects);
        }
    } else if (strcmp(cmd, "gen_list") == 0) {
        need_args(argc, 3, "gen_list VAR N");
        need_mode(MODE_GEN, cmd);
        cmd_gen_list(argv[1], parse_size(argv[2]));
    } else if (strcmp(cmd, "remember") == 0) {
        need_args(argc, 2, "remember VAR");
        Var *v = get_obj_var(argv[1]);
        v->remembered = v->ptr;
    } else if (strcmp(cmd, "object_init") == 0) {
        need_args(argc, 5, "object_init VAR PTR NUM_REFS DATA_SIZE");
        need_mode(MODE_HEAP, cmd);
        Var *p = get_live_var(argv[2]);
        if (p->kind != VAR_PTR) {
            script_error("'%s' is not a heap_alloc pointer", p->name);
        }
        size_t refs = parse_size(argv[3]);
        size_t size = parse_size(argv[4]);
        if (object_size_for(refs, size) > p->size) {
            script_error("%s (%zu bytes) is smaller than object_size_for(%zu, %zu) = %zu",
                         p->name, p->size, refs, size, object_size_for(refs, size));
        }
        Object *obj = object_init(p->ptr, refs, size);
        if (obj != p->ptr) {
            check_failed("object_init returned %p, expected the given memory %p", (void *)obj, p->ptr);
        }
        p->freed = true; /* 이제 이 메모리는 객체 변수가 쓴다 */
        set_var(argv[1], VAR_OBJ, p->ptr, size, refs);
    } else if (strcmp(cmd, "object_set_age") == 0) {
        need_args(argc, 3, "object_set_age VAR N");
        object_set_age(get_obj_var(argv[1])->ptr, (unsigned)parse_size(argv[2]));
    } else if (strcmp(cmd, "object_set_forwarded") == 0) {
        need_args(argc, 3, "object_set_forwarded VAR VAR|null");
        object_set_forwarded(get_obj_var(argv[1])->ptr, resolve_obj(argv[2]));

    /* ---- 공통 ---- */
    } else if (strcmp(cmd, "write") == 0) {
        need_args(argc, 3, "write VAR TEXT");
        Var *v = get_live_var(argv[1]);
        size_t len = strlen(argv[2]);
        if (len > v->size) {
            script_error("'%s' does not fit in %s (%zu bytes)", argv[2], v->name, v->size);
        }
        memcpy(var_data(v), argv[2], len);
    } else if (strcmp(cmd, "let") == 0) {
        need_args(argc, 3, "let NAME VALUE");
        if (num_lets == MAX_LETS) {
            script_error("too many lets");
        }
        size_t value = resolve_value(argv[2]);
        snprintf(lets[num_lets].name, sizeof lets[num_lets].name, "%s", argv[1]);
        lets[num_lets++].value = value;
        if (trace) {
            printf("    %s = %zu\n", argv[1], value);
        }
    } else if (strcmp(cmd, "repeat") == 0) {
        if (argc < 3) {
            script_error("usage: repeat N COMMAND...");
        }
        size_t n = parse_size(argv[1]);
        for (size_t i = 0; i < n; i++) {
            exec(argc - 2, argv + 2, false);
        }
    } else if (strcmp(cmd, "stats") == 0) {
        if (mode == MODE_HEAP) {
            HeapStats s = heap_stats(heap);
            printf("    size=%zu used=%zu blocks=%zu\n", s.size, s.used_bytes, s.num_blocks);
        } else if (mode == MODE_GEN) {
            GenStats s = gen_stats(gen);
            printf("    eden=%zu/%zu survivor=%zu/%zu old=%zu/%zu old_objects=%zu minor=%zu major=%zu\n",
                   s.eden_used, s.eden_size, s.survivor_used, s.survivor_size, s.old_used, s.old_size,
                   s.old_objects, s.minor_gcs, s.major_gcs);
        } else if (mode == MODE_GC) {
            GCStats s = gc_stats(gc);
            printf("    heap=%zu used=%zu live=%zu collections=%zu freed=%zu roots=%zu\n",
                   s.heap_size, s.used_bytes, s.live_objects, s.collections, s.last_freed, root_depth);
        }
    } else if (strcmp(cmd, "dump") == 0) {
        need_mode(MODE_GC, cmd);
        gc_dump(gc, stdout);
    } else if (strcmp(cmd, "expect") == 0) {
        if (argc < 2) {
            script_error("usage: expect ...");
        }
        cmd_expect(argc, argv);
    } else {
        script_error("unknown command '%s'", cmd);
    }
}

/* ---------- main ---------- */

static int tokenize(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    while (*p != '\0') {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0' || *p == '#') {
            break;
        }
        if (argc == MAX_TOKENS) {
            script_error("too many tokens");
        }
        argv[argc++] = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return argc;
}

static void *run_script(void *arg) {
    FILE *f = arg;
    char line[MAX_LINE];
    while (fgets(line, sizeof line, f) != NULL) {
        line_no++;
        line[strcspn(line, "\n")] = '\0';
        char display[MAX_LINE];
        snprintf(display, sizeof display, "%s", line);

        /* echo는 토큰으로 나누지 않고 줄 전체를 그대로 출력 */
        const char *start = display + strspn(display, " \t");
        if (strncmp(start, "echo", 4) == 0 && (start[4] == '\0' || isspace((unsigned char)start[4]))) {
            printf("  # %s\n", start + 4 + strspn(start + 4, " \t"));
            continue;
        }

        char *toks[MAX_TOKENS];
        int n = tokenize(line, toks);
        if (n == 0) {
            continue;
        }
        printf("  %3d| %s\n", line_no, display + strspn(display, " \t"));
        exec(n, toks, true);
    }
    fclose(f);

    if (mode == MODE_HEAP) {
        heap_destroy(heap);
    } else if (mode == MODE_GC) {
        gc_destroy(gc);
    } else if (mode == MODE_GEN) {
        gen_destroy(gen);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s TEST.gct\n", argv[0]);
        return 2;
    }
    script_path = argv[1];
    FILE *f = fopen(script_path, "r");
    if (f == NULL) {
        perror(script_path);
        return 2;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("(%s) begin\n", script_path);

    /* 스택 크기를 고정한 스레드에서 실행해서, 재귀 mark의 스택 오버플로를 재현 가능하게 만든다 */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, STACK_SIZE);
    pthread_t thread;
    if (pthread_create(&thread, &attr, run_script, f) != 0) {
        perror("pthread_create");
        return 2;
    }
    pthread_join(thread, NULL);

    printf("(%s) end\n", script_path);
    if (failures > 0) {
        printf("FAIL (%d check%s failed)\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
