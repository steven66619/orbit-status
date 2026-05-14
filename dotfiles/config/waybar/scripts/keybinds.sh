#!/usr/bin/env bash

# Path to your Hyprland configuration file
CONFIG="$HOME/.config/hypr/hyprland.conf"

if [ ! -f "$CONFIG" ]; then
    echo "Configuration file not found!"
    exit 1
fi

# Extract mod variable definitions (e.g., $mainMod = SUPER)
eval "$(grep -E '^[[:space:]]*\$[A-Za-z0-9_]+[[:space:]]*=' "$CONFIG" | sed 's/\s*//g')"

# Parse bindings, swap out defined variables, format into a readable list, and send to Rofi
grep -E '^[[:space:]]*bind[e]?\s*=' "$CONFIG" | \
    sed 's/^[[:space:]]*bind[e]*\s*=\s*//g' | \
    while read -r line; do
        # Replace variable names like $mainMod with their literal equivalents
        expanded_line=$(echo "$line" | sed "s/\$mainMod/$mainMod/g")
        
        # Split keybind elements cleanly into readable columns
        IFS=',' read -r mods key dispatcher params <<< "$expanded_line"
        
        # Clean up stray whitespaces
        mods=$(echo "$mods" | xargs)
        key=$(echo "$key" | xargs)
        dispatcher=$(echo "$dispatcher" | xargs)
        params=$(echo "$params" | xargs)

        # Structure formatting: Key combination -> Action performed
        printf "%-25s →   %s %s\n" "[$mods + $key]" "$dispatcher" "$params"
    done | rofi -dmenu -i -p "   Hyprland Keybinds" -theme-str 'window {width: 40%;} listview {lines: 15;}'

