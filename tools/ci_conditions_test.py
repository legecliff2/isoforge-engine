#!/usr/bin/env python3
"""Truth-table check of the job if: conditions and the ci-required gate in .github/workflows/ci.yml.
Evaluates the real if: strings for each event / label / paths-filter combination (no Actions needed).
Run: python3 tools/ci_conditions_test.py   (needs PyYAML). Exits non-zero if an expectation fails."""
import json,os,re,shutil,subprocess,tempfile,yaml,itertools
wf=yaml.safe_load(open(__import__('pathlib').Path(__file__).resolve().parents[1]/'.github/workflows/ci.yml'))
J=wf['jobs']
def ev(expr,ctx):
    e=expr.replace('\n',' ')
    e=re.sub(r"contains\(github\.event\.pull_request\.labels\.\*\.name,\s*'([^']+)'\)",lambda m:repr(m.group(1) in ctx['labels']),e)
    e=e.replace('always()','True').replace('&&',' and ').replace('||',' or ')
    e=re.sub(r"needs\.(\w[\w-]*)\.outputs\.(\w+)",lambda m:repr(ctx['out'].get(m.group(2),'')),e)
    e=re.sub(r"needs\.(\w[\w-]*)\.result",lambda m:repr(ctx['res'].get(m.group(1),'skipped')),e)
    e=e.replace("github.event.pull_request.head.repo.full_name == github.repository",repr(not ctx.get('fork',False)))
    e=e.replace('github.event.pull_request.draft == false',repr(not ctx.get('draft',False)))
    e=e.replace('github.event_name',repr(ctx['event']))
    e=e.replace(' == ',' == ').replace('!=','!=')
    return eval(e)
def gate_ok(ctx):
    if ctx['res']['changes']=='skipped' and not (ctx['event']=='pull_request' and ctx.get('draft')): return False
    # linux-runner skipped on a schedule, or on a code push/PR, would also skip the Linux
    # jobs and look green. Mirrors the shell check in ci-required.
    if ctx['res'].get('linux-runner')=='skipped' and ctx['event']=='schedule': return False
    if ctx['res'].get('linux-runner')=='skipped' and ctx['out'].get('code')=='true' and ctx['event'] in ('push','pull_request'): return False
    return all(ctx['res'][n] in('success','skipped') for n in J['ci-required']['needs'])
def run(event,labels=(),code=True,mac=False,fail=None,draft=False,fork=False,moved=True):
    # changes job: outputs only set by steps whose if matches (code: PR/push; mac: PR)
    out={'code':'true' if code and event in('pull_request','push') else '', 'mac':'true' if mac and event=='pull_request' else ''}
    ctx=dict(event=event,labels=set(labels),out=out,res={},draft=draft,fork=fork)
    ctx['res']['changes']='success' if ev(J['changes']['if'],ctx) else 'skipped'
    if ctx['res']['changes']=='skipped': ctx['out']={}
    lr=ctx['res']['changes']=='success' and ev(J['linux-runner']['if'],ctx)
    ctx['res']['linux-runner']='success' if lr else 'skipped'
    ng=ev(J['nightly-gate']['if'],ctx); ctx['res']['nightly-gate']='success' if ng else 'skipped'
    ctx['out']['run']='true' if ng and moved else ''
    for j in ['build-test','windows-release','macos']:
        c=ctx['res']['changes']=='success' or J[j]['if'].lstrip().startswith('always()')
        ctx['res'][j]=('failure' if fail==j else 'success') if c and ev(J[j]['if'],ctx) else 'skipped'
    gate=gate_ok(ctx)
    nr=ev(J['nightly-report']['if'],ctx)
    return ctx['res'],gate,nr
cases=[('pull_request',(),True,False),('pull_request',('ci-windows-release',),True,False),('pull_request',('other',),True,True),
('pull_request',('ci-windows-release',),False,False),('pull_request',(),False,False),('push',(),True,False),('push',(),False,False),
('schedule',(),True,False),('workflow_dispatch',(),True,False)]
print(f"{'event':18}{'labels':22}{'code':5}{'mac':5}| build-test windows-rel macos   | gate nightly-report")
for c in cases:
    r,g,n=run(*c); print(f"{c[0]:18}{','.join(c[1]) or '-':22}{str(c[2])[0]:5}{str(c[3])[0]:5}| {r['build-test']:10} {r['windows-release']:11} {r['macos']:7} | {'PASS' if g else 'FAIL'} {n}")
