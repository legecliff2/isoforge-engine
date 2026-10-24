#!/usr/bin/env python3
"""Job-count and shared-lock checks for tools/ci_local.sh.

Run: python3 tools/ci_local_jobs_test.py

Mirrors tools/ci_conditions_test.py: a single script, no test framework, non-zero
exit when an expectation fails. Shellcheck runs when it is on PATH.
"""
import os
import shutil
import signal
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SH = ROOT / "tools" / "ci_local.sh"
BASH = "/bin/bash"
bad = 0


def note(ok, label, detail=""):
    global bad
    if not ok:
        bad += 1
        print(f"FAIL {label} {detail}".rstrip())
    else:
        print(f"ok   {label}")


def clean_env(extra=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith("CI_LOCAL_")}
    env.setdefault("PATH", "/usr/bin:/bin")
    if extra:
        env.update(extra)
    return env


def run(script, env=None, timeout=10, args=None):
    cmd = [BASH, "-c", script, "bash", str(SH)]
    if args:
        cmd.extend(args)
    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        env=clean_env(env),
        timeout=timeout,
    )


def write_exe(path, body):
    path.write_text(body)
    path.chmod(0o755)


def bin_path(directory):
    return f"{directory}:/usr/bin:/bin"


def parse_jobs(stdout):
    line = stdout.strip().splitlines()[-1]
    jobs, _, reason = line.partition("\t")
    return int(jobs), reason


def resolve(env):
    proc = run(
        'set -uo pipefail\nsource "$1"\nci_local_resolve_jobs\n',
        env,
    )
    return proc


# floor(kb / (mb * 1024)) clamped to [1, nproc]. Golden values, not a second copy
# of the shell arithmetic, so a unit change (1000 vs 1024) fails here.
GOLDEN = [
    (1572864, 1536, 8, 1),       # exactly one 1536 MB job
    (1572863, 1536, 8, 1),       # one kB short of 1 job -> still 1
    (3145728, 1536, 8, 2),       # exactly 2
    (3145727, 1536, 8, 1),       # one kB short of 2
    (100, 1536, 4, 1),           # tiny MemAvailable
    (0, 1536, 4, 1),
    (16777216, 1536, 4, 4),      # 10 jobs before the nproc clamp
    (15728640, 1536, 8, 8),      # 15 GiB / 1.5 GiB = 10, clamp to 8 cores
    (2097152, 512, 8, 4),        # CI_LOCAL_MB_PER_JOB=512
    (67108864, 1536, 3, 3),      # plenty of RAM, clamp to nproc
]


def test_jobs_from_mem():
    for kb, mb, nproc, expect in GOLDEN:
        proc = run(
            'set -uo pipefail\nsource "$1"\nci_local_jobs_from_mem "$2" "$3" "$4"\n',
            args=[str(kb), str(mb), str(nproc)],
        )
        got = proc.stdout.strip()
        note(
            proc.returncode == 0 and got == str(expect),
            f"jobs_from_mem {kb}kB/{mb}MB nproc={nproc}",
            f"got {got!r} rc={proc.returncode} expect {expect}",
        )


