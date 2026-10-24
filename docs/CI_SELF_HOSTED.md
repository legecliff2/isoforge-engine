# CI is hosted-only

Linux GCC Debug, Linux Clang Release, and the nightly gate run on `ubuntu-latest`
for every event, including a push to `main`, `schedule`, and `workflow_dispatch`.
Windows stays on `windows-latest`. macOS stays on `macos-latest`. No workflow job
requests a self-hosted runner.

`CI_BURST` does not choose a machine. `'true'` sets Linux test parallelism to 8.
`'false'` (the one-line flip in #299) sets it to 4. Compile parallelism stays 4
either way. Both values stay on `ubuntu-latest`.

## Before the repository is public

Unregister the box. From the machine that is registered today:

```sh
sudo RUNNER_TOKEN=<removal token> tools/runner/uninstall.sh
```

The removal token is on the repository **Settings → Actions → Runners** page for
that machine. Pass it in the environment, not as an argument. Without
`RUNNER_TOKEN`, the script stops the service and deletes the local install, and
prints that the runner still has to be removed on that settings page. Do this
right before the repo goes public, so a fork pull request cannot be scheduled
onto it.

`tools/runner/setup.sh` and `tools/runner/uninstall.sh` stay in the repo. CI does
not call them. Do not register the runner again for this public repository.

## Once the repository is public

**Settings → Actions → General → Approval for running fork pull request workflows
from contributors:** select **Require approval for all external contributors**.

That is the policy that covers every outside collaborator. The two first-time
contributor choices stop applying after any commit or pull request of theirs is
merged. Approval limits who can start a workflow. It is not a substitute for
having the box unregistered: an approved fork workflow runs the YAML from the
pull request, and a repository-level runner would accept whatever `runs-on` that
file asks for.

Also under **Workflow permissions**, keep read access for the `contents` and
`packages` permissions only, and leave **Allow GitHub Actions to create and
approve pull requests** unchecked.
