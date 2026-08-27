#!/usr/bin/env bash
#
# Applies local patches under patches/<module>/*.patch to west-managed module
# checkouts. `west update` always resets modules to their manifest-pinned
# revision, so these patches (fixes for upstream bugs not yet picked up by
# the NCS-pinned module version) must be re-applied after every
# `west init`/`west update` — this script is meant to be run right after
# that step, both locally and in CI.
#
# Usage: script/apply_patches.sh <west-topdir>

set -euo pipefail

WEST_TOPDIR="${1:?Usage: $0 <west-topdir>}"
PATCH_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/patches"

# Maps a patches/<module_name>/ subdirectory to the module's checkout path,
# relative to WEST_TOPDIR. Add an entry here whenever a patch is added for a
# new module. (Avoids `declare -A` for portability with macOS's bundled
# bash 3.2, which has no associative arrays.)
module_checkout_path() {
	case "$1" in
	memfault-firmware-sdk) echo "modules/lib/memfault-firmware-sdk" ;;
	hostap) echo "modules/lib/hostap" ;;
	zephyr) echo "zephyr" ;;
	*) echo "" ;;
	esac
}

if [ ! -d "$PATCH_ROOT" ]; then
	echo "No patches/ directory found, nothing to apply"
	exit 0
fi

for module_dir in "$PATCH_ROOT"/*/; do
	[ -d "$module_dir" ] || continue
	module_name="$(basename "$module_dir")"
	module_path="$(module_checkout_path "$module_name")"

	if [ -z "$module_path" ]; then
		echo "ERROR: no checkout path registered in module_checkout_path() for patch module '$module_name'" >&2
		exit 1
	fi

	target="${WEST_TOPDIR}/${module_path}"
	if [ ! -d "$target" ]; then
		echo "ERROR: module checkout not found at $target (expected for patches/$module_name)" >&2
		exit 1
	fi

	# Reset to the manifest-pinned revision before applying. A cached west
	# workspace (see .github/workflows/*.yml) may already have these
	# patches applied from a previous run's working tree, and `west
	# update` skips modules already at the pinned revision, so it won't
	# clean this up for us. `manifest-rev` is the ref west itself maintains
	# to track that pinned commit, so resetting to it (rather than just
	# discarding working-tree edits) also recovers from a HEAD that has
	# drifted, e.g. from an earlier local `git commit` while drafting a
	# patch. Starting from this known-clean state keeps patch application
	# deterministic and avoids false conflicts when two patches touch
	# adjacent lines in the same file.
	git -C "$target" reset --hard manifest-rev

	for patch in "$module_dir"*.patch; do
		[ -f "$patch" ] || continue
		patch_name="$(basename "$patch")"

		if git -C "$target" apply --check "$patch" 2>/dev/null; then
			echo "Applying $patch_name to $module_path"
			git -C "$target" apply "$patch"
		else
			echo "ERROR: $patch_name does not apply cleanly to $module_path." \
				"The module revision may have moved and the patch needs rebasing" \
				"(or dropping, if upstream fixed it) — see patches/$module_name/." >&2
			exit 1
		fi
	done
done
