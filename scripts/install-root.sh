#!/bin/sh
# Internal privileged step; invoked from the staged installer.
set -eu
if [ "$(id -u)" -ne 0 ] || [ "$#" -ne 1 ]; then
    echo "Invalid installer invocation" >&2
    exit 1
fi
source_bundle="$1"
target_dir="/Library/Audio/Plug-Ins/HAL"
target_bundle="$target_dir/FastTrackC600.driver"
/usr/bin/codesign --verify --deep --strict "$source_bundle"
if [ -e "$target_bundle" ]; then
    echo "Driver already installed; uninstall before reinstalling" >&2
    exit 1
fi
/bin/mkdir -p "$target_dir"
/bin/cp -R "$source_bundle" "$target_bundle"
/usr/sbin/chown -R root:wheel "$target_bundle"
/bin/chmod -R a+rX "$target_bundle"
/usr/bin/killall coreaudiod 2>/dev/null || true
echo "Installed $target_bundle; Core Audio restarted"
