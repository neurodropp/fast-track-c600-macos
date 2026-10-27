#!/bin/sh
# Stage from the user's workspace before elevation; macOS may protect Documents.
set -eu
if [ "$(id -u)" -eq 0 ]; then
    echo "Run this script without sudo; it requests elevation after staging" >&2
    exit 1
fi
project_dir="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
source_bundle="$project_dir/build/cmake/FastTrackC600.driver"
if [ ! -f "$source_bundle/Contents/MacOS/FastTrackC600" ]; then
    echo "Build the driver first with cmake" >&2
    exit 1
fi
/usr/bin/codesign --verify --deep --strict "$source_bundle"
stage_dir="$(/usr/bin/mktemp -d /private/tmp/fasttrackc600-install.XXXXXX)"
trap '/bin/rm -rf "$stage_dir"' EXIT HUP INT TERM
/bin/cp -R "$source_bundle" "$stage_dir/FastTrackC600.driver"
/bin/cp "$project_dir/scripts/install-root.sh" "$stage_dir/install-root.sh"
/bin/chmod -R a+rX "$stage_dir"
/usr/bin/sudo /bin/sh "$stage_dir/install-root.sh" "$stage_dir/FastTrackC600.driver"
