#!/usr/bin/env python3
"""Truth-table check of the job if: conditions and the ci-required gate in .github/workflows/ci.yml.
Evaluates the real if: strings for each event / label / paths-filter combination (no Actions needed).
Run: python3 tools/ci_conditions_test.py   (needs PyYAML). Exits non-zero if an expectation fails."""
import yaml,re,itertools
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
assert J['nightly-gate']['needs']==['linux-runner']
assert 'ubuntu-latest' in str(J['linux-runner']['runs-on'])
can=yaml.safe_load(open(__import__('pathlib').Path(__file__).resolve().parents[1]/'.github/workflows/linux-runner-canary.yml'))
assert set(can[True])=={'workflow_dispatch'}
assert can['permissions']=={}
assert can['jobs']['ping']['runs-on']==['self-hosted','linux','isoforge']
assert all('uses' not in s for s in can['jobs']['ping']['steps'])
assert can['concurrency']['cancel-in-progress'] is True
assert set(wf[True]['pull_request']['types'])=={'opened','synchronize','reopened','ready_for_review'}
print('ALL EXPECTATIONS MET' if not bad else f'{bad} FAILED')
raise SystemExit(1 if bad else 0)
