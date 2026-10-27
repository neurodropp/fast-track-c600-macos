#!/bin/sh
# Stage the privileged removal script outside protected user folders.
set -eu
if [ "$(id -u)" -eq 0 ]; then
    echo "Run this script without sudo; it requests elevation after staging" >&2
    exit 1
fi
project_dir="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
stage_dir="$(/usr/bin/mktemp -d /private/tmp/fasttrackc600-uninstall.XXXXXX)"
trap '/bin/rm -rf "$stage_dir"' EXIT HUP INT TERM
/bin/cp "$project_dir/scripts/uninstall-root.sh" "$stage_dir/uninstall-root.sh"
/bin/chmod a+r "$stage_dir/uninstall-root.sh"
/usr/bin/sudo /bin/sh "$stage_dir/uninstall-root.sh"