def test_meminfo_and_overrides():
    with tempfile.TemporaryDirectory(prefix="ci-local-jobs-") as td:
        tmp = Path(td)
        bindir = tmp / "bin"
        bindir.mkdir()
        write_exe(bindir / "nproc", "#!/bin/sh\necho 8\n")
        huge = bindir / "sysctl-huge"
        write_exe(
            huge,
            "#!/bin/sh\ncase \"$2\" in\n"
            "  hw.memsize) echo 17179869184 ;;\n"  # 16 GiB, would be many jobs
            "  *) exit 1 ;;\n"
            "esac\n",
        )
        # Point PATH at a copy named sysctl for the "meminfo wins" case.
        write_exe(bindir / "sysctl", huge.read_text())

        low = tmp / "low"
        low.write_text("MemAvailable:   1572864 kB\n")
        two = tmp / "two"
        two.write_text("MemAvailable:\t3145728 kB\n")
        summed = tmp / "summed"
        summed.write_text(
            "MemFree: 1572864 kB\nBuffers: 1572864 kB\nCached: 0 kB\n"
        )
        missing = tmp / "no-such-meminfo"

        cases = [
            (
                {"CI_LOCAL_MEMINFO": str(low), "PATH": bin_path(bindir)},
                1,
                "MemAvailable=1572864kB",
            ),
            (
                {"CI_LOCAL_MEMINFO": str(two), "PATH": bin_path(bindir)},
                2,
                "MemAvailable=3145728kB",
            ),
            (
                {"CI_LOCAL_MEMINFO": str(summed), "PATH": bin_path(bindir)},
                2,
                "MemFree+Buffers+Cached=3145728kB",
            ),
            (
                {
                    "CI_LOCAL_MEMINFO": str(low),
                    "CI_LOCAL_JOBS": "6",
                    "PATH": bin_path(bindir),
                },
                6,
                "CI_LOCAL_JOBS=6",
            ),
        ]
        only = tmp / "only-nproc"
        only.mkdir()
        write_exe(only / "nproc", "#!/bin/sh\necho 5\n")

        for env, jobs, reason_bit in cases[:4]:
            proc = resolve(env)
            if proc.returncode != 0 or not proc.stdout.strip():
                note(False, f"resolve {reason_bit}", f"rc={proc.returncode} {proc.stderr}")
                continue
            got, reason = parse_jobs(proc.stdout)
            note(
                got == jobs and reason_bit in reason and "clamp [1, nproc=8]" in reason
                if "CI_LOCAL_JOBS" not in reason_bit
                else got == jobs and reason_bit in reason,
                f"resolve -> {jobs} ({reason_bit})",
                f"got {got} {reason!r}",
            )

        proc = resolve(
            {"CI_LOCAL_MEMINFO": str(missing), "PATH": bin_path(only)}
        )
        # only-nproc is not on the default bin_path prefix together with a sysctl.
        # bin_path(only) still has /usr/bin, which has no sysctl (that lives in /usr/sbin).
        if proc.returncode != 0 or not proc.stdout.strip():
            note(False, "no meminfo falls back to nproc", proc.stderr)
        else:
            got, reason = parse_jobs(proc.stdout)
            note(
                got == 5 and "no memory info" in reason and "nproc=5" in reason,
                "no meminfo falls back to nproc",
                f"got {got} {reason!r} stderr={proc.stderr!r}",
            )

        # Nothing that can report CPUs or memory.
        empty = tmp / "empty-bin"
        empty.mkdir()
        proc = resolve({"CI_LOCAL_MEMINFO": str(missing), "PATH": str(empty)})
        if proc.returncode != 0 or not proc.stdout.strip():
            note(False, "no nproc and no memory", proc.stderr)
        else:
            got, reason = parse_jobs(proc.stdout)
            note(
                got == 1 and "nproc=1" in reason,
                "no nproc and no memory -> 1",
                f"got {got} {reason!r}",
            )

        # sysctl hw.memsize: 3221225472 bytes = 3145728 kB = exactly 2 * 1536 MB.
        sysbin = tmp / "sysbin"
        sysbin.mkdir()
        write_exe(sysbin / "nproc", "#!/bin/sh\necho 8\n")
        write_exe(
            sysbin / "sysctl",
            "#!/bin/sh\ncase \"$2\" in\n"
            "  hw.memsize) echo 3221225472 ;;\n"
            "  *) exit 1 ;;\n"
            "esac\n",
        )
        proc = resolve({"CI_LOCAL_MEMINFO": str(missing), "PATH": bin_path(sysbin)})
        if proc.returncode != 0 or not proc.stdout.strip():
            note(False, "sysctl hw.memsize", proc.stderr)
        else:
            got, reason = parse_jobs(proc.stdout)
            note(
                got == 2 and "sysctl hw.memsize=3145728kB" in reason,
                "sysctl hw.memsize",
                f"got {got} {reason!r}",
            )

        # Free pages beat a large hw.memsize. 262144 * 4096 / 1024 = 1048576 kB -> 1 job.
        pagebin = tmp / "pagebin"
        pagebin.mkdir()
        write_exe(pagebin / "nproc", "#!/bin/sh\necho 8\n")
        write_exe(
            pagebin / "sysctl",
            "#!/bin/sh\ncase \"$2\" in\n"
            "  hw.pagesize) echo 4096 ;;\n"
            "  vm.page_free_count) echo 262144 ;;\n"
            "  hw.memsize) echo 17179869184 ;;\n"
            "  *) exit 1 ;;\n"
            "esac\n",
        )
        proc = resolve({"CI_LOCAL_MEMINFO": str(missing), "PATH": bin_path(pagebin)})
        if proc.returncode != 0 or not proc.stdout.strip():
            note(False, "sysctl page_free_count", proc.stderr)
        else:
            got, reason = parse_jobs(proc.stdout)
            note(
                got == 1 and "sysctl vm.page_free_count" in reason,
                "sysctl page_free_count beats hw.memsize",
                f"got {got} {reason!r}",
            )

        # MemAvailable wins over a sysctl that would allow many more jobs.
        proc = resolve(
            {"CI_LOCAL_MEMINFO": str(low), "PATH": bin_path(bindir)}
        )
        if proc.returncode != 0 or not proc.stdout.strip():
            note(False, "meminfo beats sysctl", proc.stderr)
        else:
            got, reason = parse_jobs(proc.stdout)
            note(
                got == 1 and reason.startswith("MemAvailable="),
                "meminfo beats sysctl",
                f"got {got} {reason!r}",
            )

        # Re-read: a second call sees a new meminfo. Not cached at process start.
        reread = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'source "$1"',
                    'export CI_LOCAL_MEMINFO="$2"',
                    "ci_local_resolve_jobs",
                    'export CI_LOCAL_MEMINFO="$3"',
                    "ci_local_resolve_jobs",
                ]
            ),
            {"PATH": bin_path(bindir)},
            args=[str(low), str(two)],
        )
        lines = [ln for ln in reread.stdout.splitlines() if ln.strip()]
        ok = (
            reread.returncode == 0
            and len(lines) == 2
            and lines[0].startswith("1\t")
            and lines[1].startswith("2\t")
        )
        note(ok, "memory is re-read on each resolve", f"{lines!r} {reread.stderr!r}")

        for env, needle in (
            ({"CI_LOCAL_JOBS": "0", "CI_LOCAL_MEMINFO": str(low)}, "CI_LOCAL_JOBS"),
            ({"CI_LOCAL_JOBS": "nope", "CI_LOCAL_MEMINFO": str(low)}, "CI_LOCAL_JOBS"),
            ({"CI_LOCAL_MB_PER_JOB": "0", "CI_LOCAL_MEMINFO": str(low)}, "CI_LOCAL_MB_PER_JOB"),
        ):
            proc = resolve(env)
            note(
                proc.returncode == 2 and needle in proc.stderr and not proc.stdout.strip(),
                f"reject {needle}={env[needle]}",
                f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}",
            )

        # -j wins over CI_LOCAL_JOBS and over a 1-job memory estimate.
        proc = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'source "$1"',
                    "jobs_fixed=3",
                    "export CI_LOCAL_JOBS=9",
                    'export CI_LOCAL_MEMINFO="$2"',
                    "ci_local_use_jobs build",
                    "printf '%s\\n' \"$par\"",
                ]
            ),
            {"PATH": bin_path(bindir)},
            args=[str(low)],
        )
        text = proc.stdout
        note(
            proc.returncode == 0
            and "ci_local: -j 3 for build (command-line -j 3)" in text
            and text.strip().splitlines()[-1] == "3",
            "command-line -j wins",
            f"rc={proc.returncode} out={text!r} err={proc.stderr!r}",
        )


