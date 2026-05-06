#!/usr/bin/env bash
# Install all qrenderdoc extensions in this directory into the user's
# qrenderdoc extensions folder by copying each one.
#
#   <repo>/extensions/<name>  ==>  ~/.local/share/qrenderdoc/extensions/<name>
#
# Copying (rather than symlinking) makes the installed extension independent
# of this repo's location: you can move, rename, or delete the repo
# afterwards and the installed extension keeps working.
#
# Re-running is safe: each existing target (file, directory, or symlink)
# is removed and replaced with a fresh copy. After running, open qrenderdoc
# -> Tools -> Manage Extensions and tick "Loaded" for each new extension.

set -euo pipefail

src_dir="$(cd "$(dirname "$0")" && pwd)"
dst_dir="${XDG_DATA_HOME:-$HOME/.local/share}/qrenderdoc/extensions"

mkdir -p "$dst_dir"

shopt -s nullglob
installed=0
for ext in "$src_dir"/*/; do
    if [[ ! -f "$ext/extension.json" ]]; then
        continue
    fi
    name="$(basename "$ext")"
    target="$dst_dir/$name"
    rm -rf "$target"
    mkdir -p "$target"
    cp -r "$ext"/. "$target"/
    rm -rf "$target/__pycache__"
    echo "installed: $target  (from $ext)"
    installed=$((installed + 1))
done

if [[ $installed -eq 0 ]]; then
    echo "no extensions found in $src_dir" >&2
    exit 1
fi

echo
echo "Done. Open qrenderdoc -> Tools -> Manage Extensions and tick 'Loaded' for each."