# failure propagation
for j in ['build-test','windows-release','macos']:
    for c in [('push',(),True,True),('pull_request',('ci-windows-release',),True,True),('schedule',(),True,False),('workflow_dispatch',(),True,False)]:
        r,g,n=run(*c,fail=j)
        if r[j]=='failure': assert not g,(j,c); print(f"fail {j:16} on {c[0]:18}-> gate {'PASS' if g else 'FAIL'}, nightly-report={n}")
EXPECT = {  # (event, labels, code, mac) -> (build-test, windows-release, macos)
    cases[0]: ('success', 'skipped', 'skipped'), cases[1]: ('success', 'success', 'skipped'),
    cases[2]: ('success', 'skipped', 'success'), cases[3]: ('skipped', 'skipped', 'skipped'),
    cases[4]: ('skipped', 'skipped', 'skipped'), cases[5]: ('success', 'success', 'skipped'),
    cases[6]: ('skipped', 'skipped', 'skipped'), cases[7]: ('skipped', 'skipped', 'success'),
    cases[8]: ('skipped', 'success', 'success'),
}
bad = 0
for c, exp in EXPECT.items():
    r, g, _ = run(*c)
    got = (r['build-test'], r['windows-release'], r['macos'])
    if got != exp or not g:
        bad += 1; print('MISMATCH', c, got, exp, g)
# New conditions: drafts, forks, nightly gate.
EXTRA = [  # kwargs -> (build-test, windows-release, macos, gate)
    (dict(event='pull_request',draft=True,mac=True,labels=('ci-windows-release',)), ('skipped','skipped','skipped',True)),
    (dict(event='pull_request',fork=True), ('skipped','skipped','skipped',False)),
    (dict(event='schedule',moved=False), ('skipped','skipped','skipped',True)),
    (dict(event='schedule',moved=True), ('skipped','skipped','success',True)),
    (dict(event='workflow_dispatch',moved=False), ('skipped','success','success',True)),
]
for kw,exp in EXTRA:
    r,g,_=run(**kw); got=(r['build-test'],r['windows-release'],r['macos'],g)
    print(f"{str(kw):70} -> {got}")
    if got!=exp: bad+=1; print('MISMATCH',kw,got,exp)
linux=[m for m in J['build-test']['strategy']['matrix']['include'] if m['name'].startswith('Linux')]
assert linux and all('self-hosted' in str(m['os']) and m['jobs']==4 and m['tests']==2 for m in linux)
wins=[m for m in J['build-test']['strategy']['matrix']['include'] if m['name'].startswith('Windows')]
assert [m['os'] for m in wins]==['"windows-latest"']
assert J['build-test']['needs']==['changes','linux-runner']
assert 'linux-runner' in str(J['build-test']['runs-on']) and 'linux-runner' in str(J['nightly-gate']['runs-on'])
assert 'gcc_runs_on' in str(J['build-test']['runs-on']) and 'clang_runs_on' in str(J['build-test']['runs-on'])
assert 'nightly_runs_on' in str(J['nightly-gate']['runs-on'])
assert J['nightly-gate']['needs']==['linux-runner']
assert 'ubuntu-latest' in str(J['linux-runner']['runs-on'])
assert wf['env']['CI_BURST'] in ('true', 'false')
can=yaml.safe_load(open(__import__('pathlib').Path(__file__).resolve().parents[1]/'.github/workflows/linux-runner-canary.yml'))
assert set(can[True])=={'workflow_dispatch'}
assert can['permissions']=={}
assert can['jobs']['ping']['runs-on']==['self-hosted','linux','isoforge']
assert all('uses' not in s for s in can['jobs']['ping']['steps'])
# Shared group still cancels in progress. Burst passes isolate=true, which turns it off.
cip = can['concurrency']['cancel-in-progress']
assert cip is True or cip == "${{ inputs.isolate != 'true' }}"
assert 'linux-runner-canary' in str(can['concurrency']['group'])
assert can[True]['workflow_dispatch']['inputs']['cid']['required'] is True
assert can[True]['workflow_dispatch']['inputs']['isolate']['default'] == 'false'
assert can['jobs']['ping'].get('if') == "github.ref == 'refs/heads/main'"
assert set(wf[True]['pull_request']['types'])=={'opened','synchronize','reopened','ready_for_review'}
# No workflow may be started by pull_request_target. The canary is dispatch-only.
root = __import__('pathlib').Path(__file__).resolve().parents[1]
for path in (root/'.github'/'workflows').glob('*.yml'):
    doc = yaml.safe_load(path.read_text())
    triggers = set(doc[True])
    assert 'pull_request_target' not in triggers, path
    for job in doc['jobs'].values():
        runs = job.get('runs-on')
        # A literal self-hosted runs-on (not a picker expression) must not be
        # reachable from a pull request event. The matrix label set in ci.yml is
        # not runs-on; the expression above it refuses the box for pull_request.
        if isinstance(runs, list) and ('self-hosted' in runs or 'isoforge' in runs):
            assert 'pull_request' not in triggers and 'pull_request_target' not in triggers, path
            blob = json.dumps(job)
            assert 'actions/checkout' not in blob, path
            assert 'github.event.pull_request' not in blob, path

