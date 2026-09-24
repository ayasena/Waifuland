# Waifuland

> **Disclaimer:** This repository is "vibe coded". Please use with caution.

A desktop Live2D model viewer that renders Live2D characters as transparent overlay windows always on top of your desktop — on Linux via Wayland's `wlr-layer-shell` protocol, and on macOS via a native Cocoa/Quartz overlay window.

Live2D models float on your desktop with click-through transparency — only the model itself receives input. Built with the Live2D Cubism SDK for Native and OpenGL, using native windowing on each platform (Wayland layer-shell on Linux, Cocoa/NSOpenGL on macOS — no X11, no GLFW).

<!-- Screenshot or GIF placeholder: place a demo image/gif here -->
<!-- ![Waifuland Demo](docs/demo.gif) -->


https://github.com/user-attachments/assets/95dfdedc-4957-4bb7-b79f-f3addd97a6db



## Table of Contents

- [Features](#features)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Usage](#usage)
- [IPC API](#ipc-api)
- [Model Setup](#model-setup)
- [Architecture](#architecture)
- [Built With](#built-with)
- [License](#license)

## Features

- **Multiple characters, one window** — show several Live2D models at once, each independently draggable, zoomable, and switchable, instead of running a separate instance per character (see [Multiple characters](#multiple-characters))
- **Native overlay on Linux & macOS** — a Wayland layer-shell surface on Linux, a borderless floating Cocoa window on macOS; always on top of your desktop
- **Click-through transparency** — only the Live2D model area receives pointer events; everything else passes through
- **Multi-compositor support (Linux)** — works on any Wayland compositor supporting `wlr-layer-shell` (Hyprland, Sway, river, etc.)
- **Multi-output support** — switch between monitors (Hyprland on Linux; any multi-display setup on macOS)
- **Interactive** — responds to mouse drag, tap, and scroll input
- **Motion & expression** — supports idle animations, lip-sync, eye-blink, physics, and expressions
- **Configurable model directory** — load models from any path via CLI flag or XDG config
- **JSON configuration** — customize behavior via `config.json` (default model, emotion timeout, additional model dirs, scale/position, window size)
- **IPC control** — Unix domain socket API for external control (model switching, expressions, motions, lipsync, zoom, position, look-at direction)

## Prerequisites

### Linux

| Dependency | Notes |
|---|---|
| **Linux with Wayland compositor** | Must support `wlr-layer-shell-v1` (Hyprland, Sway, river, etc.). x86_64 is the SDK's stable target; arm64 (Raspberry Pi, ARM servers) uses Live2D's *experimental* Cubism Core build and may be less stable. |
| **Live2D Cubism SDK for Native** | Download from [live2d.com/sdk](https://www.live2d.com/en/sdk/about/) (proprietary, not bundled) |
| **C++ compiler** | GCC or Clang with C++14 support |
| **CMake** | >= 3.16 |
| **pkg-config** | For finding Wayland/EGL libraries |
| **Wayland development libraries** | `wayland-client`, `wayland-egl`, `wayland-cursor` |
| **EGL & OpenGL** | `libegl-dev`, `libgl-dev` or equivalent |
| **wayland-scanner** | Usually part of `wayland-protocols` or `wayland` dev packages |
| **curl, unzip** | For downloading third-party dependencies (GLEW) |

### macOS

| Dependency | Notes |
|---|---|
| **macOS** | Uses native Cocoa/Quartz windowing — no Wayland/EGL/pkg-config needed |
| **Live2D Cubism SDK for Native** | Download from [live2d.com/sdk](https://www.live2d.com/en/sdk/about/) (proprietary, not bundled) |
| **Xcode Command Line Tools** | `xcode-select --install` — provides clang++ and the Cocoa/OpenGL frameworks |
| **CMake** | >= 3.16 |
| **curl, unzip** | For downloading third-party dependencies (GLEW) |

The macOS SDK ships separate `Core/lib/macos/arm64/` and `Core/lib/macos/x86_64/` static libs (no universal binary); `CMakeLists.txt` picks the right one automatically based on `CMAKE_SYSTEM_PROCESSOR`.

### Installing system dependencies

If you use [mise](https://mise.jdx.dev/), `mise trust && mise bootstrap --yes` installs the packages below automatically for whichever package manager is on your system (apt, pacman, dnf, or Homebrew on macOS). On macOS you still need to run `xcode-select --install` yourself first — that's not something a package manager can install. Otherwise, install manually:

**Arch Linux:**
```bash
sudo pacman -S --needed base-devel cmake pkgconf wayland wayland-protocols libglvnd glu egl-wayland curl unzip
```

**Ubuntu / Debian:**
```bash
sudo apt install build-essential cmake pkg-config libwayland-dev wayland-protocols libegl-dev libgl-dev libglu1-mesa-dev curl unzip
```

**Fedora:**
```bash
sudo dnf install gcc-c++ cmake pkgconf-pkg-config wayland-devel wayland-protocols-devel mesa-libEGL-devel mesa-libGL-devel mesa-libGLU-devel curl unzip
```

**macOS:**
```bash
xcode-select --install
brew install cmake curl
```

## Installation

### 1. Clone the repository

```bash
git clone https://github.com/shinkuan/waifuland.git
cd waifuland
```

### 2. Download the Live2D Cubism SDK

1. Go to [https://www.live2d.com/en/sdk/about/](https://www.live2d.com/en/sdk/about/)
2. Download **Cubism SDK for Native**
3. Extract it into the project root directory:
   ```
   waifuland/
   ├── CubismSdkForNative-5-r.5/   <-- extracted SDK here
   ├── CMakeLists.txt
   ├── src/
   └── ...
   ```
4. If the folder name differs from `CubismSdkForNative-5-r.5`, update `SDK_ROOT_PATH` in `CMakeLists.txt`.

### 3. Build

Use the install script (recommended):

```bash
chmod +x install.sh
./install.sh
```

Or build manually:

```bash
cd thirdParty && bash scripts/setup_glew && cd ..
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

The binary will be at `build/bin/waifuland`.

### Sucrette / fairyd

`./install.sh fedora` (or `arch`) first installs the build dependencies via dnf/pacman. [Sucrette](https://github.com/ayasena/sucrette)'s extras module runs it this way. It also installs `waifuland` and `waifuland-ctl` to `~/.local/bin` and enables the `waifuland.service` systemd user unit (started with `graphical-session.target`).

- The Cubism SDK license prompt needs a terminal. Without one, set `WAIFULAND_ACCEPT_LIVE2D_LICENSE=1` to accept the licenses. Otherwise the install is skipped (exit 0) and the script prints a re-run hint.
- [fairyd](https://github.com/ayasena/fairyd) talks to `$XDG_RUNTIME_DIR/waifuland.sock` for its `[[cast]]` members, binding each one by `overlay_model = "<model name>"`. List those models under `characters` in `config.json`.

## Usage

```bash
# Run with default model directory (~/.config/waifuland/models/)
./build/bin/waifuland

# Run with a custom model directory
./build/bin/waifuland --models_dir /path/to/your/models

# Run with a custom config file
./build/bin/waifuland --config /path/to/config.json
```

### Configuration

Waifuland reads a JSON config file from `$XDG_CONFIG_HOME/waifuland/config.json` (fallback: `~/.config/waifuland/config.json`). Override the path with `--config <path>`.

Example `config.json`:

```json
{
    "additional_model_dirs": [
        "/home/user/extra-models",
        "/opt/shared-models"
    ],
    "default_model": "MyFavoriteModel",
    "emotion_timeout": 5,
    "model_scale": 1.0,
    "model_x": 0.0,
    "model_y": 0.0,
    "window_width": 1900,
    "window_height": 1000
}
```

| Key | Type | Default | Description |
|---|---|---|---|
| `additional_model_dirs` | string[] | `[]` | Extra directories to scan for models (in addition to the default models dir) |
| `default_model` | string | `""` | Name of the model subfolder to load first on startup (single-character shorthand — see [Multiple characters](#multiple-characters) below) |
| `emotion_timeout` | float | `5` | Seconds before expression reverts to default. Set to `-1` to never revert |
| `model_scale` | float | `1.0` | Initial model scale (used with `default_model`) |
| `model_x` | float | `0.0` | Initial model X offset (used with `default_model`) |
| `model_y` | float | `0.0` | Initial model Y offset (used with `default_model`) |
| `window_width` | int | `1900` | Render target width in pixels |
| `window_height` | int | `1000` | Render target height in pixels |

All fields are optional. Missing fields use their default values. If the config file doesn't exist, all defaults are used.

### Multiple characters

Instead of running `waifuland` once per character, a single instance can show several Live2D models at once in one window — each independently draggable, zoomable, and switchable. Replace `default_model`/`model_scale`/`model_x`/`model_y` with a `characters` array:

```json
{
    "characters": [
        { "model": "MyFavoriteModel" },
        { "model": "AnotherModel", "x": 0.6, "scale": 0.9 }
    ],
    "window_width": 1900,
    "window_height": 1000
}
```

| Key | Type | Default | Description |
|---|---|---|---|
| `model` | string | *(required)* | Name of the model subfolder |
| `x`, `y` | float | auto | Position offset (screen space, roughly -1.0..1.0). Omit both to auto-arrange this character in an evenly-spaced row alongside the others. Supplying only one axis pins the other to 0 (same rule as IPC `add_character`) |
| `scale` | float | `1.0` | Initial zoom for this character |

A character with no `x`/`y` is auto-positioned; the row re-flows (only for auto-positioned characters) whenever a character is added or removed, including at runtime via IPC. Dragging a character removes it from auto-layout — it keeps whatever position you drop it at.

Each character can be dragged, scroll-zoomed, and right-click/middle-click switched independently — interactions are routed to whichever character is under the cursor. The IPC API can also target a specific character by id (see below). A config with no `characters` array and a `default_model` set is equivalent to a single-character `characters` array, for backward compatibility.

### Controls

| Input | Action |
|---|---|
| **Left-click Upper Body** | Trigger expression change (if any) |
| **Left-click Lower Body** | Trigger motion (if any) | 
| **Left-click drag** | Drag the model / trigger hit areas |
| **Right-click** | Switch to next model |
| **Middle-click** | Switch skin (if any) |
| **Scroll wheel** | Zoom in/out |

### Compositor Compatibility

Waifuland runs on any Wayland compositor that supports the `wlr-layer-shell` protocol. Some features require compositor-specific IPC and are only available on Hyprland:

| Feature | Hyprland | Sway | Other wlroots |
|---|---|---|---|
| Overlay rendering | Yes | Yes | Yes |
| Click-through transparency | Yes | Yes | Yes |
| Mouse drag / tap / scroll | Yes | Yes | Yes |
| Global cursor tracking (eyes follow cursor anywhere) | Yes | No | No |
| Cross-monitor drag | Yes | No | No |
| Move to focused monitor | Yes | Yes | No |

The compositor is auto-detected at startup. Feature availability is logged to the console.

### Toggling Visibility

You can hide and show your Live2D model (along with its click-through input region) by sending a `SIGUSR1` signal to the background process. When hidden, it uses virtually no resources.

```bash
kill -SIGUSR1 $(pgrep -x waifuland)
# Or
killall -s SIGUSR1 waifuland
# Or using the included toggle command:
./build/bin/waifuland toggle
```

### Move to Focused Monitor

Move the model to whichever monitor currently has focus by sending a `SIGUSR2` signal. This works on Hyprland and Sway.

```bash
kill -SIGUSR2 $(pgrep -x waifuland)
# Or
killall -s SIGUSR2 waifuland
# Or using the included focus command:
./build/bin/waifuland focus
```

**Hyprland keybind examples**  
Add these to your `~/.config/hypr/hyprland.conf`:
```ini
# Toggle waifuland visibility with Super + W
bind = SUPER, W, exec, killall -s SIGUSR1 waifuland
# Move waifuland to focused monitor with Super + Shift + W
bind = SUPER SHIFT, W, exec, killall -s SIGUSR2 waifuland
```

## IPC API

Waifuland exposes a Unix domain socket for external control by scripts and programs.

- **Socket path:** `$XDG_RUNTIME_DIR/waifuland.sock` (fallback: `/tmp/waifuland.sock`)
- **Protocol:** Newline-delimited JSON — send `{"command":"<name>", ...}\n`, receive `{"ok":true, ...}\n`
- **Requires:** `socat` (install via your package manager)

### Targeting a character

When multiple characters are shown at once (see [Multiple characters](#multiple-characters)), every per-model command below accepts an optional `"character": <id>` field to target one of them specifically. Omitting it follows the first character currently shown (not literal id `0` — ids are never reused, so after a removal the first id may be something else). Character ids come from `list_characters` (below) and stay stable across add/remove — they are not the same as a character's position in the list.

### CLI Client

A convenience CLI client `waifuland-ctl` is included in the project root:

```bash
./waifuland-ctl <command> [--key value ...]
```

### Available Commands

#### `get_status` — Get general status

Returns current model, zoom, position, and visibility.

```bash
./waifuland-ctl get_status
```

```json
{
    "ok": true,
    "model": "MyModel",
    "model_index": 0,
    "model_count": 3,
    "hidden": false,
    "zoom": 1.0,
    "x": 0.0,
    "y": 0.0
}
```

#### `get_available_models` — List all discovered models

```bash
./waifuland-ctl get_available_models
```

```json
{
    "ok": true,
    "models": ["MyModel", "AnotherModel", "ThirdModel"]
}
```

#### `get_current_model` — Get current model name and index

```bash
./waifuland-ctl get_current_model
```

```json
{
    "ok": true,
    "model": "MyModel",
    "index": 0
}
```

#### `set_model` — Switch model by name or index

```bash
# By name
./waifuland-ctl set_model --name "AnotherModel"

# By index
./waifuland-ctl set_model --index 2
```

```json
{
    "ok": true
}
```

#### `next_model` — Switch to next model

```bash
./waifuland-ctl next_model
```

```json
{
    "ok": true
}
```

#### `prev_model` — Switch to previous model

```bash
./waifuland-ctl prev_model
```

```json
{
    "ok": true
}
```

#### `get_expressions` — List expressions for current model

```bash
./waifuland-ctl get_expressions
```

```json
{
    "ok": true,
    "expressions": ["happy.exp3.json", "angry.exp3.json", "sad.exp3.json"]
}
```

#### `set_expression` — Set expression by ID

```bash
./waifuland-ctl set_expression --id "happy.exp3.json"
```

```json
{
    "ok": true
}
```

#### `get_motions` — List motions for current model

```bash
./waifuland-ctl get_motions
```

```json
{
    "ok": true,
    "motions": [
        {"group": "Idle", "index": 0, "file": "idle_01.motion3.json"},
        {"group": "TapBody", "index": 0, "file": "tap_01.motion3.json"}
    ]
}
```

#### `do_motion` — Play a motion

```bash
# Play a specific motion by group and index
./waifuland-ctl do_motion --group "TapBody" --index 0

# Play with custom priority (default: 2 = Normal)
./waifuland-ctl do_motion --group "Idle" --index 0 --priority 3

# Play a random TapBody motion (omit group)
./waifuland-ctl do_motion
```

```json
{
    "ok": true
}
```

#### `set_mouth_y` — Set mouth opening for external lipsync

Value range: `0.0` (closed) to `1.0` (fully open). Send continuously for real-time lipsync. Routes to one character only (see [Targeting a character](#targeting-a-character)) — other characters are unaffected. When traffic stops, the mouth eases shut on its own. Sends no reply unless `"ack": true` is included, so lipsync-rate traffic stays cheap.

```bash
./waifuland-ctl set_mouth_y --value 0.8
./waifuland-ctl set_mouth_y --character 1 --value 0.3 --ack
```

```json
{
    "ok": true
}
```

#### `set_mouth_batch` — Set several mouths in one call

Same as `set_mouth_y`, but moves every listed character with a single request — one syscall per audio chunk instead of one per character. Unknown ids are skipped. Silent unless `"ack": true`.

```bash
./waifuland-ctl set_mouth_batch --mouths '{"0":0.8,"1":0.1}'
```

#### `set_model_zoom` — Set model zoom/scale

Value range: `0.1` to `10.0`.

```bash
./waifuland-ctl set_model_zoom --value 1.5
```

```json
{
    "ok": true
}
```

#### `set_model_position` — Set model position offset

```bash
./waifuland-ctl set_model_position --x 0.5 --y -0.3
```

```json
{
    "ok": true
}
```

#### `get_model_position` — Get current position and zoom

```bash
./waifuland-ctl get_model_position
```

```json
{
    "ok": true,
    "x": 0.5,
    "y": -0.3,
    "zoom": 1.5
}
```

#### `toggle_hidden` — Toggle window visibility

```bash
./waifuland-ctl toggle_hidden
```

```json
{
    "ok": true,
    "hidden": true
}
```

#### `switch_skin` — Switch model skin/outfit

```bash
./waifuland-ctl switch_skin
```

```json
{
    "ok": true
}
```

#### `set_look` — Override look-at direction (for head/face tracking)

Per character (see [Targeting a character](#targeting-a-character)); `--reset` clears only the targeted character's override.

```bash
# Set look direction (x, y range: -1.0 to 1.0)
./waifuland-ctl set_look --x 0.5 --y 0.3

# Reset to default (follow cursor)
./waifuland-ctl set_look --reset
```

```json
{
    "ok": true
}
```

#### `list_characters` — List all currently shown characters

```bash
./waifuland-ctl list_characters
```

```json
{
    "ok": true,
    "characters": [
        {"id": 0, "model": "MyModel", "index": 0, "x": -0.3, "y": 0.0, "zoom": 1.0},
        {"id": 1, "model": "AnotherModel", "index": 1, "x": 0.3, "y": 0.0, "zoom": 1.0}
    ]
}
```

#### `add_character` — Add a character at runtime

```bash
# By name, auto-positioned in the row layout
./waifuland-ctl add_character --name "AnotherModel"

# By index, with an explicit position and zoom
./waifuland-ctl add_character --index 2 --x 0.6 --y 0.0 --scale 0.9
```

```json
{
    "ok": true,
    "character": 2
}
```

#### `remove_character` — Remove a character at runtime

```bash
./waifuland-ctl remove_character --character 2
```

```json
{
    "ok": true
}
```

### Raw Socket Usage

You can also communicate directly with the socket without `waifuland-ctl`:

```bash
# Using socat
echo '{"command":"get_status"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/waifuland.sock

# Using Python
python3 -c "
import socket, json
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('$XDG_RUNTIME_DIR/waifuland.sock')
sock.send(b'{\"command\":\"get_status\"}\n')
print(sock.recv(4096).decode())
sock.close()
"
```

## Model Setup

Place Live2D models in your models directory. Each model should be in its own subfolder containing a `.model3.json` file:

```
~/.config/waifuland/models/
├── MyModel/
│   ├── MyModel.model3.json
│   ├── MyModel.moc3
│   ├── textures/
│   └── motions/
└── AnotherModel/
    └── ...
```

The default models directory is `$XDG_CONFIG_HOME/waifuland/models/` (fallback: `~/.config/waifuland/models/`). Override it with the `--models_dir` flag.

## Architecture

```
waifuland/
├── src/                    # Application source code
│   ├── main.cpp            # Entry point, CLI argument parsing, initial roster
│   ├── LAppConfig.*        # JSON config file reader (singleton)
│   ├── LAppWayland.*       # Wayland client setup (display, compositor, EGL, layer-shell)
│   ├── LAppWaylandRegion.* # Input region management (one rect per character)
│   ├── LAppDelegate.*      # Main app controller, render loop, input handling
│   ├── LAppLive2DManager.* # Scene: character roster + per-character voice state, render loop
│   ├── LAppView.*          # Touch coordinate transforms (rendering lives in the manager)
│   ├── LAppModel.*         # Individual Live2D model instance (dumb renderer + animator)
│   ├── LAppIPC.*           # IPC socket server for external control
│   ├── LAppTextureManager.*# Shared texture cache (reference-counted by filename)
│   ├── JsonMini.hpp        # Shared string-aware parser for IPC/config flat JSON
│   └── LAppDefine.*        # Global constants and configuration
├── protocol/               # Wayland protocol XML files
│   ├── xdg-shell.xml
│   └── wlr-layer-shell-unstable-v1.xml
├── thirdParty/             # Third-party dependencies (GLEW, stb)
├── cmake/                  # CMake helper scripts
└── CMakeLists.txt          # Build configuration
```

### Key design decisions

- **No GLFW windowing** — Wayland surfaces are created directly via `wl_compositor` and `zwlr_layer_shell_v1` for overlay behavior that GLFW cannot provide.
- **EGL rendering** — OpenGL context is managed through EGL, bound directly to the Wayland display.
- **Layer-shell overlay** — the application renders as a Wayland layer surface, sitting above normal windows.
- **Input region masking** — one rectangle per character (derived from the same matrix state hit-testing uses) accepts input; the rest of the surface is fully transparent and click-through.

## Built With

- [Live2D Cubism SDK for Native](https://www.live2d.com/en/sdk/about/) — Live2D model rendering
- [GLEW](https://github.com/nigels-com/glew) — OpenGL extension loading
- [stb_image](https://github.com/nothings/stb) — Image loading
- [wlr-layer-shell](https://wayland.app/protocols/wlr-layer-shell-unstable-v1) — Wayland overlay protocol
- **Wayland** / **EGL** / **OpenGL** — Display and rendering stack

## License

This project includes code adapted from the Live2D Cubism SDK samples, which are subject to the [Live2D Open Software License](https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html).

The Live2D Cubism SDK (Core library) is proprietary and must be downloaded separately. See [Live2D SDK License](https://www.live2d.com/en/sdk/license/).
