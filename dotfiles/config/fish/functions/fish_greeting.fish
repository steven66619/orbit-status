function fish_greeting
    # Run fastfetch first so it stays at the top of your terminal screen
    if command -v fastfetch > /dev/null
        fastfetch
        echo ""
    end

    # Define path to your Hyprland configuration file
    set hypr_config "$HOME/.config/hypr/hyprland.conf"

    if not test -f $hypr_config
        return
    end

    # Borderlands HUD Styled Header
    set_color yellow; echo "===================================================="
    set_color cyan;   echo "     CURRENT HYPRLAND KEYBINDS SYSTEM REFERENCE "
    set_color yellow; echo "===================================================="
    set_color normal

    # Extract mod variable definitions dynamically (e.g., $mainMod = SUPER)
    set mainMod (grep -E '^[[:space:]]*\$mainMod[[:space:]]*=' $hypr_config | awk -F'= ' '{print $2}' | xargs)
    if test -z "$mainMod"
        set mainMod "SUPER"
    end

    # Parse and format the keybind rows elegantly in two text columns
    grep -E '^[[:space:]]*bind[e]?\s*=' $hypr_config | \
    sed 's/^[[:space:]]*bind[e]*\s*=\s*//g' | \
    while read -l line
        # Substitute layout variable markers into literals
        set expanded_line (string replace '$mainMod' "$mainMod" "$line")
        
        # Split tokens cleanly using commas
        set tokens (string split "," "$expanded_line")
        
        set mods (string trim $tokens[1])
        set key (string trim $tokens[2])
        set dispatcher (string trim $tokens[3])
        set params (string trim $tokens[4])

        # Skip incomplete or multi-parameter parsing layout rows safely
        if test -z "$key"; or test -z "$dispatcher"
            continue
        end

        # Format key combinations
        set combo "[$mods + $key]"
        if test -z "$mods"
            set combo "[$key]"
        end

        # Render rows using high-contrast color codes matching the theme
        set_color green
        printf "%-26s " $combo
        set_color normal
        printf "→   %s %s\n" $dispatcher $params
    end

    set_color yellow; echo "===================================================="; set_color normal
    echo ""
end