def eval_runs_on(expr, *, event, ref, name, os_json, gcc='', clang='', nightly=''):
    """Evaluate a runs-on ${{ fromJSON(...) }} expression. Returns the JSON text."""
    e = expr.strip()
    assert e.startswith('${{') and e.endswith('}}'), expr
    e = e[3:-2].strip()
    assert e.startswith('fromJSON(') and e.endswith(')'), e
    e = e[len('fromJSON('):-1]
    e = re.sub(r"startsWith\(([^,]+),\s*('[^']*')\)", r"(\1).startswith(\2)", e)
    e = e.replace('&&', ' and ').replace('||', ' or ').replace('!(', 'not (')
    e = e.replace('github.event_name', repr(event)).replace('github.ref', repr(ref))
    e = e.replace('needs.linux-runner.outputs.gcc_runs_on', repr(gcc))
    e = e.replace('needs.linux-runner.outputs.clang_runs_on', repr(clang))
    e = e.replace('needs.linux-runner.outputs.nightly_runs_on', repr(nightly))
    e = e.replace('matrix.name', repr(name)).replace('matrix.os', repr(os_json))
    return eval(e)

BOX = '["self-hosted","linux","isoforge"]'
HOSTED = '"ubuntu-latest"'
bt = str(J['build-test']['runs-on'])
ng = str(J['nightly-gate']['runs-on'])
by_name = {m['name']: m['os'] for m in J['build-test']['strategy']['matrix']['include']}
# Even if the picker emits the box, a pull request (and any untrusted event) stays hosted.
# A trusted event may use the box. Windows never moves.
runner_cases = []
for event, ref, trusted in [
    ('pull_request', 'refs/pull/7/merge', False),
    ('pull_request_target', 'refs/heads/main', False),
    ('pull_request', 'refs/heads/main', False),
    ('push', 'refs/heads/feature', False),
    ('push', 'refs/heads/main', True),
    ('schedule', 'refs/heads/main', True),
    ('schedule', 'refs/heads/feature', False),
    ('workflow_dispatch', 'refs/heads/main', True),
    ('workflow_dispatch', 'refs/heads/feature', False),
    ('issue_comment', 'refs/heads/main', False),
]:
    for name in ('Linux GCC Debug', 'Linux Clang Release'):
        got = eval_runs_on(bt, event=event, ref=ref, name=name, os_json=by_name[name],
                           gcc=BOX, clang=BOX)
        runner = json.loads(got)
        expect = ['self-hosted', 'linux', 'isoforge'] if trusted else 'ubuntu-latest'
        if runner != expect:
            bad += 1
            print('RUNS-ON MISMATCH', event, ref, name, runner, expect)
        runner_cases.append((event, ref, name, runner))
    win = eval_runs_on(bt, event=event, ref=ref, name='Windows MSVC Debug',
                       os_json=by_name['Windows MSVC Debug'], gcc=BOX, clang=BOX)
    if json.loads(win) != 'windows-latest':
        bad += 1
        print('WINDOWS MOVED', event, ref, win)
    night = eval_runs_on(ng, event=event, ref=ref, name='', os_json='', nightly=BOX)
    night_runner = json.loads(night)
    expect = ['self-hosted', 'linux', 'isoforge'] if trusted else 'ubuntu-latest'
    if night_runner != expect:
        bad += 1
        print('NIGHTLY RUNS-ON MISMATCH', event, ref, night_runner, expect)
