#!/usr/bin/env bash
# Usage:
#   ./release.sh 0.17.6              full release
#   DRY_RUN=1 ./release.sh 0.17.6    build tarball, list contents, stop
set -euo pipefail

# -----------------------------------------
NAME="storm-knell"
GH_REPO="vsix8625/storm-knell"
PKGDIR="storm-knell-pkg/arch" 
# -----------------------------------------

ver="${1:?usage: $0 <version>   e.g. $0 0.17.6}"
tag="v${ver}"
root="$(git rev-parse --show-toplevel)"
tarball="${NAME}-${ver}.tar.gz"
out="$root/$PKGDIR"

cd "$root"

if [[ -n "$(git status --porcelain)" ]]; then
    if [[ -n "${DRY_RUN:-}" ]]; then
        echo "Warning: working tree not clean (dry run, continuing)." >&2
    else
        echo "Working tree not clean. Commit first." >&2
        exit 1
    fi
fi

git submodule update --init --recursive

git ls-files -z --recurse-submodules \
    | grep -zvE \
        -e '^(storm-knell-pkg|docs|utils/nvim|utils/emacs|\.github)/' \
        -e '^(\.gitignore|\.gitmodules|README\.md)$' \
        -e '^scripts/release\.sh$' \
        -e '^external/vx/compile_commands\.json$' \
    | tar --null -T - \
          --transform="s,^,${NAME}-${ver}/," \
          -czf "$out/$tarball"

if [[ -n "${DRY_RUN:-}" ]]; then
    tar tzf "$out/$tarball"
    echo "Dry run: tarball is $out/$tarball, nothing tagged or uploaded."
    exit 0
fi

if ! git rev-parse "$tag" >/dev/null 2>&1; then
    git tag "$tag"
fi
git push origin "$tag"


gh release create "$tag" "$out/$tarball" --repo "$GH_REPO" --title "$tag" --notes "Release $ver"

sum="$(sha256sum "$out/$tarball" | cut -d' ' -f1)"
cd "$out"
sed -i "s/^pkgver=.*/pkgver=${ver}/" PKGBUILD
sed -i "s/^pkgrel=.*/pkgrel=1/" PKGBUILD
sed -i "s/^sha256sums=.*/sha256sums=('${sum}')/" PKGBUILD

# remove local tarball so makepkg downloads it 
rm -f "$tarball"

rm -f ./*.pkg.tar.zst "${NAME}.db"* "${NAME}.files"*
makepkg -f
pkg="$(ls ${NAME}-${ver}-1-*.pkg.tar.zst)"

repo-add "${NAME}.db.tar.gz" "$pkg"
cp --remove-destination "${NAME}.db.tar.gz" "${NAME}.db"

gh release upload "$tag" "$pkg" "${NAME}.db" --repo "$GH_REPO" --clobber

echo "Done: $tag published with $pkg and ${NAME}.db"
