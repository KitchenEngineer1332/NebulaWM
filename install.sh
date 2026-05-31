#!/bin/bash

# Exit on error
set -e

# ANSI Color Codes
BLUE='\033[1;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}"
cat << "EOF"
 ███╗   ██╗███████╗██████╗ ██╗   ██╗██╗      █████╗ ██╗    ██╗███╗   ███╗
 ████╗  ██║██╔════╝██╔══██╗██║   ██║██║     ██╔══██╗██║    ██║████╗ ████║
 ██╔██╗ ██║█████╗  ██████╔╝██║   ██║██║     ███████║██║ █╗ ██║██╔████╔██║
 ██║╚██╗██║██╔══╝  ██╔══██╗██║   ██║██║     ██╔══██║██║███╗██║██║╚██╔╝██║
 ██║ ╚████║███████╗██████╔╝╚██████╔╝███████╗██║  ██║╚███╔███╔╝██║ ╚═╝ ██║
 ╚═╝  ╚═══╝╚══════╝╚═════╝  ╚═════╝ ╚══════╝╚═╝  ╚═╝ ╚══╝╚══╝ ╚═╝     ╚═╝
EOF
echo -e "${NC}"

if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    LIKE=$ID_LIKE
else
    echo "Error: Could not detect distribution. /etc/os-release not found."
    exit 1
fi

install_dependencies() {
    case "$OS" in
        arch)
            echo "Installing dependencies for Arch Linux..."
            sudo pacman -S --needed --noconfirm base-devel libx11 libxinerama libxft imlib2 pam libxcomposite libxdamage libxfixes libxrender libxext mesa pkgconf
            ;;
        fedora)
            echo "Installing dependencies for Fedora..."
            sudo dnf groupinstall -y "Development Tools"
            sudo dnf install -y libX11-devel libXinerama-devel libXft-devel imlib2-devel pam-devel libXcomposite-devel libXdamage-devel libXfixes-devel libXrender-devel libXext-devel mesa-libGL-devel pkgconf-pkg-config
            ;;
        ubuntu|debian|kali|linuxmint)
            echo "Installing dependencies for $OS..."
            sudo apt update
            sudo apt install -y build-essential libx11-dev libxinerama-dev libxft-dev libimlib2-dev libpam0g-dev libxcomposite-dev libxdamage-dev libxfixes-dev libxrender-dev libxext-dev libgl1-mesa-dev pkg-config
            ;;
        *)
            # Check ID_LIKE for derivatives
            if [[ "$LIKE" == *"arch"* ]]; then
                echo "Installing dependencies for Arch-based distribution..."
                sudo pacman -S --needed --noconfirm base-devel libx11 libxinerama libxft imlib2 pam libxcomposite libxdamage libxfixes libxrender libxext mesa pkgconf
            elif [[ "$LIKE" == *"debian"* ]] || [[ "$LIKE" == *"ubuntu"* ]]; then
                echo "Installing dependencies for Debian/Ubuntu-based distribution..."
                sudo apt update
                sudo apt install -y build-essential libx11-dev libxinerama-dev libxft-dev libimlib2-dev libpam0g-dev libxcomposite-dev libxdamage-dev libxfixes-dev libxrender-dev libxext-dev libgl1-mesa-dev pkg-config
            elif [[ "$LIKE" == *"fedora"* ]]; then
                echo "Installing dependencies for Fedora-based distribution..."
                sudo dnf groupinstall -y "Development Tools"
                sudo dnf install -y libX11-devel libXinerama-devel libXft-devel imlib2-devel pam-devel libXcomposite-devel libXdamage-devel libXfixes-devel libXrender-devel libXext-devel mesa-libGL-devel pkgconf-pkg-config
            else
                echo "Unsupported distribution: $OS"
                exit 1
            fi
            ;;
    esac
}

install_dependencies

echo "Building and installing NebulaWM..."
sudo make clean
sudo make && sudo make install

echo ""
echo "We are good to go, Just logout and login to NebulaWM"