def test_parse_and_help():
    proc = run(
        "\n".join(
            [
                "set -uo pipefail",
                'source "$1"',
                "ci_local_parse_args --job=clang --parallel 4 --fresh --no-ccache --full-tests",
                'printf "%s\\n" "$job" "$jobs_fixed" "$fresh" "$use_ccache" "$full_tests"',
            ]
        )
    )
    expect = "clang\n4\n1\n0\n1\n"
    note(proc.returncode == 0 and proc.stdout == expect, "parse args", repr(proc.stdout))

    proc = subprocess.run(
        [str(SH), "--help"],
        capture_output=True,
        text=True,
        env=clean_env(),
        timeout=10,
    )
    help_ok = proc.returncode == 0 and all(
        s in proc.stdout
        for s in (
            "CI_LOCAL_JOBS",
            "CI_LOCAL_MB_PER_JOB",
            "CI_LOCAL_LOCK",
            "CI_LOCAL_NO_LOCK",
            "MemAvailable",
            "flock",
        )
    )
    note(help_ok, "--help", f"rc={proc.returncode}")

    for args, code in ((["-j", "0"], 2), (["--job", "nope"], 2), (["--bogus"], 2)):
        proc = subprocess.run(
            [str(SH), *args],
            capture_output=True,
            text=True,
            env=clean_env(),
            timeout=10,
        )
        note(proc.returncode == code, f"exit {args}", f"rc={proc.returncode} err={proc.stderr!r}")


def test_source_shape():
    body = SH.read_text()
    run_job = body[body.index("ci_local_run_job() {") : body.index("\nci_local_main()")]
    main = body[body.index("ci_local_main() {") :]
    checks = [
        ("lint returns before the lock", run_job.index("return 0") < run_job.index("ci_local_acquire_lock")),
        ("lock before build -j", run_job.index("ci_local_acquire_lock") < run_job.index("ci_local_use_jobs build")),
        (
            "build -j is re-resolved",
            run_job.index("ci_local_use_jobs build")
            < run_job.index('cmake --build --preset "$preset" --parallel "$par"'),
        ),
        (
            "test -j is re-resolved after build",
            run_job.index('cmake --build --preset "$preset" --parallel "$par"')
            < run_job.index("ci_local_use_jobs test")
            < run_job.index('ctest --preset "$preset" --parallel "$par"'),
        ),
        (
            "startup log is before any job",
            main.index("ci_local_use_jobs || exit 2") < main.index("ci_local_run_job"),
        ),
        ("default lock path", "CI_LOCAL_LOCK:-/tmp/ci_local.lock" in body),
        ("default MB per job", "CI_LOCAL_MB_PER_JOB:-1536" in body),
    ]
    for label, ok in checks:
        note(ok, label)


