#!/usr/bin/env bash

# High-risk package baseline categories
CRITICAL_CORE=("linux" "linux-lts" "linux-zen" "systemd" "glibc" "pacman" "grub" "mkinitcpio")
GRAPHICS_DRIVERS=("nvidia" "nvidia-utils" "nvidia-open" "mesa" "xf86-video-amdgpu")
DESKTOP_ENV=("hyprland" "hyprland-git" "waybar" "pipewire" "dbus")

# Check for checkupdates dependency utility
if ! command -v checkupdates &> /dev/null; then
    zenity --error --title="Dependency Missing" --text="Error: pacman-contrib is missing.\nInstall with: sudo pacman -S pacman-contrib" --width=300
    exit 1
fi

# Create a temporary file to hold the GUI text report layout
REPORT=$(mktemp)

echo "====================================================" >> "$REPORT"
echo "      ARCH LINUX UPDATES & RISK ASSESSMENT REPORT   " >> "$REPORT"
echo "====================================================" >> "$REPORT"
echo "" >> "$REPORT"

# STEP 1: Parse Pending Update List (Repo + AUR)
repo_updates=$(checkupdates 2>/dev/null)
if command -v yay &> /dev/null; then
    aur_updates=$(yay -Qua 2>/dev/null)
else
    aur_updates=""
fi

updates=$(printf "%s\n%s" "$repo_updates" "$aur_updates" | grep -v '^$')
update_count=$(echo "$updates" | grep -v '^$' | wc -l)

if [ "$update_count" -eq 0 ] || [ -z "$updates" ]; then
    zenity --info --title="System Up To Date" --text="System is fully up to date. No actions required." --width=300
    rm -f "$REPORT"
    exit 0
fi
echo "[+] Found $update_count pending package updates." >> "$REPORT"
echo "" >> "$REPORT"

# STEP 2: Pull Latest Arch News Feed
news_titles=$(curl -s archlinux.org | grep -oP '(?<=<title>)[^<]+' | tail -n +2 | head -n 5)
if [ -n "$news_titles" ]; then
    echo "--- LATEST ARCH LINUX ANNOUNCEMENTS ---" >> "$REPORT"
    echo "$news_titles" | sed 's/^/  • /' >> "$REPORT"
    echo "" >> "$REPORT"
else
    echo "[-] Warning: Unable to pull official RSS news feed." >> "$REPORT"
    echo "" >> "$REPORT"
fi

# STEP 3: Scan Packages and Perform Risk Assessment Matrix
echo "[#] Commencing upgrade profile risk evaluations..." >> "$REPORT"
echo "----------------------------------------------------" >> "$REPORT"

risk_score=0
flagged_core=()
flagged_gpu=()
flagged_wm=()

while read -r line; do
    pkg_name=$(echo "$line" | awk '{print $1}')
    old_ver=$(echo "$line" | awk '{print $2}')
    new_ver=$(echo "$line" | awk '{print $4}')

    for item in "${CRITICAL_CORE[@]}"; do
        if [[ "$pkg_name" == "$item" ]]; then
            flagged_core+=("$pkg_name ($old_ver -> $new_ver)")
            ((risk_score += 15))
        fi
    done

    for item in "${GRAPHICS_DRIVERS[@]}"; do
        if [[ "$pkg_name" == "$item" ]]; then
            flagged_gpu+=("$pkg_name ($old_ver -> $new_ver)")
            ((risk_score += 20))
        fi
    done

    for item in "${DESKTOP_ENV[@]}"; do
        if [[ "$pkg_name" == "$item" ]]; then
            flagged_wm+=("$pkg_name ($old_ver -> $new_ver)")
            ((risk_score += 10))
        fi
    done
done <<< "$updates"

# STEP 4: Format Evaluation Results Summary
if [ ${#flagged_core[@]} -ne 0 ]; then
    echo "🚨 CRITICAL SYSTEM CORE REVISIONS:" >> "$REPORT"
    printf "  • %s\n" "${flagged_core[@]}" >> "$REPORT"
fi

if [ ${#flagged_gpu[@]} -ne 0 ]; then
    echo "🎮 GRAPHICS STACK MODIFICATIONS:" >> "$REPORT"
    printf "  • %s\n" "${flagged_gpu[@]}" >> "$REPORT"
fi

if [ ${#flagged_wm[@]} -ne 0 ]; then
    echo "🖥️  WINDOW MANAGER & SESSION CHANGES:" >> "$REPORT"
    printf "  • %s\n" "${flagged_wm[@]}" >> "$REPORT"
fi

echo "----------------------------------------------------" >> "$REPORT"
echo -n "OVERALL TRANSACTION RISK INDEX: " >> "$REPORT"

if [ "$risk_score" -eq 0 ]; then
    echo "LOW ($risk_score/100) - Safe routine package update." >> "$REPORT"
elif [ "$risk_score" -le 35 ]; then
    echo "MODERATE ($risk_score/100) - Review standard application changes." >> "$REPORT"
else
    echo "HIGH ($risk_score/100) - Core dependencies changing. Run a system backup first!" >> "$REPORT"
fi
echo "====================================================" >> "$REPORT"

# STEP 5: Pipe results file directly into Zenity GUI text window panel
zenity --text-info --title="Arch Linux Update Assessor" --font="FiraCode Nerd Font 10" --width=650 --height=500 --filename="$REPORT" --ok-label="Close"

# Clean up temporary report tracking file
rm -f "$REPORT"

