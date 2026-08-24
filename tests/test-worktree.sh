#!/bin/sh
set -eu

repo_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
make_cmd=${MAKE_CMD:-make}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/vita-worktree-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
export GIT_ALLOW_PROTOCOL=file

kernel_repo="$tmp/kernel.git"
fixture="$tmp/fixture"
worktrees="$tmp/worktrees"
mkdir -p "$kernel_repo" "$fixture" "$worktrees" "$tmp/home"

git -C "$kernel_repo" init -q
git -C "$kernel_repo" config user.name 'Worktree Test'
git -C "$kernel_repo" config user.email 'worktree-test@example.invalid'
printf 'kernel fixture\n' >"$kernel_repo/README"
git -C "$kernel_repo" add README
git -C "$kernel_repo" commit -q -m 'kernel fixture'

git -C "$fixture" init -q
git -C "$fixture" config user.name 'Worktree Test'
git -C "$fixture" config user.email 'worktree-test@example.invalid'
git -C "$fixture" config protocol.file.allow always
cp "$repo_dir/Makefile" "$fixture/Makefile"
git -C "$fixture" -c protocol.file.allow=always submodule add -q "$kernel_repo" linux_vita
git -C "$fixture" add Makefile .gitmodules linux_vita
git -C "$fixture" commit -q -m 'fixture with local submodule'
printf 'local rootfs convenience artifact\n' >"$fixture/linux_vita/rootfs.cpio.zst"

# INIT_SUBMODULES=0 must not try to initialize the deliberately broken URL.
git -C "$fixture" config -f .gitmodules submodule.linux_vita.url "$tmp/missing-kernel.git"
git -C "$fixture" add .gitmodules
git -C "$fixture" commit -q -m 'break submodule URL'
git -C "$fixture" config submodule.linux_vita.url "$tmp/missing-kernel.git"
if ! "$make_cmd" -s -C "$fixture" worktree \
	NAME=no-init INIT_SUBMODULES=0 WORKTREE_BASE_DIR="$worktrees" \
	HOME="$tmp/home" >"$tmp/no-init.out" 2>&1; then
	cat "$tmp/no-init.out" >&2
	exit 1
fi
test -d "$worktrees/no-init"
test ! -e "$worktrees/no-init/linux_vita/rootfs.cpio.zst"

# A failed initialization must be reported and must not leave a stale path that
# blocks a later clone (especially one populated by the source worktree rootfs).
set +e
"$make_cmd" -s -C "$fixture" worktree \
	NAME=failed-init INIT_SUBMODULES=1 WORKTREE_BASE_DIR="$worktrees" \
	HOME="$tmp/home" >"$tmp/failed-init.out" 2>&1
status=$?
set -e
failed=0
if [ "$status" -eq 0 ]; then
	echo 'worktree target masked failed submodule initialization' >&2
	failed=1
fi
if [ -e "$worktrees/failed-init/linux_vita/rootfs.cpio.zst" ]; then
	echo 'worktree target copied rootfs into an uninitialized submodule path' >&2
	failed=1
fi
if [ -e "$worktrees/failed-init/linux_vita" ]; then
	echo 'worktree target left a stale linux_vita directory after initialization failed' >&2
	failed=1
fi
if [ "$failed" -ne 0 ]; then
	cat "$tmp/failed-init.out" >&2
	exit 1
fi

# With a valid local URL, initialization must check out the exact gitlink. The
# untracked source rootfs is intentionally not a reproducible worktree input.
git -C "$fixture" config -f .gitmodules submodule.linux_vita.url "$kernel_repo"
git -C "$fixture" add .gitmodules
git -C "$fixture" commit -q -m 'restore local submodule URL'
git -C "$fixture" submodule sync -q
expected=$(git -C "$fixture" rev-parse HEAD:linux_vita)
if ! "$make_cmd" -s -C "$fixture" worktree \
	NAME=successful-init INIT_SUBMODULES=1 WORKTREE_BASE_DIR="$worktrees" \
	HOME="$tmp/home" >"$tmp/successful-init.out" 2>&1; then
	cat "$tmp/successful-init.out" >&2
	exit 1
fi
actual=$(git -C "$worktrees/successful-init/linux_vita" rev-parse HEAD)
test "$actual" = "$expected"
test ! -e "$worktrees/successful-init/linux_vita/rootfs.cpio.zst"
grep -q "Run 'make rootfs'" "$tmp/successful-init.out"

printf 'worktree helper tests: PASS\n'
