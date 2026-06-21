#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SESSION="/Users/mreff555/.grok/sessions/%2FUsers%2Fmreff555%2F.grok%2Fworktrees%2Fsrc-detective-game%2Fdetective-game/019edda2-97be-7b63-9a61-52f0e9332303/images"

install_icon() {
  local src="$1"
  local dest="$2"
  sips -s format png -z 256 256 "$SESSION/$src" --out "$ROOT/resources/icons/$dest" >/dev/null
}

install_examine() {
  local src="$1"
  local dest="$2"
  sips -s format png -z 832 1248 "$SESSION/$src" --out "$ROOT/resources/images/$dest" >/dev/null
}

sips -s format png -z 1117 750 "$SESSION/80.jpg" --out "$ROOT/resources/images/alpine_hardware.png" >/dev/null

install_icon 87.jpg mining_pick_icon.png
install_icon 85.jpg shovel_icon.png
install_icon 88.jpg garden_rake_icon.png
install_icon 84.jpg hand_drill_icon.png
install_icon 86.jpg blacksmith_hammer_icon.png
install_icon 83.jpg mid_anvil_icon.png
install_icon 82.jpg box_of_nails_icon.png
install_icon 81.jpg copper_wire_spool_icon.png

install_examine 90.jpg mining_pick_examine.png
install_examine 91.jpg shovel_examine.png
install_examine 92.jpg garden_rake_examine.png
install_examine 89.jpg hand_drill_examine.png
install_examine 93.jpg blacksmith_hammer_examine.png
install_examine 94.jpg mid_anvil_examine.png
install_examine 95.jpg box_of_nails_examine.png
install_examine 96.jpg copper_wire_spool_examine.png

cd "$ROOT"
python3 tools/compress_images.py resources/images/alpine_hardware.png --remove-source
python3 tools/compress_images.py resources/icons/mining_pick_icon.png resources/icons/shovel_icon.png resources/icons/garden_rake_icon.png resources/icons/hand_drill_icon.png resources/icons/blacksmith_hammer_icon.png resources/icons/mid_anvil_icon.png resources/icons/box_of_nails_icon.png resources/icons/copper_wire_spool_icon.png --remove-source
python3 tools/compress_images.py resources/images/mining_pick_examine.png resources/images/shovel_examine.png resources/images/garden_rake_examine.png resources/images/hand_drill_examine.png resources/images/blacksmith_hammer_examine.png resources/images/mid_anvil_examine.png resources/images/box_of_nails_examine.png resources/images/copper_wire_spool_examine.png --remove-source

echo "Installed alpine hardware images"