#!/usr/bin/env bash

# Use Rofi to prompt a clean structural Yes/No choice layer
choice=$(printf "   Cancel\n   Exit Hyprland" | rofi -dmenu -i -p "⚠️ Are you sure you want to log out?" -theme-str 'window {width: 25%;} listview {lines: 2;}')

# Evaluate user choice selection
if [ "$choice" = "   Exit Hyprland" ]; then
    hyprctl dispatch exit
else
    echo "Logout aborted by user."
    exit 0
fi

