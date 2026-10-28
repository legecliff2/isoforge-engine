# Bestiary editor

Stdlib tkinter editor for `content/packs/srd51.core` monsters. Badges come from
`docs/SRD_MONSTERS_DEFERRED.md` only. Validate and the derive pane call
`tools/validate` (`validate_packs.main` and `checks_v4.check_monster_derive`).
No new packages, no schema changes, and no second validator.

## Requirements

- Python 3 with the standard library `tkinter` module, and a display.
  Debian/Ubuntu: `sudo apt-get install python3-tk`.
- The same third-party packages the pack validator already uses (`jsonschema`,
  `xxhash`). This tool does not add any. From a configured build, the interpreter
  is `build/content-venv/bin/python`. Without CMake:

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/validate/requirements.txt
.venv/bin/python tools/bestiary_editor/bestiary_editor.py
```

## Run

From the repository root (any working directory works; paths are based on this
file):

```sh
python3 tools/bestiary_editor/bestiary_editor.py
```

The window lists the ledger. Partial rows are a subset of Authored (the notes
cell contains `**partial**`). Parked rows are the Deferred table, ids taken from
`` `srd51.monster.*` `` backticks. A parked row with no file under
`content/packs/srd51.core/monsters/` shows the ledger's blocking mechanic and
does not invent a stat block. Goblin is `monster@3` and is not in the 191, so
it is not in this list.

| Demo | Badge | Id | File |
|---|---|---|---|
| Wolf | Authored | `srd51.monster.wolf` | `monsters/wolf.json` |
| Kobold | Partial | `srd51.monster.kobold` | `monsters/kobold.json` |
| Mimic | Parked | `srd51.monster.mimic` | none |

The list opens on Wolf. Wolf has `derive: true`, so the derive pane shows
`check_monster_derive` errors and warnings (none, on the shipped file).

**Validate** parses the buffer and runs `validate_packs.py`'s main on the whole
`srd51.core` pack, with that one document swapped in via a temporary overlay.
The real file is not written. Output uses the same `ok   `, `FAIL `, and
`warning ` lines as:

```sh
python3 tools/validate/validate_packs.py content/packs/srd51.core
```

That invocation does not pass `--script-check` and does not run the no-argument
effect-case or scenario passes.

**Save** writes canonical JSON (`json.dumps` with indent 2, a trailing newline,
key order preserved) to the existing monster file, then validates the pack.
An unedited wolf round-trip stays valid. Short one-line arrays in `wolf.json`
and `goblin.json` expand to multiple lines; the document's values do not change.

**Revert** reloads the file.

This draft does not soft-block Parked rows, does not change `ledger.md`, and
does not turn `check_monster_derive` into a new public derive API.

## Tests

Headless. `ledger.py` and `validate_bridge.py` do not import Tk. CMake and
depcheck are unchanged, so the test is not a ctest name yet. Run it with the
validator's unittest runner (the same one `content_validate_*` tests use):

```sh
python3 tools/validate/run_unittest_file.py tests/validate/test_bestiary_ledger.py
```

Use the validate venv when the system interpreter does not have `jsonschema`.
The runner exits non-zero if zero tests ran.
