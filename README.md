
```text
███╗   ██╗███████╗██████╗ ██╗   ██╗██╗      █████╗ ██╗    ██╗███╗   ███╗
████╗  ██║██╔════╝██╔══██╗██║   ██║██║     ██╔══██╗██║    ██║████╗ ████║
██╔██╗ ██║█████╗  ██████╔╝██║   ██║██║     ███████║██║ █╗ ██║██╔████╔██║
██║╚██╗██║██╔══╝  ██╔══██╗██║   ██║██║     ██╔══██║██║███╗██║██║╚██╔╝██║
██║ ╚████║███████╗██████╔╝╚██████╔╝███████╗██║  ██║╚███╔███╔╝██║ ╚═╝ ██║
╚═╝  ╚═══╝╚══════╝╚═════╝  ╚═════╝ ╚══════╝╚═╝  ╚═╝ ╚══╝╚══╝ ╚═╝     ╚═╝
```


NebulaWM is a lightweight and feature-rich window manager for X11, featuring its own compositor, bar, launcher, and lockscreen. It is designed for simplicity, speed, and a modern aesthetic.

## Components

NebulaWM is composed of several independent modules:

- **nebulawm**: The core window manager.
- **nebula-bar**: A sleek, CSS-inspired status bar with workspace indicators and system info.
- **nebula-compositor**: A high-performance X11 compositor providing transparency and rounded corners.
- **nebula-launcher**: A minimal, fast application launcher.
- **nebula-lockscreen**: A secure and customizable lockscreen with PAM integration.
- **nebula-powermenu**: A simple menu for power management (reboot, shutdown, etc.).
- **starlight**: A fast, lightweight terminal emulator built-in.

## Dependencies

Before installing NebulaWM, ensure you have the necessary dependencies installed for your distribution.

### 1) Arch Linux
```bash
sudo pacman -S base-devel libx11 libxinerama libxft imlib2 pam libxcomposite libxdamage libxfixes libxrender libxext mesa pkgconf fontconfig ttf-firacode-nerd
```

### 2) Fedora
```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install libX11-devel libXinerama-devel libXft-devel imlib2-devel pam-devel libXcomposite-devel libXdamage-devel libXfixes-devel libXrender-devel libXext-devel mesa-libGL-devel pkgconf-pkg-config fontconfig-devel
# Note: FiraCode Nerd Font is recommended for icons (e.g., from Copr)
```

### 3) Ubuntu/Debian
```bash
sudo apt update
sudo apt install build-essential libx11-dev libxinerama-dev libxft-dev libimlib2-dev libpam0g-dev libxcomposite-dev libxdamage-dev libxfixes-dev libxrender-dev libxext-dev libgl1-mesa-dev pkg-config libfontconfig1-dev
# Note: FiraCode Nerd Font is recommended for icons
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

## Configuration

NebulaWM configurations are stored in `~/.config/Nebula/`. These files are automatically created with default values upon the first run or after installation.

### 1. `nebula.config` (Main Window Manager)
This file controls the core behavior and appearance of the window manager.

| Key | Description | Default |
|-----|-------------|---------|
| `border_width` | Width of window borders in pixels | `2` |
| `primary_color` | Hex color used for focused windows and UI themes | `#ff0055` |
| `terminal` | Default terminal emulator command | `starlight` |
| `modifier` | The modifier key (`Mod4` for Super, `Mod1` for Alt) | `Mod4` |
| `launcher` | Command used to invoke the application launcher | `nebula-launcher` |
| `bar_height` | Height reserved for the status bar | `30` |
| `wmtype` | Window management style (`nogaps` or `float`) | `nogaps` |
| `wallpaper` | Absolute path to the wallpaper image | (empty) |

### 2. `bar.config` (Status Bar)
Controls the appearance of `nebula-bar`.

| Key | Description | Default |
|-----|-------------|---------|
| `height` | Height of the bar in pixels | `32` |
| `font` | Xft font string for the bar | `3270 Nerd Font Mono:size=10` |

*Note: The bar's color scheme is automatically derived from the `primary_color` defined in `nebula.config`.*

#### Interactive Bar Features:
- **Volume Module:**
    - **Scroll:** Increase/Decrease speaker volume.
    - **Left-Click:** Toggle mute/unmute.
- **Microphone Module:**
    - **Scroll:** Increase/Decrease microphone sensitivity.
    - **Left-Click:** Toggle mute/unmute.
- **Time:** Displays hours, minutes, and seconds.
- **System Info:** Real-time CPU and RAM usage, plus battery status.

### 3. `lockscreen.conf` (Lockscreen)
Controls the aesthetics and behavior of `nebula-lockscreen`.

| Key | Description | Default |
|-----|-------------|---------|
| `font` | Font used for the clock | `monospace:size=32` |
| `date_font` | Font used for the date and messages | `monospace:size=16` |
| `background_type` | Type of background (`color`, `image`, or `blur`) | `color` |
| `background_image`| Path to the image if `background_type` is `image` | (empty) |
| `blur_radius` | Blur strength if `background_type` is `blur` | `10` |
| `clock_pos_x` | X position (`center` or pixel value) | `center` |
| `clock_pos_y` | Y position (`center` or pixel value) | `center` |

## Usage

- **Mod + Enter**: Open terminal (`terminal` in config)
- **Mod + D**: Open launcher (`launcher` in config)
- **Mod + Q**: Close focused window
- **Mod + Shift + Q**: Exit NebulaWM
- **Mod + F**: Toggle tiling/floating layout for current workspace
- **Mod + Shift + F**: Toggle floating for focused window
- **Alt + Tab**: Cycle through windows
- **Ctrl + Alt + Delete**: Open power menu (`nebula-powermenu`)
- **Mod + [1-9]**: Switch to workspace 1-9
- **Mod + Shift + [1-9]**: Move window to workspace 1-9

## Post-Installation

After installation, you can select NebulaWM from your display manager (e.g., GDM, SDDM, LightDM). The configuration files will be automatically installed to `~/.config/Nebula/`.
