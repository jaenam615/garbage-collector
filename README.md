# Mark-and-Sweep GC in C

JVM 스타일의 mark-and-sweep 가비지 컬렉터를 C로 바닥부터 직접 만들어 보는 학습용 프로젝트입니다.

[PintOS](https://pintos-os.org/)처럼 과제가 여러 프로젝트로 나뉘어 있고, 각 프로젝트에는 테스트가 미리 준비되어 있습니다.
`src/`의 빈 함수를 채우고 `make check`로 테스트를 하나씩 통과시키면 됩니다.

```
  ✓ heap            16/16  make check-heap
  ✓ object          10/10  make check-object
  ✗ gc/mark          9/12  make check-mark
  ✗ gc/sweep         0/10  make check-sweep
  ✗ gc/integration   0/13  make check-integration
```

## 목차

- [시작하기](#시작하기)
- [구조](#구조)
- [테스트 시스템](#테스트-시스템)
- [Project 1: Heap](#project-1-heap)
- [Project 2: Object](#project-2-object)
- [Project 3: Mark](#project-3-mark)
- [Project 4: Sweep](#project-4-sweep)
- [Project 5: Integration](#project-5-integration)
- [디버깅 가이드](#디버깅-가이드)
- [FAQ](#faq)

---

## 시작하기

### 필요한 것

- C 컴파일러: `clang` (macOS 기본) 또는 `gcc`
- `make`, `python3`
- 디버거: `lldb` (macOS) 또는 `gdb` (Linux)

AddressSanitizer(ASan)를 쓰기 때문에 clang 또는 gcc 최신 버전을 권장합니다.
Linux에서 gcc를 쓰려면 `make CC=gcc check`.

### 처음 실행

```sh
git clone <this repo>
cd garbage-collector
make check
```

처음에는 모든 테스트가 실패합니다. 정상입니다.
이제 [Project 1](#project-1-heap)부터 시작하세요.

---

## 구조

```
.
├── include/              # 공개 인터페이스 (= 과제 명세). 수정하지 마세요.
│   ├── heap.h            #   Project 1
│   ├── object.h          #   Project 2
│   └── gc.h              #   Project 3 ~ 5
│
├── src/                  # 여러분이 구현할 곳
│   ├── heap/
│   │   ├── heap.c
│   │   └── heap_internal.h   # struct Heap 정의 (heap 모듈 내부 전용)
│   ├── object/
│   │   └── object.c          # struct Object 정의 + 구현
│   └── gc/
│       ├── gc_internal.h     # struct GC 정의 (gc.c, mark.c, sweep.c 공용)
│       ├── gc.c              # 생성/할당/루트/collect
│       ├── mark.c            # Project 3
│       └── sweep.c           # Project 4
│
├── tests/
│   ├── test_runner.c     # .gct 스크립트를 실행하는 C 프로그램
│   ├── run.py            # 전체 테스트 실행 + 결과 요약
│   ├── README.md         # 테스트 스크립트 문법
│   ├── heap/             # Project 1 테스트
│   ├── object/           # Project 2 테스트
│   └── gc/
│       ├── mark/         # Project 3 테스트
│       ├── sweep/        # Project 4 테스트
│       └── integration/  # Project 5 테스트
│
└── build/                # 빌드 결과물과 테스트 출력 (자동 생성)
```

계층은 아래에서 위로 쌓입니다. 위 계층은 아래 계층의 **공개 함수만** 씁니다.

```
   gc.h       gc_alloc, gc_collect, 루트 관리       ← Project 3~5
     │ uses
   object.h   객체 레이아웃: 헤더, refs, data       ← Project 2
     │ uses
   heap.h     고정 크기 메모리 위의 malloc/free      ← Project 1
```

`src/` 안의 파일 구성은 자유롭게 바꿔도 됩니다. `src/` 아래의 `.c` 파일은 전부 자동으로 빌드됩니다.
단, `include/`의 함수 시그니처는 테스트가 그대로 호출하므로 바꾸면 안 됩니다.

---

## 테스트 시스템

### 실행

```sh
make check                    # 전체
make check-heap               # 섹션 하나: heap, object, mark, sweep, integration
make check-gc                 # gc/ 아래 전부 (mark + sweep + integration)
make check F="heap object"    # 여러 섹션
make check F=coalesce         # 이름에 coalesce가 들어간 테스트만

make test  T=heap/split       # 테스트 하나를 실행하고 전체 출력 보기
make debug T=heap/split       # 테스트 하나를 lldb로 실행
```

### 결과 읽기

```
[heap]
  pass heap/alloc-one
  FAIL heap/coalesce-next
         FAIL tests/heap/coalesce-next.gct:12: expected d to be notnull, got 0x0
         → build/tests/heap/coalesce-next.output
```

- `FAIL 파일:줄: ...`: 스크립트 몇 번째 줄의 검사가 왜 실패했는지
- `CRASH AddressSanitizer: ...`: 메모리 오류. 마지막으로 실행한 스크립트 줄도 같이 나옵니다
- `TIMEOUT`: 10초 안에 끝나지 않음. 대부분 무한 루프입니다
- 전체 출력은 `build/tests/<섹션>/<이름>.output`에 남습니다

### 테스트는 스크립트다

테스트는 C 코드가 아니라 `.gct` 스크립트입니다. 명령 이름이 API 함수 이름과 같아서 그대로 읽힙니다.

```
# tests/heap/coalesce-next.gct
echo 앞 블록을 먼저 해제하고 뒤 블록을 해제: 두 구멍이 합쳐져야 큰 요청이 들어간다
heap_create 4096
heap_alloc a 1000
heap_alloc b 1000
heap_alloc c 1000
heap_alloc d 1500
expect null d
heap_free a
heap_free b
heap_alloc d 1500
expect notnull d
```

`make test T=heap/coalesce-next`로 실행하면 한 줄씩 무엇을 했는지 보여줍니다.

```
(tests/heap/coalesce-next.gct) begin
  # 앞 블록을 먼저 해제하고 뒤 블록을 해제: 두 구멍이 합쳐져야 큰 요청이 들어간다
    2| heap_create 4096
    3| heap_alloc a 1000
    a = 0x619000000088
  ...
```

문법 전체는 [tests/README.md](tests/README.md)에 있습니다. 직접 테스트를 추가하고 싶으면
`tests/<섹션>/<이름>.gct` 파일을 만들기만 하면 됩니다. 따로 등록할 필요는 없습니다.

### 테스트 환경

- 모든 코드는 **AddressSanitizer**와 함께 빌드됩니다. `malloc`으로 받은 영역 밖을 읽거나 해제된 메모리를 쓰면 바로 잡힙니다.
  (단, 힙 영역 *안에서* 블록끼리 침범하는 건 ASan이 모릅니다. 그건 테스트의 `expect data`, `expect fill-intact`가 잡습니다.)
- 테스트는 **C 스택이 256KB**인 스레드에서 돕니다. JVM의 `-Xss` 같은 것입니다. 객체 깊이만큼 재귀하면 터집니다.

---

## Project 1: Heap

> 구현: `src/heap/heap.c`, `src/heap/heap_internal.h` · 명세: `include/heap.h` · 테스트: `make check-heap`

### 배경

JVM은 시작할 때 OS에서 큰 메모리 덩어리 하나를 받아 두고(`-Xmx`), 그 안에서 객체를 배치합니다.
여러분도 `heap_create`에서 `malloc`을 **딱 한 번** 호출해 큰 영역을 받고,
그 뒤로는 `malloc` 없이 그 영역을 쪼개서 나눠 줍니다. 즉, malloc/free를 직접 만드는 과제입니다.

```
heap_create(4096)
┌──────────────────────────────────────────────────────────┐
│                         free                             │
└──────────────────────────────────────────────────────────┘

heap_alloc(100), heap_alloc(200), heap_alloc(100)
┌─────┬─────────┬─────┬────────────────────────────────────┐
│  A  │    B    │  C  │               free                 │
└─────┴─────────┴─────┴────────────────────────────────────┘

heap_free(B)  →  구멍이 생김. 다음 할당에서 재사용해야 한다.
┌─────┬─────────┬─────┬────────────────────────────────────┐
│  A  │  free   │  C  │               free                 │
└─────┴─────────┴─────┴────────────────────────────────────┘
```

### 구현할 것

| 함수 | 설명 |
|---|---|
| `heap_create` / `heap_destroy` | 영역 할당과 해제 |
| `heap_alloc` | 들어갈 빈 블록을 찾아 할당. 남는 공간은 **쪼개서(split)** 빈 블록으로 남긴다 |
| `heap_free` | 블록 해제. 앞뒤 빈 블록과 **합친다(coalesce)** |
| `heap_first` / `heap_next` | 할당된 블록을 주소 순서대로 순회 (Project 4의 sweep이 사용) |
| `heap_stats` | 크기, 사용 중인 바이트, 블록 수 |

### 요구사항

- 반환 주소는 8바이트(`HEAP_ALIGNMENT`) 정렬
- 해제된 공간은 재사용 가능
- 큰 빈 블록에서 작은 요청을 받으면 나머지를 쓸 수 있게 split
- 인접한 빈 블록은 coalesce (앞쪽, 뒤쪽, 양쪽 모두)

### 테스트

| 테스트 | 확인하는 것 |
|---|---|
| `create`, `alloc-one`, `free-one`, `free-null` | 기본 동작과 통계 |
| `alloc-aligned` | 정렬 |
| `alloc-no-overlap`, `alloc-until-full` | 블록끼리 겹치지 않음, 꽉 찰 때까지 할당 |
| `alloc-too-big` | 실패 처리 |
| `free-reuse`, `refill` | 해제한 공간 재사용 |
| `split` | 큰 빈 블록 쪼개기 |
| `coalesce-next`, `coalesce-prev`, `coalesce-both`, `free-all-then-big` | 빈 블록 합치기 |
| `walk` | `heap_first` / `heap_next` 순회 |

### 생각해 볼 것

- 블록마다 크기와 사용 여부를 어디에 적어 둘까요? (힌트: 블록 앞에 작은 헤더)
- 블록 크기만 알면 다음 블록의 위치를 계산할 수 있습니다. 그러면 힙 전체를 처음부터 끝까지 걸어갈 수 있습니다.
- 앞 블록과 합치려면 앞 블록이 어디서 시작하는지 알아야 합니다. 처음부터 다시 걸어가도 되지만, 더 빠른 방법이 있습니다. ("boundary tag"로 찾아보세요)
- 헤더 크기 때문에 `heap_alloc(1024)`는 1024바이트 힙에 들어가지 않습니다. 이건 정상입니다.

---

## Project 2: Object

> 구현: `src/object/object.c` · 명세: `include/object.h` · 테스트: `make check-object`

### 배경

JVM의 모든 객체는 헤더(mark word, 클래스 포인터)로 시작하고 그 뒤에 필드가 붙습니다.
GC는 필드 중에서 **다른 객체를 가리키는 참조**만 따라가면 됩니다.
그러려면 어떤 필드가 참조이고 어떤 필드가 그냥 숫자인지 알아야 합니다.

이 프로젝트에서는 문제를 단순하게 만들기 위해 참조 필드를 앞에, 원시 데이터를 뒤에 모아 둡니다.

```
Object* ─▶ ┌──────────────────────┐
           │ header               │  mark bit, num_refs, data_size ...
           ├──────────────────────┤
           │ refs[0]   ─────────────▶ 다른 Object
           │ refs[1]   = NULL     │
           │ ...                  │
           ├──────────────────────┤
           │ data[0 .. size-1]    │  GC가 보지 않는 바이트
           └──────────────────────┘
```

### 구현할 것

`object_new`, `object_num_refs`, `object_data_size`, `object_get_ref`, `object_set_ref`,
`object_data`, `object_is_marked`, `object_set_marked`

### 요구사항

- `object_new`는 `heap_alloc` 한 번으로 객체 전체(헤더 + refs + data)를 할당
- 반환하는 `Object*`는 `heap_alloc`이 반환한 주소와 **같아야** 함 (그래야 sweep이 힙을 순회하면서 각 블록을 객체로 볼 수 있음)
- 새 객체는 refs 전부 `NULL`, data 전부 0, mark bit 꺼짐. 재사용된 메모리라도 마찬가지

### 테스트

| 테스트 | 확인하는 것 |
|---|---|
| `new`, `empty`, `many`, `out-of-memory` | 생성 |
| `new-zeroed`, `new-zeroed-reused` | 초기화 (쓰레기 값이 남아 있던 메모리 포함) |
| `refs`, `layout` | refs와 data 위치 계산, 이웃 객체 침범 여부 |
| `mark-bit` | mark bit가 다른 필드를 건드리지 않음 |
| `is-heap-block` | `Object*` == 힙 블록 주소 |

### 생각해 볼 것

- `struct Object`의 마지막 멤버를 `Object *refs[]`(flexible array member)로 두면 refs 접근이 쉬워집니다.
- `data`는 refs 바로 뒤에서 시작합니다. 그 주소를 어떻게 계산할까요?

---

## Project 3: Mark

> 구현: `src/gc/gc.c`(생성, 할당, 루트), `src/gc/mark.c` · 명세: `include/gc.h` · 테스트: `make check-mark`

### 배경

어떤 객체가 쓰레기인지 판단하는 기준은 **루트에서 참조를 따라가서 도달할 수 있는가**입니다.
JVM에서 루트는 스레드 스택의 지역 변수, static 필드 같은 것이고, 여기서는 `gc_push_root`로 쌓는 루트 스택입니다.

```
roots: [ a ]

  a ──▶ b ──▶ c          d ──▶ e
                         ▲     │
                         └─────┘

gc_mark 후:  a, b, c 는 marked
             d, e 는 서로 가리키고 있지만 루트에서 닿지 않으므로 unmarked
```

### 구현할 것

- `gc_create`, `gc_destroy`: GC가 쓸 힙 만들기
- `gc_alloc`: 이 단계에서는 `object_new`만 호출하면 충분합니다 (자동 GC는 Project 5)
- `gc_push_root`, `gc_pop_root`: 루트 스택
- `gc_stats`: 최소한 `live_objects`
- `gc_mark`: 루트에서 도달 가능한 모든 객체의 mark bit 켜기

### 테스트

mark 테스트는 `gc_mark` 직후 각 객체의 mark bit를 직접 검사합니다. sweep을 구현하지 않아도 통과할 수 있습니다.

| 테스트 | 확인하는 것 |
|---|---|
| `root`, `no-roots`, `multiple-roots`, `pop-root` | 루트 처리 |
| `transitive`, `all-refs`, `diamond` | 참조 따라가기 |
| `self-ref`, `cycle` | 순환 참조에서 무한 루프에 빠지지 않기 |
| `no-free` | mark는 해제하지 않음 |
| `wide` | 참조가 512개인 객체 |
| `deep` | 길이 5만 리스트. **재귀 mark는 스택 오버플로로 실패** |

### 생각해 볼 것

- 재귀로 짜면 가장 쉽습니다. 일단 그렇게 짜서 `deep` 말고 나머지를 통과시켜 보세요.
- `deep`이 왜 터지는지 이해했다면, 재귀가 C 스택에 하던 일을 직접 만든 자료구조로 옮겨 보세요.
- 이미 mark된 객체를 다시 만나면 어떻게 해야 할까요? (`cycle`, `diamond`)

---

## Project 4: Sweep

> 구현: `src/gc/sweep.c` · 테스트: `make check-sweep`

### 배경

mark가 끝나면 힙의 모든 블록을 처음부터 끝까지 훑으면서 mark되지 않은 객체를 해제합니다.
살아남은 객체의 mark bit는 다음 GC를 위해 다시 꺼 둡니다.

### 구현할 것

`gc_sweep`: `heap_first` / `heap_next`로 힙을 순회하면서

- mark 안 된 객체: `heap_free`
- mark 된 객체: mark bit 끄기
- 해제한 개수 반환

### 테스트

sweep 테스트는 `object_set_marked`로 mark bit를 직접 켠 뒤 `gc_sweep`을 부릅니다. `gc_mark`와 무관하게 통과할 수 있습니다.

| 테스트 | 확인하는 것 |
|---|---|
| `unmarked-freed`, `all`, `none`, `empty` | 기본 동작과 반환값 |
| `clears-marks` | 살아남은 객체의 mark bit를 끄는지 |
| `ignores-roots` | sweep은 루트를 보지 않는다 |
| `consecutive` | 순회 도중 해제해도 다음 블록을 놓치지 않는지 |
| `holes`, `reuse`, `used-bytes` | 해제 후 재사용, 통계 |

### 생각해 볼 것

- `heap_free(p)` 다음에 `heap_next(h, p)`를 부르면 어떻게 될까요? coalesce까지 생각해 보세요.

---

## Project 5: Integration

> 구현: `src/gc/gc.c` · 테스트: `make check-integration`

### 배경

JVM은 사용자가 GC를 부르기를 기다리지 않습니다. 할당할 공간이 없을 때(allocation failure) 스스로 GC를 돌리고,
그래도 없으면 `OutOfMemoryError`를 던집니다.

### 구현할 것

- `gc_collect`: `gc_mark` + `gc_sweep` + 통계 갱신(`collections`, `last_freed`)
- `gc_alloc`: 할당 실패 시 `gc_collect` 후 한 번 더 시도, 그래도 안 되면 `NULL`
- `gc_stats`: 모든 필드

### 테스트

| 테스트 | 확인하는 것 |
|---|---|
| `collect`, `collect-empty`, `repeated`, `child-survives-parent`, `cycle-garbage` | collect 동작 |
| `alloc-zeroed-after-gc` | GC로 해제된 자리의 새 객체도 깨끗한지 |
| `auto-gc`, `auto-gc-keeps-roots`, `full-then-recover` | 자동 GC |
| `oom` | 전부 살아있으면 `NULL` |
| `mixed-sizes`, `churn`, `deep-list` | 스트레스 |

---

## 디버깅 가이드

### 1. 출력부터 본다

```sh
make test T=gc/sweep/holes
```

각 스크립트 줄과, 할당된 주소, GC가 해제한 개수가 순서대로 나옵니다. 실패한 줄 바로 위까지 무슨 일이 있었는지 확인하세요.
`stats` 명령을 스크립트 중간에 넣으면 그 시점의 통계도 볼 수 있습니다.

### 2. lldb로 들어간다

```sh
make debug T=gc/sweep/holes
(lldb) b gc_sweep
(lldb) run
(lldb) p *gc
(lldb) bt
(lldb) n / s / c
```

ASan 에러에서 멈추고 싶으면 `b __asan::ReportGenericError`를 걸어 두세요.

### 3. gc_dump를 구현한다 (선택)

`gc.h`의 `gc_dump(GC*, FILE*)`를 구현하면 스크립트에서 `dump` 명령으로 힙 상태를 찍어 볼 수 있습니다.
구현하지 않으면 통계만 출력됩니다. 블록마다 주소, 크기, 할당 여부, mark bit를 찍어 두면 sweep 디버깅이 훨씬 쉬워집니다.

### ASan 메시지 읽기

| 메시지 | 흔한 원인 |
|---|---|
| `heap-buffer-overflow` | 힙 영역(`malloc`으로 받은 큰 덩어리) 끝을 넘어 읽거나 씀. 마지막 블록 처리, 크기 계산 실수 |
| `heap-use-after-free` | `heap_destroy`/`gc_destroy` 후 접근, 또는 해제한 루트 배열 접근 |
| `stack-overflow` | 재귀 mark (`gc/mark/deep`) |
| `SEGV on unknown address` | 쓰레기 값을 포인터로 사용. 헤더가 덮어써졌을 가능성 |

---

## FAQ

**`include/`의 헤더를 바꿔도 되나요?**
함수 시그니처는 바꾸지 마세요. 테스트가 그대로 호출합니다. 주석이나 `#include`를 추가하는 정도는 괜찮습니다.

**파일을 더 만들어도 되나요?**
네. `src/` 아래의 `.c` 파일은 전부 자동으로 빌드됩니다.

**헤더 크기, 할당 전략(first-fit, best-fit ...)은 정해져 있나요?**
아니요. 테스트는 헤더 크기나 배치 전략에 의존하지 않도록 작성되어 있습니다.

**`malloc`을 써도 되나요?**
힙 영역 자체, 루트 스택, mark 작업용 스택처럼 **GC 객체가 아닌 것**에는 써도 됩니다.
객체는 반드시 `heap_alloc`으로 힙 안에 만들어야 합니다.

**나머지는 다 통과하는데 `gc/mark/deep`만 실패해요.**
[Project 3의 생각해 볼 것](#project-3-mark)을 보세요. 의도된 마지막 관문입니다.
