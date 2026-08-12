#!/usr/bin/env python3
"""Проверяет целостность реестра docs/FORK_DIVERGENCE.md против маркеров в коде.

Каждая строка таблицы реестра должна иметь ровно один соответствующий маркер
`// FORK: <id>` где-то в src/ или include/, и наоборот. Расхождение — сигнал,
что слияние с upstream/main могло тихо съесть намеренную дивергенцию форка
(маркер пропал из кода) либо что дивергенцию добавили в код, но не задокументировали
(маркер есть, строки в реестре нет).

Запускать вручную перед релизом форка и обязательно после `git merge upstream/main`
(см. процедуру в docs/FORK_DIVERGENCE.md). Кросс-платформенно (Windows/macOS/Linux) —
чистый Python, без shell-зависимостей.
"""
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
LEDGER = REPO_ROOT / "docs" / "FORK_DIVERGENCE.md"
CODE_DIRS = ["src", "include"]
CODE_SUFFIXES = {".cpp", ".h", ".hpp", ".cc"}

LEDGER_ROW_RE = re.compile(r"^\|\s*`([a-z0-9][a-z0-9-]*)`\s*\|")
MARKER_RE = re.compile(r"//\s*FORK:\s*([a-z0-9][a-z0-9-]*)")


def ledger_ids() -> set[str]:
    ids = set()
    in_table = False
    for line in LEDGER.read_text(encoding="utf-8").splitlines():
        if line.startswith("| id |"):
            in_table = True
            continue
        if in_table and not line.startswith("|"):
            break
        if not in_table:
            continue
        m = LEDGER_ROW_RE.match(line)
        if m:
            ids.add(m.group(1))
    return ids


def code_marker_ids() -> dict[str, list[str]]:
    hits: dict[str, list[str]] = {}
    for d in CODE_DIRS:
        base = REPO_ROOT / d
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.suffix not in CODE_SUFFIXES:
                continue
            for lineno, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1
            ):
                m = MARKER_RE.search(line)
                if m:
                    hits.setdefault(m.group(1), []).append(f"{path.relative_to(REPO_ROOT)}:{lineno}")
    return hits


def main() -> int:
    if not LEDGER.exists():
        print(f"ERROR: {LEDGER} не найден", file=sys.stderr)
        return 2

    ledger = ledger_ids()
    code = code_marker_ids()

    missing_in_code = sorted(ledger - code.keys())
    missing_in_ledger = sorted(code.keys() - ledger)

    ok = True
    if missing_in_code:
        ok = False
        print("ПРОПАЛИ МАРКЕРЫ В КОДЕ (есть в реестре, нет в src/include — дивергенция могла быть съедена слиянием):")
        for i in missing_in_code:
            print(f"  - {i}")
    if missing_in_ledger:
        ok = False
        print("НЕЗАДОКУМЕНТИРОВАННЫЕ МАРКЕРЫ (есть в коде, нет строки в docs/FORK_DIVERGENCE.md):")
        for i in missing_in_ledger:
            for loc in code[i]:
                print(f"  - {i} ({loc})")

    if ok:
        print(f"OK: {len(ledger)} дивергенций в реестре, все подтверждены маркерами в коде.")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
