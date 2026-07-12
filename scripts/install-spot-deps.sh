#!/usr/bin/env bash
set -euo pipefail

echo "Installing build dependencies for Spot (GTK4 + libadwaita)..."
sudo apt-get install -y libgtk-4-dev libadwaita-1-dev fonts-inter flatpak
