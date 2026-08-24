#! /usr/bin/bash
app=$1
packDir=$(realpath $2)
vers=$3

if [[ -z "$app" || -z "$packDir" ]]; then
   echo "Must set application name and packaging dir! Examples:"
   echo "build/package.sh package dir"
   echo "build/package.sh package dir version"
   exit 1
fi
if [[ -z "$vers" ]]; then
   vers=$(git describe --tags --abbrev=0 trunk)
fi

#0. Generate destination dir
original="$(pwd)"
dest="$packDir/$app-$vers"
/usr/bin/rm -rf "$dest"
/usr/bin/mkdir -p "$dest"

#1. Generate the source tarball at that specific version
theTarball=$dest/tarball.tar
git -c core.abbrev=no -C "$pwd" archive --format tar "$vers" > $theTarball
checksum=$(sha256sum $theTarball | awk '{print $1}')

#2. We use a PKGBUILD with the local source to build the package, and generate a PKGBUILD
#with the original source to have something to upload to AUR etc.

#AWK programs to replace checksum etc in the PKGBUILD
read -r -d '' localSourceSubst <<EOF
{ 
gsub(/source=\([^)]+\)/, "source=(tarball.tar)"); 
gsub(/pkgver=_/, "pkgver=$vers"); 
gsub(/sha256sums=\([^)]+\)/, "sha256sums=('$checksum')"); 
}1
EOF

read -r -d '' globalSourceSubst <<EOF
{ 
gsub(/pkgver=_/, "pkgver=$vers"); 
gsub(/sha256sums=\([^)]+\)/, "sha256sums=('$checksum')"); 
}1
EOF

awk -v RS='^$' -v ORS='' "$localSourceSubst" build/PKGBUILD > $dest/PKGBUILD

#3. Actually build the package 
(cd $dest \
   && makepkg \
   && /usr/bin/rm tarball.tar \
   && awk -v RS='^$' -v ORS='' "$globalSourceSubst" "$original/build/PKGBUILD" > $dest/PKGBUILD \
   && echo "Version $vers Arch package built in $dest" \
   || echo "ERROR")
