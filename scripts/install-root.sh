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
/bin/mkdir -p "$target_dir"
backup_bundle="$target_dir/.FastTrackC600.driver.backup.$$"
restore_on_failure() {
    status=$?
    trap - EXIT
    if [ "$status" -ne 0 ]; then
        /bin/rm -rf "$target_bundle"
        if [ -e "$backup_bundle" ]; then /bin/mv "$backup_bundle" "$target_bundle"; fi
    fi
    exit "$status"
}
trap restore_on_failure EXIT
if [ -e "$target_bundle" ]; then /bin/mv "$target_bundle" "$backup_bundle"; fi
/bin/cp -R "$source_bundle" "$target_bundle"
/usr/bin/codesign --verify --deep --strict "$target_bundle"
/usr/sbin/chown -R root:wheel "$target_bundle"
/bin/chmod -R a+rX "$target_bundle"
/bin/rm -rf "$backup_bundle"
trap - EXIT
/usr/bin/killall coreaudiod 2>/dev/null || true
echo "Installed $target_bundle; Core Audio restarted"
