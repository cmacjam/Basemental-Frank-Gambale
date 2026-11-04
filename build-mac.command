#!/bin/bash
# =============================================================================
#  Basemental FG 250 — build and install the Audio Unit (Logic Pro, GarageBand,
#  MainStage) and VST3 on this Mac.
#
#  Double-click this file in Finder (first time: right-click > Open), or run
#      bash build-mac.command
#  in Terminal from this folder. Takes about 3-8 minutes.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"

say() { printf "\n\033[1;33m== %s\033[0m\n" "$1"; }

say "Basemental FG 250 Mac build"

# 1. Apple's Command Line Tools (compiler, git, make)
if ! xcode-select -p >/dev/null 2>&1; then
    say "Apple Command Line Tools are needed"
    echo "A window will open. Click Install, wait for it to finish (about 5-10 min),"
    echo "then run this script again."
    xcode-select --install || true
    exit 1
fi

# 2. CMake (uses an existing one, otherwise installs it for this user only)
CMAKE="$(command -v cmake || true)"
[ -z "$CMAKE" ] && [ -x /Applications/CMake.app/Contents/bin/cmake ] && CMAKE=/Applications/CMake.app/Contents/bin/cmake
[ -z "$CMAKE" ] && [ -x /opt/homebrew/bin/cmake ] && CMAKE=/opt/homebrew/bin/cmake
if [ -z "$CMAKE" ]; then
    say "Installing CMake (user-only, via pip)"
    python3 -m pip install --user --quiet --disable-pip-version-check cmake
    CMAKE="$(python3 -m site --user-base)/bin/cmake"
fi
"$CMAKE" --version | head -1

# 3. Build a universal binary (Apple Silicon + Intel)
say "Building (first run downloads the JUCE framework)"
"$CMAKE" -S . -B build-mac -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
"$CMAKE" --build build-mac --config Release --parallel "$(sysctl -n hw.ncpu)" --target Fusion250_AU Fusion250_VST3

ART="build-mac/Fusion250_artefacts/Release"
COMP="$HOME/Library/Audio/Plug-Ins/Components"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"

# 4. Install for this user, clear quarantine, ad-hoc sign (needed on Apple Silicon)
say "Installing"
mkdir -p "$COMP" "$VST3"
rm -rf "$COMP/Basemental FG 250.component" "$VST3/Basemental FG 250.vst3"
cp -R "$ART/AU/Basemental FG 250.component" "$COMP/"
cp -R "$ART/VST3/Basemental FG 250.vst3" "$VST3/"
for b in "$COMP/Basemental FG 250.component" "$VST3/Basemental FG 250.vst3"; do
    xattr -dr com.apple.quarantine "$b" 2>/dev/null || true
    codesign --force --deep --sign - "$b"
done
echo "AU   -> $COMP/Basemental FG 250.component"
echo "VST3 -> $VST3/Basemental FG 250.vst3"

# 5. Validate exactly the way Logic Pro does
say "Validating the Audio Unit (auval)"
killall -9 AudioComponentRegistrar 2>/dev/null || true
if auval -v aufx F250 Fsna > build-mac/auval.log 2>&1 && grep -q "AU VALIDATION SUCCEEDED" build-mac/auval.log; then
    say "AU VALIDATION SUCCEEDED"
    echo "Open Logic Pro, then on a track: Audio FX > Audio Units > Basemental > Basemental FG 250."
    echo "If Logic was already open, quit and reopen it. If it still isn't listed:"
    echo "Logic Pro > Settings > Plug-in Manager > select Basemental FG 250 > Reset & Rescan Selection."
else
    tail -30 build-mac/auval.log
    say "auval reported a problem; full log in build-mac/auval.log"
    exit 1
fi
