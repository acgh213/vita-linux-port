#!/bin/sh
set -eu

repo_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
make_cmd=${MAKE_CMD:-make}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/vita-worktree-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
export GIT_ALLOW_PROTOCOL=file

kernel_repo="$tmp/kernel.git"
other_kernel_repo="$tmp/other-kernel.git"
fixture="$tmp/fixture"
post_clone_fixture="$tmp/post-clone-fixture"
worktrees="$tmp/worktrees"
mkdir -p "$kernel_repo" "$other_kernel_repo" "$fixture" \
	"$post_clone_fixture" "$worktrees" "$tmp/home"

git -C "$kernel_repo" init -q
git -C "$kernel_repo" config user.name 'Worktree Test'
git -C "$kernel_repo" config user.email 'worktree-test@example.invalid'
printf 'kernel fixture\n' >"$kernel_repo/README"
git -C "$kernel_repo" add README
git -C "$kernel_repo" commit -q -m 'kernel fixture'
missing_gitlink=$(git -C "$kernel_repo" rev-parse HEAD)

git -C "$other_kernel_repo" init -q
git -C "$other_kernel_repo" config user.name 'Worktree Test'
git -C "$other_kernel_repo" config user.email 'worktree-test@example.invalid'
printf 'different kernel history\n' >"$other_kernel_repo/README"
git -C "$other_kernel_repo" add README
git -C "$other_kernel_repo" commit -q -m 'different kernel fixture'

git -C "$post_clone_fixture" init -q
git -C "$post_clone_fixture" config user.name 'Worktree Test'
git -C "$post_clone_fixture" config user.email 'worktree-test@example.invalid'
git -C "$post_clone_fixture" config protocol.file.allow always
cp "$repo_dir/Makefile" "$post_clone_fixture/Makefile"
printf '[submodule "linux_vita"]\n\tpath = linux_vita\n\turl = %s\n' \
	"$other_kernel_repo" >"$post_clone_fixture/.gitmodules"
git -C "$post_clone_fixture" add Makefile .gitmodules
git -C "$post_clone_fixture" update-index --add --cacheinfo \
	"160000,$missing_gitlink,linux_vita"
git -C "$post_clone_fixture" commit -q -m 'fixture with unavailable gitlink'

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

# A cloneable repository that lacks the recorded gitlink fails only after Git
# has populated submodule metadata. Rollback must remove and unregister the
# entire failed integration worktree. The newly created branch is retained so
# a retry can reuse it.
post_clone_dest="$worktrees/post-clone-failure"
set +e
"$make_cmd" -s -C "$post_clone_fixture" worktree \
	NAME=post-clone-failure INIT_SUBMODULES=1 WORKTREE_BASE_DIR="$worktrees" \
	HOME="$tmp/home" >"$tmp/post-clone-failure.out" 2>&1
status=$?
set -e
failed=0
if [ "$status" -eq 0 ]; then
	echo 'worktree target masked post-clone submodule failure' >&2
	failed=1
fi
if ! git -C "$post_clone_fixture" show-ref --verify --quiet \
	"refs/heads/post-clone-failure"; then
	echo 'worktree rollback unexpectedly removed the retained branch' >&2
	failed=1
fi
if [ -e "$post_clone_dest/linux_vita/.git" ]; then
	echo 'worktree rollback left populated submodule metadata after checkout failed' >&2
	failed=1
fi
if [ -e "$post_clone_dest" ]; then
	echo 'worktree rollback left the failed integration worktree on disk' >&2
	failed=1
fi
if git -C "$post_clone_fixture" worktree list --porcelain |
	grep -Fqx "worktree $post_clone_dest"; then
	echo 'worktree rollback left the failed integration worktree registered' >&2
	failed=1
fi
if [ "$failed" -ne 0 ]; then
	cat "$tmp/post-clone-failure.out" >&2
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
