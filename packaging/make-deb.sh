#!/usr/bin/env bash
#
# Build a Debian/Ubuntu package for konqix.
#
# Usage:
#   ./packaging/make-deb.sh            # binary .deb only (default)
#   ./packaging/make-deb.sh --source   # also build the source package
#                                       # (.dsc + .orig.tar.gz + .debian.tar.xz)
#
# Output is placed in $HOME/debbuild, or override with DEB_TOPDIR=/path.
set -euo pipefail

NAME=konqix
PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CHANGELOG="$PROJECT_DIR/debian/changelog"

if [[ ! -f "$CHANGELOG" ]]; then
    echo "debian/changelog not found: $CHANGELOG" >&2
    exit 1
fi

FULL_VERSION=$(dpkg-parsechangelog -l "$CHANGELOG" -S Version)
UPSTREAM_VERSION=${FULL_VERSION%-*}

DEB_TOPDIR=${DEB_TOPDIR:-$HOME/debbuild}
mkdir -p "$DEB_TOPDIR"

WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

STAGE="$WORKDIR/$NAME-$UPSTREAM_VERSION"
mkdir -p "$STAGE"

# Stage only the files that belong in the upstream tarball -- same list as
# make-srpm.sh's Source0, and deliberately without debian/ (that's overlaid
# separately below, per the quilt source format's split of upstream vs.
# packaging).
cp -a "$PROJECT_DIR"/CMakeLists.txt \
      "$PROJECT_DIR"/LICENSE \
      "$PROJECT_DIR"/README.md \
      "$PROJECT_DIR"/AUTHORS \
      "$PROJECT_DIR"/src \
      "$PROJECT_DIR"/resources \
      "$PROJECT_DIR"/packaging \
      "$STAGE/"

# Strip backup directories and build artefacts that might have leaked in.
find "$STAGE" -maxdepth 2 -type d \( -name 'build' -o -name 'backup-*' \) \
     -prune -exec rm -rf {} +

ORIG_TARBALL="$WORKDIR/${NAME}_${UPSTREAM_VERSION}.orig.tar.gz"
tar -C "$WORKDIR" -czf "$ORIG_TARBALL" "$NAME-$UPSTREAM_VERSION"

echo "Orig tarball: $ORIG_TARBALL"

# Overlay the tracked debian/ packaging directory onto the staged upstream
# tree, then build in place.
cp -a "$PROJECT_DIR/debian" "$STAGE/"

BUILD_OPTS=(-us -uc -b)
if [[ "${1:-}" == "--source" ]]; then
    BUILD_OPTS=(-us -uc -S)
fi

(cd "$STAGE" && dpkg-buildpackage "${BUILD_OPTS[@]}")

find "$WORKDIR" -maxdepth 1 -type f -name "${NAME}_${FULL_VERSION}*" \
     -exec cp -a {} "$DEB_TOPDIR/" \;
find "$WORKDIR" -maxdepth 1 -type f -name "${NAME}_${UPSTREAM_VERSION}.orig.tar.gz" \
     -exec cp -a {} "$DEB_TOPDIR/" \;

echo
echo "Artifacts in $DEB_TOPDIR:"
find "$DEB_TOPDIR" -maxdepth 1 -name "${NAME}_${FULL_VERSION}*" -o -maxdepth 1 -name "${NAME}_${UPSTREAM_VERSION}.orig.tar.gz"
