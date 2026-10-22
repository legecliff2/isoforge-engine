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
    e=e.replace('github.event_name',repr(ctx['event']))
    return eval(e)
def run(event,labels=(),code=True,mac=False,fail=None):
    # changes job: outputs only set by steps whose if matches (code: PR/push; mac: PR)
    out={'code':'true' if code and event in('pull_request','push') else '', 'mac':'true' if mac and event=='pull_request' else ''}
    ctx=dict(event=event,labels=set(labels),out=out,res={'changes':'success'})
    for j in ['build-test','windows-release','macos']:
        ctx['res'][j]=('failure' if fail==j else 'success') if ev(J[j]['if'],ctx) else 'skipped'
    gate=all(ctx['res'][n] in('success','skipped') for n in J['ci-required']['needs'])
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
print('ALL EXPECTATIONS MET' if not bad else f'{bad} FAILED')
raise SystemExit(1 if bad else 0)
