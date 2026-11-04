#!/bin/bash
# Builds dist/BasementalFG250-<version>-macOS-Installer.pkg from the CMake artefacts.
# Usage: installer/mac/build-pkg.sh <artefact-dir> <version>
set -euo pipefail
ART="$1"; VER="$2"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
OUT="dist/BasementalFG250-$VER-macOS-Installer.pkg"
mkdir -p dist

stage () {   # stage <bundle> <subdir>
    mkdir -p "$WORK/$2"
    cp -R "$1" "$WORK/$2/"
    xattr -cr "$WORK/$2" 2>/dev/null || true
    codesign --force --deep --sign - "$WORK/$2/$(basename "$1")"   # ad-hoc: required on Apple Silicon
}
stage "$ART/AU/Basemental FG 250.component" au
stage "$ART/VST3/Basemental FG 250.vst3"   vst3

for kind in au vst3; do
    # never relocate: always install into the standard plug-in folders
    pkgbuild --analyze --root "$WORK/$kind" "$WORK/$kind.plist"
    plutil -replace BundleIsRelocatable -bool NO "$WORK/$kind.plist"
done
chmod +x "$HERE/scripts/postinstall"
pkgbuild --root "$WORK/au" --component-plist "$WORK/au.plist" --identifier com.basemental.fg250.au \
         --version "$VER" --install-location "/Library/Audio/Plug-Ins/Components" \
         --scripts "$HERE/scripts" "$WORK/au.pkg"
pkgbuild --root "$WORK/vst3" --component-plist "$WORK/vst3.plist" --identifier com.basemental.fg250.vst3 \
         --version "$VER" --install-location "/Library/Audio/Plug-Ins/VST3" "$WORK/vst3.pkg"

sed "s/@VERSION@/$VER/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"
productbuild --distribution "$WORK/distribution.xml" --resources "$HERE/resources" \
             --package-path "$WORK" "$OUT"
echo "Built $OUT"