def test_locks():
    with tempfile.TemporaryDirectory(prefix="ci-local-lock-") as td:
        tmp = Path(td)
        lock = tmp / "ci_local.lock"
        other = tmp / "other.lock"
        log = tmp / "queue.log"

        hold = "\n".join(
            [
                "set -uo pipefail",
                'source "$1"',
                'export CI_LOCAL_LOCK="$2"',
                "ci_local_acquire_lock",
                'printf "HELD %s %s\\n" "$(date +%s%N)" "$$" >>"$3"',
                "sleep 1.5",
                'printf "RELEASE %s %s\\n" "$(date +%s%N)" "$$" >>"$3"',
                "ci_local_release_lock",
            ]
        )
        waiter = "\n".join(
            [
                "set -uo pipefail",
                'source "$1"',
                'export CI_LOCAL_LOCK="$2"',
                "ci_local_acquire_lock",
                'printf "HELD %s %s\\n" "$(date +%s%N)" "$$" >>"$3"',
                "ci_local_release_lock",
            ]
        )
        env = clean_env()
        first = subprocess.Popen(
            [BASH, "-c", hold, "bash", str(SH), str(lock), str(log)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
        )
        deadline = time.time() + 5
        while time.time() < deadline and not log.exists():
            time.sleep(0.02)
        while time.time() < deadline:
            if log.exists() and "HELD" in log.read_text():
                break
            time.sleep(0.02)
        second = subprocess.Popen(
            [BASH, "-c", waiter, "bash", str(SH), str(lock), str(log)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
        )
        try:
            out1, err1 = first.communicate(timeout=10)
            out2, err2 = second.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            first.kill()
            second.kill()
            note(False, "two invocations serialize", "timed out (deadlock)")
            return

        lines = [ln.split() for ln in log.read_text().splitlines() if ln.strip()]
        # HELD pidA, RELEASE pidA, HELD pidB
        ok_order = (
            len(lines) == 3
            and lines[0][0] == "HELD"
            and lines[1][0] == "RELEASE"
            and lines[2][0] == "HELD"
            and lines[0][2] == lines[1][2]
            and lines[2][2] != lines[0][2]
            and int(lines[2][1]) > int(lines[1][1])
        )
        holder = lines[0][2] if lines else "?"
        waiting = f"waiting for lock (held by PID {holder})"
        note(
            first.returncode == 0
            and second.returncode == 0
            and ok_order
            and waiting in out2
            and "waiting for lock" not in out1,
            "two invocations serialize",
            f"order={lines} out2={out2!r} err1={err1!r} err2={err2!r}",
        )
        print("--- concurrency demo ---")
        print(f"lock: {lock}")
        print(f"pid {holder}: acquired")
        for line in out2.splitlines():
            print(f"pid {second.pid}: {line}")
        print(f"pid {holder}: released")
        if len(lines) == 3:
            print(f"pid {lines[2][2]}: acquired after release")
        print("serialized: yes" if ok_order and waiting in out2 else "serialized: NO")
        print("--- end concurrency demo ---")

        # Different CI_LOCAL_LOCK paths do not block each other.
        hold_other = subprocess.Popen(
            [BASH, "-c", hold, "bash", str(SH), str(other), str(tmp / "other.log")],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
        )
        other_log = tmp / "other.log"
        deadline = time.time() + 5
        while time.time() < deadline:
            if other_log.exists() and "HELD" in other_log.read_text():
                break
            time.sleep(0.02)
        started = time.time()
        quick = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'source "$1"',
                    'export CI_LOCAL_LOCK="$2"',
                    "ci_local_acquire_lock",
                    "echo GOT_OTHER_LOCK",
                    "ci_local_release_lock",
                ]
            ),
            {"CI_LOCAL_LOCK": str(lock)},
            timeout=5,
            args=[str(lock)],
        )
        # The env CI_LOCAL_LOCK and the script both point at `lock`, while `hold_other`
        # holds `other`. This call must not wait on `other`.
        elapsed = time.time() - started
        hold_other.wait(timeout=5)
        note(
            quick.returncode == 0
            and "GOT_OTHER_LOCK" in quick.stdout
            and "waiting for lock" not in quick.stdout
            and elapsed < 1.0,
            "CI_LOCAL_LOCK paths are independent",
            f"elapsed={elapsed:.2f}s out={quick.stdout!r} err={quick.stderr!r}",
        )

        # CI_LOCAL_NO_LOCK does not queue behind a held lock.
        blocker = subprocess.Popen(
            [BASH, "-c", hold, "bash", str(SH), str(lock), str(tmp / "block.log")],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
        )
        block_log = tmp / "block.log"
        deadline = time.time() + 5
        while time.time() < deadline:
            if block_log.exists() and "HELD" in block_log.read_text():
                break
            time.sleep(0.02)
        started = time.time()
        skipped = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'source "$1"',
                    'export CI_LOCAL_LOCK="$2"',
                    "export CI_LOCAL_NO_LOCK=1",
                    "ci_local_acquire_lock",
                    "echo SKIPPED_LOCK",
                ]
            ),
            timeout=5,
            args=[str(lock)],
        )
        elapsed = time.time() - started
        blocker.wait(timeout=5)
        note(
            skipped.returncode == 0
            and "shared lock disabled (CI_LOCAL_NO_LOCK=1)" in skipped.stdout
            and "SKIPPED_LOCK" in skipped.stdout
            and "waiting for lock" not in skipped.stdout
            and elapsed < 1.0,
            "CI_LOCAL_NO_LOCK=1 does not wait",
            f"elapsed={elapsed:.2f}s out={skipped.stdout!r}",
        )

        nested = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'source "$1"',
                    'export CI_LOCAL_LOCK="$2"',
                    "ci_local_acquire_lock",
                    'child=$(bash -c \'set -uo pipefail; source "$1"; ci_local_acquire_lock; echo CHILD_OK\' bash "$1")',
                    'printf "%s\\n" "$child"',
                    "echo PARENT_DONE",
                    "ci_local_release_lock",
                ]
            ),
            timeout=5,
            args=[str(tmp / "nested.lock")],
        )
        note(
            nested.returncode == 0
            and "CHILD_OK" in nested.stdout
            and "PARENT_DONE" in nested.stdout
            and "nested call; lock already held by PID" in nested.stdout
            and "waiting for lock" not in nested.stdout,
            "nested call does not deadlock",
            f"rc={nested.returncode} out={nested.stdout!r} err={nested.stderr!r}",
        )

        reentry = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'source "$1"',
                    'export CI_LOCAL_LOCK="$2"',
                    "ci_local_acquire_lock",
                    "ci_local_acquire_lock",
                    "echo REENTRY_OK",
                    "ci_local_release_lock",
                ]
            ),
            timeout=5,
            args=[str(tmp / "reentry.lock")],
        )
        note(
            reentry.returncode == 0
            and "REENTRY_OK" in reentry.stdout
            and "waiting for lock" not in reentry.stdout,
            "same-process re-entry does not deadlock",
            f"out={reentry.stdout!r} err={reentry.stderr!r}",
        )

        empty_bin = tmp / "no-flock"
        empty_bin.mkdir()
        missing_flock = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'source "$1"',
                    "ci_local_acquire_lock",
                    "ci_local_acquire_lock",
                    "echo NOFLOCK_OK",
                ]
            ),
            {"PATH": str(empty_bin)},
            timeout=5,
        )
        notices = missing_flock.stdout.count("flock not found; skipping shared build lock")
        note(
            missing_flock.returncode == 0 and notices == 1 and "NOFLOCK_OK" in missing_flock.stdout,
            "missing flock skips the lock once",
            f"out={missing_flock.stdout!r} err={missing_flock.stderr!r}",
        )

        # TERM while the lock is held must drop it so the next caller does not block.
        fifo = tmp / "fifo"
        os.mkfifo(fifo)
        sig_lock = tmp / "signal.lock"
        sig_script = "\n".join(
            [
                "set -uo pipefail",
                'source "$1"',
                'export CI_LOCAL_LOCK="$2"',
                'exec 3<>"$3"',
                "ci_local_acquire_lock",
                'echo READY $$',
                "read -r -t 30 -u 3 || true",
                "echo AFTER",
            ]
        )
        sig = subprocess.Popen(
            [BASH, "-c", sig_script, "bash", str(SH), str(sig_lock), str(fifo)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=clean_env(),
        )
        ready = ""
        deadline = time.time() + 5
        while time.time() < deadline:
            if sig.stdout is None:
                break
            line = sig.stdout.readline()
            if line:
                ready += line
                if "READY" in line:
                    break
        os.kill(sig.pid, 15)
        try:
            sig_out, sig_err = sig.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            sig.kill()
            note(False, "TERM releases the lock", "process ignored TERM")
            return
        follow = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'source "$1"',
                    'export CI_LOCAL_LOCK="$2"',
                    "ci_local_acquire_lock",
                    "echo SECOND_GOT",
                    "ci_local_release_lock",
                ]
            ),
            timeout=3,
            args=[str(sig_lock)],
        )
        note(
            sig.returncode in (-15, 143)
            and "READY" in ready
            and "AFTER" not in (ready + sig_out)
            and follow.returncode == 0
            and "SECOND_GOT" in follow.stdout
            and "waiting for lock" not in follow.stdout,
            "TERM releases the lock",
            f"rc={sig.returncode} ready={ready!r} out={sig_out!r} err={sig_err!r} "
            f"follow={follow.stdout!r} {follow.stderr!r}",
        )


