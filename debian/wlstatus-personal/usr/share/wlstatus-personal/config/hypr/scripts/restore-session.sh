#!/usr/bin/env bash

# Terminate any existing duplicate processes safely
pkill -x pcmanfm
pkill -x kitty
pkill -x google-chrome-stable

# Workspace 1: Primary terminal hub
hyprctl dispatch exec "[workspace 1]" kitty

# Workspace 2: Default web navigation
hyprctl dispatch exec "[workspace 2]" google-chrome-stable

# Workspace 3: File system manager
hyprctl dispatch exec "[workspace 3]" pcmanfm

