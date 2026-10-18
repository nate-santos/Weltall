# Repository data guard

Enable this checkout's hooks before making commits or pushing source:

```text
git config core.hooksPath .githooks
```

The hooks need Python 3.10 or later. If it is not on PATH, set an absolute path
using `git config weltall.guardPython <python-executable>` or the environment
variable `WELTALL_GUARD_PYTHON`. The environment variable takes precedence.

The pre-commit hook checks the complete staged Git snapshot. It reads the index
and Git blobs, so later edits to a working file cannot conceal the staged content.
The pre-push hook checks every outgoing commit, including intermediate commits
whose unwanted files were removed at the branch tip. A new remote branch is
compared with that remote's existing local tracking refs. GitHub Actions repeats
the source and introduced-history checks with synthetic tests only.

The guard rejects disc images, descriptors, saves, captures, generated folders,
archives, packages, compiled binaries, common extracted PlayStation formats,
files larger than 8 MiB, and recognizable game data under renamed extensions.
It also rejects external links and Git LFS pointers. Large objects are rejected
from Git metadata before content is requested. It never opens the external
`../bin` disc folder or unstaged local files.

Keep local discs outside the checkout. Keep APKs, staged disc copies, saves and
test artifacts inside ignored `out/` folders. Authored test source and synthetic
fixtures created by tests remain allowed. Original or licensed source assets
still need a provenance check; filename and header rules cannot determine
copyright ownership or identify every possible extracted or encoded asset.

Manual checks:

```text
python Scripts/repo_guard.py --staged
python Scripts/repo_guard.py --tracked
python Scripts/repo_guard.py --commit-range <base>..<tip>
python -m unittest discover -s Scripts/tests -p test_repo_guard.py -v
```