def test_lock_permissions():
    """0666 creation, read-only flock fallback, and a dead inherited owner."""
    with tempfile.TemporaryDirectory(prefix="ci-local-perm-") as td:
        tmp = Path(td)
        created = tmp / "new.lock"
        proc = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'source "$1"',
                    'export CI_LOCAL_LOCK="$2"',
                    "ci_local_acquire_lock",
                    "ci_local_release_lock",
                    "echo RELEASED",
                ]
            ),
            timeout=5,
            args=[str(created)],
        )
        mode = created.stat().st_mode & 0o777 if created.exists() else 0
        note(
            proc.returncode == 0
            and mode == 0o666
            and "RELEASED" in proc.stdout
            and "skipping shared build lock" not in proc.stdout
            and "not writable" not in proc.stdout,
            "new lock file is mode 0666",
            f"mode={oct(mode)} out={proc.stdout!r} err={proc.stderr!r}",
        )

        repaired = tmp / "old.lock"
        repaired.write_text("stale\n")
        repaired.chmod(0o644)
        proc = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'source "$1"',
                    'export CI_LOCAL_LOCK="$2"',
                    "ci_local_acquire_lock",
                    "ci_local_release_lock",
                    "echo RELEASED",
                ]
            ),
            timeout=5,
            args=[str(repaired)],
        )
        mode = repaired.stat().st_mode & 0o777
        note(
            proc.returncode == 0
            and mode == 0o666
            and "not writable" not in proc.stdout
            and "skipping shared build lock" not in proc.stdout,
            "owner of a 0644 lock widens it to 0666",
            f"mode={oct(mode)} out={proc.stdout!r} err={proc.stderr!r}",
        )

        readonly = tmp / "ro.lock"
        readonly.write_text("7\n")
        readonly.chmod(0o444)
        # Hold the read-only fd open across a second flock attempt.
        hold = "\n".join(
            [
                "set -uo pipefail",
                'source "$1"',
                'ci_local_open_lock_fd "$2"',
                "flock -n 9",
                "echo HELD_RO",
                "sleep 1.2",
                "flock -u 9",
                "echo RELEASED_RO",
            ]
        )
        env = clean_env()
        first = subprocess.Popen(
            [BASH, "-c", hold, "bash", str(SH), str(readonly)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
        )
        held = ""
        deadline = time.time() + 5
        while time.time() < deadline and "HELD_RO" not in held:
            if first.stdout is None:
                break
            chunk = first.stdout.readline()
            if chunk:
                held += chunk
            else:
                time.sleep(0.02)
        probe = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'exec 9<"$2"',
                    "if flock -n 9; then echo FREE; else echo BUSY; fi",
                ]
            ),
            timeout=5,
            args=[str(readonly)],
        )
        out1, err1 = first.communicate(timeout=5)
        note(
            first.returncode == 0
            and "is not writable; locking read-only" in held
            and "HELD_RO" in held
            and "BUSY" in probe.stdout
            and "FREE" not in probe.stdout,
            "read-only fd still flocks",
            f"held={held!r} rest={out1!r} err={err1!r} probe={probe.stdout!r} {probe.stderr!r}",
        )

        blocked = tmp / "nowrite"
        blocked.mkdir()
        blocked.chmod(0o555)
        try:
            proc = run(
                "\n".join(
                    [
                        "set -uo pipefail",
                        'source "$1"',
                        'export CI_LOCAL_LOCK="$2"',
                        "ci_local_acquire_lock",
                        "echo CONTINUED",
                    ]
                ),
                timeout=5,
                args=[str(blocked / "ci_local.lock")],
            )
            note(
                proc.returncode == 0
                and "cannot open" in proc.stdout
                and "read-write or read-only" in proc.stdout
                and "skipping shared build lock" in proc.stdout
                and "CONTINUED" in proc.stdout,
                "skip the lock only when both opens fail",
                f"out={proc.stdout!r} err={proc.stderr!r}",
            )
        finally:
            blocked.chmod(0o755)

        dead = 1 << 22
        while Path(f"/proc/{dead}").exists():
            dead += 1
        stale = tmp / "stale.lock"
        proc = run(
            "\n".join(
                [
                    "set -uo pipefail",
                    'source "$1"',
                    'export CI_LOCAL_LOCK="$2"',
                    "export CI_LOCAL_LOCK_HELD=1",
                    'export CI_LOCAL_LOCK_OWNER="$3"',
                    "ci_local_acquire_lock",
                    'if bash -c \'exec 9<>"$1"; if flock -n 9; then echo FREE; else echo BUSY; fi\' bash "$2"; then',
                    "  :",
                    "fi",
                    "echo TOOK",
                    "ci_local_release_lock",
                ]
            ),
            timeout=5,
            args=[str(stale), str(dead)],
        )
        # args are appended after the script path, so $2 is the lock and $3 is the dead pid.
        # The inner bash receives the lock as $1 via "$2" of the outer script.
        note(
            proc.returncode == 0
            and f"lock owner PID {dead} is not running" in proc.stdout
            and "nested call" not in proc.stdout
            and "BUSY" in proc.stdout
            and "TOOK" in proc.stdout,
            "dead CI_LOCAL_LOCK_OWNER takes the lock again",
            f"out={proc.stdout!r} err={proc.stderr!r}",
        )


