# Self-hosted runner, once this repo is public

The box is the one machine registered with labels `self-hosted`, `linux`, and `isoforge`.
Untrusted code must never run on it. This page is the setting list. The workflow file is
not the control that makes the list true.

`legecliff2` is a personal account. Personal accounts have repository runners only.
They do not have **Settings → Actions → Runner groups**, and a repository runner cannot
be limited to a workflow ref. The steps below start by moving the runner to an
organization, which is where those settings exist. GitHub's own docs say to prefer not
using a self-hosted runner with a public repository, because a fork can submit a
workflow that runs on it. The steps are how to keep the box anyway.

## What the workflow does

In `.github/workflows/ci.yml`, the box is eligible only when both are true:

- `github.ref == 'refs/heads/main'`
- `github.event_name` is `push`, `schedule`, or `workflow_dispatch`

Every other event, including `pull_request` and `pull_request_target`, forces
`ubuntu-latest` for Linux GCC Debug, Linux Clang Release, the nightly gate, and any
other Linux row. The `runs-on` expression does that itself. It does not trust the
`linux-runner` output. Windows stays on `windows-latest`. macOS stays on `macos-latest`.

`CI_BURST` does not put a pull request on the box.

- `CI_BURST=true` (the current line, until the revert in #299): pull requests are
  hosted and do not dispatch the canary. A push to `main` may still place Linux GCC
  Debug on the box when the canary accepts it. Clang Release and the nightly gate stay
  hosted. Hosted test parallelism is 8.
- `CI_BURST=false`: a push to `main`, `schedule`, and `workflow_dispatch` on `main`
  prefer the box and fall back to `ubuntu-latest` when the canary is not accepted
  (#281). Pull requests stay hosted (test parallelism 4) and do not dispatch the canary.

`.github/workflows/linux-runner-canary.yml` is `workflow_dispatch` only. Its job runs
only when `github.ref == 'refs/heads/main'`, has no `permissions`, and does not check
out the repo. `ci.yml` dispatches it with `ref` = the default branch, and only from a
trusted event.

## What the workflow cannot do

For `pull_request`, GitHub loads the workflow from the merge commit
(`refs/pull/<n>/merge`), which includes the PR head. A fork that edits `ci.yml`, or
adds another file under `.github/workflows/`, runs **that** file, not the copy on
`main`. Any `if:` or `runs-on` guard in this repository's copy is gone for that run.
Same-repo pull requests that edit the workflow have the same property: the edited file
is what runs.

Approving the run does not restore the `main` workflow. Approval is what lets the
fork's file execute.

`pull_request_target` is the opposite case, and it is worse if misused. That event
runs the workflow file from the **default branch**, with the default branch's token
and secrets, and `github.workflow_ref` is `…/ci.yml@refs/heads/main`. A runner-group
pin to `refs/heads/main` **allows** it. Checking out the PR head
(`github.event.pull_request.head.sha`, or `refs/pull/<n>/head`) onto the box would run
untrusted code on a trusted workflow ref. This repo has no `pull_request_target`
workflow. Do not add one that checks out PR code, on the box or otherwise.

There is no workflow key that changes the paragraph above. The settings below are the
control.

## Settings

Do these before the repository is public. Names below are the ones on the GitHub
settings pages as of the docs checked for this file
([Managing access to self-hosted runners using groups](https://docs.github.com/en/actions/how-tos/manage-runners/self-hosted-runners/manage-access),
[Managing GitHub Actions settings for a repository](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/enabling-features-for-your-repository/managing-github-actions-settings-for-a-repository),
[Disabling or limiting GitHub Actions for your organization](https://docs.github.com/en/organizations/managing-organization-settings/disabling-or-limiting-github-actions-for-your-organization)).
If a label is missing, stop and re-read the page rather than clicking a nearby one.

A wrong runner group does not fail closed inside the trusted workflow. `linux-runner`
cancels the canary when the box does not accept it and then uses `ubuntu-latest`. A
workflow that asks for the box with no fallback (a fork's edited file) sits on
**Waiting for a runner to pick up this job...** until the job times out. GitHub does
not say "blocked by workflow access". An idle box plus that message means the pin is
working, not that the machine is down.

### 1. Organization

Runner groups exist on organizations, not on the user `legecliff2`. Every organization
has a **Default** runner group. The docs page still says extra groups are a GitHub Team
feature. The [2024-10-17 changelog](https://github.blog/changelog/2024-10-17-actions-runner-groups-now-available-for-organizations-on-free-plan/)
says organizations on every plan, including Free, can use runner groups. If **New
runner group** is not offered, configure **Default** instead of buying a plan.

Create or pick an organization, then transfer the repository:
**Settings → General → Danger Zone → Transfer ownership**. The workflow refs below use
the org that owns the repo after the transfer (`ORG/isoforge-engine`).

### 2. Runner group

**Organization Settings → Actions → Runner groups → New runner group** (or open
**Default**).

- **Repository access:** **Selected repositories**. Select only `isoforge-engine`.
  **Save group**.
- **Allow public repositories:** check this once the repo is public, or jobs from the
  public repo (including `push` to `main`) never get a runner. The default is private
  repositories only. Checking it does not let a fork's workflow ref through. The pin in
  the next bullet is what does that. GitHub warns here that public repos plus
  self-hosted runners are unsafe; that warning is why the pin has to be exact.
- **Workflow access:** **Selected workflows**. Enter these two refs, comma-separated.
  Use `refs/heads/main`, not `main`.

  ```text
  ORG/isoforge-engine/.github/workflows/ci.yml@refs/heads/main
  ORG/isoforge-engine/.github/workflows/linux-runner-canary.yml@refs/heads/main
  ```

  Only jobs defined directly in those files are allowed. A reusable workflow called
  from them is not, unless its own ref is listed too. We do not call one.

Why a fork that edits `ci.yml` still cannot land on the box: that run's workflow ref
is `ORG/isoforge-engine/.github/workflows/ci.yml@refs/pull/<n>/merge`, or a new path
that is not on the list at all. Neither matches `@refs/heads/main`. A pull request
that does not edit the workflow has the same merge ref, so it would not match either.
That is why `ci.yml` sends pull requests to `ubuntu-latest` instead of asking the box
and waiting forever.

`pull_request_target` **does** match `@refs/heads/main`, because that event's workflow
file is the one on the default branch. The pin does not block it. The rule is still:
do not add `pull_request_target`, and never check out PR head code on this runner.

The default branch has to stay `main`. `schedule` runs with `github.ref` set to the
default branch. If that branch is renamed, the pin and the `runs-on` check both stop
matching and the nightly gate stays on `ubuntu-latest` (the picker will not ask for
the box when the ref is not `refs/heads/main`).

### 3. Register the runner on the organization, then remove the repository runner

A repository-level runner with the same labels ignores the group. While it is online,
a fork's `runs-on: [self-hosted, linux, isoforge]` is accepted.

The machine today is registered to `https://github.com/legecliff2/isoforge-engine`
(`tools/runner/setup.sh`). `setup.sh` will not re-register while
`/opt/actions-runner/.runner` exists.

1. Create the group in step 2 first. `config.sh --runnergroup` fails if the name does
   not exist.
2. Remove the repository registration:
   `sudo RUNNER_TOKEN=<removal token> tools/runner/uninstall.sh`.
   The removal token is on the repository **Settings → Actions → Runners** page for
   that machine. Pass it in the environment, not as an argument.
3. On the organization: **Settings → Actions → Runners → New self-hosted runner**, and
   use the registration token shown there (it is not the repository token).
4. Register into the group:

   ```sh
   sudo RUNNER_TOKEN=<org registration token> \
     REPO_URL=https://github.com/ORG \
     RUNNER_GROUP=<group name> \
     tools/runner/setup.sh
   ```

   `RUNNER_GROUP` is optional. If you omit it, the runner joins **Default**, and
   **Default** is the group whose **Workflow access** you must pin. To move it later:
   **Organization Settings → Actions → Runners →** the runner **→ Runner group →
   Move runner to group**.
5. Only after that runner is online, turn off repository-level runners so nobody can
   add another one that skips the group. **Organization Settings → Actions → General →
   Runners → Disabled** ("self-hosted runners cannot be created at the repository
   level"). Runners already registered to a repository show status **Disabled** and are
   not given new jobs. Organization runners in the group still work. Do not choose
   **All repositories**.

If the group rejects the canary, `linux-runner` waits out the probe window (~75s, or
~20s while `CI_BURST` is `true`), cancels the canary, and uses `ubuntu-latest`. CI
stays green. The box simply does not take the job until the pin lists both workflow
refs.

### 4. Workflow permissions

On the repository: **Settings → Actions → General → Workflow permissions**.

- Select read access for the `contents` and `packages` permissions only (the
  restricted setting). Not read and write access for all permissions.
- Leave **Allow GitHub Actions to create and approve pull requests** unchecked.

New repositories on a personal account already default to read-only `contents` and
`packages`, and workflows cannot create or approve pull requests. Check the page
after the transfer: an organization can force a more permissive default, and a more
restrictive organization default cannot be loosened here.

`ci.yml` still grants `actions: write` on the `linux-runner` job only, so a trusted
run can dispatch the canary. A fork's `pull_request` cannot use that grant. On a
public repository the fork token stays read-only; a `permissions:` block in the
fork's file cannot raise it. That is a platform rule, not a checkbox. It stops a
fork from dispatching the canary. It does **not** stop the fork from setting
`runs-on` to the box labels. The runner group does.

### 5. Approval for fork pull request workflows

On the repository: **Settings → Actions → General → Approval for running fork pull
request workflows from contributors**.

Select **Require approval for all external contributors**.

That is the setting that covers every outside collaborator, not only first-time
ones. The other two choices are **Require approval for first-time contributors who
are new to GitHub** and **Require approval for first-time contributors**. GitHub's
own warning on the page: with either first-time choice, a user who has had any
commit or pull request merged no longer needs approval, including a typo that was
merged on purpose. Do not use those two while this runner exists.

Approval is not sufficient on its own. The page says the policy is aimed at hosted
minute abuse, and that once a run is allowed, self-hosted runners will execute the
workflow that was submitted. The runner-group pin has to be in place before the
first fork run is approved.

`pull_request_target` ignores this approval setting. It runs the default-branch
workflow whether or not the fork is approved. Another reason not to add that trigger.

### 6. While the repository is still private

**Settings → Actions → General → Fork pull request workflows** exists only on private
repositories. Before the repo is public:

- Leave **Send write tokens to workflows from pull requests** unchecked.
- Leave **Send secrets to workflows from pull requests** unchecked.
- Keep **Require approval for fork pull request workflows** checked.

Those three checkboxes disappear on a public repository. Public fork `pull_request`
workflows cannot be given secrets or a write `GITHUB_TOKEN`.

### 7. After the repository is public

- Confirm **Allow public repositories** is checked on the runner group (step 2).
  Without it, `main` queues and the canary probe falls back to `ubuntu-latest`.
- Confirm the repository **Runners** list is empty (or every entry is **Disabled**).
- Confirm **Workflow access** is still the two `@refs/heads/main` refs and not
  **All workflows**.
- Confirm the approval policy is still **Require approval for all external
  contributors**.

No other workflow in this repo may set `runs-on` to `self-hosted` or `isoforge`.
The canary is the only job that hard-codes those labels, and only on `main`.
