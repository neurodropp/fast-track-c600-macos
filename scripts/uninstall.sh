#!/bin/sh
# Remove only this project's HAL driver. Run as administrator.
set -eu
if [ "$(id -u)" -ne 0 ]; then
    echo "Run with administrator privileges" >&2
    exit 1
fi
target_bundle="/Library/Audio/Plug-Ins/HAL/FastTrackC600.driver"
if [ -e "$target_bundle" ]; then
    /bin/rm -rf "$target_bundle"
fi
/usr/bin/killall coreaudiod 2>/dev/null || true
echo "Removed $target_bundle; Core Audio restarted"
