#!/usr/bin/env bash
#
# Assemble the Linux package from a finished build tree.
#
#   .github/scripts/package_linux.sh <build-dir> <version> [output-dir]
#
# Writes one tarball:
#   Canopy-Tools-<version>-linux-x64.tar.gz   the canopy command line tool and
#                                             libcanopy_core.so, for mod authors
# The game and the Canopy loader stay Windows and run through Proton, so Linux players use the
# same Canopy-<version>.zip as everyone else. Canopy Studio for Linux is packaged in its own
# repository. Nothing ships until check_linux_abi.py agrees the binaries of ours start on the
# distributions the release targets. Adapted from G-Diffuser's tools/package_linux.sh.

set -euo pipefail

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    echo "usage: $0 <build-dir> <version> [output-dir]" >&2
    exit 2
fi

build_dir=$1
version=$2
out_dir=${3:-dist}
repo_root=$(cd "$(dirname "$0")/../.." && pwd)
tools_dir="$build_dir/src/CanopyTools"

for item in "$tools_dir/canopy" "$tools_dir/libcanopy_core.so"; do
    if [ ! -f "$item" ]; then
        echo "error: build tree is missing $item" >&2
        echo "       build first: cmake --build $build_dir" >&2
        exit 1
    fi
done

python3 "$repo_root/.github/scripts/check_linux_abi.py" \
    "$tools_dir/canopy" "$tools_dir/libcanopy_core.so"

mkdir -p "$out_dir"
stage_root="$out_dir/stage-linux"
rm -rf "$stage_root"

tools="$stage_root/tools/Canopy-Tools"
mkdir -p "$tools"
cp "$tools_dir/canopy" "$tools_dir/libcanopy_core.so" "$tools/"
install -m 644 "$repo_root/LICENSE" "$repo_root/THIRD_PARTY_NOTICES.txt" "$tools/"
chmod +x "$tools/canopy"

# Fixed ownership, order and times so the same tree always produces the same tarball.
tools_tar="$out_dir/Canopy-Tools-$version-linux-x64.tar.gz"
tar --sort=name --owner=0 --group=0 --numeric-owner \
    --mtime="@${SOURCE_DATE_EPOCH:-0}" \
    -czf "$tools_tar" -C "$stage_root/tools" Canopy-Tools
rm -rf "$stage_root"

echo
sha256sum "$tools_tar"
