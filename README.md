# 🥽 Aperture Reality — Next-Gen 3DoF VR & Passthrough AR Spatial Browser for Android

<p align="center">
  <img src="https://img.shields.io/badge/Platform-Android%203DOF%20%26%20AR-success.svg?style=for-the-badge&logo=android" alt="Platform Android" />
  <img src="https://img.shields.io/badge/Graphics-OpenGL%20ES%203.0-orange.svg?style=for-the-badge&logo=opengl" alt="OpenGL ES 3.0" />
  <img src="https://img.shields.io/badge/Engine-GeckoView%20%7C%20Wolvic-blue.svg?style=for-the-badge&logo=firefox" alt="GeckoView" />
  <img src="https://img.shields.io/badge/Version-v0.3.1--beta-purple.svg?style=for-the-badge" alt="Version Beta" />
</p>

---

## 👨‍💻 Created & Developed by **Gerónimo ([@xxDMONxx](https://github.com/xxDMONxx))**

**Aperture Reality** transforms any standard Android smartphone into a high-performance **Spatial 3DoF VR & Passthrough AR Computing Environment** inside Google Cardboard, VRBox, or universal mobile VR headsets.

Built with a custom native C++/Java engine upon the open-source **Wolvic** foundation, Aperture Reality introduces live optical calibration, zero-copy passthrough augmented reality, smooth 360° rotational tracking, and ergonomic floating 3D spatial widgets.

---

## 📥 Official Download & Releases

Get the official compiled APK ready to install directly on your phone:

* 🚀 **[Download Latest Official APK (v0.3.1-beta)](https://github.com/xxDMONxx/aperture-reality/releases/tag/v0.3.1-beta)**
* 📦 **[All Release Builds](https://github.com/xxDMONxx/aperture-reality/releases)**

---

## 🌟 Key Innovations & Features

### 📷 1. Optical Lens-Calibrated Passthrough (AR Mode)
- **Live Widescreen Camera Feed**: Seamlessly toggles the phone's rear camera to display your physical room in real-time behind the browser.
- **Optical Lens Center Pre-Compensation**: Perfectly aligns the monocular camera feed with the physical optical lens centers of mobile VR headsets (`uShift = 0.5 - lensCenterEye`), eliminating vertical double vision and eye strain.
- **Hardware Zero-Copy Rendering**: Native `GL_TEXTURE_EXTERNAL_OES` pipeline operating at 60 FPS with zero CPU overhead.
- **Spatial 3D Windows in the Real World**: Web browsing windows, video players, and floating tools hover directly over your real desk or room with stereoscopic depth.

### 🧭 2. Direct 3DoF Gyroscope Tracking (Zero Jitter & Continuous 360°)
- **Direct Hardware-Fused Sensor**: Leverages `TYPE_GAME_ROTATION_VECTOR` at highest hardware sampling rates (200 Hz), completely eliminating compass magnetic twitches and secondary filter spikes.
- **Continuous 360° Smooth Rotation**: Mathematical 4D quaternion alignment ensuring seamless 360-degree continuous head rotation without boundary clamping or direction locking.
- **Instant 1:1 Response**: Zero latency, firm, and natural gaze aiming at tabs, buttons, and links.

### 🎬 3. Fullscreen Video Immersion
- **Auto-Hiding Controls**: 3D spatial widgets (Distance zoom and 3D / IPD panel) automatically fade out during fullscreen video playback for an uninterrupted cinematic experience.
- **Seamless Return**: Controls instantly reappear when exiting fullscreen mode.

### ⚙️ 4. Anchored 3D Spatial Controls (Aperture Panels)
- **Right 3D / IPD Panel** (`ApertureSideControlsWidget`):
  - Collapsible `⚙️ 3D / IPD` badge anchored to active browser windows.
  - Real-time **IPD (Interpupillary Distance)** slider (54 mm to 74 mm, default **60.2 mm**) to eliminate eye strain.
  - Instant **"🎯 Recentrar Vista"** yaw realign button.
  - **"📷 Passthrough (Cámara AR)"** one-tap toggle button.
- **Left Distance / Zoom Widget** (`ApertureLeftControlsWidget`):
  - Smooth spatial distance adjustment bringing screens from $-1.5\text{ m}$ up to $-6.0\text{ m}$.
  - Custom vector icons for sleek zoom controls.

### ⌨️ 5. Ergonomic Virtual Floating Keyboard
- **Tilted at $-30^\circ$**: Naturally oriented to match relaxed head gaze angles.
- **Forward-Floating Clearance**: Positioned with $+15\text{ cm}$ forward depth and $-12\text{ cm}$ vertical clearance to prevent any UI overlaps with the address bar.

---

## ☕ Support Aperture Reality

If you enjoy Aperture Reality and want to support the ongoing development, new features, and optimization of mobile spatial computing:

* 💖 **Star the Project**: Give this repository a ⭐ on GitHub!
* 💬 **Feedback & Testing**: Share your ideas, bug reports, and headset configs in [GitHub Issues](https://github.com/xxDMONxx/aperture-reality/issues).
* ☕ **Donations & Sponsorship**: Voluntary contributions help fund test devices, optical hardware, and dedicated development time. (Contact via GitHub profile / Sponsor button).

---

## 📜 Distribution & License Notice

- **Aperture Reality** incorporates components from the open-source **Wolvic** project (developed by [Igalia](https://igalia.com) and the Mozilla Foundation), licensed under the **Mozilla Public License (MPL) 2.0**.
- The `v0.1.0-beta` baseline release remains available under its respective open-source terms.
- **Proprietary Enhancements & Custom Development**: All original features, optical shaders, passthrough pipelines, spatial widgets, and UI adaptations developed specifically for Aperture Reality by **Gerónimo** are distributed via official binary releases. Unauthorized redistribution, removal of developer attribution, or rebranding without explicit permission is strictly prohibited.
- For integration inquiries, collaborations, or custom headset profile partnerships, please reach out via the official repository.

---

<p align="center">
  <b>Aperture Reality</b> — Spatial Computing for Everyone. Crafted with passion by <b>Gerónimo</b>.
</p>
