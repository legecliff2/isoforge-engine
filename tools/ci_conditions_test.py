#!/usr/bin/env python3
"""Truth-table check of the job if: conditions and the ci-required gate in .github/workflows/ci.yml.
Evaluates the real if: strings for each event / label / paths-filter combination, then runs the
ci-required shell (the same script the workflow step runs) with needs.*.result == 'success'
booleans filled the way Actions would. No Actions needed.
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
LAST_GATE=('', '', '')
def gate_ok(ctx):
    """Run the ci-required step script. True only when that script exits 0."""
    global LAST_GATE
    step=J['ci-required']['steps'][0]
    script=step['run']
    res=ctx['res']
    out=ctx.get('out') or {}
    def flag(name):
        return 'true' if res.get(name)=='success' else 'false'
    env=os.environ.copy()
    env.update({
        'CHANGES': res.get('changes',''),
        'LINUX_RUNNER': res.get('linux-runner',''),
        'NIGHTLY_GATE': res.get('nightly-gate',''),
        'BUILD_TEST': res.get('build-test',''),
        'WINDOWS_RELEASE': res.get('windows-release',''),
        'MACOS': res.get('macos',''),
        'CHANGES_SUCCESS': flag('changes'),
        'LINUX_RUNNER_SUCCESS': flag('linux-runner'),
        'NIGHTLY_GATE_SUCCESS': flag('nightly-gate'),
        'BUILD_TEST_SUCCESS': flag('build-test'),
        'WINDOWS_RELEASE_SUCCESS': flag('windows-release'),
        'MACOS_SUCCESS': flag('macos'),
        'EVENT': ctx['event'],
        'DRAFT': 'true' if ctx.get('draft') else 'false',
        'FORK': 'true' if ctx.get('fork') else 'false',
        'CODE': out.get('code',''),
        'MAC': out.get('mac',''),
        'NIGHTLY_RUN': out.get('run',''),
        'WIN_LABEL': 'true' if 'ci-windows-release' in ctx.get('labels',()) else 'false',
    })
    proc=subprocess.run(['bash','-c',script], env=env, capture_output=True, text=True)
    LAST_GATE=(proc.returncode, proc.stdout, proc.stderr)
    return proc.returncode==0
def run(event,labels=(),code=True,mac=False,fail=None,draft=False,fork=False,moved=True,override=None):
    # changes job: outputs only set by steps whose if matches (code: PR/push; mac: PR).
    # paths-filter writes the strings true and false. Empty is a step that did not run
    # (schedule, dispatch, or a skipped changes job).
    code_step=event in('pull_request','push')
    mac_step=event=='pull_request'
    out={
        'code':('true' if code else 'false') if code_step else '',
        'mac':('true' if mac else 'false') if mac_step else '',
    }
    ctx=dict(event=event,labels=set(labels),out=out,res={},draft=draft,fork=fork)
    ctx['res']['changes']='success' if ev(J['changes']['if'],ctx) else 'skipped'
    if ctx['res']['changes']=='skipped': ctx['out']={}
    lr=ctx['res']['changes']=='success' and ev(J['linux-runner']['if'],ctx)
    ctx['res']['linux-runner']='success' if lr else 'skipped'
    ng=ev(J['nightly-gate']['if'],ctx); ctx['res']['nightly-gate']='success' if ng else 'skipped'
    if ng:
        ctx['out']['run']='true' if moved else 'false'
    else:
        ctx['out']['run']=''
    for j in ['build-test','windows-release','macos']:
        c=ctx['res']['changes']=='success' or J[j]['if'].lstrip().startswith('always()')
        ctx['res'][j]=('failure' if fail==j else 'success') if c and ev(J[j]['if'],ctx) else 'skipped'
    if override:
        ctx['res'].update(override)
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
        bad += 1; print('MISMATCH', c, got, exp, g); print(LAST_GATE[1], LAST_GATE[2])
# New conditions: drafts, forks, nightly gate.
# Draft gate is failure: run 40114128710 (PR 318) was all-skipped with DRAFT=true and a green ci-required.
EXTRA = [  # kwargs -> (build-test, windows-release, macos, gate)
    (dict(event='pull_request',draft=True,mac=True,labels=('ci-windows-release',)), ('skipped','skipped','skipped',False)),
    (dict(event='pull_request',fork=True), ('skipped','skipped','skipped',False)),
    (dict(event='schedule',moved=False), ('skipped','skipped','skipped',True)),
    (dict(event='schedule',moved=True), ('skipped','skipped','success',True)),
    (dict(event='workflow_dispatch',moved=False), ('skipped','success','success',True)),
]
for kw,exp in EXTRA:
    r,g,_=run(**kw); got=(r['build-test'],r['windows-release'],r['macos'],g)
    print(f"{str(kw):70} -> {got}")
    if got!=exp:
        bad+=1; print('MISMATCH',kw,got,exp); print(LAST_GATE[1], LAST_GATE[2])
# Result combinations the job ifs would not produce on their own: skipped or cancelled
# where the event required the job, and skipped where it did not.
FORCED = [  # kwargs, result overrides, gate, label
    (dict(event='push', code=True), {'build-test':'skipped'}, False, 'push code build-test skipped'),
    (dict(event='push', code=True), {'build-test':'cancelled'}, False, 'push code build-test cancelled'),
    (dict(event='push', code=True), {'build-test':'failure'}, False, 'push code build-test failure'),
    (dict(event='push', code=True), {'windows-release':'skipped'}, False, 'push code windows-release skipped'),
    (dict(event='push', code=True), {'linux-runner':'skipped'}, False, 'push code linux-runner skipped'),
    (dict(event='push', code=False), {'macos':'skipped'}, True, 'push docs macos skipped'),
    (dict(event='push', code=False), {'macos':'cancelled'}, False, 'push docs macos cancelled'),
    (dict(event='pull_request', code=True), {'build-test':'skipped'}, False, 'pr code build-test skipped'),
    (dict(event='pull_request', code=True, mac=True), {'macos':'skipped'}, False, 'pr mac macos skipped'),
    (dict(event='pull_request', code=True, labels=('ci-windows-release',)), {'windows-release':'skipped'}, False, 'pr label windows skipped'),
    (dict(event='pull_request', code=True), {'windows-release':'skipped','macos':'skipped','nightly-gate':'skipped'}, True, 'pr code optional skips'),
    (dict(event='schedule', moved=True), {'macos':'skipped'}, False, 'schedule moved macos skipped'),
    (dict(event='schedule', moved=False), {'macos':'skipped'}, True, 'schedule idle macos skipped'),
    (dict(event='schedule'), {'nightly-gate':'skipped'}, False, 'schedule nightly-gate skipped'),
    (dict(event='schedule'), {'linux-runner':'failure'}, False, 'schedule linux-runner failure'),
    (dict(event='workflow_dispatch'), {'macos':'skipped'}, False, 'dispatch macos skipped'),
    (dict(event='workflow_dispatch'), {'windows-release':'cancelled'}, False, 'dispatch windows cancelled'),
    (dict(event='workflow_dispatch'), {'build-test':'skipped','linux-runner':'skipped','nightly-gate':'skipped'}, True, 'dispatch optional skips'),
    (dict(event='pull_request', draft=True, code=True, mac=True, labels=('ci-windows-release',)),
     {'changes':'success','linux-runner':'success','build-test':'success','windows-release':'success','macos':'success','nightly-gate':'success'},
     False, 'draft fails even if results look green'),
]
for kw, override, expect, label in FORCED:
    r,g,_=run(override=override, **kw)
    print(f"{label:42} -> gate {'PASS' if g else 'FAIL'}")
    if g!=expect:
        bad+=1; print('MISMATCH', label, g, expect, r); print(LAST_GATE[1], LAST_GATE[2])
# Docs-only (paths-filter code/mac are the strings false, not empty) must pass when
# changes succeeded and the build jobs are skipped. A draft that skips every job must
# still fail, including when the result strings are all skipped. failure and cancelled
# on a build that this event did not require still fail the gate.
NAMED = [  # kwargs, (build-test, windows-release, macos), gate, label
    (dict(event='pull_request', code=False, mac=False), ('skipped','skipped','skipped'), True, 'docs-only pull_request pass'),
    (dict(event='push', code=False), ('skipped','skipped','skipped'), True, 'docs-only push pass'),
    (dict(event='pull_request', code=False, mac=False, labels=('ci-windows-release',)), ('skipped','skipped','skipped'), True, 'docs-only pull_request windows label pass'),
    (dict(event='pull_request', draft=True, code=True, mac=True, labels=('ci-windows-release',)), ('skipped','skipped','skipped'), False, 'draft all-skip fail'),
    (dict(event='pull_request', fork=True, code=True), ('skipped','skipped','skipped'), False, 'fork all-skip fail'),
]
for kw, jobs, expect, label in NAMED:
    r,g,_=run(**kw)
    got=(r['build-test'], r['windows-release'], r['macos'])
    print(f"{label:42} -> jobs={got} gate={'PASS' if g else 'FAIL'}")
    if got!=jobs or g!=expect:
        bad+=1; print('MISMATCH', label, got, jobs, g, expect); print(LAST_GATE[1], LAST_GATE[2])
    if label.startswith('docs-only') and expect and r['changes']!='success':
        bad+=1; print('MISMATCH', label, 'changes', r['changes'])
    if label.startswith('docs-only') and expect and 'code=false' not in LAST_GATE[1]:
        bad+=1; print('MISMATCH', label, 'expected code=false', LAST_GATE[1])
# Same shell env a docs-only pull_request produces: code=false, every build skipped.
for kw, override, expect, label in (
    (dict(event='pull_request', code=False, mac=False),
     {'linux-runner':'skipped','build-test':'skipped','windows-release':'skipped','macos':'skipped','nightly-gate':'skipped'},
     True, 'docs-only explicit build skips pass'),
    (dict(event='pull_request', code=False, mac=False), {'build-test':'failure'}, False, 'docs-only build-test failure fails'),
    (dict(event='pull_request', code=False, mac=False), {'macos':'cancelled'}, False, 'docs-only macos cancelled fails'),
    (dict(event='push', code=False), {'build-test':'skipped','windows-release':'skipped','linux-runner':'skipped'}, True, 'docs-only push explicit skips pass'),
    (dict(event='pull_request', draft=True),
     {'changes':'skipped','linux-runner':'skipped','nightly-gate':'skipped','build-test':'skipped','windows-release':'skipped','macos':'skipped'},
     False, 'draft all-skip forced results fail'),
):
    r,g,_=run(override=override, **kw)
    print(f"{label:42} -> gate {'PASS' if g else 'FAIL'}")
    if g!=expect:
        bad+=1; print('MISMATCH', label, g, expect, r); print(LAST_GATE[1], LAST_GATE[2])
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
assert J['ci-required']['if']=='always()'
assert J['ci-required']['needs']==['changes','linux-runner','nightly-gate','build-test','windows-release','macos']
step=J['ci-required']['steps'][0]
assert step['name']=='Check job results' and 'if' not in step
env=step['env']
for job, key in (
    ('changes','CHANGES_SUCCESS'),
    ('linux-runner','LINUX_RUNNER_SUCCESS'),
    ('nightly-gate','NIGHTLY_GATE_SUCCESS'),
    ('build-test','BUILD_TEST_SUCCESS'),
    ('windows-release','WINDOWS_RELEASE_SUCCESS'),
    ('macos','MACOS_SUCCESS'),
):
    assert env[key]=="${{ needs.%s.result == 'success' }}"%job, env[key]
    assert key in step['run']
for key, expr in (
    ('CHANGES',"${{ needs.changes.result }}"),
    ('LINUX_RUNNER',"${{ needs.linux-runner.result }}"),
    ('NIGHTLY_GATE',"${{ needs.nightly-gate.result }}"),
    ('BUILD_TEST',"${{ needs.build-test.result }}"),
    ('WINDOWS_RELEASE',"${{ needs.windows-release.result }}"),
    ('MACOS',"${{ needs.macos.result }}"),
):
    assert env[key]==expr, env[key]
assert 'success|skipped' not in step['run']
assert "expected success" in step['run']
on=wf[True]
for t in ('opened','synchronize','reopened','ready_for_review'):
    assert t in on['pull_request']['types'], t
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
