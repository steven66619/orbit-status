#!/usr/bin/env bash

# Define paths
CONFIG_DIR="/home/ste/.config/hypr"
BACKUP_DIR="$CONFIG_DIR/backups"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# Create backup directory if it does not exist
mkdir -p "$BACKUP_DIR"

# Backup the core configuration file safely
if [ -f "$CONFIG_DIR/hyprland.conf" ]; then
    cp "$CONFIG_DIR/hyprland.conf" "$BACKUP_DIR/hyprland_backup_$TIMESTAMP.conf"
    BACKUP_MSG="\\n   Backup saved to ~/..hypr/backups/"
else
    BACKUP_MSG="\\n   Backup failed: hyprland.conf not found."
fi

# Wait for desktop session tracking environment to stabilize
sleep 1

# Send the desktop notification to your user session
sudo -u ste DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus \
    notify-send -u critical -i "system-software-update" \
    "   Hyprland Updated" "here we go again time to fix stuff$BACKUP_MSG"

