#!/usr/bin/env python3
"""Parse docs/SRD_MONSTERS_DEFERRED.md into authored and parked monster rows.

Pure parser: no Tk, no validator, no second content path. Badges come only from
this ledger.

Sections are split on ``## `` headings. Pipe tables are read under the Authored
and Deferred sections only. ``### `` subsections whose titles are "What the sim
plays today" or a re-check are dropped before the table scan, and only a table
whose header cell is ``monster`` is read.

Parked ids are the ``srd51.monster.*`` backticks in the Deferred table's monster
cell. Authored ids are ``srd51.monster.`` plus a snake_case of the display name.
A name whose file is not at ``content/packs/srd51.core/monsters/<snake>.json``
is a warning; the parser does not guess another id. Partial rows are authored
rows whose notes cell contains ``**partial**``.

``Ledger.count_errors`` is empty only when the parsed row counts equal the
numbers in the ``## Authored (N)`` and ``## Deferred (N)`` headings.
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LEDGER = REPO_ROOT / "docs" / "SRD_MONSTERS_DEFERRED.md"
DEFAULT_MONSTERS_DIR = REPO_ROOT / "content" / "packs" / "srd51.core" / "monsters"
ID_PREFIX = "srd51.monster."

_H2 = re.compile(r"(?m)^## (.+)$")
_H3_SPLIT = re.compile(r"(?m)(?=^### )")
_H3_TITLE = re.compile(r"^### (.+)$", re.M)
_SKIP_SUB = re.compile(r"what the sim plays today|re-check", re.I)
_HEADING_COUNT = re.compile(r"^[A-Za-z]+ \((\d+)")
_MONSTER_ID = re.compile(r"`(srd51\.monster\.[a-z0-9_]+)`")
_SEPARATOR = re.compile(r":?-{3,}:?")
PARTIAL_MARK = "**partial**"


@dataclass(frozen=True)
class Authored:
    name: str
    monster_id: str
    snake: str
    path: Path | None
    partial: bool
    cr: str
    notes: str


@dataclass(frozen=True)
class Parked:
    name: str
    monster_id: str
    path: Path | None
    cr: str
    blocking: str


@dataclass(frozen=True)
class Ledger:
    authored: tuple[Authored, ...]
    parked: tuple[Parked, ...]
    warnings: tuple[str, ...]
    authored_expected: int | None
    parked_expected: int | None
    source: Path

    def count_errors(self) -> tuple[str, ...]:
        """Empty when parsed rows match the heading numbers."""
        errs: list[str] = []
        if self.authored_expected is None:
            errs.append("no ## Authored (N) heading")
        elif len(self.authored) != self.authored_expected:
            errs.append(
                f"authored rows {len(self.authored)} != heading {self.authored_expected}"
            )
        if self.parked_expected is None:
            errs.append("no ## Deferred (N) heading")
        elif len(self.parked) != self.parked_expected:
            errs.append(
                f"deferred rows {len(self.parked)} != heading {self.parked_expected}"
            )
        return tuple(errs)

    @property
    def partial(self) -> tuple[Authored, ...]:
        return tuple(row for row in self.authored if row.partial)


def snake_case(name: str) -> str:
    """Display name to the pack's monster file stem. Apostrophes drop; other
    non-alphanumerics become a single underscore. This does not look at disk."""
    text = name.strip().lower().replace("'", "").replace("\u2019", "")
    text = re.sub(r"[^a-z0-9]+", "_", text)
    return text.strip("_")


def load_ledger(
    path: Path | None = None,
    monsters_dir: Path | None = None,
) -> Ledger:
    path = DEFAULT_LEDGER if path is None else Path(path)
    monsters = DEFAULT_MONSTERS_DIR if monsters_dir is None else Path(monsters_dir)
    return parse_ledger(path.read_text(encoding="utf-8"), monsters, source=path)


def parse_ledger(text: str, monsters_dir: Path, source: Path | None = None) -> Ledger:
    monsters_dir = Path(monsters_dir)
    sections = _split_h2(text)
    authored_title, authored_body = _section(sections, "Authored")
    parked_title, parked_body = _section(sections, "Deferred")
    warnings: list[str] = []
    authored = _authored_rows(_drop_skipped(authored_body), monsters_dir, warnings)
    parked = _parked_rows(_drop_skipped(parked_body), monsters_dir, warnings)
    return Ledger(
        authored=tuple(authored),
        parked=tuple(parked),
        warnings=tuple(warnings),
        authored_expected=_heading_count(authored_title),
        parked_expected=_heading_count(parked_title),
        source=DEFAULT_LEDGER if source is None else Path(source),
    )


def _split_h2(text: str) -> list[tuple[str, str]]:
    matches = list(_H2.finditer(text))
    out: list[tuple[str, str]] = []
    for i, match in enumerate(matches):
        start = match.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        out.append((match.group(1).strip(), text[start:end]))
    return out


def _section(sections: list[tuple[str, str]], kind: str) -> tuple[str, str]:
    for title, body in sections:
        if title == kind or title.startswith(kind + " ") or title.startswith(kind + "("):
            return title, body
    return "", ""


def _heading_count(title: str) -> int | None:
    match = _HEADING_COUNT.match(title)
    if match is None:
        return None
    return int(match.group(1))


def _drop_skipped(body: str) -> str:
    kept: list[str] = []
    for part in _H3_SPLIT.split(body):
        match = _H3_TITLE.match(part)
        if match and _SKIP_SUB.search(match.group(1)):
            continue
        kept.append(part)
    return "".join(kept)


def _split_row(line: str) -> list[str]:
    body = line.strip()
    if body.startswith("|"):
        body = body[1:]
    if body.endswith("|"):
        body = body[:-1]
    return [cell.strip() for cell in body.split("|")]


def _is_separator(cells: list[str]) -> bool:
    if not cells or not any("-" in cell for cell in cells):
        return False
    return all(cell == "" or _SEPARATOR.fullmatch(cell.replace(" ", "")) for cell in cells)


def _monster_rows(text: str) -> list[list[str]]:
    """Data rows of the first pipe table whose header cell is ``monster``."""
    rows: list[list[str]] = []
    in_table = False
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped.startswith("|"):
            if in_table:
                break
            continue
        cells = _split_row(stripped)
        if not in_table:
            if cells and cells[0].casefold() == "monster":
                in_table = True
            continue
        if _is_separator(cells):
            continue
        rows.append(cells)
    return rows


def _join_from(cells: list[str], start: int) -> str:
    if len(cells) <= start:
        return ""
    return " | ".join(cells[start:])


def _file_for_stem(monsters_dir: Path, stem: str) -> Path | None:
    if not stem:
        return None
    path = monsters_dir / f"{stem}.json"
    if path.is_file():
        return path
    return None


def _authored_rows(body: str, monsters_dir: Path, warnings: list[str]) -> list[Authored]:
    rows: list[Authored] = []
    seen: set[str] = set()
    for cells in _monster_rows(body):
        name = cells[0] if cells else ""
        cr = cells[1] if len(cells) > 1 else ""
        notes = _join_from(cells, 3)
        snake = snake_case(name)
        monster_id = f"{ID_PREFIX}{snake}" if snake else ""
        path = _file_for_stem(monsters_dir, snake)
        if path is None:
            warnings.append(
                f"unmapped authored name {name!r}: no monsters/{snake}.json (not guessing an id)"
            )
        if monster_id and monster_id in seen:
            warnings.append(f"duplicate authored id {monster_id}")
        if monster_id:
            seen.add(monster_id)
        rows.append(Authored(
            name=name,
            monster_id=monster_id,
            snake=snake,
            path=path,
            partial=PARTIAL_MARK in notes,
            cr=cr,
            notes=notes,
        ))
    return rows


def _parked_display_name(cell: str) -> str:
    name = _MONSTER_ID.sub("", cell).replace("`", "")
    name = re.sub(r"\(\s*\)", "", name)
    return re.sub(r"\s+", " ", name).strip()


def _parked_rows(body: str, monsters_dir: Path, warnings: list[str]) -> list[Parked]:
    rows: list[Parked] = []
    seen: set[str] = set()
    for cells in _monster_rows(body):
        cell = cells[0] if cells else ""
        cr = cells[1] if len(cells) > 1 else ""
        blocking = _join_from(cells, 2)
        ids = _MONSTER_ID.findall(cell)
        if not ids:
            warnings.append(f"deferred row {cell!r} has no `srd51.monster.*` id (not guessing)")
            monster_id = ""
        else:
            monster_id = ids[0]
            if len(ids) > 1:
                warnings.append(f"deferred row {cell!r} has extra ids {ids[1:]}; using {monster_id}")
        if monster_id and monster_id in seen:
            warnings.append(f"duplicate parked id {monster_id}")
        if monster_id:
            seen.add(monster_id)
        stem = monster_id[len(ID_PREFIX):] if monster_id.startswith(ID_PREFIX) else ""
        rows.append(Parked(
            name=_parked_display_name(cell),
            monster_id=monster_id,
            path=_file_for_stem(monsters_dir, stem),
            cr=cr,
            blocking=blocking,
        ))
    return rows
