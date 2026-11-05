# 테스트 스크립트 문법 (`.gct`)

테스트 하나 = `.gct` 파일 하나. 한 줄에 명령 하나, `#` 뒤는 주석입니다.
`tests/<섹션>/<이름>.gct`로 파일을 만들면 `make check`가 자동으로 찾습니다.

명령 이름은 대부분 API 함수 이름과 같습니다. 변수(`a`, `p` ...)는 러너가 들고 있는 포인터일 뿐이고 **GC 루트가 아닙니다**.
GC 모드에서 자동 GC 뒤에도 쓸 객체는 `gc_push_root`로 루트에 넣으세요.

숫자에는 `64K`, `8M`처럼 단위를 붙일 수 있습니다.

## 시작 (둘 중 하나, 맨 처음에 한 번)

| 명령 | 의미 |
|---|---|
| `heap_create SIZE` | heap 모드: heap / object 함수를 직접 테스트 |
| `gc_create SIZE` | gc 모드: GC를 통해 테스트 |

## heap 모드

| 명령 | 의미 |
|---|---|
| `heap_alloc P SIZE` | `P = heap_alloc(heap, SIZE)` |
| `heap_free P` / `heap_free null` | `heap_free(heap, P)` |
| `heap_fill SIZE` | NULL이 나올 때까지 `heap_alloc(SIZE)`. 각 블록을 블록 번호로 채움. 개수는 `filled` |
| `heap_free_fill` | `heap_fill`로 만든 블록 전부 해제 |
| `object_new A REFS DATA` | `A = object_new(heap, REFS, DATA)` |

## gc 모드

| 명령 | 의미 |
|---|---|
| `gc_alloc A REFS DATA` | `A = gc_alloc(gc, REFS, DATA)` |
| `gc_push_root A` / `gc_pop_root` | 루트 push / pop |
| `gc_pop_all` | 러너가 push한 루트를 전부 pop |
| `gc_mark` | `gc_mark(gc)` |
| `gc_sweep` / `gc_collect` | 반환값은 `result`에 저장 |
| `gc_fill REFS DATA` | NULL이 나올 때까지 `gc_alloc` + `gc_push_root`. 개수는 `filled` |
| `gc_list A N` | `refs[0]`으로 이어진 길이 N 리스트. 만드는 동안만 루트에 있음 |
| `gc_fanout A` | A의 모든 ref 슬롯에 새 자식 객체 연결 |
| `dump` | `gc_dump(gc, stdout)` |

## 공통

| 명령 | 의미 |
|---|---|
| `object_set_ref A I B` / `... null` | `object_set_ref(A, I, B)` |
| `object_set_marked A 0\|1` | `object_set_marked(A, ...)` |
| `write V TEXT` | V의 데이터 영역(객체면 `object_data`)에 TEXT 복사 |
| `let NAME VALUE` | 지금 값을 저장 (예: `let before used`) |
| `repeat N CMD...` | CMD를 N번 실행 (출력 없이) |
| `stats` | 현재 통계 출력 |
| `echo TEXT` | 출력에 설명 남기기 |

## 검사

검사가 실패해도 스크립트는 끝까지 진행하고, 마지막에 모아서 FAIL로 처리합니다.

| 명령 | 통과 조건 |
|---|---|
| `expect STAT OP VALUE` | 통계 비교. OP: `== != < <= > >=`. VALUE: 숫자, `let` 이름, 또는 다른 STAT |
| `expect null V` / `notnull V` | 할당 실패 / 성공 |
| `expect same A B` / `differ A B` | 포인터가 같음 / 다름 |
| `expect aligned P` | `HEAP_ALIGNMENT` 정렬 |
| `expect data V TEXT` | 데이터가 TEXT로 시작 |
| `expect walk` | `heap_first`/`heap_next`가 살아있는 블록 전부를, 주소 순서대로, 정확한 개수만큼 돌려줌 |
| `expect ref A I B` / `... null` | `object_get_ref(A, I) == B` |
| `expect shape A REFS DATA` | `object_num_refs`, `object_data_size` |
| `expect zeroed A` | refs 전부 NULL, data 전부 0, mark 꺼짐 |
| `expect marked A` / `unmarked A` | mark bit |
| `expect all-marked A` | A에서 도달 가능한 모든 객체가 mark 됨 |
| `expect listlen A N` | `gc_list`로 만든 리스트가 길이 N이고 내용이 안 깨짐 |
| `expect fill-intact` | `heap_fill`/`gc_fill`로 만든 블록 내용이 안 깨짐 |

### STAT

| 모드 | 이름 |
|---|---|
| heap | `size` `used` `blocks` `filled` |
| gc | `heap` `used` `live` `collections` `freed` `roots` `result` `filled` |