def test_cross_user_lock():
    """Two uids must queue on one lock, including a pre-existing 0644 file."""
    probe = subprocess.run(
        ["sudo", "-n", "-u", "nobody", "true"],
        capture_output=True,
        text=True,
        timeout=10,
    )
    if probe.returncode != 0:
        print("ok   cross-user lock skipped (sudo -u nobody unavailable)")
        return

    tag = f"{os.getpid()}-{time.time_ns()}"
    shared = Path("/tmp") / f"ci-local-shared-{tag}.lock"
    legacy = Path("/tmp") / f"ci-local-legacy-{tag}.lock"
    try:
        # Fresh lock: creator widens it to 0666, the other uid waits without a read-only fallback.
        hold = "\n".join(
            [
                "set -uo pipefail",
                'source "$1"',
                'export CI_LOCAL_LOCK="$2"',
                "ci_local_acquire_lock",
                "echo HOLDER_READY",
                "sleep 1.5",
                "echo HOLDER_RELEASE",
                "ci_local_release_lock",
            ]
        )
        holder = subprocess.Popen(
            [BASH, "-c", hold, "bash", str(SH), str(shared)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=clean_env(),
        )
        deadline = time.time() + 5
        ready = ""
        while time.time() < deadline and "HOLDER_READY" not in ready:
            if holder.stdout is None:
                break
            line = holder.stdout.readline()
            if line:
                ready += line
            else:
                time.sleep(0.02)
        nobody_script = "\n".join(
            [
                "set -uo pipefail",
                'source "$1"',
                'export CI_LOCAL_LOCK="$2"',
                "ci_local_acquire_lock",
                "echo NOBODY_GOT",
                "ci_local_release_lock",
            ]
        )
        guest = subprocess.Popen(
            [
                "sudo",
                "-n",
                "-u",
                "nobody",
                BASH,
                "-c",
                nobody_script,
                "bash",
                str(SH),
                str(shared),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            guest_out, guest_err = guest.communicate(timeout=8)
            hold_out, hold_err = holder.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            holder.kill()
            guest.kill()
            note(False, "other user queues on a 0666 lock", "timed out")
            return
        mode = shared.stat().st_mode & 0o777 if shared.exists() else 0
        # Mode 0666 is necessary but not sufficient on Linux: fs.protected_regular
        # blocks the other uid's read-write open in sticky /tmp. Read-only flock must
        # still queue, and it must not skip or spam Permission denied.
        queued = (
            holder.returncode == 0
            and guest.returncode == 0
            and mode == 0o666
            and "waiting for lock" in guest_out
            and "skipping shared build lock" not in guest_out
            and "NOBODY_GOT" in guest_out
            and "HOLDER_RELEASE" in (ready + hold_out)
            and "Permission denied" not in guest_err
        )
        note(
            queued,
            "other user queues on a 0666 lock",
            f"mode={oct(mode)} guest={guest_out!r} {guest_err!r} holder={ready!r} {hold_out!r} {hold_err!r}",
        )
        print("--- cross-user lock demo (0666) ---")
        print(f"lock: {shared} mode {oct(mode)}")
        print(f"ubuntu: {ready.strip()}")
        for line in guest_out.splitlines():
            print(f"nobody: {line}")
        print(f"ubuntu: {hold_out.strip()}")
        print("--- end 0666 demo ---")

        # Old 0644 lock owned by this user. nobody cannot open it read-write, but must still flock.
        legacy.write_text("")
        legacy.chmod(0o644)
        raw_hold = "\n".join(
            [
                "set -uo pipefail",
                'exec 9<>"$1"',
                "flock 9",
                'printf "%s\\n" $$ >"$1"',
                "echo HOLDER_READY",
                "sleep 1.5",
                "echo HOLDER_RELEASE",
                "flock -u 9",
            ]
        )
        raw = subprocess.Popen(
            [BASH, "-c", raw_hold, "bash", str(legacy)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=clean_env(),
        )
        ready = ""
        deadline = time.time() + 5
        while time.time() < deadline and "HOLDER_READY" not in ready:
            if raw.stdout is None:
                break
            line = raw.stdout.readline()
            if line:
                ready += line
            else:
                time.sleep(0.02)
        guest = subprocess.Popen(
            [
                "sudo",
                "-n",
                "-u",
                "nobody",
                BASH,
                "-c",
                nobody_script,
                "bash",
                str(SH),
                str(legacy),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            guest_out, guest_err = guest.communicate(timeout=8)
            raw_out, raw_err = raw.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            raw.kill()
            guest.kill()
            note(False, "0644 lock is taken read-only by another user", "timed out")
            return
        note(
            raw.returncode == 0
            and guest.returncode == 0
            and "is not writable; locking read-only" in guest_out
            and "waiting for lock" in guest_out
            and "skipping shared build lock" not in guest_out
            and "NOBODY_GOT" in guest_out
            and "Permission denied" not in guest_err,
            "0644 lock is taken read-only by another user",
            f"guest={guest_out!r} {guest_err!r} holder={ready!r} {raw_out!r} {raw_err!r}",
        )
        print("--- cross-user lock demo (legacy 0644, read-only flock) ---")
        print(f"lock: {legacy}")
        print(f"ubuntu: {ready.strip()} {raw_out.strip()}")
        for line in guest_out.splitlines():
            print(f"nobody: {line}")
        print("--- end 0644 demo ---")
    finally:
        for path in (shared, legacy):
            try:
                path.unlink()
            except FileNotFoundError:
                pass


def test_outer_flock_does_not_deadlock():
    """`flock <same lock> … ci_local.sh` must not wait on a lock this process already holds."""
    with tempfile.TemporaryDirectory(prefix="ci-local-outer-flock-") as td:
        lock = Path(td) / "ci_local.lock"
        lock.write_text("")
        script = "\n".join(
            [
                "set -uo pipefail",
                'source "$1"',
                'export CI_LOCAL_LOCK="$2"',
                "ci_local_acquire_lock",
                "echo CONTINUED",
                'if flock -n "$2"; then echo UNLOCKED; else echo STILL_HELD; fi',
                "echo DONE",
            ]
        )
        try:
            proc = subprocess.run(
                ["flock", str(lock), BASH, "-c", script, "bash", str(SH), str(lock)],
                capture_output=True,
                text=True,
                env=clean_env(),
                timeout=5,
            )
        except subprocess.TimeoutExpired as exc:
            out = exc.stdout or ""
            err = exc.stderr or ""
            note(False, "outer flock does not deadlock", f"timeout out={out!r} err={err!r}")
            return
        note(
            proc.returncode == 0
            and "lock already held on an inherited fd; not locking again" in proc.stdout
            and "CONTINUED" in proc.stdout
            and "STILL_HELD" in proc.stdout
            and "UNLOCKED" not in proc.stdout
            and "waiting for lock" not in proc.stdout
            and "DONE" in proc.stdout,
            "outer flock does not deadlock",
            f"rc={proc.returncode} out={proc.stdout!r} err={proc.stderr!r}",
        )

        hold = subprocess.Popen(
            ["flock", str(lock), "sleep", "30"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
        time.sleep(0.2)
        try:
            probe = run(
                "\n".join(
                    [
                        "set -uo pipefail",
                        'source "$1"',
                        'export CI_LOCAL_LOCK="$2"',
                        'if ci_local_inherited_lock "$2"; then echo INHERITED; else echo NOT_INHERITED; fi',
                    ]
                ),
                timeout=5,
                args=[str(lock)],
            )
            lines = probe.stdout.splitlines()
            note(
                probe.returncode == 0 and "NOT_INHERITED" in lines and "INHERITED" not in lines,
                "another process's flock is not an inherited lock",
                f"out={probe.stdout!r} err={probe.stderr!r}",
            )
        finally:
            os.killpg(hold.pid, signal.SIGTERM)
            try:
                hold.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(hold.pid, signal.SIGKILL)
                hold.wait(timeout=5)


def test_shellcheck():
    exe = shutil.which("shellcheck")
    if not exe:
        print("ok   shellcheck skipped (not installed)")
        return
    proc = subprocess.run([exe, str(SH)], capture_output=True, text=True)
    note(proc.returncode == 0, "shellcheck tools/ci_local.sh", proc.stdout + proc.stderr)


def main():
    test_jobs_from_mem()
    test_meminfo_and_overrides()
    test_parse_and_help()
    test_source_shape()
    test_locks()
    test_lock_permissions()
    test_cross_user_lock()
    test_outer_flock_does_not_deadlock()
    test_shellcheck()
    print("ALL EXPECTATIONS MET" if not bad else f"{bad} FAILED")
    raise SystemExit(1 if bad else 0)


if __name__ == "__main__":
    main()