# Empty picker output on a trusted push falls back to ubuntu-latest, never the matrix box labels.
for name in ('Linux GCC Debug', 'Linux Clang Release'):
    got = json.loads(eval_runs_on(bt, event='push', ref='refs/heads/main', name=name,
                                  os_json=by_name[name], gcc='', clang=''))
    if got != 'ubuntu-latest':
        bad += 1
        print('EMPTY OUTPUT FELL THROUGH', name, got)
print('runs-on samples:', ', '.join(f"{c[0]}:{c[2]}={c[3] if isinstance(c[3], str) else 'box'}" for c in runner_cases[:6]))

def run_pick(event, ref, burst, gh_mode):
    step = J['linux-runner']['steps'][0]
    assert step['id'] == 'pick' and 'REF' in step['env']
    script = step['run']
    td = tempfile.mkdtemp(prefix='ci-pick-')
    try:
        gh = os.path.join(td, 'gh')
        with open(gh, 'w') as f:
            f.write(r'''#!/usr/bin/env python3
import os, sys, json
from datetime import datetime, timezone
log = os.environ["GH_LOG"]
mode = os.environ.get("GH_MODE", "forbid")
with open(log, "a") as fh:
    fh.write(" ".join(sys.argv[1:]) + "\n")
if "--input" in sys.argv:
    sys.stdin.read()
if mode == "fail":
    sys.stderr.write("denied\n")
    raise SystemExit(1)
if mode == "forbid":
    sys.stderr.write("gh called\n")
    raise SystemExit(99)
args = sys.argv[1:]
pos = []
i = 0
while i < len(args):
    a = args[i]
    if a in ("--method", "--jq", "--input"):
        i += 2
        continue
    if a.startswith("-"):
        i += 1
        continue
    pos.append(a)
    i += 1
url = pos[-1]
now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
title = "linux-runner-canary ci-" + os.environ.get("GITHUB_RUN_ID", "1")
if "actions/runners?" in url:
    json.dump({"total_count": 1, "runners": [{"name": "box", "status": "online", "busy": False,
        "labels": [{"name": n} for n in ("self-hosted", "linux", "isoforge", "X64")]}]}, sys.stdout)
    raise SystemExit(0)
if url.endswith("/dispatches"):
    raise SystemExit(0)
if "/workflows/linux-runner-canary.yml/runs?" in url:
    json.dump({"workflow_runs": [{"id": 100, "display_title": title, "name": title,
        "conclusion": None, "created_at": now}]}, sys.stdout)
    raise SystemExit(0)
if url.endswith("/jobs"):
    json.dump({"jobs": [{"started_at": now}]}, sys.stdout)
    raise SystemExit(0)
sys.stderr.write("unexpected " + url + "\n")
raise SystemExit(3)
''')
        os.chmod(gh, 0o755)
        outp = os.path.join(td, 'out')
        logp = os.path.join(td, 'gh.log')
        env = os.environ.copy()
        env.update({
            'PATH': td + os.pathsep + env.get('PATH', ''),
            'GITHUB_OUTPUT': outp,
            'GITHUB_RUN_ID': '42',
            'CI_BURST': 'true' if burst else 'false',
            'EVENT_NAME': event,
            'REF': ref,
            'REPO': 'legecliff2/isoforge-engine',
            'DEFAULT_BRANCH': 'main',
            'GH_LOG': logp,
            'GH_MODE': gh_mode,
            'PROBE_SECONDS': '2',
            'SLEEP_SECONDS': '1',
            'BURST_PROBE_SECONDS': '5',
            'BURST_SLEEP_SECONDS': '0',
            'BURST_CONFIRM_SECONDS': '0',
        })
        proc = subprocess.run(['bash', '-c', script], env=env, capture_output=True, text=True, timeout=30)
        log = open(logp).read() if os.path.exists(logp) else ''
        out = {}
        if os.path.exists(outp):
            for line in open(outp):
                k, v = line.rstrip('\n').split('=', 1)
                out[k] = v
        return proc.returncode, out, log, proc.stdout, proc.stderr
    finally:
        shutil.rmtree(td)

