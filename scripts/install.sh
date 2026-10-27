#!/bin/sh
# Install the development HAL driver. Run as administrator.
set -eu
if [ "$(id -u)" -ne 0 ]; then
    echo "Run with administrator privileges" >&2
    exit 1
fi
source_bundle="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)/build/cmake/FastTrackC600.driver"
target_dir="/Library/Audio/Plug-Ins/HAL"
target_bundle="$target_dir/FastTrackC600.driver"
if [ ! -f "$source_bundle/Contents/MacOS/FastTrackC600" ]; then
    echo "Build the driver first with cmake" >&2
    exit 1
fi
/usr/bin/codesign --verify --deep --strict "$source_bundle"
if [ -e "$target_bundle" ]; then
    echo "Driver already installed; run scripts/uninstall.sh before reinstalling" >&2
    exit 1
fi
/bin/mkdir -p "$target_dir"
/bin/cp -R "$source_bundle" "$target_bundle"
/usr/sbin/chown -R root:wheel "$target_bundle"
/bin/chmod -R a+rX "$target_bundle"
/usr/bin/killall coreaudiod 2>/dev/null || true
echo "Installed $target_bundle; Core Audio restarted"
