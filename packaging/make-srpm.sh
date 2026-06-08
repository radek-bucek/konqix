#!/usr/bin/env bash
#
# Build a Source RPM for konqix.
#
# Usage:
#   ./packaging/make-srpm.sh            # SRPM only (default)
#   ./packaging/make-srpm.sh --rpm      # also build the binary RPM via rpmbuild --rebuild
#
# Output is placed in $HOME/rpmbuild/SRPMS (and RPMS if --rpm is used),
# or override with RPM_TOPDIR=/path/to/rpmbuild.
set -euo pipefail

NAME=konqix
PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SPEC="$PROJECT_DIR/packaging/$NAME.spec"

if [[ ! -f "$SPEC" ]]; then
    echo "Spec file not found: $SPEC" >&2
    exit 1
fi

VERSION=$(awk '/^Version:/ {print $2; exit}' "$SPEC")
if [[ -z "$VERSION" ]]; then
    echo "Could not extract Version from spec" >&2
    exit 1
fi

RPM_TOPDIR=${RPM_TOPDIR:-$HOME/rpmbuild}
mkdir -p "$RPM_TOPDIR"/{SOURCES,SPECS,SRPMS,RPMS,BUILD,BUILDROOT}

WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

STAGE="$WORKDIR/$NAME-$VERSION"
mkdir -p "$STAGE"

# Stage only the files that belong in the source tarball.
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

TARBALL="$RPM_TOPDIR/SOURCES/$NAME-$VERSION.tar.gz"
tar -C "$WORKDIR" -czf "$TARBALL" "$NAME-$VERSION"
cp "$SPEC" "$RPM_TOPDIR/SPECS/"

echo "Source tarball: $TARBALL"

rpmbuild --define "_topdir $RPM_TOPDIR" -bs "$RPM_TOPDIR/SPECS/$NAME.spec"

SRPM=$(ls -t "$RPM_TOPDIR"/SRPMS/${NAME}-${VERSION}-*.src.rpm | head -1)
echo
echo "SRPM:  $SRPM"

if [[ "${1:-}" == "--rpm" ]]; then
    echo
    echo "Rebuilding binary RPM from SRPM..."
    rpmbuild --define "_topdir $RPM_TOPDIR" --rebuild "$SRPM"
    echo
    echo "Binary RPMs:"
    find "$RPM_TOPDIR/RPMS" -name "${NAME}-${VERSION}-*.rpm" -print
fi
