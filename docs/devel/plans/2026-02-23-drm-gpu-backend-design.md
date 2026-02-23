# DRM + GPU (EGL/OpenGL ES) Backend Design

**Date:** 2026-02-23
**Status:** Approved

## Goal

Enable GPU-accelerated rendering on Raspberry Pi (3B+, 4, 5) and BTT CB1 via DRM + EGL + OpenGL ES, replacing the current fbdev CPU-only path as the default for Pi builds.

## Background

The DRM backend code exists (`display_backend_drm.cpp`) but was disabled in the systemd service (`HELIX_DISPLAY_BACKEND=fbdev`) due to crashes during atomic modesetting. The current DRM path uses dumb buffers (CPU rendering with vsync) — no GPU involvement.

LVGL 9.5 ships two DRM drivers:
- `lv_linux_drm.c` — dumb buffers, atomic modesetting, CPU rendering
- `lv_linux_drm_egl.c` — GBM surfaces, EGL context, OpenGL ES draw backend, legacy modesetting (`drmModeSetCrtc` + `drmModePageFlip`)

The EGL path is more robust (uses legacy modesetting, not atomic) and provides actual GPU acceleration.

## Target Hardware

| Device | SoC | GPU | DRM Driver | OpenGL ES |
|--------|-----|-----|------------|-----------|
| Pi 3B+ | BCM2837B0 | VideoCore IV | vc4-drm | ES 2.0 |
| Pi 4 | BCM2711 | VideoCore VI | vc4-kms-v3d | ES 3.1 |
| Pi 5 | BCM2712 | VideoCore VII | v3d + drm-rp1-dsi | ES 3.1 |
| BTT CB1 | Allwinner H616 | Mali-G31 | sun4i-drm + panfrost | ES 3.1 |

All targets have Mesa runtime libraries (libEGL, libGLESv2, libgbm) available via Debian packages.

## Architecture

### Runtime Fallback Chain

```
DRM + EGL/OpenGL ES  (GPU-accelerated)
        ↓ (EGL init fails)
DRM + dumb buffers   (CPU + vsync)
        ↓ (atomic modesetting fails)
fbdev                (CPU, direct /dev/fb0)
```

The binary is compiled with all three backends. `DisplayBackend::create_auto()` already handles DRM→fbdev fallback. The new work adds EGL→dumb buffer fallback inside `DisplayBackendDRM::create_display()`.

### Display Init Flow (DRM backend)

```
1. Auto-detect DRM device (/dev/dri/card*)
2. Try EGL path:
   a. lv_linux_drm_create()  [EGL variant]
   b. lv_linux_drm_set_file() → drm_device_init() → EGL context creation
   c. If success → GPU rendering active
3. If EGL fails, fall back to dumb buffers:
   a. lv_linux_drm_create()  [non-EGL variant]
   b. lv_linux_drm_set_file() → drm_setup() → atomic modesetting
   c. If success → CPU rendering with vsync
4. If both fail → return nullptr, create_auto() tries fbdev
```

### Key Difference: EGL vs Dumb Buffer DRM

The EGL path (`lv_linux_drm_egl.c`) uses **legacy modesetting** (`drmModeSetCrtc` + `drmModePageFlip`), NOT atomic modesetting. This is significant because:
- The previous crashes were in `drmModeAtomicCommit` (atomic path)
- Legacy modesetting is simpler and more widely supported
- The EGL path avoids the exact failure mode that caused us to disable DRM

## Changes Required

### 1. Docker Toolchain (Dockerfile.pi)

Add GPU development libraries:
```dockerfile
libgbm-dev:arm64 \
libgles2-mesa-dev:arm64 \
libegl1-mesa-dev:arm64 \
```

These are small packages (~2MB) and the linker only pulls in what's used.

### 2. Build System (Makefile + mk/cross.mk)

