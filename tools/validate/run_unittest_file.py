#!/usr/bin/env python3
"""Run one unittest file and fail if it ran zero tests.

CMake used to run tests/validate/test_*.py directly, so an empty (or broken
`if __name__` guard) file exited 0 and "passed". This loads the file as a module,
collects its unittest tests, and exits non-zero when nothing ran or anything failed.

    run_unittest_file.py path/to/test_x.py
"""
import importlib.util
import sys
import unittest
from pathlib import Path


def main(argv):
    if len(argv) != 1:
        print("usage: run_unittest_file.py <test_file.py>", file=sys.stderr)
        return 2
    path = Path(argv[0]).resolve()
    if not path.is_file():
        print(f"error: no such test file: {path}", file=sys.stderr)
        return 2
    sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[path.stem] = module
    spec.loader.exec_module(module)
    suite = unittest.defaultTestLoader.loadTestsFromModule(module)
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    if result.testsRun == 0:
        print(f"error: ZERO TESTS RAN in {path}", file=sys.stderr)
        return 1
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
