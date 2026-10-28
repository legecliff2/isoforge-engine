#!/usr/bin/env python3
"""Stdlib tkinter editor for the srd51.core bestiary.

Badges come from docs/SRD_MONSTERS_DEFERRED.md (ledger.py). Validate runs
tools/validate/validate_packs.py's main path on the whole pack (validate_bridge.py),
with the editor buffer swapped in. Save writes canonical JSON. The derive pane
shows checks_v4.check_monster_derive for documents with derive true.

  python3 tools/bestiary_editor/bestiary_editor.py

Tk is imported only in this file. A display is required to open the window.
"""
from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    import tkinter as tk
    from tkinter import messagebox, ttk
except ImportError:
    sys.exit(
        "bestiary_editor needs tkinter (Python standard library).\n"
        "Debian/Ubuntu: sudo apt-get install python3-tk"
    )

_EDITOR_DIR = Path(__file__).resolve().parent
if str(_EDITOR_DIR) not in sys.path:
    sys.path.insert(0, str(_EDITOR_DIR))

import ledger  # noqa: E402
import validate_bridge  # noqa: E402

PACK = ledger.REPO_ROOT / "content" / "packs" / "srd51.core"
DEMO_SELECT = "srd51.monster.wolf"


@dataclass(frozen=True)
class Row:
    badge: str
    name: str
    monster_id: str
    path: Path | None
    detail: str

    @property
    def rel(self) -> str | None:
        if self.path is None:
            return None
        try:
            return self.path.resolve().relative_to(PACK.resolve()).as_posix()
        except ValueError:
            return None


def rows_from_ledger(book: ledger.Ledger) -> list[Row]:
    rows = [
        Row(
            "Partial" if item.partial else "Authored",
            item.name,
            item.monster_id,
            item.path,
            item.notes,
        )
        for item in book.authored
    ]
    rows.extend(
        Row("Parked", item.name, item.monster_id, item.path, item.blocking)
        for item in book.parked
    )
    rows.sort(key=lambda row: (row.name.casefold(), row.monster_id))
    return rows