When `DISPLAY_BACKEND=drm` on Pi targets:
- Add `-DHELIX_ENABLE_OPENGLES` to CFLAGS/CXXFLAGS/SUBMODULE_*FLAGS
- Add `-lEGL -lGLESv2 -lgbm` to LDFLAGS
- Include GLAD headers: `-isystem lib/lvgl/src/drivers/opengles/glad/include`

New variable: `ENABLE_OPENGLES=yes` (set automatically for Pi DRM builds).

### 3. LVGL OpenGL ES Compilation Fix

`lv_opengles_shader.c` uses C++11 raw string literals (`R"(...)"`) which don't compile as C. Fix options:
- **Option A (preferred):** Compile the `drivers/opengles/` directory sources as C++ via a Makefile rule that uses `$(CXX)` for those specific object files
- **Option B:** Patch the shader file to use regular C string concatenation (invasive, breaks on LVGL updates)

Option A keeps LVGL unmodified and is a build-system-only change.

### 4. DisplayBackendDRM Changes

Add EGL/dumb buffer runtime selection:
```cpp
lv_display_t* DisplayBackendDRM::create_display(int width, int height) {
#ifdef HELIX_ENABLE_OPENGLES
    // Try EGL path first (GPU-accelerated)
    display_ = try_create_egl_display();
    if (display_) {
        spdlog::info("[DRM Backend] GPU-accelerated display via EGL");
        return display_;
    }
    spdlog::warn("[DRM Backend] EGL init failed, falling back to dumb buffers");
#endif
    // Dumb buffer path (CPU rendering with vsync)
    display_ = try_create_dumb_display();
    return display_;
}
```

### 5. lv_conf.h

Already correct — `HELIX_ENABLE_OPENGLES` gates:
- `LV_USE_LINUX_DRM_GBM_BUFFERS 1`
- `LV_LINUX_DRM_USE_EGL 1`
- `LV_USE_DRAW_OPENGLES 1`

### 6. Service File

Remove the hardcoded `HELIX_DISPLAY_BACKEND=fbdev` override. With the fallback chain, the binary auto-detects the best backend. Keep the env var documented for manual override.

### 7. Splash Screen

No changes needed. Splash uses fbdev and exits before main app init. The existing SIGUSR1 handoff works.

## Dependencies on Target

Runtime packages needed on Pi/BTT (most are pre-installed with desktop Raspberry Pi OS; headless may need):
```
libgbm1 libegl1 libgles2 mesa-common-dev
```

The installer script should check for these and install if missing.

## Testing Strategy

1. Build with `make pi-docker` (new toolchain with GPU libs)
2. Deploy to Pi 3 (192.168.1.39) — test EGL on VideoCore IV
3. Deploy to BTT CB1 (192.168.1.112) — test EGL on Mali-G31/panfrost
4. Verify fallback: set `HELIX_DRM_DEVICE=/dev/dri/card99` (nonexistent) → should fall to fbdev
5. Verify fallback: run on system without Mesa → should fall to dumb buffers → fbdev
6. Check for tearing, vsync behavior, rendering correctness

## Risks

- **Mesa version differences:** Bullseye (Mesa 20.3) vs Bookworm (Mesa 24.2). Building against Bullseye sysroot but running on Bookworm should work (EGL/GLES are ABI-stable), but Mesa 20.3 may lack some features.
- **Pi 3 VideoCore IV:** Oldest GPU, ES 2.0 only. LVGL's OpenGL ES driver should work but is less tested on ES 2.0.
- **BTT CB1 panfrost:** Open-source Mali driver, generally solid but less mature than vc4/v3d.

## Non-Goals

- NanoVG draw backend (LVGL has it, but OpenGL ES draw backend is simpler and sufficient)
- Vulkan (overkill for 2D UI)
- GPU-accelerated drawing for specific widgets (bed mesh heatmap, g-code viewer) — future work per PLATFORM_RENDERING_PLAN.md
