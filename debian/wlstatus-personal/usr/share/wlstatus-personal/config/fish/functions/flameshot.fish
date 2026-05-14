function flameshot --description 'Launch Flameshot wrapped in a Sway environment profile'
    env XDG_CURRENT_DESKTOP=Sway /usr/bin/flameshot $argv
end

