#!/usr/bin/env python3
"""GC 테스트 러너 (PintOS 스타일).

    python3 tests/run.py                  # 전체
    python3 tests/run.py heap             # 섹션 하나 (heap, object, gc, mark, sweep, integration, gen)
    python3 tests/run.py mark sweep       # 여러 섹션
    python3 tests/run.py coalesce         # 이름에 coalesce가 들어간 테스트
    python3 tests/run.py -v heap/split    # 실패한 테스트의 전체 출력까지

각 테스트의 전체 출력은 build/tests/<섹션>/<이름>.output 에 저장된다.
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "tests"
BIN = ROOT / "build" / "test_runner"
OUT = ROOT / "build" / "tests"
TIMEOUT = 10

# 진행 순서 (README의 Project 1~6)
SECTION_ORDER = ["heap", "object", "gc/mark", "gc/sweep", "gc/integration", "gen"]

USE_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


def color(code, text):
    return f"\033[{code}m{text}\033[0m" if USE_COLOR else text


def section_of(rel: Path) -> str:
    return rel.parent.as_posix()


def section_key(section: str):
    return (SECTION_ORDER.index(section), section) if section in SECTION_ORDER else (len(SECTION_ORDER), section)


def matches(rel: Path, f: str) -> bool:
    f = f.strip("/")
    section = section_of(rel)
    # 섹션 이름: "gc" → gc/*, "gc/mark", 또는 마지막 이름만 "mark"
    if section == f or section.startswith(f + "/") or section.split("/")[-1] == f:
        return True
    # 그 외에는 경로 일부: "heap/split", "coalesce"
    return f in rel.with_suffix("").as_posix()


def run_one(script: Path):
    rel = script.relative_to(TESTS)
    name = rel.with_suffix("")
    out_path = OUT / name.with_suffix(".output")
    out_path.parent.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    try:
        proc = subprocess.run(
            [str(BIN), str(script.relative_to(ROOT))],
            cwd=ROOT, env=env, capture_output=True, text=True,
            errors="replace", timeout=TIMEOUT,
        )
        output = proc.stdout + proc.stderr
        code = proc.returncode
    except subprocess.TimeoutExpired as e:
        out = e.stdout or ""
        output = out.decode(errors="replace") if isinstance(out, bytes) else out
        output += f"\nTIMEOUT after {TIMEOUT}s (무한 루프?)\n"
        code = None

    out_path.write_text(output)
    lines = output.splitlines()

    if code == 0 and lines and lines[-1] == "PASS":
        return rel, True, [], out_path, output

    reasons = [l for l in lines
               if l.startswith(("FAIL ", "SCRIPT ERROR", "TIMEOUT")) and not l.startswith("FAIL (")]
    asan = next((l for l in lines if "ERROR: AddressSanitizer" in l), None)
    if asan:
        # "==123==ERROR: AddressSanitizer: heap-use-after-free on ..." → 핵심만
        reasons.append("CRASH " + asan.split("ERROR: ", 1)[1])
    elif code is not None and code < 0:
        reasons.append(f"CRASH signal {-code}")
    elif not reasons and code is not None:
        reasons.append(f"exited with code {code} without PASS")
    if asan or (code is not None and code < 0):
        last_step = next((l for l in reversed(lines) if l.startswith("  ") and "|" in l[:8]), None)
        if last_step:
            reasons.append("  마지막으로 실행한 줄:" + last_step.split("|", 1)[1])
    return rel, False, reasons, out_path, output


def main():
    ap = argparse.ArgumentParser(description="GC 테스트 러너")
    ap.add_argument("filters", nargs="*", help="섹션 이름 또는 테스트 경로 일부")
    ap.add_argument("-v", "--verbose", action="store_true", help="실패한 테스트의 전체 출력 표시")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    if not BIN.exists():
        print(f"{BIN.relative_to(ROOT)} 가 없습니다. `make check`로 실행하세요.", file=sys.stderr)
        return 2

    scripts = list(TESTS.rglob("*.gct"))
    if args.filters:
        scripts = [s for s in scripts if any(matches(s.relative_to(TESTS), f) for f in args.filters)]
    scripts.sort(key=lambda s: (section_key(section_of(s.relative_to(TESTS))), s.name))
    if not scripts:
        sections = sorted({section_of(s.relative_to(TESTS)) for s in TESTS.rglob("*.gct")}, key=section_key)
        print(f"일치하는 테스트가 없습니다. 섹션: {' '.join(sections)}")
        return 2

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(run_one, scripts))

    failed = []
    current = None
    for rel, ok, reasons, out_path, output in results:
        section = section_of(rel)
        if section != current:
            if current is not None:
                print()
            print(color("1", f"[{section}]"))
            current = section
        name = rel.with_suffix("").as_posix()
        if ok:
            print(f"  {color('32', 'pass')} {name}")
            continue
        failed.append(name)
        print(f"  {color('31;1', 'FAIL')} {name}")
        for r in reasons[:5]:
            print(f"         {r}")
        if len(reasons) > 5:
            print(f"         ... ({len(reasons) - 5} more)")
        if args.verbose:
            print(color("2", "".join(f"       | {l}\n" for l in output.splitlines())), end="")
        else:
            print(color("2", f"         → {out_path.relative_to(ROOT)}"))

    # 섹션별 요약
    by_section = {}
    for rel, ok, *_ in results:
        p, t = by_section.get(section_of(rel), (0, 0))
        by_section[section_of(rel)] = (p + ok, t + 1)
    print()
    if len(by_section) > 1:
        for section, (p, t) in by_section.items():
            mark = color("32", "✓") if p == t else color("31", "✗")
            target = section.split("/")[-1]
            print(f"  {mark} {section:<15} {p:>2}/{t:<2}  make check-{target}")
        print()

    total = len(results)
    if failed:
        print(color("31;1", f"{len(failed)} of {total} tests failed."))
        print(f"하나만 보기:  make test T={failed[0]}")
        print(f"lldb로 보기:  make debug T={failed[0]}")
    else:
        print(color("32;1", f"All {total} tests passed."))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
