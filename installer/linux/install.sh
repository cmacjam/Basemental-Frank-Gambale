#!/bin/bash
# Basemental FG 250 Linux installer.
#   ./install.sh             install for this user (~/.vst3, ~/.lv2)
#   sudo ./install.sh --system   install for everyone (/usr/lib/vst3, /usr/lib/lv2)
#   ./install.sh --uninstall     remove (add --system for a system install)
set -euo pipefail
cd "$(dirname "$0")"
VST3="$HOME/.vst3"; LV2="$HOME/.lv2"
UNINSTALL=0
for a in "$@"; do
    case "$a" in
        --system) VST3=/usr/lib/vst3; LV2=/usr/lib/lv2 ;;
        --uninstall) UNINSTALL=1 ;;
    esac
done
if [ "$UNINSTALL" = 1 ]; then
    rm -rf "$VST3/Basemental FG 250.vst3" "$LV2/Basemental FG 250.lv2"
    echo "Basemental FG 250 removed."
    exit 0
fi
mkdir -p "$VST3" "$LV2"
rm -rf "$VST3/Basemental FG 250.vst3" "$LV2/Basemental FG 250.lv2"
cp -R "Basemental FG 250.vst3" "$VST3/"
cp -R "Basemental FG 250.lv2" "$LV2/"
echo "Installed:"
echo "  VST3 -> $VST3/Basemental FG 250.vst3"
echo "  LV2  -> $LV2/Basemental FG 250.lv2"
echo "Rescan plug-ins in your DAW."