class Editor:
    def __init__(self, root: tk.Tk, book: ledger.Ledger) -> None:
        self.root = root
        self.book = book
        self.rows = rows_from_ledger(book)
        self.current: Row | None = None
        self._suspend = False
        self._loading = False

        root.title("Bestiary editor — srd51.core")
        root.geometry("1180x760")
        root.minsize(860, 560)

        partial_n = sum(1 for row in self.rows if row.badge == "Partial")
        authored_n = sum(1 for row in self.rows if row.badge in ("Authored", "Partial"))
        parked_n = sum(1 for row in self.rows if row.badge == "Parked")
        summary = f"{authored_n} authored ({partial_n} partial), {parked_n} parked"
        errs = book.count_errors()
        if errs:
            summary += " — LEDGER COUNT MISMATCH: " + "; ".join(errs)

        outer = ttk.Panedwindow(root, orient=tk.HORIZONTAL)
        outer.pack(fill=tk.BOTH, expand=True)

        left = ttk.Frame(outer, padding=6)
        right = ttk.Frame(outer, padding=6)
        outer.add(left, weight=1)
        outer.add(right, weight=3)

        ttk.Label(left, text=summary).pack(anchor=tk.W)
        ttk.Label(left, text="Authored · Partial ⊂ Authored · Parked = Deferred").pack(anchor=tk.W)
        self.filter_var = tk.StringVar()
        self.filter_var.trace_add("write", lambda *_a: self.refill())
        ttk.Entry(left, textvariable=self.filter_var).pack(fill=tk.X, pady=(4, 4))

        tree_wrap = ttk.Frame(left)
        tree_wrap.pack(fill=tk.BOTH, expand=True)
        self.tree = ttk.Treeview(
            tree_wrap, columns=("badge", "name", "id"), show="headings", selectmode="browse"
        )
        self.tree.heading("badge", text="Badge")
        self.tree.heading("name", text="Monster")
        self.tree.heading("id", text="Id")
        self.tree.column("badge", width=80, stretch=False)
        self.tree.column("name", width=160, stretch=True)
        self.tree.column("id", width=200, stretch=True)
        self.tree.tag_configure("Authored", foreground="#146c2e")
        self.tree.tag_configure("Partial", foreground="#8a5a00")
        self.tree.tag_configure("Parked", foreground="#555555")
        scroll = ttk.Scrollbar(tree_wrap, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.tree.bind("<<TreeviewSelect>>", self.on_select)

        header = ttk.Frame(right)
        header.pack(fill=tk.X)
        self.title = ttk.Label(header, text="Select a monster")
        self.title.pack(side=tk.LEFT)
        buttons = ttk.Frame(header)
        buttons.pack(side=tk.RIGHT)
        self.validate_btn = ttk.Button(buttons, text="Validate", command=self.on_validate)
        self.validate_btn.pack(side=tk.LEFT, padx=(0, 4))
        self.save_btn = ttk.Button(buttons, text="Save", command=self.on_save)
        self.save_btn.pack(side=tk.LEFT, padx=(0, 4))
        ttk.Button(buttons, text="Revert", command=self.on_revert).pack(side=tk.LEFT)

        self.status = ttk.Label(right, text="", wraplength=760)
        self.status.pack(fill=tk.X, pady=(4, 4))

        vertical = ttk.Panedwindow(right, orient=tk.VERTICAL)
        vertical.pack(fill=tk.BOTH, expand=True)

        json_frame = ttk.Labelframe(vertical, text="JSON", padding=4)
        val_frame = ttk.Labelframe(vertical, text="Validate (validate_packs.py main)", padding=4)
        derive_frame = ttk.Labelframe(
            vertical, text="Derive (checks_v4.check_monster_derive)", padding=4
        )
        vertical.add(json_frame, weight=3)
        vertical.add(val_frame, weight=2)
        vertical.add(derive_frame, weight=2)

        self.editor = self._text(json_frame)
        self.val_out = self._text(val_frame, readonly=True)
        self.derive_out = self._text(derive_frame, readonly=True)

        warn = ""
        if book.warnings:
            warn = f" {len(book.warnings)} ledger warning(s); first: {book.warnings[0]}"
        self.footer = ttk.Label(
            root,
            text="Save writes canonical JSON (indent 2). Validate checks the whole pack." + warn,
        )
        self.footer.pack(fill=tk.X, padx=6, pady=4)

        self.refill()
        self.select_id(DEMO_SELECT)

    def _text(self, parent: ttk.Labelframe, readonly: bool = False) -> tk.Text:
        wrap = ttk.Frame(parent)
        wrap.pack(fill=tk.BOTH, expand=True)
        widget = tk.Text(wrap, wrap="none", undo=True, font="TkFixedFont", height=8)
        ybar = ttk.Scrollbar(wrap, orient=tk.VERTICAL, command=widget.yview)
        xbar = ttk.Scrollbar(wrap, orient=tk.HORIZONTAL, command=widget.xview)
        widget.configure(yscrollcommand=ybar.set, xscrollcommand=xbar.set)
        widget.grid(row=0, column=0, sticky="nsew")
        ybar.grid(row=0, column=1, sticky="ns")
        xbar.grid(row=1, column=0, sticky="ew")
        wrap.rowconfigure(0, weight=1)
        wrap.columnconfigure(0, weight=1)
        if readonly:
            widget.configure(state="disabled")
        return widget

    def refill(self) -> None:
        query = self.filter_var.get().casefold().strip()
        current_id = self.current.monster_id if self.current is not None else None
        self._suspend = True
        self.tree.delete(*self.tree.get_children())
        for index, row in enumerate(self.rows):
            blob = f"{row.badge} {row.name} {row.monster_id}".casefold()
            if query and query not in blob:
                continue
            self.tree.insert(
                "", "end", iid=str(index),
                values=(row.badge, row.name, row.monster_id), tags=(row.badge,),
            )
        self._suspend = False
        if current_id is not None:
            self.select_id(current_id, force=False)

    def select_id(self, monster_id: str, force: bool = True) -> None:
        for iid in self.tree.get_children():
            if self.rows[int(iid)].monster_id == monster_id:
                self.tree.selection_set(iid)
                self.tree.see(iid)
                if force:
                    self.show_row(self.rows[int(iid)])
                return

    def on_select(self, _event: object = None) -> None:
        if self._suspend:
            return
        selected = self.tree.selection()
        if not selected:
            return
        row = self.rows[int(selected[0])]
        if self.current is not None and row.monster_id == self.current.monster_id and row.badge == self.current.badge:
            return
        if self._dirty() and not self._loading:
            if not messagebox.askyesno("Discard edits?", "Discard unsaved JSON edits?"):
                self._reselect_current()
                return
        self.show_row(row)

    def _reselect_current(self) -> None:
        if self.current is None:
            return
        self._suspend = True
        for iid in self.tree.get_children():
            row = self.rows[int(iid)]
            if row.monster_id == self.current.monster_id and row.badge == self.current.badge:
                self.tree.selection_set(iid)
                break
        self._suspend = False

    def _dirty(self) -> bool:
        try:
            return bool(self.editor.edit_modified())
        except tk.TclError:
            return False

    def show_row(self, row: Row) -> None:
        self._loading = True
        self.current = row
        file_bit = row.path.name if row.path is not None else "no content file"
        self.title.configure(text=f"{row.badge}  {row.name}  {row.monster_id}  ({file_bit})")
        detail = row.detail.strip()
        self.status.configure(text=detail if detail else "(no ledger notes)")
        if row.path is None or row.rel is None:
            self._set_editor(
                f"{row.name} ({row.monster_id or 'no id'})\n"
                f"Badge: {row.badge}\n\n"
                "No content file under content/packs/srd51.core/monsters/.\n"
                "Parked rows are deferred in docs/SRD_MONSTERS_DEFERRED.md.\n"
                "This editor does not invent a stat block for them.\n\n"
                f"{row.detail}\n",
                editable=False,
            )
            self.validate_btn.state(["disabled"])
            self.save_btn.state(["disabled"])
            self._set_readonly(self.val_out, "Nothing to validate: this row has no monster document.\n")
            self._set_readonly(
                self.derive_out,
                "No monster document.\ncheck_monster_derive is not run.\n",
            )
        else:
            try:
                text = row.path.read_text(encoding="utf-8")
            except OSError as exc:
                text = ""
                self._set_readonly(self.val_out, f"Could not read {row.path}: {exc}\n")
            self._set_editor(text, editable=True)
            self.validate_btn.state(["!disabled"])
            self.save_btn.state(["!disabled"])
            self._set_readonly(self.val_out, "Validate runs the whole pack with this buffer swapped in.\n")
            self._show_derive_from_text(text)
        self._loading = False

    def _set_editor(self, text: str, editable: bool) -> None:
        self.editor.configure(state="normal")
        self.editor.delete("1.0", "end")
        self.editor.insert("1.0", text)
        self.editor.edit_modified(False)
        self.editor.configure(state="normal" if editable else "disabled")

    def _set_readonly(self, widget: tk.Text, text: str) -> None:
        widget.configure(state="normal")
        widget.delete("1.0", "end")
        widget.insert("1.0", text)
        widget.configure(state="disabled")

    def _buffer(self) -> str:
        return self.editor.get("1.0", "end-1c")

    def _parse_buffer(self) -> tuple[dict | None, str | None]:
        raw = self._buffer()
        try:
            doc = json.loads(raw)
        except json.JSONDecodeError as exc:
            return None, f"JSON does not parse: {exc}\n"
        if not isinstance(doc, dict):
            return None, "JSON value must be an object.\n"
        return doc, None

    def on_revert(self) -> None:
        if self.current is not None:
            self.show_row(self.current)

    def on_validate(self) -> None:
        row = self.current
        if row is None or row.rel is None:
            return
        doc, err = self._parse_buffer()
        if err or doc is None:
            self._set_readonly(self.val_out, err or "JSON does not parse.\n")
            self._set_readonly(self.derive_out, "JSON does not parse. check_monster_derive was not run.\n")
            return
        self._set_readonly(self.val_out, "Validating the whole pack…\n")
        self.root.configure(cursor="watch")
        self.root.update_idletasks()
        try:
            _code, text = validate_bridge.validate_documents(PACK, {row.rel: doc})
        except Exception as exc:  # surface validator failures in the pane
            text = f"validate_packs.main raised {type(exc).__name__}: {exc}\n"
        finally:
            self.root.configure(cursor="")
        self._set_readonly(self.val_out, text)
        self._show_derive(doc)

    def on_save(self) -> None:
        row = self.current
        if row is None or row.path is None or row.rel is None:
            return
        doc, err = self._parse_buffer()
        if err or doc is None:
            messagebox.showerror("Save", err or "JSON does not parse.")
            return
        text = validate_bridge.canonical_json(doc)
        try:
            row.path.write_text(text, encoding="utf-8")
        except OSError as exc:
            messagebox.showerror("Save", str(exc))
            return
        self._set_editor(text, editable=True)
        self.on_validate()

    def _show_derive_from_text(self, text: str) -> None:
        try:
            doc = json.loads(text)
        except json.JSONDecodeError as exc:
            self._set_readonly(self.derive_out, f"JSON does not parse ({exc}). check_monster_derive was not run.\n")
            return
        if not isinstance(doc, dict):
            self._set_readonly(self.derive_out, "JSON value is not an object. check_monster_derive was not run.\n")
            return
        self._show_derive(doc)

    def _show_derive(self, doc: dict) -> None:
        if doc.get("derive") is not True:
            self._set_readonly(
                self.derive_out,
                "derive is not true.\n"
                "check_monster_derive is not run. It returns no findings unless derive is true.\n",
            )
            return
        try:
            errs, warns = validate_bridge.monster_derive(PACK, doc)
        except Exception as exc:
            self._set_readonly(
                self.derive_out,
                f"check_monster_derive raised {type(exc).__name__}: {exc}\n",
            )
            return
        lines = [f"derive: true  ({doc.get('id', '')})", f"errors ({len(errs)}):"]
        lines.extend(f"  {item}" for item in errs)
        if not errs:
            lines.append("  (none)")
        lines.append(f"warnings ({len(warns)}):")
        lines.extend(f"  {item}" for item in warns)
        if not warns:
            lines.append("  (none)")
        self._set_readonly(self.derive_out, "\n".join(lines) + "\n")


def main() -> None:
    try:
        book = ledger.load_ledger()
    except OSError as exc:
        sys.exit(f"could not read the monster ledger: {exc}")
    try:
        root = tk.Tk()
    except tk.TclError as exc:
        sys.exit(f"bestiary_editor could not open a window ({exc}). A display is required.")
    Editor(root, book)
    errs = book.count_errors()
    if errs:
        messagebox.showerror(
            "Ledger counts",
            "Parsed Authored/Deferred rows do not match the heading numbers:\n" + "\n".join(errs),
        )
    root.mainloop()


if __name__ == "__main__":
    main()
