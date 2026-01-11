# GTA:SA Mobile - Ambient Occlusion Shader Mod

Real-time depth-based AO injection for GTA San Andreas Mobile (v2.10).

## Features
- ✅ True screen-space ambient occlusion
- ✅ Adjustable intensity (0-10 scale)
- ✅ Runtime enable/disable
- ✅ Minimal performance impact
- ✅ No texture modifications required

## Installation

### Method 1: Automated (GitHub Actions)
1. Download latest release from [Actions tab](../../actions)
2. Extract `libao_inject.so` to `/data/data/com.rockstargames.gtasa/lib/`
3. Copy `ao_control.lua` to your mod loader scripts folder
4. Restart game

### Method 2: Manual Build
```bash
# Install Android NDK
# Clone this repo
git clone https://github.com/YOUR_USERNAME/gtasa-ao-shader
cd gtasa-ao-shader/jni
ndk-build

# Copy output
adb push ../libs/armeabi-v7a/libao_inject.so /data/data/com.rockstargames.gtasa/lib/
