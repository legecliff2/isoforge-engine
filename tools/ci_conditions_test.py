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
include=J['build-test']['strategy']['matrix']['include']
linux=[m for m in include if m['name'].startswith('Linux')]
assert [m['name'] for m in include]==['Linux GCC Debug','Linux Clang Release','Windows MSVC Debug']
assert linux and all(m['jobs']==4 and m['tests']==4 and 'os' not in m for m in linux)
assert 'os' not in include[2]
assert J['build-test']['needs']==['changes','linux-runner']
# Linux is the literal ubuntu-latest. Only the Windows check name selects windows-latest.
# No fromJSON, no job output, no label list.
bt=str(J['build-test']['runs-on'])
assert bt=="${{ matrix.name == 'Windows MSVC Debug' && 'windows-latest' || 'ubuntu-latest' }}"
assert 'fromJSON' not in bt and 'needs.' not in bt and 'self-hosted' not in bt
for job_id, runs, timeout, name in (
    ('changes','ubuntu-latest',10,'Detect changes'),
    ('linux-runner','ubuntu-latest',5,'Linux runner'),
    ('nightly-gate','ubuntu-latest',10,'Nightly gate'),
    ('ci-required','ubuntu-latest',10,'ci-required'),
    ('nightly-report','ubuntu-latest',10,'Report nightly macOS failure'),
):
    assert J[job_id]['runs-on']==runs, job_id
    assert J[job_id]['timeout-minutes']==timeout, job_id
    assert J[job_id]['name']==name, job_id
assert J['build-test']['timeout-minutes']==90
assert J['build-test']['name']=='${{ matrix.name }}'
assert J['windows-release']['name']=='Windows MSVC Release'
assert J['windows-release']['runs-on']=='windows-latest'
assert J['macos']['name']=='macOS AppleClang Debug'
assert J['macos']['runs-on']=='macos-latest'
assert J['nightly-gate']['needs']==['linux-runner']
assert J['linux-runner'].get('permissions')=={}
assert 'runs_on' not in (J['linux-runner'].get('outputs') or {})
assert wf['env']['CI_BURST'] in ('true', 'false')
root=__import__('pathlib').Path(__file__).resolve().parents[1]
raw=(root/'.github/workflows/ci.yml').read_text()
assert "CI_BURST: 'true'" in raw or "CI_BURST: 'false'" in raw
assert 'box preferred' not in raw and 'linux-runner-canary' not in raw
assert not (root/'.github/workflows/linux-runner-canary.yml').exists()
for path in (root/'.github'/'workflows').glob('*.yml'):
    doc=yaml.safe_load(path.read_text())
    assert 'pull_request_target' not in set(doc[True]), path
    for job in doc['jobs'].values():
        blob=json.dumps(job.get('runs-on'))
        assert 'self-hosted' not in blob and 'isoforge' not in blob, (path, blob)
        for row in job.get('strategy',{}).get('matrix',{}).get('include',[]) or []:
            assert 'self-hosted' not in json.dumps(row) and 'isoforge' not in json.dumps(row)

def eval_runs_on(expr, *, name):
    e=expr.strip()
    assert e.startswith('${{') and e.endswith('}}'), expr
    e=e[3:-2].strip()
    e=e.replace('&&',' and ').replace('||',' or ').replace('matrix.name', repr(name))
    return eval(e)

for event, ref in [
    ('pull_request','refs/pull/7/merge'),
    ('pull_request_target','refs/heads/main'),
    ('push','refs/heads/main'),
    ('push','refs/heads/feature'),
    ('schedule','refs/heads/main'),
    ('workflow_dispatch','refs/heads/main'),
    ('workflow_dispatch','refs/heads/feature'),
]:
    for name in ('Linux GCC Debug','Linux Clang Release'):
        got=eval_runs_on(bt, name=name)
        if got!='ubuntu-latest':
            bad+=1; print('RUNS-ON', event, ref, name, got)
    win=eval_runs_on(bt, name='Windows MSVC Debug')
    if win!='windows-latest':
        bad+=1; print('WINDOWS', event, ref, win)

def run_pick(burst):
    step=J['linux-runner']['steps'][0]
    assert step['id']=='pick' and 'env' not in step
    script=step['run']
    assert 'self-hosted' not in script and 'isoforge' not in script and 'gh ' not in script
    assert 'runs_on' not in script
    td=tempfile.mkdtemp(prefix='ci-pick-')
    try:
        outp=os.path.join(td,'out')
        gh=os.path.join(td,'gh')
        open(gh,'w').write('#!/bin/sh\necho gh-called >&2\nexit 99\n')
        os.chmod(gh,0o755)
        env=os.environ.copy()
        env.update({'GITHUB_OUTPUT':outp,'CI_BURST':'true' if burst else 'false','PATH':td+os.pathsep+env.get('PATH','')})
        proc=subprocess.run(['bash','-c',script], env=env, capture_output=True, text=True, timeout=15)
        out={}
        for line in open(outp):
            k,v=line.rstrip('\n').split('=',1)
            out[k]=v
        return proc.returncode, out, proc.stdout, proc.stderr
    finally:
        shutil.rmtree(td)

for burst, tests in ((True,'8'),(False,'4')):
    code,out,stdout,stderr=run_pick(burst)
    hosted='runs_on' not in out and 'self-hosted' not in stdout
    caps=out.get('gcc_tests')==tests and out.get('clang_tests')==tests and out.get('gcc_jobs')=='4' and out.get('clang_jobs')=='4'
    if code!=0 or not hosted or not caps or 'gh-called' in stderr:
        bad+=1; print('PICK', burst, code, out, stderr[-300:], stdout[-300:])
print('ALL EXPECTATIONS MET' if not bad else f'{bad} FAILED')
raise SystemExit(1 if bad else 0)