def hosted_out(out, tests):
    return (out.get('gcc_runs_on'), out.get('clang_runs_on'), out.get('nightly_runs_on'),
            out.get('runs_on'), out.get('hosted'), out.get('gcc_tests'), out.get('clang_tests')) == (
            '"ubuntu-latest"', '"ubuntu-latest"', '"ubuntu-latest"', '"ubuntu-latest"', 'true', tests, tests)

untrusted_pick = [
    ('pull_request', 'refs/pull/7/merge'),
    ('pull_request_target', 'refs/heads/main'),
    ('push', 'refs/heads/feature'),
    ('workflow_dispatch', 'refs/heads/feature'),
    ('schedule', 'refs/heads/feature'),
    ('issue_comment', 'refs/heads/main'),
]
for burst in (True, False):
    tests = '8' if burst else '4'
    for event, ref in untrusted_pick:
        code, out, log, stdout, stderr = run_pick(event, ref, burst, 'forbid')
        if code != 0 or not hosted_out(out, tests) or log.strip():
            bad += 1
            print('PICK UNTRUSTED', burst, event, ref, code, out, log, stderr[-400:])
# Trusted + box online. CI_BURST false: every Linux job on the box, no dispatch.
# CI_BURST true + push: GCC on the box, Clang and nightly hosted. Schedule stays hosted.
code, out, log, stdout, stderr = run_pick('push', 'refs/heads/main', False, 'online')
if code != 0 or 'self-hosted' not in out.get('gcc_runs_on', '') or out.get('gcc_runs_on') != out.get('clang_runs_on') \
        or out.get('nightly_runs_on') != out.get('gcc_runs_on') or out.get('gcc_tests') != '2' or out.get('hosted') != 'false' \
        or 'dispatches' in log:
    bad += 1
    print('PICK MAIN FALSE', code, out, log, stderr[-400:], stdout[-400:])
code, out, log, stdout, stderr = run_pick('schedule', 'refs/heads/main', False, 'online')
if code != 0 or 'self-hosted' not in out.get('nightly_runs_on', '') or out.get('gcc_tests') != '2' or 'dispatches' in log:
    bad += 1
    print('PICK SCHEDULE FALSE', code, out, log, stderr[-400:])
code, out, log, stdout, stderr = run_pick('workflow_dispatch', 'refs/heads/main', False, 'online')
if code != 0 or 'self-hosted' not in out.get('gcc_runs_on', '') or 'dispatches' in log:
    bad += 1
    print('PICK DISPATCH FALSE', code, out, log, stderr[-400:])
code, out, log, stdout, stderr = run_pick('push', 'refs/heads/main', True, 'online')
if code != 0 or 'self-hosted' not in out.get('gcc_runs_on', '') or out.get('clang_runs_on') != '"ubuntu-latest"' \
        or out.get('nightly_runs_on') != '"ubuntu-latest"' or out.get('gcc_tests') != '2' or out.get('clang_tests') != '8' \
        or 'dispatches' not in log:
    bad += 1
    print('PICK BURST PUSH', code, out, log, stderr[-400:], stdout[-400:])
code, out, log, stdout, stderr = run_pick('schedule', 'refs/heads/main', True, 'online')
if code != 0 or not hosted_out(out, '8') or log.strip():
    bad += 1
    print('PICK BURST SCHEDULE', code, out, log, stderr[-400:])
# Canary dispatch failure on main falls back to hosted and does not leave the box selected.
code, out, log, stdout, stderr = run_pick('push', 'refs/heads/main', False, 'fail')
if code != 0 or not hosted_out(out, '4') or 'actions/runners' not in log:
    bad += 1
    print('PICK FALLBACK', code, out, log, stderr[-400:])
print('ALL EXPECTATIONS MET' if not bad else f'{bad} FAILED')
raise SystemExit(1 if bad else 0)
