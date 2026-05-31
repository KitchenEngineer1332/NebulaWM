# NebulaWM

NebulaWM is a lightweight and feature-rich window manager for X11, featuring its own compositor, bar, launcher, and lockscreen.

## Dependencies

Before installing NebulaWM, ensure you have the necessary dependencies installed for your distribution.

### 1) Arch Linux
```bash
sudo pacman -S base-devel libx11 libxinerama libxft imlib2 pam libxcomposite libxdamage libxfixes libxrender libxext mesa pkgconf
```

### 2) Fedora
```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install libX11-devel libXinerama-devel libXft-devel imlib2-devel pam-devel libXcomposite-devel libXdamage-devel libXfixes-devel libXrender-devel libXext-devel mesa-libGL-devel pkgconf-pkg-config
```

### 3) Ubuntu/Debian
```bash
sudo apt update
sudo apt install build-essential libx11-dev libxinerama-dev libxft-dev libimlib2-dev libpam0g-dev libxcomposite-dev libxdamage-dev libxfixes-dev libxrender-dev libxext-dev libgl1-mesa-dev pkg-config
```

## Installation

### Method 1: Automatic Installation (Recommended)
Simply run the included installation script, which will detect your distribution, install dependencies, and build the project:
```bash
git clone https://github.com/yourusername/NebulaWM.git
cd NebulaWM
chmod +x install.sh
./install.sh
```

### Method 2: Manual Installation
To install NebulaWM manually, follow these steps:

1. **Clone the repository:**
   ```bash
   git clone https://github.com/yourusername/NebulaWM.git
   cd NebulaWM
   ```

2. **Build and Install:**
   ```bash
   sudo make && sudo make install
   ```

## Post-Installation

After installation, you can select NebulaWM from your display manager (e.g., GDM, SDDM, LightDM). The configuration files will be automatically installed to `~/.config/Nebula/`.
