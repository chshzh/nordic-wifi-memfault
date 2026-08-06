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

	for patch in "$module_dir"*.patch; do
		[ -f "$patch" ] || continue
		patch_name="$(basename "$patch")"

		if git -C "$target" apply --check "$patch" 2>/dev/null; then
			echo "Applying $patch_name to $module_path"
			git -C "$target" apply "$patch"
		elif git -C "$target" apply --check --reverse "$patch" 2>/dev/null; then
			echo "Skipping $patch_name (already applied to $module_path)"
		else
			echo "ERROR: $patch_name does not apply cleanly to $module_path." \
				"The module revision may have moved and the patch needs rebasing" \
				"(or dropping, if upstream fixed it) — see patches/$module_name/." >&2
			exit 1
		fi
	done
done
