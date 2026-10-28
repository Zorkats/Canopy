#!/usr/bin/env bash
#
# Assemble the Linux packages from a finished build tree and a published Canopy Studio.
#
#   .github/scripts/package_linux.sh <build-dir> <studio-dir> <version> [output-dir]
#
# Writes two tarballs:
#   CanopyStudio-<version>-linux-x64.tar.gz   Canopy Studio, self-contained, with
#                                             libcanopy_core.so beside it
#   Canopy-Tools-<version>-linux-x64.tar.gz   the canopy command line tool and
#                                             libcanopy_core.so, for mod authors
# The game and the Canopy loader stay Windows and run through Proton, so Linux players use the
# same Canopy-<version>.zip as everyone else. Nothing ships until check_linux_abi.py agrees the
# binaries of ours start on the distributions the release targets. Adapted from G-Diffuser's
# tools/package_linux.sh.

set -euo pipefail

if [ $# -lt 3 ] || [ $# -gt 4 ]; then
    echo "usage: $0 <build-dir> <studio-dir> <version> [output-dir]" >&2
    exit 2
fi

build_dir=$1
studio_dir=$2
version=$3
out_dir=${4:-dist}
repo_root=$(cd "$(dirname "$0")/../.." && pwd)
tools_dir="$build_dir/src/CanopyTools"

for item in "$tools_dir/canopy" "$tools_dir/libcanopy_core.so"; do
    if [ ! -f "$item" ]; then
        echo "error: build tree is missing $item" >&2
        echo "       build first: cmake --build $build_dir" >&2
        exit 1
    fi
done
# Canopy Studio cannot start without these; the rest of the folder is the .NET runtime and
# Avalonia's native libraries, which the publish step owns.
for item in CanopyStudio CanopyStudio.dll libcanopy_core.so; do
    if [ ! -f "$studio_dir/$item" ]; then
        echo "error: the published Canopy Studio is missing $item" >&2
        exit 1
    fi
done
if ! cmp -s "$tools_dir/libcanopy_core.so" "$studio_dir/libcanopy_core.so"; then
    echo "error: the published Canopy Studio carries a libcanopy_core.so from another build" >&2
    exit 1
fi

python3 "$repo_root/.github/scripts/check_linux_abi.py" \
    "$tools_dir/canopy" "$tools_dir/libcanopy_core.so"

mkdir -p "$out_dir"
stage_root="$out_dir/stage-linux"
rm -rf "$stage_root"

studio="$stage_root/studio/CanopyStudio"
mkdir -p "$studio"
cp -r "$studio_dir/." "$studio/"
install -m 644 "$repo_root/LICENSE" "$repo_root/THIRD_PARTY_NOTICES.txt" "$studio/"
chmod +x "$studio/CanopyStudio"

tools="$stage_root/tools/Canopy-Tools"
mkdir -p "$tools"
cp "$tools_dir/canopy" "$tools_dir/libcanopy_core.so" "$tools/"
install -m 644 "$repo_root/LICENSE" "$repo_root/THIRD_PARTY_NOTICES.txt" "$tools/"
chmod +x "$tools/canopy"

# Fixed ownership, order and times so the same tree always produces the same tarball.
pack() {
    tar --sort=name --owner=0 --group=0 --numeric-owner \
        --mtime="@${SOURCE_DATE_EPOCH:-0}" \
        -czf "$1" -C "$2" "$3"
}
studio_tar="$out_dir/CanopyStudio-$version-linux-x64.tar.gz"
tools_tar="$out_dir/Canopy-Tools-$version-linux-x64.tar.gz"
pack "$studio_tar" "$stage_root/studio" CanopyStudio
pack "$tools_tar" "$stage_root/tools" Canopy-Tools
rm -rf "$stage_root"

echo
sha256sum "$studio_tar" "$tools_tar"
